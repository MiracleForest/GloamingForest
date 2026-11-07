// Contract "features" (F5): the interpreter of the imported worldgen JSON. Configured features
// (Feature#place with ensureCanWrite), placed features (PlacedFeature#placeWithContext: the
// placement modifiers chained depth first, as Java's lazy streams run them), the registries units
// add their types to, the imported data and LargeWriter.
#include "feature/features/Features.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "core/jrand/Mth.h"
#include "feature/features/Internal.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

#include "feature/features/VanillaFeatures.inc"

namespace gf::features {

using detail::field;
using detail::fieldArr;
using detail::fieldObj;

using detail::normalizeId;
using detail::warnOnce;
using Json = nlohmann::json;

// ==== registries =====================================================================================

namespace detail {

std::shared_mutex& registriesMutex() {
    static std::shared_mutex m;
    return m;
}
Registries& registries() {
    static Registries r;
    return r;
}

std::int64_t packPos(BlockPos const& pos) { return core::mth::blockPosAsLong(pos.x, pos.y, pos.z); }

} // namespace detail

namespace {
template <class Map, class Fn>
void put(Map detail::Registries::*member, std::string const& id, Fn fn) {
    std::unique_lock lock(detail::registriesMutex());
    (detail::registries().*member)[normalizeId(id)] = std::move(fn);
}
} // namespace

void registerType(std::string const& typeId, FeatureFn fn) { put(&detail::Registries::types, typeId, std::move(fn)); }
void registerPlacement(std::string const& typeId, PlacementFn fn) { put(&detail::Registries::placements, typeId, std::move(fn)); }
void registerTrunkPlacer(std::string const& typeId, TrunkPlacerFn fn) { put(&detail::Registries::trunkPlacers, typeId, std::move(fn)); }
void registerFoliagePlacer(std::string const& typeId, FoliagePlacerFn fn) { put(&detail::Registries::foliagePlacers, typeId, std::move(fn)); }
void registerTreeDecorator(std::string const& typeId, TreeDecoratorFn fn) { put(&detail::Registries::decorators, typeId, std::move(fn)); }
void registerRootPlacer(std::string const& typeId, RootPlacerFn fn) { put(&detail::Registries::rootPlacers, typeId, std::move(fn)); }
void registerStateProvider(std::string const& typeId, StateProviderFn fn) { put(&detail::Registries::stateProviders, typeId, std::move(fn)); }

// ==== data ===========================================================================================

namespace detail {

Json const& vanillaFeatures() {
    static Json const j = [] {
        auto out = Json::parse(kVanillaFeatures);
        // a named object: a range-for over a temporary's items() would read a destroyed Json (the
        // proxy is extended, the parsed object is not)
        auto const extra = Json::parse(kVanillaFeaturesExtra);
        for (auto const& [id, feature] : extra.items()) out["configured"][id] = feature;
        return out;
    }();
    return j;
}

namespace {

std::mutex&                 dataMutex() {
    static std::mutex m;
    return m;
}
std::shared_ptr<Data const>& dataPtr() {
    static std::shared_ptr<Data const> d;
    return d;
}

std::shared_ptr<Data const> loadData() {
    static std::atomic<std::uint64_t> generations{0};
    auto        d    = std::make_shared<Data>();
    d->generation    = ++generations;
    auto const& v    = vanillaFeatures();
    for (auto const& [id, j] : v["configured"].items()) d->configured[id] = j;
    for (auto const& [id, j] : v["placed"].items()) d->placed[id] = j;
    std::filesystem::path root;
    try {
        root = Mod::getInstance().getSelf().getModDir() / "data";
    } catch (...) {
        return d;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return d;
    int files = 0;
    for (auto const& ns : std::filesystem::directory_iterator(root, ec)) {
        if (!ns.is_directory()) continue;
        auto nsName = ns.path().filename().string();
        struct Kind {
            char const*                  dir;
            std::map<std::string, Json>* out;
        } kinds[] = {
            {"configured_feature", &d->configured},
            {"placed_feature", &d->placed},
            {"biome", &d->biomes},
            {"configured_carver", &d->carvers},
            {"structure", &d->structures},
        };
        for (auto& k : kinds) {
            auto dir = ns.path() / "worldgen" / k.dir;
            if (!std::filesystem::is_directory(dir, ec)) continue;
            for (auto const& f : std::filesystem::recursive_directory_iterator(dir, ec)) {
                if (!f.is_regular_file() || f.path().extension() != ".json") continue;
                auto rel = std::filesystem::relative(f.path(), dir, ec).generic_string();
                rel      = rel.substr(0, rel.size() - 5);
                try {
                    std::ifstream in(f.path());
                    (*k.out)[nsName + ":" + rel] = Json::parse(in);
                    ++files;
                } catch (std::exception const& e) {
                    gf::log().error("worldgen: cannot read {}: {}", f.path().generic_string(), e.what());
                }
            }
        }
    }
    // the Java class chains of TF's blocks (the survival rules of kit::canSurvive)
    try {
        std::ifstream in(root / "gf" / "import" / "skeleton.json");
        if (in) {
            auto skeleton = Json::parse(in);
            for (auto const& [id, chain] : fieldObj(skeleton, "classes").items()) {
                auto& out = d->blockClasses[id];
                for (auto const& cls : chain)
                    if (cls.is_string()) out.push_back(cls.get<std::string>());
            }
        }
    } catch (std::exception const& e) {
        gf::log().error("worldgen: cannot read data/gf/import/skeleton.json: {}", e.what());
    }
    for (auto const& [id, biome] : d->biomes) {
        auto& set   = d->biomeFeatureSets[id];
        auto& steps = d->biomeSteps[id];
        for (auto const& step : fieldArr(biome, "features")) {
            auto& list = steps.emplace_back();
            for (auto const& f : step) {
                if (!f.is_string()) continue;
                list.push_back(normalizeId(f.get<std::string>()));
                set.insert(list.back());
            }
        }
    }
    gf::log().info(
        "worldgen data: {} configured features, {} placed features, {} biomes, {} carvers, {} structures ({} files)",
        d->configured.size(),
        d->placed.size(),
        d->biomes.size(),
        d->carvers.size(),
        d->structures.size(),
        files
    );
    return d;
}

} // namespace

namespace {
/// Lock-free read path once loaded (every placement asks); replaced data is kept alive so that
/// references handed out earlier stay valid.
std::atomic<Data const*>&                gData() {
    static std::atomic<Data const*> d{nullptr};
    return d;
}
std::vector<std::shared_ptr<Data const>>& retired() {
    static std::vector<std::shared_ptr<Data const>> r;
    return r;
}
} // namespace

Data const& data() {
    if (auto const* d = gData().load(std::memory_order_acquire)) return *d;
    std::lock_guard lock(dataMutex());
    if (!dataPtr()) {
        dataPtr() = loadData();
        gData().store(dataPtr().get(), std::memory_order_release);
    }
    return *dataPtr();
}

void reloadData() {
    auto            d = loadData();
    std::lock_guard lock(dataMutex());
    if (dataPtr()) retired().push_back(dataPtr());
    dataPtr() = std::move(d);
    gData().store(dataPtr().get(), std::memory_order_release);
}

bool biomeHasFeature(std::string const& biomeId, std::string const& placedId) {
    auto const& sets = data().biomeFeatureSets;
    auto        it   = sets.find(biomeId);
    if (it == sets.end()) return true; // a biome without imported JSON does not restrict features
    return it->second.contains(placedId);
}

// ==== placement ======================================================================================

bool placeConfiguredObject(Json const& configured, std::string const& id, Region& region, BlockPos const& pos, RandomSource& random) {
    if (!configured.is_object()) return false;
    if (!region.isInside(pos.x, pos.z)) return false; // Feature#place: level.ensureCanWrite(origin)
    auto      type = normalizeId(configured.value("type", std::string()));
    FeatureFn fn   = withRegistries([&](Registries const& r) -> FeatureFn {
        auto it = r.types.find(type);
        return it == r.types.end() ? FeatureFn{} : it->second;
    });
    if (!fn) {
        warnOnce("feature_type " + type, "unknown feature type " + type + (id.empty() ? "" : " (" + id + ")"));
        return false;
    }
    static Json const empty = Json::object();
    FeatureContext    ctx{region, pos, random, configured.contains("config") ? configured["config"] : empty, id};
    return fn(ctx);
}

namespace {

struct PlacedRun {
    Region&                         region;
    RandomSource&                   random;
    std::string const&              topFeature;
    std::vector<Json const*>        modifiers;
    std::vector<PlacementFn>        fns;
    Json const*                     configured = nullptr;
    std::string                     configuredId;
    bool                            placed = false;

    void run(std::size_t i, BlockPos const& pos) {
        if (i == modifiers.size()) {
            if (placeConfiguredObject(*configured, configuredId, region, pos, random)) placed = true;
            return;
        }
        PlacementContext ctx{region, random, *modifiers[i], topFeature};
        for (auto const& p : fns[i](ctx, pos)) run(i + 1, p);
    }
};

} // namespace

bool placePlacedObject(Json const& placed, std::string const& topFeature, Region& region, BlockPos const& pos, RandomSource& random) {
    if (!placed.is_object()) return false;
    PlacedRun run{region, random, topFeature, {}, {}};
    auto const& feature = field(placed, "feature");
    if (feature.is_string()) {
        run.configuredId = normalizeId(feature.get<std::string>());
        auto const& cf   = data().configured;
        auto        it   = cf.find(run.configuredId);
        if (it == cf.end()) {
            warnOnce("configured " + run.configuredId, "unknown configured feature " + run.configuredId);
            return false;
        }
        run.configured = &it->second;
    } else {
        run.configured = &feature;
    }
    for (auto const& m : fieldArr(placed, "placement")) {
        auto        type = normalizeId(m.value("type", std::string()));
        PlacementFn fn   = withRegistries([&](Registries const& r) -> PlacementFn {
            auto it = r.placements.find(type);
            return it == r.placements.end() ? PlacementFn{} : it->second;
        });
        if (!fn) {
            warnOnce("placement " + type, "unknown placement modifier " + type + " (" + topFeature + "): nothing placed");
            return false;
        }
        run.modifiers.push_back(&m);
        run.fns.push_back(std::move(fn));
    }
    run.run(0, pos);
    return run.placed;
}

} // namespace detail

// ==== contract ========================================================================================

bool place(std::string const& idIn, Region& region, BlockPos const& pos, RandomSource& random) {
    auto        id   = normalizeId(idIn);
    auto const& data = detail::data();
    if (auto it = data.configured.find(id); it != data.configured.end())
        return detail::placeConfiguredObject(it->second, id, region, pos, random);
    if (auto it = data.placed.find(id); it != data.placed.end()) return detail::placePlacedObject(it->second, id, region, pos, random);
    warnOnce("feature " + id, "unknown feature " + id);
    return false;
}

bool placeJson(Json const& feature, Region& region, BlockPos const& pos, RandomSource& random) {
    if (feature.contains("placement") || (feature.contains("feature") && !feature.contains("type")))
        return detail::placePlacedObject(feature, "", region, pos, random);
    return detail::placeConfiguredObject(feature, "", region, pos, random);
}

namespace kit {

bool placeConfigured(Json const& holder, Region& region, BlockPos const& pos, RandomSource& random) {
    if (holder.is_string()) {
        auto id = normalizeId(holder.get<std::string>());
        if (auto const* j = configuredJson(id)) return detail::placeConfiguredObject(*j, id, region, pos, random);
        warnOnce("configured " + id, "unknown configured feature " + id);
        return false;
    }
    return detail::placeConfiguredObject(holder, "", region, pos, random);
}

bool placePlaced(Json const& holder, Region& region, BlockPos const& pos, RandomSource& random) {
    if (holder.is_string()) {
        auto id = normalizeId(holder.get<std::string>());
        // PlacedFeature#place (no biome check): the placed feature id is not a "top feature"
        if (auto const* j = placedJson(id)) return detail::placePlacedObject(*j, "", region, pos, random);
        warnOnce("placed " + id, "unknown placed feature " + id);
        return false;
    }
    return detail::placePlacedObject(holder, "", region, pos, random);
}

Json const* configuredJson(std::string const& id) {
    auto const& m  = detail::data().configured;
    auto        it = m.find(normalizeId(id));
    return it == m.end() ? nullptr : &it->second;
}

Json const* placedJson(std::string const& id) {
    auto const& m  = detail::data().placed;
    auto        it = m.find(normalizeId(id));
    return it == m.end() ? nullptr : &it->second;
}

BlockSource* blockSource(Region& region) {
    if (auto* s = dynamic_cast<detail::SourceRegion*>(&region)) return s->source();
    return nullptr;
}

} // namespace kit

// ==== LargeWriter ====================================================================================

namespace detail {

struct Pending {
    std::map<int, std::map<std::int64_t, std::vector<std::pair<BlockPos, Block const*>>>> byDim; ///< dim -> chunk -> blocks
    std::size_t                                                                           count = 0;
    bool                                                                                  dirty = false;
};

std::mutex& pendingMutex() {
    static std::mutex m;
    return m;
}
Pending& pending() {
    static Pending p;
    return p;
}

/// Takes the queued blocks of chunk (cx, cz) in `dimension`.
std::vector<std::pair<BlockPos, Block const*>> takePending(int dimension, int cx, int cz) {
    std::lock_guard lock(pendingMutex());
    auto&           p  = pending();
    auto            dt = p.byDim.find(dimension);
    if (dt == p.byDim.end()) return {};
    auto it = dt->second.find(core::mth::chunkPosAsLong(cx, cz));
    if (it == dt->second.end()) return {};
    auto out = std::move(it->second);
    dt->second.erase(it);
    p.count -= out.size();
    p.dirty = true;
    return out;
}

} // namespace detail

void LargeWriter::setBlock(int dimension, BlockPos const& pos, Block const& block) {
    std::lock_guard lock(detail::pendingMutex());
    auto&           p = detail::pending();
    p.byDim[dimension][core::mth::chunkPosAsLong(pos.x >> 4, pos.z >> 4)].emplace_back(pos, &block);
    ++p.count;
    p.dirty = true;
}

std::size_t LargeWriter::pending(int dimension) {
    std::lock_guard lock(detail::pendingMutex());
    auto&           p  = detail::pending();
    auto            it = p.byDim.find(dimension);
    if (it == p.byDim.end()) return 0;
    std::size_t n = 0;
    for (auto const& [_, v] : it->second) n += v.size();
    return n;
}

namespace detail {

/// Applies the queued blocks of the center chunk of `region` (the generator calls it after the
/// chunk's features). Returns how many were set.
int drainLargeWriter(int dimension, Region& region) {
    auto blocks = takePending(dimension, region.centerChunkX(), region.centerChunkZ());
    for (auto const& [pos, block] : blocks) region.setBlock(pos, *block, 2);
    return static_cast<int>(blocks.size());
}

namespace {

/// Server thread: applies queued blocks of chunks that are already loaded and decorated (their
/// generation is over, so they would never drain otherwise).
void drainLoaded() {
    std::vector<std::tuple<int, int, int>> ready;
    {
        std::lock_guard lock(pendingMutex());
        if (pending().count == 0) return;
        for (auto const& [dim, chunks] : pending().byDim) {
            auto* region = game::regionOf(dim);
            if (!region) continue;
            for (auto const& [key, _] : chunks) {
                int  cx = core::mth::chunkPosX(key), cz = core::mth::chunkPosZ(key);
                auto* lc = region->getChunk(cx, cz);
                if (lc && static_cast<int>(lc->mLoadState->load()) >= static_cast<int>(ChunkState::Loaded)) ready.emplace_back(dim, cx, cz);
            }
        }
    }
    for (auto [dim, cx, cz] : ready) {
        auto* region = game::regionOf(dim);
        if (!region) continue;
        for (auto const& [pos, block] : takePending(dim, cx, cz)) game::setBlock(*region, pos, *block);
    }
}

constexpr char kPendingKey[] = "gf:worldgen:large_writer";

void savePending() {
    Json out = Json::object();
    {
        std::lock_guard lock(pendingMutex());
        if (!pending().dirty) return;
        pending().dirty = false;
        for (auto const& [dim, chunks] : pending().byDim) {
            Json list = Json::array();
            for (auto const& [_, blocks] : chunks)
                for (auto const& [pos, block] : blocks) list.push_back({pos.x, pos.y, pos.z, blockToJson(*block)});
            if (!list.empty()) out[std::to_string(dim)] = std::move(list);
        }
    }
    if (out.empty()) game::db::erase(kPendingKey);
    else game::db::put(kPendingKey, out);
}

void loadPending() {
    std::lock_guard lock(pendingMutex());
    pending() = {};
    auto j    = game::db::get(kPendingKey);
    if (!j || !j->is_object()) return;
    for (auto const& [dim, list] : j->items()) {
        int d = std::stoi(dim);
        for (auto const& e : list) {
            if (!e.is_array() || e.size() != 4) continue;
            auto const* b = blockFromJson(e[3]);
            if (!b) continue;
            BlockPos pos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>()};
            pending().byDim[d][core::mth::chunkPosAsLong(pos.x >> 4, pos.z >> 4)].emplace_back(pos, b);
            ++pending().count;
        }
    }
    if (pending().count) gf::log().info("worldgen: {} LargeWriter blocks wait for their chunks", pending().count);
}

} // namespace

} // namespace detail

// ==== module =========================================================================================

class FeaturesModule {
public:
    void install() {
        detail::registerVanillaProviders();
        detail::registerVanillaPlacements();
        detail::registerVanillaFeatures();
        detail::registerVanillaTrees();
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldReady([] {
                detail::loadPending();
                (void)detail::data();
            });
            game::db::onWorldSave([] { detail::savePending(); });
            game::db::onWorldClosed([] {
                std::lock_guard lock(detail::pendingMutex());
                detail::pending() = {};
            });
        }
        if (game::db::available()) detail::loadPending();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            if (++mTicks % 20 == 0) detail::drainLoaded();
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        detail::savePending();
    }

private:
    ll::event::ListenerPtr mTick;
    int                    mTicks = 0;
};

} // namespace gf::features

GF_REGISTER_MODULE_ORDERED(gf::features::FeaturesModule, gf::ModuleOrder::Contracts);

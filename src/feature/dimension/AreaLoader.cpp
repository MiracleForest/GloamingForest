// Area loading of unit F4 (feature/dimension/AreaLoader.h): transient engine ticking areas.

#include "feature/dimension/AreaLoader.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/platform/UUID.h"
#include "mc/util/Bounds.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/ticking/ITickingArea.h"
#include "mc/world/level/ticking/ITickingAreaView.h"
#include "mc/world/level/ticking/PendingArea.h"
#include "mc/world/level/ticking/TickingAreaList.h"
#include "mc/world/level/ticking/TickingAreasManager.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::dimension {

namespace {

/// The scope the areas are registered under (a fixed key, "gf:dimen").
constexpr std::uint64_t kScopeKey = 0x67663a64696d656eULL;

std::atomic<int>  gGeneration{0}; ///< bumped when a world closes: holds of a closed world do nothing
std::atomic<int>  gHeld{0};
std::atomic<int>  gSettling{0};
std::atomic<int>  gCounter{0};
std::atomic<bool> gLogged{false};
std::atomic<int>  gStranded{0};

/// A released area that is still loading is kept until it settled, with no time cap: after
/// kSettleCap it goes once none of its chunks (or their neighbours) is halfway through the
/// engine's pipeline any more, else it stays until it settles or the world closes.
constexpr int kSettleCap   = 6000;
constexpr int kSettleCheck = 200;
/// A chunk that sits in the same waiting state for this long is taken to be stranded (the engine
/// lost the request that would move it on).
constexpr int kStuckTicks = 100;

/// The engine's active ticking area `uuid` (nullptr: not active, pending or gone).
ITickingArea* activeArea(Level& level, mce::UUID const& uuid) {
    for (auto const& [type, list] : *level.getTickingAreasMgr().mActiveAreas) {
        if (!list) continue;
        for (auto const& a : *list->mTickingAreas)
            if (a && a->getId() == uuid) return a.get();
    }
    return nullptr;
}

bool pendingArea(Level& level, mce::UUID const& uuid) {
    for (auto const& [type, pending] : *level.getTickingAreasMgr().mPendingAreas)
        for (auto const& p : pending)
            if (*p.mUID == uuid) return true;
    return false;
}

/// Whether the area's own loading is over: its view loaded every chunk it asked for (or the area
/// is gone). Until then chunks of it may be halfway through the engine's generation pipeline.
bool settled(Level& level, mce::UUID const& uuid) {
    if (auto* a = activeArea(level, uuid)) return a->isDoneLoadingScoped() || a->getView().isDoneLoading();
    return !pendingArea(level, uuid);
}

/// Chunk bounds (inclusive) of an area.
struct ChunkBox {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    [[nodiscard]] ChunkBox grown(int n) const { return {x0 - n, z0 - n, x1 + n, z1 + n}; }
};

ChunkBox boxOf(BlockPos const& center, int radius) {
    return {(center.x - radius) >> 4, (center.z - radius) >> 4, (center.x + radius) >> 4, (center.z + radius) >> 4};
}

/// The load state of a chunk (-1: the engine has no such chunk).
int chunkState(BlockSource& region, int cx, int cz) {
    auto chunk = region.getChunkSource().getExistingChunk(ChunkPos{cx, cz});
    return chunk ? static_cast<int>(chunk->mLoadState->load()) : -1;
}

bool inPipeline(int s) {
    return s >= static_cast<int>(ChunkState::Generating) && s <= static_cast<int>(ChunkState::LightingFinished);
}

/// Whether some chunk of the box or its ring is halfway through the pipeline (states 1..10).
bool anyInPipeline(int dim, ChunkBox const& box) {
    auto* region = game::regionOf(dim);
    if (!region) return false;
    auto ring = box.grown(1);
    for (int cz = ring.z0; cz <= ring.z1; ++cz)
        for (int cx = ring.x0; cx <= ring.x1; ++cx)
            if (inPipeline(chunkState(*region, cx, cz))) return true;
    return false;
}

/// The states at which a chunk waits for the engine to start its next step. The states in between
/// are the steps themselves, running in a task: a chunk in one of them is never queued again.
bool waitingState(int s) {
    switch (static_cast<ChunkState>(s)) {
    case ChunkState::Generated:
    case ChunkState::StructurePostProcessed:
    case ChunkState::DecorationPostProcessed:
    case ChunkState::NeedsLighting:
    case ChunkState::LightingFinished:
        return true;
    default:
        return false;
    }
}

/// Watches the chunks of a box and its ring for ones that stopped moving through the pipeline.
class StuckWatch {
public:
    explicit StuckWatch(ChunkBox box) : mBox(box) {}
    /// One tick: the chunks (with their state) that sat in one waiting state for kStuckTicks.
    std::vector<std::pair<ChunkPos, int>> observe(int dim) {
        std::vector<std::pair<ChunkPos, int>> out;
        auto*                                 region = game::regionOf(dim);
        if (!region) return out;
        auto ring = mBox.grown(1);
        for (int cz = ring.z0; cz <= ring.z1; ++cz)
            for (int cx = ring.x0; cx <= ring.x1; ++cx) {
                auto key = std::make_pair(cx, cz);
                int  s   = chunkState(*region, cx, cz);
                if (!inPipeline(s)) {
                    mSeen.erase(key);
                    continue;
                }
                auto [it, fresh] = mSeen.try_emplace(key, s, 0);
                if (!fresh && it->second.first == s) ++it->second.second;
                else it->second = {s, 0};
                if (it->second.second >= kStuckTicks && waitingState(s)) out.emplace_back(ChunkPos{cx, cz}, s);
            }
        return out;
    }
    void reset() { mSeen.clear(); }

private:
    ChunkBox                                           mBox;
    std::map<std::pair<int, int>, std::pair<int, int>> mSeen; ///< chunk -> state, ticks in it
};

std::string chunksText(std::vector<std::pair<ChunkPos, int>> const& chunks) {
    std::string out;
    for (auto const& [cp, s] : chunks) out += std::format("{}{} {} ({})", out.empty() ? "" : ", ", cp.x, cp.z, s);
    return out;
}

/// A transient ticking area of the engine registered under F4's scope.
mce::UUID addArea(Level& level, int dim, std::string const& name, ChunkBox const& box) {
    Bounds b{};
    b.mMin->x = box.x0;
    b.mMin->y = 0;
    b.mMin->z = box.z0;
    b.mMax->x = box.x1;
    b.mMax->y = 0;
    b.mMax->z = box.z1;
    b.mDim->x = box.x1 - box.x0 + 1;
    b.mDim->y = 1;
    b.mDim->z = box.z1 - box.z0 + 1;
    b.mArea   = b.mDim->x * b.mDim->z;
    b.mVolume = b.mArea;
    b.mSide   = 16;
    auto uuid = mce::UUID::random();
    auto ctx  = level.getTickingAreasMgr().addTransientScopedArea(DimensionType(dim), name, kScopeKey, uuid, b);
    if (!gLogged.exchange(true)) {
        Bounds const& got = ctx.mBounds;
        log().info(
            "dimension: transient ticking area {} in dimension {}: chunks {} {} .. {} {} (engine: {} {} .. {} {}, active {})",
            name,
            dim,
            box.x0,
            box.z0,
            box.x1,
            box.z1,
            got.mMin->x,
            got.mMin->z,
            got.mMax->x,
            got.mMax->z,
            static_cast<bool>(ctx.mIsActive)
        );
    }
    return uuid;
}

/// Removes area `uuid` once its loading is over. A ticking area removed while its chunks are still
/// in the engine's generation pipeline strands them: the neighbours the area had asked for are
/// dropped, and a chunk that was waiting for them (lighting finished, not yet loaded) never gets
/// further, even for a new area asked for over it (what made gf::world::teleport miss its
/// destination: dimension.area_handover).
void removeWhenSettled(mce::UUID uuid, std::string name, int dim, ChunkBox box, int generation) {
    ++gSettling;
    ll::coro::keepThis([uuid, name = std::move(name), dim, box, generation]() -> ll::coro::CoroTask<> {
        int  waited = 0;
        bool kept   = false;
        for (;; ++waited) {
            auto level = ll::service::getLevel();
            if (generation != gGeneration.load() || !level) break;
            if (settled(*level, uuid)) break;
            if (waited >= kSettleCap && waited % kSettleCheck == 0) {
                if (!anyInPipeline(dim, box)) {
                    log().warn(
                        "dimension: the released area {} never reported its loading done ({} ticks), though none of its chunks is loading any more; removed",
                        name,
                        waited
                    );
                    break;
                }
                if (!kept)
                    log().warn("dimension: the released area {} is still loading after {} ticks; kept until it settles or the world closes", name, waited);
                kept = true;
            }
            co_await ll::chrono::ticks(1);
        }
        if (auto level = ll::service::getLevel(); level && generation == gGeneration.load()) {
            if (waited >= 20 && waited < kSettleCap)
                log().info("dimension: the released area {} finished its loading {} ticks after its release", name, waited);
            level->getTickingAreasMgr().removeScopedAreas({uuid});
        }
        --gSettling;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

class TransientArea final : public AreaHold {
public:
    TransientArea(int dim, mce::UUID uuid, std::string name, ChunkBox box)
    : mDim(dim),
      mUuid(uuid),
      mName(std::move(name)),
      mBox(box),
      mGeneration(gGeneration.load()) {
        ++gHeld;
    }
    TransientArea(TransientArea const&)            = delete;
    TransientArea& operator=(TransientArea const&) = delete;
    ~TransientArea() override {
        --gHeld;
        if (mGeneration != gGeneration.load()) return; // the world (and its areas) are gone
        auto level = ll::service::getLevel();
        if (!level) return;
        if (!settled(*level, mUuid)) {
            removeWhenSettled(mUuid, std::move(mName), mDim, mBox, mGeneration);
            return;
        }
        level->getTickingAreasMgr().removeScopedAreas({mUuid});
    }
    [[nodiscard]] mce::UUID const&   uuid() const { return mUuid; }
    [[nodiscard]] std::string const& name() const { return mName; }
    [[nodiscard]] ChunkBox const&    box() const { return mBox; }

private:
    int         mDim;
    mce::UUID   mUuid;
    std::string mName;
    ChunkBox    mBox;
    int         mGeneration;
};

std::shared_ptr<TransientArea> newArea(Level& level, int dim, ChunkBox const& box) {
    auto name = "gf_dimension_" + std::to_string(++gCounter);
    auto uuid = addArea(level, dim, name, box);
    return std::make_shared<TransientArea>(dim, uuid, std::move(name), box);
}

/// The areas held now, by dimension and chunk bounds: a request for the same area shares it.
std::map<std::tuple<int, int, int, int, int>, std::weak_ptr<TransientArea>>& shared() {
    static std::map<std::tuple<int, int, int, int, int>, std::weak_ptr<TransientArea>> m;
    return m;
}

bool loaded(int dim, BlockPos const& center, int radius) {
    auto* region = game::regionOf(dim);
    if (!region) return false;
    BlockPos p{center.x, std::clamp(center.y, static_cast<int>(region->getMinHeight()), region->getMaxHeight() - 1), center.z};
    return region->hasChunksAt(p, radius, false);
}

std::string boundsText(Bounds const& b) { return std::format("{} {}..{} {}", b.mMin->x, b.mMin->z, b.mMax->x, b.mMax->z); }

} // namespace

std::string loadReport(int dim, BlockPos center, int radius, mce::UUID const* area) {
    auto level = ll::service::getLevel();
    if (!level) return "no level";
    auto&       mgr = level->getTickingAreasMgr();
    std::string out = std::format("standalone areas {}", mgr.countStandaloneTickingAreas());
    bool        ours = false;
    for (auto const& [type, list] : *mgr.mActiveAreas) {
        if (!list) continue;
        int         scoped = 0, standalone = 0, entity = 0, n = 0;
        std::string names;
        for (auto const& a : *list->mTickingAreas) {
            if (!a) continue;
            ++n;
            if (a->isScoped()) ++scoped;
            if (a->isStandalone()) ++standalone;
            if (a->isEntityOwned()) ++entity;
            bool mine = area && a->getId() == *area;
            if (mine) ours = true;
            if (!a->isEntityOwned()) {
                names += std::format(
                    "{}{}{}[{}{}{}{}]",
                    names.empty() ? "" : ", ",
                    mine ? "*" : "",
                    a->getName(),
                    boundsText(a->getBoundsCopy()),
                    a->isScoped() ? (a->isDoneLoadingScoped() ? " scoped done" : " scoped loading") : "",
                    a->getView().isDoneLoading() ? " view done" : " view loading",
                    a->isAlwaysActive() ? "" : " not always active"
                );
            }
        }
        out += std::format(
            "; dim {} active {} ({} scoped, {} standalone, {} entity): {}",
            type.mValue,
            n,
            scoped,
            standalone,
            entity,
            names
        );
    }
    for (auto const& [type, pending] : *mgr.mPendingAreas) {
        if (pending.empty()) continue;
        std::string names;
        for (auto const& p : pending) {
            bool mine = area && *p.mUID == *area;
            names += std::format(
                "{}{}{}[{}{}{}]",
                names.empty() ? "" : ", ",
                mine ? "*" : "",
                *p.mName,
                boundsText(*p.mBounds),
                p.mCreated ? " created" : "",
                p.mScope->has_value() ? " scoped" : ""
            );
        }
        out += std::format("; dim {} pending {}: {}", type.mValue, pending.size(), names);
    }
    if (area) out += ours ? "; this area is active" : "; this area is not active";
    // the chunks the area needs and how far each got
    if (auto* region = game::regionOf(dim)) {
        std::string chunks;
        auto        box = boxOf(center, radius);
        // and the ring of neighbours around them, in parentheses
        for (int cz = box.z0 - 1; cz <= box.z1 + 1; ++cz) {
            chunks += std::format("{}z {}:", chunks.empty() ? "" : " |", cz);
            for (int cx = box.x0 - 1; cx <= box.x1 + 1; ++cx) {
                bool ring  = cx < box.x0 || cx > box.x1 || cz < box.z0 || cz > box.z1;
                int  state = chunkState(*region, cx, cz);
                bool here  = region->hasChunksAt(BlockPos{cx * 16 + 8, center.y, cz * 16 + 8}, 0, false);
                chunks += std::format(" {}{}{}{}", ring ? "(" : "", state, here ? "" : "?", ring ? ")" : "");
            }
        }
        out += std::format("; chunk states from x {} (11 loaded, -1 none, ? not in the region): {}", box.x0 - 1, chunks);
    } else {
        out += "; the dimension has no region";
    }
    return out;
}

namespace {

/// The engine's state of one area that waits for its chunks, at most a few times per wait.
void reportSlow(int dim, BlockPos const& center, int radius, TransientArea const& area, int ticks) {
    log().info(
        "dimension: the area {} at {} {} {} of dimension {} is still loading after {} ticks: {}",
        area.name(),
        center.x,
        center.y,
        center.z,
        dim,
        ticks,
        loadReport(dim, center, radius, &area.uuid())
    );
}

/// Waits until the chunks within `radius` of `center` are loaded, at most `timeoutTicks`, while
/// `area` holds them. Chunks of the area (or its ring) that sit in one waiting state for
/// kStuckTicks were stranded by an area removed while they were in the pipeline (F4's own are
/// kept until they settle, so someone else's): that is logged as a warning once per wait, with
/// the chunks. The engine offers no way on the server to move them on (docs/spikes/S1.md).
ll::coro::CoroTask<bool> waitLoaded(int dim, BlockPos center, int radius, int timeoutTicks, std::shared_ptr<TransientArea> area) {
    StuckWatch watch{area->box()};
    bool       told = false;
    for (int i = 0; i <= timeoutTicks; ++i) {
        if (loaded(dim, center, radius)) {
            if (i >= 50) log().info("dimension: the area {} at {} {} {} of dimension {} loaded after {} ticks", area->name(), center.x, center.y, center.z, dim, i);
            co_return true;
        }
        if (i > 0 && i % 50 == 0) reportSlow(dim, center, radius, *area, i);
        if (auto stuck = watch.observe(dim); !stuck.empty() && !told) {
            told = true;
            ++gStranded;
            log().warn(
                "dimension: chunks of the area {} at {} {} {} of dimension {} have not moved on for {} ticks (chunk x z (state): {}): stranded by a ticking area or chunk view removed while they were loading",
                area->name(),
                center.x,
                center.y,
                center.z,
                dim,
                kStuckTicks,
                chunksText(stuck)
            );
        }
        co_await ll::chrono::ticks(1);
    }
    co_return false;
}

} // namespace

ll::coro::CoroTask<AreaHandle> loadArea(int dim, BlockPos center, int radius, int timeoutTicks) {
    auto level = ll::service::getLevel();
    if (!level) co_return nullptr;
    if (!level->getOrCreateDimension(DimensionType(dim)).lock()) co_return nullptr;
    auto box = boxOf(center, radius);
    auto key = std::make_tuple(dim, box.x0, box.z0, box.x1, box.z1);
    for (auto it = shared().begin(); it != shared().end();) {
        if (it->second.expired()) it = shared().erase(it);
        else ++it;
    }
    std::shared_ptr<TransientArea> hold;
    if (auto it = shared().find(key); it != shared().end()) hold = it->second.lock();
    if (hold) {
        // someone holds this area already: wait for it (recovering it as needed), and should it
        // not load, once more with a fresh area of our own
        if (co_await waitLoaded(dim, center, radius, timeoutTicks, hold)) co_return hold;
        log().warn(
            "dimension: the shared area {} at {} {} {} of dimension {} did not load within {} ticks; trying a fresh area: {}",
            hold->name(),
            center.x,
            center.y,
            center.z,
            dim,
            timeoutTicks,
            loadReport(dim, center, radius, &hold->uuid())
        );
        level = ll::service::getLevel();
        if (!level) co_return nullptr;
    }
    auto fresh = newArea(*level, dim, box);
    if (!hold) shared()[key] = fresh;
    if (co_await waitLoaded(dim, center, radius, timeoutTicks, fresh)) co_return fresh;
    log().warn(
        "dimension: the area {} at {} {} {} of dimension {} did not load within {} ticks: {}",
        fresh->name(),
        center.x,
        center.y,
        center.z,
        dim,
        timeoutTicks,
        loadReport(dim, center, radius, &fresh->uuid())
    );
    co_return nullptr;
}

AreaHandle requestArea(int dim, BlockPos center, int radius) {
    auto level = ll::service::getLevel();
    if (!level || !level->getOrCreateDimension(DimensionType(dim)).lock()) return nullptr;
    return newArea(*level, dim, boxOf(center, radius));
}

void holdFor(AreaHandle handle, int ticks) {
    if (!handle) return;
    ll::coro::keepThis([h = std::move(handle), ticks]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(std::max(ticks, 1));
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

int heldAreas() { return gHeld.load(); }

int settlingAreas() { return gSettling.load(); }

int strandedWaits() { return gStranded.load(); }

std::vector<int> chunkStates(int dim, BlockPos center, int radius) {
    std::vector<int> out;
    auto*            region = game::regionOf(dim);
    if (!region) return out;
    auto ring = boxOf(center, radius).grown(1);
    for (int cz = ring.z0; cz <= ring.z1; ++cz)
        for (int cx = ring.x0; cx <= ring.x1; ++cx) out.push_back(chunkState(*region, cx, cz));
    return out;
}


class AreaLoaderModule {
public:
    void load() {
        game::db::onWorldClosed([] {
            ++gGeneration;
            shared().clear();
        });
    }
    void install() {}
    void uninstall() {}
};

} // namespace gf::feature::dimension

GF_REGISTER_MODULE(gf::feature::dimension::AreaLoaderModule);

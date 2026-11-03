// Area loading of unit F4 (feature/dimension/AreaLoader.h): transient engine ticking areas.

#include "feature/dimension/AreaLoader.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/platform/UUID.h"
#include "mc/util/Bounds.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/BlockSource.h"
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
std::atomic<bool> gSettle{true};

/// How long a released area may keep loading before it goes all the same.
constexpr int kSettleTicks = 200;

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

/// Removes area `uuid` once its loading is over, for at most kSettleTicks. A ticking area removed
/// while its chunks are still in the engine's generation pipeline strands them: the neighbours
/// the area had asked for are dropped, and a chunk that was waiting for them (lighting finished,
/// not yet loaded) never gets further, even for a new area asked for over it in the same tick
/// (what made gf::world::teleport miss its destination: dimension.area_handover).
void removeWhenSettled(mce::UUID uuid, std::string name, int generation) {
    ++gSettling;
    ll::coro::keepThis([uuid, name = std::move(name), generation]() -> ll::coro::CoroTask<> {
        int waited = 0;
        for (; waited < kSettleTicks; ++waited) {
            auto level = ll::service::getLevel();
            if (generation != gGeneration.load() || !level) break;
            if (settled(*level, uuid)) break;
            co_await ll::chrono::ticks(1);
        }
        if (auto level = ll::service::getLevel(); level && generation == gGeneration.load()) {
            if (waited >= kSettleTicks) log().info("dimension: the released area {} was still loading after {} ticks; removed all the same", name, waited);
            else if (waited >= 20) log().info("dimension: the released area {} finished its loading {} ticks after its release", name, waited);
            level->getTickingAreasMgr().removeScopedAreas({uuid});
        }
        --gSettling;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

class TransientArea final : public AreaHold {
public:
    TransientArea(mce::UUID uuid, std::string name) : mUuid(uuid), mName(std::move(name)), mGeneration(gGeneration.load()) { ++gHeld; }
    TransientArea(TransientArea const&)            = delete;
    TransientArea& operator=(TransientArea const&) = delete;
    ~TransientArea() override {
        --gHeld;
        if (mGeneration != gGeneration.load()) return; // the world (and its areas) are gone
        auto level = ll::service::getLevel();
        if (!level) return;
        if (gSettle.load() && !settled(*level, mUuid)) {
            removeWhenSettled(mUuid, std::move(mName), mGeneration);
            return;
        }
        level->getTickingAreasMgr().removeScopedAreas({mUuid});
    }

private:
    mce::UUID   mUuid;
    std::string mName;
    int         mGeneration;
};

/// The chunk bounds of a ticking area (Bounds(min, max, 16, Flatten): chunk coordinates, y 0).
Bounds chunkBounds(BlockPos const& center, int radius) {
    Bounds b{};
    int    x0 = (center.x - radius) >> 4, x1 = (center.x + radius) >> 4;
    int    z0 = (center.z - radius) >> 4, z1 = (center.z + radius) >> 4;
    b.mMin->x = x0;
    b.mMin->y = 0;
    b.mMin->z = z0;
    b.mMax->x = x1;
    b.mMax->y = 0;
    b.mMax->z = z1;
    b.mDim->x = x1 - x0 + 1;
    b.mDim->y = 1;
    b.mDim->z = z1 - z0 + 1;
    b.mArea    = b.mDim->x * b.mDim->z;
    b.mVolume  = b.mArea;
    b.mSide    = 16;
    return b;
}

/// The areas held now, by dimension and chunk bounds: a request for the same area shares it.
std::map<std::tuple<int, int, int, int, int>, std::weak_ptr<AreaHold>>& shared() {
    static std::map<std::tuple<int, int, int, int, int>, std::weak_ptr<AreaHold>> m;
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
        auto&       source = region->getChunkSource();
        std::string chunks;
        int         x0 = (center.x - radius) >> 4, x1 = (center.x + radius) >> 4;
        int         z0 = (center.z - radius) >> 4, z1 = (center.z + radius) >> 4;
        // and the ring of neighbours around them, in parentheses
        for (int cz = z0 - 1; cz <= z1 + 1; ++cz) {
            chunks += std::format("{}z {}:", chunks.empty() ? "" : " |", cz);
            for (int cx = x0 - 1; cx <= x1 + 1; ++cx) {
                bool ring  = cx < x0 || cx > x1 || cz < z0 || cz > z1;
                auto chunk = source.getExistingChunk(ChunkPos{cx, cz});
                int  state = chunk ? static_cast<int>(chunk->mLoadState->load()) : -1;
                bool here  = region->hasChunksAt(BlockPos{cx * 16 + 8, center.y, cz * 16 + 8}, 0, false);
                chunks += std::format(" {}{}{}{}", ring ? "(" : "", state, here ? "" : "?", ring ? ")" : "");
            }
        }
        out += std::format("; chunk states from x {} (11 loaded, -1 none, ? not in the region): {}", x0 - 1, chunks);
    } else {
        out += "; the dimension has no region";
    }
    return out;
}

namespace {

/// The engine's state of one area that waits for its chunks, at most a few times per wait.
void reportSlow(int dim, BlockPos const& center, int radius, mce::UUID const* area, int ticks) {
    log().info(
        "dimension: the area at {} {} {} of dimension {} is still loading after {} ticks: {}",
        center.x,
        center.y,
        center.z,
        dim,
        ticks,
        loadReport(dim, center, radius, area)
    );
}

} // namespace

ll::coro::CoroTask<AreaHandle> loadArea(int dim, BlockPos center, int radius, int timeoutTicks) {
    auto level = ll::service::getLevel();
    if (!level) co_return nullptr;
    if (!level->getOrCreateDimension(DimensionType(dim)).lock()) co_return nullptr;
    Bounds b   = chunkBounds(center, radius);
    auto   key = std::make_tuple(dim, b.mMin->x, b.mMin->z, b.mMax->x, b.mMax->z);
    for (auto it = shared().begin(); it != shared().end();) {
        if (it->second.expired()) it = shared().erase(it);
        else ++it;
    }
    AreaHandle hold;
    if (auto it = shared().find(key); it != shared().end()) hold = it->second.lock();
    if (hold) {
        for (int i = 0; i <= timeoutTicks; ++i) {
            if (loaded(dim, center, radius)) co_return hold;
            if (i > 0 && i % 50 == 0) reportSlow(dim, center, radius, nullptr, i);
            co_await ll::chrono::ticks(1);
        }
        log().warn(
            "dimension: the shared area at {} {} {} of dimension {} did not load within {} ticks: {}",
            center.x,
            center.y,
            center.z,
            dim,
            timeoutTicks,
            loadReport(dim, center, radius, nullptr)
        );
        co_return nullptr;
    }
    auto uuid = mce::UUID::random();
    auto   name = "gf_dimension_" + std::to_string(++gCounter);
    auto   ctx  = level->getTickingAreasMgr().addTransientScopedArea(DimensionType(dim), name, kScopeKey, uuid, b);
    if (!gLogged.exchange(true)) {
        Bounds const& got = ctx.mBounds;
        log().info(
            "dimension: transient ticking area {} in dimension {}: chunks {} {} .. {} {} (engine: {} {} .. {} {}, active {})",
            name,
            dim,
            b.mMin->x,
            b.mMin->z,
            b.mMax->x,
            b.mMax->z,
            got.mMin->x,
            got.mMin->z,
            got.mMax->x,
            got.mMax->z,
            static_cast<bool>(ctx.mIsActive)
        );
    }
    hold          = std::make_shared<TransientArea>(uuid, name);
    shared()[key] = hold;
    for (int i = 0; i <= timeoutTicks; ++i) {
        if (loaded(dim, center, radius)) {
            if (i >= 50) log().info("dimension: the area {} at {} {} {} of dimension {} loaded after {} ticks", name, center.x, center.y, center.z, dim, i);
            co_return hold;
        }
        if (i > 0 && i % 50 == 0) reportSlow(dim, center, radius, &uuid, i);
        co_await ll::chrono::ticks(1);
    }
    log().warn(
        "dimension: the area {} at {} {} {} of dimension {} did not load within {} ticks: {}",
        name,
        center.x,
        center.y,
        center.z,
        dim,
        timeoutTicks,
        loadReport(dim, center, radius, &uuid)
    );
    co_return nullptr;
}

void holdFor(AreaHandle handle, int ticks) {
    if (!handle) return;
    ll::coro::keepThis([h = std::move(handle), ticks]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(std::max(ticks, 1));
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

int heldAreas() { return gHeld.load(); }

int settlingAreas() { return gSettling.load(); }

void setSettleOnReleaseForTest(bool settle) { gSettle.store(settle); }

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

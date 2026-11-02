// Area loading of unit F4 (feature/dimension/AreaLoader.h): transient engine ticking areas.

#include "feature/dimension/AreaLoader.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
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
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/ticking/TickingAreasManager.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::dimension {

namespace {

/// The scope the areas are registered under (a fixed key, "gf:dimen").
constexpr std::uint64_t kScopeKey = 0x67663a64696d656eULL;

std::atomic<int> gGeneration{0}; ///< bumped when a world closes: holds of a closed world do nothing
std::atomic<int> gHeld{0};
std::atomic<int> gCounter{0};
std::atomic<bool> gLogged{false};

class TransientArea final : public AreaHold {
public:
    explicit TransientArea(mce::UUID uuid) : mUuid(uuid), mGeneration(gGeneration.load()) { ++gHeld; }
    TransientArea(TransientArea const&)            = delete;
    TransientArea& operator=(TransientArea const&) = delete;
    ~TransientArea() override {
        --gHeld;
        if (mGeneration != gGeneration.load()) return; // the world (and its areas) are gone
        auto level = ll::service::getLevel();
        if (!level) return;
        level->getTickingAreasMgr().removeScopedAreas({mUuid});
    }

private:
    mce::UUID mUuid;
    int       mGeneration;
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
            co_await ll::chrono::ticks(1);
        }
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
    hold         = std::make_shared<TransientArea>(uuid);
    shared()[key] = hold;
    for (int i = 0; i <= timeoutTicks; ++i) {
        if (loaded(dim, center, radius)) co_return hold;
        co_await ll::chrono::ticks(1);
    }
    log().warn("dimension: the area at {} {} {} of dimension {} did not load within {} ticks", center.x, center.y, center.z, dim, timeoutTicks);
    co_return nullptr;
}

void holdFor(AreaHandle handle, int ticks) {
    if (!handle) return;
    ll::coro::keepThis([h = std::move(handle), ticks]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(std::max(ticks, 1));
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

int heldAreas() { return gHeld.load(); }

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

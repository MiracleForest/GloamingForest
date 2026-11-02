// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/ColorHandler.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11), client mod: the biome foliage colour under TF's canopy and mangrove leaves
// (LeafTint.h). ColorHandler tints them with CANOPY_COLORIZER / MANGROVE_COLORIZER of
// BiomeColors.getAverageFoliageColor; their tint functions (gf::client::tint, F12; registered in
// ClientFamilies.cpp) get only the position, so the foliage colour comes from the tint resolver of
// the chunk mesh that asks: ClientBlockPipeline::PositionDependentTintResolver::_get, the only
// place the tint functions run (F12's hook on the same function, which this one wraps: a higher
// priority), records the resolver for its thread while it runs, and biomeFoliageAt asks that
// resolver for Bedrock's "default_foliage" tint at the position (the biomes' foliage colours
// blended as the game blends them; a block without a tint function of its own is asked, so the
// tint registry hands the request to the game). A tint function reached any other way finds no
// resolver (nullopt): the leaves take ColorHandler's colour without a world (LeafColors.h Blend:
// FoliageColor.getEvergreenColor / getBirchColor).

#ifdef GF_CLIENT

#include "client/families/LeafTint.h"

#include <algorithm>
#include <atomic>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/block/tessellation_pipeline/helper/client_block_pipeline/PositionDependentTintResolver.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/TintMethod.h"

#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::families {

namespace {

/// The position-dependent tint resolver running _get on this thread (a chunk builder), if any.
struct Current {
    ClientBlockPipeline::PositionDependentTintResolver* resolver = nullptr;
    BiomeTintCache*                                     cache    = nullptr;
};
thread_local Current tCurrent;

/// A block the tint registry has no function for and whose own tint is the foliage's: the game
/// answers the foliage request for it (resolved on the client thread when a world is joined,
/// cleared when it is left: the chunk builders of the next world never see the old Block).
std::atomic<Block const*> gProbe{nullptr};
std::atomic<bool>         gLogged{false};

unsigned rgbOf(mce::Color const& c) {
    auto ch = [](float v) { return static_cast<unsigned>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return ch(c.r) << 16 | ch(c.g) << 8 | ch(c.b);
}

} // namespace

/// Records the resolver of PositionDependentTintResolver::_get for its thread while it runs.
class TintScope {
public:
    TintScope(ClientBlockPipeline::PositionDependentTintResolver* resolver, BiomeTintCache* cache) : mSaved(tCurrent) { tCurrent = {resolver, cache}; }
    ~TintScope() { tCurrent = mSaved; }
    TintScope(TintScope const&)            = delete;
    TintScope& operator=(TintScope const&) = delete;

private:
    Current mSaved;
};

std::optional<unsigned> biomeFoliageAt(BlockPos const& pos) {
    auto const   cur   = tCurrent;
    Block const* probe = gProbe.load(std::memory_order_acquire);
    if (!cur.resolver || !probe) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true)) log().warn("families: no tint resolver to ask for the biome foliage at {} {} {} (the colour without a world instead)", pos.x, pos.y, pos.z);
        return std::nullopt;
    }
    // the resolver's positions are relative to the mesh's corner
    BlockPos const& min = cur.resolver->mMinPos;
    BlockPos        rel{pos.x - min.x, pos.y - min.y, pos.z - min.z};
    mce::Color      c = cur.resolver->_get(*probe, rel, TintMethod::DefaultFoliage, cur.cache);
    if (!gLogged.exchange(true)) log().info("families: canopy and mangrove leaves blend the biome foliage colour (#{:06X} at {} {} {})", rgbOf(c), pos.x, pos.y, pos.z);
    return rgbOf(c);
}

} // namespace gf::client::families

// Outside F12's tint hook (HookPriority::High runs first): the resolver is known while the tint
// functions run.
LL_TYPE_INSTANCE_HOOK(
    FamiliesTintContextHook,
    ll::memory::HookPriority::High,
    ClientBlockPipeline::PositionDependentTintResolver,
    &ClientBlockPipeline::PositionDependentTintResolver::$_get,
    mce::Color,
    Block const&    block,
    BlockPos const& blockPos,
    TintMethod      tintMethod,
    BiomeTintCache* biomeTintCache
) {
    gf::client::families::TintScope scope(this, biomeTintCache);
    return origin(block, blockPos, tintMethod, biomeTintCache);
}

namespace gf::client::families {

namespace {

class LeafTintModule {
public:
    void install() {
        FamiliesTintContextHook::hook();
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>([](auto&) {
            gProbe.store(game::blockWith("minecraft:oak_leaves"), std::memory_order_release);
        });
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            gProbe.store(nullptr, std::memory_order_release);
        });
        if (auto const* b = game::blockWith("minecraft:oak_leaves")) gProbe.store(b, std::memory_order_release);
    }
    void uninstall() {
        if (mJoin) ll::event::EventBus::getInstance().removeListener(mJoin);
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        mJoin = nullptr;
        mExit = nullptr;
        gProbe.store(nullptr, std::memory_order_release);
        FamiliesTintContextHook::unhook();
    }

private:
    ll::event::ListenerPtr mJoin;
    ll::event::ListenerPtr mExit;
};

} // namespace

} // namespace gf::client::families

GF_REGISTER_MODULE(gf::client::families::LeafTintModule);

#endif // GF_CLIENT

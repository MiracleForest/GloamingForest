// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TFBushBlock.java
//   src/main/java/twilightforest/client/event/ColorHandler.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11), client mod: the client side of the families.
//
// - The family registry for the client's own block types: the client builds its block types per
//   world it joins (a remote server's blocks), so the registry looks them up again on every join
//   (the server thread of a local world reads the table it has meanwhile).
// - Climbable family blocks (TF's climbable hollow logs, rope, iron ladder: BlockProperty::Climbable,
//   like vanilla ladders) for the client's own movement, set again on every join.
// - Leaf colours by position (ColorHandler: time, transformation, mining, sorting and rainbow oak
//   leaves, LeafColors.h) through the client.render contract's tint (gf::client::tint, F12); the
//   packs carry Java's colour without a world for every client (tools/families).
// - Snowy grass under snowloggables: TFBushBlock.updateSnowBeneath sets the grass below a bush
//   with snow in it SNOWY. Bedrock's grass has no such state: it draws its snowed side when the
//   client's grass variant (GrassBlock::calcVariant, client only) finds snow above, so the variant
//   of grass under a snowloggable family block with layers is the snowed one.

#ifdef GF_CLIENT

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/GrassBlock.h"

#include "client/render/Render.h"
#include "feature/families/Families.h"
#include "feature/families/LeafColors.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::families::detail {
void refreshTypes();
} // namespace gf::feature::families::detail

namespace gf::client::families {

namespace fam = gf::feature::families;

/// The grass at `pos` is under a snowloggable family block with snow in it (TFBushBlock's SNOWY).
bool snowyUnder(BlockSource& region, BlockPos const& pos) {
    Block const& above = region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z});
    auto const*  e     = fam::entryOf(above);
    return e && e->has(fam::Snowloggable) && fam::intState(above, fam::state::Layers).value_or(0) > 0;
}

} // namespace gf::client::families

LL_TYPE_INSTANCE_HOOK(
    FamiliesSnowyGrassHook,
    ll::memory::HookPriority::Normal,
    GrassBlock,
    &GrassBlock::calcVariant,
    int,
    BlockSource&       region,
    BlockPos const&    pos,
    mce::Color const&  baseColor
) {
    int variant = origin(region, pos, baseColor);
    // grass_side's variants: one per side colour, then the snowed side
    if (gf::client::families::snowyUnder(region, pos)) return static_cast<int>(mSideColors->size());
    return variant;
}

namespace gf::client::families {

namespace {

class ClientFamiliesModule {
public:
    void install() {
        FamiliesSnowyGrassHook::hook();
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>([](ll::event::ClientJoinLevelEvent&) {
            fam::detail::refreshTypes(); // the joined world's own block types
            fam::applyClimbable();
        });
        static bool tinted = false;
        if (!tinted) {
            tinted = true;
            int n  = 0;
            for (auto const* e : fam::entriesOf(fam::Family::Leaves)) {
                auto color = fam::leafcolors::of(e->id);
                if (!color) continue;
                tint::registerBlock(e->id, [fn = *color](BlockPos const& p) { return fn(p.x, p.y, p.z); });
                ++n;
            }
            if (n) log().info("families: {} leaf colours by position", n);
        }
    }

    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mJoin);
        FamiliesSnowyGrassHook::unhook();
    }

private:
    ll::event::ListenerPtr mJoin;
};

} // namespace

} // namespace gf::client::families

GF_REGISTER_MODULE(gf::client::families::ClientFamiliesModule);

#endif // GF_CLIENT

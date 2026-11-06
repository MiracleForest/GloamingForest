// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TrophyPedestalBlock.java
//   src/main/java/twilightforest/block/StrongholdShieldBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U25's two blocks.
//
// The trophy pedestal: custom blocks get no neighbour updates of their own, so TF's neighborChanged
// runs from F11's neighbour dispatch of the pedestal and from the placement of a trophy (any block
// of #twilightforest:trophy_pedestal_activation_blocks) on top of one. The comparator output is the
// trophy's (BossVariant comparator value through gf::boss::trophyVariant). Breaking: the BP makes
// the inactive pedestal unbreakable and the active one minable (TrophyPedestalBlock#getDestroyProgress).
//
// The stronghold shield: breakable only from its inside face (StrongholdShieldBlock
// #getDestroyProgress). The imported BP keeps it unbreakable (strength -1, F1's import.behaviour
// checks that); a survival player's mining that starts (or ends) on any other face is refused here
// as well, so the face rule holds once the inside-face break time (digSpeed(stone) / 150 a tick) is
// given to the block. Explosions and mobs cannot break it.
#include "feature/stronghold/Blocks.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/boss/Boss.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/entity/EntityInternal.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/landmark/Landmark.h"
#include "feature/progression/Progression.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::stronghold::blocks {

namespace fe = gf::feature::entity;
namespace ia = gf::feature::common::interactions;

namespace {

constexpr char kActivationTag[] = "twilightforest:trophy_pedestal_activation_blocks";

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

bool isActive(Block const& block) {
    auto v = game::statesOf(block)["twilightforest:active"];
    return v == "true" || v == "1";
}

/// The tick of the last neighborChanged per pedestal (the placement hook's deferred call is skipped
/// when F11's neighbour dispatch already ran it for the same change).
std::map<std::tuple<int, int, int, int>, std::uint64_t>& lastUpdate() {
    static std::map<std::tuple<int, int, int, int>, std::uint64_t> m;
    return m;
}

/// level.getEntitiesOfClass(Player.class, new AABB(pos).inflate(16.0D)).
std::vector<Player*> playersAround(int dim, BlockPos const& pos) {
    std::vector<Player*> out;
    for (auto* p : fe::playersNear(dim, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 32.0)) {
        auto const& b = p->getAABB();
        if (b.max.x > pos.x - 16 && b.min.x < pos.x + 17 && b.max.y > pos.y - 16 && b.min.y < pos.y + 17 && b.max.z > pos.z - 16 && b.min.z < pos.z + 17)
            out.push_back(p);
    }
    return out;
}

/// PlayerHelper.doesPlayerHaveRequiredAdvancements(player, progress_lich) || abilities.instabuild.
bool isPlayerEligible(Player& player) {
    return gf::progression::has(player, "twilightforest:progress_lich") || player.getPlayerGameType() == GameType::Creative;
}

/// TrophyPedestalBlock#removeNearbyShields: destroyBlock(pos, false) for every shield within 5.
int removeNearbyShields(BlockSource& region, BlockPos const& pos) {
    int removed = 0;
    for (int sx = -5; sx <= 5; sx++)
        for (int sy = -5; sy <= 5; sy++)
            for (int sz = -5; sz <= 5; sz++) {
                BlockPos at{pos.x + sx, pos.y + sy, pos.z + sz};
                if (game::blockIdAt(region, at) != kShield) continue;
                region.getLevel().destroyBlock(region, at, false, BlockChangeContext{});
                removed++;
            }
    return removed;
}

void doPedestalEffect(BlockSource& region, BlockPos const& pos) {
    if (auto const* active = game::blockWithStates(kPedestal, {{"twilightforest:active", true}})) game::setBlock(region, pos, *active);
    int removed = removeNearbyShields(region, pos);
    gf::fx::sound(region.getDimension(), "block.twilightforest.trophy_pedestal.activate", Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 4.0f, 0.1f);
    gf::log().info("stronghold: trophy pedestal activated at {} {} {} ({} shields removed)", pos.x, pos.y, pos.z, removed);
}

} // namespace

bool neighborChanged(BlockSource& region, BlockPos const& pos) {
    int dimId = static_cast<int>(region.getDimensionId());
    if (lastUpdate().size() > 256) lastUpdate().clear();
    lastUpdate()[{dimId, pos.x, pos.y, pos.z}] = fe::currentTick();
    Block const& state = region.getBlock(pos);
    if (game::blockId(state) != kPedestal || isActive(state)) return false;
    // isTrophyOnTop
    if (!gf::tags::has(gf::tags::Registry::Block, kActivationTag, game::blockIdAt(region, BlockPos{pos.x, pos.y + 1, pos.z}))) return false;
    int  dim       = static_cast<int>(region.getDimensionId());
    auto nearby    = playersAround(dim, pos);
    bool activated = false;
    if (gf::landmark::enforcedProgression()) {
        if (std::any_of(nearby.begin(), nearby.end(), [](Player* p) { return isPlayerEligible(*p); })) {
            doPedestalEffect(region, pos);
            activated = true;
        }
        // warnIneligiblePlayers
        for (auto* p : nearby)
            if (!isPlayerEligible(*p)) actionBar(*p, gf::trFor(*p, "misc.twilightforest.pedestal_ineligible", "You are unworthy."));
    } else {
        doPedestalEffect(region, pos);
        activated = true;
    }
    // rewardNearbyPlayers (server players within 16, whether it activated or not)
    for (auto* p : nearby) {
        gf::progression::trigger(*p, "twilightforest:placed_on_trophy_pedestal");
        gf::stats::award(*p, "twilightforest:trophy_pedestals_activated");
    }
    return activated;
}

bool canBreakShieldFrom(Block const& shield, int face) {
    // FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
    static char const* const kNames[] = {"down", "up", "north", "south", "west", "east"};
    auto        facing = game::statesOf(shield)["twilightforest:facing"];
    int         f      = 0;
    for (int i = 0; i < 6; i++)
        if (facing == kNames[i]) f = i;
    bool upOrDown = f == 0 || f == 1;
    // upFace = facing, sideFace = facing.getOpposite()
    int wanted = upOrDown ? f : (f ^ 1);
    return face == wanted;
}

void install() {
    // the pedestal's neighborChanged, from F11's dispatch and from a trophy placed on top of one
    gf::blockevents::onNeighborChanged(kPedestal, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { neighborChanged(region, pos); });
    gf::blockevents::onPlaced(std::string("#") + kActivationTag, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) {
        BlockPos below{pos.x, pos.y - 1, pos.z};
        if (game::blockIdAt(region, below) != kPedestal) return;
        int dim = static_cast<int>(region.getDimensionId());
        auto placedAt = fe::currentTick();
        ia::nextTick([dim, below, placedAt] {
            auto it = lastUpdate().find({dim, below.x, below.y, below.z});
            if (it != lastUpdate().end() && it->second >= placedAt) return; // F11 ran it already
            if (auto* r = game::regionOf(dim)) neighborChanged(*r, below);
        });
    });
    // getAnalogOutputSignal: the trophy's comparator value
    gf::redstone::registerAnalog(kPedestal, [](BlockSource& region, BlockPos const& pos, Block const&) -> std::optional<int> {
        auto variant = gf::boss::trophyVariant(game::blockIdAt(region, BlockPos{pos.x, pos.y + 1, pos.z}));
        return variant ? variant->comparator : 0;
    });
    // the shield: survival mining only from the inside face
    auto refuseOutside = [](ia::DestroyContext const& ctx) {
        if (ctx.player.getPlayerGameType() == GameType::Creative) return ia::Result::Pass;
        return canBreakShieldFrom(ctx.block, ctx.face) ? ia::Result::Pass : ia::Result::Deny;
    };
    ia::onStartDestroy({.block = kShield}, refuseOutside, ia::priority::High);
    ia::onDestroy({.block = kShield}, refuseOutside, ia::priority::High);
}

} // namespace gf::feature::stronghold::blocks

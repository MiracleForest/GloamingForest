// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/CastleDoorBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the four castle doors. A use (or a redstone signal from a block that is not a
// door) makes a solid door active; 2..6 ticks later it vanishes (no collision, the small outline box
// of the BP permutation), plays the vanish sound, sends 64 annihilate particles and activates every
// adjacent solid door, so a whole door vanishes as a chain; 80 ticks later it turns active again and
// becomes solid 2..6 ticks after that, each step with the reappear sound. CastleDoorBlock.isBlockLocked
// is always false in 4.8 (the locked branch and its door.activate sound are never reached).
#include <cmath>

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/finalcastle/BlocksCommon.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::finalcastle::blocks {

namespace ia = gf::interactions;

namespace {

constexpr char kActive[] = "gloamingforest:active";
constexpr char kVanish[] = "gloamingforest:vanish";

/// CastleDoorBlock#isBlockLocked: always false in upstream 4.8.
bool locked(BlockSource&, BlockPos const&) { return false; }

/// changeToActiveBlock(level, pos, state).
void changeToActive(BlockSource& region, BlockPos const& pos) {
    if (isDoor(idAt(region, pos))) setStates(region, pos, {{kActive, true}});
    schedule(region, pos, 2 + random().nextInt(5));
}

void playVanish(BlockSource& region, BlockPos const& pos) {
    sound(region, pos, "block.gloamingforest.door.vanish", 0.125f, random().nextFloat() * 0.25f + 1.75f);
}

void playReappear(BlockSource& region, BlockPos const& pos) {
    sound(region, pos, "block.gloamingforest.door.reappear", 0.125f, random().nextFloat() * 0.25f + 1.25f);
}

void vanishParticles(BlockSource& region, BlockPos const& pos) {
    // a ParticlePacket to the players within 32 blocks (fx sends to the players near)
    auto& rand = random();
    for (int dx = 0; dx < 4; ++dx) {
        for (int dy = 0; dy < 4; ++dy) {
            for (int dz = 0; dz < 4; ++dz) {
                Vec3 at{float(pos.x + (dx + 0.5) / 4), float(pos.y + (dy + 0.5) / 4), float(pos.z + (dz + 0.5) / 4)};
                Vec3 v{float(rand.nextGaussian() * 0.2), float(rand.nextGaussian() * 0.2), float(rand.nextGaussian() * 0.2)};
                particle(region, "gloamingforest:annihilate", at, v);
            }
        }
    }
}

/// checkAndActivateCastleDoor.
void checkAndActivate(BlockSource& region, BlockPos const& pos) {
    if (!loaded(region, pos)) return;
    Block const& b = region.getBlock(pos);
    if (isDoor(game::blockId(b)) && !boolState(b, kVanish) && !boolState(b, kActive) && !locked(region, pos)) changeToActive(region, pos);
}

/// CastleDoorBlock#tick.
void doorTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    bool active = boolState(block, kActive);
    if (boolState(block, kVanish)) {
        if (active) setStates(region, pos, {{kVanish, false}, {kActive, false}});
        else changeToActive(region, pos);
        playReappear(region, pos);
        return;
    }
    if (!active) return;
    setStates(region, pos, {{kVanish, true}, {kActive, false}});
    schedule(region, pos, 80);
    playVanish(region, pos);
    vanishParticles(region, pos);
    // activate all adjacent inactive doors
    for (int d = 0; d < 6; ++d) checkAndActivate(region, offset(pos, d));
}

} // namespace

bool activateDoor(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (!isDoor(game::blockId(b)) || boolState(b, kVanish) || boolState(b, kActive)) return false; // FAIL
    if (locked(region, pos)) {
        sound(region, pos, "block.gloamingforest.door.activate", 1.0f, 0.3f);
        return false; // PASS
    }
    changeToActive(region, pos);
    return true;
}

void installDoors() {
    for (char const* door : kDoors) {
        onTick(door, doorTick);
        // useWithoutItem (Java skips it while sneaking with an item: the item is used instead)
        ia::onUseOn({.block = door}, [](ia::UseOnContext const& c) {
            if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            // FAIL (vanished or active): the held item is used as if the door took nothing
            if (boolState(c.block, kVanish) || boolState(c.block, kActive)) return ia::Result::Pass;
            if (!c.firstEvent) return ia::Result::Consume;
            if (activateDoor(*region, game::blockPos(c.pos)))
                gf::log().info("finalcastle: {} opened a castle door at {}", c.player.getRealName(), c.pos.key());
            return ia::Result::Consume;
        });
        // neighborChanged: a signal from a block that is not a castle door
        blockevents::onNeighborChanged(door, [](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
            if (isDoor(idAt(region, from))) return;
            if (powered(region, pos)) (void)activateDoor(region, pos);
        });
    }
}

} // namespace gf::feature::finalcastle::blocks

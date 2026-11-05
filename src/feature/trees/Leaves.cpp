// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TransformationLeavesBlock.java
//   src/main/java/twilightforest/world/components/feature/trees/growers/StructureTreeGrower.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees: what the leaves and saplings do beyond the families kit (F11 gives them decay, growth,
// pots and tints): the runes under the leaves of transformation (animateTick, run on the server for
// the blocks a client's random display ticks would pick) and the robust twilight oak sapling, whose
// grower starts the hollow tree landmark (StructureTreeGrower -> gf::landmark::generateAt).
#include "feature/trees/Trees.h"

#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/ext/families/Growers.h"
#include "feature/fx/Fx.h"
#include "feature/landmark/Landmark.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::trees {

namespace {

constexpr char kTransformationLeaves[] = "twilightforest:transformation_leaves";

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int    nextInt(int bound) { return static_cast<int>(rng()() % static_cast<unsigned>(bound)); }
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

/// TransformationLeavesBlock#animateTick: one leaf rune under the block
/// (ParticleUtils.spawnParticlesOnBlockFace(.., ConstantInt.of(1), Direction.DOWN, () -> Vec3.ZERO, 0.5F)).
void animateTick(Dimension& dim, BlockPos const& pos) {
    double x = pos.x + 0.5 + (-0.5 + nextDouble() * 1.0);
    double y = pos.y + 0.5 - 0.55;
    double z = pos.z + 0.5 + (-0.5 + nextDouble() * 1.0);
    fx::particle(dim, "twilightforest:leaf_rune", Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}, Vec3{0.0f, 0.0f, 0.0f});
}

/// ClientLevel#animateTick around a player: 667 samples at radius 16 and 667 at radius 32, each
/// axis offset nextInt(r) - nextInt(r).
void displayTicks(Player& player) {
    auto&    region = player.getDimensionBlockSource();
    auto&    dim    = region.getDimension();
    BlockPos at     = game::blockPosOf(player.getPosition());
    short    minY = region.getMinHeight(), maxY = region.getMaxHeight();
    for (int j = 0; j < 667; ++j) {
        for (int range : {16, 32}) {
            BlockPos p{at.x + nextInt(range) - nextInt(range), at.y + nextInt(range) - nextInt(range), at.z + nextInt(range) - nextInt(range)};
            if (p.y < minY || p.y >= maxY) continue;
            Block const& b = region.getBlock(p);
            if (b.getTypeName() == kTransformationLeaves) animateTick(dim, p);
        }
    }
}

bool growHollowTree(BlockSource& region, BlockPos const& pos, std::string const& structureId) {
    Block const* sapling = &region.getBlock(pos);
    auto const*  air     = game::blockWith("minecraft:air");
    if (air) game::setBlock(region, pos, *air);
    bool ok = gf::landmark::generateAt(structureId, region, pos);
    if (!ok) {
        game::setBlock(region, pos, *sapling); // StructureTreeGrower: no valid start, the sapling stays
        gf::log().info("trees: the robust twilight oak sapling at {} {} {} could not grow {}", pos.x, pos.y, pos.z, structureId);
    } else {
        gf::log().info("trees: the robust twilight oak sapling at {} {} {} grew {}", pos.x, pos.y, pos.z, structureId);
    }
    return ok;
}

} // namespace

void tickLeaves() {
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<Player*> players;
    level->forEachPlayer([&](Player& p) {
        players.push_back(&p);
        return true;
    });
    for (auto* p : players) displayTicks(*p);
}

void installLeaves() {
    gf::families_ext::registerStructureGrower("twilightforest:hollow_tree", [](BlockSource& region, BlockPos const& pos, std::string const& id, gf::families_ext::RandomSource&) {
        return growHollowTree(region, pos, id);
    });
}

void uninstallLeaves() {}

} // namespace gf::feature::trees

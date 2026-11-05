// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TransformationLeavesBlock.java
//   src/main/java/twilightforest/block/HardenedDarkLeavesBlock.java
//   src/main/java/twilightforest/world/components/feature/trees/growers/StructureTreeGrower.java
//   src/main/java/twilightforest/world/components/structures/type/HollowTreeStructure.java (checkSaplingClearance)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees: what the leaves and saplings do beyond the families kit (F11 gives them decay, growth,
// pots and tints):
// - the runes under the leaves of transformation (animateTick). The client mod draws them itself
//   (src/client/trees/LeafRunes.cpp); for a player without it the server samples the blocks that
//   player's client would pick and sends the runes to that player only;
// - hardened dark leaves: no mob spawns on them (isValidSpawn = TFBlocks::noSpawning, through the
//   spawn contract's position override) and pick block gives dark leaves (getCloneItemStack,
//   src/feature/ext/trees/PickBlock);
// - the robust twilight oak sapling, whose grower starts the hollow tree landmark
//   (StructureTreeGrower -> gf::landmark::generateAt) after the hollow tree's sapling clearance.
#include "feature/trees/Trees.h"

#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/ext/families/Growers.h"
#include "feature/ext/trees/PickBlock.h"
#include "feature/landmark/Landmark.h"
#include "feature/net/Net.h"
#include "feature/spawn/Spawn.h"
#include "feature/trees/TreeUtil.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::trees {

namespace {

constexpr char kTransformationLeaves[] = "twilightforest:transformation_leaves";
constexpr char kHardenedDarkLeaves[]   = "twilightforest:hardened_dark_leaves";
/// At most this many runes a tick go to one player without the client mod (each is a packet).
constexpr int kMaxVanillaRunes = 16;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int    nextInt(int bound) { return static_cast<int>(rng()() % static_cast<unsigned>(bound)); }
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

std::uint64_t gVanillaRunes = 0;

/// ClientLevel#animateTick around a player without the client mod: 667 samples at radius 16 and
/// 667 at radius 32, each axis offset nextInt(r) - nextInt(r); each sampled leaves of transformation
/// sheds one leaf rune below its bottom face (TransformationLeavesBlock#animateTick), sent to that
/// player alone.
void displayTicks(Player& player) {
    auto&    region = player.getDimensionBlockSource();
    BlockPos at     = game::blockPosOf(player.getPosition());
    short    minY = region.getMinHeight(), maxY = region.getMaxHeight();
    auto     dim  = static_cast<uchar>(static_cast<int>(region.getDimensionId()));
    int      sent = 0;
    for (int j = 0; j < 667 && sent < kMaxVanillaRunes; ++j) {
        for (int range : {16, 32}) {
            BlockPos p{at.x + nextInt(range) - nextInt(range), at.y + nextInt(range) - nextInt(range), at.z + nextInt(range) - nextInt(range)};
            if (p.y < minY || p.y >= maxY) continue;
            if (region.getBlock(p).getTypeName() != kTransformationLeaves) continue;
            // ParticleUtils.spawnParticleOnFace(DOWN, spread 0.5)
            Vec3 v{static_cast<float>(p.x + 0.5 + (-0.5 + nextDouble())), static_cast<float>(p.y + 0.5 - 0.55), static_cast<float>(p.z + 0.5 + (-0.5 + nextDouble()))};
            SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(v, "twilightforest:leaf_rune", dim, std::nullopt));
            pkt.sendTo(player);
            ++sent;
        }
    }
    if (sent > 0 && gVanillaRunes == 0) gf::log().info("trees: the server sends the leaf runes to {} (no client mod)", player.getRealName());
    gVanillaRunes += static_cast<std::uint64_t>(sent);
}

/// HollowTreeStructure#checkSaplingClearance: every block from 3 below to 128 above the sapling,
/// 3 blocks around, is replaceable by trees (FeatureLogic.treesReplaceable).
bool saplingClearance(BlockSource& region, BlockPos const& pos) {
    constexpr int r = 3;
    for (int x = pos.x - r; x <= pos.x + r; ++x)
        for (int y = pos.y - r; y <= pos.y + 128; ++y)
            for (int z = pos.z - r; z <= pos.z + r; ++z) {
                if (x == pos.x && y == pos.y && z == pos.z) continue;
                if (y < region.getMinHeight() || y >= region.getMaxHeight()) {
                    // outside the world: Java reads void air there (replaceable)
                    continue;
                }
                if (!isReplaceable(region.getBlock(BlockPos{x, y, z}), true)) return false;
            }
    return true;
}

bool growHollowTree(BlockSource& region, BlockPos const& pos, std::string const& structureId) {
    // StructureTreeGrower#growTree: the clearance first; the sapling stays when it fails
    if (!saplingClearance(region, pos)) {
        gf::log().info("trees: the robust twilight oak sapling at {} {} {} has no clearance for {}", pos.x, pos.y, pos.z, structureId);
        return false;
    }
    Block const* sapling = &region.getBlock(pos);
    auto const*  air     = game::blockWith("minecraft:air");
    if (air) game::setBlock(region, pos, *air);
    bool ok = gf::landmark::generateAt(structureId, region, pos);
    if (!ok) {
        game::setBlock(region, pos, *sapling); // no valid start (or no chunk loaded): the sapling stays
        gf::log().info("trees: the robust twilight oak sapling at {} {} {} could not grow {}", pos.x, pos.y, pos.z, structureId);
    } else {
        gf::log().info("trees: the robust twilight oak sapling at {} {} {} grew {}", pos.x, pos.y, pos.z, structureId);
    }
    return ok;
}

} // namespace

bool hollowTreeClearance(BlockSource& region, BlockPos const& pos) { return saplingClearance(region, pos); }

std::uint64_t vanillaLeafRunesSent() { return gVanillaRunes; }

void tickLeaves() {
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<Player*> players;
    level->forEachPlayer([&](Player& p) {
        // the client mod draws its own runes; simulated players have no client
        if (!p.isSimulatedPlayer() && !gf::net::isCompanion(p)) players.push_back(&p);
        return true;
    });
    for (auto* p : players) displayTicks(*p);
}

void installLeaves() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    gf::families_ext::registerStructureGrower("twilightforest:hollow_tree", [](BlockSource& region, BlockPos const& pos, std::string const& id, gf::families_ext::RandomSource&) {
        return growHollowTree(region, pos, id);
    });
    // TFBlocks.HARDENED_DARK_LEAVES isValidSpawn(noSpawning): no natural spawn stands on them (the
    // spawners' position list is empty there)
    gf::spawn::onSpecialSpawn([](BlockSource& region, BlockPos const& pos, std::string const&) -> std::optional<std::vector<gf::spawn::SpawnEntry>> {
        BlockPos below{pos.x, pos.y - 1, pos.z};
        if (below.y < region.getMinHeight()) return std::nullopt;
        if (region.getBlock(below).getTypeName() == kHardenedDarkLeaves) return std::vector<gf::spawn::SpawnEntry>{};
        return std::nullopt;
    });
    // HardenedDarkLeavesBlock#getCloneItemStack: dark leaves
    gf::trees_ext::setPickItem(kHardenedDarkLeaves, "twilightforest:dark_leaves");
}

void uninstallLeaves() {}

} // namespace gf::feature::trees

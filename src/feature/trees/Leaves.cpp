// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/TransformationLeavesBlock.java
//   src/main/java/gloamingforest/block/HardenedDarkLeavesBlock.java
//   src/main/java/gloamingforest/world/components/feature/trees/growers/StructureTreeGrower.java
//   src/main/java/gloamingforest/world/components/structures/type/HollowTreeStructure.java (checkSaplingClearance)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees: what the leaves and saplings do beyond the families kit (F11 gives them decay, growth,
// pots and tints):
// - the runes under the leaves of transformation (animateTick). The client mod draws them itself
//   (src/client/trees/LeafRunes.cpp); for a player without it the server samples the blocks that
//   player's client would pick and sends the runes to that player only;
// - hardened dark leaves: no mob spawns on them (isValidSpawn = GloamingBlocks::noSpawning, through the
//   spawn contract's position override) and pick block gives dark leaves (getCloneItemStack,
//   src/feature/ext/trees/PickBlock);
// - the robust gloaming oak sapling, whose grower starts the hollow tree landmark
//   (StructureTreeGrower -> gf::landmark::generateAt) after the hollow tree's sapling clearance.
#include "feature/trees/Trees.h"

#include <random>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunk.h"
#include "mc/world/level/chunk/SubChunkStorage.h"
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

constexpr char kTransformationLeaves[] = "gloamingforest:transformation_leaves";
constexpr char kHardenedDarkLeaves[]   = "gloamingforest:hardened_dark_leaves";
/// At most this many runes a tick go to one player without the client mod (each is a packet).
constexpr int kMaxVanillaRunes = 16;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int    nextInt(int bound) { return static_cast<int>(rng()() % static_cast<unsigned>(bound)); }
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

std::uint64_t gVanillaRunes = 0;

/// Per player without the client mod: whether leaves of transformation are within the samples'
/// reach, and the tick that looks again.
struct NearLeaves {
    std::int64_t next = 0;
    bool         has  = false;
    int          dim = -1, cx = 0, cy = 0, cz = 0; ///< where the last look was (a move to another section looks again)
};
constexpr std::int64_t                       kNearRefresh = 20;
std::int64_t                                 gTick        = 0;
std::unordered_map<std::int64_t, NearLeaves> gNear;

/// Whether a sub-chunk the animateTick samples around `at` can reach (31 blocks on each axis) holds
/// leaves of transformation (its block palette has them).
bool leavesNear(BlockSource& region, BlockPos const& at) {
    auto const* leaves = game::blockWith(kTransformationLeaves);
    if (!leaves) return false;
    BlockType const* type = &leaves->getBlockType();
    auto             is   = [type](Block const& b) { return &b.getBlockType() == type; };
    for (int cx = (at.x - 31) >> 4; cx <= (at.x + 31) >> 4; ++cx)
        for (int cz = (at.z - 31) >> 4; cz <= (at.z + 31) >> 4; ++cz) {
            LevelChunk* chunk = region.getChunk(ChunkPos{cx, cz});
            if (!chunk) continue;
            for (auto const& sub : *chunk->mSubChunks) {
                int y0 = static_cast<int>(sub.mAbsoluteIndex) * 16;
                if (y0 + 15 < at.y - 31 || y0 > at.y + 31) continue;
                auto const* storage = sub.mBlocksReadPtr[0];
                if (storage && storage->hasAnyElementMatchingFilterInPalette(is)) return true;
            }
        }
    return false;
}

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
            if (sent >= kMaxVanillaRunes) break;
            BlockPos p{at.x + nextInt(range) - nextInt(range), at.y + nextInt(range) - nextInt(range), at.z + nextInt(range) - nextInt(range)};
            if (p.y < minY || p.y >= maxY) continue;
            if (region.getBlock(p).getTypeName() != kTransformationLeaves) continue;
            // ParticleUtils.spawnParticleOnFace(DOWN, spread 0.5)
            Vec3 v{static_cast<float>(p.x + 0.5 + (-0.5 + nextDouble())), static_cast<float>(p.y + 0.5 - 0.55), static_cast<float>(p.z + 0.5 + (-0.5 + nextDouble()))};
            SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(v, "gloamingforest:leaf_rune", dim, std::nullopt));
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
        gf::log().info("trees: the robust gloaming oak sapling at {} {} {} has no clearance for {}", pos.x, pos.y, pos.z, structureId);
        return false;
    }
    // upstream removes the sapling (UPDATE_ALL) only once the start is valid, then places the pieces.
    // The landmark contract builds and places a start in one call (generateAt; it has no
    // validate-only step), so the sapling stays while the start is made: a failed start leaves it
    // untouched (no block update, no F11 removal / placement events), and after a valid one it
    // goes unless a piece already put something else there (upstream's pieces would overwrite upstream's air).
    Block const* sapling = &region.getBlock(pos);
    bool         ok      = gf::landmark::generateAt(structureId, region, pos);
    if (!ok) {
        gf::log().info("trees: the robust gloaming oak sapling at {} {} {} could not grow {}", pos.x, pos.y, pos.z, structureId);
        return false;
    }
    if (&region.getBlock(pos) == sapling) {
        // no piece put a block into the sapling's cell: the hollow tree's trunk is hollow
        // (HollowTreeTrunk#postProcess fills only radius / 2 < dist <= radius), so its center is the
        // air upstream's grower left when it removed the sapling: the usual case, not a problem. (A sapling
        // at a chunk corner grows the tree elsewhere until F6 records `forced` starts, see
        // docs/units/trees.md; the start is `placed`, so landmark::startAt cannot tell here.)
        gf::log().debug("trees: the {} start left the robust gloaming oak sapling's cell {} {} {} alone (its hollow trunk; upstream cleared it before placing)",
                        structureId, pos.x, pos.y, pos.z);
        if (auto const* air = game::blockWith("minecraft:air")) game::setBlock(region, pos, *air);
    }
    gf::log().info("trees: the robust gloaming oak sapling at {} {} {} grew {}", pos.x, pos.y, pos.z, structureId);
    return true;
}

} // namespace

bool hollowTreeClearance(BlockSource& region, BlockPos const& pos) { return saplingClearance(region, pos); }

std::uint64_t vanillaLeafRunesSent() { return gVanillaRunes; }

void tickLeaves() {
    auto level = ll::service::getLevel();
    if (!level) return;
    ++gTick;
    std::vector<Player*> players;
    level->forEachPlayer([&](Player& p) {
        // the client mod draws its own runes; simulated players have no client
        if (!p.isSimulatedPlayer() && !gf::net::isCompanion(p)) players.push_back(&p);
        return true;
    });
    if (players.empty()) {
        gNear.clear();
        return;
    }
    for (auto* p : players) {
        // the sampling runs only while leaves of transformation are within its reach (a palette
        // look over the sub-chunks the samples can hit, refreshed every kNearRefresh ticks)
        auto&    near = gNear[p->getOrCreateUniqueID().rawID];
        BlockPos at   = game::blockPosOf(p->getPosition());
        int      dim  = static_cast<int>(p->getDimensionId());
        if (gTick >= near.next || dim != near.dim || (at.x >> 4) != near.cx || (at.y >> 4) != near.cy || (at.z >> 4) != near.cz) {
            near.has  = leavesNear(p->getDimensionBlockSource(), at);
            near.next = gTick + kNearRefresh;
            near.dim = dim, near.cx = at.x >> 4, near.cy = at.y >> 4, near.cz = at.z >> 4;
        }
        if (near.has) displayTicks(*p);
    }
    if (gNear.size() > players.size() * 2 + 16) gNear.clear(); // players who left
}

void installLeaves() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    gf::families_ext::registerStructureGrower("gloamingforest:hollow_tree", [](BlockSource& region, BlockPos const& pos, std::string const& id, gf::families_ext::RandomSource&) {
        return growHollowTree(region, pos, id);
    });
    // No natural spawn stands on upstream's leaves (the spawners' position list is empty there; F5's
    // spawner does not ask BlockState#isValidSpawn):
    // - hardened dark leaves: isValidSpawn(noSpawning);
    // - the LeavesBlock leaves: the default isValidSpawn, isFaceSturdy(UP), on LeavesBlock's empty
    //   support shape. Dark leaves (DarkLeavesBlock: a full support shape) are not in the list.
    gf::spawn::onSpecialSpawn([](BlockSource& region, BlockPos const& pos, std::string const&) -> std::optional<std::vector<gf::spawn::SpawnEntry>> {
        static std::unordered_set<std::string> const noSpawn = {kHardenedDarkLeaves,
                                                                "gloamingforest:gloaming_oak_leaves",
                                                                "gloamingforest:canopy_leaves",
                                                                "gloamingforest:mangrove_leaves",
                                                                "gloamingforest:rainbow_oak_leaves",
                                                                "gloamingforest:time_leaves",
                                                                kTransformationLeaves,
                                                                "gloamingforest:mining_leaves",
                                                                "gloamingforest:sorting_leaves"};
        BlockPos below{pos.x, pos.y - 1, pos.z};
        if (below.y < region.getMinHeight()) return std::nullopt;
        if (noSpawn.contains(region.getBlock(below).getTypeName())) return std::vector<gf::spawn::SpawnEntry>{};
        return std::nullopt;
    });
    // HardenedDarkLeavesBlock#getCloneItemStack: dark leaves
    gf::trees_ext::setPickItem(kHardenedDarkLeaves, "gloamingforest:dark_leaves");
}

void uninstallLeaves() {}

} // namespace gf::feature::trees

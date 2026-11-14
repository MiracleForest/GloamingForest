// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/type/HydraLairStructure.java
//   src/main/java/gloamingforest/world/components/structures/HydraLairComponent.java
//   src/main/java/gloamingforest/util/features/FeaturePlacers.java (replaceBlocksDome, processDomeColumn)
//   src/main/java/gloamingforest/world/components/feature/FireJetFeature.java
//   src/main/java/gloamingforest/util/features/FeatureUtil.java (isAreaSuitable)
//   src/main/java/gloamingforest/util/features/FeatureLogic.java (isBlockNotOk)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hydra lair landmark: its generator for F6 (the one HydraLairComponent piece at
// (x - 7, y, z - 7), hill size 2, and the structure terraformer), the piece itself (an exact
// translation over F7's HollowHillComponent base, rule R19) and the gloamingforest:fire_jet
// feature type of the fire swamp's fire jets and smokers.

#include "feature/hydra/Hydra.h"
#include "feature/hydra/Lair.h"

#include <algorithm>
#include <cmath>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "core/hydra/Lair.h"
#include "core/jrand/JRand.h"
#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/Structure.h"
#include "feature/structure/bases/HollowHill.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::hydra {

namespace ch  = gf::core::hydra;
namespace st  = gf::structure;
namespace kit = gf::features::kit;
using core::BlockBox;

namespace {

constexpr char kPieceType[]  = "gloamingforest:GloamingHydra";
constexpr char kSpeleothem[] = "gloamingforest:hydra_lair";
constexpr int  kGloamingSeaLevel = 0; ///< ChunkGenerator#getSeaLevel of the Gloaming Forest

Block const* javaBlock(char const* id) { return kit::javaBlock(id); }

bool isAnyOf(Block const& b, std::initializer_list<char const*> ids) {
    auto id = kit::javaId(b);
    for (auto const* i : ids)
        if (id == i) return true;
    return false;
}

std::optional<BlockBox> intersect(BlockBox const& a, BlockBox const& b) {
    BlockBox r{std::max(a.minX, b.minX), std::max(a.minY, b.minY), std::max(a.minZ, b.minZ), std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY), std::min(a.maxZ, b.maxZ)};
    if (r.minX > r.maxX || r.minY > r.maxY || r.minZ > r.maxZ) return std::nullopt;
    return r;
}

BlockBox inflated(BlockBox const& b, int n) { return {b.minX - n, b.minY - n, b.minZ - n, b.maxX + n, b.maxY + n, b.maxZ + n}; }

/// FeaturePlacers#processDomeColumn.
void processDomeColumn(st::Region& region, BlockPos const& pos, float height, BlockBox const& mask, std::initializer_list<char const*> target, Block const& replacement) {
    if (!mask.contains(pos.x, pos.y, pos.z)) return;
    int top = region.maxY(); // nothing to replace above the world
    for (int dY = 0; static_cast<float>(dY) < height && pos.y + dY < top; dY++) {
        BlockPos p{pos.x, pos.y + dY, pos.z};
        if (isAnyOf(region.getBlock(p), target)) region.setBlock(p, replacement, 3);
    }
}

/// FeaturePlacers#replaceBlocksDome(level, center, radius, squish, chunkBox, structureBox, target, replacement).
void replaceBlocksDome(st::Region& region, BlockPos const& center, float radius, float squish, BlockBox const& chunkBox, BlockBox const& structureBox,
                       std::initializer_list<char const*> target, Block const& replacement) {
    float radiusSquared = radius * radius;
    auto  common        = intersect(chunkBox, structureBox);
    if (!common) return;
    processDomeColumn(region, center, radiusSquared, *common, target, replacement);
    for (int z = 1; static_cast<float>(z) <= radius; z++) {
        for (int x = 0; static_cast<float>(x) <= radius; x++) {
            int distSq = x * x + z * z;
            if (static_cast<float>(distSq) <= radiusSquared) {
                float height = (radiusSquared - static_cast<float>(distSq)) * squish;
                processDomeColumn(region, BlockPos{center.x + x, center.y, center.z + z}, height, *common, target, replacement);
                processDomeColumn(region, BlockPos{center.x - x, center.y, center.z - z}, height, *common, target, replacement);
                processDomeColumn(region, BlockPos{center.x - z, center.y, center.z + x}, height, *common, target, replacement);
                processDomeColumn(region, BlockPos{center.x + z, center.y, center.z - x}, height, *common, target, replacement);
            }
        }
    }
}

} // namespace

// ---- HydraLairComponent ------------------------------------------------------------------------------

int gSpeleothemTries = 0; ///< lattice points that got a speleothem try (selftests)

int speleothemTriesForTest() { return gSpeleothemTries; }

HydraLair::HydraLair(int depth, int x, int y, int z) : HollowHill(kPieceType, depth, 2, x, y + 2, z, kSpeleothem), mBaseOffset(bbox.minY - y) {}

void HydraLair::addChildren(Piece&, std::vector<std::unique_ptr<Piece>>&, RandomSource&) {
    // NO-OP
}

void HydraLair::postProcess(Region& region, RandomSource& rand, BlockBox const& writeableBounds, BlockPos const&) {
    auto const* stone = javaBlock("minecraft:stone");
    auto const* grass = javaBlock("minecraft:grass_block");
    auto        c     = ch::boxCenter(bbox);
    if (stone)
        replaceBlocksDome(region, BlockPos{c.x, worldY(-4), c.z}, 36.5F, 1.0F / 80.0F, writeableBounds, inflated(bbox, 4), {"minecraft:grass_block", "minecraft:dirt"}, *stone);
    if (grass) replaceBlocksDome(region, BlockPos{c.x - 16, worldY(-1), c.z - 16}, 18.5F, 1.0F / 80.0F, writeableBounds, inflated(bbox, 1), {"minecraft:stone"}, *grass);

    generateSpeleothems(region, rand, writeableBounds);

    // boss spawner seems important
    placeBlock(region, kSpawner, Json::object(), 27, 3, 27, writeableBounds);
}

void HydraLair::generateSpeleothems(Region& region, RandomSource& rand, BlockBox const& writeableBounds) {
    float    radiusSq = 0.9F * static_cast<float>(radius) * static_cast<float>(radius);
    int      speleothemBase = bbox.minY - mBaseOffset; // the start's y: 0 in the Gloaming Forest
    BlockPos locator  = getLocatorPosition();
    BlockPos exclusionCenter{locator.x - 16, locator.y, locator.z - 16};
    float    exclusionRadiusSq = 23 * 23;
    for (auto const& d : ch::lairLattice(writeableBounds, speleothemBase)) {
        // xz -9 -9 from spawner, center of exclude circle with radius 24
        int dX = d.x - exclusionCenter.x;
        int dZ = d.z - exclusionCenter.z;
        if (static_cast<float>(dX * dX + dZ * dZ) < exclusionRadiusSq) continue;
        BlockPos dest{d.x, d.y, d.z};
        float    distSq = getDistSqFromCenter(locator, dest);
        if (distSq > rand.nextFloat() * 0.9F * radiusSq) continue;
        // an absolute height (upstream sets the lattice position's y, not a piece-relative one)
        dest.y = speleothemBase + core::mth::floorf(core::jrand::mthCos(std::sqrt(distSq) / static_cast<float>(hdiam) * 3.1415927F) * (static_cast<float>(hdiam) / 4.0F));
        ++gSpeleothemTries;
        if (rand.nextFloat() < ch::kStalactiteChance) generateSpeleothem(region, dest, writeableBounds, true);
        if (rand.nextFloat() < ch::kStalagmiteChance) {
            if ((rand.nextFloat() * 0.667F + 0.333F) * distSq / radiusSq < 0.333F) continue;
            dest.y = speleothemBase + 1;
            generateSpeleothem(region, dest, writeableBounds, false);
        }
    }
}

// ---- FireJetFeature ------------------------------------------------------------------------------------

namespace {

/// FeatureLogic#isBlockNotOk.
bool isBlockNotOk(Block const& b) {
    return kit::isFluid(b) || isAnyOf(b, {"minecraft:bedrock", "gloamingforest:hardened_dark_leaves"}) || kit::hasTag(b, "gloamingforest:clouds")
        || kit::javaId(b).find("giant_") != std::string::npos; // GiantBlock (giant cobblestone, log, leaves, obsidian)
}

/// BlockState#isSolidRender (an opaque full cube; approximated by a full colliding non-leaf block).
bool solidRender(Block const& b) { return kit::blocksMotion(b) && !kit::isLeaves(b) && !kit::isFluid(b) && !kit::isAir(b); }

/// FeatureUtil#isAreaSuitable(world, pos, xWidth, height, zWidth, false).
bool isAreaSuitable(st::Region& region, BlockPos const& pos, int xWidth, int height, int zWidth) {
    for (int cx = 0; cx < xWidth; cx++) {
        for (int cz = 0; cz < zWidth; cz++) {
            BlockPos p{pos.x + cx, pos.y, pos.z + cz};
            if (!region.isInside(p.x, p.z)) return false;
            Block const& below = region.getBlock(BlockPos{p.x, p.y - 1, p.z});
            if (!solidRender(below) || isBlockNotOk(below)) return false;
            for (int cy = 0; cy < height; cy++) {
                Block const& b = region.getBlock(BlockPos{p.x, p.y + cy, p.z});
                if (!kit::isAir(b) && !kit::isReplaceable(b)) return false;
            }
        }
    }
    return true;
}

/// Level#canSeeSkyFromBelowWater in a generating region (sky by the WORLD_SURFACE_WG heightmap).
bool canSeeSkyFromBelowWater(st::Region& region, BlockPos const& pos) {
    auto canSeeSky = [&](BlockPos const& p) { return p.y >= region.height(worldgen::Heightmap::WorldSurfaceWg, p.x, p.z); };
    if (pos.y >= kGloamingSeaLevel) return canSeeSky(pos);
    BlockPos sea{pos.x, kGloamingSeaLevel, pos.z};
    if (!canSeeSky(sea)) return false;
    for (int y = sea.y - 1; y > pos.y; --y) {
        Block const& b = region.getBlock(BlockPos{pos.x, y, pos.z});
        if (kit::blocksMotion(b) && !kit::isFluid(b)) return false; // getLightBlock > 0 && !liquid
    }
    return true;
}

bool dirtAt(st::Region& region, int x, int y, int z) { return kit::hasTag(region.getBlock(BlockPos{x, y, z}), "minecraft:dirt"); }

bool fireJetFeature(gf::features::FeatureContext const& ctx) {
    auto&    world = ctx.region;
    BlockPos pos   = ctx.origin;
    auto&    rand  = ctx.random;
    Block const* state = ctx.config.contains("state") ? kit::javaState(ctx.config["state"]) : nullptr;
    if (!state) return false;
    if (!isAreaSuitable(world, pos, 5, 2, 5)) return false;
    auto const* grass = javaBlock("minecraft:grass_block");
    auto const* lava  = javaBlock("minecraft:lava");
    auto const* stone = javaBlock("minecraft:stone");
    if (!grass || !lava || !stone) return false;
    for (int i = 0; i < 4; ++i) {
        int      ox = rand.nextInt(8) - rand.nextInt(8);
        int      oy = rand.nextInt(4) - rand.nextInt(4);
        int      oz = rand.nextInt(8) - rand.nextInt(8);
        BlockPos d{pos.x + ox, pos.y + oy, pos.z + oz};
        if (kit::isAir(world.getBlock(d)) && canSeeSkyFromBelowWater(world, d) && dirtAt(world, d.x, d.y - 1, d.z) && dirtAt(world, d.x + 1, d.y - 1, d.z)
            && dirtAt(world, d.x - 1, d.y - 1, d.z) && dirtAt(world, d.x, d.y - 1, d.z + 1) && dirtAt(world, d.x, d.y - 1, d.z - 1)) {
            // create blocks around the jet/smoker, just in case
            for (int gx = -2; gx <= 2; gx++)
                for (int gz = -2; gz <= 2; gz++) world.setBlock(BlockPos{d.x + gx, d.y - 1, d.z + gz}, *grass, 2);
            // jet
            noteGeneratedJet(d.x, d.y - 1, d.z);
            world.setBlock(BlockPos{d.x, d.y - 1, d.z}, *state, 2);
            // create reservoir with stone walls
            for (int rx = -2; rx <= 2; rx++) {
                for (int rz = -2; rz <= 2; rz++) {
                    BlockPos d2{d.x + rx, d.y - 2, d.z + rz};
                    if (rx >= -1 && rx <= 1 && rz >= -1 && rz <= 1) world.setBlock(d2, *lava, 2); // lava reservoir
                    else if (!kit::isLava(world.getBlock(d2))) world.setBlock(d2, *stone, 2);    // only stone where there is no lava
                    world.setBlock(BlockPos{d2.x, d2.y - 1, d2.z}, *stone, 2);
                }
            }
        }
    }
    return true;
}

} // namespace

// ---- selftests ---------------------------------------------------------------------------------------

namespace {

/// A worldgen region over a live block source that writes only inside a clip box.
class ClippedRegion final : public st::Region {
public:
    ClippedRegion(BlockSource& source, BlockBox const& clip, std::int64_t seed) : mSource(source), mClip(clip), mSeed(seed) {}
    [[nodiscard]] int centerChunkX() const override { return (mClip.minX + mClip.maxX) / 2 >> 4; }
    [[nodiscard]] int centerChunkZ() const override { return (mClip.minZ + mClip.maxZ) / 2 >> 4; }
    [[nodiscard]] int minY() const override { return mSource.getMinHeight(); }
    [[nodiscard]] int maxY() const override { return mSource.getMaxHeight(); }
    [[nodiscard]] bool isInside(int x, int z) const override { return x >= mClip.minX && x <= mClip.maxX && z >= mClip.minZ && z <= mClip.maxZ; }
    [[nodiscard]] Block const& getBlock(BlockPos const& pos) const override { return mSource.getBlock(pos); }
    bool setBlock(BlockPos const& pos, Block const& block, int) override {
        if (!mClip.contains(pos.x, pos.y, pos.z)) return false;
        return game::setBlock(mSource, pos, block);
    }
    /// The heightmaps of the clipped column (the hill the test built included): one above the top
    /// motion-blocking block (leaves skipped for the *_NO_LEAVES map), the clip's floor without one.
    [[nodiscard]] int height(worldgen::Heightmap map, int x, int z) const override {
        for (int y = mClip.maxY; y >= mClip.minY; --y) {
            Block const& b = mSource.getBlock(BlockPos{x, y, z});
            if (kit::isAir(b) || !kit::blocksMotion(b)) continue;
            if (map == worldgen::Heightmap::MotionBlockingNoLeaves && kit::isLeaves(b)) continue;
            return y + 1;
        }
        return mClip.minY;
    }
    [[nodiscard]] std::string biome(BlockPos const&) const override { return "gloamingforest:fire_swamp"; }
    [[nodiscard]] std::int64_t seed() const override { return mSeed; }
    [[nodiscard]] int dimension() const override { return static_cast<int>(mSource.getDimensionId()); }

private:
    BlockSource& mSource;
    BlockBox     mClip;
    std::int64_t mSeed;
};

} // namespace

BlockBox placeLairForTest(BlockSource& source, BlockBox const& clip, int x, int y, int z, std::int64_t seed, bool terraform) {
    ClippedRegion          region(source, clip, seed);
    HydraLair              lair(0, x - 7, y, z - 7);
    if (terraform) {
        // the hill the terrain pass makes before the piece (HydraLairStructure's terraformer added to
        // a flat ground at the start's y, the Gloaming Forest's sea level, translated to y): dirt
        // where the density is positive, grass on top
        auto const* dirt  = javaBlock("minecraft:dirt");
        auto const* grass = javaBlock("minecraft:grass_block");
        auto const* air   = javaBlock("minecraft:air");
        BlockBox    shifted{lair.bbox.minX, lair.bbox.minY - y, lair.bbox.minZ, lair.bbox.maxX, lair.bbox.maxY - y, lair.bbox.maxZ};
        auto        solid = [&](int bx, int by, int bz) {
            int yr = by - y;
            // the ground below y, a nearly flat density above it: the hill is the terraformer's own shape
            double ground = yr < 0 ? 1.0 : -0.01 * (static_cast<double>(yr) + 0.5);
            return ground + ch::lairTerraformer(shifted, bx, yr, bz) > 0.0;
        };
        if (dirt && grass && air)
            for (int bx = clip.minX; bx <= clip.maxX; ++bx)
                for (int bz = clip.minZ; bz <= clip.maxZ; ++bz)
                    for (int by = clip.minY; by <= clip.maxY; ++by) {
                        Block const* b = !solid(bx, by, bz) ? air : (by < clip.maxY && solid(bx, by + 1, bz) ? dirt : grass);
                        region.setBlock(BlockPos{bx, by, bz}, *b, 2);
                    }
    }
    core::jrand::LegacyRandom random(seed);
    lair.postProcess(region, random, clip, BlockPos{x, y, z});
    return lair.bbox;
}

// ---- registration ------------------------------------------------------------------------------------

void installLair() {
    st::registerPieceType(kPieceType, [](st::Json const& saved) -> std::unique_ptr<st::Piece> {
        auto p = std::make_unique<HydraLair>(0, 0, 0, 0);
        p->load(saved);
        return p;
    });
    gf::landmark::registerGenerator(kLair, {
                                              .layout =
                                                  [](gf::landmark::LayoutContext& ctx) {
                                                      auto const& c = ctx.start.center;
                                                      // HydraLairStructure#getFirstPiece: (x - 7, y, z - 7)
                                                      ctx.pieces.push_back(std::make_unique<HydraLair>(0, c.x - 7, c.y, c.z - 7));
                                                      return true;
                                                  },
                                              .terraform = [](gf::landmark::Start const& start, int x, int y, int z) { return ch::lairTerraformer(start.box, x, y, z); },
                                          });
    gf::features::registerType("gloamingforest:fire_jet", fireJetFeature);
}

void uninstallLair() {}

} // namespace gf::feature::hydra

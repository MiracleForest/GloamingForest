// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/trees/TFTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/CanopyTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/MegaOakTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/MegaCanopyTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/LargeWinterTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/MiningTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/HollowTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/TimeTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/DarkCanopyTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/SnowTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/SnowUnderTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/config/TFTreeFeatureConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees: TF's own tree feature types. TFTreeFeature subclasses read TFTreeFeatureConfig and
// generate through a trunk, a leaves and a decoration (root) setter; the dark canopy tree and the
// snow tree are TreeFeature copies that delegate the tree itself to the minecraft:tree interpreter
// after their own checks. Leaf distances are not written: the families kit computes them on the
// first random tick (distance 0 = not computed yet); TF's own tree features run TreeFeature.updateLeaves.
#include "feature/trees/Trees.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "mc/world/level/block/Block.h"

#include "core/jrand/JavaOrder.h"
#include "core/trees/JavaHashSet.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/trees/TreeUtil.h"
#include "game/Blocks.h"
#include "mod/Log.h"

#pragma clang fp contract(off)

namespace gf::feature::trees {

namespace kit = gf::features::kit;
using gf::features::FeatureContext;

namespace {

Json const& member(Json const& j, char const* key) {
    static Json const null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}

/// Random#nextInt with Java's bound check (a non-positive bound would throw: TF never passes one;
/// 0 is answered without a draw here).
int nextIntSafe(RandomSource& random, int bound) { return bound > 0 ? random.nextInt(bound) : 0; }

/// TFTreeFeatureConfig.
struct TFConfig {
    Json const& trunk;
    Json const& leaves;
    Json const& branch;
    Json const& roots;
    int         minHeight;
    int         chanceFirst;
    int         chanceSecond;
    bool        hasLeaves;
    Json const& decorators;

    explicit TFConfig(Json const& c)
    : trunk(member(c, "trunk_provider")),
      leaves(member(c, "leaves_provider")),
      branch(member(c, "branch_provider")),
      roots(member(c, "roots_provider")),
      minHeight(c.value("minimum_size", 20)),
      chanceFirst(std::max(c.value("add_first_five_chance", 1), 1)),
      chanceSecond(std::max(c.value("add_second_five_chance", 1), 1)),
      hasLeaves(c.value("has_leaves", true)),
      decorators(member(c, "decorators")) {}
};

/// What TFTreeFeature#generate gets.
struct Gen {
    Region&       region;
    RandomSource& random;
    BlockPos      pos;
    Placer        trunk;  ///< biconsumer (set)
    Placer        leaves; ///< biconsumer1 (set1)
    RootPlacer    deco;   ///< RootPlacer(biconsumer2 (set2), 1)
    TFConfig      config;
};
using GenerateFn = bool (*)(Gen& g);

/// The Java order of a position set handed to decorators: java.util.HashSet iteration (by
/// Vec3i#hashCode), then by y (stable), as F5's vanilla tree feature orders it.
void javaOrder(std::vector<BlockPos>& v) {
    std::vector<int> hashes;
    hashes.reserve(v.size());
    for (auto const& p : v) hashes.push_back(core::jrand::blockPosHash(p.x, p.y, p.z));
    std::vector<BlockPos> out;
    out.reserve(v.size());
    for (auto i : core::jrand::hashSetOrder(hashes)) out.push_back(v[i]);
    std::stable_sort(out.begin(), out.end(), [](BlockPos const& a, BlockPos const& b) { return a.y < b.y; });
    v = std::move(out);
}

/// LeavesBlock.getOptionalDistanceAt: 0 for #minecraft:logs, the distance of a block with one.
std::optional<int> optionalDistanceAt(Block const& b) {
    if (kit::hasTag(b, "minecraft:logs")) return 0;
    if (auto d = game::intState(b, "twilightforest:distance")) return *d == 0 ? 7 : *d; // 0: not computed yet (F11), Java's 7
    if (kit::isLeaves(b)) return 7; // Bedrock's own leaves keep no distance: none known
    return std::nullopt;
}

/// TreeFeature.updateLeaves (Minecraft 1.21.1) as TFTreeFeature#place calls it: `sources` start at
/// distance 0 (TF hands its leaf set here), the trunk is marked done first (and the decorators'
/// set, empty in TF's data); leaves reached from them get distance k (setBlockKnownShape) while
/// k < 7. The seven work sets are HashSets: their iteration order (core::trees::JavaHashSet)
/// decides the order of the writes. StructureTemplate.updateShapeAtEdge, which follows in Java,
/// recomputes the shapes of the blocks around the box's filled cells: Bedrock connects block shapes
/// itself, and F11 recomputes leaf distances on neighbour changes.
void updateLeaves(Region& region, std::vector<BlockPos> const& sources, std::vector<BlockPos> const& trunk, std::vector<BlockPos> const& roots) {
    using core::trees::JavaHashSet;
    // BoundingBox.encapsulatingPositions(set, set1, set2, set3)
    bool any  = false;
    int  minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    for (auto const* list : {&trunk, &sources, &roots})
        for (auto const& p : *list) {
            if (!any) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                minZ = maxZ = p.z;
                any  = true;
            }
            minX = std::min(minX, p.x), minY = std::min(minY, p.y), minZ = std::min(minZ, p.z);
            maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y), maxZ = std::max(maxZ, p.z);
        }
    if (!any) return;
    int const sy = maxY - minY + 1, sz = maxZ - minZ + 1;
    auto      inside = [&](BlockPos const& p) { return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY && p.z >= minZ && p.z <= maxZ; };
    auto      cell   = [&](BlockPos const& p) { return (static_cast<std::size_t>(p.x - minX) * sy + static_cast<std::size_t>(p.y - minY)) * sz + static_cast<std::size_t>(p.z - minZ); };
    std::vector<bool> full(static_cast<std::size_t>(maxX - minX + 1) * sy * sz, false);
    for (auto const& p : trunk)
        if (inside(p)) full[cell(p)] = true;
    std::vector<JavaHashSet> lists(7);
    // list.get(0).addAll(rootPositions): in the source HashSet's order
    JavaHashSet sourceSet;
    for (auto const& p : sources) sourceSet.add({p.x, p.y, p.z});
    for (auto const& p : sourceSet.elements()) lists[0].add(p);
    int k1 = 0;
    while (true) {
        while (k1 >= 7 || !lists[static_cast<std::size_t>(k1)].empty()) {
            if (k1 >= 7) return;
            auto     taken = *lists[static_cast<std::size_t>(k1)].pollFirst();
            BlockPos p{taken.x, taken.y, taken.z};
            if (!inside(p)) continue;
            if (k1 != 0) {
                Block const& state = region.getBlock(p);
                Block const& with  = kit::withProperty(state, "distance", std::to_string(k1));
                if (&with != &state) region.setBlock(p, with, 19);
            }
            full[cell(p)] = true;
            for (Dir d : kAll) {
                BlockPos n = rel(p, d);
                if (!inside(n) || full[cell(n)]) continue;
                auto dist = optionalDistanceAt(region.getBlock(n));
                if (!dist) continue;
                int j1 = std::min(*dist, k1 + 1);
                if (j1 < 7) {
                    lists[static_cast<std::size_t>(j1)].add({n.x, n.y, n.z});
                    k1 = std::min(k1, j1);
                }
            }
        }
        ++k1;
    }
}

/// TFTreeFeature#place: the setters, generate, then the config's decorators.
bool tfTree(FeatureContext const& c, GenerateFn generate) {
    PosSet trunkSet, leafSet, rootSet;
    Region& region = c.region;
    Gen     g{
        region,
        c.random,
        c.origin,
        [&](BlockPos const& p, Block const& b) {
            trunkSet.add(p);
            setBlock(region, p, b);
        },
        [&](BlockPos const& p, Block const& b) {
            leafSet.add(p);
            setBlock(region, p, b);
        },
        RootPlacer{[&](BlockPos const& p, Block const& b) {
                       rootSet.add(p);
                       setBlock(region, p, b);
                   },
                   1},
        TFConfig(c.config),
    };
    if (!generate(g)) return false;
    if (leafSet.list.empty() && rootSet.list.empty()) return false;
    if (g.config.decorators.is_array() && !g.config.decorators.empty()) {
        // TreeDecorator.Context(level, decorationSetter, random, set1, set2, set): TF hands the
        // leaves as "logs", the roots as "leaves" and the trunk as "roots", each a HashSet sorted
        // by y (the HashSet's iteration order first, then a stable sort)
        gf::features::TreeContext tree{region, c.random, c.config, leafSet.list, rootSet.list, trunkSet.list};
        javaOrder(tree.logs);
        javaOrder(tree.leaves);
        javaOrder(tree.roots);
        for (auto const& d : g.config.decorators) runDecorator(tree, d);
    }
    // TreeFeature.updateLeaves(level, box, set1, set3, set): TF passes its leaves as the distance
    // sources and the trunk (and the decorators' blocks, none in TF's data) as filled
    updateLeaves(region, leafSet.list, trunkSet.list, rootSet.list);
    return true;
}

Block const* firefly(Dir facing) { return kit::javaBlock("twilightforest:firefly", Json{{"facing", dirName(facing)}}); }

/// The fireflies on a 2x2 trunk (MegaOakTreeFeature / MegaCanopyTreeFeature buildTrunk).
void trunkFireflies(Gen& g, int treeHeight) {
    for (int i = 0; i < 7; ++i) {
        if (g.random.nextInt(3) != 0) continue;
        Dir  direction = randomDir(g.random);
        bool xAxis = direction == Dir::East || direction == Dir::West, zAxis = direction == Dir::North || direction == Dir::South;
        if (!xAxis && !zAxis) continue;
        int      h   = nextIntSafe(g.random, treeHeight);
        BlockPos bug = at(g.pos, direction == Dir::East ? 1 : 0, h, direction == Dir::South ? 1 : 0);
        bug          = rel(bug, direction);
        int mx       = zAxis ? g.random.nextInt(2) : 0;
        int mz       = xAxis ? g.random.nextInt(2) : 0;
        bug          = at(bug, mx, 0, mz);
        if (!isSolidRender(g.region.getBlock(bug)))
            if (auto const* f = firefly(direction)) g.region.setBlock(bug, *f, 3);
    }
}

/// CanopyTreeFeature.makeRoots.
void makeRoots(Gen& g, BlockPos const& pos) {
    if (hasAirAround(g.region, below(pos))) placeIfValidTreePos(g.region, g.trunk, g.random, below(pos), g.config.trunk);
    else placeIfValidRootPos(g.region, g.deco, g.random, below(pos), g.config.roots);
    int   numRoots = 1 + g.random.nextInt(2);
    float offset   = g.random.nextFloat();
    for (int b = 0; b < numRoots; ++b) buildRoot(g.region, g.deco, g.random, pos, offset, b, g.config.roots);
}

BlockPos clampSpread(BlockPos dest, BlockPos const& pos, int limit) {
    if (dest.x - pos.x < -limit) dest.x = pos.x - limit;
    if (dest.x - pos.x > limit) dest.x = pos.x + limit;
    if (dest.z - pos.z < -limit) dest.z = pos.z - limit;
    if (dest.z - pos.z > limit) dest.z = pos.z + limit;
    return dest;
}

// ---- MegaOakTreeFeature ------------------------------------------------------------------------------

bool megaOak(Gen& g) {
    std::vector<BlockPos> leaves;
    int                   treeHeight = g.config.minHeight;
    if (g.random.nextInt(g.config.chanceFirst) == 0) {
        treeHeight += nextIntSafe(g.random, treeHeight / 2);
        if (g.random.nextInt(g.config.chanceSecond) == 0) treeHeight += g.random.nextInt(5);
    }
    if (outsideBuildHeight(g.region, g.pos.y + treeHeight)) return false;
    if (!saplingSoil(g.region.getBlock(below(g.pos)))) return false; // canSustainPlant(canopy sapling)
    // the 2x2 trunk
    for (int dy = 0; dy < treeHeight; ++dy) {
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 0, dy, 0), g.config.trunk);
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 1, dy, 0), g.config.trunk);
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 0, dy, 1), g.config.trunk);
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 1, dy, 1), g.config.trunk);
    }
    trunkFireflies(g, treeHeight);
    leaves.push_back(at(g.pos, 0, treeHeight, 0));
    // 12 - 20 branches
    int   numBranches = 12 + g.random.nextInt(9);
    float bangle      = g.random.nextFloat();
    for (int b = 0; b < numBranches; ++b) {
        float    btilt = 0.15F + g.random.nextFloat() * 0.35F;
        BlockPos src   = above(g.pos, treeHeight - 10 + b / 2);
        BlockPos dest  = clampSpread(translate(src, 5, bangle, btilt), g.pos, 5);
        drawBresenhamBranch(g.region, g.trunk, g.random, src, dest, g.config.branch);
        placeIfValidTreePos(g.region, g.trunk, g.random, rel(dest, Dir::East), g.config.branch);
        placeIfValidTreePos(g.region, g.trunk, g.random, rel(dest, Dir::West), g.config.branch);
        placeIfValidTreePos(g.region, g.trunk, g.random, rel(dest, Dir::North), g.config.branch);
        placeIfValidTreePos(g.region, g.trunk, g.random, rel(dest, Dir::South), g.config.branch);
        leaves.push_back(dest);
        bangle += g.random.nextFloat() * 0.4F;
        if (bangle > 1.0F) bangle -= 1.0F;
    }
    for (auto const& leaf : leaves) placeSpheroid(g.region, g.leaves, g.random, leaf, 2.5F, 2.5F, g.config.leaves);
    makeRoots(g, g.pos);
    makeRoots(g, rel(g.pos, Dir::East));
    makeRoots(g, rel(g.pos, Dir::South));
    makeRoots(g, rel(rel(g.pos, Dir::East), Dir::South));
    return true;
}

// ---- MegaCanopyTreeFeature ---------------------------------------------------------------------------

BlockPos randomlyOffset(BlockPos const& p, RandomSource& random) {
    switch (random.nextInt(4)) {
    case 0:
        return p;
    case 1:
        return at(p, 1, 0, 0);
    case 2:
        return at(p, 0, 0, 1);
    default:
        return at(p, 1, 0, 1);
    }
}

void megaCanopyLeafBlob(Gen& g, BlockPos const& leaf) {
    auto wood = [&](BlockPos const& p) { placeIfValidTreePos(g.region, g.trunk, g.random, p, g.config.branch); };
    wood(leaf);
    for (Dir d : kHorizontal) {
        wood(rel(leaf, d, 1));
        wood(rel(leaf, d, 2));
        wood(rel(leaf, d, 3));
        wood(rel(leaf, d, 4));
        wood(rel(rel(leaf, d, 5), clockWise(d)));
        wood(rel(rel(leaf, d, 5), counterClockWise(d)));
        wood(rel(rel(leaf, d, 2), clockWise(d), 1));
        wood(rel(rel(leaf, d, 3), clockWise(d), 2));
        wood(rel(rel(leaf, d, 4), clockWise(d), 3));
    }
    // circles with the legacy distance, to match the 1.4.7 version
    placeCircleOdd(g.region, g.leaves, g.random, above(leaf, 2), 3, g.config.leaves, true);
    placeCircleOdd(g.region, g.leaves, g.random, above(leaf), 6, g.config.leaves, true);
    placeCircleOdd(g.region, g.leaves, g.random, leaf, 8, g.config.leaves, true);
    placeCircleOdd(g.region, g.leaves, g.random, below(leaf), 7, g.config.leaves, true);
    placeCircleOdd(g.region, g.leaves, g.random, below(leaf, 2), 4.5F, g.config.leaves, true);
}

bool megaCanopy(Gen& g) {
    std::vector<BlockPos> leaves;
    int                   treeHeight = g.config.minHeight;
    if (g.random.nextInt(g.config.chanceFirst) == 0) {
        treeHeight += nextIntSafe(g.random, treeHeight / 2);
        if (g.random.nextInt(g.config.chanceSecond) == 0) treeHeight += nextIntSafe(g.random, treeHeight / 4);
    }
    if (outsideBuildHeight(g.region, g.pos.y + treeHeight)) return false;
    if (!saplingSoil(g.region.getBlock(below(g.pos)))) return false;
    for (int dy = 0; dy <= treeHeight; ++dy) {
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 0, dy, 0), g.config.trunk);
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 1, dy, 0), g.config.trunk);
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 0, dy, 1), g.config.trunk);
        placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, 1, dy, 1), g.config.trunk);
    }
    trunkFireflies(g, treeHeight);
    leaves.push_back(randomlyOffset(above(g.pos, treeHeight), g.random));
    // 6 - 8 branches
    int   numBranches = 6 + g.random.nextInt(3);
    float bangle      = g.random.nextFloat();
    int   offset      = 0;
    for (int b = 0; b < numBranches; ++b) {
        float    btilt  = 0.25F;
        BlockPos base   = randomlyOffset(g.pos, g.random);
        int      length = 15 + g.random.nextInt(4);
        BlockPos src    = above(base, treeHeight - 15 - (b + offset));
        BlockPos dest   = clampSpread(translate(src, length, bangle, btilt), base, 12);
        drawBresenhamBranch(g.region, g.trunk, g.random, src, dest, g.config.branch);
        drawBresenhamBranch(g.region, g.trunk, g.random, below(src), below(dest), g.config.branch);
        leaves.push_back(dest);
        offset += g.random.nextInt(2);
        bangle += g.random.nextFloat() * 0.4F;
        if (bangle > 1.0F) bangle -= 1.0F;
    }
    for (auto const& leaf : leaves) megaCanopyLeafBlob(g, leaf);
    makeRoots(g, g.pos);
    makeRoots(g, rel(g.pos, Dir::East));
    makeRoots(g, rel(g.pos, Dir::South));
    makeRoots(g, rel(rel(g.pos, Dir::East), Dir::South));
    return true;
}

// ---- LargeWinterTreeFeature -----------------------------------------------------------------------------

void placeLogAt(Gen& g, BlockPos const& pos, char const* axis) {
    if (auto const* b = provide(g.config.trunk, g.random, pos)) g.trunk(pos, kit::withProperty(*b, "axis", axis));
}

void makePineBranches(Gen& g, BlockPos const& pos, int radius) {
    int branchLength = radius > 4 ? radius - 1 : radius - 2;
    switch (pos.y % 2) { // Java's remainder (negative y: no branches)
    case 0:
        for (int i = 1; i <= branchLength; ++i) {
            placeLogAt(g, at(pos, -i, 0, 0), "x");
            placeLogAt(g, at(pos, 0, 0, i + 1), "z");
            placeLogAt(g, at(pos, i + 1, 0, 1), "x");
            placeLogAt(g, at(pos, 1, 0, -i), "z");
        }
        break;
    case 1:
        for (int i = 1; i <= branchLength; ++i) {
            placeLogAt(g, at(pos, -1, 0, 1), "x");
            placeLogAt(g, at(pos, 1, 0, i + 1), "z");
            placeLogAt(g, at(pos, i + 1, 0, 0), "x");
            placeLogAt(g, at(pos, 0, 0, -i), "z");
        }
        break;
    default:
        break;
    }
}

bool largeWinter(Gen& g) {
    int treeHeight = 35;
    if (g.random.nextInt(3) == 0) {
        treeHeight += g.random.nextInt(10);
        if (g.random.nextInt(8) == 0) treeHeight += g.random.nextInt(10);
    }
    if (outsideBuildHeight(g.region, g.pos.y + treeHeight)) return false;
    if (!treeFeatureValidPos(g.region, g.pos)) return false;
    // the 2x2 trunk, dirt under it
    Block const* dirt = kit::javaBlock("minecraft:dirt");
    for (int dy = 0; dy < treeHeight; ++dy) {
        for (auto [dx, dz] : {std::pair{0, 0}, std::pair{0, 1}, std::pair{1, 0}, std::pair{1, 1}}) {
            if (placeIfValidTreePos(g.region, g.trunk, g.random, at(g.pos, dx, dy, dz), g.config.trunk) && dy == 0 && dirt)
                setBlock(g.region, at(g.pos, dx, -1, dz), *dirt);
        }
    }
    // the leaves, pine branches in them
    for (int dy = 0; dy < treeHeight; ++dy) {
        int      radius = static_cast<int>(4.0F * static_cast<float>(dy) / static_cast<float>(treeHeight) + std::fmod(0.75F * static_cast<float>(dy), 3.0F));
        BlockPos p      = above(g.pos, 3 + treeHeight - dy);
        placeCircleEven(g.region, g.leaves, g.random, p, static_cast<float>(radius), g.config.leaves);
        makePineBranches(g, p, radius);
    }
    int   numRoots = 4 + g.random.nextInt(3);
    float offset   = g.random.nextFloat();
    for (int b = 0; b < numRoots; ++b) buildRoot(g.region, g.deco, g.random, g.pos, offset, b, g.config.roots);
    return true;
}

// ---- MiningTreeFeature -----------------------------------------------------------------------------------

void putBranchWithLeaves(Gen& g, BlockPos const& pos, bool bushy) {
    placeIfValidTreePos(g.region, g.trunk, g.random, pos, g.config.branch);
    for (int lx = -1; lx <= 1; ++lx)
        for (int ly = -1; ly <= 1; ++ly)
            for (int lz = -1; lz <= 1; ++lz) {
                if (!bushy && std::abs(ly) > 0 && std::abs(lx) > 0) continue;
                placeProvided(g.region, g.leaves, treeFeatureValidPos, at(pos, lx, ly, lz), g.config.leaves, g.random);
            }
}

/// A core placed by a tree feature where the trunk is (a log or something replaceable).
void placeCore(Gen& g, BlockPos const& check, BlockPos const& corePos, char const* coreId) {
    auto const& state = g.region.getBlock(check);
    if (!kit::hasTag(state, "minecraft:logs") && !kit::isReplaceable(state)) return;
    if (auto const* core = kit::javaBlock(coreId, Json{{"axis", "y"}})) g.region.setBlock(corePos, *core, 3);
}

bool miningTree(Gen& g) {
    if (outsideBuildHeight(g.region, g.pos.y + 12)) return false;
    if (!saplingSoil(g.region.getBlock(below(g.pos)))) return false;
    for (int dy = 0; dy <= 9; ++dy) placeIfValidTreePos(g.region, g.trunk, g.random, above(g.pos, dy), g.config.trunk);
    putBranchWithLeaves(g, at(g.pos, 0, 9, 1), true);
    putBranchWithLeaves(g, at(g.pos, 0, 9, 2), false);
    putBranchWithLeaves(g, at(g.pos, 0, 8, 3), false);
    putBranchWithLeaves(g, at(g.pos, 0, 7, 4), false);
    putBranchWithLeaves(g, at(g.pos, 0, 6, 5), false);
    putBranchWithLeaves(g, at(g.pos, 0, 9, -1), true);
    putBranchWithLeaves(g, at(g.pos, 0, 9, -2), false);
    putBranchWithLeaves(g, at(g.pos, 0, 8, -3), false);
    putBranchWithLeaves(g, at(g.pos, 0, 7, -4), false);
    putBranchWithLeaves(g, at(g.pos, 0, 6, -5), false);
    // the minewood core (its tick runs while it is active: Cores.cpp)
    placeCore(g, above(g.pos), above(g.pos), "twilightforest:mining_log_core");
    if (hasAirAround(g.region, below(g.pos))) placeIfValidTreePos(g.region, g.trunk, g.random, below(g.pos), g.config.trunk);
    else placeIfValidRootPos(g.region, g.deco, g.random, below(g.pos), g.config.roots);
    int   numRoots = 3 + g.random.nextInt(2);
    float offset   = g.random.nextFloat();
    for (int b = 0; b < numRoots; ++b) buildRoot(g.region, g.deco, g.random, g.pos, offset, b, g.config.roots);
    return true;
}

// ---- HollowTreeFeature (the parts the tree of time uses) ------------------------------------------------

void makeSmallBranch(Gen& g, BlockPos const& src, double length, double angle, double tilt, bool leafy) {
    BlockPos dest = translate(src, length, angle, tilt);
    drawBresenhamBranch(g.region, g.trunk, g.random, src, dest, g.config.branch);
    if (leafy) {
        float leafRad = static_cast<float>(g.random.nextInt(2)) + 1.5F;
        placeSpheroid(g.region, g.leaves, g.random, dest, leafRad, leafRad, g.config.leaves);
    }
}

void makeMedBranch(Gen& g, BlockPos const& src, double length, double angle, double tilt, bool leafy) {
    BlockPos dest = translate(src, length, angle, tilt);
    drawBresenhamBranch(g.region, g.trunk, g.random, src, dest, g.config.branch);
    if (leafy) placeSpheroid(g.region, g.leaves, g.random, dest, 2.5F, 2.5F, g.config.leaves); // a blob at the end
    int    numShoots = g.random.nextInt(2) + 1;
    double angleInc  = 0.8 / numShoots;
    for (int i = 0; i <= numShoots; ++i) {
        double angleVar = angleInc * i - 0.4;
        double outVar   = g.random.nextDouble() * 0.8 + 0.2;
        double tiltVar  = g.random.nextDouble() * 0.75 + 0.15;
        BlockPos bsrc   = translate(src, length * outVar, angle, tilt);
        makeSmallBranch(g, bsrc, length * 0.4, angle + angleVar, tilt * tiltVar, leafy);
    }
}

void makeRoot(Gen& g, int diameter, int branchHeight, double length, double angle, double tilt) {
    BlockPos   src  = translate(above(g.pos, branchHeight), diameter, angle, 0.5);
    BlockPos   dest = translate(src, length, angle, tilt);
    Region&    region = g.region;
    RootPlacer placer{[&region](BlockPos const& p, Block const& b) {
                          region.setBlock(p, b, 3);
                          region.setBlock(below(p), b, 3);
                      },
                      2};
    Bresenham  line(src, dest);
    traceExposedRoot(g.region, placer, g.random, g.config.branch, g.config.roots, line);
}

/// buildBranchRing for sizes 0 (small), 1 (medium) and 3 (root); the tree of time has no large ones.
void buildBranchRing(Gen& g, int radius, int branchHeight, int heightVar, int length, double tilt, int minBranches, int maxBranches, int size, bool leafy) {
    int    numBranches    = nextIntSafe(g.random, maxBranches - minBranches) + minBranches;
    double branchRotation = 1.0 / (numBranches + 1);
    double branchOffset   = g.random.nextDouble();
    for (int i = 0; i <= numBranches; ++i) {
        int    dHeight = heightVar > 0 ? branchHeight - heightVar + g.random.nextInt(2 * heightVar) : branchHeight;
        double angle   = i * branchRotation + branchOffset;
        if (size == 3) {
            makeRoot(g, radius, dHeight, length, angle, tilt);
        } else {
            BlockPos src = translate(above(g.pos, dHeight), radius, angle, 0.5);
            if (size == 1) makeMedBranch(g, src, length, angle, tilt, leafy);
            else makeSmallBranch(g, src, length, angle, tilt, leafy);
        }
    }
}

void buildHollowTrunk(Gen& g, int radius, int height) {
    int const hollow = radius >> 1;
    // down 4 blocks: extra trunk where the ground is uneven
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            int ax = std::abs(dx), az = std::abs(dz);
            int dist = std::max(ax, az) + (std::min(ax, az) >> 1);
            if (dist > radius) continue;
            for (int dy = -4; dy < 0; ++dy) {
                BlockPos d = at(g.pos, dx, dy, dz);
                if (hasAirAround(g.region, d)) {
                    if (auto const* b = provide(dist > hollow ? g.config.trunk : g.config.branch, g.random, d)) g.trunk(d, *b);
                } else {
                    placeIfValidRootPos(g.region, g.deco, g.random, d, g.config.roots);
                }
            }
        }
    }
    Block const* vine = kit::javaBlock("minecraft:vine", Json{{"east", "true"}});
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dy = 0; dy <= height; ++dy) {
                BlockPos d    = at(g.pos, dx, dy, dz);
                int      ax   = std::abs(dx), az = std::abs(dz);
                int      dist = static_cast<int>(std::max(ax, az) + std::min(ax, az) * 0.5);
                if (dist <= radius && dist > hollow) placeIfValidTreePos(g.region, g.trunk, g.random, d, g.config.trunk);
                if (dist == hollow && dx == hollow && vine) g.region.setBlock(d, *vine, 3); // a ladder of vines
            }
        }
    }
}

// ---- TimeTreeFeature ------------------------------------------------------------------------------------

bool anyMatchInArea(BlockPos const& pos, int xw, int h, int zw, std::function<bool(BlockPos const&)> const& pred) {
    for (int cx = 0; cx < xw; ++cx)
        for (int cy = 0; cy < h; ++cy)
            for (int cz = 0; cz < zw; ++cz)
                if (pred(at(pos, cx, cy, cz))) return true;
    return false;
}

bool timeTree(Gen& g) {
    int const height = 8, radius = 1;
    if (outsideBuildHeight(g.region, g.pos.y + 1) || outsideBuildHeight(g.region, g.pos.y + height + radius)
        || anyMatchInArea(at(g.pos, -1, -4, -1), 3, 4, 3, [&](BlockPos const& p) { return kit::hasTag(g.region.getBlock(p), "minecraft:features_cannot_replace"); })
        || anyMatchInArea(at(g.pos, -1, 0, -1), 3, 16, 3, [&](BlockPos const& p) { return !validTreePos(g.region, p); }))
        return false;
    if (!saplingSoil(g.region.getBlock(below(g.pos)))) return false;
    // roots first, so they do not fail because they meet the trunk shell
    buildBranchRing(g, radius, 1, 0, 12, 0.75, 3, 5, 3, false);
    buildBranchRing(g, radius, 1, 2, 18, 0.9, 3, 5, 3, false);
    buildHollowTrunk(g, radius, height);
    // the tiny crown
    int const crownRadius = 4, bvar = 1;
    buildBranchRing(g, radius, height - crownRadius, 0, crownRadius, 0.35, bvar, bvar + 2, 1, true);
    buildBranchRing(g, radius, height - (crownRadius >> 1), 0, crownRadius, 0.28, bvar, bvar + 2, 1, true);
    buildBranchRing(g, radius, height, 0, crownRadius, 0.15, 2, 4, 0, true);
    buildBranchRing(g, radius, height, 0, crownRadius >> 1, 0.05, bvar, bvar + 2, 0, true);
    // the clock (TF checks the block above the base and sets one beside it)
    placeCore(g, above(g.pos), at(g.pos, -1, 2, 0), "twilightforest:time_log_core");
    return true;
}

// ---- DarkCanopyTreeFeature / SnowTreeFeature ---------------------------------------------------------------

bool placeVanillaTree(FeatureContext const& c, BlockPos const& origin) {
    Json feature{{"type", "minecraft:tree"}, {"config", c.config}};
    return gf::features::placeJson(feature, c.region, origin, c.random);
}

/// A random source that first answers nextInt(bound) with draws already taken from `inner`, then
/// forwards everything to it (the tree interpreter re-draws the trunk height the dark canopy tree
/// drew for its own free height check).
class ReplayRandom final : public RandomSource {
public:
    ReplayRandom(RandomSource& inner, std::vector<int> ints) : mInner(inner), mInts(std::move(ints)) {}
    void         setSeed(std::int64_t seed) override { mInner.setSeed(seed); }
    int          nextInt() override { return mInner.nextInt(); }
    int          nextInt(int bound) override {
        if (mNext < mInts.size()) return mInts[mNext++];
        return mInner.nextInt(bound);
    }
    std::int64_t nextLong() override { return mInner.nextLong(); }
    bool         nextBoolean() override { return mInner.nextBoolean(); }
    float        nextFloat() override { return mInner.nextFloat(); }
    double       nextDouble() override { return mInner.nextDouble(); }
    double       nextGaussian() override { return mInner.nextGaussian(); }
    void         consumeCount(int count) override { mInner.consumeCount(count); }
    std::unique_ptr<RandomSource>                         fork() override { return mInner.fork(); }
    std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override { return mInner.forkPositional(); }

private:
    RandomSource&    mInner;
    std::vector<int> mInts;
    std::size_t      mNext = 0;
};

bool darkCanopy(FeatureContext const& c) {
    BlockPos pos   = c.origin;
    bool     found = false;
    // given leaves as the start, seek dirt underneath
    for (int dy = pos.y; dy >= c.region.minY(); --dy) {
        auto const& state = c.region.getBlock(BlockPos{pos.x, dy - 1, pos.z});
        if (kit::hasTag(state, "minecraft:dirt")) {
            found = true;
            pos   = BlockPos{pos.x, dy, pos.z};
            break;
        }
        if (kit::hasTag(state, "minecraft:base_stone_overworld") || kit::hasTag(state, "minecraft:sand")) break;
    }
    if (!found) return false;
    for (int i = 0; i < 4; ++i)
        if (!validTreePos(c.region, above(pos, i))) return false;
    // not next to another tree
    for (Dir d : kHorizontal)
        if (kit::hasTag(c.region.getBlock(rel(pos, d)), "minecraft:logs")) return false;
    // DarkCanopyTreeFeature#doPlace is TreeFeature#doPlace except for getMaxFreeTreeHeight, which
    // tests DarkCanopyTreeFeature.validTreePos (air or #replaceable_by_trees: a log is not free)
    // and returns i - 2 at the first position when ignore_vines is false. The trunk height is
    // drawn first (TrunkPlacer#getTreeHeight: two nextInt calls; the dark trees' foliage height and
    // radius are constants and they have no root placer), so the free height is checked here and
    // the tree interpreter replays the two draws.
    Json const& config = c.config;
    Json const  tp     = config.value("trunk_placer", Json::object());
    int const   a      = c.random.nextInt(tp.value("height_rand_a", 0) + 1);
    int const   b      = c.random.nextInt(tp.value("height_rand_b", 0) + 1);
    int const   i      = tp.value("base_height", 0) + a + b;
    if (pos.y < c.region.minY() + 1 || pos.y + i + 1 > c.region.maxY()) return false;
    bool const  ignoreVines = config.value("ignore_vines", false);
    Json const  size        = config.value("minimum_size", Json::object());
    int         k1          = i;
    for (int y = 0; y <= i + 1 && k1 == i; ++y) {
        // FeatureSize#getSizeAtHeight (two_layers / three_layers_feature_size)
        int j = 0;
        if (size.value("type", std::string("minecraft:two_layers_feature_size")).ends_with("three_layers_feature_size")) {
            if (y < size.value("limit", 1)) j = size.value("lower_size", 0);
            else j = y >= i - size.value("upper_limit", 1) ? size.value("upper_size", 1) : size.value("middle_size", 1);
        } else j = y < size.value("limit", 1) ? size.value("lower_size", 0) : size.value("upper_size", 1);
        for (int dx = -j; dx <= j && k1 == i; ++dx)
            for (int dz = -j; dz <= j && k1 == i; ++dz)
                if (!kit::validTreePos(c.region, at(pos, dx, y, dz)) || !ignoreVines) k1 = y - 2;
    }
    bool const hasClip = size.contains("min_clipped_height");
    if (!(k1 >= i || (hasClip && k1 >= size["min_clipped_height"].get<int>()))) return false;
    ReplayRandom replay(c.random, {a, b});
    Json         feature{{"type", "minecraft:tree"}, {"config", config}};
    return gf::features::placeJson(feature, c.region, pos, replay);
}

// ---- SnowUnderTreeFeature ---------------------------------------------------------------------------------

bool snowUnderTrees(FeatureContext const& c) {
    Block const* snow  = kit::javaBlock("minecraft:snow");
    static Json const sturdy{{"type", "minecraft:has_sturdy_face"}, {"offset", Json::array({0, -1, 0})}, {"direction", "up"}};
    if (!snow) return true;
    for (int xi = 0; xi < 16; ++xi) {
        for (int zi = 0; zi < 16; ++zi) {
            int         x   = c.origin.x + xi, z = c.origin.z + zi;
            BlockPos    top{x, c.region.height(gf::worldgen::Heightmap::MotionBlocking, x, z) - 1, z};
            auto const& t   = c.region.getBlock(top);
            if (!kit::hasTag(t, "minecraft:leaves") || kit::javaId(t) == "twilightforest:hardened_dark_leaves") continue; // instanceof LeavesBlock
            BlockPos m{x, c.region.height(gf::worldgen::Heightmap::MotionBlockingNoLeaves, x, z), z};
            if (!c.region.getBlock(m).isAir()) continue;
            if (!kit::testPredicate(sturdy, c.region, m)) continue;
            c.region.setBlock(m, *snow, 2);
            BlockPos     down  = below(m);
            auto const&  under = c.region.getBlock(down);
            Block const& snowy = kit::withProperty(under, "snowy", "true");
            if (&snowy != &under) c.region.setBlock(down, snowy, 2);
        }
    }
    return true;
}

} // namespace

void registerTreeFeatures() {
    using gf::features::registerType;
    registerType("twilightforest:mega_oak", [](FeatureContext const& c) { return tfTree(c, megaOak); });
    registerType("twilightforest:mega_canopy", [](FeatureContext const& c) { return tfTree(c, megaCanopy); });
    registerType("twilightforest:large_winter_tree", [](FeatureContext const& c) { return tfTree(c, largeWinter); });
    registerType("twilightforest:miners_tree", [](FeatureContext const& c) { return tfTree(c, miningTree); });
    registerType("twilightforest:tree_of_time", [](FeatureContext const& c) { return tfTree(c, timeTree); });
    registerType("twilightforest:dark_canopy_tree", darkCanopy);
    // SnowTreeFeature: a copy of TreeFeature (trees on any sturdy ground: the placement checks it)
    registerType("twilightforest:anywhere_tree", [](FeatureContext const& c) { return placeVanillaTree(c, c.origin); });
    registerType("twilightforest:snow_under_trees", snowUnderTrees);
}

} // namespace gf::feature::trees

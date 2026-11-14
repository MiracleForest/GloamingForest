// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/feature/trees/CanopyMushroomFeature.java
//   src/main/java/gloamingforest/world/components/feature/trees/BrownCanopyMushroomFeature.java
//   src/main/java/gloamingforest/world/components/feature/trees/RedCanopyMushroomFeature.java
//   src/main/java/gloamingforest/util/features/FeatureLogic.java (mushroom block states)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U3 trees: upstream's canopy mushrooms (AbstractHugeMushroomFeature subclasses with a branching stem and
// four red cap shapes). Java's huge mushroom blocks have six face booleans; Bedrock's have
// huge_mushroom_bits (0 pores .. 9 cap corners, 10 stem, 14 all cap, 15 all stem): the faces map to
// the closest value (feature/ext/trees/HugeMushrooms.h, which also registers Minecraft's own huge
// brown / red mushroom feature types: the features contract does not have them and upstream's
// vanilla_mushrooms selector, this unit's, places them).
#include "feature/trees/Trees.h"

#include <cmath>

#include "mc/world/level/block/Block.h"

#include "feature/ext/trees/HugeMushrooms.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/trees/TreeUtil.h"
#include "game/Blocks.h"

#pragma clang fp contract(off)

namespace gf::feature::trees {

namespace kit = gf::features::kit;
using gf::features::FeatureContext;

namespace {

using gf::trees_ext::Faces;
using gf::trees_ext::MushroomState;

using gf::trees_ext::mushroomBlock;

MushroomState stateOf(Json const& provider) { return gf::trees_ext::mushroomStateOf(provider); }

struct Mushroom {
    FeatureContext const& c;
    MushroomState         cap, stem;
    int                   foliageRadius;

    explicit Mushroom(FeatureContext const& ctx)
    : c(ctx),
      cap(stateOf(ctx.config.contains("cap_provider") ? ctx.config["cap_provider"] : Json())),
      stem(stateOf(ctx.config.contains("stem_provider") ? ctx.config["stem_provider"] : Json())),
      foliageRadius(ctx.config.value("foliage_radius", 2)) {}

    void set(BlockPos const& p, MushroomState const& s, Faces const& f) {
        if (auto const* b = mushroomBlock(s, f)) c.region.setBlock(p, *b, 3);
    }
};

// ---- AbstractHugeMushroomFeature ---------------------------------------------------------------------

using RadiusFn = int (*)(int foliageRadius, int y);

/// isValidPosition.
bool validPosition(Mushroom& m, BlockPos const& pos, int maxHeight, RadiusFn radius) {
    if (pos.y < m.c.region.minY() + 1 || pos.y + maxHeight + 1 >= m.c.region.maxY()) return false;
    auto const& under = m.c.region.getBlock(below(pos));
    if (!kit::hasTag(under, "minecraft:dirt") && !kit::hasTag(under, "minecraft:mushroom_grow_block")) return false;
    for (int j = 0; j <= maxHeight; ++j) {
        int k = radius(m.foliageRadius, j);
        for (int l = -k; l <= k; ++l)
            for (int i1 = -k; i1 <= k; ++i1) {
                auto const& b = m.c.region.getBlock(at(pos, l, j, i1));
                if (!b.isAir() && !kit::hasTag(b, "minecraft:leaves")) return false;
            }
    }
    return true;
}

// ---- CanopyMushroomFeature ----------------------------------------------------------------------------

enum class Head { Vanilla, Smooth, Spheroid, Flat, Brown };

/// CanopyMushroomFeature#isReplaceable.
bool canopyReplaceable(Region& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    return isReplaceable(b, true) && !isSolidRender(b);
}

bool isEdge(int x, int edge) { return x == edge || x == -edge; }

/// FeatureLogic.getHorizontalMushroomBlockState over the provider's faces.
Faces horizontal(Faces f, int x, int z, int r) {
    f.west  = x == -r || (isEdge(z, r) && x == 1 - r);
    f.east  = x == r || (isEdge(z, r) && x == r - 1);
    f.north = z == -r || (isEdge(x, r) && z == 1 - r);
    f.south = z == r || (isEdge(x, r) && z == r - 1);
    return f;
}

bool insideSmooth(int height, int j, int x, int y, int z) {
    int i = y - (height - 2);
    if (i == 4 || std::abs(x) > j || std::abs(z) > j) return false;
    if (i >= 2) return true;
    bool xMinMax = x == -j || x == j;
    bool zMinMax = z == -j || z == j;
    if (i == 1 && ((xMinMax && std::abs(z) == j - 1) || (zMinMax && std::abs(x) == j - 1))) return false;
    return xMinMax != zMinMax || (std::abs(x) == std::abs(z) && std::abs(x) == j - 1);
}

struct Canopy : Mushroom {
    Head head;

    Canopy(FeatureContext const& ctx, Head h) : Mushroom(ctx), head(h) {}

    bool replaceable(BlockPos const& p) { return canopyReplaceable(c.region, p); }

    /// CanopyMushroomFeature#makeCap (the brown and flat caps).
    void flatCap(BlockPos const& pos, int height, int r) {
        for (int x = -r; x <= r; ++x)
            for (int z = -r; z <= r; ++z) {
                if (isEdge(x, r) && isEdge(z, r)) continue; // isCornerInSquare
                BlockPos p = at(pos, x, height, z);
                if (replaceable(p)) set(p, cap, horizontal(cap.faces, x, z, r));
            }
    }

    void vanillaCap(BlockPos const& pos, int height, int r) {
        for (int y = height - 3; y <= height; ++y) {
            int j = y < height ? r : r - 1;
            int k = r - 2;
            for (int x = -j; x <= j; ++x)
                for (int z = -j; z <= j; ++z) {
                    bool xMinMax = x == -j || x == j, zMinMax = z == -j || z == j;
                    if (!(y >= height || xMinMax != zMinMax)) continue;
                    BlockPos p = at(pos, x, y, z);
                    if (!replaceable(p)) continue;
                    Faces f = cap.faces;
                    if (cap.hasFaces) f = {y >= height - 1, cap.faces.down, z < -k, x > k, z > k, x < -k};
                    set(p, cap, f);
                }
        }
    }

    void smoothCap(BlockPos const& pos, int height, int r) {
        for (int y = height - 2; y <= height + 1; ++y) {
            int j = r - std::max(0, y - (height - 1)) + 1;
            for (int x = -j; x <= j; ++x)
                for (int z = -j; z <= j; ++z) {
                    if (!insideSmooth(height, j, x, y, z)) continue;
                    BlockPos p = at(pos, x, y, z);
                    if (!replaceable(p)) continue;
                    Faces f = cap.faces;
                    if (cap.hasFaces) {
                        f.up    = !insideSmooth(height, j - (y > height - 2 ? 1 : 0), x, y + 1, z);
                        f.west  = !insideSmooth(height, j, x - 1, y, z) && x < 0;
                        f.east  = !insideSmooth(height, j, x + 1, y, z) && x > 0;
                        f.north = !insideSmooth(height, j, x, y, z - 1) && z < 0;
                        f.south = !insideSmooth(height, j, x, y, z + 1) && z > 0;
                    }
                    set(p, cap, f);
                }
        }
    }

    void spheroidCap(BlockPos const& pos, int height, int r) {
        for (int y = height - 2; y <= height; ++y) {
            int j = y == height - 1 ? r + 2 : r + 1;
            for (int x = -j; x <= j; ++x)
                for (int z = -j; z <= j; ++z) {
                    double distance    = std::sqrt(static_cast<double>(x * x + z * z));
                    double maxDistance = static_cast<double>(j) + 0.1;
                    if (distance > maxDistance) continue;
                    BlockPos p = at(pos, x, y, z);
                    if (!replaceable(p)) continue;
                    Faces f = cap.faces;
                    if (cap.hasFaces) {
                        f.up    = y > height - 2 && (y == height || distance > maxDistance - 1.0);
                        f.west  = std::sqrt(static_cast<double>((x - 1) * (x - 1) + z * z)) > maxDistance;
                        f.east  = std::sqrt(static_cast<double>((x + 1) * (x + 1) + z * z)) > maxDistance;
                        f.north = std::sqrt(static_cast<double>(x * x + (z - 1) * (z - 1))) > maxDistance;
                        f.south = std::sqrt(static_cast<double>(x * x + (z + 1) * (z + 1))) > maxDistance;
                    }
                    set(p, cap, f);
                }
        }
    }

    void makeCap(BlockPos const& pos, int height, int r) {
        switch (head) {
        case Head::Vanilla:
            vanillaCap(pos, height, r);
            break;
        case Head::Smooth:
            smoothCap(pos, height, r);
            break;
        case Head::Spheroid:
            spheroidCap(pos, height, r);
            break;
        default:
            flatCap(pos, height, r);
            break;
        }
    }

    int treeHeight() { return 9 + c.random.nextInt(5) + (head == Head::Brown ? 0 : 3); }
    int branches() { return head == Head::Brown ? std::max(c.random.nextInt(5), 3) : 3; }
    double length() { return head == Head::Brown ? 9 - c.random.nextInt(2) : 10 + c.random.nextInt(2); }

    /// addFirefly: on a random side (not up / down).
    bool addFirefly(BlockPos const& pos) {
        Dir d = randomDir(c.random);
        if (d == Dir::Up || d == Dir::Down) return false;
        BlockPos bug = rel(pos, d);
        if (!replaceable(bug)) return false;
        if (auto const* f = kit::javaBlock("gloamingforest:firefly", Json{{"facing", dirName(d)}})) c.region.setBlock(bug, *f, 3);
        return true;
    }

    int buildABranch(BlockPos const& pos, int height, double length, double angle, int r, int bugsLeft) {
        BlockPos  src  = above(pos, height);
        BlockPos  dest = translate(src, length, angle, 0.2);
        Bresenham line(src, BlockPos{dest.x, src.y, dest.z});
        BlockPos  p;
        while (line.next(p)) {
            Faces f = stem.faces;
            if (stem.hasFaces) f.down = f.up = true; // seal up the ups and downs
            if (replaceable(p)) set(p, stem, f);
        }
        int lo = std::min(src.y, dest.y), hi = std::max(src.y, dest.y);
        for (int i = lo; i < hi + 1; ++i) {
            Faces f = stem.faces;
            if (stem.hasFaces && i == lo) f.down = true; // seal up the bottom one
            BlockPos b{dest.x, i, dest.z};
            if (replaceable(b)) set(b, stem, f);
            if (bugsLeft > 0 && i > lo / 2 && c.random.nextInt(20) == 0)
                if (addFirefly(b)) --bugsLeft;
        }
        makeCap(dest, 1, r); // branches need caps as well (height 1)
        return bugsLeft;
    }

    void placeTrunk(BlockPos const& pos, int height) {
        int bugsLeft = std::max(0, c.random.nextInt(10) - 4) / 2;
        for (int i = 0; i < height; ++i) {
            BlockPos m = above(pos, i);
            if (replaceable(m)) {
                set(m, stem, stem.faces);
                if (bugsLeft > 0 && i > height / 2 && c.random.nextInt(10) == 9)
                    if (addFirefly(m)) --bugsLeft;
            } else {
                height = i;
                break;
            }
        }
        int   numBranches = branches();
        float offset      = c.random.nextFloat();
        for (int b = 0; b < numBranches; ++b) {
            double len = length();
            bugsLeft   = buildABranch(pos, height - 6 + b, len, 0.3 * b + static_cast<double>(offset), foliageRadius - 1, bugsLeft);
        }
    }

    bool place() {
        int i = treeHeight();
        if (!validPosition(*this, c.origin, i, [](int fr, int y) { return y <= 3 ? 0 : static_cast<int>(static_cast<float>(fr) * 1.5F); })) return false;
        makeCap(c.origin, i, foliageRadius);
        placeTrunk(c.origin, i);
        return true;
    }
};

} // namespace

void registerMushrooms() {
    using gf::features::registerType;
    registerType("gloamingforest:canopy_brown_mushroom", [](FeatureContext const& c) { return Canopy(c, Head::Brown).place(); });
    registerType("gloamingforest:canopy_red_vanilla_mushroom", [](FeatureContext const& c) { return Canopy(c, Head::Vanilla).place(); });
    registerType("gloamingforest:canopy_red_smooth_mushroom", [](FeatureContext const& c) { return Canopy(c, Head::Smooth).place(); });
    registerType("gloamingforest:canopy_red_spheroid_mushroom", [](FeatureContext const& c) { return Canopy(c, Head::Spheroid).place(); });
    registerType("gloamingforest:canopy_red_flat_mushroom", [](FeatureContext const& c) { return Canopy(c, Head::Flat).place(); });
    gf::trees_ext::registerVanillaHugeMushrooms();
}

} // namespace gf::feature::trees

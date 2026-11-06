// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/BigMushgloomFeature.java
//   src/main/java/twilightforest/world/components/feature/trollcave/TrollBigMushgloomFeature.java
//   src/main/java/twilightforest/world/components/feature/trollcave/TrollHugeBrownMushroomFeature.java
//   src/main/java/twilightforest/world/components/feature/trollcave/TrollHugeRedMushroomFeature.java
//   src/main/java/twilightforest/world/components/feature/UndergroundPlantFeature.java
//   src/main/java/twilightforest/util/features/FeatureLogic.java (mushroom helpers)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U27's feature types (F5 registerType). The mushroom features follow Minecraft 1.21.1's
// AbstractHugeMushroomFeature#place (tree height, isValidPosition, makeCap, placeTrunk) with TF's
// own caps. Java's six huge mushroom faces are TF's own block states on the mushgloom blocks; the
// vanilla caps and stems take Bedrock's huge_mushroom_bits nearest to the faces.
#include "feature/caves/Caves.h"

#include <climits>
#include <string>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "feature/ext/caves/W0Internals.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "game/Blocks.h"

namespace gf::feature::caves {

namespace kit = gf::features::kit;
using gf::features::FeatureContext;
using Json = nlohmann::json;

namespace {

struct Faces {
    bool up = false, down = false, north = false, east = false, south = false, west = false;
};

/// A provider's state (simple_state_provider): the Java id and its six faces.
struct MushroomState {
    std::string id;
    Faces       faces;
    bool        hasUp = false, hasHorizontal = false;
};

MushroomState stateOf(Json const& provider) {
    MushroomState s;
    Json const*   st = provider.is_object() && provider.contains("state") ? &provider["state"] : &provider;
    if (!st->is_object()) return s;
    s.id = st->value("Name", std::string());
    if (st->contains("Properties") && (*st)["Properties"].is_object()) {
        auto const& p   = (*st)["Properties"];
        auto        b   = [&](char const* k) { return p.contains(k) && p[k].is_string() && p[k].get<std::string>() == "true"; };
        s.hasUp         = p.contains("up");
        s.hasHorizontal = p.contains("north") && p.contains("south") && p.contains("east") && p.contains("west");
        s.faces         = {b("up"), b("down"), b("north"), b("east"), b("south"), b("west")};
    }
    return s;
}

/// The Bedrock huge_mushroom_bits nearest to Java's faces (cap: 0 pores .. 9 corners, 14 all cap;
/// stem: 10 sides, 15 all stem).
int mushroomBits(Faces const& f, bool stem) {
    if (stem) return (f.up || f.down) ? 15 : 10;
    struct Entry {
        int   bits;
        Faces f;
    };
    // {up, down, north, east, south, west}
    static Entry const table[] = {
        {0, {false, false, false, false, false, false}}, {1, {true, false, true, false, false, true}}, {2, {true, false, true, false, false, false}},
        {3, {true, false, true, true, false, false}},    {4, {true, false, false, false, false, true}}, {5, {true, false, false, false, false, false}},
        {6, {true, false, false, true, false, false}},   {7, {true, false, false, false, true, true}},  {8, {true, false, false, false, true, false}},
        {9, {true, false, false, true, true, false}},    {14, {true, true, true, true, true, true}},
    };
    int best = 0, bestScore = INT_MAX;
    for (auto const& e : table) {
        int miss  = (f.up && !e.f.up) + (f.north && !e.f.north) + (f.east && !e.f.east) + (f.south && !e.f.south) + (f.west && !e.f.west) + (f.down && !e.f.down);
        int extra = (!f.up && e.f.up) + (!f.north && e.f.north) + (!f.east && e.f.east) + (!f.south && e.f.south) + (!f.west && e.f.west) + (!f.down && e.f.down);
        int score = (miss + extra) * 4 + miss;
        if (score < bestScore) bestScore = score, best = e.bits;
    }
    return best;
}

/// The Bedrock block of a Java huge mushroom state with these faces.
Block const* mushroomBlock(MushroomState const& s, Faces const& f) {
    if (s.id.rfind("twilightforest:", 0) == 0) {
        Json states{{"twilightforest:up", f.up},     {"twilightforest:down", f.down}, {"twilightforest:north", f.north},
                    {"twilightforest:south", f.south}, {"twilightforest:east", f.east}, {"twilightforest:west", f.west}};
        if (auto const* b = game::blockWithStates(s.id, states)) return b;
        return kit::javaBlock(s.id);
    }
    Block const* base = kit::javaBlock(s.id);
    if (!base || !s.hasUp) return base;
    bool stem = s.id == "minecraft:mushroom_stem";
    if (auto const* b = game::blockWithStates(base->getTypeName(), Json{{"huge_mushroom_bits", mushroomBits(f, stem)}})) return b;
    return base;
}

BlockPos at(BlockPos const& p, int dx, int dy, int dz) { return {p.x + dx, p.y + dy, p.z + dz}; }

/// BlockState#isSolidRender (a full opaque cube).
bool solidRender(Block const& b) {
    if (b.isAir() || kit::isLeaves(b) || kit::isFluid(b)) return false;
    return kit::blocksMotion(b);
}

bool cannotReplace(Block const& b) { return kit::hasTag(b, "minecraft:features_cannot_replace"); }

void setBlock(FeatureContext const& c, BlockPos const& p, Block const* b) {
    if (b) c.region.setBlock(p, *b, 3);
}

/// AbstractHugeMushroomFeature#getTreeHeight (vanilla).
int vanillaTreeHeight(gf::features::RandomSource& random) {
    int i = random.nextInt(3) + 4;
    if (random.nextInt(12) == 0) i *= 2;
    return i;
}

/// AbstractHugeMushroomFeature#isValidPosition with the feature's getTreeRadiusForHeight(-1, -1, r, y).
template <class RadiusFn>
bool validPosition(FeatureContext const& c, BlockPos const& pos, int maxHeight, int foliageRadius, RadiusFn radius) {
    if (!(pos.y >= c.region.minY() + 1 && pos.y + maxHeight + 1 < c.region.maxY())) return false;
    Block const& below = c.region.getBlock(at(pos, 0, -1, 0));
    if (!kit::hasTag(below, "minecraft:dirt") && !kit::hasTag(below, "minecraft:mushroom_grow_block")) return false;
    for (int j = 0; j <= maxHeight; j++) {
        int k = radius(foliageRadius, j);
        for (int l = -k; l <= k; l++)
            for (int i1 = -k; i1 <= k; i1++) {
                Block const& b = c.region.getBlock(at(pos, l, j, i1));
                if (!b.isAir() && !kit::hasTag(b, "minecraft:leaves")) return false;
            }
    }
    return true;
}

/// AbstractHugeMushroomFeature#placeTrunk.
void placeTrunk(FeatureContext const& c, MushroomState const& stem, int maxHeight) {
    for (int i = 0; i < maxHeight; i++) {
        BlockPos p = at(c.origin, 0, i, 0);
        if (!solidRender(c.region.getBlock(p))) setBlock(c, p, mushroomBlock(stem, stem.faces));
    }
}

MushroomState capOf(FeatureContext const& c) { return stateOf(c.config.contains("cap_provider") ? c.config["cap_provider"] : Json()); }
MushroomState stemOf(FeatureContext const& c) { return stateOf(c.config.contains("stem_provider") ? c.config["stem_provider"] : Json()); }

/// BigMushgloomFeature / TrollBigMushgloomFeature.
bool bigMushgloom(FeatureContext const& c, bool troll) {
    MushroomState cap  = capOf(c);
    MushroomState stem = stemOf(c);
    int           r    = c.config.value("foliage_radius", 2);
    int           h    = 2 + c.random.nextInt(2); // getTreeHeight
    // getTreeRadiusForHeight(i, i1, foliageRadius, y): y <= 2 ? 0 : foliageRadius
    if (!validPosition(c, c.origin, h, r, [](int fr, int y) { return y <= 2 ? 0 : fr; })) return false;
    // makeCap
    int capHeight = c.random.nextBoolean() ? 1 : 2;
    for (int y = 0; y < capHeight; y++)
        for (int x = -r; x <= r; ++x)
            for (int z = -r; z <= r; ++z) {
                BlockPos     p = at(c.origin, x, h + y, z);
                Block const& b = c.region.getBlock(p);
                if (troll ? cannotReplace(b) : solidRender(b)) continue;
                Faces f = cap.faces;
                if (cap.hasUp && cap.hasHorizontal) // FeatureLogic.getSphericalMushroomBlockState
                    f = {y >= capHeight - 1, cap.faces.down, z == -r, x == r, z == r, x == -r};
                setBlock(c, p, mushroomBlock(cap, f));
            }
    placeTrunk(c, stem, h);
    return true;
}

bool isEdge(int x, int edge) { return x == edge || x == -edge; }

/// TrollHugeBrownMushroomFeature (HugeBrownMushroomFeature with TF's cap).
bool trollHugeBrown(FeatureContext const& c) {
    MushroomState cap  = capOf(c);
    MushroomState stem = stemOf(c);
    int           r    = c.config.value("foliage_radius", 2);
    int           h    = vanillaTreeHeight(c.random);
    if (!validPosition(c, c.origin, h, r, [](int fr, int y) { return y <= 3 ? 0 : fr; })) return false;
    for (int x = -r; x <= r; x++)
        for (int z = -r; z <= r; z++) {
            if (isEdge(x, r) && isEdge(z, r)) continue; // FeatureLogic.isCornerInSquare
            BlockPos p = at(c.origin, x, h, z);
            if (cannotReplace(c.region.getBlock(p))) continue;
            Faces f = cap.faces;
            if (cap.hasHorizontal) { // FeatureLogic.getHorizontalMushroomBlockState
                f.west  = x == -r || (isEdge(z, r) && x == 1 - r);
                f.east  = x == r || (isEdge(z, r) && x == r - 1);
                f.north = z == -r || (isEdge(x, r) && z == 1 - r);
                f.south = z == r || (isEdge(x, r) && z == r - 1);
            }
            setBlock(c, p, mushroomBlock(cap, f));
        }
    placeTrunk(c, stem, h);
    return true;
}

/// TrollHugeRedMushroomFeature (HugeRedMushroomFeature with TF's cap).
bool trollHugeRed(FeatureContext const& c) {
    MushroomState cap  = capOf(c);
    MushroomState stem = stemOf(c);
    int           fr   = c.config.value("foliage_radius", 2);
    int           h    = vanillaTreeHeight(c.random);
    // HugeRedMushroomFeature#getTreeRadiusForHeight(-1, -1, r, y): 0 (the cap band lies at -1)
    if (!validPosition(c, c.origin, h, fr, [](int, int) { return 0; })) return false;
    for (int y = h - 3; y <= h; y++) {
        int foliageRadius = y < h ? fr : fr - 1;
        int innerRadius   = fr - 2;
        for (int x = -foliageRadius; x <= foliageRadius; x++)
            for (int z = -foliageRadius; z <= foliageRadius; z++) {
                if (!(y >= h || isEdge(x, foliageRadius) != isEdge(z, foliageRadius))) continue;
                BlockPos p = at(c.origin, x, y, z);
                if (cannotReplace(c.region.getBlock(p))) continue;
                Faces f = cap.faces;
                if (cap.hasUp && cap.hasHorizontal) f = {y >= h - 1, cap.faces.down, z < -innerRadius, x > innerRadius, z > innerRadius, x < -innerRadius};
                setBlock(c, p, mushroomBlock(cap, f));
            }
    }
    placeTrunk(c, stem, h);
    return true;
}

/// UndergroundPlantFeature(config, spawnInStructure = true) ("twilightforest:troll_vines").
bool trollVines(FeatureContext const& c) {
    Json const& stateJson = c.config.contains("state") ? c.config["state"] : Json();
    std::string baseId    = stateJson.is_object() ? stateJson.value("Name", std::string()) : std::string();
    int         x = c.origin.x, z = c.origin.z;
    int         placed = 0;
    for (int y = c.origin.y; y > c.region.minY(); y--) {
        BlockPos pos{x, y, z};
        if (!c.region.getBlock(pos).isAir() || c.random.nextInt(6) == 0) {
            int a = c.random.nextInt(4);
            int b = c.random.nextInt(4);
            x     = c.origin.x + a - b;
            a     = c.random.nextInt(4);
            b     = c.random.nextInt(4);
            z     = c.origin.z + a - b;
            continue;
        }
        std::string id = baseId;
        if (id == kTrollvidr && c.random.nextInt(10) == 0) id = kUnripeTrollber;
        Block const* state = id == baseId ? kit::javaState(stateJson) : kit::javaBlock(id);
        if (!state) continue;
        // TrollRootBlock#canSurvive (another plant: its own canSurvive)
        bool survives = (id == kTrollvidr || id == kTrollber || id == kUnripeTrollber)
                          ? [&] {
                                Block const& above = c.region.getBlock(BlockPos{x, y + 1, z});
                                auto         aid   = kit::javaId(above);
                                return aid == kTrollvidr || aid == kTrollber || aid == kUnripeTrollber || kit::hasTag(above, "minecraft:base_stone_overworld");
                            }()
                          : kit::canSurvive(c.region, pos, *state);
        if (survives) {
            c.region.setBlock(pos, *state, 2 | 16); // UPDATE_CLIENTS | UPDATE_KNOWN_SHAPE
            placed++;
        }
    }
    return placed > 0;
}

} // namespace

void installFeatures() {
    gf::features::registerType("twilightforest:big_mushgloom", [](FeatureContext const& c) { return bigMushgloom(c, false); });
    gf::features::registerType("twilightforest:troll_cave_big_mushgloom", [](FeatureContext const& c) { return bigMushgloom(c, true); });
    gf::features::registerType("twilightforest:troll_cave_huge_brown_mushroom", trollHugeBrown);
    gf::features::registerType("twilightforest:troll_cave_huge_red_mushroom", trollHugeRed);
    gf::features::registerType("twilightforest:troll_vines", trollVines);
}

} // namespace gf::feature::caves

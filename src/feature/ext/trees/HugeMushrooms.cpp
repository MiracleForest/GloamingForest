// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Contract extension for U3 (rule R18), see HugeMushrooms.h. Minecraft 1.21.1's
// AbstractHugeMushroomFeature, HugeBrownMushroomFeature and HugeRedMushroomFeature, which upstream's
// vanilla_mushrooms selector places.
#include "feature/ext/trees/HugeMushrooms.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "game/Blocks.h"

namespace gf::trees_ext {

namespace kit = gf::features::kit;
using gf::features::FeatureContext;
using Json = nlohmann::json;

MushroomState mushroomStateOf(Json const& provider) {
    MushroomState s;
    Json const*   state = provider.is_object() && provider.contains("state") ? &provider["state"] : &provider;
    if (!state->is_object()) return s;
    s.id = state->value("Name", std::string());
    if (state->contains("Properties") && (*state)["Properties"].is_object()) {
        auto const& p = (*state)["Properties"];
        auto        b = [&](char const* k) { return p.contains(k) && p[k].is_string() && p[k].get<std::string>() == "true"; };
        s.hasFaces    = p.contains("north") && p.contains("up");
        s.faces       = {b("up"), b("down"), b("north"), b("east"), b("south"), b("west")};
    }
    return s;
}

int mushroomBits(Faces const& f, bool stem) {
    if (stem) return (f.up || f.down) ? 15 : 10;
    if (f.up && f.down && f.north && f.east && f.south && f.west) return 14;
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
    int best = 0, bestScore = 1 << 30;
    for (auto const& e : table) {
        // the fewest differing faces, then the fewest Java faces left without the cap skin
        int miss  = (f.up && !e.f.up) + (f.north && !e.f.north) + (f.east && !e.f.east) + (f.south && !e.f.south) + (f.west && !e.f.west) + (f.down && !e.f.down);
        int extra = (!f.up && e.f.up) + (!f.north && e.f.north) + (!f.east && e.f.east) + (!f.south && e.f.south) + (!f.west && e.f.west) + (!f.down && e.f.down);
        int score = (miss + extra) * 4 + miss;
        if (score < bestScore) {
            bestScore = score;
            best      = e.bits;
        }
    }
    return best;
}

Block const* mushroomBlock(MushroomState const& s, Faces const& faces) {
    Block const* base = kit::javaBlock(s.id);
    if (!base) return nullptr;
    if (!s.hasFaces) return base;
    bool stem = s.id == "minecraft:mushroom_stem";
    if (auto const* b = game::blockWithStates(base->getTypeName(), Json{{"huge_mushroom_bits", mushroomBits(faces, stem)}})) return b;
    return base;
}

namespace {

BlockPos at(BlockPos const& p, int dx, int dy, int dz) { return {p.x + dx, p.y + dy, p.z + dz}; }

/// BlockState#isSolidRender (a full opaque cube), as U3's tree helpers approximate it.
bool solidRender(Block const& b) {
    if (b.isAir() || kit::isLeaves(b) || kit::isFluid(b)) return false;
    return kit::blocksMotion(b);
}

using RadiusFn = int (*)(int foliageRadius, int y);

/// AbstractHugeMushroomFeature#isValidPosition.
bool validPosition(FeatureContext const& c, BlockPos const& pos, int maxHeight, int foliageRadius, RadiusFn radius) {
    if (pos.y < c.region.minY() + 1 || pos.y + maxHeight + 1 >= c.region.maxY()) return false;
    auto const& under = c.region.getBlock(at(pos, 0, -1, 0));
    if (!kit::hasTag(under, "minecraft:dirt") && !kit::hasTag(under, "minecraft:mushroom_grow_block")) return false;
    for (int j = 0; j <= maxHeight; ++j) {
        int k = radius(foliageRadius, j);
        for (int l = -k; l <= k; ++l)
            for (int i1 = -k; i1 <= k; ++i1) {
                auto const& b = c.region.getBlock(at(pos, l, j, i1));
                if (!b.isAir() && !kit::hasTag(b, "minecraft:leaves")) return false;
            }
    }
    return true;
}

bool vanillaMushroom(FeatureContext const& c, bool red) {
    MushroomState cap  = mushroomStateOf(c.config.contains("cap_provider") ? c.config["cap_provider"] : Json());
    MushroomState stem = mushroomStateOf(c.config.contains("stem_provider") ? c.config["stem_provider"] : Json());
    int           r    = c.config.value("foliage_radius", 2);
    auto          set  = [&](BlockPos const& p, MushroomState const& s, Faces const& f) {
        if (auto const* b = mushroomBlock(s, f)) c.region.setBlock(p, *b, 3);
    };
    int height = c.random.nextInt(3) + 4; // AbstractHugeMushroomFeature#getTreeHeight
    if (c.random.nextInt(12) == 0) height *= 2;
    // getTreeRadiusForHeight(-1, -1, foliageRadius, y) as isValidPosition calls it: the red one's
    // cap band lies at the height -1, so it checks the trunk column only
    RadiusFn radius = red ? RadiusFn([](int, int) { return 0; }) : RadiusFn([](int fr, int y) { return y <= 3 ? 0 : fr; });
    if (!validPosition(c, c.origin, height, r, radius)) return false;
    BlockPos const& pos = c.origin;
    if (red) {
        // HugeRedMushroomFeature#makeCap
        for (int i = height - 3; i <= height; ++i) {
            int j = i < height ? r : r - 1;
            int k = r - 2;
            for (int l = -j; l <= j; ++l)
                for (int i1 = -j; i1 <= j; ++i1) {
                    bool f4 = l == -j || l == j, f5 = i1 == -j || i1 == j;
                    if (!(i >= height || f4 != f5)) continue;
                    BlockPos p = at(pos, l, i, i1);
                    if (solidRender(c.region.getBlock(p))) continue;
                    Faces f = cap.faces;
                    if (cap.hasFaces) f = {i >= height - 1, cap.faces.down, i1 < -k, l > k, i1 > k, l < -k};
                    set(p, cap, f);
                }
        }
    } else {
        // HugeBrownMushroomFeature#makeCap
        for (int j = -r; j <= r; ++j)
            for (int k = -r; k <= r; ++k) {
                bool f = j == -r, f1 = j == r, f2 = k == -r, f3 = k == r;
                bool f4 = f || f1, f5 = f2 || f3;
                if (f4 && f5) continue;
                BlockPos p = at(pos, j, height, k);
                if (solidRender(c.region.getBlock(p))) continue;
                Faces faces = cap.faces;
                if (cap.hasFaces) {
                    faces.west  = f || (f5 && j == 1 - r);
                    faces.east  = f1 || (f5 && j == r - 1);
                    faces.north = f2 || (f4 && k == 1 - r);
                    faces.south = f3 || (f4 && k == r - 1);
                }
                set(p, cap, faces);
            }
    }
    // AbstractHugeMushroomFeature#placeTrunk
    for (int i = 0; i < height; ++i) {
        BlockPos p = at(pos, 0, i, 0);
        if (!solidRender(c.region.getBlock(p))) set(p, stem, stem.faces);
    }
    return true;
}

} // namespace

void registerVanillaHugeMushrooms() {
    gf::features::registerType("minecraft:huge_brown_mushroom", [](FeatureContext const& c) { return vanillaMushroom(c, false); });
    gf::features::registerType("minecraft:huge_red_mushroom", [](FeatureContext const& c) { return vanillaMushroom(c, true); });
}

} // namespace gf::trees_ext

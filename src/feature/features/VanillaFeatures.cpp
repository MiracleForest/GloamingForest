// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/WeightedListFeature.java
//   src/main/java/twilightforest/world/components/feature/config/WeightedListFeatureConfig.java
//   src/main/java/twilightforest/world/components/placements/AvoidLandmarkModifier.java
//   src/main/java/twilightforest/world/components/placements/ChunkCenterModifier.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Together with the vanilla feature types and placement modifiers of Minecraft 1.21.1 that the
// imported worldgen JSON uses (behaviour-compatible implementations, F5): random_selector,
// simple_random_selector, random_boolean_selector, simple_block, block_column, ore, scattered_ore,
// random_patch, flower, no_op, disk, freeze_top_layer, vines, seagrass, forest_rock; count,
// count_on_every_layer, rarity_filter, in_square, heightmap, height_range, biome,
// block_predicate_filter, environment_scan, surface_water_depth_filter,
// surface_relative_threshold_filter, random_offset, noise_based_count, noise_threshold_count.
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"
#include "core/noise/PerlinSimplex.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/features/Internal.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Internal.h"

#pragma clang fp contract(off)

namespace gf::features::detail {

using namespace kit;
namespace mth = core::mth;
using H       = worldgen::Heightmap;

namespace {

BlockPos at(BlockPos const& p, int dx, int dy, int dz) { return {p.x + dx, p.y + dy, p.z + dz}; }

/// Direction by Java name -> step.
BlockPos stepOf(std::string const& d) {
    if (d == "down") return {0, -1, 0};
    if (d == "up") return {0, 1, 0};
    if (d == "north") return {0, 0, -1};
    if (d == "south") return {0, 0, 1};
    if (d == "west") return {-1, 0, 0};
    return {1, 0, 0};
}

bool isEmptyBlock(Region& region, BlockPos const& p) { return region.getBlock(p).isAir(); }

/// A DoublePlantBlock (with its subclasses TallFlowerBlock, TallSeagrassBlock, PitcherCropBlock,
/// SmallDripleafBlock): the Java id has an upper half.
bool isDoublePlant(std::string const& javaId) {
    static std::set<std::string> const ids{"minecraft:tall_grass", "minecraft:large_fern",   "minecraft:sunflower",      "minecraft:lilac",
                                           "minecraft:rose_bush",  "minecraft:peony",        "minecraft:pitcher_plant",  "minecraft:small_dripleaf",
                                           "minecraft:tall_seagrass", "minecraft:pitcher_crop"};
    return ids.contains(javaId);
}

/// Java's Math.round(float): the closest int, ties towards positive infinity.
int javaRound(float a) { return static_cast<int>(std::floor(static_cast<double>(a) + 0.5)); }

/// OreFeature#shouldSkipAirCheck.
bool shouldSkipAirCheck(RandomSource& random, float chance) {
    if (chance <= 0.0F) return true;
    return chance >= 1.0F ? false : random.nextFloat() >= chance;
}

/// OreFeature#isAdjacentToAir (Direction.values(): down, up, north, south, west, east).
bool isAdjacentToAir(Region& region, BlockPos const& p) {
    static int const d[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (auto const& s : d)
        if (region.getBlock(at(p, s[0], s[1], s[2])).isAir()) return true;
    return false;
}

/// OreFeature#canPlaceOre for one target of the configuration.
bool canPlaceOre(Region& region, Block const& state, RandomSource& random, float discard, Json const& target, BlockPos const& pos) {
    if (!testRule(fieldObj(target, "target"), state, random)) return false;
    return shouldSkipAirCheck(random, discard) ? true : !isAdjacentToAir(region, pos);
}

// ---- features --------------------------------------------------------------------------------------

bool simpleBlock(FeatureContext const& c) {
    Block const* state = sampleState(field(c.config, "to_place"), c.random, c.origin);
    if (!state || !canSurvive(c.region, c.origin, *state)) return false;
    auto id = javaId(*state);
    if (isDoublePlant(id)) {
        BlockPos up = at(c.origin, 0, 1, 0);
        if (!isEmptyBlock(c.region, up)) return false;
        c.region.setBlock(c.origin, withProperty(*state, "half", "lower"), 2);
        c.region.setBlock(up, withProperty(*state, "half", "upper"), 2);
    } else {
        c.region.setBlock(c.origin, *state, 2);
    }
    return true;
}

bool blockColumn(FeatureContext const& c) {
    auto const&      layers = fieldArr(c.config, "layers");
    std::size_t      n      = layers.size();
    std::vector<int> heights(n);
    int              total = 0;
    for (std::size_t k = 0; k < n; ++k) {
        heights[k] = sampleInt(layers[k].value("height", Json(0)), c.random);
        total += heights[k];
    }
    if (total == 0) return false;
    BlockPos dir  = stepOf(c.config.value("direction", std::string("up")));
    bool     tip  = c.config.value("prioritize_tip", false);
    auto     allowed = fieldObj(c.config, "allowed_placement");
    BlockPos check = at(c.origin, dir.x, dir.y, dir.z);
    for (int l = 0; l < total; ++l) {
        if (!testPredicate(allowed, c.region, check)) {
            // truncate(layerHeights, totalHeight, truncatedHeight, prioritizeTip)
            int i  = total - l;
            int j  = tip ? 1 : -1;
            int k  = tip ? 0 : static_cast<int>(n) - 1;
            int e  = tip ? static_cast<int>(n) : -1;
            for (int i1 = k; i1 != e && i > 0; i1 += j) {
                int k1 = std::min(heights[static_cast<std::size_t>(i1)], i);
                i -= k1;
                heights[static_cast<std::size_t>(i1)] -= k1;
            }
            break;
        }
        check = at(check, dir.x, dir.y, dir.z);
    }
    BlockPos m = c.origin;
    for (std::size_t i1 = 0; i1 < n; ++i1) {
        for (int k1 = 0; k1 < heights[i1]; ++k1) {
            if (auto const* b = sampleState(field(layers[i1], "provider"), c.random, m)) c.region.setBlock(m, *b, 2);
            m = at(m, dir.x, dir.y, dir.z);
        }
    }
    return true;
}

bool randomPatch(FeatureContext const& c) {
    int  placed = 0;
    int  j      = c.config.value("xz_spread", 7) + 1;
    int  k      = c.config.value("y_spread", 3) + 1;
    int  tries  = c.config.value("tries", 128);
    auto const& feature = field(c.config, "feature");
    for (int l = 0; l < tries; ++l) {
        int dx0 = c.random.nextInt(j);
        int dx1 = c.random.nextInt(j);
        int dy0 = c.random.nextInt(k);
        int dy1 = c.random.nextInt(k);
        int dz0 = c.random.nextInt(j);
        int dz1 = c.random.nextInt(j);
        if (placePlaced(feature, c.region, at(c.origin, dx0 - dx1, dy0 - dy1, dz0 - dz1), c.random)) ++placed;
    }
    return placed > 0;
}

bool randomSelector(FeatureContext const& c) {
    for (auto const& w : fieldArr(c.config, "features")) {
        if (c.random.nextFloat() < w.value("chance", 0.0F)) return placePlaced(field(w, "feature"), c.region, c.origin, c.random);
    }
    return placePlaced(field(c.config, "default"), c.region, c.origin, c.random);
}

bool simpleRandomSelector(FeatureContext const& c) {
    auto const& fs = fieldArr(c.config, "features");
    if (fs.is_string()) return placePlaced(fs, c.region, c.origin, c.random); // a tag of placed features is not used by TF
    if (!fs.is_array() || fs.empty()) return false;
    int i = c.random.nextInt(static_cast<int>(fs.size()));
    return placePlaced(fs[static_cast<std::size_t>(i)], c.region, c.origin, c.random);
}

bool randomBooleanSelector(FeatureContext const& c) {
    bool flag = c.random.nextBoolean();
    return placePlaced(c.config.value(flag ? "feature_true" : "feature_false", Json()), c.region, c.origin, c.random);
}

/// TF's WeightedListFeature: one weighted pick of a placed feature, placed at the origin.
bool weightedList(FeatureContext const& c) {
    // the config is the SimpleWeightedRandomList itself: [{data: placed feature, weight}]
    auto const* f = weightedPick(c.config.is_array() ? c.config : fieldArr(c.config, "features"), c.random);
    if (!f) return false;
    return placePlaced(*f, c.region, c.origin, c.random);
}

bool ore(FeatureContext const& c) {
    int   size = c.config.value("size", 0);
    float discard = c.config.value("discard_chance_on_air_exposure", 0.0F);
    auto const& targets = fieldArr(c.config, "targets");
    auto& random = c.random;
    BlockPos const& o = c.origin;
    float  f  = random.nextFloat() * static_cast<float>(std::numbers::pi);
    float  f1 = static_cast<float>(size) / 8.0F;
    int    i  = mth::ceil(static_cast<double>((static_cast<float>(size) / 16.0F * 2.0F + 1.0F) / 2.0F));
    double minX = static_cast<double>(o.x) + std::sin(static_cast<double>(f)) * static_cast<double>(f1);
    double maxX = static_cast<double>(o.x) - std::sin(static_cast<double>(f)) * static_cast<double>(f1);
    double minZ = static_cast<double>(o.z) + std::cos(static_cast<double>(f)) * static_cast<double>(f1);
    double maxZ = static_cast<double>(o.z) - std::cos(static_cast<double>(f)) * static_cast<double>(f1);
    double minY = static_cast<double>(o.y + random.nextInt(3) - 2);
    double maxY = static_cast<double>(o.y + random.nextInt(3) - 2);
    int    cf1  = mth::ceil(static_cast<double>(f1));
    int    x    = o.x - cf1 - i;
    int    y    = o.y - 2 - i;
    int    z    = o.z - cf1 - i;
    int    width  = 2 * (cf1 + i);
    int    height = 2 * (2 + i);
    bool   any    = false;
    for (int l1 = x; l1 <= x + width && !any; ++l1)
        for (int i2 = z; i2 <= z + width && !any; ++i2)
            if (y <= c.region.height(H::OceanFloorWg, l1, i2)) any = true;
    if (!any) return false;

    // doPlace
    int                 placed = 0;
    std::vector<bool>   bits(static_cast<std::size_t>(width * height * width));
    std::vector<double> a(static_cast<std::size_t>(size * 4));
    for (int k = 0; k < size; ++k) {
        float  t  = static_cast<float>(k) / static_cast<float>(size);
        double d0 = mth::lerp(static_cast<double>(t), minX, maxX);
        double d1 = mth::lerp(static_cast<double>(t), minY, maxY);
        double d2 = mth::lerp(static_cast<double>(t), minZ, maxZ);
        double d3 = random.nextDouble() * static_cast<double>(size) / 16.0;
        double d4 = (static_cast<double>(core::jrand::mthSin(static_cast<float>(std::numbers::pi) * t) + 1.0F) * d3 + 1.0) / 2.0;
        a[static_cast<std::size_t>(k * 4 + 0)] = d0;
        a[static_cast<std::size_t>(k * 4 + 1)] = d1;
        a[static_cast<std::size_t>(k * 4 + 2)] = d2;
        a[static_cast<std::size_t>(k * 4 + 3)] = d4;
    }
    auto A = [&](int idx) -> double& { return a[static_cast<std::size_t>(idx)]; };
    for (int l3 = 0; l3 < size - 1; ++l3) {
        if (A(l3 * 4 + 3) <= 0.0) continue;
        for (int i4 = l3 + 1; i4 < size; ++i4) {
            if (A(i4 * 4 + 3) <= 0.0) continue;
            double d8  = A(l3 * 4 + 0) - A(i4 * 4 + 0);
            double d10 = A(l3 * 4 + 1) - A(i4 * 4 + 1);
            double d12 = A(l3 * 4 + 2) - A(i4 * 4 + 2);
            double d14 = A(l3 * 4 + 3) - A(i4 * 4 + 3);
            if (d14 * d14 > d8 * d8 + d10 * d10 + d12 * d12) {
                if (d14 > 0.0) A(i4 * 4 + 3) = -1.0;
                else A(l3 * 4 + 3) = -1.0;
            }
        }
    }
    for (int j4 = 0; j4 < size; ++j4) {
        double d9 = A(j4 * 4 + 3);
        if (d9 < 0.0) continue;
        double d11 = A(j4 * 4 + 0), d13 = A(j4 * 4 + 1), d15 = A(j4 * 4 + 2);
        int    k4 = std::max(mth::floor(d11 - d9), x);
        int    l  = std::max(mth::floor(d13 - d9), y);
        int    i1 = std::max(mth::floor(d15 - d9), z);
        int    j1 = std::max(mth::floor(d11 + d9), k4);
        int    k1 = std::max(mth::floor(d13 + d9), l);
        int    l1 = std::max(mth::floor(d15 + d9), i1);
        for (int i2 = k4; i2 <= j1; ++i2) {
            double d5 = (static_cast<double>(i2) + 0.5 - d11) / d9;
            if (!(d5 * d5 < 1.0)) continue;
            for (int j2 = l; j2 <= k1; ++j2) {
                double d6 = (static_cast<double>(j2) + 0.5 - d13) / d9;
                if (!(d5 * d5 + d6 * d6 < 1.0)) continue;
                for (int k2 = i1; k2 <= l1; ++k2) {
                    double d7 = (static_cast<double>(k2) + 0.5 - d15) / d9;
                    if (!(d5 * d5 + d6 * d6 + d7 * d7 < 1.0) || j2 < c.region.minY() || j2 >= c.region.maxY()) continue;
                    // Java's BitSet grows: indices past width * height * width (the far side of a
                    // large vein) are kept, not dropped; the loop bounds never give a negative one
                    int l2 = i2 - x + (j2 - y) * width + (k2 - z) * width * height;
                    if (l2 < 0) continue;
                    auto bit = static_cast<std::size_t>(l2);
                    if (bit >= bits.size()) bits.resize(bit + 1);
                    if (bits[bit]) continue;
                    bits[bit] = true;
                    BlockPos m{i2, j2, k2};
                    if (!c.region.isInside(i2, k2)) continue; // WorldGenRegion#ensureCanWrite
                    Block const& current = c.region.getBlock(m);
                    for (auto const& target : targets) {
                        if (!canPlaceOre(c.region, current, random, discard, target, m)) continue;
                        if (auto const* s = javaState(field(target, "state"))) {
                            c.region.setBlock(m, *s, 2);
                            ++placed;
                        }
                        break;
                    }
                }
            }
        }
    }
    return placed > 0;
}

/// ScatteredOreFeature: nextInt(size + 1) single blocks around the origin, each offset per axis by
/// round((nextFloat() - nextFloat()) * min(j, 7)).
bool scatteredOre(FeatureContext const& c) {
    auto&       random  = c.random;
    int         size    = c.config.value("size", 0);
    float       discard = c.config.value("discard_chance_on_air_exposure", 0.0F);
    auto const& targets = fieldArr(c.config, "targets");
    int         i       = random.nextInt(size + 1);
    for (int j = 0; j < i; ++j) {
        // offsetTargetPos: x, y, z in that order (getRandomPlacementInOneAxisRelativeToOrigin)
        int  range = std::min(j, 7);
        auto axis  = [&] {
            float a = random.nextFloat();
            float b = random.nextFloat();
            return javaRound((a - b) * static_cast<float>(range));
        };
        int         ox = axis();
        int         oy = axis();
        int         oz = axis();
        BlockPos    m  = at(c.origin, ox, oy, oz);
        Block const& state = c.region.getBlock(m);
        for (auto const& target : targets) {
            if (canPlaceOre(c.region, state, random, discard, target, m)) {
                if (auto const* s = javaState(field(target, "state"))) c.region.setBlock(m, *s, 2);
                break;
            }
        }
    }
    return true;
}

bool disk(FeatureContext const& c) {
    bool        flag = false;
    BlockPos const& o = c.origin;
    int         half  = c.config.value("half_height", 0);
    int         maxY  = o.y + half;
    int         minY  = o.y - half - 1;
    int         l     = sampleInt(c.config.value("radius", Json(0)), c.random);
    auto const& target   = fieldObj(c.config, "target");
    auto const& provider = fieldObj(c.config, "state_provider");
    for (int bz = o.z - l; bz <= o.z + l; ++bz) {
        for (int bx = o.x - l; bx <= o.x + l; ++bx) {
            int i1 = bx - o.x, j1 = bz - o.z;
            if (i1 * i1 + j1 * j1 > l * l) continue;
            bool col = false;
            for (int yy = maxY; yy > minY; --yy) {
                BlockPos p{bx, yy, bz};
                if (testPredicate(target, c.region, p)) {
                    if (auto const* s = sampleRuleState(provider, c.region, c.random, p)) c.region.setBlock(p, *s, 2);
                    col = true;
                }
            }
            flag |= col;
        }
    }
    return flag;
}

/// Biome#getTemperature(pos) (height adjusted) of biome `id`, from its imported JSON; a biome
/// without JSON (vanilla) uses `engineBase`, the engine biome's own temperature.
float biomeTemperature(std::string const& id, BlockPos const& pos, float engineBase) {
    float       base = engineBase;
    auto const& biomes = data().biomes;
    if (auto it = biomes.find(id); it != biomes.end()) {
        base = it->second.value("temperature", 0.8F);
        if (it->second.value("temperature_modifier", std::string("none")) == "frozen") {
            double d0 = core::noise::PerlinSimplexNoise::frozenTemperatureNoise().getValue(pos.x * 0.05, pos.z * 0.05, false) * 7.0;
            double d1 = core::noise::PerlinSimplexNoise::biomeInfoNoise().getValue(pos.x * 0.2, pos.z * 0.2, false);
            if (d0 + d1 < 0.3 && core::noise::PerlinSimplexNoise::biomeInfoNoise().getValue(pos.x * 0.09, pos.z * 0.09, false) < 0.8) base = 0.2F;
        }
    }
    if (pos.y > 80) {
        auto f1 = static_cast<float>(
            core::noise::PerlinSimplexNoise::temperatureNoise().getValue(static_cast<double>(static_cast<float>(pos.x) / 8.0F), static_cast<double>(static_cast<float>(pos.z) / 8.0F), false)
            * 8.0
        );
        return base - (f1 + static_cast<float>(pos.y) - 80.0F) * 0.05F / 40.0F;
    }
    return base;
}

/// LevelReader#getBrightness(LightLayer.BLOCK, pos): the engine's block light through the region's
/// BlockSource (0 for in-memory regions; during generation, as in Java, nothing is lit yet).
int blockLight(Region& region, BlockPos const& pos) {
    if (auto* src = blockSource(region)) return static_cast<int>(src->getBrightnessPair(pos).block->mValue);
    return 0;
}

/// A water source block (Java: the fluid is Fluids.WATER and the block a LiquidBlock, so neither
/// flowing water nor a waterlogged block): Bedrock water or flowing water at depth 0.
bool isWaterSource(Block const& block) {
    if (!isWater(block)) return false;
    auto depth = javaProperty(block, "liquid_depth");
    return !depth || *depth == "0";
}

bool freezeTopLayer(FeatureContext const& c) {
    static Block const* ice  = javaBlock("minecraft:ice");
    static Block const* snow = javaBlock("minecraft:snow");
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            int      k  = c.origin.x + i, l = c.origin.z + j;
            int      i1 = c.region.height(H::MotionBlocking, k, l);
            BlockPos m{k, i1, l};
            BlockPos m1{k, i1 - 1, l};
            // the biome of the top position m (LevelReader#getBiome: BiomeManager's zoom) decides
            // both (its temperature taken at m1 and m)
            auto  biome      = c.region.biome(m);
            float engineBase = 0.8F;
            if (auto* src = blockSource(c.region))
                if (auto const* b = worldgen::detail::engineBiome(src->getDimension().getBiomeRegistry(), biome)) engineBase = b->mTemperature;
            // Biome#shouldFreeze(level, m1, false)
            if (ice && biomeTemperature(biome, m1, engineBase) < 0.15F && m1.y >= c.region.minY() && m1.y < c.region.maxY() && blockLight(c.region, m1) < 10
                && isWaterSource(c.region.getBlock(m1)))
                c.region.setBlock(m1, *ice, 2);
            // Biome#shouldSnow(level, m); Java then sets the block below snowy, a state Bedrock's
            // grass / podzol / mycelium do not have (they render snowy from the snow above)
            if (snow && biomeTemperature(biome, m, engineBase) < 0.15F && m.y >= c.region.minY() && m.y < c.region.maxY() && blockLight(c.region, m) < 10) {
                auto const& here = c.region.getBlock(m);
                if ((here.isAir() || javaId(here) == "minecraft:snow") && canSurvive(c.region, m, *snow)) c.region.setBlock(m, *snow, 2);
            }
        }
    }
    return true;
}

bool vines(FeatureContext const& c) {
    if (!isEmptyBlock(c.region, c.origin)) return false;
    // Direction.values() without DOWN: UP, NORTH, SOUTH, WEST, EAST. Bedrock vines have no "up"
    // face: a vine under a full block keeps no side bits (it hangs from the block above).
    if (c.region.getBlock(at(c.origin, 0, 1, 0)).canProvideFullSupport(static_cast<uchar>(0))) {
        if (auto const* v = javaBlock("minecraft:vine")) c.region.setBlock(c.origin, *v, 2);
        return true;
    }
    static char const* const names[] = {"north", "south", "west", "east"};
    static int const         faces[] = {3, 2, 5, 4}; // the neighbour's face pointing back at the vine
    for (int d = 0; d < 4; ++d) {
        BlockPos s = stepOf(names[d]);
        BlockPos n = at(c.origin, s.x, s.y, s.z);
        if (c.region.getBlock(n).canProvideFullSupport(static_cast<uchar>(faces[d]))) {
            if (auto const* v = javaBlock("minecraft:vine", Json{{names[d], "true"}})) c.region.setBlock(c.origin, *v, 2);
            return true;
        }
    }
    return false;
}

bool seagrass(FeatureContext const& c) {
    int  i = c.random.nextInt(8) - c.random.nextInt(8);
    int  j = c.random.nextInt(8) - c.random.nextInt(8);
    int  k = c.region.height(H::OceanFloor, c.origin.x + i, c.origin.z + j);
    BlockPos p{c.origin.x + i, k, c.origin.z + j};
    if (javaId(c.region.getBlock(p)) != "minecraft:water") return false;
    bool tall = c.random.nextDouble() < static_cast<double>(c.config.value("probability", 0.0F));
    auto const* state = javaBlock(tall ? "minecraft:tall_seagrass" : "minecraft:seagrass", tall ? Json{{"half", "lower"}} : Json::object());
    if (!state || !canSurvive(c.region, p, *state)) return false;
    if (tall) {
        BlockPos up = at(p, 0, 1, 0);
        if (javaId(c.region.getBlock(up)) == "minecraft:water") {
            c.region.setBlock(p, *state, 2);
            if (auto const* u = javaBlock("minecraft:tall_seagrass", Json{{"half", "upper"}})) c.region.setBlock(up, *u, 2);
        }
    } else {
        c.region.setBlock(p, *state, 2);
    }
    return true;
}

bool forestRock(FeatureContext const& c) {
    auto const* state = javaState(field(c.config, "state"));
    if (!state) return false;
    BlockPos p = c.origin;
    while (p.y > c.region.minY() + 3) {
        BlockPos below = at(p, 0, -1, 0);
        if (!isEmptyBlock(c.region, below)) {
            auto const& b = c.region.getBlock(below);
            if (hasTag(b, "minecraft:dirt") || hasTag(b, "minecraft:base_stone_overworld")) break;
        }
        p = below;
    }
    if (p.y <= c.region.minY() + 3) return false;
    for (int i = 0; i < 3; ++i) {
        int   j = c.random.nextInt(2);
        int   k = c.random.nextInt(2);
        int   l = c.random.nextInt(2);
        float f = static_cast<float>(j + k + l) * 0.333F + 0.5F;
        for (int bz = p.z - l; bz <= p.z + l; ++bz)
            for (int by = p.y - k; by <= p.y + k; ++by)
                for (int bx = p.x - j; bx <= p.x + j; ++bx)
                    if (mth::distSqr(bx, by, bz, p.x, p.y, p.z) <= static_cast<double>(f * f)) c.region.setBlock({bx, by, bz}, *state, 3);
        int ox = -1 + c.random.nextInt(2);
        int oy = -c.random.nextInt(2);
        int oz = -1 + c.random.nextInt(2);
        p      = at(p, ox, oy, oz);
    }
    return true;
}

// ---- placement modifiers ---------------------------------------------------------------------------

using Positions = std::vector<BlockPos>;

/// CountOnEveryLayerPlacement#isEmpty: air, Blocks.WATER or Blocks.LAVA, whose flowing levels
/// Bedrock keeps as flowing_water / flowing_lava.
bool isEmptyForLayer(Block const& b) {
    if (b.isAir()) return true;
    return isWater(b) || isLava(b);
}

int findOnGroundYPosition(PlacementContext const& c, int x, int y, int z, int count) {
    BlockPos     m{x, y, z};
    int          i     = 0;
    Block const* state = &c.region.getBlock(m);
    for (int j = y; j >= c.region.minY() + 1; --j) {
        m.y                 = j - 1;
        Block const* below = &c.region.getBlock(m);
        if (!isEmptyForLayer(*below) && isEmptyForLayer(*state) && below->getTypeName() != "minecraft:bedrock") {
            if (i == count) return m.y + 1;
            ++i;
        }
        state = below;
    }
    return std::numeric_limits<int>::max();
}

/// The structure ids of a HolderSet<Structure> (structures_allowed): an id, a list of ids, or a
/// "#tag" of structures.
std::vector<std::string> structureKeys(Json const& set) {
    std::vector<std::string> out;
    auto add = [&](std::string const& entry) {
        if (entry.starts_with('#')) {
            for (auto& id : tags::members(tags::Registry::Structure, normalizeId(entry))) out.push_back(std::move(id));
        } else {
            out.push_back(normalizeId(entry));
        }
    };
    if (set.is_string()) add(set.get<std::string>());
    if (set.is_array())
        for (auto const& e : set)
            if (e.is_string()) add(e.get<std::string>());
    return out;
}

/// AvoidLandmarkModifier#getPositions: F6 decides from the starts referencing the position's chunk
/// (DecorationClearance, the pieces with UtilityPiece#allowFeatures, the start box's center).
Positions noStructure(PlacementContext const& c, BlockPos const& pos) {
    landmark::ext::Occupancy occupancy;
    occupancy.surface             = c.config.value("occupies_surface", false);
    occupancy.underground         = c.config.value("occupies_underground", false);
    occupancy.vegetation          = c.config.value("occupies_vegetation", false);
    occupancy.additionalClearance = c.config.value("additional_clearance", 0);
    if (auto it = c.config.find("structures_allowed"); it != c.config.end()) occupancy.structuresAllowed = structureKeys(*it);
    if (landmark::ext::featureBlocked(c.region.dimension(), pos.x, pos.y, pos.z, occupancy)) return {};
    return {pos};
}

} // namespace

void registerVanillaFeatures() {
    registerType("minecraft:simple_block", simpleBlock);
    registerType("minecraft:block_column", blockColumn);
    registerType("minecraft:random_patch", randomPatch);
    registerType("minecraft:flower", randomPatch);
    registerType("minecraft:no_bonemeal_flower", randomPatch);
    registerType("minecraft:random_selector", randomSelector);
    registerType("minecraft:simple_random_selector", simpleRandomSelector);
    registerType("minecraft:random_boolean_selector", randomBooleanSelector);
    registerType("minecraft:no_op", [](FeatureContext const&) { return true; });
    registerType("minecraft:ore", ore);
    registerType("minecraft:scattered_ore", scatteredOre);
    registerType("minecraft:disk", disk);
    registerType("minecraft:freeze_top_layer", freezeTopLayer);
    registerType("minecraft:vines", vines);
    registerType("minecraft:seagrass", seagrass);
    registerType("minecraft:forest_rock", forestRock);
    registerType("twilightforest:weighted_list", weightedList);
}

void registerVanillaPlacements() {
    registerPlacement("minecraft:count", [](PlacementContext const& c, BlockPos const& pos) {
        int n = sampleInt(c.config.value("count", Json(1)), c.random);
        return Positions(static_cast<std::size_t>(std::max(0, n)), pos);
    });
    registerPlacement("minecraft:count_on_every_layer", [](PlacementContext const& c, BlockPos const& pos) {
        Positions out;
        int       i = 0;
        bool      flag;
        do {
            flag = false;
            for (int j = 0; j < sampleInt(c.config.value("count", Json(1)), c.random); ++j) {
                int k  = c.random.nextInt(16) + pos.x;
                int l  = c.random.nextInt(16) + pos.z;
                int i1 = c.region.height(H::MotionBlocking, k, l);
                int j1 = findOnGroundYPosition(c, k, i1, l, i);
                if (j1 != std::numeric_limits<int>::max()) {
                    out.push_back({k, j1, l});
                    flag = true;
                }
            }
            ++i;
        } while (flag);
        return out;
    });
    registerPlacement("minecraft:rarity_filter", [](PlacementContext const& c, BlockPos const& pos) {
        int chance = c.config.value("chance", 1);
        return c.random.nextFloat() < 1.0F / static_cast<float>(chance) ? Positions{pos} : Positions{};
    });
    registerPlacement("minecraft:in_square", [](PlacementContext const& c, BlockPos const& pos) {
        int i = c.random.nextInt(16) + pos.x;
        int j = c.random.nextInt(16) + pos.z;
        return Positions{{i, pos.y, j}};
    });
    registerPlacement("minecraft:heightmap", [](PlacementContext const& c, BlockPos const& pos) {
        int k = c.region.height(heightmapOf(c.config.value("heightmap", std::string("MOTION_BLOCKING"))), pos.x, pos.z);
        return k > c.region.minY() ? Positions{{pos.x, k, pos.z}} : Positions{};
    });
    registerPlacement("minecraft:height_range", [](PlacementContext const& c, BlockPos const& pos) {
        return Positions{{pos.x, sampleHeight(field(c.config, "height"), c.random, c.region), pos.z}};
    });
    registerPlacement("minecraft:biome", [](PlacementContext const& c, BlockPos const& pos) {
        if (c.placedFeatureId.empty()) return Positions{pos}; // placed directly: no biome restriction
        return biomeHasFeature(c.region.biome(pos), c.placedFeatureId) ? Positions{pos} : Positions{};
    });
    registerPlacement("minecraft:block_predicate_filter", [](PlacementContext const& c, BlockPos const& pos) {
        return testPredicate(field(c.config, "predicate"), c.region, pos) ? Positions{pos} : Positions{};
    });
    registerPlacement("minecraft:environment_scan", [](PlacementContext const& c, BlockPos const& pos) {
        BlockPos    dir     = stepOf(c.config.value("direction_of_search", std::string("down")));
        auto const& target  = field(c.config, "target_condition");
        auto const& allowed = c.config.value("allowed_search_condition", Json{{"type", "minecraft:true"}});
        int         steps   = c.config.value("max_steps", 0);
        BlockPos    m       = pos;
        if (!testPredicate(allowed, c.region, m)) return Positions{};
        for (int i = 0; i < steps; ++i) {
            if (testPredicate(target, c.region, m)) return Positions{m};
            m = at(m, dir.x, dir.y, dir.z);
            if (m.y < c.region.minY() || m.y >= c.region.maxY()) return Positions{};
            if (!testPredicate(allowed, c.region, m)) break;
        }
        return testPredicate(target, c.region, m) ? Positions{m} : Positions{};
    });
    registerPlacement("minecraft:surface_water_depth_filter", [](PlacementContext const& c, BlockPos const& pos) {
        int i = c.region.height(H::OceanFloor, pos.x, pos.z);
        int j = c.region.height(H::WorldSurface, pos.x, pos.z);
        return j - i <= c.config.value("max_water_depth", 0) ? Positions{pos} : Positions{};
    });
    registerPlacement("minecraft:surface_relative_threshold_filter", [](PlacementContext const& c, BlockPos const& pos) {
        long long i = c.region.height(heightmapOf(c.config.value("heightmap", std::string("WORLD_SURFACE_WG"))), pos.x, pos.z);
        long long j = i + c.config.value("min_inclusive", std::numeric_limits<int>::min());
        long long k = i + c.config.value("max_inclusive", std::numeric_limits<int>::max());
        return j <= pos.y && pos.y <= k ? Positions{pos} : Positions{};
    });
    registerPlacement("minecraft:random_offset", [](PlacementContext const& c, BlockPos const& pos) {
        int i = pos.x + sampleInt(c.config.value("xz_spread", Json(0)), c.random);
        int j = pos.y + sampleInt(c.config.value("y_spread", Json(0)), c.random);
        int k = pos.z + sampleInt(c.config.value("xz_spread", Json(0)), c.random);
        return Positions{{i, j, k}};
    });
    registerPlacement("minecraft:noise_based_count", [](PlacementContext const& c, BlockPos const& pos) {
        double factor = c.config.value("noise_factor", 1.0);
        double d0     = core::noise::PerlinSimplexNoise::biomeInfoNoise().getValue(pos.x / factor, pos.z / factor, false);
        int    n      = static_cast<int>(std::ceil((d0 + c.config.value("noise_offset", 0.0)) * static_cast<double>(c.config.value("noise_to_count_ratio", 0))));
        return Positions(static_cast<std::size_t>(std::max(0, n)), pos);
    });
    registerPlacement("minecraft:noise_threshold_count", [](PlacementContext const& c, BlockPos const& pos) {
        double d0 = core::noise::PerlinSimplexNoise::biomeInfoNoise().getValue(pos.x / 200.0, pos.z / 200.0, false);
        int    n  = d0 < c.config.value("noise_level", 0.0) ? c.config.value("below_noise", 0) : c.config.value("above_noise", 0);
        return Positions(static_cast<std::size_t>(std::max(0, n)), pos);
    });
    registerPlacement("minecraft:carving_mask", [](PlacementContext const& c, BlockPos const& pos) {
        // CarvingMaskPlacement: every block the carvers of `step` carved in the chunk of pos (the
        // mask the pipeline keeps from the chunk's Carvers stage until its decoration). 1.21.1
        // carves only the "air" step (ChunkStatusTasks#generateCarvers): the "liquid" mask
        // (ProtoChunk#getOrCreateCarvingMask(LIQUID)) is always empty
        Positions out;
        auto const& stepJson = field(c.config, "step");
        std::string step     = stepJson.is_string() ? stepJson.get<std::string>() : std::string();
        if (step != "air") {
            if (step != "liquid") warnOnce("carving_mask step " + step, "minecraft:carving_mask without a valid step (\"" + step + "\"; Java refuses it)");
            return out;
        }
        int cx = pos.x >> 4, cz = pos.z >> 4;
        if (auto mask = worldgen::detail::carvingMaskOf(c.region.dimension(), cx, cz))
            mask->forEach([&](int lx, int y, int lz) { out.push_back({cx * 16 + lx, y, cz * 16 + lz}); });
        return out;
    });
    registerPlacement("twilightforest:chunk_centerer", [](PlacementContext const&, BlockPos const& pos) {
        return Positions{{(pos.x & static_cast<int>(0xfffffff0)) + 8, pos.y, (pos.z & static_cast<int>(0xfffffff0)) + 8}};
    });
    registerPlacement("twilightforest:no_structure", noStructure);
}

} // namespace gf::features::detail

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/TFCavesCarver.java
//   src/main/java/twilightforest/init/TFCaveCarvers.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TFCavesCarver (twilightforest:tf_caves and twilightforest:highland_caves) with the parts of
// Minecraft 1.21.1's WorldCarver it inherits (carveEllipsoid, canReach) and CarvingContext#
// topMaterial (the surface rules at the block below a carved surface). F5 calls a carver once per
// source chunk within 8 chunks with setLargeFeatureSeed(seed + index, source); the context carries
// the source chunk, the configured carver's JSON and the shared carving mask.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/Mth.h"
#include "core/landmark/Landmark.h"
#include "core/noise/Noise.h"
#include "feature/ext/terrain/Pipeline.h"
#include "feature/ext/worldgen/Carving.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/terrain/Terrain.h"

#pragma clang fp contract(off)

namespace gf::feature::terrain {

namespace kit = gf::features::kit;
namespace mth = gf::core::mth;
using Json    = nlohmann::json;
using core::jrand::RandomSource;

namespace {

// ---- block classes ----------------------------------------------------------------------------------------
enum Flags : std::uint16_t {
    kAir        = 1 << 0,
    kWater      = 1 << 1, ///< FluidTags.WATER
    kFluid      = 1 << 2, ///< any fluid state
    kReplace    = 1 << 3, ///< #twilightforest:carver_replaceables
    kIce        = 1 << 4, ///< #minecraft:ice
    kSurface    = 1 << 5, ///< grass block, mycelium, podzol, dirt path
    kDirt       = 1 << 6, ///< Blocks.DIRT
    kBaseStone  = 1 << 7, ///< #minecraft:base_stone_overworld
    kLayerFluid = 1 << 8, ///< the fluid is in Bedrock's liquid layer (a waterlogged block): a write must empty it
};

std::uint16_t flagsOf(State const& s, Block const& b) {
    thread_local std::uint64_t                                  owner = 0;
    thread_local std::unordered_map<Block const*, std::uint16_t> cache;
    if (owner != s.generation) {
        cache.clear();
        owner = s.generation;
    }
    if (auto it = cache.find(&b); it != cache.end()) return it->second;
    std::uint16_t f  = 0;
    auto          id = kit::javaId(b);
    if (kit::isAir(b)) f |= kAir;
    // BlockState#getFluidState: water and lava (still and flowing), and the blocks whose fluid state
    // is always water (bubble columns and the underwater plants Java builds waterlogged). A
    // waterlogged block keeps its water in Bedrock's liquid layer: Carver::flags adds that layer's
    // fluid (ext::LiquidLayer) for the game's chunks
    if (kit::isWater(b) || id == "minecraft:bubble_column" || id == "minecraft:kelp" || id == "minecraft:kelp_plant" || id == "minecraft:seagrass" ||
        id == "minecraft:tall_seagrass")
        f |= kWater | kFluid;
    if (kit::isLava(b)) f |= kFluid;
    if (kit::hasTag(b, "twilightforest:carver_replaceables")) f |= kReplace;
    if (id == "minecraft:ice" || id == "minecraft:packed_ice" || id == "minecraft:blue_ice" || id == "minecraft:frosted_ice" || kit::hasTag(b, "minecraft:ice")) f |= kIce;
    if (id == "minecraft:grass_block" || id == "minecraft:mycelium" || id == "minecraft:podzol" || id == "minecraft:dirt_path") f |= kSurface;
    if (id == "minecraft:dirt") f |= kDirt;
    if (kit::hasTag(b, "minecraft:base_stone_overworld")) f |= kBaseStone;
    cache.emplace(&b, f);
    return f;
}

constexpr std::array<std::array<int, 3>, 6> kDirections{{{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}}; // DOWN UP NORTH SOUTH WEST EAST

/// TFCavesCarver's wall providers (TFCaveCarvers): forest caves a NoiseProvider of dirts, highland
/// caves trollsteinn 1 / stone 3.
Json const& wallProvider(bool highlands) {
    static Json const forest = [] {
        Json dirt = {{"Name", "minecraft:dirt"}}, rooted = {{"Name", "minecraft:rooted_dirt"}}, coarse = {{"Name", "minecraft:coarse_dirt"}};
        return Json{{"type", "minecraft:noise_provider"},
                    {"seed", 6972119253061020355LL},
                    {"noise", {{"firstOctave", 0}, {"amplitudes", {1.0}}}},
                    {"scale", static_cast<double>(0.5F)},
                    {"states", {dirt, dirt, rooted, dirt, dirt, coarse, dirt, dirt}}};
    }();
    static Json const highland = Json{{"type", "minecraft:weighted_state_provider"},
                                      {"entries", {{{"data", {{"Name", "twilightforest:trollsteinn"}}}, {"weight", 1}}, {{"data", {{"Name", "minecraft:stone"}}}, {"weight", 3}}}}};
    return highlands ? highland : forest;
}

core::noise::ImprovedNoise const& roofNoise() {
    static core::noise::ImprovedNoise const n = [] {
        core::jrand::LegacyRandom r(6972119253061020355LL);
        return core::noise::ImprovedNoise(r);
    }();
    return n;
}

struct Carver {
    bool                         highlands;
    State const&                 s;
    worldgen::Chunk&             chunk;
    worldgen::ext::CarverContext& ctx;
    mutable std::unique_ptr<ChunkColumns> cols; ///< built when a surface block needs its top material
    Json const&                  config;
    int                          minY, maxY;      ///< the chunk's build range
    int                          genMinY, genDepth; ///< WorldGenerationContext
    Block const*                 caveAir;
    gf::terrain::ext::LiquidLayer liquid; ///< a game chunk's liquid layer, where waterlogged blocks hold their water

    Block const& get(int x, int y, int z) const {
        int lx = x - chunk.chunkX() * 16, lz = z - chunk.chunkZ() * 16;
        if (y < minY || y >= maxY || lx < 0 || lz < 0 || lx > 15 || lz > 15) return *s.block(code::Air);
        return chunk.getBlock(lx, y, lz);
    }
    /// `f`: flags() of the position written (its kLayerFluid says whether it holds a liquid layer).
    void set(int x, int y, int z, Block const& b, std::uint16_t f) const {
        int lx = x - chunk.chunkX() * 16, lz = z - chunk.chunkZ() * 16;
        if (y < minY || y >= maxY || lx < 0 || lz < 0 || lx > 15 || lz > 15) return;
        chunk.setBlock(lx, y, lz, b);
        // Java's setBlockState replaces the whole state, the fluid of a waterlogged one included
        if (f & kLayerFluid) liquid.clear(lx, y, lz, *s.block(code::Air));
    }
    std::uint16_t flags(int x, int y, int z) const {
        std::uint16_t f = flagsOf(s, get(x, y, z));
        if (liquid && !(f & kFluid)) {
            // a waterlogged block: BlockState#getFluidState is its water
            int lx = x - chunk.chunkX() * 16, lz = z - chunk.chunkZ() * 16;
            if (auto const* l = liquid.at(lx, y, lz)) {
                std::uint16_t lf = flagsOf(s, *l) & (kWater | kFluid);
                if (lf) f |= lf | kLayerFluid;
            }
        }
        return f;
    }

    bool canReplaceBlock(std::uint16_t f) const { return !(f & kIce) && !(f & kWater) && (f & kReplace); }
    bool insideChunk(int x, int z) const {
        int dx = x - chunk.chunkX() * 16, dz = z - chunk.chunkZ() * 16;
        return dx >= 0 && dz >= 0 && dx <= 15 && dz <= 15;
    }

    // WorldCarver#canReach (the chunk being carved)
    bool canReach(double x, double z, int branchIndex, int branchCount, float width) const {
        double d0 = static_cast<double>(chunk.chunkX() * 16 + 8);
        double d1 = static_cast<double>(chunk.chunkZ() * 16 + 8);
        double d2 = x - d0, d3 = z - d1;
        double d4 = static_cast<double>(branchCount - branchIndex);
        double d5 = static_cast<double>(width + 2.0F + 16.0F);
        return d2 * d2 + d3 * d3 - d4 * d4 <= d5 * d5;
    }

    bool shouldSkip(double dX, double dY, double dZ, double floor) const {
        if (dY <= floor) return true;
        return dX * dX + dY * dY + dZ * dZ >= 1.0;
    }

    // WorldCarver#carveEllipsoid
    bool carveEllipsoid(double x, double y, double z, double hr, double vr, double floor) const {
        int    cx = chunk.chunkX(), cz = chunk.chunkZ();
        double d0 = static_cast<double>(cx * 16 + 8), d1 = static_cast<double>(cz * 16 + 8);
        double d2 = 16.0 + hr * 2.0;
        if (std::fabs(x - d0) > d2 || std::fabs(z - d1) > d2) return false;
        int  i = cx * 16, j = cz * 16;
        int  k  = std::max(mth::floor(x - hr) - i - 1, 0);
        int  l  = std::min(mth::floor(x + hr) - i, 15);
        int  i1 = std::max(mth::floor(y - vr) - 1, genMinY + 1);
        int  k1 = std::min(mth::floor(y + vr) + 1, genMinY + genDepth - 1 - 7);
        int  l1 = std::max(mth::floor(z - hr) - j - 1, 0);
        int  i2 = std::min(mth::floor(z + hr) - j, 15);
        bool flag = false;
        for (int j2 = k; j2 <= l; ++j2) {
            int    k2 = i + j2;
            double d3 = (static_cast<double>(k2) + 0.5 - x) / hr;
            for (int l2 = l1; l2 <= i2; ++l2) {
                int    i3 = j + l2;
                double d4 = (static_cast<double>(i3) + 0.5 - z) / hr;
                if (d3 * d3 + d4 * d4 >= 1.0) continue;
                bool isSurface = false;
                for (int j3 = k1; j3 > i1; --j3) {
                    double d5 = (static_cast<double>(j3) - 0.5 - y) / vr;
                    if (!shouldSkip(d3, d5, d4, floor) && !ctx.mask->get(j2, j3, l2)) {
                        ctx.mask->set(j2, j3, l2);
                        flag |= carveBlock(k2, j3, i3, isSurface);
                    }
                }
            }
        }
        return flag;
    }

    // TFCavesCarver#carveBlock
    bool carveBlock(int x, int y, int z, bool& isSurface) const {
        std::uint16_t f = flags(x, y, z);
        if (f & kSurface) isSurface = true;
        if (y < minY + 6) return false;
        if (!canReplaceBlock(f)) return false;
        for (auto const& d : kDirections) {
            int rx = x + d[0], ry = y + d[1], rz = z + d[2];
            if (insideChunk(rx, rz) && (flags(rx, ry, rz) & kWater)) return false;
        }
        auto         randomFromPos = s.oreRandom->at(x, y, z);
        Block const* place         = caveAir;
        if (flags(x, y + 2, z) & kFluid) {
            place = randomFromPos->nextBoolean() ? s.rootedDirt : s.coarseDirt;
        }
        if (!place) return false;
        set(x, y, z, *place, f);
        if (std::uint16_t below = isSurface ? flags(x, y - 1, z) : 0; below & kDirt) {
            // CarvingContext#topMaterial(biome, chunk, below, hasFluid = false)
            if (!cols) cols = std::make_unique<ChunkColumns>(s, chunk.chunkX(), chunk.chunkZ());
            auto top = s.gen->surface().topMaterial(x, y - 1, z, false, [&](int bx, int by, int bz) -> std::string const& { return cols->blockBiome(bx, by, bz); });
            if (top)
                if (auto const* b = s.block(*top)) set(x, y - 1, z, *b, below);
        }
        postCarveBlock(x, y, z, *randomFromPos);
        return true;
    }

    // TFCavesCarver#postCarveBlock
    void postCarveBlock(int x, int y, int z, RandomSource& rand) const {
        for (std::size_t di = 0; di < kDirections.size(); ++di) {
            auto const& d  = kDirections[di];
            int         rx = x + d[0], ry = y + d[1], rz = z + d[2];
            if (!insideChunk(rx, rz)) continue;
            if (highlands) {
                if (rand.nextInt(4) == 0) {
                    std::uint16_t nf = flags(rx, ry, rz);
                    if (!canReplaceBlock(nf)) continue;
                    BlockPos p(rx, ry, rz);
                    if (auto const* w = kit::sampleState(wallProvider(true), rand, p)) set(rx, ry, rz, *w, nf);
                }
            } else if (di != 0 && (di == 1 || (flags(rx, ry + 1, rz) & kAir) || checkNoiseThreshold(rx, ry, rz))) {
                std::uint16_t nf = flags(rx, ry, rz);
                if ((nf & kBaseStone) || (nf & kWater)) {
                    BlockPos p(rx, ry, rz);
                    if (auto const* w = kit::sampleState(wallProvider(false), rand, p)) set(rx, ry, rz, *w, nf);
                }
            }
        }
    }

    static bool checkNoiseThreshold(int x, int y, int z) {
        double n = roofNoise().noise(static_cast<double>(x) * 0.25, static_cast<double>(y) * 0.25, static_cast<double>(z) * 0.25);
        return n * 0.5 + 0.5 > 0.5;
    }

    float getThickness(RandomSource& rand) const {
        float f = rand.nextFloat() * 2.0F + rand.nextFloat();
        if (rand.nextInt(10) == 0) f *= rand.nextFloat() * rand.nextFloat() * 3.0F + 1.0F;
        return f;
    }

    void createRoom(double x, double y, double z, float radius, double horizToVertRatio, double floor) const {
        double d0 = 1.5 + static_cast<double>(jmth::sin(jmth::kPi / 2.0F) * radius);
        double d1 = d0 * horizToVertRatio;
        carveEllipsoid(x, y, z, d0, d1, floor);
    }

    void createTunnel(std::int64_t seed, double posX, double posY, double posZ, double horizMult, double vertMult, float thickness, float yaw, float pitch,
                      int branchIndex, int branchCount, double horizToVertRatio, double floor) const {
        core::jrand::LegacyRandom random(seed);
        int                       i    = random.nextInt(branchCount / 2) + branchCount / 4;
        bool                      flag = random.nextInt(6) == 0;
        float                     f = 0.0F, f1 = 0.0F;
        for (int j = branchIndex; j < branchCount; ++j) {
            double horizontalRadius = 1.5 + static_cast<double>(jmth::sin(jmth::kPi * static_cast<float>(j) / static_cast<float>(branchCount)) * thickness);
            double verticalRadius   = horizontalRadius * horizToVertRatio;
            float  f2               = jmth::cos(pitch);
            posX += static_cast<double>(jmth::cos(yaw) * f2);
            float yShift = jmth::sin(pitch);
            posY += (yShift > 0.0F || posY + static_cast<double>(yShift) > static_cast<double>(minY + 10)) ? static_cast<double>(yShift)
                                                                                                           : static_cast<double>(yShift * 0.25F);
            posZ += static_cast<double>(jmth::sin(yaw) * f2);
            pitch = pitch * (flag ? 0.92F : 0.7F);
            pitch = pitch + f1 * 0.1F;
            yaw += f * 0.1F;
            f1 = f1 * 0.9F;
            f  = f * 0.75F;
            f1 = f1 + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
            f  = f + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;
            if (j == i && thickness > 1.0F) {
                std::int64_t s1 = random.nextLong();
                float        t1 = random.nextFloat() * 0.5F + 0.5F;
                createTunnel(s1, posX, posY, posZ, horizMult, vertMult, t1, yaw - (jmth::kPi / 2.0F), pitch / 3.0F, j, branchCount, 1.0, floor);
                std::int64_t s2 = random.nextLong();
                float        t2 = random.nextFloat() * 0.5F + 0.5F;
                createTunnel(s2, posX, posY, posZ, horizMult, vertMult, t2, yaw + (jmth::kPi / 2.0F), pitch / 3.0F, j, branchCount, 1.0, floor);
                return;
            }
            if (random.nextInt(4) != 0) {
                if (!canReach(posX, posZ, j, branchCount, thickness)) return;
                bool   enlarge        = posY > static_cast<double>(minY + 12) && random.nextInt(48) == 0;
                float  sizeMultiplier = enlarge ? random.nextFloat() * random.nextFloat() * 2.0F + 1.0F : 1.0F;
                double sphereH        = std::min(horizontalRadius * horizMult * static_cast<double>(sizeMultiplier), 10.0);
                double sphereV        = verticalRadius * vertMult * static_cast<double>(sizeMultiplier);
                double sphereVLimited = enlarge ? std::min(sphereV, sphereH * static_cast<double>(0.65F)) : sphereV;
                carveEllipsoid(posX, posY, posZ, sphereH, sphereVLimited, floor);
            }
        }
    }

    // TFCavesCarver#carve
    void carve(RandomSource& random) const {
        int sx = ctx.sourceChunkX, sz = ctx.sourceChunkZ;
        if (highlands && (std::clamp(core::landmark::manhattanDistanceFromCenter(sx, sz), 0, 3) & 1) == 1) return;
        int const i         = (4 * 2 - 1) << 4; // sectionToBlockCoord(getRange() * 2 - 1)
        int       caveCount = highlands ? random.nextInt(2) : random.nextInt(4);
        for (int caveIndex = 0; caveIndex < caveCount; ++caveIndex) {
            double x = static_cast<double>(sx * 16 + random.nextInt(16));
            double y = static_cast<double>(sampleHeight(random));
            double z = static_cast<double>(sz * 16 + random.nextInt(16));
            double horiz = static_cast<double>(kit::sampleFloat(config.contains("horizontal_radius_multiplier") ? config["horizontal_radius_multiplier"] : Json(1.0), random));
            double vert  = static_cast<double>(kit::sampleFloat(config.contains("vertical_radius_multiplier") ? config["vertical_radius_multiplier"] : Json(1.0), random));
            double floor = static_cast<double>(kit::sampleFloat(config.contains("floor_level") ? config["floor_level"] : Json(-0.7), random));
            int    tunnelCount = 1;
            if (highlands || random.nextInt(4) == 0) {
                double horizToVertRatio = static_cast<double>(kit::sampleFloat(config.contains("yScale") ? config["yScale"] : Json(1.0), random));
                float  radius           = 1.0F + random.nextFloat() * 6.0F;
                createRoom(x, y, z, radius, horizToVertRatio, floor);
                tunnelCount += random.nextInt(4);
            }
            for (int t = 0; t < tunnelCount; ++t) {
                float randomRadian = random.nextFloat() * (jmth::kPi * 2.0F);
                float randomPitch  = (random.nextFloat() - 0.5F) / 4.0F;
                float thickness    = getThickness(random);
                int   branchCount  = i - random.nextInt(i / 4);
                createTunnel(random.nextLong(), x, y, z, horiz, vert, thickness, randomRadian, randomPitch, 0, branchCount, 1.0, floor);
            }
        }
    }

    /// The configured "y" height provider (UniformHeight / BiasedToBottomHeight, anchors in the
    /// generation context).
    int sampleHeight(RandomSource& random) const {
        Json const& p      = config.contains("y") ? config["y"] : Json();
        auto        anchor = [&](Json const& a) {
            if (a.contains("absolute")) return a["absolute"].get<int>();
            if (a.contains("above_bottom")) return genMinY + a["above_bottom"].get<int>();
            if (a.contains("below_top")) return genMinY + genDepth - 1 - a["below_top"].get<int>();
            return 0;
        };
        std::string type = p.value("type", std::string("minecraft:uniform"));
        int         lo   = anchor(p.value("min_inclusive", Json::object()));
        int         hi   = anchor(p.value("max_inclusive", Json::object()));
        if (type == "minecraft:biased_to_bottom") {
            int inner = p.value("inner", 1);
            if (hi - lo - inner + 1 <= 0) return lo;
            int k = random.nextInt(hi - lo - inner + 1);
            return random.nextInt(k + inner) + lo;
        }
        if (type == "minecraft:very_biased_to_bottom") {
            int inner = p.value("inner", 1);
            if (hi - lo - inner + 1 <= 0) return lo;
            int k = random.nextIntBetweenInclusive(lo + inner, hi);
            int l = random.nextIntBetweenInclusive(lo, k - 1);
            return random.nextIntBetweenInclusive(lo, l - 1 + inner);
        }
        if (type == "minecraft:constant") return anchor(p.value("value", Json::object()));
        if (lo > hi) return lo;
        return random.nextIntBetweenInclusive(lo, hi);
    }
};

} // namespace

void carve(bool highlands, worldgen::Chunk& chunk, worldgen::Region&, RandomSource& random) {
    auto* ctx = worldgen::ext::carverContext();
    if (!ctx || !ctx->mask) return;
    auto s = state();
    if (!s) return;
    carveWith(*s, highlands, chunk, *ctx, random);
}

void carveWith(State const& st, bool highlands, worldgen::Chunk& chunk, worldgen::ext::CarverContext& ctxRef, RandomSource& random) {
    auto* ctx = &ctxRef;
    auto* s   = &st;
    static Json const empty = Json::object();
    Json const&       cfg   = ctx->config ? *ctx->config : empty;
    // WorldCarver#isStartChunk (TF: the highland caves always start)
    float probability = cfg.contains("probability") ? cfg["probability"].get<float>() : 0.1F;
    if (!highlands && !(random.nextFloat() <= probability)) return;
    auto start = std::chrono::steady_clock::now();
    auto shape = s->gen->shape();
    Carver c{
        highlands,
        *s,
        chunk,
        *ctx,
        {},
        cfg,
        chunk.minY(),
        chunk.maxY(),
        std::max(chunk.minY(), shape.minY),
        std::min(chunk.maxY() - chunk.minY(), shape.height),
        s->caveAir,
        gf::terrain::ext::LiquidLayer::of(chunk)
    };
    c.carve(random);
    addChunkMs(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

bool carveBlockAt(
    State const&                  s,
    bool                          highlands,
    worldgen::Chunk&              chunk,
    worldgen::ext::CarverContext& ctx,
    int                           x,
    int                           y,
    int                           z
) {
    static Json const empty = Json::object();
    auto              shape = s.gen->shape();
    Carver            c{
        highlands,
        s,
        chunk,
        ctx,
        {},
        ctx.config ? *ctx.config : empty,
        chunk.minY(),
        chunk.maxY(),
        std::max(chunk.minY(), shape.minY),
        std::min(chunk.maxY() - chunk.minY(), shape.height),
        s.caveAir,
        gf::terrain::ext::LiquidLayer::of(chunk)
    };
    bool isSurface = false;
    return c.carveBlock(x, y, z, isSurface);
}

} // namespace gf::feature::terrain

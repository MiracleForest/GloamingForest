// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/registration/surface_rules/TFSurfaceRules.java
//   src/main/java/gloamingforest/world/components/chunkblanketing/CanopyBlanketProcessor.java
//   src/main/java/gloamingforest/world/components/chunkblanketing/GlacierBlanketProcessor.java
//   src/main/java/gloamingforest/world/components/chunkblanketing/ChunkBlanketProcessor.java
//   src/main/java/gloamingforest/init/custom/ChunkBlanketProcessors.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The surface evaluation follows Minecraft 1.21.1's SurfaceSystem and SurfaceRules (context
// updates, stone depths, water height, the lazy conditions' formulas) and NoiseBasedChunkGenerator.
#include "core/terrain/Surface.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/jrand/Mth.h"
#include "core/landmark/Landmark.h"

#pragma clang fp contract(off)

namespace gf::core::terrain {

namespace mth = core::mth;
using Json    = nlohmann::json;

// ---- BlockTable -----------------------------------------------------------------------------------------

std::uint16_t BlockTable::codeOf(Json const& state) {
    if (!state.is_object()) return code::Air;
    std::string name = state.value("Name", std::string("minecraft:air"));
    Json        props = state.contains("Properties") && state["Properties"].is_object() ? state["Properties"] : Json::object();
    return codeOf(name, props);
}

std::uint16_t BlockTable::codeOf(std::string const& rawName, Json const& properties) {
    std::string name = rawName.find(':') == std::string::npos ? "minecraft:" + rawName : rawName;
    if (name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air") return code::Air;
    Json props = properties.is_object() ? properties : Json::object();
    for (std::size_t i = 0; i < mEntries.size(); ++i)
        if (mEntries[i].name == name && mEntries[i].properties == props) return static_cast<std::uint16_t>(code::FirstNamed + i);
    Entry e;
    e.name       = name;
    e.properties = props;
    e.fluid      = name == "minecraft:water" || name == "minecraft:lava";
    mEntries.push_back(std::move(e));
    return static_cast<std::uint16_t>(code::FirstNamed + mEntries.size() - 1);
}

BlockTable::Entry const* BlockTable::entry(std::uint16_t c) const {
    if (c < code::FirstNamed) return nullptr;
    std::size_t i = c - code::FirstNamed;
    return i < mEntries.size() ? &mEntries[i] : nullptr;
}

bool isFluidCode(std::uint16_t c, BlockTable const& table) {
    if (c == code::Water || c == code::Lava) return true;
    auto const* e = table.entry(c);
    return e && e->fluid;
}

int Grid::worldSurfaceWg(int x, int z) const {
    for (int y = minY + height - 1; y >= minY; --y)
        if (get(x, y, z) != code::Air) return y;
    return minY - 1;
}

int Grid::oceanFloorWg(int x, int z, BlockTable const& table) const {
    for (int y = minY + height - 1; y >= minY; --y) {
        auto c = get(x, y, z);
        if (c != code::Air && !isFluidCode(c, table)) return y;
    }
    return minY - 1;
}

// ---- surface rules ---------------------------------------------------------------------------------------

namespace {

enum class CondKind { Biome, StoneDepth, Water, YAbove, NoiseThreshold, VerticalGradient, Not, Hole };

struct Cond {
    CondKind                 kind = CondKind::Hole;
    std::vector<std::string> biomes;
    int                      offset = 0, secondaryDepthRange = 0, surfaceDepthMultiplier = 0, anchorY = 0, trueAtAndBelow = 0, falseAtAndAbove = 0;
    bool                     addSurfaceDepth = false, addStoneDepth = false, ceiling = false;
    double                   minThreshold = 0, maxThreshold = 0;
    std::shared_ptr<noise::NormalNoise const>       noise;
    std::shared_ptr<jrand::PositionalRandomFactory> random;
    int                                             inner = -1; ///< Not
};

enum class RuleKind { Sequence, Condition, Block };

struct Rule {
    RuleKind         kind = RuleKind::Block;
    std::vector<int> children; ///< Sequence
    int              cond = -1, then = -1;
    std::uint16_t    block = code::Air;
};

/// SurfaceRules.Context: what the conditions read.
struct Context {
    int                blockX = 0, blockZ = 0, surfaceDepth = 0;
    int                blockY = 0, waterHeight = std::numeric_limits<int>::min(), stoneDepthBelow = 0, stoneDepthAbove = 0;
    mutable bool       secondarySet = false;
    mutable double     secondary    = 0;
    mutable std::string const* biome = nullptr;
    BlockBiomeFn const* biomeFn = nullptr;
};

} // namespace

struct SurfaceRules::Impl {
    std::vector<Rule>                                 rules;
    std::vector<Cond>                                 conds;
    int                                               root = -1;
    GenContext                                        gen;
    std::shared_ptr<noise::NormalNoise const>         surfaceNoise, secondaryNoise;
    std::shared_ptr<jrand::PositionalRandomFactory>   noiseRandom;

    [[nodiscard]] int surfaceDepth(int x, int z) const {
        double d0 = surfaceNoise->getValue(static_cast<double>(x), 0.0, static_cast<double>(z));
        auto   r  = noiseRandom->at(x, 0, z);
        return static_cast<int>(d0 * 2.75 + 3.0 + r->nextDouble() * 0.25);
    }

    [[nodiscard]] double surfaceSecondary(Context const& c) const {
        if (!c.secondarySet) {
            c.secondary    = secondaryNoise->getValue(static_cast<double>(c.blockX), 0.0, static_cast<double>(c.blockZ));
            c.secondarySet = true;
        }
        return c.secondary;
    }

    [[nodiscard]] bool test(int i, Context const& c) const {
        Cond const& k = conds[static_cast<std::size_t>(i)];
        switch (k.kind) {
        case CondKind::Biome: {
            if (!c.biome) c.biome = &(*c.biomeFn)(c.blockX, c.blockY, c.blockZ);
            return std::find(k.biomes.begin(), k.biomes.end(), *c.biome) != k.biomes.end();
        }
        case CondKind::StoneDepth: {
            int depth = k.ceiling ? c.stoneDepthBelow : c.stoneDepthAbove;
            int j     = k.addSurfaceDepth ? c.surfaceDepth : 0;
            int l     = k.secondaryDepthRange == 0 ? 0 : static_cast<int>(mth::map(surfaceSecondary(c), -1.0, 1.0, 0.0, static_cast<double>(k.secondaryDepthRange)));
            return depth <= 1 + k.offset + j + l;
        }
        case CondKind::Water:
            return c.waterHeight == std::numeric_limits<int>::min()
                || c.blockY + (k.addStoneDepth ? c.stoneDepthAbove : 0) >= c.waterHeight + k.offset + c.surfaceDepth * k.surfaceDepthMultiplier;
        case CondKind::YAbove:
            return c.blockY + (k.addStoneDepth ? c.stoneDepthAbove : 0) >= k.anchorY + c.surfaceDepth * k.surfaceDepthMultiplier;
        case CondKind::NoiseThreshold: {
            double d0 = k.noise->getValue(static_cast<double>(c.blockX), 0.0, static_cast<double>(c.blockZ));
            return d0 >= k.minThreshold && d0 <= k.maxThreshold;
        }
        case CondKind::VerticalGradient: {
            int y = c.blockY;
            if (y <= k.trueAtAndBelow) return true;
            if (y >= k.falseAtAndAbove) return false;
            double d0 = mth::map(static_cast<double>(y), static_cast<double>(k.trueAtAndBelow), static_cast<double>(k.falseAtAndAbove), 1.0, 0.0);
            auto   r  = k.random->at(c.blockX, y, c.blockZ);
            return static_cast<double>(r->nextFloat()) < d0;
        }
        case CondKind::Not:
            return !test(k.inner, c);
        case CondKind::Hole:
            return c.surfaceDepth <= 0;
        }
        return false;
    }

    [[nodiscard]] std::optional<std::uint16_t> apply(int i, Context const& c) const {
        Rule const& r = rules[static_cast<std::size_t>(i)];
        switch (r.kind) {
        case RuleKind::Block:
            return r.block;
        case RuleKind::Condition:
            if (!test(r.cond, c)) return std::nullopt;
            return apply(r.then, c);
        case RuleKind::Sequence:
            for (int child : r.children)
                if (auto s = apply(child, c)) return s;
            return std::nullopt;
        }
        return std::nullopt;
    }
};

namespace {

std::string normalize(std::string id) {
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

/// VerticalAnchor#resolveY.
std::optional<int> resolveAnchor(Json const& a, GenContext gen) {
    if (!a.is_object()) return std::nullopt;
    if (a.contains("absolute")) return a["absolute"].get<int>();
    if (a.contains("above_bottom")) return gen.minY + a["above_bottom"].get<int>();
    if (a.contains("below_top")) return gen.minY + gen.height - 1 - a["below_top"].get<int>();
    return std::nullopt;
}

class RuleCompiler {
public:
    RuleCompiler(SurfaceRules::Impl& impl, std::int64_t seed, BlockTable& table) : mImpl(impl), mSeed(seed), mTable(table), mFactory(randomStateFactory(seed)) {}
    std::string error;

    int rule(Json const& j) {
        std::string type = normalize(j.value("type", std::string()));
        Rule        r;
        if (type == "minecraft:sequence") {
            r.kind = RuleKind::Sequence;
            for (auto const& c : j.value("sequence", Json::array())) {
                int i = rule(c);
                if (i < 0) return -1;
                r.children.push_back(i);
            }
        } else if (type == "minecraft:condition") {
            r.kind = RuleKind::Condition;
            r.cond = cond(j.value("if_true", Json()));
            if (r.cond < 0) return -1;
            r.then = rule(j.value("then_run", Json()));
            if (r.then < 0) return -1;
        } else if (type == "minecraft:block") {
            r.kind  = RuleKind::Block;
            r.block = mTable.codeOf(j.value("result_state", Json()));
        } else {
            return fail("unsupported surface rule " + type);
        }
        mImpl.rules.push_back(std::move(r));
        return static_cast<int>(mImpl.rules.size()) - 1;
    }

private:
    int fail(std::string m) {
        if (error.empty()) error = std::move(m);
        return -1;
    }

    int cond(Json const& j) {
        std::string type = normalize(j.value("type", std::string()));
        Cond        c;
        if (type == "minecraft:biome") {
            c.kind = CondKind::Biome;
            for (auto const& b : j.value("biome_is", Json::array()))
                if (b.is_string()) c.biomes.push_back(normalize(b.get<std::string>()));
        } else if (type == "minecraft:stone_depth") {
            c.kind                = CondKind::StoneDepth;
            c.offset              = j.value("offset", 0);
            c.addSurfaceDepth     = j.value("add_surface_depth", false);
            c.secondaryDepthRange = j.value("secondary_depth_range", 0);
            c.ceiling             = j.value("surface_type", std::string("floor")) == "ceiling";
        } else if (type == "minecraft:water") {
            c.kind                   = CondKind::Water;
            c.offset                 = j.value("offset", 0);
            c.surfaceDepthMultiplier = j.value("surface_depth_multiplier", 0);
            c.addStoneDepth          = j.value("add_stone_depth", false);
        } else if (type == "minecraft:y_above") {
            c.kind                   = CondKind::YAbove;
            auto y                   = resolveAnchor(j.value("anchor", Json()), mImpl.gen);
            if (!y) return fail("y_above: bad anchor");
            c.anchorY                = *y;
            c.surfaceDepthMultiplier = j.value("surface_depth_multiplier", 0);
            c.addStoneDepth          = j.value("add_stone_depth", false);
        } else if (type == "minecraft:noise_threshold") {
            c.kind         = CondKind::NoiseThreshold;
            std::string id = normalize(j.value("noise", std::string()));
            auto        p  = vanillaNoise(id);
            if (!p) return fail("noise_threshold: unknown noise " + id);
            c.noise        = std::make_shared<noise::NormalNoise const>(randomStateNoise(mSeed, id, *p));
            c.minThreshold = j.value("min_threshold", 0.0);
            c.maxThreshold = j.value("max_threshold", 0.0);
        } else if (type == "minecraft:vertical_gradient") {
            c.kind  = CondKind::VerticalGradient;
            auto lo = resolveAnchor(j.value("true_at_and_below", Json()), mImpl.gen);
            auto hi = resolveAnchor(j.value("false_at_and_above", Json()), mImpl.gen);
            if (!lo || !hi) return fail("vertical_gradient: bad anchors");
            c.trueAtAndBelow  = *lo;
            c.falseAtAndAbove = *hi;
            // RandomState#getOrCreateRandomFactory: random.fromHashOf(name).forkPositional()
            c.random = std::shared_ptr<jrand::PositionalRandomFactory>(mFactory->fromHashOf(normalize(j.value("random_name", std::string())))->forkPositional());
        } else if (type == "minecraft:not") {
            c.kind  = CondKind::Not;
            c.inner = cond(j.value("invert", Json()));
            if (c.inner < 0) return -1;
        } else if (type == "minecraft:hole") {
            c.kind = CondKind::Hole;
        } else {
            return fail("unsupported surface condition " + type);
        }
        mImpl.conds.push_back(std::move(c));
        return static_cast<int>(mImpl.conds.size()) - 1;
    }

    SurfaceRules::Impl&                             mImpl;
    std::int64_t                                    mSeed;
    BlockTable&                                     mTable;
    std::unique_ptr<jrand::PositionalRandomFactory> mFactory;
};

} // namespace

std::shared_ptr<SurfaceRules const> SurfaceRules::compile(Json const& rule, std::int64_t seed, GenContext gen, BlockTable& table, std::string* error) {
    auto impl            = std::make_shared<Impl>();
    impl->gen            = gen;
    impl->surfaceNoise   = std::make_shared<noise::NormalNoise const>(randomStateNoise(seed, "minecraft:surface", *vanillaNoise("minecraft:surface")));
    impl->secondaryNoise = std::make_shared<noise::NormalNoise const>(
        randomStateNoise(seed, "minecraft:surface_secondary", *vanillaNoise("minecraft:surface_secondary"))
    );
    impl->noiseRandom = std::shared_ptr<jrand::PositionalRandomFactory>(randomStateFactory(seed));
    RuleCompiler c(*impl, seed, table);
    impl->root = c.rule(rule);
    if (impl->root < 0) {
        if (error) *error = c.error;
        return nullptr;
    }
    return std::make_shared<SurfaceRules const>(std::move(impl));
}

int SurfaceRules::surfaceDepth(int x, int z) const { return mImpl->surfaceDepth(x, z); }

void SurfaceRules::buildSurface(int chunkX, int chunkZ, Grid& grid, BlockTable const& table, BlockBiomeFn const& biome) const {
    constexpr int kWayBelowMinY = -2032 << 4; // DimensionType.WAY_BELOW_MIN_Y
    int const     minX = chunkX * 16, minZ = chunkZ * 16;
    int const     l2   = grid.minY; // chunk.getMinBuildHeight()
    auto          isStone = [&](std::uint16_t c) { return c != code::Air && !isFluidCode(c, table); };
    for (int k = 0; k < 16; ++k) {
        for (int l = 0; l < 16; ++l) {
            int     i1 = minX + k, j1 = minZ + l;
            int     l1 = grid.worldSurfaceWg(k, l) + 1;
            Context ctx;
            ctx.blockX       = i1;
            ctx.blockZ       = j1;
            ctx.surfaceDepth = mImpl->surfaceDepth(i1, j1);
            ctx.biomeFn      = &biome;
            int i2 = 0, j2 = std::numeric_limits<int>::min(), k2 = std::numeric_limits<int>::max();
            for (int i3 = l1; i3 >= l2; --i3) {
                std::uint16_t s = grid.get(k, i3, l);
                if (s == code::Air) {
                    i2 = 0;
                    j2 = std::numeric_limits<int>::min();
                } else if (isFluidCode(s, table)) {
                    if (j2 == std::numeric_limits<int>::min()) j2 = i3 + 1;
                } else {
                    if (k2 >= i3) {
                        k2 = kWayBelowMinY;
                        for (int j3 = i3 - 1; j3 >= l2 - 1; --j3) {
                            if (!isStone(grid.get(k, j3, l))) {
                                k2 = j3 + 1;
                                break;
                            }
                        }
                    }
                    ++i2;
                    int k3              = i3 - k2 + 1;
                    ctx.stoneDepthAbove = i2;
                    ctx.stoneDepthBelow = k3;
                    ctx.waterHeight     = j2;
                    ctx.blockY          = i3;
                    ctx.biome           = nullptr;
                    if (s == code::DefaultBlock)
                        if (auto r = mImpl->apply(mImpl->root, ctx)) grid.set(k, i3, l, *r);
                }
            }
        }
    }
}

std::optional<std::uint16_t> SurfaceRules::topMaterial(int x, int y, int z, bool hasFluid, BlockBiomeFn const& biome) const {
    Context ctx;
    ctx.blockX          = x;
    ctx.blockZ          = z;
    ctx.surfaceDepth    = mImpl->surfaceDepth(x, z);
    ctx.biomeFn         = &biome;
    ctx.stoneDepthAbove = 1;
    ctx.stoneDepthBelow = 1;
    ctx.waterHeight     = hasFluid ? y + 1 : std::numeric_limits<int>::min();
    ctx.blockY          = y;
    return mImpl->apply(mImpl->root, ctx);
}

// ---- blankets ---------------------------------------------------------------------------------------

bool Blankets::add(std::string const& id, Json const& json, BlockTable& table) {
    if (!json.is_object()) return false;
    Processor p;
    p.id   = id;
    p.type = normalize(json.value("type", std::string()));
    for (auto const& b : json.value("biome_mask", Json::array()))
        if (b.is_string()) p.biomes.push_back(normalize(b.get<std::string>()));
    if (json.contains("biome_mask") && json["biome_mask"].is_string()) p.biomes.push_back(normalize(json["biome_mask"].get<std::string>()));
    p.height = json.value("height", 0);
    // simple_state_provider {"state": {...}} (the only provider upstream's two processors use)
    auto stateOf = [&](char const* key) -> std::optional<std::uint16_t> {
        if (!json.contains(key) || !json[key].is_object()) return std::nullopt;
        auto const& prov = json[key];
        if (normalize(prov.value("type", std::string())) != "minecraft:simple_state_provider" || !prov.contains("state")) return std::nullopt;
        return table.codeOf(prov["state"]);
    };
    if (p.type == "gloamingforest:canopy") {
        auto b = stateOf("block");
        if (!b) return false;
        p.block = *b;
        for (auto const& s : json.value("avoid_structures", Json::array()))
            if (s.is_string()) p.avoidStructures.push_back(normalize(s.get<std::string>()));
    } else if (p.type == "gloamingforest:glacier") {
        auto b = stateOf("body_block"), t = stateOf("top_block");
        if (!b || !t) return false;
        p.block = *b;
        p.top   = *t;
    } else {
        return false;
    }
    mProcessors.push_back(std::move(p));
    std::sort(mProcessors.begin(), mProcessors.end(), [](Processor const& a, Processor const& b) { return a.id < b.id; });
    return true;
}

namespace {

void canopy(Blankets::Processor const& p, int chunkX, int chunkZ, Grid& grid, BlockTable const& table, BlockBiomeFn const& biome, Blankets::ReferencesFn const& refs) {
    int const ox = chunkX * 16, oz = chunkZ * 16;
    int       thicks[5 * 5] = {};
    bool      biomeFound    = false;
    for (int dZ = 0; dZ < 5; ++dZ)
        for (int dX = 0; dX < 5; ++dX)
            for (int bx = -1; bx <= 1; ++bx)
                for (int bz = -1; bz <= 1; ++bz) {
                    auto const& b = biome(ox + ((dX + bx) << 2), 0, oz + ((dZ + bz) << 2));
                    if (std::find(p.biomes.begin(), p.biomes.end(), b) != p.biomes.end()) {
                        ++thicks[dX + dZ * 5];
                        biomeFound = true;
                    }
                }
    if (!biomeFound) return;

    bool clearing = refs && refs(p.avoidStructures);
    int  hx = 0, hz = 0;
    if (clearing) {
        // LegacyLandmarkPlacements.getNearestCenterXZ(...).subtract(chunkOrigin)
        auto c = core::landmark::nearestCenter(chunkX, chunkZ);
        hx     = c.x - ox;
        hz     = c.z - oz;
    }
    for (int dZ = 0; dZ < 16; ++dZ) {
        for (int dX = 0; dX < 16; ++dX) {
            int qx  = dX >> 2;
            int qz  = dZ >> 2;
            int top = grid.worldSurfaceWg(dX, dZ);
            if (grid.get(dX, top, dZ) == code::Water) continue; // getFluidState(surfacePos).is(WATER)

            float xweight   = static_cast<float>(dX % 4) * 0.25F + 0.125F;
            float zweight   = static_cast<float>(dZ % 4) * 0.25F + 0.125F;
            float thickness = static_cast<float>(thicks[qx + qz * 5]) * (1.0F - xweight) * (1.0F - zweight)
                            + static_cast<float>(thicks[qx + 1 + qz * 5]) * xweight * (1.0F - zweight)
                            + static_cast<float>(thicks[qx + (qz + 1) * 5]) * (1.0F - xweight) * zweight
                            + static_cast<float>(thicks[qx + 1 + (qz + 1) * 5]) * xweight * zweight - 4.0F;
            if (clearing) {
                int rx   = dX - hx;
                int rz   = dZ - hz;
                int dist = jmth::f2i(jmth::sqrt(static_cast<float>(rx * rx + rz * rz)));
                if (dist < 24) thickness -= static_cast<float>(24 - dist);
            }
            if (thickness > 1.0F) {
                int dY = grid.worldSurfaceWg(dX, dZ);
                if (isFluidCode(grid.get(dX, dY, dZ), table)) continue;
                int treeBottom = dY + p.height - jmth::f2i(thickness * 0.5F);
                int treeTop    = treeBottom + jmth::f2i(thickness);
                for (int y = treeBottom; y < treeTop; ++y) grid.set(dX, y, dZ, p.block);
            }
        }
    }
}

void glacier(Blankets::Processor const& p, int chunkX, int chunkZ, Grid& grid, BlockBiomeFn const& biome) {
    // SimpleProcessor#processChunk: x outer, z inner, at WORLD_SURFACE_WG + 1
    for (int dX = 0; dX < 16; ++dX)
        for (int dZ = 0; dZ < 16; ++dZ) {
            int firstAvailableY = grid.worldSurfaceWg(dX, dZ) + 1;
            auto const& b       = biome(chunkX * 16 + dX, firstAvailableY, chunkZ * 16 + dZ);
            if (std::find(p.biomes.begin(), p.biomes.end(), b) == p.biomes.end()) continue;
            int maxY = firstAvailableY + p.height;
            grid.set(dX, maxY, dZ, p.top);
            for (int y = maxY - 1; y >= firstAvailableY; --y) grid.set(dX, y, dZ, p.block);
        }
}

} // namespace

void Blankets::apply(int chunkX, int chunkZ, Grid& grid, BlockTable const& table, std::vector<std::string> const& chunkBiomes, BlockBiomeFn const& biome,
                     ReferencesFn const& references, std::function<void()> const& fork) const {
    for (auto const& p : mProcessors) {
        bool applies = std::any_of(p.biomes.begin(), p.biomes.end(), [&](std::string const& b) {
            return std::find(chunkBiomes.begin(), chunkBiomes.end(), b) != chunkBiomes.end();
        });
        if (!applies) continue;
        if (fork) fork();
        if (p.type == "gloamingforest:canopy") canopy(p, chunkX, chunkZ, grid, table, biome, references);
        else if (p.type == "gloamingforest:glacier") glacier(p, chunkX, chunkZ, grid, biome);
    }
}

// ---- Generator ----------------------------------------------------------------------------------------

std::shared_ptr<Generator const>
Generator::create(Json const& settings, std::int64_t seed, Density::Resolver const& densities, BlockTable& table, std::string* error) {
    auto fail = [&](std::string m) -> std::shared_ptr<Generator const> {
        if (error) *error = std::move(m);
        return nullptr;
    };
    if (!settings.is_object()) return fail("noise settings: not an object");
    auto g = std::make_shared<Generator>();
    noise::NoiseShape shape;
    auto const&       n = settings.value("noise", Json::object());
    shape.minY           = n.value("min_y", -32);
    shape.height         = n.value("height", 256);
    shape.sizeHorizontal = n.value("size_horizontal", 2);
    shape.sizeVertical   = n.value("size_vertical", 2);
    g->mSeaLevel         = settings.value("sea_level", 0);
    auto block           = settings.value("default_block", Json::object());
    g->mDefaultBlock     = block.value("Name", std::string("minecraft:stone"));
    auto fluid           = settings.value("default_fluid", Json::object()).value("Name", std::string("minecraft:water"));
    g->mDefaultFluid     = fluid == "minecraft:air" ? code::Air : (fluid == "minecraft:lava" ? code::Lava : code::Water);

    auto        router = settings.value("noise_router", Json::object());
    std::string err;
    g->mDensity = Density::compile(router.value("final_density", Json(0.0)), seed, densities, shape, &err);
    if (!g->mDensity) return fail("final_density: " + err);
    GenContext gen{shape.minY, shape.height};
    g->mSurface = SurfaceRules::compile(settings.value("surface_rule", Json::object()), seed, gen, table, &err);
    if (!g->mSurface) return fail("surface_rule: " + err);
    return g;
}

std::uint16_t Generator::stateAt(double density, int y) const {
    if (density > 0.0) return code::DefaultBlock;
    // Aquifer.createDisabled with the global fluid picker
    if (y < std::min(-54, mSeaLevel)) return code::Lava;
    if (y < mSeaLevel) return mDefaultFluid;
    return code::Air;
}

void Generator::fillNoise(int chunkX, int chunkZ, TerrainSampler const& sampler, ExtraDensity const& extra, Grid& grid) const {
    std::vector<double> d;
    mDensity->fillChunk(chunkX, chunkZ, sampler, d, extra);
    auto const& s = mDensity->shape();
    for (int y = s.minY; y < s.minY + s.height; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                auto c = stateAt(d[static_cast<std::size_t>(((y - s.minY) * 16 + z) * 16 + x)], y);
                if (c != code::Air) grid.set(x, y, z, c);
            }
}

int Generator::baseHeight(int x, int z, bool oceanFloor, TerrainSampler const& sampler) const {
    std::vector<double> d;
    mDensity->fillColumn(x, z, sampler, d);
    auto const& s = mDensity->shape();
    for (int y = s.minY + s.height - 1; y >= s.minY; --y) {
        auto c     = stateAt(d[static_cast<std::size_t>(y - s.minY)], y);
        bool match = oceanFloor ? (c == code::DefaultBlock || c == code::OtherSolid) : c != code::Air;
        if (match) return y + 1;
    }
    return s.minY;
}

} // namespace gf::core::terrain

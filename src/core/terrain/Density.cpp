// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/chunkgenerators/TerrainDensityRouter.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/NoiseDensityRouter.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/SqrtDensityFunction.java
//   src/main/java/gloamingforest/init/GloamingDensityFunctions.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The vanilla part follows Minecraft 1.21.1's DensityFunctions and NoiseChunk (interpolation in
// NoiseInterpolator's lerp order, FlatCache at quart corners y 0, the corner slices of the cells).
#include "core/terrain/Density.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>

#include "core/jrand/Mth.h"

#pragma clang fp contract(off)

namespace gf::core::terrain {

namespace mth = core::mth;
using Json    = nlohmann::json;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

enum class Kind {
    Constant,
    Add,
    Mul,
    Min,
    Max,
    AddConst,
    MulConst,
    Abs,
    Square,
    Cube,
    HalfNegative,
    QuarterNegative,
    Squeeze,
    Clamp,
    YGradient,
    Noise,
    RangeChoice,
    Interpolated,
    FlatCache,
    PassThrough, // cache_2d, cache_once, cache_all_in_cell, blend_density
    BiomeTerrain,
    BiomeNoise,
    Sqrt,
    Focused,
    HollowHill,
    CoordMin,
    CoordMax,
};

struct Node {
    Kind   kind = Kind::Constant;
    int    a = -1, b = -1, c = -1; ///< children
    double v0 = 0, v1 = 0, v2 = 0, v3 = 0;
    double lo = -kInf, hi = kInf;
    int    slot = -1; ///< interpolated / flat_cache: their cache index
    std::shared_ptr<noise::NormalNoise const> noise;
    terrain::Focused    focused;
    terrain::HollowHill hollow;
};

} // namespace

struct Density::Graph {
    std::vector<Node> nodes;
    int               root         = -1;
    int               interpCount  = 0;
    int               flatCount    = 0;
    bool              usesTerrain  = false;
};

namespace {

/// Per-fill state (Java's NoiseChunk): cell geometry, the corner slices of every interpolated node,
/// the flat caches and the column cache of the biome-driven routers.
struct Fill {
    int  minX = 0, minZ = 0; ///< cell-aligned origin (chunk: its minimum block; column: its cell)
    int  cellCountXZ = 2, cellCountY = 32, cellNoiseMinY = -4, cellWidth = 8, cellHeight = 8;
    int  firstNoiseX = 0, firstNoiseZ = 0, noiseSizeXZ = 4;
    std::vector<std::vector<double>> corners; ///< [slot][(cx * (n + 1) + cz) * (cellCountY + 1) + cy]
    std::vector<std::vector<double>> flat;    ///< [slot][i * (noiseSizeXZ + 1) + j]
    std::vector<DensityData>         columns; ///< 16 x 16 (sectionRelative), ChunkCached*Router
    std::vector<char>                columnSet;
    TerrainSampler const*            sampler = nullptr;
};

struct Ctx {
    int   x = 0, y = 0, z = 0;
    Fill* fill   = nullptr;
    bool  corner = false; ///< evaluating an interpolated node's corner (no interpolation below)
};

class Evaluator {
public:
    Evaluator(Density::Graph const& g, TerrainSampler const& sampler) : mG(g), mSampler(sampler) {}

    double eval(int i, Ctx const& c) const {
        Node const& n = mG.nodes[static_cast<std::size_t>(i)];
        switch (n.kind) {
        case Kind::Constant:
            return n.v0;
        case Kind::Add:
            return eval(n.a, c) + eval(n.b, c);
        case Kind::Mul: {
            double d0 = eval(n.a, c);
            return d0 == 0.0 ? 0.0 : d0 * eval(n.b, c);
        }
        case Kind::Min: {
            double d0 = eval(n.a, c);
            Node const& nb = mG.nodes[static_cast<std::size_t>(n.b)];
            if (d0 < nb.lo) return d0;
            double d1 = eval(n.b, c);
            return std::isnan(d0) || std::isnan(d1) ? std::numeric_limits<double>::quiet_NaN() : (d0 <= d1 ? d0 : d1);
        }
        case Kind::Max: {
            double d0 = eval(n.a, c);
            Node const& nb = mG.nodes[static_cast<std::size_t>(n.b)];
            if (d0 > nb.hi) return d0;
            double d1 = eval(n.b, c);
            return std::isnan(d0) || std::isnan(d1) ? std::numeric_limits<double>::quiet_NaN() : (d0 >= d1 ? d0 : d1);
        }
        case Kind::AddConst:
            return eval(n.a, c) + n.v0;
        case Kind::MulConst:
            return eval(n.a, c) * n.v0;
        case Kind::Abs:
            return std::fabs(eval(n.a, c));
        case Kind::Square: {
            double d = eval(n.a, c);
            return d * d;
        }
        case Kind::Cube: {
            double d = eval(n.a, c);
            return d * d * d;
        }
        case Kind::HalfNegative: {
            double d = eval(n.a, c);
            return d > 0.0 ? d : d * 0.5;
        }
        case Kind::QuarterNegative: {
            double d = eval(n.a, c);
            return d > 0.0 ? d : d * 0.25;
        }
        case Kind::Squeeze: {
            double d0 = mth::clamp(eval(n.a, c), -1.0, 1.0);
            return d0 / 2.0 - d0 * d0 * d0 / 24.0;
        }
        case Kind::Clamp: {
            double d = eval(n.a, c);
            return d < n.v0 ? n.v0 : (std::isnan(d) ? d : (d <= n.v1 ? d : n.v1));
        }
        case Kind::YGradient:
            return mth::clampedMap(static_cast<double>(c.y), n.v0, n.v1, n.v2, n.v3);
        case Kind::Noise:
            return n.noise ? n.noise->getValue(static_cast<double>(c.x) * n.v0, static_cast<double>(c.y) * n.v1, static_cast<double>(c.z) * n.v0) : 0.0;
        case Kind::RangeChoice: {
            double d = eval(n.a, c);
            return d >= n.v0 && d < n.v1 ? eval(n.b, c) : eval(n.c, c);
        }
        case Kind::Interpolated:
            if (!c.fill || c.corner) return eval(n.a, c);
            return interpolate(n, c);
        case Kind::FlatCache: {
            if (!c.fill) return eval(n.a, c);
            Fill const& f = *c.fill;
            int         k = mth::fromBlock(c.x) - f.firstNoiseX;
            int         l = mth::fromBlock(c.z) - f.firstNoiseZ;
            int         len = f.noiseSizeXZ + 1;
            if (k >= 0 && l >= 0 && k < len && l < len) return f.flat[static_cast<std::size_t>(n.slot)][static_cast<std::size_t>(k * len + l)];
            return eval(n.a, c);
        }
        case Kind::PassThrough:
            return eval(n.a, c);
        case Kind::BiomeTerrain: {
            auto d = terrain(c);
            return n.v1 + d.depth * n.v0 + d.depth; // baseOffset + depth * baseFactor, + depth
        }
        case Kind::BiomeNoise:
            return terrain(c).scale;
        case Kind::Sqrt:
            return std::sqrt(eval(n.a, c));
        case Kind::Focused:
            return n.focused.compute(c.x, c.y, c.z);
        case Kind::HollowHill:
            return n.hollow.compute(c.x, c.y, c.z);
        case Kind::CoordMin:
        case Kind::CoordMax: {
            AbsoluteDifference d{n.kind == Kind::CoordMax, n.v0, n.v1, n.v2};
            return d.compute(c.x, c.z);
        }
        }
        return 0.0;
    }

    /// BiomeDensitySource#sampleTerrain; inside a fill the ChunkCached router's 16 x 16 cache
    /// (keyed by the section-relative column, as Java's).
    DensityData terrain(Ctx const& c) const {
        if (!c.fill) return mSampler ? mSampler(c.x, c.z) : DensityData{};
        Fill&       f   = *c.fill;
        std::size_t idx = static_cast<std::size_t>((c.z & 15) + ((c.x & 15) << 4));
        if (!f.columnSet[idx]) {
            f.columns[idx]   = mSampler ? mSampler(c.x, c.z) : DensityData{};
            f.columnSet[idx] = 1;
        }
        return f.columns[idx];
    }

    double interpolate(Node const& n, Ctx const& c) const {
        Fill const& f   = *c.fill;
        int         rx  = c.x - f.minX, rz = c.z - f.minZ;
        int         cx  = rx / f.cellWidth, cz = rz / f.cellWidth;
        int         cyA = mth::floorDiv(c.y, f.cellHeight);
        int         cy  = cyA - f.cellNoiseMinY;
        if (cx < 0 || cz < 0 || cx >= f.cellCountXZ || cz >= f.cellCountXZ || cy < 0 || cy >= f.cellCountY) {
            Ctx direct = c;
            direct.fill = nullptr;
            return eval(n.a, direct);
        }
        double dx = static_cast<double>(rx - cx * f.cellWidth) / static_cast<double>(f.cellWidth);
        double dz = static_cast<double>(rz - cz * f.cellWidth) / static_cast<double>(f.cellWidth);
        double dy = static_cast<double>(c.y - cyA * f.cellHeight) / static_cast<double>(f.cellHeight);
        auto const& s  = f.corners[static_cast<std::size_t>(n.slot)];
        int         ny = f.cellCountY + 1, nz = f.cellCountXZ + 1;
        auto at = [&](int x, int y, int z) { return s[static_cast<std::size_t>(((cx + x) * nz + (cz + z)) * ny + (cy + y))]; };
        double n000 = at(0, 0, 0), n001 = at(0, 0, 1), n010 = at(0, 1, 0), n011 = at(0, 1, 1);
        double n100 = at(1, 0, 0), n101 = at(1, 0, 1), n110 = at(1, 1, 0), n111 = at(1, 1, 1);
        // NoiseInterpolator: updateForY, updateForX, updateForZ
        double xz00 = mth::lerp(dy, n000, n010);
        double xz10 = mth::lerp(dy, n100, n110);
        double xz01 = mth::lerp(dy, n001, n011);
        double xz11 = mth::lerp(dy, n101, n111);
        double z0   = mth::lerp(dx, xz00, xz10);
        double z1   = mth::lerp(dx, xz01, xz11);
        return mth::lerp(dz, z0, z1);
    }

    /// NoiseChunk's initialization: the flat caches (quart corners, y 0) and the corner slices.
    void prepare(Fill& f) const {
        f.flat.assign(static_cast<std::size_t>(mG.flatCount), {});
        f.corners.assign(static_cast<std::size_t>(mG.interpCount), {});
        f.columns.assign(256, {});
        f.columnSet.assign(256, 0);
        int len = f.noiseSizeXZ + 1;
        // flat caches first (corners read them)
        for (std::size_t i = 0; i < mG.nodes.size(); ++i) {
            Node const& n = mG.nodes[i];
            if (n.kind != Kind::FlatCache) continue;
            auto& v = f.flat[static_cast<std::size_t>(n.slot)];
            v.resize(static_cast<std::size_t>(len * len));
            for (int a = 0; a < len; ++a)
                for (int b = 0; b < len; ++b) {
                    // FlatCache: noiseFiller.compute(SinglePointContext(toBlock(firstNoiseX + a), 0, toBlock(firstNoiseZ + b)))
                    Ctx p{mth::toBlock(f.firstNoiseX + a), 0, mth::toBlock(f.firstNoiseZ + b), &f, true};
                    v[static_cast<std::size_t>(a * len + b)] = evalFlatArg(n, p);
                }
        }
        int nx = f.cellCountXZ + 1, ny = f.cellCountY + 1;
        for (std::size_t i = 0; i < mG.nodes.size(); ++i) {
            Node const& n = mG.nodes[i];
            if (n.kind != Kind::Interpolated) continue;
            auto& v = f.corners[static_cast<std::size_t>(n.slot)];
            v.resize(static_cast<std::size_t>(nx * nx * ny));
            for (int cx = 0; cx < nx; ++cx)
                for (int cz = 0; cz < nx; ++cz)
                    for (int cy = 0; cy < ny; ++cy) {
                        Ctx p{f.minX + cx * f.cellWidth, (cy + f.cellNoiseMinY) * f.cellHeight, f.minZ + cz * f.cellWidth, &f, true};
                        v[static_cast<std::size_t>((cx * nx + cz) * ny + cy)] = eval(n.a, p);
                    }
        }
    }

private:
    /// A flat cache's argument at its fill point: nested flat caches of the same fill are not yet
    /// filled when an outer one fills (Java fills each FlatCache in its constructor, inner first,
    /// because mapAll wraps bottom-up); evaluate them directly, which gives the same value.
    double evalFlatArg(Node const& n, Ctx const& p) const {
        Ctx direct  = p;
        direct.fill = nullptr; // single point below (markers pass through, uncached routers)
        return eval(n.a, direct);
    }

    Density::Graph const& mG;
    TerrainSampler const& mSampler;
};

// ---- compiler ---------------------------------------------------------------------------------------------

std::string normalizeId(std::string id) {
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

class Compiler {
public:
    Compiler(Density::Graph& g, std::int64_t seed, Density::Resolver const& resolver)
    : mG(g),
      mSeed(seed),
      mResolver(resolver),
      mFactory(randomStateFactory(seed)) {}

    std::string error;

    int build(Json const& j) {
        if (j.is_number()) return constant(j.get<double>());
        if (j.is_string()) return ref(normalizeId(j.get<std::string>()));
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return fail("not a density function: " + j.dump().substr(0, 80));
        std::string type = normalizeId(j["type"].get<std::string>());
        auto num = [&](char const* key, double def) { return j.contains(key) && j[key].is_number() ? j[key].get<double>() : def; };
        auto arg = [&](char const* key) { return j.contains(key) ? build(j[key]) : fail(type + ": missing " + key); };

        if (type == "minecraft:constant") return constant(num("argument", 0.0));
        if (type == "minecraft:add" || type == "minecraft:mul" || type == "minecraft:min" || type == "minecraft:max") {
            int a = arg("argument1");
            if (a < 0) return -1;
            int b = arg("argument2");
            if (b < 0) return -1;
            return twoArg(type, a, b);
        }
        static std::map<std::string, Kind> const mapped{
            {"minecraft:abs", Kind::Abs},
            {"minecraft:square", Kind::Square},
            {"minecraft:cube", Kind::Cube},
            {"minecraft:half_negative", Kind::HalfNegative},
            {"minecraft:quarter_negative", Kind::QuarterNegative},
            {"minecraft:squeeze", Kind::Squeeze},
        };
        if (auto it = mapped.find(type); it != mapped.end()) {
            int a = arg("argument");
            if (a < 0) return -1;
            Node n;
            n.kind = it->second;
            n.a    = a;
            mappedBounds(n, node(a));
            return add(n);
        }
        if (type == "minecraft:clamp") {
            int a = arg("input");
            if (a < 0) return -1;
            Node n;
            n.kind = Kind::Clamp;
            n.a    = a;
            n.v0 = n.lo = num("min", 0.0);
            n.v1 = n.hi = num("max", 0.0);
            return add(n);
        }
        if (type == "minecraft:y_clamped_gradient") {
            Node n;
            n.kind = Kind::YGradient;
            n.v0   = static_cast<double>(static_cast<int>(num("from_y", 0)));
            n.v1   = static_cast<double>(static_cast<int>(num("to_y", 0)));
            n.v2   = num("from_value", 0);
            n.v3   = num("to_value", 0);
            n.lo   = std::min(n.v2, n.v3);
            n.hi   = std::max(n.v2, n.v3);
            return add(n);
        }
        if (type == "minecraft:noise") {
            auto noise = makeNoise(j.contains("noise") ? j["noise"] : Json());
            if (!noise) return fail("unknown noise in " + j.dump().substr(0, 80));
            Node n;
            n.kind  = Kind::Noise;
            n.noise = noise;
            n.v0    = num("xz_scale", 1.0);
            n.v1    = num("y_scale", 1.0);
            n.lo    = -noise->maxValue();
            n.hi    = noise->maxValue();
            return add(n);
        }
        if (type == "minecraft:range_choice") {
            int a = arg("input");
            if (a < 0) return -1;
            int b = arg("when_in_range");
            if (b < 0) return -1;
            int c = arg("when_out_of_range");
            if (c < 0) return -1;
            Node n;
            n.kind = Kind::RangeChoice;
            n.a = a, n.b = b, n.c = c;
            n.v0 = num("min_inclusive", 0);
            n.v1 = num("max_exclusive", 0);
            n.lo = std::min(node(b).lo, node(c).lo);
            n.hi = std::max(node(b).hi, node(c).hi);
            return add(n);
        }
        static std::map<std::string, Kind> const markers{
            {"minecraft:interpolated", Kind::Interpolated},
            {"minecraft:flat_cache", Kind::FlatCache},
            {"minecraft:cache_2d", Kind::PassThrough},
            {"minecraft:cache_once", Kind::PassThrough},
            {"minecraft:cache_all_in_cell", Kind::PassThrough},
        };
        if (auto it = markers.find(type); it != markers.end()) {
            int a = arg("argument");
            if (a < 0) return -1;
            Node n;
            n.kind = it->second;
            n.a    = a;
            n.lo   = node(a).lo;
            n.hi   = node(a).hi;
            if (n.kind == Kind::Interpolated) n.slot = mG.interpCount++;
            if (n.kind == Kind::FlatCache) n.slot = mG.flatCount++;
            return add(n);
        }
        if (type == "minecraft:blend_alpha") return constant(1.0);
        if (type == "minecraft:blend_offset" || type == "minecraft:beardifier") return constant(0.0);
        if (type == "minecraft:blend_density") {
            int a = arg("argument");
            if (a < 0) return -1;
            Node n;
            n.kind = Kind::PassThrough;
            n.a    = a;
            n.lo = node(a).lo, n.hi = node(a).hi;
            return add(n);
        }
        if (type == "gloamingforest:biome_driven_terrain" || type == "gloamingforest:biome_driven_noise") {
            Node n;
            n.kind = type == "gloamingforest:biome_driven_terrain" ? Kind::BiomeTerrain : Kind::BiomeNoise;
            n.lo   = num("lower_density_bound", -kInf);
            n.hi   = num("upper_density_bound", kInf);
            if (n.kind == Kind::BiomeTerrain) {
                // base_factor / base_offset are density functions in upstream's codec; upstream's data uses constants
                auto constOf = [&](char const* key, double& out) {
                    if (!j.contains(key)) return false;
                    int i = build(j[key]);
                    if (i < 0) return false;
                    if (node(i).kind != Kind::Constant) {
                        fail(type + ": non-constant " + key + " is not supported");
                        return false;
                    }
                    out = node(i).v0;
                    return true;
                };
                if (!constOf("base_factor", n.v0) || !constOf("base_offset", n.v1)) return error.empty() ? fail(type + ": base_factor / base_offset") : -1;
            }
            mG.usesTerrain = true;
            return add(n);
        }
        if (type == "gloamingforest:sqrt") {
            int a = arg("input");
            if (a < 0) return -1;
            Node n;
            n.kind = Kind::Sqrt;
            n.a    = a;
            n.lo = node(a).lo, n.hi = node(a).hi;
            return add(n);
        }
        if (type == "gloamingforest:focused") {
            Node n;
            n.kind    = Kind::Focused;
            n.focused = terrain::Focused{static_cast<float>(num("x_center", 0)), static_cast<float>(num("y_bottom", 0)), static_cast<float>(num("z_center", 0)),
                                         static_cast<float>(num("radius", 0)),   static_cast<float>(num("near_value", 0)), static_cast<float>(num("far_value", 0))};
            n.lo = 0.0;
            n.hi = static_cast<double>(n.focused.radius);
            return add(n);
        }
        if (type == "gloamingforest:hollow_hill") {
            Node n;
            n.kind   = Kind::HollowHill;
            n.hollow = terrain::HollowHill{static_cast<float>(num("x_center", 0)), static_cast<float>(num("y_bottom", 0)), static_cast<float>(num("z_center", 0)),
                                           static_cast<float>(num("radius", 0)),   static_cast<float>(num("height_scale", 0))};
            n.lo = -1.0, n.hi = 1.0;
            return add(n);
        }
        if (type == "gloamingforest:coord_min" || type == "gloamingforest:coord_max") {
            Node n;
            n.kind = type == "gloamingforest:coord_max" ? Kind::CoordMax : Kind::CoordMin;
            n.v0   = num("max", 0);
            n.v1   = num("x_center", 0);
            n.v2   = num("z_center", 0);
            n.lo = 0.0, n.hi = n.v0;
            return add(n);
        }
        return fail("unsupported density function type " + type);
    }

private:
    Node const& node(int i) const { return mG.nodes[static_cast<std::size_t>(i)]; }
    int add(Node n) {
        mG.nodes.push_back(std::move(n));
        return static_cast<int>(mG.nodes.size()) - 1;
    }
    int fail(std::string message) {
        if (error.empty()) error = std::move(message);
        return -1;
    }
    int constant(double v) {
        Node n;
        n.kind = Kind::Constant;
        n.v0 = n.lo = n.hi = v;
        return add(n);
    }

    static void mappedBounds(Node& n, Node const& in) {
        // DensityFunctions.Mapped.create: the transform of the input bounds
        double lo = in.lo, hi = in.hi;
        auto   t  = [&](double d) {
            switch (n.kind) {
            case Kind::Abs:
                return std::fabs(d);
            case Kind::Square:
                return d * d;
            case Kind::Cube:
                return d * d * d;
            case Kind::HalfNegative:
                return d > 0.0 ? d : d * 0.5;
            case Kind::QuarterNegative:
                return d > 0.0 ? d : d * 0.25;
            default: {
                double d0 = mth::clamp(d, -1.0, 1.0);
                return d0 / 2.0 - d0 * d0 * d0 / 24.0;
            }
            }
        };
        double d0 = t(lo), d1 = t(hi);
        if (n.kind == Kind::Abs || n.kind == Kind::Square) {
            n.lo = std::max(0.0, lo);
            n.hi = std::max(d0, d1);
        } else {
            n.lo = d0;
            n.hi = d1;
        }
    }

    int twoArg(std::string const& type, int a, int b) {
        Node const& n1 = node(a);
        Node const& n2 = node(b);
        double      d0 = n1.lo, d1 = n2.lo, d2 = n1.hi, d3 = n2.hi;
        Node        n;
        if (type == "minecraft:add") {
            n.kind = Kind::Add;
            n.lo = d0 + d1, n.hi = d2 + d3;
        } else if (type == "minecraft:min") {
            n.kind = Kind::Min;
            n.lo = std::min(d0, d1), n.hi = std::min(d2, d3);
        } else if (type == "minecraft:max") {
            n.kind = Kind::Max;
            n.lo = std::max(d0, d1), n.hi = std::max(d2, d3);
        } else {
            n.kind = Kind::Mul;
            n.lo   = (d0 > 0.0 && d1 > 0.0) ? d0 * d1 : ((d2 < 0.0 && d3 < 0.0) ? d2 * d3 : std::min(d0 * d3, d2 * d1));
            n.hi   = (d0 > 0.0 && d1 > 0.0) ? d2 * d3 : ((d2 < 0.0 && d3 < 0.0) ? d0 * d1 : std::max(d0 * d1, d2 * d3));
        }
        if (n.kind == Kind::Add || n.kind == Kind::Mul) {
            // TwoArgumentSimpleFunction.create: a constant argument makes a MulOrAdd
            bool c1 = n1.kind == Kind::Constant, c2 = n2.kind == Kind::Constant;
            if (c1 || c2) {
                double value = c1 ? n1.v0 : n2.v0;
                int    input = c1 ? b : a;
                n.kind       = n.kind == Kind::Add ? Kind::AddConst : Kind::MulConst;
                n.a          = input;
                n.v0         = value;
                return add(n);
            }
        }
        n.a = a;
        n.b = b;
        return add(n);
    }

    int ref(std::string const& id) {
        if (auto it = mRefs.find(id); it != mRefs.end()) return it->second;
        if (mBuilding.contains(id)) return fail("recursive density reference " + id);
        std::optional<Json> json;
        if (mResolver.density) json = mResolver.density(id);
        if (!json) return fail("unknown density function " + id);
        mBuilding.insert(id);
        int i = build(*json);
        mBuilding.erase(id);
        if (i >= 0) mRefs[id] = i;
        return i;
    }

    std::shared_ptr<noise::NormalNoise const> makeNoise(Json const& j) {
        std::string                           key;
        std::optional<noise::NoiseParameters> params;
        if (j.is_string()) {
            key = normalizeId(j.get<std::string>());
            if (auto it = mNoises.find(key); it != mNoises.end()) return it->second;
            if (mResolver.noise) params = mResolver.noise(key);
            if (!params) params = vanillaNoise(key);
        } else if (j.is_object() && j.contains("amplitudes")) {
            key = j.dump();
            noise::NoiseParameters p;
            p.firstOctave = j.value("firstOctave", 0);
            for (auto const& a : j["amplitudes"]) p.amplitudes.push_back(a.get<double>());
            params = p;
        }
        if (!params) return nullptr;
        auto src   = mFactory->fromHashOf(key);
        auto noise = std::make_shared<noise::NormalNoise const>(noise::NormalNoise::create(*src, *params));
        mNoises.emplace(key, noise);
        return noise;
    }

    Density::Graph&                                                mG;
    std::int64_t                                                   mSeed;
    Density::Resolver const&                                       mResolver;
    std::unique_ptr<jrand::PositionalRandomFactory>                mFactory;
    std::map<std::string, int>                                     mRefs;
    std::set<std::string>                                          mBuilding;
    std::map<std::string, std::shared_ptr<noise::NormalNoise const>> mNoises;
};

void setupFill(Fill& f, noise::NoiseShape const& shape, int minX, int minZ, int cellCountXZ) {
    f.cellWidth     = mth::toBlock(shape.sizeHorizontal);
    f.cellHeight    = mth::toBlock(shape.sizeVertical);
    f.cellCountXZ   = cellCountXZ;
    f.cellCountY    = mth::floorDiv(shape.height, f.cellHeight);
    f.cellNoiseMinY = mth::floorDiv(shape.minY, f.cellHeight);
    f.minX          = minX;
    f.minZ          = minZ;
    f.firstNoiseX   = mth::fromBlock(minX);
    f.firstNoiseZ   = mth::fromBlock(minZ);
    f.noiseSizeXZ   = mth::fromBlock(cellCountXZ * f.cellWidth);
}

} // namespace

// ---- Density ----------------------------------------------------------------------------------------------

std::shared_ptr<Density const>
Density::compile(nlohmann::json const& json, std::int64_t seed, Resolver const& resolver, noise::NoiseShape shape, std::string* error) {
    auto     g = std::make_shared<Graph>();
    Compiler c(*g, seed, resolver);
    int      root = c.build(json);
    if (root < 0) {
        if (error) *error = c.error.empty() ? "density function does not compile" : c.error;
        return nullptr;
    }
    g->root = root;
    return std::make_shared<Density const>(std::move(g), shape);
}

void Density::fillChunk(int chunkX, int chunkZ, TerrainSampler const& sampler, std::vector<double>& out, ExtraDensity const& extra) const {
    Fill f;
    setupFill(f, mShape, chunkX * 16, chunkZ * 16, 16 / mth::toBlock(mShape.sizeHorizontal));
    Evaluator ev(*mGraph, sampler);
    ev.prepare(f);
    int height = f.cellCountY * f.cellHeight;
    out.assign(static_cast<std::size_t>(mShape.height) * 256, 0.0);
    int minY = f.cellNoiseMinY * f.cellHeight;
    for (int y = minY; y < minY + height; ++y) {
        if (y - mShape.minY < 0 || y - mShape.minY >= mShape.height) continue;
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                Ctx    c{f.minX + x, y, f.minZ + z, &f, false};
                double d = ev.eval(mGraph->root, c);
                if (extra) d += extra(c.x, y, c.z);
                out[static_cast<std::size_t>(((y - mShape.minY) * 16 + z) * 16 + x)] = d;
            }
    }
}

void Density::fillColumn(int x, int z, TerrainSampler const& sampler, std::vector<double>& out) const {
    Fill f;
    int  cw = mth::toBlock(mShape.sizeHorizontal);
    setupFill(f, mShape, mth::floorDiv(x, cw) * cw, mth::floorDiv(z, cw) * cw, 1);
    Evaluator ev(*mGraph, sampler);
    ev.prepare(f);
    out.assign(static_cast<std::size_t>(mShape.height), 0.0);
    int minY = f.cellNoiseMinY * f.cellHeight;
    int height = f.cellCountY * f.cellHeight;
    for (int y = minY; y < minY + height; ++y) {
        if (y - mShape.minY < 0 || y - mShape.minY >= mShape.height) continue;
        Ctx c{x, y, z, &f, false};
        out[static_cast<std::size_t>(y - mShape.minY)] = ev.eval(mGraph->root, c);
    }
}

double Density::computePoint(int x, int y, int z, TerrainSampler const& sampler) const {
    Evaluator ev(*mGraph, sampler);
    return ev.eval(mGraph->root, Ctx{x, y, z, nullptr, false});
}

double Density::minValue() const { return mGraph->nodes[static_cast<std::size_t>(mGraph->root)].lo; }
double Density::maxValue() const { return mGraph->nodes[static_cast<std::size_t>(mGraph->root)].hi; }

// ---- noises ---------------------------------------------------------------------------------------------

std::optional<noise::NoiseParameters> vanillaNoise(std::string const& id) {
    // Minecraft 1.21.1 NoiseData#bootstrap
    static std::map<std::string, noise::NoiseParameters> const table{
        {"minecraft:ridge", {-7, {1.0, 2.0, 1.0, 0.0, 0.0, 0.0}}},
        {"minecraft:surface", {-6, {1.0, 1.0, 1.0}}},
        {"minecraft:surface_secondary", {-6, {1.0, 1.0, 0.0, 1.0}}},
        {"minecraft:clay_bands_offset", {-8, {1.0}}},
        {"minecraft:jagged", {-16, {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0}}},
        {"minecraft:cave_entrance", {-7, {0.4, 0.5, 1.0}}},
        {"minecraft:cave_layer", {-8, {1.0}}},
        {"minecraft:cave_cheese", {-8, {0.5, 1.0, 2.0, 1.0, 2.0, 1.0, 0.0, 2.0, 0.0}}},
        {"minecraft:offset", {-3, {1.0, 1.0, 1.0, 0.0}}},
    };
    auto it = table.find(id);
    if (it == table.end()) return std::nullopt;
    return it->second;
}

std::unique_ptr<jrand::PositionalRandomFactory> randomStateFactory(std::int64_t seed) { return jrand::Xoroshiro128pp(seed).forkPositional(); }

noise::NormalNoise randomStateNoise(std::int64_t seed, std::string const& id, noise::NoiseParameters const& params) {
    auto factory = randomStateFactory(seed);
    auto src     = factory->fromHashOf(id);
    return noise::NormalNoise::create(*src, params);
}

} // namespace gf::core::terrain

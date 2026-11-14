// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/layer/BiomeDensitySource.java
//   src/main/java/gloamingforest/world/components/layer/RandomBiomeLayer.java
//   src/main/java/gloamingforest/world/components/layer/KeyBiomesLayer.java
//   src/main/java/gloamingforest/world/components/layer/CompanionBiomesLayer.java
//   src/main/java/gloamingforest/world/components/layer/BorderLayer.java
//   src/main/java/gloamingforest/world/components/layer/StabilizeLayer.java
//   src/main/java/gloamingforest/world/components/layer/SeamLayer.java
//   src/main/java/gloamingforest/world/components/layer/FilteredBiomeLayer.java
//   src/main/java/gloamingforest/world/components/layer/MedianLayer.java
//   src/main/java/gloamingforest/world/components/layer/IThornsTransformer.java
//   src/main/java/gloamingforest/world/components/layer/vanillalegacy/ZoomLayer.java
//   src/main/java/gloamingforest/world/components/layer/vanillalegacy/SmoothLayer.java
//   src/main/java/gloamingforest/world/components/layer/vanillalegacy/area/LazyArea.java
//   src/main/java/gloamingforest/world/components/layer/vanillalegacy/context/LazyAreaContext.java
//   src/main/java/gloamingforest/world/components/layer/vanillalegacy/context/RandomContext.java
//   src/main/java/gloamingforest/world/components/layer/vanillalegacy/traits/CastleTransformer.java
//   src/main/java/gloamingforest/world/components/biomesources/TFBiomeProvider.java
//   src/main/java/gloamingforest/world/components/chunkgenerators/TerrainColumn.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/biomes/BiomeLayers.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <set>
#include <stdexcept>

#include "core/jrand/BiomeManager.h"
#include "core/jrand/JRand.h"

#pragma clang fp contract(off)

namespace gf::core::biomes {

namespace {

using core::biome::lcgNext;

// ---- ids -------------------------------------------------------------------------------------

/// "forest" -> "minecraft:forest" (ResourceLocation's default namespace).
std::string normalize(std::string id) {
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

// ---- RandomContext ---------------------------------------------------------------------------

/// Math.floorMod(long, int).
int floorMod(std::int64_t x, int y) {
    std::int64_t m = x % y;
    if (m != 0 && ((m < 0) != (y < 0))) m += y;
    return static_cast<int>(m);
}

/// RandomContext: the per-pixel LCG of the vanilla legacy layers.
struct RandomContext {
    std::int64_t seed = 0;
    std::int64_t rval = 0;

    explicit RandomContext(std::int64_t s) : seed(s) {}
    void initRandom(std::int64_t x, std::int64_t z) {
        std::int64_t i = seed;
        i              = lcgNext(i, x);
        i              = lcgNext(i, z);
        i              = lcgNext(i, x);
        i              = lcgNext(i, z);
        rval           = i;
    }
    int nextRandom(int limit) {
        int result = floorMod(rval >> 24, limit);
        rval       = lcgNext(rval, seed);
        return result;
    }
    BiomeKey random(BiomeKey a, BiomeKey b) { return nextRandom(2) == 0 ? a : b; }
    BiomeKey random(BiomeKey a, BiomeKey b, BiomeKey c, BiomeKey d) {
        int i = nextRandom(4);
        return i == 0 ? a : i == 1 ? b : i == 2 ? c : d;
    }
};

// ---- LazyArea --------------------------------------------------------------------------------

/// A layer with upstream's LazyArea cache. The cache only avoids recomputation (upstream's samples are pure
/// functions of (x, z)); here it is a lock-free direct-mapped table whose entries carry their own
/// coordinates, so concurrent readers never see another pixel's value.
class CachedLayer : public Layer {
public:
    explicit CachedLayer(std::int64_t contextSeed) : mSeed(contextSeed), mCache(new std::atomic<std::uint64_t>[kSize]) {
        for (std::size_t i = 0; i < kSize; ++i) mCache[i].store(0, std::memory_order_relaxed);
    }

    BiomeKey get(int x, int z) const final {
        constexpr int kLimit = 1 << 23;
        bool          inRange = x >= -kLimit && x < kLimit && z >= -kLimit && z < kLimit;
        if (!inRange) return compute(x, z);
        std::uint64_t pos  = (std::uint64_t(std::uint32_t(x + kLimit)) << 24) | std::uint64_t(std::uint32_t(z + kLimit));
        std::size_t   slot = static_cast<std::size_t>((pos * 0x9E3779B97F4A7C15ULL) >> (64 - kBits));
        std::uint64_t e    = mCache[slot].load(std::memory_order_relaxed);
        if ((e >> 63) != 0 && (e & kPosMask) == pos) return static_cast<BiomeKey>((e >> 48) & 0x7FFF);
        BiomeKey v = compute(x, z);
        mCache[slot].store((std::uint64_t(1) << 63) | (std::uint64_t(v & 0x7FFF) << 48) | pos, std::memory_order_relaxed);
        return v;
    }

protected:
    /// AreaTransformer#applyPixel with a fresh RandomContext initialised at (x, z).
    virtual BiomeKey compute(int x, int z) const = 0;
    RandomContext    context(int x, int z) const {
        RandomContext c(mSeed);
        c.initRandom(x, z);
        return c;
    }

    std::int64_t mSeed;

private:
    static constexpr int           kBits    = 13;
    static constexpr std::size_t   kSize    = std::size_t(1) << kBits;
    static constexpr std::uint64_t kPosMask = (std::uint64_t(1) << 48) - 1;
    std::unique_ptr<std::atomic<std::uint64_t>[]> mCache;
};

// ---- the layer types ---------------------------------------------------------------------------

/// RandomBiomeLayer (AreaTransformer0).
class RandomBiomeLayer final : public CachedLayer {
public:
    RandomBiomeLayer(std::int64_t s, int chance, std::vector<BiomeKey> common, std::vector<BiomeKey> rare)
    : CachedLayer(s),
      mChance(chance),
      mCommon(std::move(common)),
      mRare(std::move(rare)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        RandomContext c = context(x, z);
        if (c.nextRandom(mChance) == 0) return mRare.at(c.nextRandom(static_cast<int>(mRare.size())));
        return mCommon.at(c.nextRandom(static_cast<int>(mCommon.size())));
    }

private:
    int                   mChance;
    std::vector<BiomeKey> mCommon, mRare;
};

/// KeyBiomesLayer: one key biome per 8x8 region (the 4 quadrants rotate the 4 key biomes).
class KeyBiomesLayer final : public CachedLayer {
public:
    KeyBiomesLayer(std::int64_t s, std::int64_t worldSeed, std::vector<BiomeKey> keys, std::unique_ptr<Layer> parent)
    : CachedLayer(s),
      mWorldSeed(worldSeed),
      mKeys(std::move(keys)),
      mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        // new Random(seed + (x & -4) * 25117L + (z & -4) * 151121L): 64-bit wrapping products
        auto mix = [&](std::int64_t a, std::int64_t b) {
            return static_cast<std::int64_t>(
                std::uint64_t(mWorldSeed) + std::uint64_t(a) * 25117ULL + std::uint64_t(b) * 151121ULL
            );
        };
        jrand::LegacyRandom rand(mix(x & -4, z & -4));
        int                 ox = rand.nextInt(2) + 1;
        int                 oz = rand.nextInt(2) + 1;
        rand.setSeed(mix(x / 8, z / 8)); // Java int division: truncates toward zero
        int offset = rand.nextInt(3);
        if ((x & 3) == ox && (z & 3) == oz) {
            if ((x & 4) == 0) return key((z & 4) == 0 ? offset : offset + 1);
            return key((z & 4) == 0 ? offset + 2 : offset + 3);
        }
        return mParent->get(x, z);
    }

private:
    BiomeKey key(int index) const { return mKeys.at(static_cast<std::size_t>(index & 0b11)); }

    std::int64_t           mWorldSeed;
    std::vector<BiomeKey>  mKeys;
    std::unique_ptr<Layer> mParent;
};

/// CastleTransformer: the 4 neighbours and the centre of the parent at (x, z).
struct Castle {
    BiomeKey up, right, down, left, center;
    static Castle at(Layer const& p, int x, int z) {
        // getParentX/Y = x - 1 / z - 1 (DimensionOffset1Transformer), sampled at +1 / +2
        return Castle{p.get(x, z - 1), p.get(x + 1, z), p.get(x, z + 1), p.get(x - 1, z), p.get(x, z)};
    }
};

/// CompanionBiomesLayer: a pixel touching a key biome (4-neighbours) becomes its companion.
class CompanionBiomesLayer final : public CachedLayer {
public:
    CompanionBiomesLayer(std::int64_t s, std::vector<std::pair<BiomeKey, BiomeKey>> pairs, std::unique_ptr<Layer> parent)
    : CachedLayer(s),
      mPairs(std::move(pairs)),
      mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        Castle n = Castle::at(*mParent, x, z);
        for (auto const& [key, companion] : mPairs) {
            if (n.center != key && (n.right == key || n.left == key || n.up == key || n.down == key)) return companion;
        }
        return n.center;
    }

private:
    std::vector<std::pair<BiomeKey, BiomeKey>> mPairs;
    std::unique_ptr<Layer>                     mParent;
};

/// ZoomLayer (NORMAL or FUZZY).
class ZoomLayer final : public CachedLayer {
public:
    ZoomLayer(std::int64_t s, bool fuzzy, std::unique_ptr<Layer> parent) : CachedLayer(s), mFuzzy(fuzzy), mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        Layer const&  p = *mParent;
        BiomeKey      i = p.get(x >> 1, z >> 1);
        RandomContext c(mSeed);
        c.initRandom(x >> 1 << 1, z >> 1 << 1);
        int j = x & 1;
        int k = z & 1;
        if (j == 0 && k == 0) return i;
        BiomeKey l  = p.get(x >> 1, (z + 1) >> 1);
        BiomeKey i1 = c.random(i, l);
        if (j == 0 && k == 1) return i1;
        BiomeKey j1 = p.get((x + 1) >> 1, z >> 1);
        BiomeKey k1 = c.random(i, j1);
        if (j == 1 && k == 0) return k1;
        BiomeKey l1 = p.get((x + 1) >> 1, (z + 1) >> 1);
        return mFuzzy ? c.random(i, j1, l, l1) : modeOrRandom(c, i, j1, l, l1);
    }

private:
    static BiomeKey modeOrRandom(RandomContext& c, BiomeKey a, BiomeKey b, BiomeKey d, BiomeKey e) {
        if (b == d && d == e) return b;
        if (a == b && a == d) return a;
        if (a == b && a == e) return a;
        if (a == d && a == e) return a;
        if (a == b && d != e) return a;
        if (a == d && b != e) return a;
        if (a == e && b != d) return a;
        if (b == d && a != e) return b;
        if (b == e && a != d) return b;
        return d == e && a != b ? d : c.random(a, b, d, e);
    }

    bool                   mFuzzy;
    std::unique_ptr<Layer> mParent;
};

/// StabilizeLayer: the 3x3 around each 4x4 centre copies the centre (offX = (x << 4) & 3 = 0).
class StabilizeLayer final : public CachedLayer {
public:
    StabilizeLayer(std::int64_t s, std::unique_ptr<Layer> parent) : CachedLayer(s), mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        int offX    = (x << 4) & 3;
        int offZ    = (z << 4) & 3;
        int centerX = ((x + offX + 1) & -4) - offX;
        int centerZ = ((z + offZ + 1) & -4) - offZ;
        if (x <= centerX + 1 && x >= centerX - 1 && z <= centerZ + 1 && z >= centerZ - 1) return mParent->get(centerX, centerZ);
        return mParent->get(x, z);
    }

private:
    std::unique_ptr<Layer> mParent;
};

/// BorderLayer (IThornsTransformer): pixels next to the target (4-neighbours, then diagonals)
/// become the border biome.
class BorderLayer final : public CachedLayer {
public:
    BorderLayer(std::int64_t s, BiomeKey target, BiomeKey border, std::unique_ptr<Layer> parent)
    : CachedLayer(s),
      mTarget(target),
      mBorder(border),
      mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        Layer const& p = *mParent;
        // IThornsTransformer#applyPixel's samples (getParentX/Y = x - 1 / z - 1), in its order
        BiomeKey up = p.get(x, z - 1), left = p.get(x + 1, z), down = p.get(x, z + 1), right = p.get(x - 1, z);
        BiomeKey center = p.get(x, z), nw = p.get(x + 1, z - 1), sw = p.get(x + 1, z + 1), se = p.get(x - 1, z + 1);
        BiomeKey ne = p.get(x - 1, z - 1);
        auto onBorder = [&](BiomeKey a, BiomeKey b, BiomeKey c, BiomeKey d) {
            return center != mTarget && (a == mTarget || b == mTarget || c == mTarget || d == mTarget);
        };
        if (onBorder(right, left, up, down)) return mBorder;
        if (onBorder(ne, nw, se, sw)) return mBorder;
        return center;
    }

private:
    BiomeKey               mTarget, mBorder;
    std::unique_ptr<Layer> mParent;
};

/// SeamLayer: the dividing biome between differing neighbours, unless excluded.
class SeamLayer final : public CachedLayer {
public:
    SeamLayer(
        std::int64_t                               s,
        BiomeKey                                   dividing,
        std::vector<BiomeKey>                      excluded,
        std::vector<std::pair<BiomeKey, BiomeKey>> excludedPairs,
        std::unique_ptr<Layer>                     parent
    )
    : CachedLayer(s),
      mDividing(dividing),
      mExcluded(std::move(excluded)),
      mExcludedPairs(std::move(excludedPairs)),
      mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        Castle n = Castle::at(*mParent, x, z);
        // apply(up, left, down, right, mid): the castle's right / left arrive swapped, all four tested
        if (partition(n.center, n.right) || partition(n.center, n.left) || partition(n.center, n.down) || partition(n.center, n.up))
            return mDividing;
        return n.center;
    }

private:
    bool partition(BiomeKey a, BiomeKey b) const {
        if (a == b) return false;
        for (BiomeKey e : mExcluded)
            if (a == e || b == e) return false;
        for (auto const& [p, q] : mExcludedPairs)
            if ((a == p && b == q) || (b == p && a == q)) return false;
        return true;
    }

    BiomeKey                                   mDividing;
    std::vector<BiomeKey>                      mExcluded;
    std::vector<std::pair<BiomeKey, BiomeKey>> mExcludedPairs;
    std::unique_ptr<Layer>                     mParent;
};

/// SmoothLayer (CastleTransformer).
class SmoothLayer final : public CachedLayer {
public:
    SmoothLayer(std::int64_t s, std::unique_ptr<Layer> parent) : CachedLayer(s), mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        Castle        n      = Castle::at(*mParent, x, z);
        RandomContext c      = context(x, z);
        bool          xMatch = n.right == n.left;
        bool          zMatch = n.up == n.down;
        if (xMatch == zMatch) {
            if (xMatch) return c.nextRandom(2) == 0 ? n.left : n.up;
            return n.center;
        }
        return xMatch ? n.left : n.up;
    }

private:
    std::unique_ptr<Layer> mParent;
};

/// FilteredBiomeLayer (AreaTransformer2): the kept biome of the filtered layer, else the fallback.
class FilteredBiomeLayer final : public CachedLayer {
public:
    FilteredBiomeLayer(std::int64_t s, BiomeKey keep, std::unique_ptr<Layer> fallback, std::unique_ptr<Layer> filtered)
    : CachedLayer(s),
      mKeep(keep),
      mFallback(std::move(fallback)),
      mFiltered(std::move(filtered)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        BiomeKey river = mFiltered->get(x, z);
        if (river == mKeep) return river;
        return mFallback->get(x, z);
    }

private:
    BiomeKey               mKeep;
    std::unique_ptr<Layer> mFallback, mFiltered;
};

/// MedianLayer as upstream's code behaves: the first biome (index order) seen at least 5 times in the
/// 3x3 (x .. x+2, z .. z+2), else the sample at (x, z). The tie branch never runs (its count
/// starts at 0 and every count is at least 1), so no context random is drawn.
class MedianLayer final : public CachedLayer {
public:
    MedianLayer(std::int64_t s, std::unique_ptr<Layer> parent) : CachedLayer(s), mParent(std::move(parent)) {}

protected:
    BiomeKey compute(int x, int z) const override {
        BiomeKey b[9];
        for (int pos = 0; pos < 9; ++pos) b[pos] = mParent->get(x + pos % 3, z + pos / 3);
        for (int index = 0; index < 9; ++index) {
            int n = 0;
            for (int test = 0; test < 9; ++test) {
                if (b[index] == b[test] && ++n > 4) return b[index];
            }
        }
        return b[0];
    }

private:
    std::unique_ptr<Layer> mParent;
};

// ---- building from JSON ------------------------------------------------------------------------

std::string typeOf(Json const& j) {
    std::string t = normalize(j.at("layer_type").get<std::string>());
    if (t.starts_with("gloamingforest:")) return t.substr(15);
    throw std::runtime_error("unknown biome layer type " + t);
}

std::vector<BiomeKey> keysOf(Json const& arr, BiomeNames& names) {
    std::vector<BiomeKey> out;
    for (auto const& v : arr) out.push_back(names.intern(normalize(v.get<std::string>())));
    return out;
}

std::unique_ptr<Layer> build(Json const& j, LayerStackRegistry const& reg, BiomeNames& names, std::int64_t seed, int depth) {
    if (depth > 64) throw std::runtime_error("biome layer stack nests too deep (a cycle?)");
    if (j.is_string()) {
        auto id = normalize(j.get<std::string>());
        auto it = reg.entries.find(id);
        if (it == reg.entries.end()) throw std::runtime_error("unknown biome layer stack " + id);
        return build(it->second, reg, names, seed, depth + 1);
    }
    std::string  type = typeOf(j);
    std::int64_t s    = mixSeed(seed, j.at("salt").get<std::int64_t>());
    auto parent       = [&](char const* key = "parent") { return build(j.at(key), reg, names, seed, depth + 1); };
    auto biome        = [&](char const* key) { return names.intern(normalize(j.at(key).get<std::string>())); };
    if (type == "random_biomes")
        return std::make_unique<RandomBiomeLayer>(
            s,
            j.at("rare_biome_chance").get<int>(),
            keysOf(j.at("common_biomes"), names),
            keysOf(j.at("rare_biomes"), names)
        );
    if (type == "key_biomes") {
        auto keys = keysOf(j.at("key_biomes"), names);
        if (keys.size() < 4) throw std::runtime_error("key_biomes needs 4 biomes");
        return std::make_unique<KeyBiomesLayer>(s, seed, std::move(keys), parent());
    }
    if (type == "companion_biomes") {
        std::vector<std::pair<BiomeKey, BiomeKey>> pairs;
        for (auto const& p : j.at("keys_to_companions"))
            pairs.emplace_back(names.intern(normalize(p.at("key").get<std::string>())), names.intern(normalize(p.at("companion").get<std::string>())));
        return std::make_unique<CompanionBiomesLayer>(s, std::move(pairs), parent());
    }
    if (type == "zoom") return std::make_unique<ZoomLayer>(s, j.value("fuzzy", false), parent());
    if (type == "stabilize") return std::make_unique<StabilizeLayer>(s, parent());
    if (type == "border") return std::make_unique<BorderLayer>(s, biome("target_biome"), biome("bordering_biome"), parent());
    if (type == "seam") {
        std::vector<std::pair<BiomeKey, BiomeKey>> pairs;
        for (auto const& p : j.at("excluded_biome_intersections"))
            pairs.emplace_back(names.intern(normalize(p.at(0).get<std::string>())), names.intern(normalize(p.at(1).get<std::string>())));
        auto dividing = biome("dividing_biome");
        auto excluded = keysOf(j.at("excluded_neighbor_biomes"), names);
        return std::make_unique<SeamLayer>(s, dividing, std::move(excluded), std::move(pairs), parent());
    }
    if (type == "smooth") return std::make_unique<SmoothLayer>(s, parent());
    if (type == "filtered") {
        auto keep     = biome("keep_biome");
        auto fallback = parent("fallback_layer"); // FilteredBiomeLayer.Factory#build builds the fallback first
        auto filtered = parent("filtered_layer");
        return std::make_unique<FilteredBiomeLayer>(s, keep, std::move(fallback), std::move(filtered));
    }
    if (type == "median") return std::make_unique<MedianLayer>(s, parent());
    throw std::runtime_error("unknown biome layer type gloamingforest:" + type);
}

/// The terrain values are density function holders in upstream; its data ships constants.
double constantOf(Json const& j, char const* key) {
    auto const& v = j.at(key);
    if (v.is_number()) return v.get<double>();
    if (v.is_object() && v.value("type", "") == "minecraft:constant") return v.at("argument").get<double>();
    throw std::runtime_error(std::string("biome terrain value ") + key + " is not a constant");
}

} // namespace

// ---- public ------------------------------------------------------------------------------------

BiomeKey BiomeNames::intern(std::string const& id) {
    if (auto it = mKeys.find(id); it != mKeys.end()) return it->second;
    if (mNames.size() >= 0x7FFF) throw std::runtime_error("too many biome ids");
    auto key = static_cast<BiomeKey>(mNames.size());
    mNames.push_back(id);
    mKeys.emplace(id, key);
    return key;
}

std::optional<BiomeKey> BiomeNames::find(std::string const& id) const {
    if (auto it = mKeys.find(id); it != mKeys.end()) return it->second;
    return std::nullopt;
}

std::int64_t mixSeed(std::int64_t seed, std::int64_t salt) {
    std::int64_t i = lcgNext(salt, salt);
    i              = lcgNext(i, salt);
    i              = lcgNext(i, salt);
    std::int64_t j = lcgNext(seed, i);
    j              = lcgNext(j, i);
    return lcgNext(j, i);
}

std::unique_ptr<Layer> buildLayer(Json const& layer, LayerStackRegistry const& registry, BiomeNames& names, std::int64_t seed) {
    return build(layer, registry, names, seed, 0);
}

std::string const& TerrainColumn::biomeAt(int quartY) const {
    // reduce((a, b) -> |a - qy| <= |b - qy| ? a : b) over the ascending layers
    std::pair<double, std::string> const* best = nullptr;
    for (auto const& layer : layers) {
        if (!best || !(std::abs(best->first - quartY) <= std::abs(layer.first - quartY))) best = &layer;
    }
    return best ? best->second : key;
}

bool resourceLocationLess(std::string const& a, std::string const& b) {
    auto split = [](std::string const& id) -> std::pair<std::string, std::string> {
        auto c = id.find(':');
        if (c == std::string::npos) return {"minecraft", id};
        return {id.substr(0, c), id.substr(c + 1)};
    };
    auto [na, pa] = split(a);
    auto [nb, pb] = split(b);
    if (pa != pb) return pa < pb;
    return na < nb;
}

std::vector<TerrainColumn> terrainColumnsOf(Json const& terrainData) {
    std::vector<TerrainColumn> columns;
    for (auto const& c : terrainData.at("biome_landscape")) {
        TerrainColumn col;
        col.key    = normalize(c.at("key_biome").get<std::string>());
        col.depth  = constantOf(c, "depth");
        col.scale  = constantOf(c, "scale");
        col.weight = constantOf(c, "weight");
        for (auto const& [k, v] : c.at("biome_layers").items()) col.layers.emplace_back(std::stod(k), normalize(v.get<std::string>()));
        std::sort(col.layers.begin(), col.layers.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        columns.push_back(std::move(col));
    }
    std::stable_sort(columns.begin(), columns.end(), [](auto const& a, auto const& b) { return resourceLocationLess(a.key, b.key); });
    return columns;
}

std::vector<std::string> possibleBiomesOf(Json const& terrainData) {
    std::vector<std::string> out;
    std::set<std::string>    seen;
    for (auto const& col : terrainColumnsOf(terrainData)) {
        for (auto const& [elevation, biome] : col.layers) {
            if (seen.insert(biome).second) out.push_back(biome);
        }
    }
    return out;
}

BiomeProvider::BiomeProvider(Json const& terrainData, LayerStackRegistry const& stacks, std::int64_t seed) : mSeed(seed) {
    mStack   = buildLayer(terrainData.at("biome_layer_config"), stacks, mNames, seed);
    mColumns = terrainColumnsOf(terrainData);
    for (auto const& col : mColumns) mColumnByName[col.key] = &col;
    mColumnOfKey.assign(mNames.size(), nullptr);
    for (std::size_t i = 0; i < mNames.size(); ++i) {
        if (auto it = mColumnByName.find(mNames.name(static_cast<BiomeKey>(i))); it != mColumnByName.end()) mColumnOfKey[i] = it->second;
    }
}

std::string const& BiomeProvider::keyBiome(int quartX, int quartZ) const { return mNames.name(mStack->get(quartX, quartZ)); }

TerrainColumn const* BiomeProvider::terrainColumn(int quartX, int quartZ) const {
    BiomeKey k = mStack->get(quartX, quartZ);
    return k < mColumnOfKey.size() ? mColumnOfKey[k] : nullptr;
}

TerrainColumn const* BiomeProvider::terrainColumn(std::string const& keyBiome) const {
    auto it = mColumnByName.find(keyBiome);
    return it == mColumnByName.end() ? nullptr : it->second;
}

std::string const& BiomeProvider::mainBiome(int quartX, int quartZ) const {
    BiomeKey k = mStack->get(quartX, quartZ);
    if (k < mColumnOfKey.size() && mColumnOfKey[k]) return mColumnOfKey[k]->key;
    return mNames.name(k); // upstream throws here (no column); the stack's biome is the best answer
}

std::string const& BiomeProvider::noiseBiome(int quartX, int quartY, int quartZ) const {
    BiomeKey k = mStack->get(quartX, quartZ);
    if (k < mColumnOfKey.size() && mColumnOfKey[k]) return mColumnOfKey[k]->biomeAt(quartY);
    return mNames.name(k);
}

DensityData BiomeProvider::sampleTerrain(int blockX, int blockZ) const {
    constexpr double kBlendRadius    = 8.75;
    constexpr int    kBlendRadiusInt = 9; // Mth.floor(8.75 + 1.0)
    constexpr int    kOffset         = 2; // QuartPos.SIZE / 2
    double const     minus04         = static_cast<double>(-0.4f);
    double           totalDepth = 0.0, totalContribution = 0.0, totalScale = 0.0, totalScaleContribution = 0.0;
    int              bxo         = blockX - kOffset;
    int              bzo         = blockZ - kOffset;
    int              xQuartStart = (bxo - kBlendRadiusInt) >> 2;
    int              zQuartStart = (bzo - kBlendRadiusInt) >> 2;
    int              xQuartEnd   = (bxo + kBlendRadiusInt) >> 2;
    int              zQuartEnd   = (bzo + kBlendRadiusInt) >> 2;
    int              xCount      = xQuartEnd - xQuartStart + 1;
    int              zCount      = zQuartEnd - zQuartStart + 1;
    double           xQuartDelta = (bxo - (xQuartStart << 2)) * (1.0 / 4);
    double           zQuartDelta = (bzo - (zQuartStart << 2)) * (1.0 / 4);
    for (int cx = 0; cx < xCount; ++cx) {
        for (int cz = 0; cz < zCount; ++cz) {
            double dX     = xQuartDelta - cx;
            double dZ     = zQuartDelta - cz;
            double distSq = dX * dX + dZ * dZ;
            if (!(distSq < kBlendRadius * kBlendRadius)) continue;
            TerrainColumn const* col = terrainColumn(cx + xQuartStart, cz + zQuartStart);
            if (!col) continue;
            double falloff      = kBlendRadius * kBlendRadius * col->weight;
            double scaleFalloff = kBlendRadius * kBlendRadius * col->weight;
            double depth        = col->depth;
            double scale        = col->scale;
            falloff            *= std::exp((distSq * 2.0 + depth) * minus04);
            totalDepth         += depth * falloff;
            totalContribution  += falloff;
            scaleFalloff       *= std::exp((distSq * 2.0 + scale) * minus04);
            totalScale         += scale * scaleFalloff;
            totalScaleContribution += scaleFalloff;
        }
    }
    return DensityData{totalDepth / totalContribution, totalScale / totalScaleContribution};
}

} // namespace gf::core::biomes

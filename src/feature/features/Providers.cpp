// Value providers of the feature interpreter (F5): the int / float / height providers, vertical
// anchors, block state providers, block predicates and rule tests of Minecraft 1.21.1's worldgen
// JSON, drawing from the random source in Java's order.
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"

#include "core/jrand/Mth.h"
#include "core/noise/Noise.h"
#include "feature/features/Internal.h"

#pragma clang fp contract(off)

namespace gf::features {

using detail::field;
using detail::fieldArr;
using detail::fieldObj;

using detail::normalizeId;
using detail::warnOnce;
using Json = nlohmann::json;
namespace mth = core::mth;

namespace kit {

Json const* weightedPick(Json const& entries, RandomSource& random) {
    if (!entries.is_array() || entries.empty()) return nullptr;
    int total = 0;
    for (auto const& e : entries) total += e.value("weight", 1);
    if (total <= 0) return nullptr;
    int i = random.nextInt(total);
    for (auto const& e : entries) {
        i -= e.value("weight", 1);
        if (i < 0) return e.contains("data") ? &e["data"] : &e;
    }
    return nullptr;
}

int sampleInt(Json const& p, RandomSource& random) {
    if (p.is_number()) return p.get<int>();
    if (!p.is_object()) return 0;
    auto type = normalizeId(p.value("type", std::string("constant")));
    if (type == "minecraft:constant") return p.value("value", 0);
    if (type == "minecraft:uniform") {
        int min = p.value("min_inclusive", 0), max = p.value("max_inclusive", 0);
        return random.nextIntBetweenInclusive(min, max);
    }
    if (type == "minecraft:biased_to_bottom") {
        int min = p.value("min_inclusive", 0), max = p.value("max_inclusive", 0);
        return min + random.nextInt(random.nextInt(max - min + 1) + 1);
    }
    if (type == "minecraft:clamped") {
        int v = sampleInt(p.value("source", Json(0)), random);
        return mth::clampi(v, p.value("min_inclusive", 0), p.value("max_inclusive", 0));
    }
    if (type == "minecraft:clamped_normal") {
        float mean = p.value("mean", 0.0F), dev = p.value("deviation", 0.0F);
        float n    = mean + static_cast<float>(random.nextGaussian()) * dev;
        return static_cast<int>(mth::clampf(n, static_cast<float>(p.value("min_inclusive", 0)), static_cast<float>(p.value("max_inclusive", 0))));
    }
    if (type == "minecraft:weighted_list") {
        if (auto const* d = weightedPick(fieldArr(p, "distribution"), random)) return sampleInt(*d, random);
        return 0;
    }
    warnOnce("int_provider " + type, "unknown int provider " + type);
    return 0;
}

float sampleFloat(Json const& p, RandomSource& random) {
    if (p.is_number()) return p.get<float>();
    if (!p.is_object()) return 0.0F;
    auto type = normalizeId(p.value("type", std::string("constant")));
    if (type == "minecraft:constant") return p.value("value", 0.0F);
    if (type == "minecraft:uniform") {
        float min = p.value("min_inclusive", 0.0F), max = p.value("max_exclusive", 0.0F);
        return random.nextFloat() * (max - min) + min;
    }
    if (type == "minecraft:clamped_normal") {
        float mean = p.value("mean", 0.0F), dev = p.value("deviation", 0.0F);
        return mth::clampf(mean + static_cast<float>(random.nextGaussian()) * dev, p.value("min", 0.0F), p.value("max", 0.0F));
    }
    if (type == "minecraft:trapezoid") {
        float min = p.value("min", 0.0F), max = p.value("max", 0.0F), plateau = p.value("plateau", 0.0F);
        float f  = max - min;
        float f1 = (f - plateau) / 2.0F;
        float f2 = f - f1;
        float a  = random.nextFloat() * f2;
        float b  = random.nextFloat() * f1;
        return min + a + b;
    }
    warnOnce("float_provider " + type, "unknown float provider " + type);
    return 0.0F;
}

int resolveAnchor(Json const& a, Region const& region) {
    if (a.is_number()) return a.get<int>();
    if (!a.is_object()) return 0;
    if (a.contains("absolute")) return a["absolute"].get<int>();
    if (a.contains("above_bottom")) return region.minY() + a["above_bottom"].get<int>();
    if (a.contains("below_top")) return region.maxY() - 1 - a["below_top"].get<int>();
    return 0;
}

int sampleHeight(Json const& p, RandomSource& random, Region const& region) {
    if (!p.is_object()) return resolveAnchor(p, region);
    if (!p.contains("type")) return resolveAnchor(p, region); // a bare anchor is a constant height
    auto type = normalizeId(p["type"].get<std::string>());
    if (type == "minecraft:constant") return resolveAnchor(field(p, "value"), region);
    int  i      = resolveAnchor(field(p, "min_inclusive"), region);
    int  j      = resolveAnchor(field(p, "max_inclusive"), region);
    auto nextInt = [&](int min, int max) { return min >= max ? min : random.nextInt(max - min + 1) + min; }; // Mth.nextInt
    if (type == "minecraft:uniform") {
        if (i > j) return i;
        return random.nextIntBetweenInclusive(i, j);
    }
    if (type == "minecraft:biased_to_bottom") {
        int inner = p.value("inner", 1);
        if (j - i - inner + 1 <= 0) return i;
        int k = random.nextInt(j - i - inner + 1);
        return random.nextInt(k + inner) + i;
    }
    if (type == "minecraft:very_biased_to_bottom") {
        int inner = p.value("inner", 1);
        if (j - i - inner + 1 <= 0) return i;
        int k = nextInt(i + inner, j);
        int l = nextInt(i, k - 1);
        return nextInt(i, l - 1 + inner);
    }
    if (type == "minecraft:trapezoid") {
        int plateau = p.value("plateau", 0);
        if (i > j) return i;
        int k = j - i;
        if (plateau >= k) return random.nextIntBetweenInclusive(i, j);
        int l  = (k - plateau) / 2;
        int i1 = k - l;
        int a  = random.nextIntBetweenInclusive(0, i1);
        int b  = random.nextIntBetweenInclusive(0, l);
        return i + a + b;
    }
    if (type == "minecraft:weighted_list") {
        if (auto const* d = weightedPick(fieldArr(p, "distribution"), random)) return sampleHeight(*d, random, region);
        return i;
    }
    warnOnce("height_provider " + type, "unknown height provider " + type);
    return i;
}

Block const* sampleState(Json const& provider, RandomSource& random, BlockPos const& pos) {
    if (!provider.is_object()) return nullptr;
    if (!provider.contains("type")) return javaState(provider); // a bare state
    auto            type = normalizeId(provider["type"].get<std::string>());
    StateProviderFn fn   = detail::withRegistries([&](detail::Registries const& r) -> StateProviderFn {
        auto it = r.stateProviders.find(type);
        return it == r.stateProviders.end() ? StateProviderFn{} : it->second;
    });
    if (!fn) {
        warnOnce("state_provider " + type, "unknown block state provider " + type);
        return nullptr;
    }
    return fn(random, pos, provider);
}

Block const* sampleRuleState(Json const& provider, Region& region, RandomSource& random, BlockPos const& pos) {
    if (provider.contains("rules")) {
        for (auto const& rule : provider["rules"]) {
            if (testPredicate(field(rule, "if_true"), region, pos)) return sampleState(field(rule, "then"), random, pos);
        }
    }
    return sampleState(field(provider, "fallback"), random, pos);
}

namespace {

BlockPos offsetOf(Json const& p, BlockPos const& pos) {
    if (p.contains("offset") && p["offset"].is_array() && p["offset"].size() == 3) {
        auto const& o = p["offset"];
        return {pos.x + o[0].get<int>(), pos.y + o[1].get<int>(), pos.z + o[2].get<int>()};
    }
    return pos;
}

/// A block HolderSet: an id, a "#tag" or a list of ids.
bool matchesBlocks(Json const& set, Block const& block) {
    if (set.is_string()) {
        auto s = set.get<std::string>();
        if (!s.empty() && s[0] == '#') return hasTag(block, s.substr(1));
        return javaId(block) == normalizeId(s);
    }
    if (set.is_array()) {
        auto id = javaId(block);
        for (auto const& e : set)
            if (e.is_string() && normalizeId(e.get<std::string>()) == id) return true;
    }
    return false;
}

bool matchesFluids(Json const& set, Block const& block) {
    auto const& n = block.getTypeName();
    std::string fluid;
    if (n == "minecraft:water") fluid = "minecraft:water";
    else if (n == "minecraft:flowing_water") fluid = "minecraft:flowing_water";
    else if (n == "minecraft:lava") fluid = "minecraft:lava";
    else if (n == "minecraft:flowing_lava") fluid = "minecraft:flowing_lava";
    else return false;
    auto test = [&](std::string s) {
        s = normalizeId(s);
        if (s == "minecraft:water" && fluid == "minecraft:flowing_water") return false;
        return s == fluid;
    };
    if (set.is_string()) {
        auto s = set.get<std::string>();
        if (!s.empty() && s[0] == '#') return (s == "#minecraft:water" && fluid.find("water") != std::string::npos)
                                           || (s == "#minecraft:lava" && fluid.find("lava") != std::string::npos);
        return test(s);
    }
    if (set.is_array())
        for (auto const& e : set)
            if (e.is_string() && test(e.get<std::string>())) return true;
    return false;
}

/// Java Direction name -> Bedrock face (0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
int faceOf(std::string const& d) {
    static std::map<std::string, int> const m{{"down", 0}, {"up", 1}, {"north", 2}, {"south", 3}, {"west", 4}, {"east", 5}};
    auto it = m.find(d);
    return it == m.end() ? 1 : it->second;
}

} // namespace

bool testPredicate(Json const& p, Region& region, BlockPos const& pos) {
    if (!p.is_object()) return true;
    auto type = normalizeId(p.value("type", std::string("true")));
    if (type == "minecraft:true") return true;
    if (type == "minecraft:not") return !testPredicate(field(p, "predicate"), region, pos);
    if (type == "minecraft:all_of") {
        for (auto const& q : fieldArr(p, "predicates"))
            if (!testPredicate(q, region, pos)) return false;
        return true;
    }
    if (type == "minecraft:any_of") {
        for (auto const& q : fieldArr(p, "predicates"))
            if (testPredicate(q, region, pos)) return true;
        return false;
    }
    BlockPos at = offsetOf(p, pos);
    if (type == "minecraft:inside_world_bounds") return at.y >= region.minY() && at.y < region.maxY();
    Block const& block = region.getBlock(at);
    if (type == "minecraft:matching_blocks") return matchesBlocks(field(p, "blocks"), block);
    if (type == "minecraft:matching_block_tag") return hasTag(block, p.value("tag", std::string()));
    if (type == "minecraft:matching_fluids") return matchesFluids(field(p, "fluids"), block);
    if (type == "minecraft:solid") return block.getMaterial().mSolid;
    if (type == "minecraft:replaceable") return isReplaceable(block);
    if (type == "minecraft:would_survive") {
        auto const* state = javaState(field(p, "state"));
        return state && canSurvive(region, at, *state);
    }
    if (type == "minecraft:has_sturdy_face") {
        auto face = faceOf(p.value("direction", std::string("up")));
        return block.canProvideFullSupport(static_cast<uchar>(face));
    }
    warnOnce("block_predicate " + type, "unknown block predicate " + type);
    return false;
}

bool testRule(Json const& rule, Block const& block, RandomSource& random) {
    auto type = normalizeId(rule.value("predicate_type", std::string("minecraft:always_true")));
    if (type == "minecraft:always_true") return true;
    if (type == "minecraft:block_match") return javaId(block) == normalizeId(rule.value("block", std::string()));
    if (type == "minecraft:blockstate_match") {
        auto const* s = javaState(field(rule, "block_state"));
        return s && *s == block;
    }
    if (type == "minecraft:tag_match") return hasTag(block, rule.value("tag", std::string()));
    if (type == "minecraft:random_block_match") {
        return javaId(block) == normalizeId(rule.value("block", std::string())) && random.nextFloat() < rule.value("probability", 0.0F);
    }
    if (type == "minecraft:random_blockstate_match") {
        auto const* s = javaState(field(rule, "block_state"));
        return s && *s == block && random.nextFloat() < rule.value("probability", 0.0F);
    }
    warnOnce("rule_test " + type, "unknown rule test " + type);
    return false;
}

worldgen::Heightmap heightmapOf(std::string_view name) {
    using H = worldgen::Heightmap;
    if (name == "WORLD_SURFACE_WG") return H::WorldSurfaceWg;
    if (name == "WORLD_SURFACE") return H::WorldSurface;
    if (name == "OCEAN_FLOOR_WG") return H::OceanFloorWg;
    if (name == "OCEAN_FLOOR") return H::OceanFloor;
    if (name == "MOTION_BLOCKING_NO_LEAVES") return H::MotionBlockingNoLeaves;
    return H::MotionBlocking;
}

} // namespace kit

namespace detail {

namespace {

/// NoiseBasedStateProvider's noise: NormalNoise.create(WorldgenRandom(LegacyRandom(seed)), parameters).
core::noise::NormalNoise const& providerNoise(Json const& p) {
    static std::mutex                                               m;
    static std::unordered_map<std::string, core::noise::NormalNoise> cache;
    std::string                                                     key = std::to_string(p.value("seed", std::int64_t{0})) + field(p, "noise").dump();
    std::lock_guard                                                 lock(m);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    core::noise::NoiseParameters params;
    auto const&                  n = fieldObj(p, "noise");
    params.firstOctave             = n.value("firstOctave", 0);
    for (auto const& a : fieldArr(n, "amplitudes")) params.amplitudes.push_back(a.get<double>());
    core::jrand::WorldgenRandom r(std::make_unique<core::jrand::LegacyRandom>(p.value("seed", std::int64_t{0})));
    return cache.emplace(key, core::noise::NormalNoise::create(r, params)).first->second;
}

double noiseValue(Json const& p, BlockPos const& pos, double scale) {
    return providerNoise(p).getValue(static_cast<double>(pos.x) * scale, static_cast<double>(pos.y) * scale, static_cast<double>(pos.z) * scale);
}

Block const* randomState(Json const& states, RandomSource& random) {
    if (!states.is_array() || states.empty()) return nullptr;
    return kit::javaState(states[static_cast<std::size_t>(random.nextInt(static_cast<int>(states.size())))]);
}

} // namespace

void registerVanillaProviders() {
    registerStateProvider("minecraft:simple_state_provider", [](RandomSource&, BlockPos const&, Json const& c) {
        return kit::javaState(field(c, "state"));
    });
    registerStateProvider("minecraft:weighted_state_provider", [](RandomSource& random, BlockPos const&, Json const& c) -> Block const* {
        auto const* e = kit::weightedPick(fieldArr(c, "entries"), random);
        return e ? kit::javaState(*e) : nullptr;
    });
    registerStateProvider("minecraft:rotated_block_provider", [](RandomSource& random, BlockPos const&, Json const& c) -> Block const* {
        auto const* b = kit::javaState(field(c, "state"));
        if (!b) return nullptr;
        static char const* const axes[] = {"x", "y", "z"};
        return &kit::withProperty(*b, "axis", axes[random.nextInt(3)]);
    });
    registerStateProvider("minecraft:randomized_int_state_provider", [](RandomSource& random, BlockPos const& pos, Json const& c) -> Block const* {
        auto const* b = kit::sampleState(field(c, "source"), random, pos);
        if (!b) return nullptr;
        int v = kit::sampleInt(c.value("values", Json(0)), random);
        return &kit::withProperty(*b, c.value("property", std::string()), std::to_string(v));
    });
    registerStateProvider("minecraft:noise_provider", [](RandomSource&, BlockPos const& pos, Json const& c) -> Block const* {
        auto const& states = fieldArr(c, "states");
        if (states.empty()) return nullptr;
        double d0 = mth::clamp((1.0 + noiseValue(c, pos, c.value("scale", 1.0F))) / 2.0, 0.0, 0.9999);
        return kit::javaState(states[static_cast<std::size_t>(static_cast<int>(d0 * static_cast<double>(states.size())))]);
    });
    registerStateProvider("minecraft:noise_threshold_provider", [](RandomSource& random, BlockPos const& pos, Json const& c) -> Block const* {
        double d0 = noiseValue(c, pos, c.value("scale", 1.0F));
        if (d0 < static_cast<double>(c.value("threshold", 0.0F))) return randomState(fieldArr(c, "low_states"), random);
        if (random.nextFloat() < c.value("high_chance", 0.0F)) return randomState(fieldArr(c, "high_states"), random);
        return kit::javaState(field(c, "default_state"));
    });
}

} // namespace detail

} // namespace gf::features

#pragma once

// Java-parity seeding helpers behind the random sources of core/jrand/JRand.h (F5; the contract
// header stays as F0 wrote it, rule R18): Minecraft 1.21.1's RandomSupport (Stafford's mix 13, the
// 64 -> 128 bit seed upgrade, the MD5 seed of a name) and PositionalRandomFactory#fromSeed. Pure
// C++ (gf-core); checked against the Java oracle's tests/golden/rng.json in tests/worldgen.

#include <cstdint>
#include <memory>
#include <string_view>

#include "core/jrand/JRand.h"

namespace gf::core::jrand {

/// RandomSupport.GOLDEN_RATIO_64 / SILVER_RATIO_64.
inline constexpr std::int64_t kGoldenRatio64 = static_cast<std::int64_t>(0x9E3779B97F4A7C15ULL); // -7046029254386353131
inline constexpr std::int64_t kSilverRatio64 = 0x6A09E667F3BCC909LL;                               //  7640891576956012809

/// RandomSupport.Seed128bit.
struct Seed128bit {
    std::int64_t lo = 0, hi = 0;
    friend bool operator==(Seed128bit const&, Seed128bit const&) = default;
};

/// RandomSupport#mixStafford13.
[[nodiscard]] std::int64_t mixStafford13(std::int64_t z);
/// RandomSupport#upgradeSeedTo128bitUnmixed: (seed ^ SILVER_RATIO_64, that + GOLDEN_RATIO_64).
[[nodiscard]] Seed128bit upgradeSeedTo128bitUnmixed(std::int64_t seed);
/// RandomSupport#upgradeSeedTo128bit: the unmixed pair through mixStafford13 (XoroshiroRandomSource(seed)).
[[nodiscard]] Seed128bit upgradeSeedTo128bit(std::int64_t seed);
/// RandomSupport#seedFromHashOf: the MD5 of the UTF-8 bytes of `name` as two big-endian longs.
[[nodiscard]] Seed128bit seedFromHashOf(std::string_view name);

/// PositionalRandomFactory#fromSeed of a factory made by LegacyRandom / Xoroshiro128pp /
/// WorldgenRandom::forkPositional: LegacyRandomSource(seed), or XoroshiroRandomSource(seed ^ seedLo,
/// seed ^ seedHi). nullptr for a factory of another kind.
[[nodiscard]] std::unique_ptr<RandomSource> positionalFromSeed(PositionalRandomFactory& factory, std::int64_t seed);

} // namespace gf::core::jrand

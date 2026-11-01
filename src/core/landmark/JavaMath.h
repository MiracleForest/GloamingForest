#pragma once

// Java's int and long arithmetic for the landmark translations (F6, pure, gf-core): Java wraps on
// overflow, C++ signed overflow is undefined, so these compute in unsigned types and stay bit exact.
// Shared by the grid (Landmark.cpp), the placements (Placement.cpp) and the start seeding
// (feature/landmark/Starts.cpp).

#include <cstdint>

namespace gf::core::landmark::java {

/// `a + b` on Java ints.
[[nodiscard]] constexpr int iadd(int a, int b) { return static_cast<int>(static_cast<unsigned>(a) + static_cast<unsigned>(b)); }
/// `a * b` on Java ints.
[[nodiscard]] constexpr int imul(int a, int b) { return static_cast<int>(static_cast<unsigned>(a) * static_cast<unsigned>(b)); }
/// `a << n` on a Java int (the shift distance masked to 5 bits).
[[nodiscard]] constexpr int ishl(int a, int n) { return static_cast<int>(static_cast<unsigned>(a) << (n & 31)); }
/// `a + b` on Java longs.
[[nodiscard]] constexpr std::int64_t ladd(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
/// `a * b` on Java longs.
[[nodiscard]] constexpr std::int64_t lmul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
/// `seed + chunkX * 25117L + chunkZ * 151121L`: the seed of a landmark chunk's random source
/// (pickVarietyLandmark, LandmarkStructure's start random).
[[nodiscard]] constexpr std::int64_t chunkSeed(std::int64_t seed, int chunkX, int chunkZ) {
    return ladd(ladd(seed, lmul(chunkX, 25117LL)), lmul(chunkZ, 151121LL));
}

} // namespace gf::core::landmark::java

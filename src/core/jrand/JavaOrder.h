#pragma once

// Java-parity orderings that world generation depends on (pure C++, gf-core):
//   - featureOrder   net.minecraft.world.level.biome.FeatureSorter#buildFeaturesPerStep: the
//                    per-step feature lists whose indices seed every placed feature
//                    (WorldgenRandom#setFeatureSeed(decorationSeed, index, step)).
//   - hashSetOrder   the iteration order of a java.util.HashSet<BlockPos> (TreeFeature hands its
//                    log / leaf sets to the decorators in this order, then sorted by y).
//   - mthSin/mthCos  net.minecraft.util.Mth#sin / #cos (the 65536-entry float table).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gf::core::jrand {

/// One feature source (a biome): its placed feature ids per decoration step.
using StepFeatures = std::vector<std::vector<std::string>>;

/// FeatureSorter#buildFeaturesPerStep over `sources` in the order the biome source lists its
/// possible biomes. Result[step] = the sorted feature ids of that step; a feature's index in it is
/// its feature seed index. Cycles (a feature ordered both before and after another) are broken the
/// way Java's retry does not: the offending edges are ignored and `cycle` is set.
[[nodiscard]] std::vector<std::vector<std::string>> featureOrder(std::vector<StepFeatures> const& sources, bool* cycle = nullptr);

/// Vec3i#hashCode of a block position.
[[nodiscard]] constexpr int blockPosHash(int x, int y, int z) {
    // (y + z * 31) * 31 + x with int overflow
    auto u = static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(z) * 31u;
    return static_cast<int>(u * 31u + static_cast<std::uint32_t>(x));
}

/// The iteration order of a java.util.HashSet filled with `hashes` (distinct elements, in
/// insertion order): returns the insertion indices in iteration order.
[[nodiscard]] std::vector<std::size_t> hashSetOrder(std::vector<int> const& hashes);

/// Mth#sin / Mth#cos (table lookup, float).
[[nodiscard]] float mthSin(float value);
[[nodiscard]] float mthCos(float value);

} // namespace gf::core::jrand

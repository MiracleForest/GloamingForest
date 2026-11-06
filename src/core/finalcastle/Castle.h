// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleMural.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/DestroyArea.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U30 (finalcastle): the pure maths of the final castle structure (no game types; gtested in
// tests/finalcastle/CastleTest.cpp): the rune mural bitmap, the decoRNG seeds, the bridge arch and
// the destroy areas of the damaged / wrecked towers.

#include <cstdint>
#include <optional>
#include <vector>

#include "core/GlobalPos.h"
#include "core/jrand/JRand.h"

namespace gf::core::finalcastle {

/// RandomSource.create(seed + (minX * 321534781L) ^ (minZ * 756839L)) seed (Java precedence:
/// `(seed + minX * 321534781) ^ (minZ * 756839)`, long arithmetic).
[[nodiscard]] std::int64_t decoSeed(std::int64_t seed, int minX, int minZ);
/// FinalCastleFoundation13ComponentThorns#makeThornBranch: `seed + (x * 321534781L) ^ (y * 756839L) + z`
/// = `(seed + x * 321534781) ^ (y * 756839 + z)`.
[[nodiscard]] std::int64_t thornBranchSeed(std::int64_t seed, int x, int y, int z);
/// FinalCastleBridgeComponent's arch: the depth of support `i` of a bridge with `l3 = length / 3`:
/// `l3 - (int) (Mth.cos((float) (l3 - i) / (float) l3 * 1.6F) * l3)`.
[[nodiscard]] int bridgeArchDepth(int l3, int i);

/// FinalCastleMural: a width x height byte bitmap (index x * height + y); a set pixel (> 0) is a
/// rune brick.
class Mural {
public:
    /// An empty mural (loading a mural without data).
    Mural() = default;
    /// FinalCastleMural(boundingBox, orientation, seed): `width` is the box's z span for a mural
    /// facing south / north, else its x span; `height` its y span.
    Mural(int width, int height, std::int64_t seed);
    /// FinalCastleMural(CompoundTag): saved data (muralWidth, muralHeight, muralBytes).
    Mural(int width, int height, std::vector<std::int8_t> bytes);

    [[nodiscard]] int                              width() const { return mWidth; }
    [[nodiscard]] int                              height() const { return mHeight; }
    [[nodiscard]] std::vector<std::int8_t> const&  bytes() const { return mMural; }
    [[nodiscard]] std::int8_t                      get(int x, int y) const;
    void                                           set(int x, int y, std::int8_t value);

private:
    void generateMural(std::int64_t seed);
    void makeHorizontalTree(jrand::RandomSource& rand, int centerX, int centerY, int branchLength, bool positive);
    void makeVerticalTree(jrand::RandomSource& rand, int centerX, int centerY, int branchLength, bool positive);
    bool makeHorizontalBranch(jrand::RandomSource& rand, int sx, int sy, int length, bool plusX, bool plusY);
    bool makeVerticalBranch(jrand::RandomSource& rand, int sx, int sy, int length, bool plusY, bool plusX);
    void fillHorizontalLine(int sx, int sy, int length, bool positive);
    void fillVerticalLine(int sx, int sy, int length, bool positive);
    void makeStripes(jrand::RandomSource& rand);
    void makeSingleStripe(int y);

    int                      mWidth  = 0;
    int                      mHeight = 0;
    std::vector<std::int8_t> mMural;
};

/// DestroyArea: a 5 x 11 x 5 box (bx .. bx + 4, y - 10 .. y, bz .. bz + 4) of a damaged / wrecked
/// tower that is carved out (or kept).
struct DestroyArea {
    BlockBox box;
    /// DestroyArea(tower, rand, y): bx = tower.minX - 2 + rand(xSpan), bz likewise.
    DestroyArea(BlockBox const& tower, jrand::RandomSource& rand, int y);
    [[nodiscard]] bool isEntirelyAbove(int y) const { return box.minY > y; }
    [[nodiscard]] bool isVecInside(int x, int y, int z) const { return box.contains(x, y, z); }
    /// intersectsWith: Java's BoundingBox#intersects(minX, minZ, maxX, maxZ) called with
    /// (other.maxX + 1, other.minX - 1, other.maxZ + 1, other.minZ - 1) (TF's argument order kept).
    [[nodiscard]] bool intersectsWith(DestroyArea const& other) const;
    /// createNonIntersecting: up to 100 tries; a try is taken when the list is empty or when it misses
    /// any one area of the list (TF's loop), nullopt when none was taken.
    [[nodiscard]] static std::optional<DestroyArea> createNonIntersecting(BlockBox const& tower, jrand::RandomSource& rand, int y,
                                                                          std::vector<std::optional<DestroyArea>> const& others);
};

} // namespace gf::core::finalcastle

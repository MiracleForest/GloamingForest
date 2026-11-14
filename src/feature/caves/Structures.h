// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveConnectComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveGardenComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollVaultComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/CloudCastleComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/CloudComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/CloudTreeComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCloudComponent.java
//   src/main/java/gloamingforest/world/components/structures/YetiCaveComponent.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U27's structure pieces: the troll caves ("gloamingforest:TFTC*"), the cloud castle pieces the
// giant house (U29) starts by string ("gloamingforest:TFClCa", "TFCloud", "TFClTr", the legacy
// "TFTCloud") and the yeti cave ("gloamingforest:TFYeti", on F7's HollowHillComponent base).

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "feature/structure/bases/ComponentOld.h"
#include "feature/structure/bases/HollowHill.h"

namespace gf::feature::caves {

inline constexpr char kTrollCave[]      = "gloamingforest:troll_cave";
inline constexpr char kYetiCave[]       = "gloamingforest:yeti_cave";
inline constexpr char kTrollSpeleothem[] = "gloamingforest:troll_cave";
inline constexpr char kYetiSpeleothem[]  = "gloamingforest:yeti_cave";

using structure::BlockBox;
using structure::Json;
using structure::Orientation;
using structure::Piece;
using structure::RandomSource;
using structure::Region;
using structure::Rotation;
using structure::bases::PieceList;

/// TrollCaveMainComponent ("gloamingforest:TFTCMai").
class TrollCaveMain : public structure::bases::ComponentOld {
public:
    /// TrollCaveMainComponent(type, i, x, y, z, config): size 30, height 20, box from (-15, -20, -15), SOUTH.
    TrollCaveMain(std::string typeId, int index, int x, int y, int z, std::string configId = kTrollSpeleothem);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;
    [[nodiscard]] structure::bases::TerrainAdjustment getTerrainAdjustment() const override;

    /// getValidOpening.
    [[nodiscard]] BlockPos getValidOpening(RandomSource& rand, Rotation direction) const;

    int         size   = 30;
    int         height = 20;
    std::string configId;

protected:
    virtual bool makeSmallerCave(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int caveSize, int caveHeight, Rotation rotation);
    void         placeSpeleothems(Region& region, RandomSource& rand, BlockBox const& sbb, RandomSource& decoRNG);
    [[nodiscard]] BlockPos getCoordsInCave(RandomSource& rand) const;
    void         hollowCaveMiddle(Region& region, BlockBox const& sbb, RandomSource& rand, int minX, int minY, int minZ, int maxX, int maxY, int maxZ);
    [[nodiscard]] BlockPos offsetTowerCCoords(int x, int y, int z, int towerSize, Orientation direction) const override;
    /// generateAtSurface: a configured feature at the first open spot above sea level + 15.
    void generateAtSurface(Region& region, std::string const& feature, RandomSource& rand, int x, int z, BlockBox const& sbb);
    void makeTreasureCrate(Region& region, BlockBox const& sbb);
    /// RandomSource.create(seed + minX * 321534781L ^ minZ * 756839L) (Java's precedence).
    [[nodiscard]] std::int64_t decoSeed(Region& region) const;
};

/// TrollCaveConnectComponent ("gloamingforest:TFTCCon").
class TrollCaveConnect : public TrollCaveMain {
public:
    TrollCaveConnect(int index, int x, int y, int z, int caveSize, int caveHeight, Orientation direction, std::string configId = kTrollSpeleothem);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

    std::array<bool, 4> openingTowards{false, false, true, false};

protected:
    bool makeSmallerCave(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int caveSize, int caveHeight, Rotation rotation) override;

private:
    void makeMonolith(Region& region, RandomSource& rand, BlockBox const& sbb);
    [[nodiscard]] int countExits() const;
    void decorateWall(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation);
    void decorateStoneFormation(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation);
    void makeSingleStoneFormation(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation, int z, int y, int width, int depth);
    void decorateBracketMushrooms(Region& region, BlockBox const& sbb, RandomSource& decoRNG, Rotation rotation);
    void makeSingleBracketMushroom(Region& region, BlockBox const& sbb, Rotation rotation, int z, int y, int width, int depth, std::string const& mushBlock);
    bool makeGardenCave(PieceList& list, RandomSource& rand, int index, int x, int y, int z, int caveSize, int caveHeight, Rotation rotation);
};

/// TrollCaveGardenComponent ("gloamingforest:TFTCGard").
class TrollCaveGarden : public TrollCaveMain {
public:
    TrollCaveGarden(int index, int x, int y, int z, int caveSize, int caveHeight, Orientation direction, std::string configId = kTrollSpeleothem);

    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

protected:
    /// generate: a configured feature at piece-local (x, y, z) when inside `sbb`.
    void generate(Region& region, std::string const& feature, RandomSource& rand, int x, int y, int z, BlockBox const& sbb);
};

/// TrollVaultComponent ("gloamingforest:TFTCVa").
class TrollVault : public structure::bases::ComponentOld {
public:
    TrollVault(int index, int x, int y, int z);
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// CloudCastleComponent ("gloamingforest:TFClCa").
class CloudCastle : public structure::bases::ComponentOld {
public:
    CloudCastle(int index, int x, int y, int z);
    void addChildren(Piece& parent, PieceList& list, RandomSource& rand) override;
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;

private:
    void placeGiant(Region& region, BlockBox const& sbb, int x, int z, char const* entityId);
};

/// CloudComponent ("gloamingforest:TFCloud"): a vanilla StructurePiece.
class Cloud : public structure::Piece {
public:
    Cloud(int genDepth, int centerX, int yHeight, int centerZ);
    /// The load form (the box comes from the saved tag).
    Cloud();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& chunkBox, BlockPos const& pivot) override;
};

/// CloudTreeComponent ("gloamingforest:TFClTr").
class CloudTree : public structure::bases::ComponentOld {
public:
    CloudTree(int index, int x, int y, int z);
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
};

/// TrollCloudComponent ("gloamingforest:TFTCloud"): legacy, only ever loaded.
class TrollCloud : public structure::bases::ComponentOld {
public:
    TrollCloud();
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

    int size   = 0;
    int height = 0;
};

/// YetiCaveComponent ("gloamingforest:TFYeti").
class YetiCave : public structure::bases::HollowHill {
public:
    YetiCave(int index, int x, int y, int z, std::string configId = kYetiSpeleothem);
    void postProcess(Region& region, RandomSource& rand, BlockBox const& sbb, BlockPos const& pivot) override;
    [[nodiscard]] int worldY(int y) const override;

    /// YetiCaveComponent.drainWater.
    static void drainWater(BlockBox const& chunkBox, Region& region, int maxDepth, Block const& airState, int xCenter, int zCenter, double radius,
                           Block const& undergroundBlock, int yStart);
};

/// TrollCaveStructure#getFirstPiece + LandmarkStructure#generatePieces at start (x, y, z).
void layoutTrollCave(RandomSource& contextRandom, int x, int y, int z, PieceList& pieces);
/// YetiCaveStructure#getFirstPiece at start (x, y, z).
void layoutYetiCave(int x, int y, int z, PieceList& pieces);

/// Registers the piece types and the two landmark generators (with the yeti cave's terraformer).
void installStructures();

/// Selftests: the garden caves skip their biome check (isBoundingBoxOutsideBiomes) when set.
void setGardenBiomeCheckForTest(bool enabled);

} // namespace gf::feature::caves

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U18 (minorlandmarks) internals: the minor landmarks (hollow hills, hedge maze, hollow trees,
// fallen trunks), the hedge block and TF's decorative feature types. Shared by the unit's files
// only (rule R2: no other unit includes it).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/minorlandmarks/MinorLandmarks.h"
#include "feature/ext/minorlandmarks/W0Internals.h"
#include "feature/structure/Structure.h"
#include "feature/structure/bases/Component.h"
#include "feature/structure/bases/ComponentOld.h"

class Block;

namespace gf::feature::minorlandmarks {

namespace st  = gf::structure;
namespace sb  = gf::structure::bases;
namespace cml = gf::core::minorlandmarks;
using Json    = nlohmann::json;
using core::BlockBox;
using st::RandomSource;
using st::Region;

// ---- ids ---------------------------------------------------------------------------------------------
inline constexpr char kHedge[]            = "twilightforest:hedge";
inline constexpr char kSmallHill[]        = "twilightforest:small_hollow_hill";
inline constexpr char kMediumHill[]       = "twilightforest:medium_hollow_hill";
inline constexpr char kLargeHill[]        = "twilightforest:large_hollow_hill";
inline constexpr char kHedgeMaze[]        = "twilightforest:hedge_maze";
inline constexpr char kHollowTree[]       = "twilightforest:hollow_tree";
inline constexpr char kSwampHollowTree[]  = "twilightforest:swamp_hollow_tree";
inline constexpr char kFallenTrunk[]      = "twilightforest:fallen_trunk";
inline constexpr char kTFHill[]           = "twilightforest:TFHill";
inline constexpr char kTFHedge[]          = "twilightforest:TFHedge";
inline constexpr char kTFFallenTrunk[]    = "twilightforest:TFFallenTrunk";
inline constexpr char kTFHTTr[]           = "twilightforest:TFHTTr";
inline constexpr char kTFHTLB[]           = "twilightforest:TFHTLB";
inline constexpr char kTFHTMB[]           = "twilightforest:TFHTMB";
inline constexpr char kTFHTSB[]           = "twilightforest:TFHTSB";
inline constexpr char kTFHTRo[]           = "twilightforest:TFHTRo";
inline constexpr char kTFHTLD[]           = "twilightforest:TFHTLD";

// ---- helpers (Helpers.cpp) -----------------------------------------------------------------------------
/// StructurePiece#setOrientation of a vanilla piece (mirror / rotation from the facing).
void setVanillaOrientation(st::Piece& piece, std::optional<st::Orientation> facing);
[[nodiscard]] st::Orientation toOrientation(cml::Dir d);
[[nodiscard]] cml::Dir        toDir(st::Orientation o);
[[nodiscard]] BlockPos        toBlockPos(cml::Pos const& p);
[[nodiscard]] cml::Pos        toPos(BlockPos const& p);
/// FeatureLogic#isReplaceable(state, includeFlowers) (worldGenReplaceable / treesReplaceable).
[[nodiscard]] bool treesReplaceable(Block const& block);
/// TreeFeature#validTreePos (air or #minecraft:replaceable_by_trees).
[[nodiscard]] bool validTreePos(Region& region, BlockPos const& pos);
/// A BlockStateProvider JSON sampled as Java does (simple / weighted / rule based through F5's kit).
[[nodiscard]] Block const* sampleProvider(Json const& provider, RandomSource& random, BlockPos const& pos);
/// The Java id of a simple_state_provider (or "" for other providers).
[[nodiscard]] std::string providerBlockId(Json const& provider);
/// A simple_state_provider JSON for a Java id (+ Java properties).
[[nodiscard]] Json simpleProvider(std::string const& javaId, Json const& props = Json::object());
/// LeavesBlock.DISTANCE set on a leaves block (unchanged for other blocks).
[[nodiscard]] Block const& withLeafDistance(Block const& block, int distance);
/// TFLootTables#generateChest / StructurePiece#createChest at a world position with a facing (any
/// chest-like block id); nothing when `sbb` does not hold `pos`.
void placeContainerAt(Region& region, BlockBox const& sbb, BlockPos const& pos, std::string const& blockId, std::optional<st::Orientation> facing,
                      std::string const& tableId, std::int64_t seed);
/// A spawner for `entityId` at a world position inside `sbb`.
void placeSpawnerAt(Region& region, BlockBox const& sbb, BlockPos const& pos, std::string const& entityId);
/// Whether `entityId` is one TF's HedgeBlock spares (Spider and its subclasses).
[[nodiscard]] bool isSpiderType(std::string const& entityId);

// ---- pieces ------------------------------------------------------------------------------------------
/// HedgeMazeComponent ("twilightforest:TFHedge") at (x, y, z) (HedgeMazeStructure: start + (1, 4, 1)).
[[nodiscard]] std::unique_ptr<st::Piece> makeHedgeMaze(int genDepth, int x, int y, int z);
/// HollowHillComponent ("twilightforest:TFHill") for HollowHillStructure#getFirstPiece.
[[nodiscard]] std::unique_ptr<st::Piece> makeHollowHill(int hillSize, int x, int y, int z, std::string const& speleothemConfig);

/// HollowTreeStructure's configuration (the structure JSON's providers, loot, monster).
struct HollowTreeConfig {
    Json        height, radius;
    Json        log, wood, root, leaves, vine, bug;
    Json        dungeonWood, dungeonAir, dungeonLootBlock;
    std::string dungeonLootTable = "twilightforest:tree_cache";
    std::string dungeonMonster   = "twilightforest:swarm_spider";
    bool        allowInWater     = false;
    /// From the imported structure JSON (TF's defaults for missing fields).
    [[nodiscard]] static HollowTreeConfig fromJson(Json const& structure);
    [[nodiscard]] static HollowTreeConfig defaults();
};
/// HollowTreeTrunk + addChildren (the whole tree's pieces) for a trunk of `height` and `radius`
/// standing at (x, y, z), drawing from `random` (GenerationContext#random(), or the sapling's).
[[nodiscard]] std::vector<std::unique_ptr<st::Piece>> hollowTreePieces(HollowTreeConfig const& config, int height, int radius, int x, int y, int z,
                                                                       RandomSource& random);

/// FallenTrunkStructure's configuration.
struct FallenTrunkConfig {
    Json        length, bigTrunkLength, log;
    std::string chestLootTable = "twilightforest:fallen_trunk_loot";
    [[nodiscard]] static FallenTrunkConfig fromJson(Json const& structure);
};
/// FallenTrunkPiece + its two utility pieces.
[[nodiscard]] std::vector<std::unique_ptr<st::Piece>> fallenTrunkPieces(FallenTrunkConfig const& config, int length, int radius, cml::Dir orientation,
                                                                        BlockBox const& box, std::int64_t holeSeed);
/// The fallen trunk's terraformer (TrunkUnderDensityFunction) for a start's first piece.
[[nodiscard]] double fallenTrunkTerraform(BlockBox const& firstPieceBox, cml::Dir orientation, int length, int radius, std::int64_t holeSeed, int x, int y,
                                          int z);

// ---- install ----------------------------------------------------------------------------------------
void installHedge();
void uninstallHedge();
void installLandmarks();   ///< hollow hills, hedge maze (Landmarks.cpp)
void installHollowTrees(); ///< HollowTrees.cpp
void installFallenTrunks(); ///< FallenTrunks.cpp
void installFeatures();    ///< Features.cpp

// ---- features (Features.cpp) ---------------------------------------------------------------------------
/// FeatureUtil#isAreaSuitable(world, pos, xWidth, height, zWidth, underwaterAllowed).
[[nodiscard]] bool isAreaSuitable(Region& world, BlockPos const& pos, int xWidth, int height, int zWidth, bool underwaterAllowed = false);
/// BlockSpikeFeature#startSpike with STONE_STALACTITE (stone, size variation 0.25, max length 11).
bool placeStoneSpike(Region& level, BlockPos const& start, RandomSource& random, bool hanging);

} // namespace gf::feature::minorlandmarks

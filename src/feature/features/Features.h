#pragma once

// Contract "features" (header owned by F0, implemented by F5; spike S2).
//
// An interpreter for the imported worldgen JSON (configured_feature / placed_feature, 129 / 107 in
// TF 4.8) with the vanilla feature types it uses (tree with the vanilla trunk / foliage placers and
// decorators, random_selector, ore, random_patch, flower, random_boolean_selector, simple_block,
// block_column, no_op), weighted_list, and the placement modifiers (count, rarity_filter,
// heightmap, biome, in_square, ...) plus TF's avoid_landmark and chunk_center. Units register TF's
// own feature types and tree parts by id:
//
//     gf::features::registerType("twilightforest:hollow_log", [](gf::features::FeatureContext const& c) {
//         ...; return true;
//     });
//     gf::features::place("twilightforest:tree/canopy_tree", region, pos, random);
//
// Features larger than the 3x3 chunk window (canopy and dark trees) write through LargeWriter,
// which queues blocks for chunks that are not generated yet.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

#include "feature/worldgen/Worldgen.h"

class Block;

namespace gf::features {

using worldgen::RandomSource;
using worldgen::Region;
using Json = nlohmann::json;

/// What a feature placement gets (Java's FeaturePlaceContext).
struct FeatureContext {
    Region&       region;
    BlockPos      origin;
    RandomSource& random;
    Json const&   config; ///< the configured feature's "config" object
    std::string   featureId; ///< the configured feature id when placed by id ("" for inline JSON)
};
using FeatureFn = std::function<bool(FeatureContext const& ctx)>;

/// What a placement modifier gets (Java's PlacementContext).
struct PlacementContext {
    Region&       region;
    RandomSource& random;
    Json const&   config; ///< the modifier's JSON object
    std::string   placedFeatureId;
};
/// Maps one position to zero or more positions (Java's PlacementModifier#getPositions).
using PlacementFn = std::function<std::vector<BlockPos>(PlacementContext const& ctx, BlockPos const& pos)>;

/// A tree being placed (Java's TreeFeature internals): positions the parts placed, for decorators.
struct TreeContext {
    Region&               region;
    RandomSource&         random;
    Json const&           config;   ///< the tree configuration
    std::vector<BlockPos> logs;     ///< trunk blocks placed so far
    std::vector<BlockPos> leaves;   ///< foliage blocks placed so far
    std::vector<BlockPos> roots;    ///< root blocks placed so far
};
/// FoliagePlacer.FoliageAttachment.
struct FoliageAttachment {
    BlockPos pos;
    int      radiusOffset = 0;
    bool     doubleTrunk  = false;
};
using TrunkPlacerFn   = std::function<std::vector<FoliageAttachment>(TreeContext& tree, int height, BlockPos const& start, Json const& config)>;
using FoliagePlacerFn = std::function<void(TreeContext& tree, FoliageAttachment const& at, int foliageHeight, int radius, Json const& config)>;
using TreeDecoratorFn = std::function<void(TreeContext& tree, Json const& config)>;
using RootPlacerFn    = std::function<bool(TreeContext& tree, BlockPos const& pos, BlockPos const& trunkOrigin, Json const& config)>;
/// BlockStateProvider#getState: the block to place at `pos` (nullptr: none).
using StateProviderFn = std::function<Block const*(RandomSource& random, BlockPos const& pos, Json const& config)>;

/// A feature type ("twilightforest:hollow_log"); replaces an earlier one with the same id.
void registerType(std::string const& typeId, FeatureFn fn);
/// A placement modifier type ("twilightforest:avoid_landmark").
void registerPlacement(std::string const& typeId, PlacementFn fn);
void registerTrunkPlacer(std::string const& typeId, TrunkPlacerFn fn);
void registerFoliagePlacer(std::string const& typeId, FoliagePlacerFn fn);
void registerTreeDecorator(std::string const& typeId, TreeDecoratorFn fn);
void registerRootPlacer(std::string const& typeId, RootPlacerFn fn);
void registerStateProvider(std::string const& typeId, StateProviderFn fn);

/// Places configured or placed feature `id` (imported JSON, "twilightforest:..." or
/// "minecraft:..."); false when it placed nothing or the id is unknown (one warning per id).
bool place(std::string const& id, Region& region, BlockPos const& pos, RandomSource& random);
/// Places an inline configured / placed feature JSON object.
bool placeJson(Json const& feature, Region& region, BlockPos const& pos, RandomSource& random);

/// Writes blocks of features that cross the 3x3 chunk window: blocks for chunks that are not
/// generated yet are queued and applied when those chunks generate (persisted per world).
class LargeWriter {
public:
    /// Sets (or queues) a block in dimension `dimension`.
    static void setBlock(int dimension, BlockPos const& pos, Block const& block);
    /// Blocks waiting for ungenerated chunks in `dimension`.
    [[nodiscard]] static std::size_t pending(int dimension);
};

} // namespace gf::features

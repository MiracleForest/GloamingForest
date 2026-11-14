#pragma once

// Contract extension of "features" (F5, rule R18; to be folded into Features.h by A8).
//
// The helpers a feature, placer or decorator implementation needs to read its imported Java
// configuration the way Minecraft 1.21.1 does: Java block states, state / int / float / height
// providers, vertical anchors, block predicates, rule tests, the tree helpers of TreeFeature /
// TrunkPlacer / FoliagePlacer, and placed features by id. Everything is thread-safe (features run
// on generation worker threads) and draws from the given RandomSource in Java's order.
//
//     using namespace gf::features;
//     Block const* log = kit::sampleState(ctx.config["trunk_provider"], ctx.random, pos);
//     int height       = kit::sampleInt(ctx.config["height"], ctx.random);
//     if (kit::testPredicate(ctx.config["allowed"], ctx.region, pos)) ctx.region.setBlock(pos, *log);

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"

#include "feature/features/Features.h"

class Block;
class BlockSource;

namespace gf::features::kit {

using Json = nlohmann::json;

// ---- blocks ---------------------------------------------------------------------------------------
/// A Java block state {"Name": "minecraft:oak_log", "Properties": {"axis": "y"}} as the Bedrock
/// permutation (vanilla names and properties translated; upstream blocks by their imported states).
/// nullptr when the block does not exist (one warning per id).
[[nodiscard]] Block const* javaState(Json const& state);
/// A Java block id (+ Java properties) as the Bedrock permutation.
[[nodiscard]] Block const* javaBlock(std::string_view javaId, Json const& properties = Json::object());
/// The Java id of a Bedrock block ("minecraft:short_grass", "gloamingforest:canopy_log").
[[nodiscard]] std::string javaId(Block const& block);
/// `block` with Java property `name` set to `value` ("axis" -> "x"); `block` when it has no such state.
[[nodiscard]] Block const& withProperty(Block const& block, std::string_view name, std::string_view value);

/// Java block properties used by world generation.
[[nodiscard]] bool isAir(Block const& block);
/// Water or lava, including a waterlogged block (Bedrock's liquid layer) when `region` is given.
[[nodiscard]] bool isFluid(Block const& block);
/// Blocks.WATER / Blocks.LAVA: the still and the flowing ids (Java's fluid levels).
[[nodiscard]] bool isWater(Block const& block);
[[nodiscard]] bool isLava(Block const& block);
[[nodiscard]] bool blocksMotion(Block const& block);
[[nodiscard]] bool isLeaves(Block const& block);
/// BlockState#canBeReplaced (the blocks built `replaceable`: air, water, grass, snow layer, ...,
/// and upstream's fiddlehead, fallen leaves, ominous fire and wispy cloud).
[[nodiscard]] bool isReplaceable(Block const& block);
/// Membership in a Java block tag ("minecraft:dirt", "gloamingforest:mazestone"): gf::tags (upstream's
/// tag data and its vanilla bridge, asked with the block's game id), plus the vanilla tags of Java
/// 1.21.1 the bridge does not answer (minecraft:fences, doors, trapdoors, overworld_natural_logs).
/// During a chunk's generation or decoration and a natural spawner tick the answer of a (tag, block)
/// is asked once and kept for the rest of that pass.
[[nodiscard]] bool hasTag(Block const& block, std::string_view javaTag);
/// `block` may stay at `pos` (BlockState#canSurvive).
[[nodiscard]] bool canSurvive(Region& region, BlockPos const& pos, Block const& block);
/// The block source behind a region (nullptr for in-memory regions).
[[nodiscard]] BlockSource* blockSource(Region& region);

// ---- providers --------------------------------------------------------------------------------------
[[nodiscard]] Block const* sampleState(Json const& provider, RandomSource& random, BlockPos const& pos);
/// RuleBasedBlockStateProvider (disk): {fallback, rules: [{if_true, then}]}.
[[nodiscard]] Block const* sampleRuleState(Json const& provider, Region& region, RandomSource& random, BlockPos const& pos);
[[nodiscard]] int          sampleInt(Json const& provider, RandomSource& random);
[[nodiscard]] float        sampleFloat(Json const& provider, RandomSource& random);
[[nodiscard]] int          sampleHeight(Json const& provider, RandomSource& random, Region const& region);
/// A vertical anchor ({"absolute": y} / {"above_bottom": n} / {"below_top": n}).
[[nodiscard]] int resolveAnchor(Json const& anchor, Region const& region);
/// A block predicate (matching_blocks, matching_block_tag, matching_fluids, solid, replaceable,
/// would_survive, has_sturdy_face, inside_world_bounds, not, all_of, any_of, true).
[[nodiscard]] bool testPredicate(Json const& predicate, Region& region, BlockPos const& pos);
/// A structure rule test (always_true, block_match, blockstate_match, tag_match, random_block_match,
/// random_blockstate_match).
[[nodiscard]] bool testRule(Json const& rule, Block const& block, RandomSource& random);
/// Java Heightmap names ("MOTION_BLOCKING") -> worldgen::Heightmap.
[[nodiscard]] worldgen::Heightmap heightmapOf(std::string_view javaName);

// ---- features -----------------------------------------------------------------------------------------
/// A configured feature holder: an id or an inline {type, config}.
bool placeConfigured(Json const& holder, Region& region, BlockPos const& pos, RandomSource& random);
/// A placed feature holder: an id or an inline {feature, placement}.
bool placePlaced(Json const& holder, Region& region, BlockPos const& pos, RandomSource& random);
/// The imported JSON of a configured / placed feature (nullptr: unknown).
[[nodiscard]] Json const* configuredJson(std::string const& id);
[[nodiscard]] Json const* placedJson(std::string const& id);
/// Random weighted pick from [{data, weight}] (SimpleWeightedRandomList#getRandomValue).
[[nodiscard]] Json const* weightedPick(Json const& entries, RandomSource& random);

// ---- trees (TreeFeature, TrunkPlacer, FoliagePlacer) -------------------------------------------------
// The tree feature computes the trunk height (base_height + rand_a + rand_b), the foliage height
// ("height" when the placer has one, the vanilla rules otherwise, else 0) and the foliage radius
// ("radius") as Java does, then calls the trunk placer and, per attachment, the foliage placer. A
// foliage placer samples its own "offset" first (FoliagePlacer#createFoliage). Decorators get
// tree.logs / tree.leaves / tree.roots in Java's order (HashSet iteration, then by y).
/// TreeFeature#validTreePos: air or #minecraft:replaceable_by_trees.
[[nodiscard]] bool validTreePos(Region& region, BlockPos const& pos);
/// TrunkPlacer#isFree: validTreePos or a log.
[[nodiscard]] bool isFree(Region& region, BlockPos const& pos);
/// TrunkPlacer#placeLog with the tree's trunk_provider (optionally with the Java "axis").
bool placeLog(TreeContext& tree, BlockPos const& pos, char axis = 0);
/// TrunkPlacer#setDirtAt with the tree's dirt_provider / force_dirt.
void setDirtAt(TreeContext& tree, BlockPos const& pos);
/// FoliagePlacer#tryPlaceLeaf with the tree's foliage_provider.
bool tryPlaceLeaf(TreeContext& tree, BlockPos const& pos);
/// Sets a decoration block (vines, cocoa, ...) of the tree.
void setDecoration(TreeContext& tree, BlockPos const& pos, Block const& block);

} // namespace gf::features::kit

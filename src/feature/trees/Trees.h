#pragma once

// U3 trees (unit-internal header): TF's trees, leaves, saplings and magic log cores.
//
//   TreePlacers.cpp   branching_trunk_placer, trunk_mover_upper, spheroid_foliage_placer, core_placer,
//                     trunkside_decorator, tree_roots, dangle_from_tree_decorator
//   TreeFeatures.cpp  mega_oak, mega_canopy, large_winter_tree, miners_tree, tree_of_time,
//                     dark_canopy_tree, anywhere_tree, snow_under_trees
//   Mushrooms.cpp     canopy_brown_mushroom, canopy_red_{vanilla,smooth,spheroid,flat}_mushroom
//   Cores.cpp         the four magic log cores (toggle, 20-tick cycle, effects)
//   Leaves.cpp        transformation leaves' runes, mangrove sapling water, hollow oak sapling
//   Module.cpp        the module and the config section "trees"

#include <cstdint>
#include <map>
#include <string>

#include "nlohmann/json.hpp"

#include "feature/features/Features.h"

class Biome;
class Block;
class BlockPos;
class BlockSource;
class ChunkPos;
class Player;

namespace gf::feature::trees {

/// Config section "trees" (docs/spec/config.md): TFConfig's Magic Trees ranges (0..128, default 16;
/// a value <= 0 disables that core).
struct TreesConfig {
    int version                 = 1;
    int timeCoreRange           = 16;
    int transformationCoreRange = 16;
    int miningCoreRange         = 16;
    int sortingCoreRange        = 16;
};
[[nodiscard]] TreesConfig const& config();

/// TreeFeature's trunk / foliage setters for a tree being placed by the features contract.
void setLog(gf::features::TreeContext& tree, BlockPos const& pos, Block const& block);
void setLeaf(gf::features::TreeContext& tree, BlockPos const& pos, Block const& block);
/// Runs one of TF's tree decorators on a tree (TF's own tree features); the positions it decorates
/// (the decoration setter's set) are appended to `decorations` when given.
void runDecorator(gf::features::TreeContext& tree, nlohmann::json const& decorator, std::vector<BlockPos>* decorations = nullptr);

void registerTreePlacers();
void registerTreeFeatures();
void registerMushrooms();

void installCores();
void uninstallCores();
void tickCores();
void installLeaves();
void uninstallLeaves();
void tickLeaves();

/// The four cores.
enum class Core { Time, Transformation, Mining, Sorting };
/// The core of a block id (nullopt: not a core).
[[nodiscard]] bool coreOf(std::string const& blockId, Core& out);
/// The configured range of a core (<= 0: disabled).
[[nodiscard]] int coreRange(Core core);
/// One cycle of a core's effect at `pos` (SpecialMagicLogBlock#tick without the reschedule); the
/// return value counts what it did (blocks ticked, columns transformed, ores moved, items sorted).
int performCoreEffect(Core core, BlockSource& region, BlockPos const& pos);
/// Whether the tree of time's cycle works the block at `pos` (TimeLogCoreBlock: BlockState#isRandomlyTicking,
/// or a block entity with a ticker).
[[nodiscard]] bool timeCoreWorks(BlockSource& region, BlockPos const& pos);
/// The action bar text a disabled core shows (misc.twilightforest.core_disabled with the core's name).
[[nodiscard]] std::string coreDisabledMessage(Player const& player, Core core);
/// The cores known to tick (selftests).
[[nodiscard]] std::size_t activeCoreCount();
/// Selftests: the transformation core turns columns into `biomeId` instead of the enchanted forest
/// ("" restores it).
void setTransformationTargetForTest(std::string biomeId);
/// Whether the world has the enchanted forest biome (U1's behaviour pack biome).
[[nodiscard]] bool hasTransformationTarget(BlockSource& region);
/// The chunk resends of the transformation core so far (sent: LevelChunkPackets queued to players;
/// refused: the publisher did not send).
struct ChunkResendStats {
    std::uint64_t sent    = 0;
    std::uint64_t refused = 0;
};
[[nodiscard]] ChunkResendStats chunkResendStats();
/// The minewood core's ore -> ground map (Bedrock ids).
[[nodiscard]] std::map<std::string, std::string> miningCoreOres();
/// The leaf distances TreeFeature.updateLeaves wrote so far (selftests).
[[nodiscard]] std::uint64_t leafDistanceWrites();
/// HollowTreeStructure#checkSaplingClearance at the robust twilight oak sapling `pos`.
[[nodiscard]] bool hollowTreeClearance(BlockSource& region, BlockPos const& pos);
/// The leaf runes the server sent to players without the client mod so far.
[[nodiscard]] std::uint64_t vanillaLeafRunesSent();
/// `block` with its twilightforest:distance set to `distance` (its other states kept); `block` when it
/// has no distance.
[[nodiscard]] Block const& leafWithDistance(Block const& block, int distance);
/// Sends chunk `chunk` again to every player whose view holds it (the transformation core's biome
/// resend); returns how many got it.
int resendChunkToViewers(BlockSource& region, ChunkPos const& chunk);
/// "minecraft:plains" / "twilightforest:enchanted_forest" of a biome.
[[nodiscard]] std::string biomeName(Biome const& biome);

} // namespace gf::feature::trees

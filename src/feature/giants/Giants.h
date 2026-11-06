// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U29 (giants): the unit's internal header (its own files and selftests only; rule R2).
//
//   Beanstalk.cpp   magic beans, the beanstalk grower (a block entity in TF: BlockData "grower"
//                   + gf:giants:growers), uberous soil
//   Clouds.cpp      cloud blocks: fall damage, cloud puffs, precipitation (random ticks,
//                   isRainingAt), the vanilla clients' rain / snow
//   GiantBlocks.cpp the 4 x 4 x 4 giant blocks, their tiling state and the giant pickaxe / sword
//   Mobs.cpp        giant miner and armored giant
//   Structure.cpp   twilightforest:giant_house

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "core/giants/Giants.h"
#include "core/jrand/JRand.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::feature::giants {

/// Config section "giants" (R13, docs/spec/config.md).
struct GiantsConfig {
    int version = 1;
    /// common.cloudBlockPrecipitationDistance (cloud_precipitation): blocks below a cloud the
    /// weather logic checks; 0 disables cloud weather.
    int cloudBlockPrecipitationDistance = 32;
    /// client.cloudBlockPrecipitationDistance: the client mod's render distance of cloud
    /// precipitation; -1 = the common value, 0 = off.
    int clientCloudBlockPrecipitationDistance = -1;
    /// client.giantSkinUUIDs (giant_skin_uuid_list): skins the giants use; empty = the viewer's.
    std::vector<std::string> giantSkinUUIDs;
};
[[nodiscard]] GiantsConfig const& config();

namespace id {
inline constexpr char UberousSoil[]      = "twilightforest:uberous_soil";
inline constexpr char HugeStalk[]        = "twilightforest:huge_stalk";
inline constexpr char BeanstalkGrower[]  = "twilightforest:beanstalk_grower";
inline constexpr char BeanstalkLeaves[]  = "twilightforest:beanstalk_leaves";
inline constexpr char WispyCloud[]       = "twilightforest:wispy_cloud";
inline constexpr char FluffyCloud[]      = "twilightforest:fluffy_cloud";
inline constexpr char RainyCloud[]       = "twilightforest:rainy_cloud";
inline constexpr char SnowyCloud[]       = "twilightforest:snowy_cloud";
inline constexpr char GiantCobblestone[] = "twilightforest:giant_cobblestone";
inline constexpr char GiantLog[]         = "twilightforest:giant_log";
inline constexpr char GiantLeaves[]      = "twilightforest:giant_leaves";
inline constexpr char GiantObsidian[]    = "twilightforest:giant_obsidian";
inline constexpr char MagicBeans[]       = "twilightforest:magic_beans";
inline constexpr char GiantPickaxe[]     = "twilightforest:giant_pickaxe";
inline constexpr char GiantSword[]       = "twilightforest:giant_sword";
inline constexpr char GiantMiner[]       = "twilightforest:giant_miner";
inline constexpr char ArmoredGiant[]     = "twilightforest:armored_giant";
inline constexpr char GiantHouse[]       = "twilightforest:giant_house";
inline constexpr char CloudPuff[]        = "twilightforest:cloud_puff";
inline constexpr char GiantCell[]        = "twilightforest:giant_cell";
} // namespace id

/// The module's random source (Java's level random).
[[nodiscard]] core::jrand::RandomSource& random();

// ---- Beanstalk.cpp ---------------------------------------------------------------------------------
void installBeanstalk();
/// MagicBeansItem#useOn on uberous soil at `soil` (true: SUCCESS, the grower was placed).
bool useMagicBeans(Player& player, ItemStack& stack, BlockSource& region, BlockPos const& soil);
/// Places a beanstalk grower at `pos` and starts driving it.
void startGrower(BlockSource& region, BlockPos const& pos);
/// The positions of the growers the module drives.
[[nodiscard]] std::vector<core::GlobalPos> growers();
/// One server tick of every loaded grower (the module calls it every tick; selftests may add more).
void tickGrowers();
/// UberousSoilBlock#neighborChanged (from = the changed neighbour).
void uberousNeighborChanged(BlockSource& region, BlockPos const& pos, BlockPos const& from);
/// UberousSoilBlock#isValidBonemealTarget / #performBonemeal.
[[nodiscard]] bool uberousBonemealTarget(BlockSource& region, BlockPos const& pos);
void uberousPerformBonemeal(BlockSource& region, BlockPos const& pos, core::jrand::RandomSource& random);
/// BoneMealItem#applyBonemeal on the block at `pos` (false: not a valid target).
bool applyBonemeal(BlockSource& region, BlockPos const& pos);

// ---- Clouds.cpp ------------------------------------------------------------------------------------
void installClouds();
enum class Precipitation { None, Rain, Snow };
[[nodiscard]] bool isCloud(std::string const& blockId);
/// CloudBlock#getCurrentPrecipitation's precipitation (the rain level is the dimension's).
[[nodiscard]] Precipitation cloudPrecipitation(BlockSource& region, BlockPos const& pos, std::string const& blockId);
/// CloudBlock#randomTick.
void cloudRandomTick(BlockSource& region, BlockPos const& pos, std::string const& blockId, core::jrand::RandomSource& random);
/// BlockHooks#isRainingAt's addition: a raining cloud above `pos` (the level's own rain is not asked).
[[nodiscard]] bool rainingFromCloudAt(BlockSource& region, BlockPos const& pos);
/// common.cloudBlockPrecipitationDistance.
[[nodiscard]] int commonPrecipitationDistance();
/// Heightmap.Types.MOTION_BLOCKING's isOpaque of the block at `pos`.
[[nodiscard]] bool motionBlockingOpaque(BlockSource& region, BlockPos const& pos);
/// CloudBlock#fallOn: fall damage on a cloud is 10 %.
[[nodiscard]] float cloudFallDamage(float amount);

// ---- GiantBlocks.cpp -------------------------------------------------------------------------------
void installGiantBlocks();
[[nodiscard]] bool isGiantBlock(std::string const& blockId);
/// The giant block `blockId` at `pos` with its giant_cell tiling state (nullptr: unknown id).
[[nodiscard]] Block const* giantBlockAt(std::string const& blockId, int x, int y, int z);
/// GiantBlock#setPlacedBy: fills the cell of `pos` with `blockId`.
void fillGiantCell(BlockSource& region, BlockPos const& pos, std::string const& blockId);
/// Gives every block of the giant block type in the cell of `pos` its tiling state.
void fixGiantCell(BlockSource& region, BlockPos const& pos);
/// Whether every block of the cell is `blockId` (GiantBlock#isVolumeFilled).
[[nodiscard]] bool giantCellFilled(BlockSource& region, BlockPos const& pos, std::string const& blockId);
/// The GiantPickaxeMiningAttachment of a player (by unique id, R16).
[[nodiscard]] core::giants::GiantPickMining& pickMining(std::int64_t playerUid);
/// GiantPickItem#getDestroySpeed for a block id (base: the tier's speed 4).
[[nodiscard]] float giantPickDestroySpeed(std::string const& blockId, float base);

// ---- Mobs.cpp --------------------------------------------------------------------------------------
void installMobs();
/// GiantMiner / ArmoredGiant#populateDefaultEquipmentSlots.
void equipGiant(Actor& giant);
/// The damage of the giant's melee (ATTACK_DAMAGE 2 + the held weapon's bonus).
[[nodiscard]] float giantAttackDamage(Actor& giant);

// ---- Structure.cpp ---------------------------------------------------------------------------------
void installStructure();

} // namespace gf::feature::giants

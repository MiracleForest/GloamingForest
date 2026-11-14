#pragma once

// U30 (finalcastle): helpers shared by the castle blocks (doors, force fields, thorns, cinder
// furnace) and their selftests. Server thread only. Rule R16: nothing here keeps Actor* / Player* /
// Block* across ticks.
//
// Custom Bedrock blocks have neither scheduled ticks nor block entities, so this part keeps both for
// its own blocks (as U26 does for the dark tower devices):
//
//   scheduled ticks   Java's level.scheduleTick(pos, block, delay): one pending tick per position and
//                     block (a second schedule while one is pending is ignored, as Java's
//                     LevelTicks), run when due and the chunk is loaded, dropped when the block
//                     changed type; persisted in the world DB (gf:finalcastle:ticks).
//   device tickers    Java's BlockEntityTicker (the cinder furnace): every server tick for each
//                     tracked position whose chunk is loaded; positions are tracked when the block is
//                     placed (any cause) or first seen by a handler, forgotten when the block is
//                     gone; persisted (gf:finalcastle:devices).

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

#include "core/GlobalPos.h"
#include "core/jrand/JRand.h"

class Actor;
class Block;
class BlockSource;
class Level;
class Player;

namespace gf::feature::finalcastle::blocks {

using Json = nlohmann::json;
using core::GlobalPos;

// ---- ids -------------------------------------------------------------------------------------------
namespace id {
inline constexpr char YellowDoor[]   = "gloamingforest:yellow_castle_door";
inline constexpr char VioletDoor[]   = "gloamingforest:violet_castle_door";
inline constexpr char PinkDoor[]     = "gloamingforest:pink_castle_door";
inline constexpr char BlueDoor[]     = "gloamingforest:blue_castle_door";
inline constexpr char VioletField[]  = "gloamingforest:violet_force_field";
inline constexpr char PinkField[]    = "gloamingforest:pink_force_field";
inline constexpr char OrangeField[]  = "gloamingforest:orange_force_field";
inline constexpr char GreenField[]   = "gloamingforest:green_force_field";
inline constexpr char BlueField[]    = "gloamingforest:blue_force_field";
inline constexpr char BrownThorns[]  = "gloamingforest:brown_thorns";
inline constexpr char GreenThorns[]  = "gloamingforest:green_thorns";
inline constexpr char BurntThorns[]  = "gloamingforest:burnt_thorns";
inline constexpr char ThornRose[]    = "gloamingforest:thorn_rose";
inline constexpr char ThornLeaves[]  = "gloamingforest:thorn_leaves";
inline constexpr char Deadrock[]     = "gloamingforest:weathered_deadrock";
inline constexpr char CinderFurnace[] = "gloamingforest:cinder_furnace";
inline constexpr char CinderLog[]    = "gloamingforest:cinder_log";
inline constexpr char CinderWood[]   = "gloamingforest:cinder_wood";
} // namespace id

inline constexpr std::array<char const*, 4> kDoors{id::YellowDoor, id::VioletDoor, id::PinkDoor, id::BlueDoor};
inline constexpr std::array<char const*, 5> kFields{id::VioletField, id::PinkField, id::OrangeField, id::GreenField, id::BlueField};
/// Every ThornsBlock (BurntThornsBlock extends it).
inline constexpr std::array<char const*, 3> kThorns{id::BrownThorns, id::GreenThorns, id::BurntThorns};

[[nodiscard]] bool isDoor(std::string const& blockId);
[[nodiscard]] bool isField(std::string const& blockId);
/// `instanceof ThornsBlock`: brown, green and burnt thorns.
[[nodiscard]] bool isThorns(std::string const& blockId);

// ---- directions (Java's Direction.values() order is Bedrock's FacingID order) ------------------------
enum Dir : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
inline constexpr std::array<int, 6> kDx{0, 0, 0, 0, -1, 1};
inline constexpr std::array<int, 6> kDy{-1, 1, 0, 0, 0, 0};
inline constexpr std::array<int, 6> kDz{0, 0, -1, 1, 0, 0};
[[nodiscard]] inline int opposite(int d) { return d ^ 1; }
/// 0 x, 1 y, 2 z (Direction.Axis)
[[nodiscard]] inline int axisOf(int d) { return d < 2 ? 1 : (d < 4 ? 2 : 0); }
[[nodiscard]] char const* dirName(int d);
[[nodiscard]] char const* axisName(int axis);
/// "x" / "y" / "z" -> 0 / 1 / 2 (1 for anything else)
[[nodiscard]] int axisFromName(std::string const& name);
[[nodiscard]] std::optional<int> dirFromName(std::string const& name);
[[nodiscard]] BlockPos offset(BlockPos const& p, int d, int n = 1);
/// gloamingforest:<direction>
[[nodiscard]] std::string sideState(int d);

// ---- level -----------------------------------------------------------------------------------------
[[nodiscard]] Level*        level();
[[nodiscard]] std::uint64_t now();
/// The level random of the blocks (Java's level.getRandom()).
[[nodiscard]] core::jrand::RandomSource& random();
[[nodiscard]] int dimOf(BlockSource& region);

// ---- blocks ----------------------------------------------------------------------------------------
[[nodiscard]] std::string idAt(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool        boolState(Block const& block, char const* state);
[[nodiscard]] bool        boolStateAt(BlockSource& region, BlockPos const& pos, char const* state);
[[nodiscard]] std::string textState(Block const& block, char const* state);
/// The block states of `block` as JSON (booleans, integers and strings).
[[nodiscard]] Json statesJson(Block const& block);
/// Sets `blockId` with `states` (Java's setBlockAndUpdate). False when the id or a state is unknown.
bool set(BlockSource& region, BlockPos const& pos, std::string const& blockId, Json const& states = Json::object());
/// Changes some states of the block at `pos`, keeping the others (state.setValue(...)); true when
/// the block changed.
bool setStates(BlockSource& region, BlockPos const& pos, Json const& changes);
[[nodiscard]] bool isAir(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool loaded(BlockSource& region, BlockPos const& pos);
/// Java isFaceSturdy(FULL) of the block at `pos` for its face `face`.
[[nodiscard]] bool sturdy(BlockSource& region, BlockPos const& pos, int face);
/// Java Block.isExceptionForConnection: leaves, barrier, carved / jack o'lantern, melon, pumpkin,
/// shulker boxes.
[[nodiscard]] bool exceptionForConnection(Block const& block);
/// Java BlockTags.LEAVES (Bedrock's leaves and the #minecraft:leaves members).
[[nodiscard]] bool isLeaves(Block const& block);
/// Java BlockTags.LOGS (Bedrock's "log" block tag and the #minecraft:logs members).
[[nodiscard]] bool isLog(Block const& block);
/// Java's hasNeighborSignal.
[[nodiscard]] bool powered(BlockSource& region, BlockPos const& pos);
/// Level#destroyBlock(pos, drops): break effects, drops when asked.
void destroy(BlockSource& region, BlockPos const& pos, bool drops);
[[nodiscard]] Vec3 center(BlockPos const& p);

// ---- effects ---------------------------------------------------------------------------------------
void sound(BlockSource& region, BlockPos const& pos, char const* event, float volume, float pitch);
void particle(BlockSource& region, char const* particleId, Vec3 const& pos, Vec3 const& velocity = {});
/// Level#levelEvent(event, pos, data) for the players near.
void levelEvent(BlockSource& region, int event, Vec3 const& pos, int data = 0);

// ---- players ---------------------------------------------------------------------------------------
[[nodiscard]] bool creative(Player const& player);

// ---- scheduled ticks and device tickers ------------------------------------------------------------
using BlockFn = std::function<void(BlockSource& region, BlockPos const& pos, Block const& block)>;

/// Java's scheduleTick(pos, block, delay) for the block now at `pos`.
void schedule(BlockSource& region, BlockPos const& pos, int delay);
/// Whether a tick is pending at `pos`.
[[nodiscard]] bool scheduled(GlobalPos const& pos);
/// The handler of scheduled ticks of `blockId` (Block#tick).
void onTick(std::string const& blockId, BlockFn fn);
/// Starts ticking the device at `pos` (idempotent).
void track(BlockSource& region, BlockPos const& pos);
/// The device ticker of `blockId` (BlockEntityTicker).
void onDeviceTick(std::string const& blockId, BlockFn fn);
/// Called when a tracked device is forgotten (block gone or world closed).
void onForget(std::string const& blockId, std::function<void(GlobalPos const&)> fn);
[[nodiscard]] bool tracked(GlobalPos const& pos);

/// Installs the runtime (once per process).
void installRuntime();

// ---- the parts -------------------------------------------------------------------------------------
void installDoors();
void installForceFields();
void installThorns();
void installThornFeature();
void installCinderFurnace();

// ---- shared with the selftests ---------------------------------------------------------------------
/// CastleDoorBlock#onActivation: true when the door went active (SUCCESS).
bool activateDoor(BlockSource& region, BlockPos const& pos);
/// ForceFieldBlock#canConnectTo; `state` null: placement (neighbours counted from the world).
[[nodiscard]] bool fieldConnects(BlockSource& region, BlockPos const& pos, Block const* state, int dir);
/// ForceFieldBlock#getStateForPlacement as connection booleans by direction.
[[nodiscard]] std::array<bool, 6> fieldPlacement(BlockSource& region, BlockPos const& pos, int clickedFace, bool sneaking);
/// ThornsBlock#canConnectTo.
[[nodiscard]] bool thornsConnectTo(Block const& neighbour);
/// ThornsBlock#doThornBurst at `pos` (axis of the thorns there).
void thornBurst(BlockSource& region, BlockPos const& pos, int axis);
/// ThornRoseBlock#canSurvive of a rose at `pos` facing `facing`.
[[nodiscard]] bool roseCanSurvive(BlockSource& region, BlockPos const& pos, int facing);

/// The cinder furnace's block entity fields (CinderFurnaceBlockEntity / AbstractFurnaceBlockEntity).
struct FurnaceState {
    int litTime          = 0;
    int litDuration      = 0;
    int cookingProgress  = 0;
    int cookingTotalTime = 0;
    /// The input item last seen (AbstractFurnaceBlockEntity#setItem resets the cooking time when the
    /// input slot gets another item; the slots here change behind the furnace's back).
    std::string lastInput;
};
[[nodiscard]] FurnaceState furnaceState(GlobalPos const& pos);
void                       setFurnaceState(GlobalPos const& pos, FurnaceState const& s);
/// CinderFurnaceBlockEntity#countNearbyLogs.
[[nodiscard]] int countCinderLogs(BlockSource& region, BlockPos const& pos);
/// CinderFurnaceBlockEntity#getCurrentMultiplier(factor).
[[nodiscard]] int cinderMultiplier(BlockSource& region, BlockPos const& pos, int factor);
/// One server tick of the cinder furnace at `pos` (CinderFurnaceBlockEntity#tick).
void tickFurnace(BlockSource& region, BlockPos const& pos);

} // namespace gf::feature::finalcastle::blocks

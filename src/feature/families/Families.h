#pragma once

// Block families (F11), internal header of src/feature/families: the family data of every unit
// (assets/data/families/<u>.json -> <mod>/data/gf/families/, plus the gf_test fixture families of
// development builds), the block state schema tools/families writes, and the world helpers the
// family behaviours share. Other units never include this header (rule R2): they declare blocks
// as family data and react through gf::blockevents / gf::signs.
//
// State schema (tools/families/families.lua writes the same names):
//   traits   minecraft:cardinal_direction (Java's horizontal FACING), minecraft:block_face (the
//            clicked face: pillar axis, button face), minecraft:vertical_half (slab / trapdoor
//            half), minecraft:corner (stairs), minecraft:facing_direction (directional)
//   custom   twilightforest:<Java property> as integers 0..n; twilightforest:type (chests:
//            single / left / right) and twilightforest:lighting (none / normal / dim / ominous)

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class BlockType;
class ItemStack;
class Player;

namespace gf::feature::families {

using Json = nlohmann::json;
using core::GlobalPos;

enum class Family {
    Cube,
    Stairs,
    Slab,
    Fence,
    FenceGate,
    Wall,
    Door,
    Trapdoor,
    Button,
    PressurePlate,
    Pane,
    Pillar,
    Log,
    Wood,
    Leaves,
    Cross,
    Sapling,
    Potted,
    Sign,
    WallSign,
    HangingSign,
    WallHangingSign,
    Chest,
    TrappedChest,
    Horizontal,
    Directional,
    BinaryRotated,
    WallPillar,
    ConnectablePillar,
    ConnectedTexture,
};

/// Modifier families (any shape may add them).
enum Modifier : unsigned {
    Waterloggable = 1u << 0,
    Snowloggable  = 1u << 1,
    Climbable     = 1u << 2,
    Lightable     = 1u << 3,
};

struct Entry {
    std::string id;
    Family      family    = Family::Cube;
    unsigned    modifiers = 0;
    Json        params    = Json::object();

    [[nodiscard]] bool has(Modifier m) const { return (modifiers & m) != 0; }
    [[nodiscard]] std::string str(char const* key, std::string fallback = {}) const;
    [[nodiscard]] int  integer(char const* key, int fallback) const;
    [[nodiscard]] bool flag(char const* key, bool fallback = false) const;
};

/// The family name in data ("stairs", "fence_gate", ...); nullopt for an unknown name.
[[nodiscard]] std::optional<Family> familyOf(std::string_view name);
[[nodiscard]] char const*           familyName(Family family);

// ---- registry ------------------------------------------------------------------------------------

/// Loads the family data (mod load and every world ready; the block types are looked up again).
void loadRegistry();
/// The entry of a block id / block (nullptr when it is not a family block).
[[nodiscard]] Entry const* entryOf(std::string_view id);
[[nodiscard]] Entry const* entryOf(Block const& block);
/// Every entry (sorted by id).
[[nodiscard]] std::vector<Entry const*> entries();
/// The entries of one family.
[[nodiscard]] std::vector<Entry const*> entriesOf(Family family);

// ---- states --------------------------------------------------------------------------------------

namespace state {
inline constexpr char Cardinal[]     = "minecraft:cardinal_direction";
inline constexpr char BlockFace[]    = "minecraft:block_face";
inline constexpr char VerticalHalf[] = "minecraft:vertical_half";
inline constexpr char FacingDir[]    = "minecraft:facing_direction";
inline constexpr char Double[]       = "twilightforest:double";
inline constexpr char Open[]         = "twilightforest:open";
inline constexpr char Powered[]      = "twilightforest:powered";
inline constexpr char InWall[]       = "twilightforest:in_wall";
inline constexpr char Upper[]        = "twilightforest:upper";
inline constexpr char Hinge[]        = "twilightforest:hinge";
inline constexpr char Up[]           = "twilightforest:up";
inline constexpr char Distance[]     = "twilightforest:distance";
inline constexpr char Persistent[]   = "twilightforest:persistent";
inline constexpr char Stage[]        = "twilightforest:stage";
inline constexpr char Type[]         = "twilightforest:type";
inline constexpr char Rotation[]     = "twilightforest:rotation";
inline constexpr char Attached[]     = "twilightforest:attached";
inline constexpr char Layers[]       = "twilightforest:layers";
inline constexpr char Lighting[]     = "twilightforest:lighting";
/// twilightforest:north / east / south / west / up / down (connections), <dir>_wall (walls)
[[nodiscard]] std::string side(int dir);
[[nodiscard]] std::string wallSide(int dir);
} // namespace state

// ---- directions ----------------------------------------------------------------------------------
// FacingID order: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.

enum Dir : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
inline constexpr std::array<int, 6> kDx{0, 0, 0, 0, -1, 1};
inline constexpr std::array<int, 6> kDy{-1, 1, 0, 0, 0, 0};
inline constexpr std::array<int, 6> kDz{0, 0, -1, 1, 0, 0};
inline constexpr std::array<int, 4> kHorizontal{North, East, South, West};
[[nodiscard]] char const* dirName(int dir);
[[nodiscard]] std::optional<int> dirOf(std::string_view name);
[[nodiscard]] inline int opposite(int dir) { return dir ^ 1; }
[[nodiscard]] int clockwise(int dir);        ///< horizontal: north -> east -> south -> west
[[nodiscard]] int counterClockwise(int dir); ///< horizontal: north -> west -> south -> east
[[nodiscard]] inline bool horizontal(int dir) { return dir >= 2; }
[[nodiscard]] GlobalPos offset(GlobalPos p, int dir, int n = 1);
[[nodiscard]] BlockPos  offset(BlockPos const& p, int dir, int n = 1);

// ---- world ---------------------------------------------------------------------------------------

[[nodiscard]] BlockSource* regionOf(int dim);
/// The block at `p` (nullptr: no level, outside the height range or not loaded).
[[nodiscard]] Block const* blockAt(GlobalPos const& p);
[[nodiscard]] Block const* blockAt(BlockSource& region, BlockPos const& p);
[[nodiscard]] std::string  idAt(GlobalPos const& p);
bool                       set(GlobalPos const& p, Block const& block);
bool                       clear(GlobalPos const& p);
[[nodiscard]] bool         replaceableAt(GlobalPos const& p);

/// Integer / boolean state (nullopt: no such state).
[[nodiscard]] std::optional<int> intState(Block const& block, std::string_view name);
/// Any state as text ("north", "1", "true"); "" when absent.
[[nodiscard]] std::string strState(Block const& block, std::string_view name);
/// `block` with the states replaced ({"name": int | bool | "text"}); nullptr when a value is
/// unknown. States the block does not have are ignored.
[[nodiscard]] Block const* withStates(Block const& block, Json const& states);
/// Sets states of the block at `p` (only when they change). True when the block changed.
bool setStates(GlobalPos const& p, Json const& states);

/// Java's horizontal facing a placed block faces (Direction#getOpposite of the player's
/// horizontal look direction is `opposite(lookDir(player))`).
[[nodiscard]] int lookDir(Actor const& actor);
/// Java Direction.orderedByNearest of the player's look, nearest first.
[[nodiscard]] std::array<int, 6> nearestLooking(Actor const& actor);
/// Rotation 0..15 of a standing sign placed by `actor` (Java RotationSegment.convertToSegment(yRot + 180)).
[[nodiscard]] int signRotation(Actor const& actor);

/// The block's face toward `face` is sturdy (Java isFaceSturdy FULL).
[[nodiscard]] bool sturdy(BlockSource& region, BlockPos const& pos, int face);
/// A solid, opaque full block (Java isRedstoneConductor / isSolidRender for hinge checks).
[[nodiscard]] bool solidBlock(BlockSource& region, BlockPos const& pos);
/// Java Block.isExceptionForConnection: leaves, barriers, pumpkins, melons, shulker boxes.
[[nodiscard]] bool exceptionForConnection(Block const& block);
/// Redstone signal reaching the block (vanilla sources and gf::redstone emitters).
[[nodiscard]] bool hasNeighborSignal(BlockSource& region, BlockPos const& pos);

/// Plays a Java sound event at the block centre through gf::fx (silent until F10 maps it).
void sound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume = 1.f, float pitch = 1.f);
/// Plays a Bedrock sound name at the block (PlaySoundPacket to players nearby).
void bedrockSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume = 1.f, float pitch = 1.f);

[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1);
void                    drop(BlockSource& region, BlockPos const& pos, ItemStack const& item);
void                    give(Player& player, ItemStack const& item);
/// Uses one of the player's held item (not in creative).
void consumeHeld(Player& player, ItemStack const& contextItem);

/// Runs `fn` at the end of the current server tick (after the block change that asked).
void defer(std::function<void()> fn);
/// Runs `fn` after `ticks` server ticks (Java scheduleTick); dropped when the world closes.
void schedule(int ticks, std::function<void()> fn);

// ---- behaviours (one file each) ------------------------------------------------------------------

void installShapes();
void uninstallShapes();
void installDoors();
void uninstallDoors();
void installSwitches();
void uninstallSwitches();
void installPlants();
void uninstallPlants();
void installChests();
void uninstallChests();
void installSigns();
void uninstallSigns();
/// BlockProperty::Climbable on the climbable family blocks (server and client builds).
void applyClimbable();

/// Recomputes the neighbour-dependent states of the family block at `pos` (connections, wall
/// posts, leaf distance, chest pairs); true when the block changed.
bool updateShape(BlockSource& region, BlockPos const& pos);

/// Slab doubling (Java SlabBlock.canBeReplaced) for selftests and the use-on handler.
bool mergeSlab(BlockSource& region, BlockPos const& pos);
/// Door / trapdoor / fence gate use (Java useWithoutItem); `player` may be null.
bool useDoor(GlobalPos const& pos, Player* player);
bool useTrapdoor(GlobalPos const& pos, Player* player);
bool useFenceGate(GlobalPos const& pos, Player* player);
/// Places the upper half of the door just placed at `pos` (Java DoorBlock.setPlacedBy).
bool placeDoor(GlobalPos const& pos, Actor* placer, double hitX, double hitZ);
/// Presses a button (Java ButtonBlock.press); false when it is pressed already.
bool pressButton(GlobalPos const& pos);
/// Java LeavesBlock.updateDistance; the distance after the update (7: no log within 6).
int updateLeafDistance(BlockSource& region, BlockPos const& pos);
/// Java SaplingBlock.advanceTree.
void advanceSapling(BlockSource& region, BlockPos const& pos);
/// Pairs the chest at `pos` with a neighbour as Java ChestBlock.getStateForPlacement does.
void pairChest(BlockSource& region, BlockPos const& pos, int facing, bool sneaking, std::optional<int> clickedFace);
/// The other half of a double chest (nullopt: single).
[[nodiscard]] std::optional<BlockPos> chestPartner(BlockSource& region, BlockPos const& pos);

} // namespace gf::feature::families

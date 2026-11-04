#pragma once

// Block families (F11), internal header of src/feature/families: the family data of every unit
// (assets/data/families/<u>.json and F1's imported data -> <mod>/data/gf/families/, plus the gf_test
// fixture families of development builds), the block state schema, and the world helpers the
// family behaviours share. Other units never include this header (rule R2): they declare blocks
// as family data and react through gf::blockevents / gf::signs.
//
// State schema: docs/spec/blockstates.md (F1, frozen, rule R1). Every Java property p is the
// Bedrock state twilightforest:p with Java's values (booleans [false, true], integers 0..n, enums by
// name); no placement traits: the runtime sets the states on placement as Java's
// getStateForPlacement does, and keeps the neighbour-dependent ones as Java's updateShape does.
// States this kit adds to blocks (rule R1: additions only). The ones no Java block declares live
// in the kit's own namespace, so a TF block keeps exactly its frozen twilightforest:* states:
//   gloamingforest:lid         0..4   chests and trapped chests: the lid's angle (22.5 degree steps)
//   gloamingforest:open_count  0..15  trapped chests: the players looking inside (their signal)
//   gloamingforest:<direction> bool   connected textures: a connection on that side
//   gloamingforest:corner_<a>_<b> bool connected textures: the diagonal block (when the budget allows)
//   twilightforest:layers / lighting   snowloggable / lightable blocks without those states (Java's
//                                      SnowLoggable / LightableBlock properties)
// The params every family reads are documented in tools/families/README.md.

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

class AABB;
class Actor;
class Block;
class BlockPos;
class BlockSource;
class BlockType;
class Container;
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
    bool        shaped    = false; ///< a shape family was given (modifier-only entries keep their block)
    unsigned    modifiers = 0;
    Json        params    = Json::object();

    [[nodiscard]] bool has(Modifier m) const { return (modifiers & m) != 0; }
    [[nodiscard]] std::string str(char const* key, std::string fallback = {}) const;
    [[nodiscard]] int  integer(char const* key, int fallback) const;
    [[nodiscard]] double number(char const* key, double fallback) const;
    [[nodiscard]] bool flag(char const* key, bool fallback = false) const;
};

/// The family name in data ("stairs", "fence_gate", ...); nullopt for an unknown name.
[[nodiscard]] std::optional<Family> familyOf(std::string_view name);
/// The family of a data entry: its name refined by the Java class F1 imports (params.class: a
/// "sign" of class WallSignBlock is a wall sign, a "pillar" of class WallPillarBlock a wall pillar).
[[nodiscard]] std::optional<Family> familyOf(std::string_view name, std::string_view javaClass);
[[nodiscard]] char const*           familyName(Family family);

// ---- registry ------------------------------------------------------------------------------------

/// Loads the family data (mod load; the block types are looked up again on every world).
void loadRegistry();
/// The entry of a block id / block (nullptr when it is not a family block). Lock-free: the
/// registry is an immutable snapshot published atomically (server and client threads read it).
[[nodiscard]] Entry const* entryOf(std::string_view id);
[[nodiscard]] Entry const* entryOf(Block const& block);
[[nodiscard]] Entry const* entryOf(BlockType const& type);
/// Every entry (sorted by id).
[[nodiscard]] std::vector<Entry const*> entries();
/// The entries of one family.
[[nodiscard]] std::vector<Entry const*> entriesOf(Family family);
/// Block types with a 24 px collision (fences, walls, closed gates): a cheap pre-check for the
/// collision hook, which runs for every block the game tests.
[[nodiscard]] bool tallCollisionType(BlockType const& type);

// ---- states --------------------------------------------------------------------------------------

namespace state {
inline constexpr char Facing[]     = "twilightforest:facing";
inline constexpr char Half[]       = "twilightforest:half";
inline constexpr char Shape[]      = "twilightforest:shape";
inline constexpr char Type[]       = "twilightforest:type";
inline constexpr char Hinge[]      = "twilightforest:hinge";
inline constexpr char Open[]       = "twilightforest:open";
inline constexpr char Powered[]    = "twilightforest:powered";
inline constexpr char InWall[]     = "twilightforest:in_wall";
inline constexpr char Up[]         = "twilightforest:up";
inline constexpr char Axis[]       = "twilightforest:axis";
inline constexpr char Face[]       = "twilightforest:face";
inline constexpr char Distance[]   = "twilightforest:distance";
inline constexpr char Persistent[] = "twilightforest:persistent";
inline constexpr char Stage[]      = "twilightforest:stage";
inline constexpr char Rotation[]   = "twilightforest:rotation";
inline constexpr char Attached[]   = "twilightforest:attached";
inline constexpr char Layers[]     = "twilightforest:layers";
inline constexpr char Lighting[]   = "twilightforest:lighting";
inline constexpr char Rotated[]    = "twilightforest:rotated";
/// The namespace of the states the kit adds that no Java block declares.
inline constexpr char kKit[]       = "gloamingforest:";
inline constexpr char OpenCount[]  = "gloamingforest:open_count";
inline constexpr char Lid[]        = "gloamingforest:lid";
/// twilightforest:north / east / south / west / up / down (fence / pane / pillar connections are
/// booleans, wall sides none / low / tall)
[[nodiscard]] std::string side(int dir);
/// gloamingforest:north / ... (connected textures: a connection on that side)
[[nodiscard]] std::string ctSide(int dir);
/// gloamingforest:corner_<a>_<b> (connected textures: the diagonal block of sides a and b)
[[nodiscard]] std::string corner(int a, int b);
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
/// 0 x, 1 y, 2 z
[[nodiscard]] inline int axisOf(int dir) { return dir < 2 ? 1 : (dir < 4 ? 2 : 0); }
[[nodiscard]] char const* axisName(int axis);
/// Java Direction.get2DDataValue (S 0, W 1, N 2, E 3) and back.
[[nodiscard]] int to2D(int dir);
[[nodiscard]] int from2D(int value);
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

/// Integer state (booleans 0 / 1; nullopt: no such state).
[[nodiscard]] std::optional<int> intState(Block const& block, std::string_view name);
/// Boolean state (nullopt: no such state).
[[nodiscard]] std::optional<bool> boolState(Block const& block, std::string_view name);
[[nodiscard]] bool               flagState(Block const& block, std::string_view name); ///< false when absent
/// Any state as text ("north", "1", "true" for booleans); "" when absent.
[[nodiscard]] std::string strState(Block const& block, std::string_view name);
/// twilightforest:facing as a direction (`fallback` when absent).
[[nodiscard]] int facingOf(Block const& block, int fallback = North);
/// twilightforest:axis as 0 x, 1 y, 2 z (1 when absent).
[[nodiscard]] int axisStateOf(Block const& block);
/// `block` with the states replaced ({"name": bool | int | "text"}); nullptr when a value is
/// unknown. States the block does not have are ignored.
[[nodiscard]] Block const* withStates(Block const& block, Json const& states);
/// Sets states of the block at `p` (only when they change). True when the block changed.
bool setStates(GlobalPos const& p, Json const& states);
bool setStates(BlockSource& region, BlockPos const& p, Json const& states);

/// Java Entity.getDirection (the player's horizontal look direction).
[[nodiscard]] int lookDir(Actor const& actor);
/// Java Direction.orderedByNearest of the player's look, nearest first.
[[nodiscard]] std::array<int, 6> nearestLooking(Actor const& actor);
/// Java's yaw of an entity (degrees).
[[nodiscard]] float yawOf(Actor const& actor);
/// Java RotationSegment.convertToSegment(degrees).
[[nodiscard]] int rotationSegment(float degrees);

/// The entity type that shows a sign side's text as its name tag (the vanilla clients' fallback,
/// generated by tools/families; Signs.cpp spawns them, the client mod's SignText.cpp hides them).
inline constexpr char kSignTextDisplayType[] = "gloamingforest:sign_text";
/// Java SignBlock.getYRotationDegrees, the direction a sign's front text faces: a standing or
/// ceiling sign's rotation x 22.5, a wall sign's facing (Direction.toYRot: south 0, west 90, north
/// 180, east 270). The server places the display entities by it (Signs.cpp), the client mod turns
/// the text on the board by it (SignText.cpp).
[[nodiscard]] float signYaw(Block const& block, Entry const& e);
namespace detail {
/// Selftests (both builds): the server's display entity of a sign side (nullptr when none).
[[nodiscard]] Actor* signDisplay(GlobalPos const& pos, bool front);
} // namespace detail

/// The block's face toward `face` is sturdy (Java isFaceSturdy FULL).
[[nodiscard]] bool sturdy(BlockSource& region, BlockPos const& pos, int face);
/// Java isFaceSturdy(SupportType.CENTER): the face can hold something in its centre.
[[nodiscard]] bool sturdyCenter(BlockSource& region, BlockPos const& pos, int face);
/// Java BlockState.isSolid (the block blocks movement and is not replaceable).
[[nodiscard]] bool solidAt(BlockSource& region, BlockPos const& pos);
/// A solid, opaque full block (Java isRedstoneConductor / isCollisionShapeFullBlock).
[[nodiscard]] bool solidBlock(BlockSource& region, BlockPos const& pos);
/// Java Block.isExceptionForConnection: leaves, barriers, pumpkins, melons, shulker boxes.
[[nodiscard]] bool exceptionForConnection(Block const& block);
/// Vanilla leaves (Bedrock's leaves carry no Java distance; they count as leaves).
[[nodiscard]] bool vanillaLeaves(Block const& block);
/// The entities whose box intersects `box` (players too: the game keeps them outside the chunks).
[[nodiscard]] std::vector<Actor*> entitiesIn(BlockSource& region, AABB const& box);
/// Redstone signal reaching the block (vanilla sources and gf::redstone emitters).
[[nodiscard]] bool hasNeighborSignal(BlockSource& region, BlockPos const& pos);

/// Plays a TF sound event ("block.twilightforest.jar.place") at the block centre through gf::fx.
void sound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume = 1.f, float pitch = 1.f);
/// Plays a Bedrock sound name (a vanilla sound Java plays: "open.wooden_door") at the block.
void bedrockSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume = 1.f, float pitch = 1.f);
/// Java's random pitch of doors, gates and chests: nextFloat() * 0.1 + 0.9.
[[nodiscard]] float wobblePitch();
/// A vanilla particle effect at the block centre (Bedrock effect name).
void particle(BlockSource& region, BlockPos const& pos, std::string const& effect);
/// The block's own break effect (Java levelEvent 2001: break particles and sound).
void breakEffect(BlockSource& region, BlockPos const& pos, Block const& block);

[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1);
void                    drop(BlockSource& region, BlockPos const& pos, ItemStack const& item);
void                    give(Player& player, ItemStack const& item);
/// Uses one of the player's held item (not in creative).
void consumeHeld(Player& player, ItemStack const& contextItem);
/// Breaks the block at `pos` as Java's destroyBlock(pos, true): its drops (loot) and effect.
void destroyWithDrops(BlockSource& region, BlockPos const& pos);
/// Java's dropResources(state, level, pos) then removeBlock(pos, false): the drops without the
/// break effect (decaying leaves are silent); a waterlogged block leaves its water source.
void dropAndRemove(BlockSource& region, BlockPos const& pos);
/// Java Player.mayBuild (not adventure / spectator).
[[nodiscard]] bool mayBuild(Player const& player);

/// Bedrock repeats a use-on while the button is held, and simulated players report every use as
/// a repeat: true when `player` used `pos` less than 5 ticks ago (a held button), so a click acts
/// once. Every call counts as a use.
[[nodiscard]] bool repeatedUse(Player& player, GlobalPos const& pos, bool firstEvent);

/// Runs `fn` at the end of the current server tick (after the block change that asked).
void defer(std::function<void()> fn);
/// Runs `fn` after `ticks` server ticks (Java scheduleTick); dropped when the world closes (the
/// families that need one across a reload persist it themselves, see Switches.cpp).
void schedule(int ticks, std::function<void()> fn);
/// The server ticks since the module started (scheduled work).
[[nodiscard]] std::int64_t now();

// ---- behaviours (one file each) ------------------------------------------------------------------

void installPlacement();
void uninstallPlacement();
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
void installHoppers();
void uninstallHoppers();
/// BlockProperty::Climbable on the climbable family blocks (server and client builds).
void applyClimbable();

/// Recomputes the neighbour-dependent states of the family block at `pos` (connections, wall
/// posts, stairs shapes, leaf distance, chest pairs); true when the block changed.
bool updateShape(BlockSource& region, BlockPos const& pos);
/// Java StairBlock.getStairsShape for the stairs at `pos` (by their facing and half).
[[nodiscard]] std::string stairsShape(BlockSource& region, BlockPos const& pos, Block const& stairs);
/// ConnectedTextureModel.shouldConnectSide / isCornerBlockPresent: `other` is one of the blocks a
/// connected texture block `e` joins (params.connects_to, default: itself).
[[nodiscard]] bool ctConnects(Entry const& e, Block const* other);
/// ConnectionLogic.AXIS_PLANE_DIRECTIONS: the four sides around a face of axis `axis` (0 x, 1 y,
/// 2 z), in quadrant order (quadrant i lies between sides i and i + 1).
[[nodiscard]] constexpr std::array<int, 4> ctPlane(int axis) {
    return axis == 0 ? std::array<int, 4>{Up, North, Down, South} : axis == 1 ? std::array<int, 4>{North, East, South, West} : std::array<int, 4>{Up, East, Down, West};
}

/// Slab doubling (Java SlabBlock.canBeReplaced) for selftests and the use-on handler.
bool mergeSlab(BlockSource& region, BlockPos const& pos);
/// Door / trapdoor / fence gate use (Java useWithoutItem); `player` may be null.
bool useDoor(GlobalPos const& pos, Player* player);
bool useTrapdoor(GlobalPos const& pos, Player* player);
bool useFenceGate(GlobalPos const& pos, Player* player);
/// Orients a door just placed at `pos` and places its upper half (Java DoorBlock
/// getStateForPlacement + setPlacedBy).
bool placeDoor(GlobalPos const& pos, Actor* placer, double hitX, double hitZ);
/// Presses a button (Java ButtonBlock.press); false when it is pressed already.
bool pressButton(GlobalPos const& pos);
/// Java LeavesBlock.updateDistance; the distance after the update (7: no log within 6).
int updateLeafDistance(BlockSource& region, BlockPos const& pos);
/// Java SaplingBlock.advanceTree.
void advanceSapling(BlockSource& region, BlockPos const& pos);
/// Pairs the chest at `pos` as Java ChestBlock.getStateForPlacement does (`facing`: the chest's).
void pairChest(BlockSource& region, BlockPos const& pos, int facing, bool sneaking, std::optional<int> clickedFace);
/// The other half of a double chest (nullopt: single).
[[nodiscard]] std::optional<BlockPos> chestPartner(BlockSource& region, BlockPos const& pos);
/// The lid step (gloamingforest:lid) drawn for a ChestLidController openness in tenths (0..10).
[[nodiscard]] int chestLidStep(int openness);
/// The neighbour whose block decides whether the chest half `block` at `pos` pairs (Java
/// ChestBlock.getConnectedDirection; nullopt: a single chest, or not a chest).
[[nodiscard]] std::optional<BlockPos> chestPairingNeighbour(Block const& block, BlockPos const& pos);
/// What hoppers, hopper minecarts and droppers see at `pos` (Java HopperBlockEntity#getContainerAt
/// of a container block, Hoppers.cpp): the inventory contract's native container of a block hoppers
/// may reach (feature/ext/families/Hoppers.h: by default the chest-like declarations), for a paired
/// half of a declared double chest both halves as one 54-slot CompoundContainer (the right half
/// first; ChestBlock#getContainer ignoring the blocked rule). Every write reaches the inventory
/// contract's change notices (feature/ext/combat/InventoryChanges.h: open screens follow). A half
/// whose stacks are still in block data moves them first (migrateChest without a new block actor)
/// and a pending loot table is rolled (RandomizableContainer). nullptr: no native container there, a
/// block hoppers do not reach, or the client's level. Valid for the current tick only.
[[nodiscard]] Container* hopperContainerAt(BlockSource& region, BlockPos const& pos);

/// A chest saved without its block actor, or whose stacks still wait in block data (placed while
/// the chests had no block actor, F10's BlockData backend; feature/ext/combat/InventoryMigrate.h):
/// with `makeActor` a chest without a block actor gets the one its type declares first (never while
/// the game may be iterating the chunk's block actors, as during a hopper's tick, or inside a click),
/// then the waiting stacks move into its native container (inventory::ext::migrateBlockDataToNative;
/// without `makeActor` only when every stack finds room, else at the end of the tick). Runs for the
/// chests of a chunk at the end of the tick it loaded and in its first tick, lazily before a hopper
/// reads the chest, and on the tick after a player opens it. Returns the stacks moved (0: nothing to
/// move, or no native container to move them to).
int migrateChest(BlockSource& region, BlockPos const& pos, bool makeActor = true);
namespace detail {
/// The chests of the loaded chunks around chunk (cx, cz), that chunk excepted, move their block
/// data stacks into the block actors they have (migrateChest without making one). A chunk's first
/// tick after it loaded does it for its neighbours (loaded before the mod saw them load, they may be
/// loaded without ticking). Returns the stacks moved.
int settleNeighbourChests(BlockSource& region, int cx, int cz);
/// Before a hopper reads the chest half at `pos`: stacks it still keeps in block data move into the
/// block actor it has (none is made) and a pending loot table rolls
/// (RandomizableContainer#unpackLootTable(null)); at most once a tick per half.
void settleForAccess(BlockSource& region, GlobalPos const& pos);
/// The game loaded chunk (cx, cz) of dimension `dim` (any thread): its chests are settled at the end
/// of the tick (migrateChest, a block actor made for a chest saved without one).
void chunkLoaded(int dim, int cx, int cz);
/// Chunk loads noted so far (selftests).
[[nodiscard]] long long chunkLoadsSeen();
/// Settles the chunks noted since the last tick now (selftests).
void settleLoadedChunksNow();
} // namespace detail

} // namespace gf::feature::families

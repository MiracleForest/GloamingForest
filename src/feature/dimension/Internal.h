#pragma once

// Internal helpers of unit F4 (dimension), shared by its .cpp files only (rule R2: other units use
// feature/dimension/World.h).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class Dimension;
class ItemActor;
class Player;

namespace gf::feature::dimension {

inline constexpr char kPortalBlock[]     = "twilightforest:twilight_portal";
inline constexpr char kOneWayState[]     = "twilightforest:is_one_way";
inline constexpr char kWhooshSound[]     = "block.twilightforest.portal.whoosh";
inline constexpr char kMakePortalTrigger[] = "twilightforest:make_tf_portal";
inline constexpr char kDefaultDelayRule[]  = "playersTfPortalDefaultDelay";
inline constexpr char kCreativeDelayRule[] = "playersTfPortalCreativeDelay";
/// TF's defaults of the two gamerules (TFGameRules), used while the gamerule store has no value.
inline constexpr int kDefaultDelay  = 60;
inline constexpr int kCreativeDelay = 1;

// ---- dimensions ------------------------------------------------------------------------------------

/// The engine id of the Twilight Forest in the current world, -1 when it is not registered. Cheap
/// (cached); any thread.
[[nodiscard]] int twilightId();
/// Drops the cached id (world closed / a new world).
void resetTwilightId();
/// Whether a dimension object is the Twilight Forest (by its identifier; any thread, no level access).
[[nodiscard]] bool isTwilightDimension(Dimension const& dimension);
/// "minecraft:overworld" / "minecraft:the_nether" / "minecraft:the_end" / custom identifiers -> engine id.
[[nodiscard]] std::optional<int> dimensionByName(std::string_view identifier);
/// The identifier of an engine dimension id ("" when unknown).
[[nodiscard]] std::string dimensionName(int dim);
/// Java's DimensionType#coordinateScale of a dimension: overworld 1, nether 8, end 1, Twilight 0.125.
[[nodiscard]] double coordinateScaleOf(int dim);
/// dimension_type ambient_light: writes DimensionType#brightness with TF's 0.01 into the
/// dimension's brightness ramp (the Twilight Forest's Dimension object of a server or client
/// level). True when it changed.
bool applyAmbientLight(Dimension& dimension);
/// Build limits of a loaded dimension ([min, max) in blocks), nullopt when it is not loaded.
struct Height {
    int min = 0, max = 0;
};
[[nodiscard]] std::optional<Height> heightOf(int dim);

// ---- portal blocks -----------------------------------------------------------------------------------

/// The block is a Twilight portal (any is_one_way).
[[nodiscard]] bool isPortal(Block const& block);
/// The block is a portal one can travel through (is_one_way = false).
[[nodiscard]] bool isOpenPortal(Block const& block);
/// The portal block with is_one_way = `oneWay` (nullptr when the block is not registered).
[[nodiscard]] Block const* portalBlock(bool oneWay);
/// Block tag test with TF's own member lists as a fallback while the tag store is empty (rule R6:
/// the tags are F4's, imported from TF: portal/decoration, portal/edge, portal/fluid,
/// portal/generated_decoration).
[[nodiscard]] bool blockTagged(std::string const& tag, std::string const& blockId);
/// The members of a block tag (same fallback).
[[nodiscard]] std::vector<std::string> blockTagMembers(std::string const& tag);
/// Item tag test (portal/activator: #c:gems/diamond -> minecraft:diamond).
[[nodiscard]] bool itemTagged(std::string const& tag, std::string const& itemId);

// ---- portal travel --------------------------------------------------------------------------------------

/// The portal transition time of a player (TFPortalBlock#getPortalTransitionTime).
[[nodiscard]] int portalDelay(Player const& player);
/// The portal timer of the player (TFPortalAttachment: 0..60), for overlays and tests.
[[nodiscard]] int portalTimer(Player const& player);
/// An entity's portal process as vanilla keeps it (Entity#portalProcess, portalCooldown), whether
/// it is on a trip and whether its destination is preloaded (tests).
struct PortalProcessView {
    bool hasProcess = false;
    int  portalTime = 0;
    int  cooldown   = 0;
    bool travelling = false;
    bool preloaded  = false;
};
[[nodiscard]] PortalProcessView portalProcessOf(Actor const& actor);
/// Entity#setPortalCooldown with a given length (tests: an entity on cooldown that stays in a
/// portal never goes through it, its cooldown starts again every tick it is inside).
void setPortalCooldown(Actor& actor, int ticks);
/// Fires the portal::onTeleported listeners.
void notifyTeleported(Player& player, int from, int to);
/// Schedules a cross-dimension move and runs `then(player)` once the player is in `dim` and its
/// chunk is loaded, or `failed(player)` (the player may be gone: nullptr) when it did not get there
/// within 10 s. Rule R16: the player is looked up again by id.
void moveAcross(
    Player&                      player,
    int                          dim,
    Vec3 const&                  pos,
    float                        yaw,
    float                        pitch,
    std::function<void(Player&)> then,
    std::function<void(Player*)> failed = {}
);

/// gf::world::teleport: within a dimension at once; into another one the destination loads first
/// (the player waits where it is), then the player moves there with its riders (its shoulder
/// parrots on Bedrock) and portal::onTeleported fires; a move that does not complete is logged and
/// told to the player. Off its vehicle first (ServerPlayer#teleportTo). False when the player is
/// already on a trip.
bool teleportPlayer(Player& player, int dest, Vec3 const& at, float yaw, float pitch);

/// CapabilityEvents#newSpawnInTwilightForest (no-op unless newPlayersSpawnInTF).
void newSpawnInTwilightForest(Player& player);
/// ServerPlayer#getRespawnPosition() != null: the player has a bed / anchor / forced spawn point.
[[nodiscard]] bool hasRespawnPosition(Player const& player);
/// Forgets that a player (by UUID string) was banished to the Twilight Forest (tests).
void unbanish(std::string const& uuid);

/// Portal blocks changed at `pos` (lit, built, broken): the portal index reads that area again.
void portalIndexTouch(int dim, BlockPos const& pos);
/// The portal blocks the index knows in a dimension (the chunks near players; tests).
[[nodiscard]] std::vector<BlockPos> indexedPortals(int dim);

/// TF's functions tp_twilight / tp_overworld (Functions.cpp), `execute in <destination> run tp @p
/// ~ ~ ~` run at `source` in `srcDim`: who goes (@p, 0: nobody) and where. nullopt when the
/// destination does not exist.
struct FunctionTeleport {
    std::int64_t player = 0;
    int          dim    = 0;
    Vec3         pos;
};
[[nodiscard]] std::optional<FunctionTeleport> runTpFunction(int srcDim, Vec3 const& source, bool toTwilight);

/// Per-player state kept across ticks, keyed by the unique id (rule R16).
[[nodiscard]] std::int64_t idOf(Actor const& actor);
[[nodiscard]] Player* playerById(std::int64_t id);

// ---- portal lighting (Portal.cpp) -----------------------------------------------------------------------

/// Sends an action bar message (Java displayClientMessage(..., true)).
void actionBar(Player& player, std::string const& text);
/// TFConfig#getPortalLockingAdvancement: the advancement that unlocks portals (nullopt: none). The
/// configured id is checked once per value against the advancements the mod ships
/// (data/<namespace>/advancement[s]/<path>.json); an unknown one is logged, cleared and saved. A
/// vanilla id (minecraft:...) is kept unchecked with a warning: Bedrock has no vanilla advancements.
[[nodiscard]] std::optional<std::string> portalLockingAdvancement(bool quiet = false);
/// Whether advancement `id` exists (data/<namespace>/advancement[s]/<path>.json in the mod's data).
[[nodiscard]] bool advancementExists(std::string const& id);
/// A well-formed id in the minecraft namespace (Java's server knows every vanilla advancement).
[[nodiscard]] bool isVanillaAdvancement(std::string const& id);
/// TFPortalBlock#entityInside / ProgressionEvents: the "unworthy" message + missing-advancement
/// toast; true when the player may not use / light portals.
bool unworthy(Player& player);
/// Forgets which players saw the toast (TFPortalBlock.playersNotified).
void resetNotified();
/// TFPortalBlock#causeLightning.
void causeLightning(BlockSource& region, BlockPos const& pos, bool destructive);
/// TFPortalBlock#tryToCreatePortal (`catalyst` loses one item, may be null).
bool tryToCreatePortal(BlockSource& region, BlockPos const& pos, ItemActor* catalyst, Player* player);
/// ProgressionEvents#checkForPortalCreation.
void checkForPortalCreation(Player& player, BlockSource& world, float rangeToCheck);
/// TFPortalBlock#neighborChanged: the portal block at `pos` turns back into water when its floor
/// or one of its four sides is no longer valid (true when it did).
bool portalNeighborChanged(BlockSource& region, BlockPos const& pos);
/// Registers the portal block's handlers with gf::blockevents (once per process): neighbour
/// changes (TFPortalBlock#neighborChanged), and placements / removals by any cause, which the
/// portal index reads again at once.
void registerPortalBlockEvents();
/// The portal's explosion window (Portal.cpp, TFPortalBlock#getFluidState for explosions): from
/// the start of the first explosion in flight to the end of the last one (nested ones, or ones on
/// another thread), the portal's two states hold the water's explosion resistance; the last one
/// to end puts back what they held before. The Explosion::explode hook opens one per explosion
/// with PortalExplosionScope (an explosion that throws closes it too); selftests open them by hand.
void beginPortalExplosion();
void endPortalExplosion();
/// Explosions in flight (0: none).
[[nodiscard]] int portalExplosionDepth();
/// The water's explosion resistance the portal takes during an explosion (0 before the first world).
[[nodiscard]] float portalWaterResistance();
struct PortalExplosionScope {
    PortalExplosionScope() { beginPortalExplosion(); }
    ~PortalExplosionScope() { endPortalExplosion(); }
    PortalExplosionScope(PortalExplosionScope const&)            = delete;
    PortalExplosionScope& operator=(PortalExplosionScope const&) = delete;
};
/// ItemEntity#getOwner: the player who threw an item (-1: nobody). Only throws name their thrower
/// (Player#drop with includeThrowerName): death drops and other spawned items have none.
[[nodiscard]] std::int64_t itemOwner(ItemActor const& item);
/// Gives an item a thrower (an item made from a thrown one keeps its thrower).
void setItemOwner(ItemActor const& item, std::int64_t owner);
/// The player drop / item spawn hooks that remember who threw what, and the block survival hooks
/// (mushrooms, sugar cane).
void installItemOwners();
void uninstallItemOwners();
/// Looks up the portal's states and the water's explosion resistance the explosion window uses
/// (each world).
void refreshPortalTypes();
/// Forgets the item owners of a closed world.
void clearItemOwners();
/// The item `from` was copied into another dimension as `to`: it keeps its owner.
void moveItemOwner(std::int64_t from, std::int64_t to);
/// BlockHooks#modifySoilDecisionForMushroomBlockSurvivability: a Twilight portal among the eight
/// blocks around the one under `pos` keeps a mushroom at `pos` alive.
[[nodiscard]] bool portalKeepsMushroom(BlockSource& region, BlockPos const& pos);
/// TFPortalBlock#getFluidState for SugarCaneBlock#canSurvive: sugar cane at `pos` on #dirt / #sand
/// with a Twilight portal on a side of that soil block has its water.
[[nodiscard]] bool portalWatersSugarCane(BlockSource& region, BlockPos const& pos);

// ---- the portal's water (PortalFluid.cpp) -----------------------------------------------------------------

/// TFPortalBlock#getFluidState as Entity#updateFluidHeightAndDoFluidPushing sees it: the entity's
/// box reaches below the surface of a portal's level-1 water (1/9 of a block).
[[nodiscard]] bool inPortalWater(Actor& actor, BlockSource& region);
/// What the water does to an entity in it this tick (fall distance, fire, splash, water-sensitive
/// mobs); called for every entity near portal blocks, players included, once a tick.
void portalWaterTick(Actor& actor, BlockSource& region);
/// After every entity of the tick: who was in the water (the splash is for those who enter).
void portalWaterEndTick();
/// Forgets who was in the water (a closed world).
void portalWaterClear();
/// FarmBlock#isNearWater with the portal's water: a portal within 4 blocks of the farmland at
/// `pos`, at its level or one above.
[[nodiscard]] bool portalWatersFarmland(BlockSource& region, BlockPos const& pos);
/// The fall damage rule (gf::combat) and the farmland hook.
void installPortalFluid();
void uninstallPortalFluid();

} // namespace gf::feature::dimension

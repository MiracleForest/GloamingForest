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
/// (data/<namespace>/advancement/<path>.json); an unknown one is logged, cleared and saved.
[[nodiscard]] std::optional<std::string> portalLockingAdvancement(bool quiet = false);
/// Whether advancement `id` exists (data/<namespace>/advancement/<path>.json in the mod's data).
[[nodiscard]] bool advancementExists(std::string const& id);
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
/// TFPortalBlock#neighborChanged.
void portalNeighborChanged(BlockSource& region, BlockPos const& pos);
/// Registers the portal block's neighbour handler with gf::blockevents.
void registerPortalBlockEvents();
/// ItemEntity#getOwner: the player who threw an item (-1: nobody). Only throws name their thrower
/// (Player#drop with includeThrowerName): death drops and other spawned items have none.
[[nodiscard]] std::int64_t itemOwner(ItemActor const& item);
/// Gives an item a thrower (an item made from a thrown one keeps its thrower).
void setItemOwner(ItemActor const& item, std::int64_t owner);
/// The player drop / item spawn hooks that remember who threw what, and the block survival hooks
/// (mushrooms, sugar cane).
void installItemOwners();
void uninstallItemOwners();
/// Looks up the portal and water block types the explosion rule compares with (each world).
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

} // namespace gf::feature::dimension

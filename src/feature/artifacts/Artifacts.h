// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U9 (artifacts): scepters, charms, keepsake casket, fortification shields, potion flasks, ore
// meter and the other artifacts (docs/spec/artifacts.md). Shared declarations of the unit's .cpp
// files; not a contract (rule R2: other units use the ids, tags and triggers only).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/event/ListenerBase.h"

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/artifacts/Artifacts.h"
#include "core/jrand/JRand.h"

class AABB;
class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class Mob;
class Player;

namespace gf::feature::artifacts {

using Json   = nlohmann::json;
using Random = core::jrand::LegacyRandom;

// ---- ids ---------------------------------------------------------------------------------------------

inline constexpr char kTwilightScepter[]      = "twilightforest:twilight_scepter";
inline constexpr char kLifedrainScepter[]     = "twilightforest:lifedrain_scepter";
inline constexpr char kZombieScepter[]        = "twilightforest:zombie_scepter";
inline constexpr char kFortificationScepter[] = "twilightforest:fortification_scepter";
inline constexpr char kEssence[]              = "twilightforest:exanimate_essence";
inline constexpr char kWandBolt[]             = "twilightforest:wand_bolt";
inline constexpr char kLoyalZombie[]          = "twilightforest:loyal_zombie";
inline constexpr char kCharmEffect[]          = "twilightforest:charm_effect";
inline constexpr char kLife1[]                = "twilightforest:charm_of_life_1";
inline constexpr char kLife2[]                = "twilightforest:charm_of_life_2";
inline constexpr char kKeeping1[]             = "twilightforest:charm_of_keeping_1";
inline constexpr char kKeeping2[]             = "twilightforest:charm_of_keeping_2";
inline constexpr char kKeeping3[]             = "twilightforest:charm_of_keeping_3";
inline constexpr char kCasket[]               = "twilightforest:keepsake_casket";
inline constexpr char kSkullChest[]           = "twilightforest:skull_chest";
inline constexpr char kBrittleFlask[]         = "twilightforest:brittle_potion_flask";
inline constexpr char kGreaterFlask[]         = "twilightforest:greater_potion_flask";
inline constexpr char kOreMeter[]             = "twilightforest:ore_meter";
inline constexpr char kMoonDial[]             = "twilightforest:moon_dial";
inline constexpr char kPocketWatch[]          = "twilightforest:pocket_watch";
inline constexpr char kEmperorsCloth[]        = "twilightforest:emperors_cloth";
inline constexpr char kMysticCrown[]          = "twilightforest:mystic_crown";
inline constexpr char kCrownSplinter[]        = "twilightforest:crown_splinter";
inline constexpr char kFourLeafClover[]       = "twilightforest:four_leaf_clover";

/// Item data keys (TF's data components).
inline constexpr char kDataCasketDamage[] = "casket_damage";
inline constexpr char kDataCloth[]        = "emperors_cloth";
inline constexpr char kDataFlask[]        = "flask_contents";
inline constexpr char kDataOreData[]      = "ore_data";
inline constexpr char kDataOreFilter[]    = "ore_filter";
inline constexpr char kDataOreLoading[]   = "ore_loading";
inline constexpr char kDataOreRange[]     = "ore_range";
inline constexpr char kDataOreScanner[]   = "ore_scanner";

/// Net channels (companion protocol, rule R15).
inline constexpr char kChanShields[]   = "artifacts.shields";
inline constexpr char kChanLifedrain[] = "artifacts.lifedrain_particle";
inline constexpr char kChanCharm[]     = "artifacts.spawn_charm";
inline constexpr char kChanWipeOre[]   = "artifacts.wipe_ore_meter";
inline constexpr char kChanConfig[]    = "artifacts.config";

// ---- config (section "artifacts", docs/spec/config.md) ---------------------------------------------------

struct ArtifactsConfig {
    int version = 1;
    /// common.casketUUIDLocking
    bool casketUUIDLocking = false;
    /// client.fortificationShieldIndicator
    bool fortificationShieldIndicator = true;
    /// client.fortificationShieldIndicatorInCreative
    bool fortificationShieldIndicatorInCreative = false;
    /// client.prettifyOreMeterGui
    bool prettifyOreMeterGui = true;
    /// client.totemCharmAnimation
    bool totemCharmAnimation = false;
};
[[nodiscard]] ArtifactsConfig const& config();
/// Selftests: replaces the values until restoreConfig.
void overrideConfig(ArtifactsConfig const& values);
void restoreConfig();

// ---- helpers (Common.cpp) ------------------------------------------------------------------------------

[[nodiscard]] std::int64_t uidOf(Actor const& actor);
[[nodiscard]] Actor*       actorById(std::int64_t uid);
[[nodiscard]] Mob*         asMob(Actor& actor);
[[nodiscard]] bool         isType(Actor const& actor, char const* id);
[[nodiscard]] int          dimOf(Actor const& actor);
[[nodiscard]] bool         isLiving(Actor const& actor);
[[nodiscard]] std::vector<Player*> playersIn(int dim);
[[nodiscard]] std::vector<Actor*>  actorsIn(BlockSource& region, int dim, AABB const& box);
[[nodiscard]] Vec3   viewVector(Actor const& actor);
void                 setMotion(Actor& actor, Vec3 const& v);
[[nodiscard]] float  bbWidth(Actor const& actor);
[[nodiscard]] float  bbHeight(Actor const& actor);
/// Spawns a mob `id` (optionally "id<event>"); nullptr on failure.
Mob* spawnMob(BlockSource& region, std::string const& id, Vec3 const& pos, float yaw = 0.0f);
/// The actor's java.util.Random (created on first use; seeded by tests).
[[nodiscard]] Random& randomOf(Actor const& actor);
void                  seedRandom(Actor const& actor, std::int64_t seed);
void                  pruneRandoms();
/// The level's random (Level#getRandom), reseeded by tests.
[[nodiscard]] Random& levelRandom();
void                  seedLevelRandom(std::int64_t seed);
void playSound(Actor& at, std::string const& event, float volume, float pitch);
void playSoundAt(Actor& in, Vec3 const& pos, std::string const& event, float volume, float pitch);
void particle(Actor& in, std::string const& id, Vec3 const& pos, Vec3 const& velocity = {}, Json const& params = {});
/// The action bar message of a player (Player#displayClientMessage(..., true)).
void actionBar(Player& player, std::string const& text);
/// Java's Player#getAbilities().instabuild.
[[nodiscard]] bool creative(Player const& player);
/// Whether the actor wears the mystic crown on its head.
[[nodiscard]] bool wearsCrown(Actor const& actor);
/// The main / off hand stack of a player or mob.
[[nodiscard]] ItemStack const& mainHand(Actor const& actor);
[[nodiscard]] ItemStack const& offHand(Actor const& actor);
/// LivingEntity#isHolding(item).
[[nodiscard]] bool holding(Actor const& actor, std::string const& itemId);
/// The server tick (Level#getGameTime).
[[nodiscard]] std::int64_t gameTime();
/// The actor's tickCount equivalent (ticks since this session saw it).
[[nodiscard]] int tickCountOf(Actor const& actor);

// ---- scepters (Scepters.cpp) ---------------------------------------------------------------------------

/// The scepter's remaining charges (max damage - damage).
[[nodiscard]] int charges(ItemStack const& stack);
/// TFItemStackUtils#hurtButDontBreak for scepters (never past the max damage).
void hurtButDontBreak(ItemStack& stack, int amount);
/// RechargeScepterEffect#applyRecharge (renewal).
void applyRecharge(ItemStack& scepter, Player& player);
/// LifedrainScepterItem#getPlayerLookTarget (20 blocks).
[[nodiscard]] Actor* lookTarget(Actor& user, double range = 20.0);
/// One lifedrain use tick (`count` = remaining use ticks).
void lifedrainTick(Player& player, ItemStack& stack, int count);
/// The zombie scepter's summon: the loyal zombie, or nullptr.
Mob* summonLoyalZombie(Player& player, ItemStack& stack);
/// Fires a wand bolt from `shooter` (TwilightWandBolt(level, thrower)); nullptr on failure.
Actor* shootWandBolt(Actor& shooter, float inaccuracy = 1.0f);
void   registerScepters();

// ---- wand bolt (WandBolt.cpp) / loyal zombie (LoyalZombie.cpp) ------------------------------------------

void registerWandBolt();
/// TwilightWandBolt#hurt: re-shot along `by`'s look, the direct living entity its new owner.
void reflectWandBolt(Actor& bolt, Actor& by, Actor* direct);
[[nodiscard]] Actor* boltOwner(Actor const& bolt);
void                 setBoltOwner(Actor const& bolt, Actor* owner);
void registerLoyalZombie();
/// LoyalZombie: tame `zombie` for `owner` (setTame + setOwnerUUID).
void tameLoyalZombie(Mob& zombie, Player& owner);
void setLoyalZombieBaby(Mob& zombie, bool baby);
[[nodiscard]] bool loyalZombieBaby(Actor& zombie);

// ---- shields (Shields.cpp) -----------------------------------------------------------------------------

[[nodiscard]] core::artifacts::Shields shieldsOf(Actor& actor);
void setShields(Actor& actor, int amount, bool temp);
void addShields(Actor& actor, int amount, bool temp);
/// Breaks one shield (FortificationShieldAttachment#breakShield).
void breakShield(Actor& actor, bool expired);
void registerShields();
/// The shields' tick and join listeners (removed by the module's uninstall).
void installShieldListeners(std::vector<ll::event::ListenerPtr>& out);

// ---- charms and the casket (Charms.cpp, Casket.cpp) -------------------------------------------------------

/// CharmEvents#handleCharmOfLife: true when a charm saved the player.
bool handleCharmOfLife(Player& player);
/// SpawnCharmPacket: the charm animation and sound for `player`.
void spawnCharm(Player& player, std::string const& charmId, std::string const& sound);
void registerCharms();
/// Places a keepsake casket for `player` (stockKeepsakeCasket); the stacks in casket order.
bool placeCasket(Player& player, std::vector<ItemStack> const& contents, int damage);
/// The casket owner's UUID text ("" = none) and name stored at the casket.
struct CasketInfo {
    std::string ownerUuid;
    std::string ownerName;
    std::string name;
};
[[nodiscard]] std::optional<CasketInfo> casketInfo(BlockSource& region, BlockPos const& pos);
void setCasketInfo(BlockSource& region, BlockPos const& pos, CasketInfo const& info);
/// SkullChestBlockEntity#canOpen: the owner or permission level 3.
[[nodiscard]] bool canOpenCasket(Player& player, BlockSource& region, BlockPos const& pos);
void registerCasket();

// ---- flasks (Flasks.cpp) ---------------------------------------------------------------------------------

[[nodiscard]] core::artifacts::Flask flaskOf(ItemStack const& stack);
void setFlask(ItemStack& stack, core::artifacts::Flask const& flask);
/// Fills `flask` from `potion` (both stacks of the player); true when a dose went in.
bool fillFlask(Player& player, ItemStack& flask, ItemStack& potion, bool flaskInHand);
/// BrittleFlaskItem#finishUsingItem on the stack in the player's hand.
void drinkFlask(Player& player, ItemStack& stack);
/// The drink_from_flask tracking (PotionFlaskTrackingAttachment).
[[nodiscard]] core::artifacts::FlaskTracking& flaskTracking(Player& player);
void registerFlasks();

// ---- ore meter (OreMeter.cpp) / other artifacts (Misc.cpp) / recipes (Recipes.cpp) ----------------------

void tickOreMeter(ItemStack& stack, Player& player);
/// Starts a scan (OreMeterItem#beginScanning).
void beginOreScan(Player& player, ItemStack& stack);
void registerOreMeter();
/// The ore meter's swing listener (the wipe).
void installOreMeterListeners(std::vector<ll::event::ListenerPtr>& out);
/// The charms' join listener (stale kept records).
void installCharmListeners(std::vector<ll::event::ListenerPtr>& out);
/// The Bedrock natural dimension test of the moon dial.
[[nodiscard]] std::string moonPhaseKeyFor(Actor const* viewer);
void registerMisc();
/// Whether an item carries the emperor's cloth (TFDataComponents.EMPEROR_CLOTH).
[[nodiscard]] bool hasCloth(ItemStack const& stack);
void registerRecipes();
/// The scepter repair recipes (twilightforest:scepter_repair) as loaded: scepter, ingredients, durability.
struct IngredientAlt {
    std::string item; ///< Bedrock item id ("" with a tag)
    std::string tag;  ///< item tag ("" with an item)
    int         aux = -1; ///< data value required (a potion's), -1 = any
};
struct ScepterRepair {
    std::string                             id;
    std::string                             scepter;
    std::vector<std::vector<IngredientAlt>> ingredients; ///< the alternatives of each ingredient
    int                                     durability = 0;
};
[[nodiscard]] std::vector<ScepterRepair> scepterRepairs();
[[nodiscard]] bool ingredientMatches(std::vector<IngredientAlt> const& alts, ItemStack const& stack);

/// Selftest entry points of the death keeping (Charms.cpp) and the recipes (Recipes.cpp).
namespace test {
/// handleCharmOfKeeping on the player now (what the first keeper does at a death).
void keep(Player& player);
/// stockKeepsakeCasket on the drops (the non-empty slots in F10's order).
void stock(Player& player, std::vector<ItemStack>& drops);
/// returnStoredItems.
void returnStored(Player& player);
/// The kept record of the player (null: none).
[[nodiscard]] Json kept(Player& player);
/// A special recipe's match and result for the stacks of a grid ("scepter", "essence", "casket",
/// "cloth"; `scepterRecipe` the data recipe id for "scepter").
[[nodiscard]] std::optional<ItemStack> craft(std::string const& kind, std::vector<ItemStack const*> const& grid, std::string const& scepterRecipe = {});
} // namespace test

} // namespace gf::feature::artifacts


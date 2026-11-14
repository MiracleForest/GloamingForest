#pragma once

// Contract extension of F8 (rule R18: new files only, folded into the W0 headers by A8 in W2b).
// What the entity / projectile / boss contracts lack for upstream's bosses and mobs:
//
//   gf::entity::ext     TFPart hits (Java's attackEntityFromPart), part management, the
//                       NoClipMoveControl wanted position, the flying speed of FlyingMoveControl,
//                       ITFCharger / IBreathAttacker state and the breath attack, IHostileMount
//                       (marker, riders) / YetiThrowAttachment state, the string_list data
//                       serializer (Hydra#HEAD_NAMES), Entity#canUsePortal of an actor type.
//   gf::projectile::ext the owner and velocity of a simulated projectile, TFArrow / AbstractArrow
//                       (default hit damage, crit, pickup, water inertia, doPostHurtEffects),
//                       TFThrowable#makeTrail.
//   gf::boss::ext       ServerTFBossBar#updateStyle (the Lich's colour changes), the boss bar
//                       flags, the death time, BaseTFBoss's overridable lifecycle steps (loot,
//                       spawner, postmortem, death animation, death container, postRemoval), the
//                       home radius of spawned bosses, the 27-slot loot buffer, the "bosses" config
//                       values the multiplayer loot functions read, the alpha yeti trophy's rare
//                       roar and the trophies' particle options, and the sinister spawner block
//                       entity (SinisterSpawnerLogic).
// The client mod's readers of what F8 syncs are in feature/ext/entity/EntityClientExt.h.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/jrand/JRand.h"
#include "feature/entity/Entity.h"
#include "feature/projectile/Projectile.h"

class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class Mob;
class Player;

namespace gf::entity::ext {

/// The part id of the hit being forwarded from a part to its parent ("" when the current damage
/// did not come through a part): read it in Behaviour::hurt (Hydra heads, Naga segments).
[[nodiscard]] std::string const& hurtPart();
/// The parts of a mob, in addPart order.
[[nodiscard]] std::vector<PartHandle> parts(Mob& mob);
/// Removes one part actor (a Hydra head that died).
void removePart(PartHandle const& part);
/// TFPart#setSize: resizes the part's hitbox.
void setPartSize(PartHandle const& part, PartSize size);
/// The part's yaw / pitch (Java's part.setYRot / setXRot by the parent: Naga segments, Hydra heads),
/// applied with the next follow move; the part models' animations read it.
void setPartRotation(PartHandle const& part, float yRot, float xRot);
/// The parent of a part actor (nullptr: not a part, or the parent is gone).
[[nodiscard]] Actor* parentOf(Actor const& part);

/// MoveControl#setWantedPosition (Java's MoveControl.Operation.MOVE_TO). The controller of the mob's
/// gf::entity::setMoveControl kind steers it there: NoClip (NoClipMoveControl, also for a mob with
/// an engine navigation: gf::entity goals' moveTo then uses it too), Ground (MoveControl), Flying /
/// Hovering (FlyingMoveControl(10, hoversInPlace false / true)) and Swimming
/// (SmoothSwimmingMoveControl(85, 10, 0.02, 0.1, true)); the position stays wanted until reached.
/// A mob of the other kinds with an engine navigation is moved by the goals' moveTo through that
/// navigation and the movement component of its BP (Bedrock's own controllers of those kinds).
void setWantedPosition(Mob& mob, Vec3 const& pos, double speed);
/// The wanted position (nullopt: the controller is idle).
[[nodiscard]] std::optional<Vec3> wantedPosition(Mob const& mob);
/// Java's Attributes.FLYING_SPEED of an entity type (SnowQueen 0.23, DeathTome 0.6): the speed the
/// Flying / Hovering controllers (FlyingMoveControl) give a mob in the air (on the ground they use
/// its movement speed). Without one: the attribute's default 0.4 (Bedrock's minecraft:flying_speed
/// is another value: the air acceleration of LivingEntity#getFlyingSpeed).
void setFlyingSpeed(std::string const& entityId, double flyingSpeed);
[[nodiscard]] double flyingSpeed(Mob& mob);

/// ITFCharger#isCharging, IBreathAttacker#isBreathing (set by the charge / breath goals).
[[nodiscard]] bool isCharging(Actor const& mob);
[[nodiscard]] bool isBreathing(Actor const& mob);
/// IBreathAttacker#doBreathAttack of an entity type; without one the breath deals 2 damage of the
/// goal's damage type (upstream's fire beetle and winter wolf numbers).
using BreathAttackFn = std::function<void(Mob& breather, Actor& target)>;
void onBreathAttack(std::string const& entityId, BreathAttackFn fn);
/// Runs the breath attack of `breather` on `target` (used by goals::breath).
void doBreathAttack(Mob& breather, Actor& target, std::string const& damageType);

/// IHostileMount (the marker interface): every mob of `entityId` is a hostile mount, so whoever
/// rides it, however they got on, rides unfriendly (no dismount, crouch or teleport).
void markHostileMount(std::string const& entityId);
/// HostileMountEvents#isRidingUnfriendly: `actor` rides a hostile mount (an IHostileMount type, or
/// a mob that grabbed it with mount::grab).
[[nodiscard]] bool isRidingUnfriendly(Actor const& actor);
/// HostileMountEvents#hostileDismount: the rider may leave (the mob lets go).
void hostileDismount(Actor& rider);
/// Entity#canUsePortal of an actor type: false keeps every actor of `entityId` out of portals
/// (Bedrock's and the Gloaming portal), whatever its kind: mobs (RisingZombie), projectiles and
/// other non-mob actors (ChainBlock, CubeOfAnnihilation), falling blocks. upstream's bosses
/// (gf::boss::registerBoss, BaseTFBoss) and the parts (addPart: HydraPart, NagaSegment) never use
/// portals without it. Players are never refused. F4's Gloaming portal takes an actor that carries
/// a passenger through without asking (gf::portal's canUsePortal, reported to F4).
void setCanUsePortal(std::string const& entityId, bool canUse);

/// YetiThrowAttachment: thrown by a hostile mount and still in the air.
[[nodiscard]] bool thrown(Player const& player);
/// YetiThrowAttachment#getThrowCooldown (200 ticks after a throw).
[[nodiscard]] int throwCooldown(Player const& player);
/// The mob that threw the player (nullptr: none or gone).
[[nodiscard]] Actor* thrower(Player const& player);

/// TFDataSerializers.STRING_LIST: a synced list of strings (Hydra#HEAD_NAMES), kept as a JSON
/// array in data(actor, key) and sent to companion clients (sync); read it there with
/// gf::client::entity::stringList(uid, key).
void                                   setStringList(Actor& actor, std::string const& key, std::vector<std::string> const& values);
[[nodiscard]] std::vector<std::string> stringList(Actor& actor, std::string const& key);

} // namespace gf::entity::ext

namespace gf::projectile::ext {

/// Whether `actor` is a projectile simulated by gf::projectile.
[[nodiscard]] bool isProjectile(Actor const& actor);
/// The shooter of a projectile (nullptr: none or gone).
[[nodiscard]] Actor* owner(Actor const& projectile);
[[nodiscard]] Vec3 velocity(Actor const& projectile);
void               setVelocity(Actor& projectile, Vec3 const& velocity);

/// AbstractArrow.Pickup.
enum class Pickup { Disallowed, Allowed, CreativeOnly };
/// TFArrow (AbstractArrow): what an arrow type does on its own, before / around the Spec hooks.
struct ArrowSpec {
    double      baseDamage   = 2.0;               ///< AbstractArrow#baseDamage (SeekerArrow: 1.0)
    std::string pickupItem   = "minecraft:arrow"; ///< TFArrow#getDefaultPickupItem
    std::string hitSound     = "minecraft:entity.arrow.hit"; ///< AbstractArrow#soundEvent / getHitGroundSoundEvent
    float       gravity      = 0.05f;             ///< AbstractArrow#getDefaultGravity (Spec::gravity is not used)
    float       waterInertia = 0.6f;              ///< AbstractArrow#getWaterInertia (Spec::drag is the air's 0.99)
    /// Runs before the default hit (SeekerArrow#onHitEntity: setCritArrow(false) first).
    std::function<void(Actor& arrow, Actor& target)> beforeHitEntity;
    /// AbstractArrow#doPostHurtEffects additions of the type: run only for a hit that landed on a
    /// living target, after the effects of the fired stack (TFArrow#doPostHurtEffects ->
    /// Arrow#doPostHurtEffects: a tipped arrow's potion at 1/8 of its duration, at least 1 tick).
    std::function<void(Actor& arrow, Actor& target)> postHurt;
};
/// Registers an arrow type: AbstractArrow#tick / #onHitEntity / #onHitBlock / #playerTouch run for
/// it (ArrowSpec::gravity, Spec::drag in the air, ArrowSpec::waterInertia in water).
/// An entity hit deals ceil(speed * baseDamage) (+ the crit bonus) of "minecraft:arrow" by the
/// owner; a hit that lands removes the arrow, one that does not deflects it (ProjectileDeflection
/// REVERSE then * 0.2: velocity * -0.1, turned 170..190 degrees; gone, its item dropped when
/// pickup is Allowed, below a speed of 1e-7). A block hit sticks it (shake 7, despawns after 1200 ticks, falls again when
/// the block's state changes and nothing holds it); players touching a stuck arrow pick it up by its Pickup rule (a player owner:
/// Allowed). A player's arrow passes through players while the pvp gamerule is off (Player#canHarmPlayer). Spec::onHitEntity / onHitBlock then run as the subclass's additions (IceArrow's
/// chill); their return value is ignored for arrows.
void registerArrow(std::string const& entityId, gf::projectile::Spec spec, ArrowSpec arrow = {});
void   setBaseDamage(Actor& arrow, double damage);
[[nodiscard]] double baseDamage(Actor const& arrow);
/// AbstractArrow#setCritArrow (a fully drawn bow).
void               setCritArrow(Actor& arrow, bool crit);
[[nodiscard]] bool isCritArrow(Actor const& arrow);
void               setPickup(Actor& arrow, Pickup pickup);
[[nodiscard]] Pickup pickup(Actor const& arrow);
/// AbstractArrow#inGround.
[[nodiscard]] bool inGround(Actor const& arrow);
/// The firing weapon's knockback (EnchantmentHelper#modifyKnockback: punch) for #doKnockback.
void setKnockback(Actor& arrow, double knockback);
/// AbstractArrow#pickupItemStack: the stack the arrow was fired with (one of it comes back when it
/// is picked up; a tipped or spectral arrow from an ice or seeker bow returns as itself). Also set
/// by gf::projectile::shoot's init {"pickupItem": <item JSON (game::items::toJson)>}. Without one:
/// ArrowSpec::pickupItem.
void setPickupItem(Actor& arrow, ItemStack const& stack);
[[nodiscard]] std::optional<ItemStack> pickupItem(Actor const& arrow);
/// TFThrowable#makeTrail: `amount` particles around the projectile (0.5 * (r1 - r2) per axis), with
/// the "velocity" `rgb` (the colour of dust-like particles).
void makeTrail(Actor& projectile, std::string const& particle, int amount, Vec3 const& rgb = {});

} // namespace gf::projectile::ext

namespace gf::boss::ext {

/// BossEvent.BossBarOverlay: 0 progress, 1..4 notched 6 / 10 / 12 / 20.
enum class Overlay { Progress = 0, Notched6, Notched10, Notched12, Notched20 };
/// ServerTFBossBar#updateStyle: a new colour / overlay (allowLerp false: the client snaps).
void setBarStyle(Mob& boss, unsigned rgb, Overlay overlay = Overlay::Progress, bool allowLerp = true);
/// BossEvent#setDarkenScreen / setCreateWorldFog at runtime.
void setBarFlags(Mob& boss, bool darken, bool fog);
/// The current colour of a boss's bar (0xRRGGBB; nullopt: not a registered boss).
[[nodiscard]] std::optional<unsigned> barColor(Mob& boss);
/// The nearest of the 8 Bedrock BossBarColor values (0 pink .. 7 white) to `rgb`.
[[nodiscard]] int nearestBarColor(unsigned rgb);

/// LivingEntity#deathTime of a dying boss (0: alive).
[[nodiscard]] int deathTime(Mob const& boss);
/// Sets the death time of a dying boss and tells the companions that see it (UpdateDeathTimePacket;
/// KnightPhantom#postmortem restarts the other knights' animation at 1). No effect on a live boss.
void setDeathTime(Mob& boss, int deathTime);

/// The overridable steps of BaseTFBoss's lifecycle (rule R8: units never hook die; F8 calls these
/// from its own death, death-animation and despawn handling). Every member is optional: an empty
/// one runs BaseTFBoss's default. A boss dies like this:
///   die:       shouldSpawnLoot ? postmortem (default: defaultPostmortem) : nothing; the "hurt_boss"
///              trigger fires for the qualified players either way (EntityEvents).
///   each tick: deathTime++, tickDeath(boss, deathTime) (Hydra's heads, the Knight Phantoms'
///              hitbox); the dead body stays in the level, F8 holds Bedrock's own 20-tick removal,
///              until deathAnimationFinished(boss, deathTime) (default: deathTime >= BossSpec::
///              deathTicks, or 20).
///   finished:  postRemoval: shouldSpawnLoot && shouldDeposit ? the buffer into a deathContainer chest at
///              EntityUtil#bossChestLocation : nothing; then onPostRemoval (Lich#lightNearbyCandles);
///              then the engine removes the body (XP, death particles).
///   peaceful:  BaseTFBoss#checkDespawn: shouldCreateSpawner ? placeSpawner(home) : nothing; gone.
/// A dying boss is kept in the world DB (upstream saves it with its chunk): while its body's chunk is not
/// loaded the death waits, and it goes on after a world reload. A body that does not come back with
/// its loaded chunk finishes its death without it (the defaults, with what was known at its last
/// tick); a death chest whose chunk is not loaded is placed when the chunk loads.
struct Lifecycle {
    /// BaseTFBoss#shouldSpawnLoot (default: the doMobLoot gamerule). Lich: !isShadowClone().
    std::function<bool(Mob& boss)> shouldSpawnLoot;
    /// BaseTFBoss#shouldCreateSpawner (default: true). Lich: !isShadowClone().
    std::function<bool(Mob& boss)> shouldCreateSpawner;
    /// BaseTFBoss#postmortem, replacing the default (defaultPostmortem: bar to 0, the loot table into
    /// the buffer, the home landmark conquered). KnightPhantom: the loot to the remaining knights.
    std::function<void(Mob& boss, std::string const& damageType, Actor* killer)> postmortem;
    /// BaseTFBoss#tickDeath additions, every tick of the death animation (server side).
    std::function<void(Mob& boss, int deathTime)> tickDeath;
    /// BaseTFBoss#isDeathAnimationFinished. KnightPhantom: also no knight left alive nearby.
    std::function<bool(Mob& boss, int deathTime)> deathAnimationFinished;
    /// BaseTFBoss#postRemoval: the type's own condition for the death chest, on top of
    /// shouldSpawnLoot (both asked when the animation ends). KnightPhantom: only the last knight
    /// (IT_IS_OVER).
    std::function<bool(Mob& boss)> shouldDeposit;
    /// BaseTFBoss#getDeathContainer (default: BossSpec::deathChestBlock); `random` is the boss's.
    /// Naga / Lich: gloaming_oak_chest or canopy_chest at random.
    std::function<std::string(Mob& boss, gf::core::jrand::RandomSource& random)> deathContainer;
    /// Runs after the death chest step of postRemoval (Lich#postRemoval: lightNearbyCandles).
    std::function<void(Mob& boss)> onPostRemoval;
    /// BaseTFBoss#placeSpawner, replacing the default (placeBossSpawner). Lich: + lightNearbyCandles.
    std::function<void(Mob& boss, BlockPos const& pos)> placeSpawner;
};
/// Sets the lifecycle overrides of a boss type (registered with gf::boss::registerBoss; a second
/// call replaces the first).
void setLifecycle(std::string const& entityId, Lifecycle lifecycle);
/// BaseTFBoss#postmortem's default: the bar to 0, IBossLootBuffer#saveDropsIntoBoss with the boss's
/// loot table, LandmarkUtil#markStructureConquered of BossSpec::homeStructure at the home point.
void defaultPostmortem(Mob& boss, std::string const& damageType, Actor* killer);
/// BaseTFBoss#placeSpawner's default: the boss's spawner block at `pos`.
void placeBossSpawner(Mob& boss, BlockPos const& pos);
/// The boss's loot table rolled with its death context (TFLootTables#createLootParams; `tableId`
/// "" is BossSpec::lootTable).
[[nodiscard]] std::vector<ItemStack> rollLoot(Mob& boss, std::string const& tableId, std::string const& damageType, Actor* killer);
/// IBossLootBuffer#getAvailableSlots: the 27 slot indices shuffled (Util#shuffle).
[[nodiscard]] std::vector<int> availableSlots(gf::core::jrand::RandomSource& random);
/// LootTable#shuffleAndSplitItems.
void shuffleAndSplitItems(std::vector<ItemStack>& items, int emptySlots, gf::core::jrand::RandomSource& random);

/// EnforcedHomePoint#getHomeRadius of a boss type: the radius a boss spawner gives the boss it
/// spawns (Naga 40, Lich 30, Hydra 20, ...; 0 until set). The spawner sets the home (and
/// data(boss, "boss:spawner")) right after the spawn, before the boss's first tick: Behaviour::
/// onSpawn already sees them.
void setHomeRadius(std::string const& entityId, int radius);
[[nodiscard]] int homeRadius(std::string const& entityId);

/// IBossLootBuffer: the 27 slots that go into the death chest (empty stacks for free slots).
[[nodiscard]] std::vector<ItemStack> lootBuffer(Mob& boss);
void                                 setLootBufferSlot(Mob& boss, int slot, ItemStack const& stack);

/// TFConfig.bossDropChests and multiplayerFightAdjuster (config section "bosses").
[[nodiscard]] bool dropChests();
[[nodiscard]] bool adjustsLootRolls();
[[nodiscard]] bool adjustsHealth();

/// The rare sound of a trophy (the alpha yeti roars once in 50): {sound, 1 / chance}.
struct RareSound {
    std::string sound;
    int         oneIn = 0;
};
[[nodiscard]] std::optional<RareSound> trophyRareSound(std::string const& blockId);

/// AbstractTrophyBlock#createParticle beyond TrophyVariant::particle: how many particles one use
/// shows and the options Java creates them with, as gf::fx::particle params:
///   ur_ghast        {"color": [1, 0, 0], "scale": 1}               DustParticleOptions.REDSTONE
///   knight_phantom  {"item": "gloamingforest:knightmetal_sword"}   ItemParticleOption
///   minoshroom      {"block": <the block under the trophy>}        BlockParticleOption(pos.below());
///                   {"block": "below"} when no region / position is given
///   quest_ram       {"color": "random"}                            ColorParticleOption of 3 nextFloat()
/// The others have no options ({}). nullopt: no particle (hydra, final boss) or not a trophy.
struct TrophyParticle {
    std::string    particle;
    int            count = 0;
    nlohmann::json params = nlohmann::json::object();
};
[[nodiscard]] std::optional<TrophyParticle>
trophyParticle(std::string const& blockId, BlockSource* region = nullptr, BlockPos const* pos = nullptr);

/// The sinister spawner block entity (SinisterSpawnerBlockEntity + SinisterSpawnerLogic over
/// vanilla's BaseSpawner) of a gloamingforest:sinister_spawner block, kept in its block data
/// (custom blocks have no block entities). F8 ticks every sinister spawner near a player: while a
/// player is within requiredPlayerRange it lazily scans for safe spots (solid floor or coronation
/// carpet, air above), spawns up to nextInt(1 + spawnCount) mobs there after the delay (max
/// maxNearbyEntities of the type within entityScanRange), shows its particles and resets the delay
/// (minSpawnDelay..maxSpawnDelay). The block's own behaviour (trophy particles, drops, the
/// /gloamingforest sinister_spawner command) belongs to its block owner (U22).
struct SinisterSpawner {
    /// BaseSpawner#spawnPotentials: weighted entity ids (the next spawn is drawn on every reset).
    std::vector<std::pair<std::string, int>> potentials;
    std::string              nextEntity;               ///< BaseSpawner#nextSpawnData ("" = draw one)
    int                      spawnDelay          = 20; ///< ticks to the next spawn (-1: reset first)
    int                      minSpawnDelay       = 200;
    int                      maxSpawnDelay       = 800;
    int                      spawnCount          = 4;
    int                      maxNearbyEntities   = 6;
    int                      requiredPlayerRange = 16;
    int                      spawnRange          = 4;
    int                      entityScanRange     = -1; ///< SinisterSpawnerLogic#entityScanRange (-1: spawnRange, as #load)
    std::vector<std::string> particles;                ///< SinisterSpawnerLogic#particleOptions
    std::string              lootTable;                ///< SinisterSpawnerBlockEntity#lootTable ("" = none)
    nlohmann::json           particleParams;           ///< extra fx params per particle id (dust colour)
};
/// Writes the spawner of the block at `pos` (the block must be gloamingforest:sinister_spawner).
bool setSinisterSpawner(BlockSource& region, BlockPos const& pos, SinisterSpawner const& spawner);
/// The spawner of the block at `pos` (defaults for a sinister spawner without data; nullopt for
/// any other block).
[[nodiscard]] std::optional<SinisterSpawner> sinisterSpawner(BlockSource& region, BlockPos const& pos);
/// SinisterSpawnerLogic#setParticles / #addParticle / #removeParticle (true: changed).
bool setSinisterParticles(BlockSource& region, BlockPos const& pos, std::vector<std::string> const& particles);
bool addSinisterParticle(BlockSource& region, BlockPos const& pos, std::string const& particle);
bool removeSinisterParticle(BlockSource& region, BlockPos const& pos, std::string const& particle);
/// SinisterSpawnerBlockEntity#setLootTable ("" clears; true: changed) / #getLootTableId.
bool                      setSinisterLootTable(BlockSource& region, BlockPos const& pos, std::string const& tableId);
[[nodiscard]] std::string sinisterLootTable(BlockSource& region, BlockPos const& pos);

} // namespace gf::boss::ext

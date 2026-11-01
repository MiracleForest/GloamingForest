#pragma once

// Contract extension of F8 (rule R18: new files only, folded into the W0 headers by A8 in W2b).
// What the entity / projectile / boss contracts lack for TF's bosses and mobs:
//
//   gf::entity::ext     TFPart hits (Java's attackEntityFromPart), part management, the
//                       NoClipMoveControl wanted position, ITFCharger / IBreathAttacker state and
//                       the breath attack, IHostileMount (marker, riders) / YetiThrowAttachment
//                       state, the string_list data serializer (Hydra#HEAD_NAMES).
//   gf::projectile::ext the owner and velocity of a simulated projectile, TFArrow / AbstractArrow
//                       (default hit damage, crit, pickup, water inertia), TFThrowable#makeTrail.
//   gf::boss::ext       ServerTFBossBar#updateStyle (the Lich's colour changes), the boss bar
//                       flags, the death time, the 27-slot loot buffer, the "bosses" config values
//                       the multiplayer loot functions read, the alpha yeti trophy's rare roar, and
//                       the sinister spawner block entity (SinisterSpawnerLogic).
// The client mod's readers of what F8 syncs are in feature/ext/entity/EntityClientExt.h.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

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

/// MoveControl#setWantedPosition for the NoClip controller (Java's MoveControl.Operation.MOVE_TO).
void setWantedPosition(Mob& mob, Vec3 const& pos, double speed);
/// The wanted position (nullopt: the controller is idle).
[[nodiscard]] std::optional<Vec3> wantedPosition(Mob const& mob);

/// ITFCharger#isCharging, IBreathAttacker#isBreathing (set by the charge / breath goals).
[[nodiscard]] bool isCharging(Actor const& mob);
[[nodiscard]] bool isBreathing(Actor const& mob);
/// IBreathAttacker#doBreathAttack of an entity type; without one the breath deals 2 damage of the
/// goal's damage type (TF's fire beetle and winter wolf numbers).
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
};
/// Registers an arrow type: AbstractArrow#tick / #onHitEntity / #onHitBlock / #playerTouch run for
/// it (ArrowSpec::gravity, Spec::drag in the air, ArrowSpec::waterInertia in water).
/// An entity hit deals ceil(speed * baseDamage) (+ the crit bonus) of "minecraft:arrow" by the
/// owner; a hit that lands removes the arrow, one that does not deflects it (ProjectileDeflection
/// REVERSE then * 0.2: velocity * -0.1, turned 170..190 degrees; gone, its item dropped when
/// pickup is Allowed, below a speed of 1e-7). A block hit sticks it (shake 7, despawns after 1200 ticks, falls again when
/// the block goes); players touching a stuck arrow pick it up by its Pickup rule (a player owner:
/// Allowed). Spec::onHitEntity / onHitBlock then run as the subclass's additions (IceArrow's
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

/// The sinister spawner block entity (SinisterSpawnerBlockEntity + SinisterSpawnerLogic over
/// vanilla's BaseSpawner) of a twilightforest:sinister_spawner block, kept in its block data
/// (custom blocks have no block entities). F8 ticks every sinister spawner near a player: while a
/// player is within requiredPlayerRange it lazily scans for safe spots (solid floor or coronation
/// carpet, air above), spawns up to nextInt(1 + spawnCount) mobs there after the delay (max
/// maxNearbyEntities of the type within entityScanRange), shows its particles and resets the delay
/// (minSpawnDelay..maxSpawnDelay). The block's own behaviour (trophy particles, drops, the
/// /tf sinister_spawner command) belongs to its block owner (U22).
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
/// Writes the spawner of the block at `pos` (the block must be twilightforest:sinister_spawner).
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

#pragma once

// U21 (lich): helpers shared by the Lich, its minions, the Death Tome, the rising zombie and their
// projectiles. Small Java equivalents over the engine (line of sight, navigation, random teleport
// checks, entity searches); not a contract, only src/feature/lich uses it.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/jrand/JRand.h"

class AABB;
class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class Mob;
class Player;

namespace gf::feature::lich {

using Json   = nlohmann::json;
using Random = core::jrand::LegacyRandom;

inline constexpr char const* kLich       = "twilightforest:lich";
inline constexpr char const* kMinion     = "twilightforest:lich_minion";
inline constexpr char const* kTome       = "twilightforest:death_tome";
inline constexpr char const* kRising     = "twilightforest:rising_zombie";
inline constexpr char const* kLichBolt   = "twilightforest:lich_bolt";
inline constexpr char const* kLichBomb   = "twilightforest:lich_bomb";
inline constexpr char const* kTomeBolt   = "twilightforest:tome_bolt";
inline constexpr char const* kWandBolt   = "twilightforest:wand_bolt";
inline constexpr char const* kSpawner    = "twilightforest:lich_boss_spawner";
inline constexpr char const* kTowerKey   = "twilightforest:lich_tower";

// ---- actors ----------------------------------------------------------------------------------------

[[nodiscard]] std::int64_t uidOf(Actor const& actor);
[[nodiscard]] Actor*       actorById(std::int64_t uid);
[[nodiscard]] Mob*         mobById(std::int64_t uid);
[[nodiscard]] Mob*         asMob(Actor& actor);
[[nodiscard]] bool         isType(Actor const& actor, char const* id);
[[nodiscard]] bool         isServerActor(Actor const& actor);
[[nodiscard]] int          dimOf(Actor const& actor);
/// The mob's current attack target (alive), nullptr when none.
[[nodiscard]] Actor* targetOf(Mob& mob);
void                 setTarget(Mob& mob, Actor* target);
/// The last mob that hurt `mob` (alive).
[[nodiscard]] Actor* lastHurtBy(Mob& mob);
void                 setLastHurtBy(Mob& mob, Actor* by);

/// The actor's random source (java.util.Random), created on first use; seed it for tests.
[[nodiscard]] Random& randomOf(Actor const& actor);
void                  seedRandom(Actor const& actor, std::int64_t seed);
void                  forgetRandom(std::int64_t uid);

/// Living players of the actor's dimension within `radius` of `center` (Java's strict distance).
[[nodiscard]] std::vector<Player*> playersNear(int dim, Vec3 const& center, double radius);
/// Every living player of the dimension.
[[nodiscard]] std::vector<Player*> playersIn(int dim);
/// Level#getEntities(box) (players included).
[[nodiscard]] std::vector<Actor*> actorsIn(BlockSource& region, int dim, AABB const& box);
/// Level#getNearestPlayer(entity, range): the nearest living, non-spectator player (range < 0: any).
[[nodiscard]] Player* nearestPlayer(int dim, Vec3 const& c, double range);

// ---- geometry ----------------------------------------------------------------------------------------

[[nodiscard]] double distSqr(Vec3 const& a, Vec3 const& b);
[[nodiscard]] double distance(Actor const& a, Actor const& b); ///< Entity#distanceTo (feet)
[[nodiscard]] Vec3   eyeOf(Actor const& actor);
[[nodiscard]] float  bbHeight(Actor const& actor);
[[nodiscard]] float  bbWidth(Actor const& actor);
/// The block clip between two points (ClipContext.Block.COLLIDER / VISUAL, no fluids): true when
/// nothing blocks it.
[[nodiscard]] bool clearPath(BlockSource& region, Vec3 const& from, Vec3 const& to);
/// LivingEntity#hasLineOfSight(target): from the mob's eyes to the target's eyes, within 128.
[[nodiscard]] bool hasLineOfSight(Mob& mob, Actor& target);
/// Java's entity view vector (Entity#getViewVector).
[[nodiscard]] Vec3 viewVector(Actor const& actor);
/// LivingEntity#isLookingAtMe as TF's DeathTome / RisingZombie write it: the viewer looks within
/// `tolerance` (dot > 1 - tolerance) at one of the heights `ys` of `self` and the clip from that
/// height to the viewer's eyes is clear.
[[nodiscard]] bool isLookedAtBy(Actor& self, Player& viewer, double tolerance, std::vector<double> const& ys);

// ---- mobs ------------------------------------------------------------------------------------------

/// PathNavigation#moveTo through the engine navigation (false: no navigation or no path).
bool               moveTo(Mob& mob, Vec3 const& pos, double speed);
void               stopNavigation(Mob& mob);
[[nodiscard]] bool navigationDone(Mob& mob);
/// LookControl#setLookAt.
void lookAt(Mob& mob, Actor& target, float yMax, float xMax);
void lookAt(Mob& mob, Vec3 const& pos, float yMax, float xMax);
/// Mob#isWithinMeleeAttackRange.
[[nodiscard]] bool withinMeleeRange(Mob& mob, Actor& target);
bool               doHurtTarget(Mob& mob, Actor& target);
void               swing(Mob& mob);
void               setMainHand(Mob& mob, std::string const& itemId); ///< "" empties the hand
[[nodiscard]] std::string mainHandId(Mob& mob);
/// LivingEntity#heal (Bedrock health is whole: rounded, at least 1 when > 0).
void heal(Mob& mob, float amount);
[[nodiscard]] float health(Actor const& actor);
[[nodiscard]] float maxHealth(Actor const& actor);
/// Entity#teleportTo (no checks).
void teleportTo(Actor& actor, Vec3 const& pos);
/// Sets the motion (Entity#setDeltaMovement).
void setMotion(Actor& actor, Vec3 const& v);
/// Spawns a mob of `id` (optionally "id<event>") at `pos`; nullptr on failure.
Mob* spawnMob(BlockSource& region, std::string const& id, Vec3 const& pos, float yaw = 0.0f);
/// Removes an actor (Entity#discard).
void discard(Actor& actor);
/// Removes an actor on the next server tick (an actor removing itself during its own tick).
void discardLater(Actor& actor);
/// Drops the random sources of actors that are gone.
void pruneRandoms();
/// Drops every actor's random (a world closed).
void forgetAllRandoms();
/// The server thread (the level tick notes it): the lich tower's layout reads the loaded terrain
/// only there.
void               noteServerThread();
[[nodiscard]] bool onServerThread();

/// LivingEntity#randomTeleport's checks without moving: the spot (x, y, z) dropped onto the first
/// block below that blocks motion (not below the world), clear of collisions and liquids for an
/// entity of `width` x `height`. Returns the landing position or nullopt.
[[nodiscard]] std::optional<Vec3> randomTeleportSpot(BlockSource& region, Vec3 const& at, float width, float height);

// ---- effects ---------------------------------------------------------------------------------------

/// Entity#playSound at the actor (the entity's sound source).
void playSound(Actor& actor, std::string const& event, float volume, float pitch);
void playSoundAt(Actor& at, Vec3 const& pos, std::string const& event, float volume, float pitch);
/// One particle with velocity; `params` as gf::fx::particle.
void particle(Actor& in, std::string const& id, Vec3 const& pos, Vec3 const& velocity = {}, Json const& params = {});
/// The voice pitch of a mob (LivingEntity#getVoicePitch: babies 1.5 + ..., else (r1 - r2) * 0.2 + 1).
[[nodiscard]] float voicePitch(Actor& actor);

/// Damage type checks.
[[nodiscard]] bool bypassesInvulnerability(std::string const& damageType);
[[nodiscard]] bool damageTypeIn(std::string const& tag, std::string const& damageType);
[[nodiscard]] bool entityTypeIn(std::string const& tag, std::string const& entityId);

} // namespace gf::feature::lich

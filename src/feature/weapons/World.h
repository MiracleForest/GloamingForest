// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U8 (weapons): the Java-side world helpers the weapons need (Entity#is,
// Level#getEntities(box), Entity#push, Mob#hasLineOfSight, BlockStateBase#isSolid, the mobGriefing
// gamerule, ...), written on the game's own API so the unit uses only F8's contract headers
// (Entity.h, EntityExt.h) and none of F8's internals. Server thread only.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

#include "core/jrand/JRand.h"

class AABB;
class Actor;
class BlockPos;
class BlockSource;
class Mob;
class Player;

namespace gf::feature::weapons::world {

/// java.util.Random (Minecraft's LegacyRandomSource).
using JavaRandom = core::jrand::LegacyRandom;

/// A fresh seed (std::random_device).
[[nodiscard]] std::int64_t freshSeed();
/// The unit's shared java.util.Random (Level#getRandom stand-in; server thread).
[[nodiscard]] JavaRandom& random();
/// Java's `r.next() - r.next()` with its left-to-right order (C++ leaves the operands of `-`
/// unsequenced).
inline float  diffF(JavaRandom& r) { float a = r.nextFloat(); return a - r.nextFloat(); }
inline double diffD(JavaRandom& r) { double a = r.nextDouble(); return a - r.nextDouble(); }
inline int    diffI(JavaRandom& r, int n) { int a = r.nextInt(n); return a - r.nextInt(n); }
/// Reseeds it (deterministic selftests).
void seedRandom(std::int64_t seed);

/// ActorUniqueID of an actor.
[[nodiscard]] std::int64_t uidOf(Actor const& actor);
/// The actor / player with ActorUniqueID `uid` in the server level (nullptr: gone or not loaded).
[[nodiscard]] Actor*  actorById(std::int64_t uid);
[[nodiscard]] Player* playerById(std::int64_t uid);
/// A Mob view of `actor` (nullptr when it is no mob).
[[nodiscard]] Mob* asMob(Actor& actor);
/// The server tick counter.
[[nodiscard]] std::uint64_t currentTick();

/// DamageSource#getEntity and #getDirectEntity from a gf::combat::DamageInfo's attacker and direct
/// pair. F10 (src/feature/combat/Combat.cpp, entitiesOf) reads ActorDamageByChildActorSource with
/// getEntityUniqueID as the direct entity and getDamagingEntityUniqueID as the attacker, which for a
/// projectile shot by a mob gives attacker = projectile, direct = shooter: when the attacker is no
/// mob and the direct entity is one, the two are swapped back. A shooterless projectile stays the
/// causing entity, as in Java 1.21.1 (AbstractArrow#onHitEntity: arrow(this, owner ?: this)).
/// The one place both the hydra and its mortar read the pair (routed to F10 for a fix).
struct CausingDirect {
    Actor* causing = nullptr;
    Actor* direct  = nullptr;
};
[[nodiscard]] CausingDirect causingAndDirect(Actor* attacker, Actor* direct);

/// Damage type tags of Minecraft 1.21.1 that Bedrock has no data for: vanilla's own members plus
/// the tag data (TF's members and the units' fragments). Unit-local until F10 / A8 expose vanilla
/// damage-type tag membership (routed).
[[nodiscard]] bool isFireDamage(std::string const& type);
[[nodiscard]] bool isProjectileDamage(std::string const& type);

/// The parent of a part actor (0: not a part, or the parent is gone): F8's ext::parentOf.
[[nodiscard]] std::int64_t parentOfPart(Actor const& part);

/// Sets the actor's motion (players get a SetActorMotionPacket: their client moves them).
void setMotion(Actor& actor, Vec3 const& motion);
/// Entity#push: adds to the motion.
void push(Actor& actor, Vec3 const& delta);

/// The alive players of dimension `dim` closer than `radius` to `center`.
[[nodiscard]] std::vector<Player*> playersNear(int dim, Vec3 const& center, double radius);
/// Level#getEntities(box): the actors whose box intersects `box`, players included.
[[nodiscard]] std::vector<Actor*> actorsIn(BlockSource& region, int dim, AABB const& box);

/// Entity#distanceToSqr between the feet positions.
[[nodiscard]] double distanceSqr(Actor const& a, Actor const& b);
[[nodiscard]] double distanceSqr(Actor const& a, Vec3 const& p);
/// LivingEntity#hasLineOfSight (128 blocks, a COLLIDER clip eye to eye).
[[nodiscard]] bool hasLineOfSight(Mob& mob, Actor& target);
/// BlockStateBase#isSolid (legacy solid: a collision box of average size >= 0.729 or 1 high).
[[nodiscard]] bool legacySolid(BlockSource& region, BlockPos const& pos);
/// The mobGriefing gamerule (EventHooks#canEntityGrief).
[[nodiscard]] bool mobGriefing();
/// The level's difficulty is HARD.
[[nodiscard]] bool hard();

/// Runs `fn` on every server tick (after the actors ticked); the returned id removes it.
int  onLevelTick(std::function<void()> fn);
void removeLevelTick(int id);
/// Spawns entity `entityId` at `pos` moving along `direction` (Projectile#shoot's spread, no
/// shooter: a dispenser's projectile). nullptr on failure.
Actor* shootFrom(BlockSource& region, Vec3 const& pos, std::string const& entityId, Vec3 const& direction, float speed, float inaccuracy);
/// Removes the actor `uid` on the next tick (never inside its own tick).
void removeLater(std::int64_t uid);

} // namespace gf::feature::weapons::world

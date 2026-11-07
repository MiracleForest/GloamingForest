// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U24 (hydra): the Java-side world helpers the Hydra, its mortar and the jets need (Entity#is,
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

namespace gf::feature::hydra::world {

/// java.util.Random (Minecraft's LegacyRandomSource).
using JavaRandom = core::jrand::LegacyRandom;

/// A fresh seed (std::random_device).
[[nodiscard]] std::int64_t freshSeed();

/// ActorUniqueID of an actor.
[[nodiscard]] std::int64_t uidOf(Actor const& actor);
/// The actor / player with ActorUniqueID `uid` in the server level (nullptr: gone or not loaded).
[[nodiscard]] Actor*  actorById(std::int64_t uid);
[[nodiscard]] Player* playerById(std::int64_t uid);
/// A Mob view of `actor` (nullptr when it is no mob).
[[nodiscard]] Mob* asMob(Actor& actor);
/// The server tick counter.
[[nodiscard]] std::uint64_t currentTick();

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
/// Removes the actor `uid` on the next tick (never inside its own tick).
void removeLater(std::int64_t uid);

} // namespace gf::feature::hydra::world

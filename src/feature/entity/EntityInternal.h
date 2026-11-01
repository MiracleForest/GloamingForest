#pragma once

// Internals of F8 (contracts "entity", "projectile", "boss"): the per-actor state the hooks keep,
// helpers shared by the three implementations and the seams their selftests use. Not a contract:
// other units include only feature/{entity,projectile,boss}/*.h and feature/ext/entity/*.h.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "feature/entity/Entity.h"

class Actor;
class BlockSource;
class Mob;
class Player;

namespace gf::feature::entity {

using Json = nlohmann::json;

/// java.util.Random (Minecraft's LegacyRandomSource): the random source of a mob's goals, so the
/// goal code consumes random numbers the way TF does (seedable for deterministic selftests).
class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed = 0) { setSeed(seed); }
    void         setSeed(std::int64_t seed);
    int          next(int bits);
    int          nextInt();
    int          nextInt(int bound);
    std::int64_t nextLong();
    bool         nextBoolean();
    float        nextFloat();
    double       nextDouble();
    double       nextGaussian();
    /// RandomSource#triangle.
    double triangle(double mode, double spread) { return mode + spread * (nextDouble() - nextDouble()); }

private:
    std::int64_t mSeed         = 0;
    double       mNextGaussian = 0;
    bool         mHaveGaussian = false;
};

/// A fresh seed (std::random_device), used for actors that were not seeded by a test.
[[nodiscard]] std::int64_t freshSeed();

// ---- actors ----------------------------------------------------------------------------------

[[nodiscard]] std::int64_t uidOf(Actor const& actor);
/// The actor with ActorUniqueID `uid` in the server level (nullptr: gone or not loaded).
[[nodiscard]] Actor* actorById(std::int64_t uid);
[[nodiscard]] Player* playerById(std::int64_t uid);
/// A Mob view of `actor` (nullptr when it is not a mob).
[[nodiscard]] Mob* asMob(Actor& actor);
/// The server tick counter (Level::getCurrentServerTick).
[[nodiscard]] std::uint64_t currentTick();
/// True for actors of the server level (the client build also ticks its own client actors).
[[nodiscard]] bool isServerActor(Actor const& actor);
/// Sets the actor's motion; players get a SetActorMotionPacket (their client moves them).
void setMotion(Actor& actor, Vec3 const& motion);
/// Java's Entity#push: adds to the motion.
void push(Actor& actor, Vec3 const& delta);
/// The players of dimension `dim` within `radius` of `center` (alive ones).
[[nodiscard]] std::vector<Player*> playersNear(int dim, Vec3 const& center, double radius);
/// Java's damage type id of a Bedrock damage source ("minecraft:player_attack", "minecraft:fall", ...).
[[nodiscard]] std::string damageTypeOf(int cause, bool byPlayer);
/// The actor's eye position (Entity#getEyePosition).
[[nodiscard]] Vec3 eyeOf(Actor const& actor);

// ---- per-actor state -------------------------------------------------------------------------

class GoalSelector;
struct PartRecord {
    std::string                      partId;
    std::int64_t                     actorId = 0; ///< the part actor
    gf::entity::PartSize             size;
    std::function<Vec3(Mob& parent)> offset;
    int                              index = 0;
};

/// Everything F8 keeps about one actor while it is loaded (keyed by ActorUniqueID, rule R16).
struct ActorState {
    std::string                   typeId;
    bool                          spawned = false;     ///< onSpawn ran this session
    bool                          loaded  = false;     ///< read from a save
    Json                          data    = Json::object(); ///< data(actor, key) documents (persisted)
    std::unique_ptr<JavaRandom>   random;
    std::unique_ptr<GoalSelector> goals;
    std::unique_ptr<GoalSelector> targets;
    // movement
    std::optional<gf::entity::MoveKind>      moveKind;
    std::function<void(Mob&)>                moveFn;
    bool                                     hasWanted = false; ///< NoClipMoveControl's MOVE_TO
    Vec3                                     wanted{};
    double                                   wantedSpeed = 1.0;
    int                                      courseChangeCooldown = 0;
    // multipart
    std::vector<PartRecord> parts;
    std::int64_t            parentId = 0; ///< set on part actors
    std::string             partId;
    // hostile mount
    std::int64_t grabbed = 0; ///< the player this mob holds as a rider
    bool         hostileMount = false;
    // the last time anything touched the state (sweep)
    std::uint64_t lastSeen = 0;
};

/// The state of `actor` (created on demand).
[[nodiscard]] ActorState& stateOf(Actor& actor);
/// The state of the actor with id `uid` (nullptr: none).
[[nodiscard]] ActorState* findState(std::int64_t uid);
/// The goal random source of `actor`.
[[nodiscard]] JavaRandom& randomOf(Actor& actor);

/// Internal listeners of the shared hooks (boss, projectile and multipart plug in here; units use
/// gf::entity::registerType). `interesting(typeId)` makes the tick hook look at an actor type.
struct InternalHooks {
    std::function<bool(std::string const& typeId)>                                    interesting;
    std::function<void(Actor& actor, ActorState& state)>                              tick;
    std::function<void(Actor& actor, std::string const& damageType, Actor* killer)>   die;
    std::function<void(Actor& actor, ActorState& state)>                              spawn; ///< first tick of a session
    /// false: the actor is not written to the world (multipart parts).
    std::function<bool(Actor const& actor)>                                           save;
    /// Right before the state's data is written into the actor's save tag.
    std::function<void(Actor const& actor, ActorState& state)>                        persist;
};
void addInternalHooks(InternalHooks hooks);

/// Runs `fn` once on every server tick (level tick), after the actors ticked.
void onLevelTick(std::function<void()> fn);
/// Removes the actor on the next level tick (never inside an actor's own tick).
void removeLater(std::int64_t uid);

/// Whether a Behaviour is registered for `typeId`.
[[nodiscard]] bool hasBehaviour(std::string const& typeId);
/// Calls the registered Behaviour::hurt of `actor`'s type (true: the damage goes on).
bool dispatchHurt(Actor& actor, float& amount, std::string const& damageType, Actor* attacker);

// ---- goals (Goals.cpp) -----------------------------------------------------------------------

/// Java's GoalSelector.
class GoalSelector {
public:
    void add(int priority, gf::entity::GoalPtr goal);
    /// GoalSelector#tick (stop, start) followed by tickRunningGoals(true).
    void tick();
    /// GoalSelector#tickRunningGoals(force).
    void tickRunning(bool force);
    void stopAll();
    [[nodiscard]] std::size_t size() const { return mGoals.size(); }
    [[nodiscard]] bool        running(gf::entity::Goal const* goal) const;

private:
    struct Wrapped {
        int                  priority = 0;
        gf::entity::GoalPtr  goal;
        bool                 isRunning = false;
    };
    std::vector<Wrapped> mGoals;
    int                  mLocked[4]{-1, -1, -1, -1}; ///< the goal holding Move, Look, Jump, Target
};

/// Runs the goal selectors and the movement controller of a mob (from the tick hook).
void tickGoals(Mob& mob, ActorState& state);

// Mob helpers the goals use (also handy for the boss kit).
namespace mobs {
[[nodiscard]] Actor* target(Mob& mob);
[[nodiscard]] Actor* lastHurtByMob(Mob& mob);
[[nodiscard]] bool   hasLineOfSight(Mob& mob, Actor& target);
[[nodiscard]] double distanceSqr(Actor const& a, Actor const& b);
[[nodiscard]] double distanceSqr(Actor const& a, Vec3 const& p);
/// PathNavigation#moveTo(x, y, z, speed); false when the mob has no navigation or no path.
bool moveTo(Mob& mob, Vec3 const& pos, double speed);
[[nodiscard]] bool navigationDone(Mob& mob);
void               stopNavigation(Mob& mob);
void               setSpeedModifier(Mob& mob, double speed);
void               lookAt(Mob& mob, Vec3 const& pos, float yMax, float xMax);
void               lookAt(Mob& mob, Actor& target, float yMax, float xMax);
/// Mob#isWithinMeleeAttackRange (attack box inflated by sqrt(2.04) - 0.6).
[[nodiscard]] bool isWithinMeleeAttackRange(Mob& mob, Actor& target);
bool               doHurtTarget(Mob& mob, Actor& target);
/// EntitySelector.NO_CREATIVE_OR_SPECTATOR and LIVING_ENTITY_STILL_ALIVE.
[[nodiscard]] bool attackable(Actor& actor);
/// DefaultRandomPos#getPos / getPosAway / getPosTowards (nullopt: none found).
[[nodiscard]] std::optional<Vec3> randomPos(Mob& mob, int radius, int vertical);
[[nodiscard]] std::optional<Vec3> randomPosAway(Mob& mob, int radius, int vertical, Vec3 const& away);
[[nodiscard]] std::optional<Vec3> randomPosTowards(Mob& mob, int radius, int vertical, Vec3 const& towards, double maxAngle);
} // namespace mobs

// ---- multipart (Multipart.cpp) / mounts (Mount.cpp) --------------------------------------------

/// The parent actor id of a part actor (0: not a part).
[[nodiscard]] std::int64_t parentOfPart(Actor const& part);
/// Moves the parts of `parent` to their offsets (tick hook).
void followParts(Mob& parent, ActorState& state);
/// Installs the part damage / interaction routing.
void installMultipart();
void uninstallMultipart();
void installMounts();
void uninstallMounts();
/// The hostile mount tick of a mob that holds a rider.
void tickMount(Mob& mob, ActorState& state);

/// The part id of the hit being forwarded to its parent right now ("" outside such a hit).
[[nodiscard]] std::string const& currentPartHit();

} // namespace gf::feature::entity

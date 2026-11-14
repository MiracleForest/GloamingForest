#pragma once

// Contract "entity" (header owned by F0, implemented by F8; spike S4).
//
// upstream's 80 entity types are BP/RP entities (imported skeleton + F2 geometry); their behaviour is
// native and registered by id. Every hook into the engine's actors (tick, hurt, die, AI goals,
// multipart, hostile mounts) lives in F8 (rule R8); units only register:
//
//     gf::entity::registerType("gloamingforest:boar", {
//         .onSpawn = [](Actor& a) { ... },
//         .tick    = [](Actor& a) { ... },
//     });
//     gf::entity::addGoal(mob, 2, gf::entity::goals::charge(mob, {.speed = 2.0}));
//     auto head = gf::entity::addPart(hydra, "head_1", {1.5f, 1.5f}, [](Mob& m) { return Vec3{...}; });
//
// Server thread only. Actors are looked up again every tick: never keep Actor* / Player* across
// ticks outside the goal / behaviour objects the mob owns.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/GlobalPos.h"

class Actor;
class Mob;
class Player;

namespace gf::entity {

using Json = nlohmann::json;

/// The native behaviour of an entity type. Every member is optional.
struct Behaviour {
    std::function<void(Actor& self)>                                                       onSpawn;
    std::function<void(Actor& self)>                                                       tick;
    /// Java's aiStep (after the engine's movement step).
    std::function<void(Actor& self)>                                                       aiStep;
    /// Incoming damage: change `amount` or return false to cancel (runs inside gf::combat).
    std::function<bool(Actor& self, float& amount, std::string const& damageType, Actor* attacker)> hurt;
    std::function<void(Actor& self, std::string const& damageType, Actor* killer)>        die;
    /// Persisted state beyond `data(actor, key)` (Java's addAdditionalSaveData / read...).
    std::function<void(Actor& self, Json& out)>                                            save;
    std::function<void(Actor& self, Json const& in)>                                       load;
    /// A player's right-click (Java's mobInteract): true when it was used.
    std::function<bool(Actor& self, Player& player)>                                       interact;
};
void registerType(std::string const& entityId, Behaviour behaviour);

/// The actor's persisted GloamingForest document under `key` (created as null); saved with the
/// actor.
[[nodiscard]] Json& data(Actor& actor, std::string const& key);
/// Sends `data(actor, key)` to companion clients that see the actor (client renderers).
void sync(Actor& actor, std::string const& key);
/// Sets a Bedrock entity property (declared in the entity's BP JSON; drives animations and
/// render controllers on every client, vanilla included).
using PropertyValue = std::variant<int, float, bool, std::string>;
void setProperty(Actor& actor, std::string const& name, PropertyValue const& value);

/// Java's Goal.
class Goal {
public:
    enum Flag : unsigned { Move = 1, Look = 2, Jump = 4, Target = 8 };
    virtual ~Goal() = default;
    [[nodiscard]] virtual bool canUse() = 0;
    [[nodiscard]] virtual bool canContinueToUse() { return canUse(); }
    [[nodiscard]] virtual bool isInterruptable() { return true; }
    [[nodiscard]] virtual bool requiresUpdateEveryTick() { return false; }
    virtual void               start() {}
    virtual void               stop() {}
    virtual void               tick() {}
    unsigned                   flags = 0;
};
using GoalPtr = std::shared_ptr<Goal>;
/// Adds a goal to the mob's goal selector (lower priority value runs first, as Java).
void addGoal(Mob& mob, int priority, GoalPtr goal);
/// Adds a goal to the mob's target selector.
void addTargetGoal(Mob& mob, int priority, GoalPtr goal);

/// The shared goals of upstream's entity/ai package.
namespace goals {
struct ChargeSpec {
    double speed = 2.0;       ///< ChargeAttackGoal speed
    bool   canBreak = false;  ///< breaks blocks while charging (minoshroom)
};
struct BreathSpec {
    float  range = 5.0f;
    int    durationTicks = 40;
    float  chance = 0.07f;
    std::string damageType = "minecraft:on_fire";
};
[[nodiscard]] GoalPtr charge(Mob& mob, ChargeSpec spec);                   ///< ITFCharger / ChargeAttackGoal
[[nodiscard]] GoalPtr breath(Mob& mob, BreathSpec spec);                   ///< IBreathAttacker / BreathAttackGoal
[[nodiscard]] GoalPtr flockToSameKind(Mob& mob, double speed);            ///< FlockToSameKindGoal
[[nodiscard]] GoalPtr panicOnFlockDeath(Mob& mob, double speed);          ///< PanicOnFlockDeathGoal
[[nodiscard]] GoalPtr avoidAny(Mob& mob, std::function<bool(Actor&)> avoid, float distance, double walkSpeed, double sprintSpeed); ///< AvoidAnyEntityGoal
[[nodiscard]] GoalPtr goHome(Mob& mob, double speed);                     ///< AttemptToGoHomeGoal
[[nodiscard]] GoalPtr alwaysWatchTarget(Mob& mob);                        ///< AlwaysWatchTargetGoal
[[nodiscard]] GoalPtr randomLookIfBored(Mob& mob);                        ///< (upstream's random look when idle)
[[nodiscard]] GoalPtr simplifiedAttack(Mob& mob);                         ///< SimplifiedAttackGoal
} // namespace goals

/// Movement controllers (Java's MoveControl subclasses).
enum class MoveKind { Ground, Flying, Swimming, NoClip, Hovering };
void setMoveControl(Mob& mob, MoveKind kind);
/// A custom controller run every tick instead of the engine's.
void setMoveControl(Mob& mob, std::function<void(Mob& mob)> control);

/// EnforcedHomePoint: the mob returns to `home` when farther than `radius`.
void setHome(Mob& mob, core::GlobalPos const& home, int radius);
struct Home {
    core::GlobalPos pos;
    int             radius = 0;
};
[[nodiscard]] std::optional<Home> home(Mob const& mob);

/// A linked multipart part (TFPart): its own hitbox actor that follows the parent; damage to it
/// goes to the parent with the part id.
struct PartSize {
    float width  = 1.0f;
    float height = 1.0f;
};
struct PartHandle {
    std::int64_t parentId = 0; ///< the parent's unique id
    std::string  partId;
    [[nodiscard]] bool valid() const { return parentId != 0 && !partId.empty(); }
};
/// `offset` returns the part's position relative to the parent every tick.
PartHandle addPart(Mob& mob, std::string const& partId, PartSize size, std::function<Vec3(Mob& parent)> offset);
/// The part actor of a handle (nullptr: gone).
[[nodiscard]] Actor* partActor(PartHandle const& part);

/// IHostileMount.
namespace mount {
/// The mob grabs the player as a rider (alpha yeti, pinch beetle).
bool grab(Mob& mob, Player& player);
/// Throws the current rider with `velocity`.
void throwRider(Mob& mob, Vec3 const& velocity);
} // namespace mount

/// Refuses targets: `fn(attacker, candidate)` false = the attacker can't target it (stealth).
void addTargetFilter(std::function<bool(Mob& attacker, Actor& candidate)> fn);

/// Seeds the actor's random source (deterministic selftests, rule R12).
void seedForTest(Actor& actor, std::int64_t seed);

} // namespace gf::entity

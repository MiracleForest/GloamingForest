// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U25 (stronghold): the Knight Phantoms and their thrown weapons (Phantom.cpp). Unit-internal: the
// module and the selftests use it.

#include <optional>

#include "nlohmann/json.hpp"

class Actor;
class Mob;

namespace gf::feature::stronghold::phantom {

using Json = nlohmann::json;

/// KnightPhantom.Formation (ordinal order).
enum class Formation {
    Hover,
    LargeClockwise,
    SmallClockwise,
    LargeAnticlockwise,
    SmallAnticlockwise,
    ChargePlusX,
    ChargeMinusX,
    ChargePlusZ,
    ChargeMinusZ,
    WaitingForLeader,
    AttackPlayerStart,
    AttackPlayerAttack
};

/// KnightPhantom#doHurtTarget (the haunt damage of the attack damage).
bool doHurtTarget(Mob& knight, Actor& target);
/// PhantomThrowWeaponGoal#launchAxeAt / #launchPicks.
void launchAxeAt(Mob& knight, Actor& target);
void launchPicks(Mob& knight);

[[nodiscard]] std::optional<Formation> formationOf(Actor const& knight);
[[nodiscard]] int                      numberOf(Actor const& knight);
[[nodiscard]] bool                     isCharging(Actor const& knight);
[[nodiscard]] bool                     isOver(Actor const& knight);
[[nodiscard]] int                      ticksProgressOf(Actor const& knight);
void forceFormationForTest(Mob& knight, Formation formation, int ticksProgress);
void setNumberForTest(Mob& knight, int number);

void install();

} // namespace gf::feature::stronghold::phantom

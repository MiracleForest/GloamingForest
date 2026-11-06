// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U25 (stronghold): the goblin knights, the block-and-chain goblin and the helmet crab
// (Goblins.cpp). Unit-internal: the module and the selftests use it.

class Actor;
class Mob;

namespace gf::feature::stronghold::goblins {

/// UpperGoblinKnight#takeHitOnShield (true: the hit was blocked).
bool takeHitOnShield(Mob& upper, Actor* attacker, float amount);
/// UpperGoblinKnight#landHeavySpearAttack.
void landHeavySpearAttack(Mob& upper);
[[nodiscard]] int  spearTimer(Actor const& upper);
/// The armour flag of a lower or upper goblin knight.
[[nodiscard]] bool hasArmor(Actor const& goblin);
[[nodiscard]] bool hasShield(Actor const& upper);

[[nodiscard]] float chainLengthOf(Actor& goblin);
[[nodiscard]] float throwLengthOf(Actor& goblin);
[[nodiscard]] bool  isThrowing(Actor& goblin);
void                startThrowForTest(Mob& goblin);

[[nodiscard]] bool isBlue(Actor const& crab);
void               setBlueForTest(Actor& crab, bool blue);

void install();

} // namespace gf::feature::stronghold::goblins

#pragma once

// U11 extension (rule R18: new files only; to be folded into a W0 contract by A8 in W2b).
//
// Two player events TF's Travellers' Gear needs and no W0 contract offers:
//
// - armour damage (NeoForge's ArmorHurtEvent): ServerPlayer::hurtArmorSlots is wrapped; listeners get
//   the player and its four armour stacks from before the damage, after the game applied it. U11 uses
//   it for TF's never-break rule of Travellers' Gear (a piece that would break stays at max damage - 1)
//   and the last armour damage time of auto repair. A listener may set the armour again.
// - movement exhaustion (TF's PlayerHooks ASM in Player#causeFoodExhaustion during
//   checkMovementStatistics): ServerPlayer::checkMovementStats is wrapped; the exhaustion it adds is
//   divided by the factor the registered function returns (1: unchanged). U11's efficient eater
//   returns its component value (2) while the modifier is active.
//
//     gf::ext::tgear::onArmorHurt([](Player& p, std::array<ItemStack, 4> const& before) { ... });
//     gf::ext::tgear::setMovementExhaustionDivisor([](Player& p) { return 2.f; });
//
// Server thread. Both hooks are installed by the U11 module (feature/tgear) and removed with it.

#include <array>
#include <functional>

class ItemStack;
class Player;

namespace gf::ext::tgear {

using ArmorHurtFn = std::function<void(Player& player, std::array<ItemStack, 4> const& before)>;
void onArmorHurt(ArmorHurtFn fn);

using ExhaustionDivisorFn = std::function<float(Player& player)>;
void setMovementExhaustionDivisor(ExhaustionDivisorFn fn);

/// The player's food exhaustion (the EXHAUSTION attribute; selftests).
[[nodiscard]] float exhaustion(Player const& player);
void                setExhaustion(Player& player, float value);

void installHooks();
void uninstallHooks();

} // namespace gf::ext::tgear

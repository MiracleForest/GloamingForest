#pragma once

// U11 extension (rule R18: new files only; to be folded into a W0 contract by A8 in W2b).
//
// Armour damage (NeoForge's ArmorHurtEvent), which no W0 contract offers: ServerPlayer::hurtArmorSlots is
// wrapped; listeners get the player and its four armour stacks from before the damage, after the game
// applied it, and may set the armour again. U11 uses it for TF's never-break rule of Travellers' Gear (a
// piece that would break stays at max damage - 1) and the last armour damage time of auto repair.
//
//     gf::ext::tgear::onArmorHurt([](Player& p, std::array<ItemStack, 4> const& before) { ... });
//
// Server thread. The hook is installed by the U11 module (feature/tgear) and removed with it.

#include <array>
#include <functional>

class ItemStack;
class Player;

namespace gf::ext::tgear {

using ArmorHurtFn = std::function<void(Player& player, std::array<ItemStack, 4> const& before)>;
void onArmorHurt(ArmorHurtFn fn);

void installHooks();
void uninstallHooks();

} // namespace gf::ext::tgear

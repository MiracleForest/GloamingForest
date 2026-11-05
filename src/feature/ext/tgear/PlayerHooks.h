#pragma once

// U11 extension (rule R18: new files only; to be folded into a W0 contract by A8 in W2b).
//
// Armour damage (NeoForge's ArmorHurtEvent), which no W0 contract offers: Mob::hurtArmorSlots and its
// ServerPlayer override are wrapped (the outermost call of a wearer only). Listeners get:
//   - before the game applies it, the wearer, the damage and the armour slots to hurt (bits 0 head ..
//     3 feet, 4 body), and may take slots out of the set to damage those pieces themselves (TF's
//     stopDamagingTravellersGear changes the new damage of a slot, which Bedrock's hurtArmorSlots has
//     no parameter for);
//   - after it, the wearer and its four armour stacks from before the damage.
// U11 uses it for TF's never-break rule of Travellers' Gear and the last armour damage time of auto
// repair.
//
//     gf::ext::tgear::onArmorHurtPre([](Mob& m, int damage, std::bitset<5>& slots) { ... });
//     gf::ext::tgear::onArmorHurt([](Mob& m, std::array<ItemStack, 4> const& before) { ... });
//
// Server thread. The hooks are installed by the U11 module (feature/tgear) and removed with it.

#include <array>
#include <bitset>
#include <functional>

class ItemStack;
class Mob;

namespace gf::ext::tgear {

using ArmorHurtPreFn = std::function<void(Mob& wearer, int damage, std::bitset<5>& slots)>;
using ArmorHurtFn    = std::function<void(Mob& wearer, std::array<ItemStack, 4> const& before)>;
void onArmorHurtPre(ArmorHurtPreFn fn);
void onArmorHurt(ArmorHurtFn fn);

void installHooks();
void uninstallHooks();

} // namespace gf::ext::tgear

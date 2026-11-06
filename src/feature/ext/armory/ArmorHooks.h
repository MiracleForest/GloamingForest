#pragma once

// U7 extension (rule R18: new files only; to be folded into F9's item contract by A8 in W2b).
//
// Armour toughness (Java's ArmorMaterial#toughness), which Bedrock's data-driven items have no field
// for and no W0 contract offers: Bedrock's absorption reads Mob::getToughnessValue (the sum of the
// worn items' Item::getToughnessValue, 0 for data items). The hook adds what a listener gives for the
// four worn stacks (Java's float sum, rounded to Bedrock's integer total).
//
//     gf::ext::armory::setToughness([](ItemStack const& worn) { return toughnessOf(worn); });
//
// Installed by U7's module (feature/armory), removed with it.

#include <functional>

class ItemStack;

namespace gf::ext::armory {

/// The extra (Java) toughness of one worn stack (0 when none).
using ToughnessFn = std::function<float(ItemStack const& worn)>;
void setToughness(ToughnessFn fn);

/// The toughness the listener adds for the four armour slots of a wearer (rounded total).
[[nodiscard]] int extraToughness(ItemStack const& head, ItemStack const& chest, ItemStack const& legs, ItemStack const& feet);

void installHooks();
void uninstallHooks();

} // namespace gf::ext::armory

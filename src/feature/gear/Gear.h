#pragma once

// Contract "data", Travellers' Gear modifiers (header owned by F0, implemented by F9).
//
// upstream's 24 travellers_modifiers (gloaming/travellers_modifiers) are stored on the gear items; this
// is the query side used by U11 (equipment modifiers) and U12 (movement modifiers).
//
//     int lvl = gf::gear::level(player, "gloamingforest:double_jump");

#include <map>
#include <string>

class ItemStack;
class Player;

namespace gf::gear {

/// The highest level of `modifierId` over the player's worn / held gear (0: none).
[[nodiscard]] int level(Player const& player, std::string const& modifierId);
/// The modifiers of one gear stack, by id with their level.
[[nodiscard]] std::map<std::string, int> modifiers(ItemStack const& stack);
/// Adds (or raises to `level`) a modifier on a gear stack; false when it does not apply.
bool add(ItemStack& stack, std::string const& modifierId, int level = 1);
bool remove(ItemStack& stack, std::string const& modifierId);

} // namespace gf::gear

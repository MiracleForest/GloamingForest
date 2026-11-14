#pragma once

// Contract "data", player traits (header owned by F0, implemented by F9).
//
// Boolean per-player flags that both sides need (upstream's data attachments of that kind: banished,
// shield visuals, giant pick in use, ...), persisted in the world DB and synced to the player's
// client mod.
//
//     gf::traits::set(player, "gloamingforest:banished", true);

#include <string>

class Player;

namespace gf::traits {

void set(Player& player, std::string const& traitId, bool value);
[[nodiscard]] bool get(Player const& player, std::string const& traitId);

} // namespace gf::traits

#pragma once

// Contract "data", statistics (header owned by F0, implemented by F9).
//
// TF's 9 custom statistics (bugs_squished, uncrafting_table_interactions,
// trophy_pedestals_activated, e115_slices_eaten, torchberries_harvested, blocks_crumbled,
// tf_shields_broken, life_charms_activated, keeping_charms_activated, skull_candles_made),
// persisted per player in the world DB.
//
//     gf::stats::award(player, "twilightforest:bugs_squished");

#include <string>

class Player;

namespace gf::stats {

void award(Player& player, std::string const& statId, int amount = 1);
[[nodiscard]] long long value(Player const& player, std::string const& statId);

} // namespace gf::stats

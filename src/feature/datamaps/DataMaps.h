#pragma once

// Contract "data", data maps (header owned by F0, implemented by F9).
//
// TF's 8 data maps (crumble_horn, transformation_powder, ore_map_color, magic_map_color,
// ominous_fire, uncrafting ingredients... plus NeoForge compostables, furnace_fuels and
// parrot_imitations), imported as data. A data map belongs to its consumer (rule R7).
//
//     if (auto const* v = gf::datamaps::get("twilightforest:crumble_horn", blockId)) ...;

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::datamaps {

/// The value of `key` (a block / item / entity id) in data map `mapId`; nullptr when absent.
/// The pointer stays valid until the data is reloaded.
[[nodiscard]] nlohmann::json const* get(std::string const& mapId, std::string const& key);
/// Every key of a data map.
[[nodiscard]] std::vector<std::string> keys(std::string const& mapId);

} // namespace gf::datamaps

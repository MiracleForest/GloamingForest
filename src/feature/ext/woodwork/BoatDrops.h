#pragma once

// Contract extension of U4 woodwork (rule R18: new files only; A8 folds it into the death / loot
// contracts). Bedrock's Boat#destroy / ChestBoat#destroy drop the vanilla boat item of the boat's
// variant on their own (no loot table, no gf::loot / gf::death event covers them). A unit whose
// data-driven entity runs on the game's boat (BP runtime identifier minecraft:boat /
// minecraft:chest_boat) registers the item its boat drops instead:
//
//     gf::woodwork_ext::boatdrops::setReplacement([](Actor& boat) -> std::optional<std::string> {
//         if (boat.getTypeName() != "twilightforest:boat") return std::nullopt;   // other boats: vanilla
//         return "twilightforest:canopy_boat";
//     });
//
// Only the boat's own drop is replaced: an item actor that appeared while the game ran that boat's
// destroy call, of exactly the item of the boat's variant (Boat::getWoodID; <wood>_chest_boat for
// a chest boat), one plain item; at most one per boat. A chest boat's contents (a vanilla boat item
// in its chest) and a boat destroyed next to it keep their items; a nested Boat::$destroy inside
// ChestBoat::$destroy does not replace twice. The replacement keeps the position, the motion and
// the pickup delay of the game's item.
// Server thread. One replacement function (a later call replaces it; an empty one removes it).

#include <functional>
#include <optional>
#include <string>

class Actor;

namespace gf::woodwork_ext::boatdrops {

using ReplacementFn = std::function<std::optional<std::string>(Actor& boat)>;
void setReplacement(ReplacementFn fn);

/// The game's own drop of a boat or chest boat actor ("minecraft:oak_boat", "minecraft:oak_chest_boat";
/// "" for an unknown wood).
[[nodiscard]] std::string vanillaDropOf(Actor& boat);

/// Installs / removes the Boat::$destroy and ChestBoat::$destroy hooks.
void install();
void uninstall();

} // namespace gf::woodwork_ext::boatdrops

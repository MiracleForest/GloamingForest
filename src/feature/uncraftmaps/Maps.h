// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/MagicMapItem.java
//   src/main/java/gloamingforest/item/MazeMapItem.java
//   src/main/java/gloamingforest/item/mapdata/TFMagicMapData.java
//   src/main/java/gloamingforest/item/mapdata/TFMazeMapData.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// upstream's maps on Bedrock (U13). Bedrock draws maps only for its own map item, so a filled upstream map is
// a minecraft:filled_map whose map data is locked against Bedrock's terrain sampling and painted by
// U13, marked as upstream's in the stack's item data ("uncraftmaps.map": {"kind": "magic" | "maze" |
// "ore"}) and named after upstream's item (README deviation). Its pixels are written on the server, so
// vanilla clients see them; upstream's landmark icons are vanilla map decorations for vanilla clients
// and, for companions, upstream's icons over the map ("uncraftmaps.magic_map", F12's map decorations with
// the conquered red X and the player icon on top).

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/item/ItemStack.h"

class MapItemSavedData;
class Player;

namespace gf::feature::uncraftmaps {

enum class MapKind { Magic, Maze, Ore };

/// The upstream map kind of a stack (nullopt: not a filled upstream map).
[[nodiscard]] std::optional<MapKind> mapKindOf(ItemStack const& stack);
/// The map id (map_uuid) of a filled map stack (0: none).
[[nodiscard]] std::int64_t mapIdOf(ItemStack const& stack);

/// MagicMapItem#setupNewMap / MazeMapItem#setupNewMap for `player`'s position (scale 4 / 0,
/// tracking): the filled map stack (empty when the map data could not be made).
[[nodiscard]] ItemStack setupMagicMap(Player& player);
[[nodiscard]] ItemStack setupMazeMap(Player& player, bool ore);

/// The centre of a new maze / ore map made at (x, z) in `dimension` (TFMazeMapData#calculateMapCenter:
/// the 128-block grid, or the labyrinth's centre inside a labyrinth chunk).
[[nodiscard]] std::pair<int, int> mazeCenterFor(int dimension, int x, int z);
/// MagicMapCloningRecipe / MazeMapCloningRecipe#assemble over the stacks of a grid (empty: no match).
[[nodiscard]] ItemStack cloneResult(std::vector<ItemStack const*> const& grid, MapKind kind);

/// One update of a held map as MapItem#inventoryTick does it while selected (selftests call it).
void updateHeldMap(Player& player, ItemStack const& stack);

/// An upstream decoration of a magic map (TFMagicMapData#addTFDecoration).
struct TfDecoration {
    std::string  type; ///< "gloamingforest:lich_tower"
    int          x = 0, z = 0, rotation = 0;
    bool         conquered = false;
};
/// The upstream decorations of a magic map so far (selftests, the companion packet).
[[nodiscard]] std::vector<TfDecoration> decorationsOf(std::int64_t mapId);
/// The yCenter of a maze / ore map (nullopt: not one).
[[nodiscard]] std::optional<int> mazeYCenter(std::int64_t mapId);

/// The game's map data of `mapId` (nullptr: unknown).
[[nodiscard]] MapItemSavedData* mapData(std::int64_t mapId);
/// The pixel colour of a map (ARGB; 0 = transparent).
[[nodiscard]] std::uint32_t mapPixel(MapItemSavedData const& data, int x, int z);

/// The vanilla map decoration an upstream decoration is shown as for vanilla clients (MapDecoration::Type).
[[nodiscard]] int vanillaDecorationFor(std::string const& tfType);

/// Java's MapItem#create + MapItemSavedData#addTargetDecoration ("+", red X) for map_locator: a
/// vanilla filled map of scale 4 centred on Java's map grid around (x, z) in `dimension`.
[[nodiscard]] ItemStack locatorMap(int dimension, int x, int z);

inline constexpr char kMapData[]      = "uncraftmaps.map";
inline constexpr char kMagicChannel[] = "uncraftmaps.magic_map";
inline constexpr char kMazeChannel[]  = "uncraftmaps.maze_map";

} // namespace gf::feature::uncraftmaps

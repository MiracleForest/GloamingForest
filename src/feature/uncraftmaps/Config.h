// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/config/GloamingCommonConfig.java
//   src/main/java/gloamingforest/network/SyncUncraftingTableConfigPacket.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Config section "uncraftmaps" (rule R13): upstream's ten "Uncrafting Table" keys of GloamingCommonConfig
// (docs/spec/config.md), synced to companions as SyncUncraftingTableConfigPacket does
// ("uncraftmaps.sync_config", at the handshake and after /gloamingforest reload).

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/uncraftmaps/Uncrafting.h"

namespace gf::feature::uncraftmaps {

struct UncraftmapsConfig {
    int version = 1;
    /// common.Uncrafting Table.uncraftingXpCostMultiplier (uncrafting_xp_cost)
    double uncraftingXpCostMultiplier = 1.0;
    /// common.Uncrafting Table.repairingXpCostMultiplier (repairing_xp_cost)
    double repairingXpCostMultiplier = 1.0;
    /// common.Uncrafting Table.disableUncraftingRecipes (uncrafting_recipes)
    std::vector<std::string> disableUncraftingRecipes{"gloamingforest:giant_log_to_oak_planks"};
    /// common.Uncrafting Table.flipRecipeList (uncrafting_recipes_flip)
    bool flipRecipeList = false;
    /// common.Uncrafting Table.blacklistedUncraftingModIds (uncrafting_mod_ids)
    std::vector<std::string> blacklistedUncraftingModIds{};
    /// common.Uncrafting Table.flipIdList (uncrafting_mod_id_flip)
    bool flipIdList = false;
    /// common.Uncrafting Table.enableShapelessCrafting (shapeless_uncrafting)
    bool enableShapelessCrafting = false;
    /// common.Uncrafting Table.disableIngredientSwitching (ingredient_switching)
    bool disableIngredientSwitching = false;
    /// common.Uncrafting Table.disableUncrafting (disable_uncrafting)
    bool disableUncrafting = false;
    /// common.Uncrafting Table.disableUncraftingTable (disable_uncrafting_table)
    bool disableUncraftingTable = false;
};

/// The values in effect (the config file's; on a companion client the server's last sync).
[[nodiscard]] UncraftmapsConfig const& config();
/// Selftests: replaces the values in effect until `restoreConfig` (the file is not touched).
void overrideConfig(UncraftmapsConfig const& values);
void restoreConfig();

/// The recipe filter of the two id lists.
[[nodiscard]] core::uncraftmaps::RecipeFilter recipeFilter();

/// SyncUncraftingTableConfigPacket's fields as JSON, and back.
[[nodiscard]] nlohmann::json configToJson(UncraftmapsConfig const& c);
[[nodiscard]] UncraftmapsConfig configFromJson(nlohmann::json const& j);

inline constexpr char kTable[]         = "gloamingforest:uncrafting_table";
inline constexpr char kScreen[]        = "uncraftmaps.uncrafting";
inline constexpr char kSyncChannel[]   = "uncraftmaps.sync_config";
inline constexpr char kGuiChannel[]    = "uncraftmaps.gui";

} // namespace gf::feature::uncraftmaps

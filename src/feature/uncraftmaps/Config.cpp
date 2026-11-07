// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/config/TFCommonConfig.java
//   src/main/java/twilightforest/config/TFConfig.java
//   src/main/java/twilightforest/network/SyncUncraftingTableConfigPacket.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Config section "uncraftmaps" and its sync to companions (TFConfig#syncUncraftingConfig: on
// login and after a config reload, SyncUncraftingTableConfigPacket to every player).
#include "feature/uncraftmaps/Config.h"

#include <optional>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/ext/item/Recipes.h"
#include "feature/net/Net.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::uncraftmaps {

namespace {
std::optional<UncraftmapsConfig>& overridden() {
    static std::optional<UncraftmapsConfig> o;
    return o;
}

void syncTo(Player& player) { (void)net::send(player, kSyncChannel, configToJson(config())); }

void syncAll() {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([](Player& p) {
        if (net::isCompanion(p)) syncTo(p);
        return true;
    });
}
} // namespace

UncraftmapsConfig const& config() {
    if (auto const& o = overridden()) return *o;
    return gf::config::get<UncraftmapsConfig>();
}

void overrideConfig(UncraftmapsConfig const& values) {
    overridden() = values;
    syncAll();
}

void restoreConfig() {
    overridden().reset();
    syncAll();
}

core::uncraftmaps::RecipeFilter recipeFilter() {
    auto const& c = config();
    return {c.disableUncraftingRecipes, c.flipRecipeList, c.blacklistedUncraftingModIds, c.flipIdList};
}

nlohmann::json configToJson(UncraftmapsConfig const& c) {
    return {
        {"uncraftingMultiplier", c.uncraftingXpCostMultiplier},
        {"repairingMultiplier", c.repairingXpCostMultiplier},
        {"allowShapeless", c.enableShapelessCrafting},
        {"disableIngredientSwitching", c.disableIngredientSwitching},
        {"disabledUncrafting", c.disableUncrafting},
        {"disabledTable", c.disableUncraftingTable},
        {"disabledRecipes", c.disableUncraftingRecipes},
        {"flipRecipeList", c.flipRecipeList},
        {"disabledModids", c.blacklistedUncraftingModIds},
        {"flipModidList", c.flipIdList},
    };
}

UncraftmapsConfig configFromJson(nlohmann::json const& j) {
    UncraftmapsConfig c;
    if (!j.is_object()) return c;
    c.uncraftingXpCostMultiplier = j.value("uncraftingMultiplier", 1.0);
    c.repairingXpCostMultiplier  = j.value("repairingMultiplier", 1.0);
    c.enableShapelessCrafting    = j.value("allowShapeless", false);
    c.disableIngredientSwitching = j.value("disableIngredientSwitching", false);
    c.disableUncrafting          = j.value("disabledUncrafting", false);
    c.disableUncraftingTable     = j.value("disabledTable", false);
    c.disableUncraftingRecipes   = j.value("disabledRecipes", std::vector<std::string>{});
    c.flipRecipeList             = j.value("flipRecipeList", false);
    c.blacklistedUncraftingModIds = j.value("disabledModids", std::vector<std::string>{});
    c.flipIdList                 = j.value("flipModidList", false);
    return c;
}

class ConfigModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        net::onCompanionJoined([](Player& p) { syncTo(p); });
        gf::config::onReload([] { syncAll(); });
        // UncraftingTableCondition (recipe condition of twilightforest:uncrafting_table):
        // !TFConfig.disableEntireTable
        recipes::ext::registerCondition("twilightforest:uncrafting_table_enabled", [](nlohmann::json const&) { return !config().disableUncraftingTable; });
        // The same condition on the table's own BP crafting recipe (Bedrock loads it whatever the
        // config): a special crafting gate over that layout refuses the craft while the table is
        // disabled and crafts the recipe's own result otherwise
        recipes::registerSpecialCrafting(
            "twilightforest:uncrafting_table_enabled_gate", [](recipes::CraftingGrid const&) { return !config().disableUncraftingTable; },
            [](recipes::CraftingGrid const&) {
                auto s = game::items::make("twilightforest:uncrafting_table", 1);
                return s ? *s : ItemStack{};
            }
        );
        recipes::ext::addSpecialLayout("twilightforest:uncrafting_table_enabled_gate", "twilightforest:uncrafting_table");
    }
    void uninstall() {}
};

} // namespace gf::feature::uncraftmaps

GF_REGISTER_CONFIG("uncraftmaps", gf::feature::uncraftmaps::UncraftmapsConfig);
GF_REGISTER_MODULE(gf::feature::uncraftmaps::ConfigModule);

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Extension of contract "progression" by U14 (rule R18; to be folded into F10's events or F9's
// crafting hooks by A8): the vanilla trigger minecraft:recipe_crafted, which neither F10 nor F9
// raises. Java fires it from ResultSlot#checkTakeAchievements when a player takes a crafting
// result; Bedrock crafts through item stack requests, so a successful CraftRecipe /
// CraftRecipeAuto action of the crafting screen (2x2 or crafting table) fires
//
//     progression::trigger(player, "minecraft:recipe_crafted", {"recipe_id": "<the BP recipe id>"})
//
// with the identifier of the BP recipe the client crafted (F1 splits a Java recipe with tag
// ingredients into numbered layouts, "twilightforest:equipment/travellers_belt_3"; the criteria
// engine counts every layout as the Java recipe). TF's craft_travellers_gear, maze_map,
// naga_armors and ore_map criteria use it.

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/ItemStackRequestActionType.h"
#include "mc/world/inventory/network/TypedServerNetId.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftBase.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftRecipeOptional.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "feature/progression/Progression.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::ext::progression {

/// The BP recipe a CraftRecipe / CraftRecipeAuto action names ("" for other actions).
std::string recipeOf(ItemStackRequestActionCraftBase const& action) {
    auto type = static_cast<ItemStackRequestActionType>(action.mActionType);
    if (type != ItemStackRequestActionType::CraftRecipe && type != ItemStackRequestActionType::CraftRecipeAuto) return {};
    auto level = ll::service::getLevel();
    if (!level) return {};
    // ItemStackRequestActionCraft<RecipeNetId, 12 | 13>: the net id follows the craft base as in
    // ItemStackRequestActionCraftRecipeOptional (F9 reads it the same way)
    RecipeNetId const& netId  = static_cast<ItemStackRequestActionCraftRecipeOptional const&>(action).mRecipeNetId;
    auto const*        recipe = level->getRecipes().getRecipeByNetId(netId);
    if (!recipe) return {};
    std::string id = *recipe->mRecipeId;
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

} // namespace gf::feature::ext::progression

LL_TYPE_INSTANCE_HOOK(
    ProgressionRecipeCraftedHook,
    ll::memory::HookPriority::Low,
    CraftHandlerCrafting,
    &CraftHandlerCrafting::$_handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& action
) {
    std::string recipe;
    try {
        recipe = gf::feature::ext::progression::recipeOf(action);
    } catch (...) {
        recipe.clear();
    }
    auto result = origin(action);
    if (recipe.empty() || result != ItemStackNetResult::Success) return result;
    try {
        Player& player = static_cast<ItemStackRequestActionCraftHandler&>(mCraftRequestHandler).mPlayer;
        if (!player.isClientSide()) gf::progression::trigger(player, "minecraft:recipe_crafted", {{"recipe_id", recipe}});
    } catch (std::exception const& e) {
        gf::log().warn("progression: recipe_crafted failed: {}", e.what());
    }
    return result;
}

namespace gf::feature::ext::progression {
namespace {
class RecipeCraftedModule {
public:
    void install() {
        if (int r = ProgressionRecipeCraftedHook::hook(); r != 0) log().error("progression: the recipe_crafted hook failed ({})", r);
    }
    void uninstall() { ProgressionRecipeCraftedHook::unhook(); }
};
} // namespace
} // namespace gf::feature::ext::progression

GF_REGISTER_MODULE(gf::feature::ext::progression::RecipeCraftedModule);

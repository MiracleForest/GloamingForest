#pragma once

// Contract "recipes" (header owned by F0, implemented by F9).
//
// Vanilla-type recipes (crafting, smelting, stonecutting, smithing) are Bedrock recipe JSON (F1
// import). upstream's 13 custom recipe serializers (drying, uncrafting, travellers shaped / shapeless,
// vest / gloves merge, casket_repair, emperors_cloth, essence_repair, magic / maze map cloning,
// moonworm_queen_repair, scepter_repair, no_template_smithing) are loaded from
// assets/data/recipes by the loader their owner registers (rule R4), and special crafting runs
// natively. `producing` answers "which recipes make this item" for the uncrafting table (U13).
//
//     gf::recipes::registerType("gloamingforest:drying", [](std::string const& id, nlohmann::json const& j) { ... });
//     for (auto const* r : gf::recipes::producing("gloamingforest:naga_chestplate")) ...;

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

class ItemStack;

namespace gf::recipes {

using Json = nlohmann::json;

/// An ingredient: any of `items`, or the members of item tag `tag`.
struct Ingredient {
    std::vector<std::string> items;
    std::string              tag;
    [[nodiscard]] bool       empty() const { return items.empty() && tag.empty(); }
};

/// A loaded recipe of any type.
struct CraftingRecipe {
    std::string             id;     ///< "gloamingforest:naga_chestplate"
    std::string             type;   ///< "minecraft:crafting_shaped", "gloamingforest:drying", ...
    int                     width  = 0; ///< shaped grids (0 for shapeless)
    int                     height = 0;
    std::vector<Ingredient> ingredients; ///< row-major for shaped recipes
    std::string             resultItem;
    int                     resultCount = 1;
    Json                    data;   ///< the full recipe JSON (type-specific fields)
};

/// Loads a custom-type recipe JSON (nullopt: invalid, one warning).
using Loader = std::function<std::optional<CraftingRecipe>(std::string const& id, Json const& json)>;
void registerType(std::string const& typeId, Loader loader);

/// A crafting grid being matched (row-major, `width` x `height`; null / empty stacks allowed).
struct CraftingGrid {
    int                            width  = 3;
    int                            height = 3;
    std::vector<ItemStack const*>  slots;
};
using Matcher   = std::function<bool(CraftingGrid const& grid)>;
using Assembler = std::function<ItemStack(CraftingGrid const& grid)>;
/// The items left in the grid after crafting (getRemainingItems), one per slot.
using Remainder = std::function<std::vector<ItemStack>(CraftingGrid const& grid)>;
/// A special (code-driven) crafting recipe in the crafting table.
void registerSpecialCrafting(std::string const& id, Matcher matcher, Assembler assembler, Remainder remainder = {});

/// Every recipe whose result is `itemId` (vanilla types and custom types).
[[nodiscard]] std::vector<CraftingRecipe const*> producing(std::string const& itemId);
/// Every recipe of `typeId`.
[[nodiscard]] std::vector<CraftingRecipe const*> ofType(std::string const& typeId);
/// The first recipe of `typeId` accepted by `match` (nullptr: none).
[[nodiscard]] CraftingRecipe const* findFirst(std::string const& typeId, std::function<bool(CraftingRecipe const&)> const& match);
/// A recipe by id (nullptr: unknown).
[[nodiscard]] CraftingRecipe const* byId(std::string const& recipeId);

} // namespace gf::recipes

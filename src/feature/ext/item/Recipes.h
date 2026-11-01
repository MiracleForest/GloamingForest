#pragma once

// Extension of contract "recipes" by F9 (rule R18; to be folded into feature/recipes/Recipes.h by A8).
//
// How a special (code-driven) crafting recipe reaches Bedrock's crafting table. The crafting screen
// of every client (with or without the client mod) only offers a craft that matches a BP recipe of
// the "crafting_table" tag: the client sends the BP recipe's net id with the grid, the server checks
// the grid against that recipe's ingredients (shaped: pattern and items; shapeless: exactly these
// items, one per ingredient), and only then F9's crafting hook asks the special recipe's matcher
// and assembler (CraftHandlerCrafting -> ItemStackRequestActionCraftHandler::_initCraftResults) and
// replaces the BP recipe's static result. So a special recipe is declared as BP recipe(s) whose
// identifier names it:
//
// - one layout: a BP recipe with the special recipe's id ("twilightforest:casket_repair" as a
//   shapeless recipe of the casket and the materials);
// - a variable layout (TF's map cloning: one filled map plus 1..8 empty maps; the scepter repairs:
//   any number of repair items): one BP recipe per layout, each with its own identifier
//   ("twilightforest:magic_map_cloning_2" ... "_9"), mapped to the special recipe here. Bedrock's
//   shapeless match takes exactly the listed items, so each count is its own recipe; the result
//   item of each BP recipe is what clients preview (the assembler's stack replaces it).
//
// The assembler sees the whole grid (CraftingGrid, the player's 2x2 or 3x3 UI container slots), so
// one matcher serves every layout. The remainder (getRemainingItems) goes back into the grid slot
// the craft emptied, as Java's ResultSlot#onTake.
//
//     gf::recipes::registerSpecialCrafting("twilightforest:magic_map_cloning", match, assemble);
//     for (int n = 2; n <= 9; ++n)
//         gf::recipes::ext::addSpecialLayout("twilightforest:magic_map_cloning", "twilightforest:magic_map_cloning_" + std::to_string(n));

#include <string>

namespace gf::recipes::ext {

/// Crafts with BP recipe `bpRecipeId` run special recipe `specialId` (registerSpecialCrafting).
void addSpecialLayout(std::string const& specialId, std::string const& bpRecipeId);

} // namespace gf::recipes::ext

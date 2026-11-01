// Neutral stub of the "recipes" contract (F0). F9 replaces this file (rule R18).
#include "feature/recipes/Recipes.h"

#include "core/Stub.h"

namespace gf::recipes {

void registerType(std::string const&, Loader) { GF_STUB("recipes.registerType"); }
void registerSpecialCrafting(std::string const&, Matcher, Assembler, Remainder) { GF_STUB("recipes.registerSpecialCrafting"); }
std::vector<CraftingRecipe const*> producing(std::string const&) {
    GF_STUB("recipes.producing");
    return {};
}
std::vector<CraftingRecipe const*> ofType(std::string const&) {
    GF_STUB("recipes.ofType");
    return {};
}
CraftingRecipe const* findFirst(std::string const&, std::function<bool(CraftingRecipe const&)> const&) {
    GF_STUB("recipes.findFirst");
    return nullptr;
}
CraftingRecipe const* byId(std::string const&) {
    GF_STUB("recipes.byId");
    return nullptr;
}

} // namespace gf::recipes

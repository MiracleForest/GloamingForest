// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFRecipes.java
//   src/main/java/twilightforest/item/recipe/DryingRecipe.java
//   src/main/java/twilightforest/item/recipe/UncraftingRecipe.java
//   src/main/java/twilightforest/item/recipe/ScepterRepairRecipe.java
//   src/main/java/twilightforest/item/recipe/NoTemplateSmithingRecipe.java
//   src/main/java/twilightforest/item/recipe/travellers/TravellersGearModifierShapedRecipe.java
//   src/main/java/twilightforest/item/recipe/travellers/TravellersGearModifierShapelessRecipe.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "recipes" (F9). Three sources:
// - the Java-format recipes of the data tree (data/<ns>/recipe/**.json, F1's import, plus unit
//   bundles data/gf/recipes/<unit>.json), parsed by the loader of their type: the vanilla types and
//   TF's 13 serializers are built in (the codecs of TFRecipes), an owner's `registerType` replaces
//   the built-in loader of its type (rule R4);
// - the game's own crafting table recipes (Level::getRecipes(), the vanilla ones and the Bedrock
//   recipes of the packs), converted once per world for `producing` (the uncrafting table
//   uncrafts vanilla items too);
// - special (code-driven) crafting: the crafting hook asks the matcher / assembler of the recipe
//   id the client crafted (a BP recipe with that identifier shows the shape to vanilla clients).
#include "feature/recipes/Recipes.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemDescriptor.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/RecipeIngredient.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "feature/common/Interactions.h"
#include "feature/item/DataFiles.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::recipes {

namespace {

namespace data = feature::item::data;

struct Special {
    Matcher   matcher;
    Assembler assembler;
    Remainder remainder;
};

struct Store {
    std::recursive_mutex                                       mutex;
    std::map<std::string, Loader>                              loaders;   ///< registered by owners
    std::deque<CraftingRecipe>                                 dataRecipes;
    std::deque<CraftingRecipe>                                 nativeRecipes;
    std::unordered_map<std::string, CraftingRecipe const*>     byId;
    std::unordered_map<std::string, std::vector<CraftingRecipe const*>> byResult;
    std::unordered_map<std::string, std::vector<CraftingRecipe const*>> byType;
    std::map<std::string, Special>                             specials;
    bool                                                       nativeDirty = true;
    bool                                                       dataLoaded  = false;
    bool                                                       dataDirty   = false; ///< a loader came after the data
};

Store& store() {
    static Store s;
    return s;
}

// ---- Java data ---------------------------------------------------------------------------------------

std::string idField(Json const& j) {
    if (j.is_string()) return data::withNamespace(j.get<std::string>());
    if (!j.is_object()) return {};
    for (auto const* k : {"id", "item"})
        if (auto it = j.find(k); it != j.end() && it->is_string()) return data::withNamespace(it->get<std::string>());
    return {};
}

void parseIngredientInto(Json const& j, Ingredient& out) {
    if (j.is_array()) {
        for (auto const& e : j) parseIngredientInto(e, out);
        return;
    }
    if (j.is_string()) {
        auto s = j.get<std::string>();
        if (s.starts_with('#')) out.tag = data::withNamespace(s.substr(1));
        else out.items.push_back(data::withNamespace(s));
        return;
    }
    if (!j.is_object()) return;
    if (auto it = j.find("item"); it != j.end() && it->is_string()) out.items.push_back(data::withNamespace(it->get<std::string>()));
    if (auto it = j.find("tag"); it != j.end() && it->is_string()) {
        if (out.tag.empty()) out.tag = data::withNamespace(it->get<std::string>());
        else // a second tag: keep its members (an Ingredient holds one tag)
            for (auto& m : tags::members(tags::Registry::Item, data::withNamespace(it->get<std::string>()))) out.items.push_back(std::move(m));
    }
    if (auto it = j.find("items"); it != j.end()) parseIngredientInto(*it, out);
    if (auto it = j.find("children"); it != j.end()) parseIngredientInto(*it, out); // neoforge:compound
    if (auto it = j.find("ingredient"); it != j.end()) parseIngredientInto(*it, out);  // neoforge:components etc.
}

/// The result of a Java recipe: {"id"/"item", "count"} or a string.
void parseResult(Json const& j, CraftingRecipe& r) {
    if (j.is_null()) return;
    r.resultItem  = idField(j);
    r.resultCount = j.is_object() ? j.value("count", 1) : 1;
}

/// A ShapedRecipePattern ({"pattern": [...], "key": {...}}) into width / height / row-major ingredients.
bool parsePattern(Json const& j, CraftingRecipe& r) {
    auto p = j.find("pattern");
    auto k = j.find("key");
    if (p == j.end() || !p->is_array() || p->empty() || k == j.end() || !k->is_object()) return false;
    std::map<char, Ingredient> key;
    for (auto& [c, v] : k->items()) {
        if (c.size() != 1) continue;
        parseIngredientInto(v, key[c[0]]);
    }
    std::vector<std::string> rows;
    for (auto const& row : *p)
        if (row.is_string()) rows.push_back(row.get<std::string>());
    r.height = static_cast<int>(rows.size());
    r.width  = 0;
    for (auto const& row : rows) r.width = std::max(r.width, static_cast<int>(row.size()));
    r.ingredients.clear();
    for (auto const& row : rows)
        for (int x = 0; x < r.width; ++x) {
            char c = x < static_cast<int>(row.size()) ? row[x] : ' ';
            r.ingredients.push_back(c == ' ' ? Ingredient{} : key[c]);
        }
    return true;
}

std::vector<Ingredient> parseList(Json const& j) {
    std::vector<Ingredient> out;
    if (!j.is_array()) return out;
    for (auto const& e : j) {
        Ingredient i;
        parseIngredientInto(e, i);
        out.push_back(std::move(i));
    }
    return out;
}

Ingredient one(Json const& j) {
    Ingredient i;
    parseIngredientInto(j, i);
    return i;
}

/// The Travellers' Gear item of a modifier recipe: its first ingredient naming a travellers_ item.
std::string travellersItemOf(std::vector<Ingredient> const& list) {
    for (auto const& i : list)
        for (auto const& id : i.items)
            if (id.starts_with("twilightforest:travellers_")) return id;
    return {};
}

std::optional<CraftingRecipe> builtIn(std::string const& id, Json const& j) {
    CraftingRecipe r;
    r.id   = id;
    r.type = data::withNamespace(j.value("type", std::string()));
    r.data = j;
    auto const& t = r.type;
    if (t == "minecraft:crafting_shaped") {
        if (!parsePattern(j, r)) return std::nullopt;
        parseResult(j.value("result", Json()), r);
    } else if (t == "minecraft:crafting_shapeless") {
        r.ingredients = parseList(j.value("ingredients", Json::array()));
        parseResult(j.value("result", Json()), r);
    } else if (t == "minecraft:smelting" || t == "minecraft:blasting" || t == "minecraft:smoking" || t == "minecraft:campfire_cooking"
               || t == "minecraft:stonecutting") {
        r.ingredients = {one(j.value("ingredient", Json()))};
        parseResult(j.value("result", Json()), r);
        if (t == "minecraft:stonecutting" && j.contains("count")) r.resultCount = j.value("count", 1);
    } else if (t == "minecraft:smithing_transform") {
        r.ingredients = {one(j.value("template", Json())), one(j.value("base", Json())), one(j.value("addition", Json()))};
        parseResult(j.value("result", Json()), r);
    } else if (t == "minecraft:smithing_trim") {
        r.ingredients = {one(j.value("template", Json())), one(j.value("base", Json())), one(j.value("addition", Json()))};
    } else if (t == "twilightforest:drying") {
        // DryingRecipe: input, result, filter_time (the fixture spells them ingredient / time)
        r.ingredients = {one(j.contains("input") ? j["input"] : j.value("ingredient", Json()))};
        parseResult(j.value("result", Json()), r);
    } else if (t == "twilightforest:uncrafting") {
        // UncraftingRecipe: the input (input_count of it) comes apart into the pattern
        if (!parsePattern(j, r)) return std::nullopt;
        auto input    = one(j.value("input", Json()));
        r.resultItem  = input.items.empty() ? std::string() : input.items.front();
        r.resultCount = j.value("input_count", 1);
    } else if (t == "twilightforest:travellers_gear_modifier_shaped_recipe") {
        if (!parsePattern(j.value("pattern", Json::object()), r)) return std::nullopt;
        r.resultItem = travellersItemOf(r.ingredients);
    } else if (t == "twilightforest:travellers_gear_modifier_shapeless_recipe") {
        r.ingredients = parseList(j.value("ingredients", Json::array()));
        r.resultItem  = travellersItemOf(r.ingredients);
    } else if (t == "twilightforest:scepter_repair") {
        // ScepterRepairRecipe: the scepter plus its repair ingredients restore `durability`
        auto scepter = data::withNamespace(j.value("scepter", std::string()));
        r.ingredients.push_back(Ingredient{{scepter}, {}});
        for (auto& i : parseList(j.value("repair_ingredients", Json::array()))) r.ingredients.push_back(std::move(i));
        r.resultItem = scepter;
    } else if (t == "twilightforest:no_template_smithing") {
        // NoTemplateSmithingRecipe: base + addition, the base gets additional_data
        r.ingredients = {one(j.value("base", Json())), one(j.value("addition", Json()))};
    } else if (t == "twilightforest:casket_repair_recipe" || t == "twilightforest:emperors_cloth_recipe" || t == "twilightforest:essence_repair_recipe"
               || t == "twilightforest:magic_map_cloning_recipe" || t == "twilightforest:maze_map_cloning_recipe"
               || t == "twilightforest:moonworm_queen_repair_recipe" || t == "twilightforest:travellers_vest_gloves_merge_recipe") {
        // SimpleCraftingRecipeSerializer: code only (special crafting of the owner), {"category"}
    } else if (t.starts_with("minecraft:crafting_special_") || t == "minecraft:crafting_decorated_pot" || t == "minecraft:crafting_transmute") {
        // vanilla code recipes: nothing to index
    } else {
        return std::nullopt;
    }
    return r;
}

void index(Store& s, CraftingRecipe const& r) {
    s.byId[r.id] = &r;
    s.byType[r.type].push_back(&r);
    if (!r.resultItem.empty()) s.byResult[r.resultItem].push_back(&r);
}

void reindex(Store& s) {
    s.byId.clear();
    s.byResult.clear();
    s.byType.clear();
    for (auto const& r : s.nativeRecipes) index(s, r);
    for (auto const& r : s.dataRecipes) index(s, r); // data wins over a game recipe of the same id
    // the game recipe of an id the data has too: drop it from the lists
    for (auto& [item, list] : s.byResult)
        std::erase_if(list, [&](CraftingRecipe const* r) { return s.byId[r->id] != r; });
    for (auto& [type, list] : s.byType) std::erase_if(list, [&](CraftingRecipe const* r) { return s.byId[r->id] != r; });
}

std::optional<CraftingRecipe> loadOne(Store& s, std::string const& id, Json const& j) {
    if (!j.is_object()) return std::nullopt;
    auto type = data::withNamespace(j.value("type", std::string()));
    if (type.empty() || type == "minecraft:") return std::nullopt;
    if (auto it = s.loaders.find(type); it != s.loaders.end()) {
        try {
            auto r = it->second(id, j);
            if (r) {
                if (r->id.empty()) r->id = id;
                if (r->type.empty()) r->type = type;
                if (r->data.is_null()) r->data = j;
            } else {
                log().warn("recipes: {} ({}) was refused by its loader", id, type);
            }
            return r;
        } catch (std::exception const& e) {
            log().warn("recipes: the loader of {} failed on {}: {}", type, id, e.what());
            return std::nullopt;
        }
    }
    auto r = builtIn(id, j);
    if (!r) {
        static std::set<std::string> warned;
        if (warned.insert(type).second) log().warn("recipes: no loader for recipe type {} (first: {})", type, id);
    }
    return r;
}

void loadData() {
    auto&                     s = store();
    std::lock_guard           lock(s.mutex);
    s.dataRecipes.clear();
    auto add = [&](std::string const& id, Json const& j) {
        if (auto r = loadOne(s, id, j)) s.dataRecipes.push_back(std::move(*r));
    };
    for (auto const* dir : {"recipe", "recipes"}) {
        for (auto const& f : data::list(dir)) {
            auto doc = data::read(f.path);
            if (f.ns == "gf") {
                // unit bundles: {"<id>": recipe, ...} or [{"id": ..., ...}, ...]
                if (doc.is_object() && !doc.contains("type")) {
                    for (auto& [id, v] : doc.items()) add(data::withNamespace(id), v);
                } else if (doc.is_array()) {
                    for (auto const& v : doc)
                        if (v.is_object() && v.contains("id")) add(data::withNamespace(v.value("id", std::string())), v);
                } else {
                    add(f.id(), doc);
                }
                continue;
            }
            add(f.id(), doc);
        }
    }
    s.dataLoaded = true;
    s.dataDirty  = false;
    reindex(s);
    log().info("recipes: {} data recipes loaded", s.dataRecipes.size());
}

/// Loads the data again when a loader was registered after it (lock held, recursive).
void ensureData() {
    auto& s = store();
    if (s.dataDirty) loadData();
}

[[maybe_unused]] bool const kRegistered = (data::onReload(&loadData), true);

// ---- the game's recipes ------------------------------------------------------------------------------

Ingredient ingredientOf(RecipeIngredient const& ri) {
    Ingredient out;
    if (ri.isNull()) return out;
    if (auto const* item = ri.getItem()) {
        out.items.push_back(item->mFullName->getString());
        return out;
    }
    ri.forEachItemUntil([&](Item const& item, short) {
        out.items.push_back(item.mFullName->getString());
        return false;
    });
    return out;
}

void convertNative(Store& s) {
    s.nativeRecipes.clear();
    s.nativeDirty = false;
    auto level    = ll::service::getLevel();
    if (!level) return;
    auto const& all = level->getRecipes().mRecipes.get();
    auto        it  = all.find(HashedString("crafting_table"));
    if (it == all.end()) return;
    for (auto const& [id, recipe] : it->second) {
        if (!recipe || recipe->isMultiRecipe()) continue;
        try {
            CraftingRecipe r;
            r.id = id.find(':') == std::string::npos ? "minecraft:" + id : id;
            bool shapeless = recipe->isShapeless();
            r.type         = shapeless ? "minecraft:crafting_shapeless" : "minecraft:crafting_shaped";
            r.width        = shapeless ? 0 : recipe->mWidth;
            r.height       = shapeless ? 0 : recipe->mHeight;
            for (auto const& ri : recipe->mMyIngredients.get()) r.ingredients.push_back(ingredientOf(ri));
            auto const& results = recipe->getResultItems();
            if (results.empty() || results.front().isNull()) continue;
            r.resultItem  = results.front().getTypeName();
            r.resultCount = results.front().mCount;
            r.data        = Json{{"type", r.type}, {"native", true}};
            s.nativeRecipes.push_back(std::move(r));
        } catch (...) {
        }
    }
}

void ensureNative() {
    ensureData();
    auto& s = store();
    if (!s.nativeDirty) return;
    if (!ll::service::getLevel()) return;
    convertNative(s);
    reindex(s);
    log().info("recipes: {} crafting table recipes of the game indexed", s.nativeRecipes.size());
}

} // namespace

void registerType(std::string const& typeId, Loader loader) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.loaders[data::withNamespace(typeId)] = std::move(loader);
    // the data was loaded before the registration (a unit installed after the data module): parse
    // it again with this loader at the next query
    if (s.dataLoaded) s.dataDirty = true;
}

void registerSpecialCrafting(std::string const& id, Matcher matcher, Assembler assembler, Remainder remainder) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.specials[data::withNamespace(id)] = Special{std::move(matcher), std::move(assembler), std::move(remainder)};
}

std::vector<CraftingRecipe const*> producing(std::string const& itemId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureNative();
    auto it = s.byResult.find(data::withNamespace(itemId));
    return it == s.byResult.end() ? std::vector<CraftingRecipe const*>{} : it->second;
}

std::vector<CraftingRecipe const*> ofType(std::string const& typeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureNative();
    auto it = s.byType.find(data::withNamespace(typeId));
    return it == s.byType.end() ? std::vector<CraftingRecipe const*>{} : it->second;
}

CraftingRecipe const* findFirst(std::string const& typeId, std::function<bool(CraftingRecipe const&)> const& match) {
    for (auto const* r : ofType(typeId))
        if (!match || match(*r)) return r;
    return nullptr;
}

CraftingRecipe const* byId(std::string const& recipeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureNative();
    auto it = s.byId.find(data::withNamespace(recipeId));
    return it == s.byId.end() ? nullptr : it->second;
}

// ---- internals ---------------------------------------------------------------------------------------

namespace internal {

Ingredient parseIngredient(Json const& j) { return one(j); }

bool matches(Ingredient const& ingredient, std::string const& itemId) {
    if (ingredient.empty()) return itemId.empty();
    if (itemId.empty()) return false;
    auto id = data::withNamespace(itemId);
    if (std::find(ingredient.items.begin(), ingredient.items.end(), id) != ingredient.items.end()) return true;
    return !ingredient.tag.empty() && tags::has(tags::Registry::Item, ingredient.tag, id);
}

PlayerGrid gridOf(Player& player, bool workbench) {
    PlayerGrid out;
    int        size  = workbench ? 3 : 2;
    int        first = static_cast<int>(workbench ? PlayerUISlot::Crafting3x3Input1 : PlayerUISlot::Crafting2x2Input1);
    auto&      ui    = player.mPlayerUIContainer.get();
    out.stacks.reserve(static_cast<std::size_t>(size * size));
    for (int i = 0; i < size * size; ++i) out.stacks.push_back(ui.getItem(first + i));
    out.grid.width  = size;
    out.grid.height = size;
    for (auto const& st : out.stacks) out.grid.slots.push_back(&st);
    return out;
}

bool isSpecial(std::string const& recipeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.specials.contains(data::withNamespace(recipeId));
}

std::optional<ItemStack> assembleSpecial(std::string const& recipeId, CraftingGrid const& grid) {
    Special sp;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto            it = s.specials.find(data::withNamespace(recipeId));
        if (it == s.specials.end()) return std::nullopt;
        sp = it->second;
    }
    if (sp.matcher && !sp.matcher(grid)) return std::nullopt;
    if (!sp.assembler) return std::nullopt;
    return sp.assembler(grid);
}

std::vector<ItemStack> remainderOf(std::string const& recipeId, CraftingGrid const& grid) {
    Remainder rem;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto            it = s.specials.find(data::withNamespace(recipeId));
        if (it == s.specials.end() || !it->second.remainder) return {};
        rem = it->second.remainder;
    }
    auto out = rem(grid);
    std::erase_if(out, [](ItemStack const& st) { return game::items::isEmpty(st); });
    return out;
}

void unregisterSpecial(std::string const& recipeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.specials.erase(data::withNamespace(recipeId));
}

std::size_t dataRecipeCount() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.dataRecipes.size();
}

std::size_t nativeRecipeCount() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureNative();
    return s.nativeRecipes.size();
}

void refreshNative() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.nativeDirty = true;
    ensureNative();
}

} // namespace internal

namespace {
/// The crafting handler of the craft being processed (set around CraftHandlerCrafting::_handleCraftAction).
thread_local CraftHandlerCrafting* gCrafting = nullptr;
} // namespace

} // namespace gf::recipes

namespace rc = gf::recipes;

LL_TYPE_INSTANCE_HOOK(
    RecipesCraftActionHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerCrafting,
    &CraftHandlerCrafting::$_handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& action
) {
    struct Restore {
        CraftHandlerCrafting* prev;
        ~Restore() { rc::gCrafting = prev; }
    } restore{rc::gCrafting};
    rc::gCrafting = this;
    return origin(action);
}

// The results of a craft: a special recipe's assembler replaces the recipe's static result.
LL_TYPE_INSTANCE_HOOK(
    RecipesInitResultsHook,
    ll::memory::HookPriority::Normal,
    ItemStackRequestActionCraftHandler,
    &ItemStackRequestActionCraftHandler::_initCraftResults,
    ItemStackNetResult,
    std::vector<ItemInstance> const& results,
    uchar                            numCrafts
) {
    auto* crafting = rc::gCrafting;
    if (!crafting) return origin(results, numCrafts);
    try {
        Player& player = crafting->mPlayer;
        if (player.isClientSide()) return origin(results, numCrafts);
        auto level = ll::service::getLevel();
        if (!level) return origin(results, numCrafts);
        auto const* recipe = level->getRecipes().getRecipeByNetId(crafting->mRecipeNetId);
        if (!recipe) return origin(results, numCrafts);
        std::string id = *recipe->mRecipeId;
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        if (!rc::internal::isSpecial(id)) return origin(results, numCrafts);
        auto grid   = rc::internal::gridOf(player, crafting->mWorkbench);
        auto result = rc::internal::assembleSpecial(id, grid.grid);
        if (!result || gf::game::items::isEmpty(*result)) {
            gf::log().info("recipes: special crafting {} refused for {}", id, player.getRealName());
            return ItemStackNetResult::Error;
        }
        auto remainder = rc::internal::remainderOf(id, grid.grid);
        if (!remainder.empty()) {
            auto uid = player.getOrCreateUniqueID();
            gf::interactions::nextTick([uid, remainder] {
                auto lvl = ll::service::getLevel();
                if (!lvl) return;
                auto* p = lvl->getPlayer(uid);
                if (!p) return;
                for (auto st : remainder)
                    if (!p->add(st)) p->drop(st, false);
                p->sendInventory(false);
            });
        }
        gf::log().info("recipes: special crafting {} for {}", id, player.getRealName());
        std::vector<ItemInstance> replaced{ItemInstance(*result)};
        return origin(replaced, numCrafts);
    } catch (std::exception const& e) {
        gf::log().error("recipes: special crafting failed: {}", e.what());
        return origin(results, numCrafts);
    }
}

namespace gf::recipes {
namespace {
class RecipesModule {
public:
    void install() {
        RecipesCraftActionHook::hook();
        RecipesInitResultsHook::hook();
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                s.nativeDirty = true;
            });
            game::db::onWorldClosed([] {
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                s.nativeRecipes.clear();
                s.nativeDirty = true;
                reindex(s);
            });
        }
    }
    void uninstall() {
        RecipesInitResultsHook::unhook();
        RecipesCraftActionHook::unhook();
    }
};
} // namespace
} // namespace gf::recipes

GF_REGISTER_MODULE_ORDERED(gf::recipes::RecipesModule, gf::ModuleOrder::Contracts);

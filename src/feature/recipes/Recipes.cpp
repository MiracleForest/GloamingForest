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
// - special (code-driven) crafting: the crafting hook asks the matcher / assembler of the BP recipe
//   the client crafted (the request's recipe net id; a BP recipe with the special's identifier
//   shows the shape to vanilla clients; variable layouts: ext/item/Recipes.h), never the Java data
//   recipe of the same id, and puts the remainders back into the grid.
// The data's vanilla item ids are Java's (minecraft:cobweb, dead_bush, item_frame, chain, ...): the
// built-in loaders hold the game's Bedrock ids (tags/JavaIds.h; ext::parseIngredient / itemId for
// owners' loaders); a recipe's `data` keeps the file's JSON as it is.
// Data recipes with "neoforge:conditions" load only when the conditions hold (NeoForge's
// ConditionalOps; TF's uncrafting_table_enabled, mod_loaded for the jeed integration recipes).
// The recipe pointers the queries return stay valid for the process: a data reload or a new world
// keeps the previous generation of recipes alive instead of freeing it.
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
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/InventorySlotPacketPayload.h"
#include "mc/world/ContainerID.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/ItemStackRequestActionType.h"
#include "mc/world/inventory/network/TypedServerNetId.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftBase.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftRecipeOptional.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemDescriptor.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/RecipeIngredient.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "feature/common/Interactions.h"
#include "feature/ext/item/Recipes.h"
#include "feature/item/DataFiles.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/tags/JavaIds.h"
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
    /// Earlier generations (a data reload, a new world): the recipe pointers the queries handed
    /// out stay valid for the process (a deque's move keeps its elements where they are).
    std::vector<std::unique_ptr<std::deque<CraftingRecipe>>>   retired;
    std::map<std::string, std::string>                         layouts; ///< BP recipe id -> special id (ext::addSpecialLayout)
    std::unordered_map<std::string, CraftingRecipe const*>     byId;     ///< data wins over the game's recipe of an id
    std::unordered_map<std::string, CraftingRecipe const*>     nativeById; ///< the game's recipes only (BP identifiers)
    std::unordered_map<std::string, std::vector<CraftingRecipe const*>> byResult;
    std::unordered_map<std::string, std::vector<CraftingRecipe const*>> byType;
    std::map<std::string, Special>                             specials;
    std::map<std::string, ext::ConditionFn>                    conditions; ///< registered condition types
    std::map<std::string, Json>                                testData;   ///< selftests: data recipes by id
    std::map<std::string, CraftingRecipe>                      testNative; ///< selftests: stand-ins for BP recipes
    std::set<std::string>                                      noLoader;   ///< recipe types without a loader (warned once)
    bool levelConditions = false; ///< a condition was evaluated without a level (tag_empty): again at world ready
    bool                                                       nativeDirty = true;
    bool                                                       dataLoaded  = false;
    bool                                                       dataDirty   = false; ///< a loader came after the data
};

Store& store() {
    static Store s;
    return s;
}

/// Keeps a generation of recipes alive (lock held) and empties `list` for the next one.
void retire(Store& s, std::deque<CraftingRecipe>& list) {
    if (!list.empty()) s.retired.push_back(std::make_unique<std::deque<CraftingRecipe>>(std::move(list)));
    list.clear();
}

// ---- Java data ---------------------------------------------------------------------------------------
// The data names vanilla items by Java id (minecraft:cobweb, lily_pad, slime_block, item_frame,
// dead_bush, chain, stone_slab): ingredients take every Bedrock id of one, results its main one
// (tags/JavaIds.h). Tags stay tag ids: the tag runtime renames their members.

/// A Java item id of the data as the game's item id.
std::string bedrockItem(std::string const& javaId) { return tags::javaids::primary("item", data::withNamespace(javaId)); }

/// Adds the Bedrock ids of a Java item id to an ingredient (once each).
void addItem(Ingredient& out, std::string const& javaId) {
    for (auto& id : tags::javaids::bedrock("item", data::withNamespace(javaId)))
        if (std::find(out.items.begin(), out.items.end(), id) == out.items.end()) out.items.push_back(std::move(id));
}

std::string idField(Json const& j) {
    if (j.is_string()) return bedrockItem(j.get<std::string>());
    if (!j.is_object()) return {};
    for (auto const* k : {"id", "item"})
        if (auto it = j.find(k); it != j.end() && it->is_string()) return bedrockItem(it->get<std::string>());
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
        else addItem(out, s);
        return;
    }
    if (!j.is_object()) return;
    if (auto it = j.find("item"); it != j.end() && it->is_string()) addItem(out, it->get<std::string>());
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
        // a CustomRecipe: getResultItem is EMPTY on Java (the uncrafting table never sees it); the
        // gear the modifier goes on is data["gear_item"]
        if (!parsePattern(j.value("pattern", Json::object()), r)) return std::nullopt;
        r.data["gear_item"] = travellersItemOf(r.ingredients);
    } else if (t == "twilightforest:travellers_gear_modifier_shapeless_recipe") {
        r.ingredients       = parseList(j.value("ingredients", Json::array()));
        r.data["gear_item"] = travellersItemOf(r.ingredients);
    } else if (t == "twilightforest:scepter_repair") {
        // ScepterRepairRecipe: the scepter plus its repair ingredients restore `durability`
        auto scepter = bedrockItem(j.value("scepter", std::string()));
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
    s.nativeById.clear();
    s.byResult.clear();
    s.byType.clear();
    for (auto const& r : s.nativeRecipes) {
        index(s, r);
        s.nativeById[r.id] = &r;
    }
    for (auto const& r : s.dataRecipes) index(s, r); // data wins over a game recipe of the same id
    // the game recipe of an id the data has too: drop it from the lists
    for (auto& [item, list] : s.byResult)
        std::erase_if(list, [&](CraftingRecipe const* r) { return s.byId[r->id] != r; });
    for (auto& [type, list] : s.byType) std::erase_if(list, [&](CraftingRecipe const* r) { return s.byId[r->id] != r; });
}

// ---- NeoForge conditions (net.neoforged.neoforge.common.conditions) -----------------------------------

bool conditionHolds(Store& s, Json const& c);

bool allHold(Store& s, Json const& list) {
    if (list.is_object()) return conditionHolds(s, list);
    if (!list.is_array()) return true;
    for (auto const& c : list)
        if (!conditionHolds(s, c)) return false;
    return true;
}

/// One condition (lock held): the registered test of its type, else NeoForge's own and TF's.
bool conditionHolds(Store& s, Json const& c) {
    if (!c.is_object()) return true;
    auto type = data::withNamespace(c.value("type", std::string()));
    if (auto it = s.conditions.find(type); it != s.conditions.end()) {
        try {
            return it->second(c);
        } catch (std::exception const& e) {
            log().warn("recipes: condition {} failed: {}", type, e.what());
            return false;
        }
    }
    if (type == "neoforge:true") return true;
    if (type == "neoforge:false") return false;
    if (type == "neoforge:not") return !conditionHolds(s, c.value("value", Json::object()));
    if (type == "neoforge:and") return allHold(s, c.value("values", Json::array()));
    if (type == "neoforge:or") {
        auto values = c.value("values", Json::array());
        if (!values.is_array()) return false;
        for (auto const& v : values)
            if (conditionHolds(s, v)) return true;
        return false;
    }
    if (type == "neoforge:mod_loaded") {
        // the platform runs vanilla and TF only (TF's jeed integration recipes stay out)
        auto mod = c.value("modid", std::string());
        return mod == "minecraft" || mod == "neoforge" || mod == "twilightforest";
    }
    if (type == "neoforge:item_exists") {
        auto item = data::withNamespace(c.value("item", std::string()));
        return item.starts_with("minecraft:") || item.starts_with("twilightforest:") || item.starts_with("gf_test:");
    }
    if (type == "neoforge:tag_empty") {
        // the vanilla tag bridge enumerates the game's items: evaluated again once a world is ready
        if (!ll::service::getLevel()) s.levelConditions = true;
        return tags::members(tags::Registry::Item, data::withNamespace(c.value("tag", std::string()))).empty();
    }
    // UncraftingTableCondition: !TFConfig.disableEntireTable, TF's default until U13 registers it
    if (type == "twilightforest:uncrafting_table_enabled") return true;
    static std::set<std::string> warned;
    if (warned.insert(type).second) log().warn("recipes: unknown recipe condition type {}: the recipe does not load", type);
    return false;
}

std::optional<CraftingRecipe> loadOne(Store& s, std::string const& id, Json const& j) {
    if (!j.is_object()) return std::nullopt;
    // ConditionalOps: a recipe whose conditions fail is not there (no warning, as on NeoForge)
    if (auto it = j.find("neoforge:conditions"); it != j.end() && !allHold(s, *it)) return std::nullopt;
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
    if (!r && s.noLoader.insert(type).second) log().warn("recipes: no loader for recipe type {} (first: {})", type, id);
    return r;
}

void loadData() {
    auto&                     s = store();
    std::lock_guard           lock(s.mutex);
    retire(s, s.dataRecipes);
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
    for (auto const& [id, j] : s.testData) add(id, j);
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
    retire(s, s.nativeRecipes);
    s.nativeDirty = false;
    for (auto const& [id, r] : s.testNative) s.nativeRecipes.push_back(r);
    auto level = ll::service::getLevel();
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

namespace ext {
void addSpecialLayout(std::string const& specialId, std::string const& bpRecipeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.layouts[data::withNamespace(bpRecipeId)] = data::withNamespace(specialId);
}

void registerCondition(std::string const& typeId, ConditionFn test) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.conditions[data::withNamespace(typeId)] = std::move(test);
    if (s.dataLoaded) s.dataDirty = true; // the data's conditions are evaluated again
}

Ingredient parseIngredient(Json const& ingredient) { return one(ingredient); }

std::string itemId(Json const& result) { return idField(result); }
} // namespace ext

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
    out.firstSlot   = first;
    for (auto const& st : out.stacks) out.grid.slots.push_back(&st);
    return out;
}

namespace {
/// The special recipe of a BP recipe id (its own id or a layout of ext::addSpecialLayout), lock held.
Special const* specialOf(Store& s, std::string const& recipeId) {
    auto id = data::withNamespace(recipeId);
    if (auto it = s.specials.find(id); it != s.specials.end()) return &it->second;
    if (auto l = s.layouts.find(id); l != s.layouts.end())
        if (auto it = s.specials.find(l->second); it != s.specials.end()) return &it->second;
    return nullptr;
}
} // namespace

bool anySpecial() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return !s.specials.empty();
}

namespace {
std::string idOfStack(ItemStack const* s) { return s && !game::items::isEmpty(*s) ? game::items::idOf(*s) : std::string(); }

bool fitsShaped(CraftingRecipe const& r, CraftingGrid const& grid, bool mirrored) {
    int minX = grid.width, minY = grid.height, maxX = -1, maxY = -1;
    for (int y = 0; y < grid.height; ++y)
        for (int x = 0; x < grid.width; ++x) {
            auto i = static_cast<std::size_t>(y * grid.width + x);
            if (i >= grid.slots.size() || idOfStack(grid.slots[i]).empty()) continue;
            minX = std::min(minX, x), minY = std::min(minY, y), maxX = std::max(maxX, x), maxY = std::max(maxY, y);
        }
    if (maxX < 0) return false;
    // the recipe's own empty rim (a pattern with blank edges) is part of its shape
    if (r.width < maxX - minX + 1 || r.height < maxY - minY + 1) return false;
    for (int ox = std::max(0, maxX - r.width + 1); ox <= std::min(minX, grid.width - r.width); ++ox)
        for (int oy = std::max(0, maxY - r.height + 1); oy <= std::min(minY, grid.height - r.height); ++oy) {
            bool ok = true;
            for (int y = 0; ok && y < grid.height; ++y)
                for (int x = 0; ok && x < grid.width; ++x) {
                    auto id = idOfStack(grid.slots[static_cast<std::size_t>(y * grid.width + x)]);
                    int  rx = x - ox, ry = y - oy;
                    if (rx < 0 || ry < 0 || rx >= r.width || ry >= r.height) {
                        ok = id.empty();
                        continue;
                    }
                    if (mirrored) rx = r.width - 1 - rx;
                    auto i = static_cast<std::size_t>(ry * r.width + rx);
                    ok     = i < r.ingredients.size() && internal::matches(r.ingredients[i], id);
                }
            if (ok) return true;
        }
    return false;
}

bool assign(std::vector<std::string> const& ids, std::vector<Ingredient> const& ingredients, std::vector<bool>& used, std::size_t i) {
    if (i == ids.size()) return true;
    for (std::size_t k = 0; k < ingredients.size(); ++k) {
        if (used[k] || !internal::matches(ingredients[k], ids[i])) continue;
        used[k] = true;
        if (assign(ids, ingredients, used, i + 1)) return true;
        used[k] = false;
    }
    return false;
}

bool fitsShapeless(CraftingRecipe const& r, CraftingGrid const& grid) {
    std::vector<std::string> ids;
    for (auto const* s : grid.slots)
        if (auto id = idOfStack(s); !id.empty()) ids.push_back(std::move(id));
    std::vector<Ingredient> ingredients;
    for (auto const& i : r.ingredients)
        if (!i.empty()) ingredients.push_back(i);
    if (ids.empty() || ids.size() != ingredients.size()) return false;
    std::vector<bool> used(ingredients.size(), false);
    return assign(ids, ingredients, used, 0);
}

std::map<std::int64_t, std::string>& routes() {
    static std::map<std::int64_t, std::string> m;
    return m;
}
} // namespace

bool fits(CraftingRecipe const& recipe, CraftingGrid const& grid) {
    if (recipe.width > 0 && recipe.height > 0) return fitsShaped(recipe, grid, false) || fitsShaped(recipe, grid, true);
    return fitsShapeless(recipe, grid);
}

std::vector<std::string> specialCandidates(std::string const& resultItemId, CraftingGrid const& grid) {
    std::vector<std::pair<std::string, CraftingRecipe const*>> found;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        ensureNative();
        auto result = data::withNamespace(resultItemId);
        auto native = [&](std::string const& bpId) -> CraftingRecipe const* {
            auto it = s.nativeById.find(bpId);
            return it != s.nativeById.end() && it->second->resultItem == result ? it->second : nullptr;
        };
        for (auto const& [id, sp] : s.specials)
            if (auto const* r = native(id)) found.emplace_back(id, r);
        for (auto const& [bp, id] : s.layouts)
            if (s.specials.contains(id))
                if (auto const* r = native(bp)) found.emplace_back(bp, r);
    }
    // the client crafted a BP recipe the server checked against the grid: only those that fit
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<std::string> fitting;
    for (auto const& [id, r] : found)
        if (fits(*r, grid)) fitting.push_back(id);
    return fitting;
}

std::string resolveSpecial(std::string const& bpRecipeId, std::string const& resultItemId, CraftingGrid const& grid, bool* refused) {
    if (refused) *refused = false;
    if (!bpRecipeId.empty()) {
        if (!isSpecial(bpRecipeId)) return {};
        Matcher matcher;
        {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            if (auto const* sp = specialOf(s, bpRecipeId)) matcher = sp->matcher;
        }
        if (matcher && !matcher(grid)) {
            if (refused) *refused = true;
            return {};
        }
        return data::withNamespace(bpRecipeId);
    }
    bool rejected = false;
    for (auto const& id : specialCandidates(resultItemId, grid)) {
        Matcher matcher;
        {
            auto&           s = store();
            std::lock_guard lock(s.mutex);
            if (auto const* sp = specialOf(s, id)) matcher = sp->matcher;
        }
        if (!matcher || matcher(grid)) return id;
        rejected = true;
    }
    // every special recipe that fits the grid said no (e.g. a gate such as U13's
    // disableUncraftingTable): the craft is refused, never left to the BP recipe's static result
    if (refused && rejected) *refused = true;
    return {};
}

std::string lastSpecialRoute(std::int64_t playerUid) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = routes().find(playerUid);
    return it == routes().end() ? std::string() : it->second;
}

CraftingRecipe const* nativeById(std::string const& recipeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureNative();
    auto it = s.nativeById.find(data::withNamespace(recipeId));
    return it == s.nativeById.end() ? nullptr : it->second;
}

void setTestDataRecipe(std::string const& id, Json const& recipe) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    if (recipe.is_null()) s.testData.erase(data::withNamespace(id));
    else s.testData[data::withNamespace(id)] = recipe;
    loadData();
}

void setTestNativeRecipe(CraftingRecipe const& recipe, bool remove) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    if (remove) s.testNative.erase(recipe.id);
    else s.testNative[recipe.id] = recipe;
    s.nativeDirty = true;
    convertNative(s);
    reindex(s);
}

void unregisterCondition(std::string const& typeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.conditions.erase(data::withNamespace(typeId));
    if (s.dataLoaded) s.dataDirty = true;
}

bool conditionsHold(Json const& conditions) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return allHold(s, conditions);
}

std::set<std::string> typesWithoutLoader() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureData();
    return s.noLoader;
}

bool isSpecial(std::string const& recipeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return specialOf(s, recipeId) != nullptr;
}

std::optional<ItemStack> assembleSpecial(std::string const& recipeId, CraftingGrid const& grid) {
    Special sp;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto const*     found = specialOf(s, recipeId);
        if (!found) return std::nullopt;
        sp = *found;
    }
    if (sp.matcher && !sp.matcher(grid)) return std::nullopt;
    if (!sp.assembler) return std::nullopt;
    return sp.assembler(grid);
}

std::vector<ItemStack> remaindersBySlot(std::string const& recipeId, CraftingGrid const& grid) {
    Remainder rem;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto const*     found = specialOf(s, recipeId);
        if (!found || !found->remainder) return {};
        rem = found->remainder;
    }
    auto out = rem(grid);
    out.resize(grid.slots.size(), ItemStack::EMPTY_ITEM());
    return out;
}

std::vector<ItemStack> remainderOf(std::string const& recipeId, CraftingGrid const& grid) {
    auto out = remaindersBySlot(recipeId, grid);
    std::erase_if(out, [](ItemStack const& st) { return game::items::isEmpty(st); });
    return out;
}

void placeRemainders(Player& player, int firstSlot, std::vector<ItemStack> const& remainders) {
    auto& ui      = player.mPlayerUIContainer.get();
    bool  changed = false;
    for (std::size_t i = 0; i < remainders.size(); ++i) {
        auto rem = remainders[i];
        if (game::items::isEmpty(rem)) continue;
        int  slot = firstSlot + static_cast<int>(i);
        auto cur  = ui.getItem(slot);
        if (game::items::isEmpty(cur)) {
            rem.serverInitNetId();
            ui.setItem(slot, rem);
        } else if (game::items::sameItemSameData(cur, rem)) {
            auto grown = game::items::withCount(cur, game::items::countOf(cur) + game::items::countOf(rem));
            grown.serverInitNetId();
            ui.setItem(slot, grown);
        } else {
            if (!player.add(rem)) player.drop(rem, false);
            continue;
        }
        changed = true;
        if (!player.isSimulatedPlayer()) {
            InventorySlotPacket packet(InventorySlotPacketPayload(ContainerID::PlayerOnlyUi, slot, ui.getItem(slot)));
            packet.sendTo(player);
        }
    }
    if (changed) player.sendInventory(false);
}

std::map<std::int64_t, int>& craftCounts() {
    static std::map<std::int64_t, int> m;
    return m;
}

int specialCraftCount(std::int64_t playerUid) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = craftCounts().find(playerUid);
    return it == craftCounts().end() ? 0 : it->second;
}

void unregisterSpecial(std::string const& recipeId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            id = data::withNamespace(recipeId);
    s.specials.erase(id);
    std::erase_if(s.layouts, [&](auto const& l) { return l.second == id || l.first == id; });
}

std::size_t dataRecipeCount() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.dataRecipes.size();
}

std::vector<CraftingRecipe const*> dataRecipes() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureData();
    std::vector<CraftingRecipe const*> out;
    out.reserve(s.dataRecipes.size());
    for (auto const& r : s.dataRecipes) out.push_back(&r);
    return out;
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
/// What the crafting table's craft handler saw of the item stack request being processed on this
/// thread, as copied values (no handler pointer is ever followed): set by its craft actions (the
/// CraftRecipe action names the BP recipe by its net id), cleared at the end of the request.
struct CraftState {
    void const* owner = nullptr; ///< the request's ItemStackRequestActionCraftHandler (compared only)
    std::string recipeId;        ///< the BP recipe the client crafted ("" unknown)
    std::string resultItem;      ///< that recipe's result item
    bool        workbench = false;
    bool        gridCraft = false; ///< the request crafts a recipe of the grid (CraftRecipe / CraftRecipeAuto)
};
thread_local CraftState gCraft;

/// The recipe a CraftRecipe / CraftRecipeAuto action crafts (its id and result item). The action is
/// ItemStackRequestActionCraft<RecipeNetId, 12 | 13>: the net id follows the craft base as in
/// ItemStackRequestActionCraftRecipeOptional (the same base and first member); a recipe whose
/// result is not what the craft makes is not taken (see the results hook).
std::pair<std::string, std::string> recipeOfAction(ItemStackRequestActionCraftBase const& action) {
    auto type = static_cast<ItemStackRequestActionType>(action.mActionType);
    if (type != ItemStackRequestActionType::CraftRecipe && type != ItemStackRequestActionType::CraftRecipeAuto) return {};
    auto level = ll::service::getLevel();
    if (!level) return {};
    RecipeNetId const& netId  = static_cast<ItemStackRequestActionCraftRecipeOptional const&>(action).mRecipeNetId;
    auto const*        recipe = level->getRecipes().getRecipeByNetId(netId);
    if (!recipe) return {};
    std::string id = *recipe->mRecipeId;
    if (id.find(':') == std::string::npos) id = "minecraft:" + id;
    auto const& results = recipe->getResultItems();
    return {id, results.empty() || results.front().isNull() ? std::string() : results.front().getTypeName()};
}
} // namespace

} // namespace gf::recipes

namespace rc = gf::recipes;

// The crafting table's craft actions: which BP recipe the request crafts, in which grid.
LL_TYPE_INSTANCE_HOOK(
    RecipesCraftActionHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerCrafting,
    &CraftHandlerCrafting::$_handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& action
) {
    try {
        void const* owner = &static_cast<ItemStackRequestActionCraftHandler&>(mCraftRequestHandler);
        if (rc::gCraft.owner != owner) rc::gCraft = {};
        rc::gCraft.owner     = owner;
        rc::gCraft.workbench = mWorkbench;
        auto type            = static_cast<ItemStackRequestActionType>(action.mActionType);
        if (type == ItemStackRequestActionType::CraftRecipe || type == ItemStackRequestActionType::CraftRecipeAuto) {
            auto [id, result]     = rc::recipeOfAction(action);
            rc::gCraft.recipeId   = std::move(id);
            rc::gCraft.resultItem = std::move(result);
            rc::gCraft.gridCraft  = true;
        }
    } catch (...) {
        rc::gCraft = {};
    }
    return origin(action);
}

// The end of an item stack request: nothing of it is kept for the next one.
LL_TYPE_INSTANCE_HOOK(
    RecipesEndRequestHook,
    ll::memory::HookPriority::Normal,
    ItemStackRequestActionCraftHandler,
    &ItemStackRequestActionCraftHandler::endRequest,
    ItemStackNetResult,
    ItemStackNetResult currentResult
) {
    if (rc::gCraft.owner == this) rc::gCraft = {};
    return origin(currentResult);
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
    try {
        Player& player = mPlayer;
        if (player.isClientSide() || results.size() != 1 || results.front().isNull() || !rc::internal::anySpecial()) return origin(results, numCrafts);
        // only a grid craft of the crafting screen (CraftHandlerCrafting's CraftRecipe / CraftRecipeAuto
        // of this request): a trade, the loom, the smithing table, the anvil or a creative pick of
        // the same item never takes the grid's ingredients
        if (rc::gCraft.owner != this || !rc::gCraft.gridCraft) return origin(results, numCrafts);
        auto resultItem = results.front().getTypeName();
        // the BP recipe the request crafted: only when it makes this result (a net id read from an
        // unexpected action layout names another recipe or none, and the result decides then)
        std::string bp        = rc::gCraft.resultItem == resultItem ? rc::gCraft.recipeId : std::string();
        bool        workbench = rc::gCraft.workbench;
        auto        grid      = rc::internal::gridOf(player, workbench);
        bool refused = false;
        auto id      = rc::internal::resolveSpecial(bp, resultItem, grid.grid, &refused);
        if (refused) {
            gf::log().info("recipes: special crafting {} refused for {}", bp.empty() ? resultItem : bp, player.getRealName());
            return ItemStackNetResult::Error;
        }
        if (id.empty()) return origin(results, numCrafts);
        auto result = rc::internal::assembleSpecial(id, grid.grid);
        if (!result || gf::game::items::isEmpty(*result)) {
            if (bp.empty()) return origin(results, numCrafts); // a candidate by its result only
            gf::log().info("recipes: special crafting {} refused for {}", id, player.getRealName());
            return ItemStackNetResult::Error;
        }
        // Java's ResultSlot#onTake: each slot's remainder (getRemainingItems) goes back into its
        // grid slot when the craft emptied it (or onto the same item there), else into the
        // inventory or onto the ground. The request consumes the grid after this point: the
        // remainders are placed on the next tick and sent to the client. A shift-click
        // (numCrafts > 1) gives one remainder per craft; the grid takes the first.
        auto remainders = rc::internal::remaindersBySlot(id, grid.grid);
        bool any        = std::any_of(remainders.begin(), remainders.end(), [](ItemStack const& st) { return !gf::game::items::isEmpty(st); });
        if (any) {
            auto uid    = player.getOrCreateUniqueID();
            int  crafts = std::max<int>(1, numCrafts);
            int  first  = grid.firstSlot;
            gf::interactions::nextTick([uid, remainders, crafts, first] {
                auto lvl = ll::service::getLevel();
                if (!lvl) return;
                auto* p = lvl->getPlayer(uid);
                if (!p) return;
                rc::internal::placeRemainders(*p, first, remainders);
                for (int n = 1; n < crafts; ++n)
                    for (auto st : remainders)
                        if (!gf::game::items::isEmpty(st) && !p->add(st)) p->drop(st, false);
                p->sendInventory(false);
                gf::log().info("recipes: special crafting remainder placed for {}", p->getRealName());
            });
        }
        char const* route = bp.empty() ? "result" : "request";
        {
            auto&           s = rc::store();
            std::lock_guard lock(s.mutex);
            auto            uid = player.getOrCreateUniqueID().rawID;
            ++rc::internal::craftCounts()[uid];
            rc::internal::routes()[uid] = route;
        }
        gf::log().info("recipes: special crafting {} for {} (found by the {})", id, player.getRealName(), route);
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
        if (int r = RecipesCraftActionHook::hook(); r != 0) log().error("recipes: the crafting table hook failed ({})", r);
        if (int r = RecipesInitResultsHook::hook(); r != 0) log().error("recipes: the craft results hook failed ({})", r);
        if (int r = RecipesEndRequestHook::hook(); r != 0) log().error("recipes: the request end hook failed ({})", r);
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                s.nativeDirty = true;
                if (s.levelConditions && s.dataLoaded) {
                    s.levelConditions = false;
                    s.dataDirty       = true;
                }
            });
            game::db::onWorldClosed([] {
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                retire(s, s.nativeRecipes);
                s.nativeDirty = true;
                reindex(s);
            });
        }
    }
    void uninstall() {
        RecipesEndRequestHook::unhook();
        RecipesInitResultsHook::unhook();
        RecipesCraftActionHook::unhook();
        gCraft = {};
    }
};
} // namespace
} // namespace gf::recipes

GF_REGISTER_MODULE_ORDERED(gf::recipes::RecipesModule, gf::ModuleOrder::Contracts);

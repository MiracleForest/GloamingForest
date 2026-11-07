// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/recipe/ScepterRepairRecipe.java
//   src/main/java/twilightforest/item/recipe/EssenceRepairRecipe.java
//   src/main/java/twilightforest/item/recipe/CasketRepairRecipe.java
//   src/main/java/twilightforest/item/recipe/EmperorsClothRecipe.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U9's four custom recipe types (rule R4) as F9 special crafting recipes. The data recipes
// (twilightforest:scepter_repair JSON: scepter, repair_ingredients, durability) are loaded by the
// type loader registered here; the crafting table offers them through BP recipes naming each layout
// (assets/pack/bp/recipes/artifacts, feature/ext/item/Recipes.h):
//   - equipment/<scepter>_<n>: a damaged scepter and n of its repair item (1..8); the zombie
//     scepter's two ingredients as equipment/zombie_scepter_<strength potion>;
//   - essence_repair_recipe_<scepter>: a damaged scepter and an exanimate essence;
//   - casket_repair_recipe: a damaged casket and a charm of keeping III;
//   - emperors_cloth_recipe_{armor,elytra}: the cloth and one applicable item.
#include <algorithm>
#include <mutex>

#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/ext/item/Recipes.h"
#include "feature/ext/item/Tags.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace items = game::items;
namespace rc    = gf::recipes;

namespace {

constexpr char kScepterRepairType[] = "twilightforest:scepter_repair";
constexpr char kEssenceRepair[]     = "twilightforest:essence_repair_recipe";
constexpr char kCasketRepair[]      = "twilightforest:casket_repair_recipe";
constexpr char kClothRecipe[]       = "twilightforest:emperors_cloth_recipe";

int potionAuxOf(std::string const& javaId) {
    for (int aux = 0; aux < 64; ++aux)
        if (auto const* p = core::artifacts::potionByAux(aux); p && p->javaId == javaId) return aux;
    return -1;
}

/// One Java ingredient alternative of the recipe JSON.
std::optional<IngredientAlt> parseAlt(Json const& j) {
    if (!j.is_object()) return std::nullopt;
    IngredientAlt a;
    if (j.contains("tag")) {
        a.tag = j["tag"].get<std::string>();
        return a;
    }
    if (j.value("type", std::string{}) == "neoforge:components") {
        a.item = gf::tags::ext::bedrockId("item", j.value("items", std::string{}));
        if (auto c = j.find("components"); c != j.end() && c->contains("minecraft:potion_contents"))
            a.aux = potionAuxOf((*c)["minecraft:potion_contents"].value("potion", std::string{}));
        return a;
    }
    if (j.contains("item")) {
        a.item = gf::tags::ext::bedrockId("item", j["item"].get<std::string>());
        return a;
    }
    return std::nullopt;
}

std::vector<IngredientAlt> parseIngredient(Json const& j) {
    std::vector<IngredientAlt> out;
    if (j.is_array()) {
        for (auto const& e : j)
            if (auto a = parseAlt(e)) out.push_back(*a);
    } else if (auto a = parseAlt(j)) {
        out.push_back(*a);
    }
    return out;
}

std::optional<ScepterRepair> parseRepair(std::string const& id, Json const& j) {
    ScepterRepair r;
    r.id         = id;
    r.scepter    = j.value("scepter", std::string{});
    r.durability = j.value("durability", 0);
    for (auto const& e : j.value("repair_ingredients", Json::array())) {
        auto alts = parseIngredient(e);
        if (alts.empty()) return std::nullopt;
        r.ingredients.push_back(std::move(alts));
    }
    if (r.scepter.empty() || r.ingredients.empty()) return std::nullopt;
    return r;
}

std::vector<ItemStack const*> filled(rc::CraftingGrid const& g) {
    std::vector<ItemStack const*> out;
    for (auto const* s : g.slots)
        if (s && !items::isEmpty(*s)) out.push_back(s);
    return out;
}

bool isDamagedScepter(ItemStack const& s, std::string const& id) { return items::idOf(s) == id && s.getDamageValue() > 0; }

/// ScepterRepairRecipe#matches (+ #assemble into `out`).
bool scepterRepairMatch(ScepterRepair const& r, rc::CraftingGrid const& g, ItemStack* out) {
    auto              stacks  = filled(g);
    ItemStack const*  scepter = nullptr;
    int               n       = 0;
    if (r.ingredients.size() == 1) {
        for (auto const* s : stacks) {
            if (isDamagedScepter(*s, r.scepter)) {
                if (scepter) return false;
                scepter = s;
            } else if (ingredientMatches(r.ingredients.front(), *s)) {
                ++n;
            } else {
                return false;
            }
        }
        if (!scepter || !core::artifacts::scepterRepairMatches(scepter->getDamageValue(), n, r.durability)) return false;
    } else {
        std::vector<ItemStack const*> rest;
        for (auto const* s : stacks) {
            if (isDamagedScepter(*s, r.scepter)) {
                if (scepter) return false;
                scepter = s;
            } else {
                rest.push_back(s);
            }
        }
        if (!scepter || rest.size() != r.ingredients.size()) return false;
        // StackedContents#canCraft: each ingredient takes a distinct stack
        std::vector<std::size_t> order(rest.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        bool ok = false;
        do {
            bool all = true;
            for (std::size_t i = 0; i < order.size() && all; ++i) all = ingredientMatches(r.ingredients[i], *rest[order[i]]);
            ok = all;
        } while (!ok && std::next_permutation(order.begin(), order.end()));
        if (!ok) return false;
    }
    if (out) {
        *out = *scepter;
        out->setDamageValue(static_cast<short>(core::artifacts::scepterRepairResult(scepter->getDamageValue(), r.ingredients.size() == 1 ? n : 1, r.durability)));
    }
    return true;
}

/// EssenceRepairRecipe.
bool essenceMatch(rc::CraftingGrid const& g, ItemStack* out) {
    ItemStack const* scepter = nullptr;
    bool             essence = false;
    for (auto const* s : filled(g)) {
        auto id = items::idOf(*s);
        if (gf::tags::has(gf::tags::Registry::Item, "twilightforest:scepters", id) && s->isDamaged()) {
            if (scepter) return false;
            scepter = s;
        } else if (id == kEssence) {
            if (essence) return false;
            essence = true;
        } else {
            return false;
        }
    }
    if (!scepter || !essence) return false;
    if (out) {
        *out = *scepter;
        out->setDamageValue(0);
    }
    return true;
}

int casketDamage(ItemStack const& s) {
    auto d = items::getData(s, kDataCasketDamage);
    return d.is_number_integer() ? d.get<int>() : 0;
}

/// CasketRepairRecipe.
bool casketMatch(rc::CraftingGrid const& g, ItemStack* out) {
    ItemStack const* casket = nullptr;
    bool             charm  = false;
    for (auto const* s : filled(g)) {
        auto id = items::idOf(*s);
        if (id == kCasket && casketDamage(*s) > 0) {
            if (casket) return false;
            casket = s;
        } else if (id == kKeeping3) {
            if (charm) return false;
            charm = true;
        } else {
            return false;
        }
    }
    if (!casket || !charm) return false;
    if (out) {
        *out = items::withCount(*casket, 1);
        int d = casketDamage(*casket) - 1;
        items::setData(*out, kDataCasketDamage, d > 0 ? Json(d) : Json());
    }
    return true;
}

bool clothApplicable(ItemStack const& s) {
    auto id = items::idOf(s);
    return gf::tags::has(gf::tags::Registry::Item, "twilightforest:emperors_cloth_applicable", id) || gf::tags::has(gf::tags::Registry::Item, "c:armors", id)
        || id == "minecraft:elytra";
}

/// EmperorsClothRecipe.
bool clothMatch(rc::CraftingGrid const& g, ItemStack* out) {
    bool             cloth = false;
    ItemStack const* item  = nullptr;
    for (auto const* s : filled(g)) {
        if (items::idOf(*s) == kEmperorsCloth && !cloth) {
            cloth = true;
        } else if (!item) {
            // no crafting remainder among armour and elytra
            if (clothApplicable(*s) && !hasCloth(*s)) item = s;
            else return false;
        } else {
            return false;
        }
    }
    if (!cloth || !item) return false;
    if (out) {
        *out = items::withCount(*item, 1);
        items::setData(*out, kDataCloth, Json::object()); // Unit.INSTANCE
    }
    return true;
}

} // namespace

bool ingredientMatches(std::vector<IngredientAlt> const& alts, ItemStack const& stack) {
    if (items::isEmpty(stack)) return false;
    auto id = items::idOf(stack);
    for (auto const& a : alts) {
        if (!a.tag.empty()) {
            if (gf::tags::has(gf::tags::Registry::Item, a.tag, id)) return true;
            continue;
        }
        if (a.item == id && (a.aux < 0 || a.aux == stack.getAuxValue())) return true;
    }
    return false;
}

std::vector<ScepterRepair> scepterRepairs() {
    // parsed once per set of loaded recipes (a reload changes the recipe pointers)
    static std::mutex                       mutex;
    static std::vector<rc::CraftingRecipe const*> seen;
    static std::vector<ScepterRepair>        cache;
    auto                                     now = rc::ofType(kScepterRepairType);
    std::lock_guard                          lock(mutex);
    if (now != seen) {
        seen = now;
        cache.clear();
        for (auto const* r : now)
            if (auto p = parseRepair(r->id, r->data)) cache.push_back(*p);
    }
    return cache;
}

namespace test {
std::optional<ItemStack> craft(std::string const& kind, std::vector<ItemStack const*> const& grid, std::string const& scepterRecipe) {
    rc::CraftingGrid g;
    g.slots = grid;
    ItemStack out;
    bool      ok = false;
    if (kind == "scepter") {
        for (auto const& r : scepterRepairs())
            if (r.id == scepterRecipe) ok = scepterRepairMatch(r, g, &out);
    } else if (kind == "essence") ok = essenceMatch(g, &out);
    else if (kind == "casket") ok = casketMatch(g, &out);
    else if (kind == "cloth") ok = clothMatch(g, &out);
    if (!ok) return std::nullopt;
    return out;
}
} // namespace test

void registerRecipes() {
    rc::registerType(kScepterRepairType, [](std::string const& id, Json const& j) -> std::optional<rc::CraftingRecipe> {
        auto r = parseRepair(id, j);
        if (!r) return std::nullopt;
        rc::CraftingRecipe out;
        out.id         = id;
        out.type       = kScepterRepairType;
        out.resultItem = r->scepter;
        out.data       = j;
        // F9's convention (and the uncrafting table's): the scepter, then its repair ingredients
        out.ingredients.push_back(rc::Ingredient{{r->scepter}, {}});
        for (auto const& ing : r->ingredients) {
            rc::Ingredient in;
            for (auto const& a : ing) {
                if (!a.tag.empty()) in.tag = a.tag;
                else in.items.push_back(a.item);
            }
            out.ingredients.push_back(in);
        }
        return out;
    });
    // CustomRecipe types without data fields
    for (char const* type : {kEssenceRepair, kCasketRepair, kClothRecipe}) {
        rc::registerType(type, [type](std::string const& id, Json const& j) -> std::optional<rc::CraftingRecipe> {
            rc::CraftingRecipe out;
            out.id   = id;
            out.type = type;
            out.data = j;
            return out;
        });
    }
    // the scepter repairs, one special recipe per data recipe
    static char const* const kScepters[] = {"twilight_scepter", "lifedrain_scepter", "zombie_scepter", "fortification_scepter"};
    for (char const* path : kScepters) {
        std::string id = std::string("twilightforest:equipment/") + path;
        rc::registerSpecialCrafting(
            id,
            [id](rc::CraftingGrid const& g) {
                for (auto const& r : scepterRepairs())
                    if (r.id == id) return scepterRepairMatch(r, g, nullptr);
                return false;
            },
            [id](rc::CraftingGrid const& g) {
                ItemStack out;
                for (auto const& r : scepterRepairs())
                    if (r.id == id) scepterRepairMatch(r, g, &out);
                return out;
            }
        );
        if (std::string(path) == "zombie_scepter") {
            for (char const* p : {"strength", "long_strength", "strong_strength"}) rc::ext::addSpecialLayout(id, id + "_" + p);
        } else {
            for (int n = 1; n <= 8; ++n) rc::ext::addSpecialLayout(id, id + "_" + std::to_string(n));
        }
        rc::ext::addSpecialLayout(kEssenceRepair, std::string(kEssenceRepair) + "_" + path);
    }
    rc::registerSpecialCrafting(kEssenceRepair, [](rc::CraftingGrid const& g) { return essenceMatch(g, nullptr); }, [](rc::CraftingGrid const& g) {
        ItemStack out;
        essenceMatch(g, &out);
        return out;
    });
    rc::registerSpecialCrafting(kCasketRepair, [](rc::CraftingGrid const& g) { return casketMatch(g, nullptr); }, [](rc::CraftingGrid const& g) {
        ItemStack out;
        casketMatch(g, &out);
        return out;
    });
    rc::registerSpecialCrafting(kClothRecipe, [](rc::CraftingGrid const& g) { return clothMatch(g, nullptr); }, [](rc::CraftingGrid const& g) {
        ItemStack out;
        clothMatch(g, &out);
        return out;
    });
    rc::ext::addSpecialLayout(kClothRecipe, std::string(kClothRecipe) + "_armor");
    rc::ext::addSpecialLayout(kClothRecipe, std::string(kClothRecipe) + "_elytra");
}

} // namespace gf::feature::artifacts



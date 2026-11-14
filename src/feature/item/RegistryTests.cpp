// Selftests of F9 (prefix "item.", no world positions): upstream's data names vanilla content by Java id
// (tags/JavaIds.h); the recipe, tag and data map runtimes must hold ids the game's block, item and
// entity registries know. item.recipes_java_ids reads upstream's own recipes (F1's import) where the
// renaming matters; item.java_ids_registry resolves every Bedrock id of the tables and every vanilla
// id the loaded recipes, tags and data maps produce.

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/ActorDefinitionGroup.h"
#include "mc/world/actor/ActorDefinitionPtr.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/datamaps/DataMaps.h"
#include "feature/datamaps/DataMapsInternal.h"
#include "feature/ext/item/Recipes.h"
#include "feature/ext/item/Tags.h"
#include "feature/recipes/Recipes.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/tags/JavaIds.h"
#include "feature/tags/TagsInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using JsonDoc = nlohmann::json;
namespace items = gf::game::items;
namespace rc    = gf::recipes;

/// Whether an ingredient of a recipe takes `itemId` (any slot).
bool takes(rc::CraftingRecipe const& r, std::string const& itemId) {
    return std::any_of(r.ingredients.begin(), r.ingredients.end(), [&](rc::Ingredient const& i) { return rc::internal::matches(i, itemId); });
}

/// Resolves ids in the game's registries ("block", "item", "entity_type"); an id the registry knows
/// only under another name (an alias) counts as unknown: stacks and blocks report the other name.
class Resolver {
public:
    /// "" when `id` is the registry's own id, else why not.
    std::string problem(std::string const& dir, std::string const& id) {
        auto key = dir + "|" + id;
        if (auto it = mCache.find(key); it != mCache.end()) return it->second;
        std::string why;
        if (dir == "item") {
            auto stack = items::make(id);
            if (!stack) why = "no item";
            else if (auto real = items::idOf(*stack); real != id) why = "an alias of the item " + real;
        } else if (dir == "block") {
            auto const* block = gf::game::blockWith(id);
            if (!block) why = "no block";
            else if (auto real = gf::game::blockId(*block); real != id) why = "an alias of the block " + real;
        } else if (dir == "entity_type") {
            auto  level = ll::service::getLevel();
            auto* group = level ? level->getEntityDefinitions() : nullptr;
            if (!group) why = "no entity definitions";
            else if (static_cast<ActorDefinition*>(group->tryGetDefinition(id).mPtr) == nullptr) why = "no entity definition";
        }
        mCache[key] = why;
        return why;
    }

private:
    std::map<std::string, std::string> mCache;
};

/// Collects the unknown ids of one source and reports them as one failure.
struct Unknown {
    Context&                 ctx;
    std::string              what;
    std::set<std::string>    ids;
    int                      checked = 0;
    void add(std::string const& id, std::string const& source, std::string const& why) { ids.insert(id + " (" + why + ", " + source + ")"); }
    void done() {
        ctx.info(what + ": " + std::to_string(checked) + " vanilla ids checked, " + std::to_string(ids.size()) + " unknown");
        if (ids.empty()) return;
        std::string list;
        int         n = 0;
        for (auto const& i : ids) {
            if (n++ == 30) {
                list += " ...";
                break;
            }
            list += (list.empty() ? "" : "; ") + i;
        }
        for (auto const& i : ids) gf::log().warn("item: {} names an unknown id: {}", what, i);
        ctx.check(false, what + " name ids the game does not know: " + list);
    }
};

} // namespace

// upstream's own recipes name vanilla items by Java id: the runtime holds the game's.
GF_REGISTER_SELFTEST("item.recipes_java_ids", [](Context& ctx) {
    // the parser and the owners' helpers (ext/item/Recipes.h)
    auto web = rc::ext::parseIngredient(JsonDoc{{"item", "minecraft:cobweb"}});
    ctx.check(web.items == std::vector<std::string>{"minecraft:web"}, "{\"item\": \"minecraft:cobweb\"} is Bedrock's minecraft:web");
    ctx.check(rc::internal::matches(web, "minecraft:web") && !rc::internal::matches(web, "minecraft:cobweb"), "it matches a web stack");
    auto list = rc::ext::parseIngredient(JsonDoc::array({"minecraft:lily_pad", {{"item", "minecraft:slime_block"}}, {{"item", "minecraft:stick"}}}));
    ctx.check(list.items == std::vector<std::string>{"minecraft:waterlily", "minecraft:slime", "minecraft:stick"}, "lists: waterlily, slime, and ids Bedrock shares stay");
    ctx.check(rc::ext::itemId(JsonDoc{{"id", "minecraft:dead_bush"}, {"count", 1}}) == "minecraft:deadbush", "a result minecraft:dead_bush is Bedrock's deadbush");
    ctx.check(rc::ext::itemId(JsonDoc("minecraft:item_frame")) == "minecraft:frame" && rc::ext::itemId(JsonDoc{{"item", "minecraft:map"}}) == "minecraft:empty_map",
              "item_frame is frame, map is empty_map");
    ctx.check(gf::tags::ext::bedrockId("item", "minecraft:chain") == "minecraft:iron_chain" && gf::tags::ext::bedrockId("block", "minecraft:cobweb") == "minecraft:web",
              "tags::ext::bedrockId by registry");
    // upstream's data (F1's import), when it is in this build
    auto const* glide = rc::byId("gloamingforest:add_modifier_to_travellers_gear/travellers_wings/gradual_glide_modifier");
    if (!glide) {
        ctx.info("upstream's recipe data is not in this build (F1's import): only the parser is checked");
        return;
    }
    ctx.require(glide->width == 3 && glide->height == 3 && glide->ingredients.size() == 9, "gradual_glide: the 3x3 pattern");
    ctx.check(rc::internal::matches(glide->ingredients[1], "minecraft:web"), "gradual_glide's 'C' (minecraft:cobweb) takes Bedrock's minecraft:web");
    struct Case {
        char const* recipe;
        char const* item;
        char const* what;
    };
    for (auto const& c : {Case{"gloamingforest:add_modifier_to_travellers_gear/travellers_boots/water_walk_modifier", "minecraft:waterlily", "water_walk: lily_pad"},
                          Case{"gloamingforest:add_modifier_to_travellers_gear/travellers_boots/slimy_soles_modifier", "minecraft:slime", "slimy_soles: slime_block"},
                          Case{"gloamingforest:add_modifier_to_travellers_gear/travellers_goggles/item_display_modifier", "minecraft:frame", "item_display: item_frame"},
                          Case{"gloamingforest:wood/canopy_hanging_sign", "minecraft:iron_chain", "the hanging signs: chain"},
                          Case{"gloamingforest:nagastone/nagastone_spiral", "minecraft:normal_stone_slab", "nagastone_spiral: stone_slab"}}) {
        auto const* r = rc::byId(c.recipe);
        ctx.check(r && takes(*r, c.item), std::string(c.what) + " takes Bedrock's " + c.item);
    }
    auto const* bush = rc::byId("gloamingforest:drying/sapling_to_dead_bush");
    ctx.check(bush && bush->resultItem == "minecraft:deadbush", "the drying recipe sapling_to_dead_bush gives minecraft:deadbush");
    auto made = rc::producing("minecraft:deadbush");
    ctx.check(std::any_of(made.begin(), made.end(), [](auto const* r) { return r->id == "gloamingforest:drying/sapling_to_dead_bush"; }), "producing(minecraft:deadbush) lists it");
    ctx.check(rc::producing("minecraft:dead_bush").empty(), "nothing produces the Java id");
    auto const* arrow = rc::byId("gloamingforest:uncrafting/tipped_arrow");
    ctx.check(arrow && arrow->resultItem == "minecraft:arrow", "the uncrafting recipe tipped_arrow uncrafts Bedrock's minecraft:arrow (the potion is its data value)");
});

// Every Bedrock id of the Java -> Bedrock tables, and every vanilla id the loaded recipes, tags and
// data maps produce, is an id of the game's registries.
GF_REGISTER_SELFTEST("item.java_ids_registry", [](Context& ctx) {
    ctx.require(ll::service::getLevel().has_value(), "a level (the registries)");
    Resolver resolve;
    // the tables
    {
        Unknown bad{ctx, "the Java -> Bedrock tables"};
        for (auto const* dir : {"block", "item", "entity_type"})
            for (auto const& [java, ids] : gf::tags::javaids::entries(dir))
                for (auto const& id : ids) {
                    ++bad.checked;
                    if (auto why = resolve.problem(dir, id); !why.empty()) bad.add(id, std::string(dir) + " " + java, why);
                }
        bad.done();
    }
    // the data recipes: ingredients and results
    {
        Unknown bad{ctx, "the data recipes"};
        auto    check = [&](std::string const& id, rc::CraftingRecipe const& r) {
            if (!id.starts_with("minecraft:")) return;
            ++bad.checked;
            if (auto why = resolve.problem("item", id); !why.empty()) bad.add(id, r.id, why);
        };
        auto all = rc::internal::dataRecipes();
        for (auto const* r : all) {
            for (auto const& i : r->ingredients)
                for (auto const& id : i.items) check(id, *r);
            check(r->resultItem, *r);
        }
        ctx.info(std::to_string(all.size()) + " data recipes");
        bad.done();
    }
    // the tags' data members (the vanilla bridge's come from the registries)
    {
        Unknown bad{ctx, "the tags"};
        for (auto const* dir : {"block", "item", "entity_type"})
            for (auto const& tag : gf::tags::internal::tagsOf(dir))
                for (auto const& id : gf::tags::internal::dataMembers(dir, tag)) {
                    if (!id.starts_with("minecraft:")) continue;
                    ++bad.checked;
                    if (auto why = resolve.problem(dir, id); !why.empty()) bad.add(id, std::string(dir) + " #" + tag, why);
                }
        bad.done();
    }
    // the data maps: keys by the map's registry, id fields by theirs
    {
        Unknown bad{ctx, "the data maps"};
        for (auto const& [map, registry] : gf::datamaps::internal::maps()) {
            auto fields = gf::datamaps::internal::idFields(map);
            for (auto const& key : gf::datamaps::internal::directKeys(map)) {
                if (key.starts_with("minecraft:") && (registry == "block" || registry == "item" || registry == "entity_type")) {
                    ++bad.checked;
                    if (auto why = resolve.problem(registry, key); !why.empty()) bad.add(key, map, why);
                }
                auto const* value = gf::datamaps::get(map, key);
                if (!value || !value->is_object()) continue;
                for (auto const& [field, dir] : fields) {
                    auto v = value->find(field);
                    if (v == value->end() || !v->is_string() || !v->get<std::string>().starts_with("minecraft:")) continue;
                    ++bad.checked;
                    if (auto why = resolve.problem(dir, v->get<std::string>()); !why.empty()) bad.add(v->get<std::string>(), map + " " + key + "." + field, why);
                }
            }
        }
        bad.done();
    }
});

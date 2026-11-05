// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/advancements/predicate/ItemColorPredicate.java
//   src/main/java/twilightforest/advancements/DrinkFromFlaskTrigger.java
//   src/main/java/twilightforest/advancements/KillBugTrigger.java
//   src/main/java/twilightforest/advancements/UncraftItemTrigger.java
//   src/main/java/twilightforest/advancements/HurtBossTrigger.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/progression/Criteria.h"

#include <algorithm>
#include <cctype>

namespace gf::core::progression {

namespace {

std::string withNamespace(std::string id) {
    if (!id.empty() && id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

/// A block / item id without a Java state suffix ("twilightforest:firefly[facing=up]").
std::string bareId(std::string id) {
    if (auto p = id.find('['); p != std::string::npos) id.resize(p);
    return withNamespace(std::move(id));
}

std::string scalarText(Json const& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) return std::to_string(v.get<double>());
    return v.dump();
}

bool matchLocation(Json const& loc, EntityView const& e, Env const& env) {
    if (!loc.is_object()) return true;
    for (auto const& [key, value] : loc.items()) {
        if (key == "dimension") {
            if (!value.is_string() || withNamespace(value.get<std::string>()) != e.dimension) return false;
        } else if (key == "biomes") {
            if (!matchIds(value, e.biome, env.biomeTag)) return false;
        } else if (key == "structures") {
            bool any = std::any_of(e.structures.begin(), e.structures.end(), [&](std::string const& s) {
                return matchIds(value, s, nullptr);
            });
            if (!any) return false;
        } else {
            return false; // a field TF's advancements never use: fail closed
        }
    }
    return true;
}

bool matchTypeSpecific(Json const& ts, EntityView const& e) {
    if (!ts.is_object()) return true;
    if (ts.value("type", std::string{}) != "minecraft:player" || e.type != "minecraft:player") return false;
    if (auto a = ts.find("advancements"); a != ts.end() && a->is_object()) {
        for (auto const& [id, want] : a->items()) {
            bool done = e.advancements.contains(withNamespace(id));
            if (want.is_boolean() && done != want.get<bool>()) return false;
            if (want.is_object()) return false; // per-criterion checks are not used by TF
        }
    }
    if (auto s = ts.find("stats"); s != ts.end() && s->is_array()) {
        for (auto const& stat : *s) {
            auto id = withNamespace(stat.value("stat", std::string{}));
            auto it = e.stats.find(id);
            long long v = it == e.stats.end() ? 0 : it->second;
            if (!matchInts(stat.value("value", Json{}), v)) return false;
        }
    }
    for (auto const& [key, _] : ts.items())
        if (key != "type" && key != "advancements" && key != "stats") return false;
    return true;
}

} // namespace

bool matchInts(Json const& bounds, long long value) {
    if (bounds.is_null()) return true;
    if (bounds.is_number()) return static_cast<long long>(bounds.get<double>()) == value;
    if (!bounds.is_object()) return false;
    if (auto m = bounds.find("min"); m != bounds.end() && m->is_number() && value < static_cast<long long>(m->get<double>())) return false;
    if (auto m = bounds.find("max"); m != bounds.end() && m->is_number() && value > static_cast<long long>(m->get<double>())) return false;
    return true;
}

bool matchIds(Json const& set, std::string const& id, std::function<bool(std::string const&, std::string const&)> const& tag) {
    if (id.empty()) return false;
    if (set.is_string()) {
        auto s = set.get<std::string>();
        if (!s.empty() && s.front() == '#') return tag && tag(withNamespace(s.substr(1)), id);
        return withNamespace(s) == id;
    }
    if (set.is_array())
        return std::any_of(set.begin(), set.end(), [&](Json const& e) { return e.is_string() && matchIds(e, id, tag); });
    return false;
}

bool matchItemColor(Json const& predicate, ItemView const& item) {
    int color = predicate.is_object() ? predicate.value("color", -1) : -1;
    // if (stack.has(DYED_COLOR) && color == -1) return true; return value.rgb() == color
    // (SingleComponentItemPredicate: a stack without the component never matches)
    if (!item.dyedColor) return false;
    if (color == -1) return true;
    return (*item.dyedColor & 0xFFFFFF) == (color & 0xFFFFFF);
}

bool matchItem(Json const& predicate, ItemView const& item, Env const& env) {
    if (item.id.empty() || item.id == "minecraft:air") return false;
    if (!predicate.is_object()) return predicate.is_null();
    for (auto const& [key, value] : predicate.items()) {
        if (key == "items") {
            if (!matchIds(value, item.id, env.itemTag)) return false;
        } else if (key == "count") {
            if (!matchInts(value, item.count)) return false;
        } else if (key == "predicates") {
            if (!value.is_object()) return false;
            for (auto const& [type, sub] : value.items()) {
                if (type == "twilightforest:color") {
                    if (!matchItemColor(sub, item)) return false;
                } else {
                    return false;
                }
            }
        } else {
            return false;
        }
    }
    return true;
}

bool matchEntity(Json const& predicate, EntityView const& entity, Env const& env) {
    if (predicate.is_null()) return true;
    if (!predicate.is_object()) return false;
    for (auto const& [key, value] : predicate.items()) {
        if (key == "type") {
            if (!matchIds(value, entity.type, env.entityTag)) return false;
        } else if (key == "flags") {
            if (!value.is_object()) return false;
            for (auto const& [flag, want] : value.items()) {
                if (flag == "is_baby") {
                    if (!want.is_boolean() || want.get<bool>() != entity.isBaby) return false;
                } else {
                    return false;
                }
            }
        } else if (key == "location") {
            if (!matchLocation(value, entity, env)) return false;
        } else if (key == "vehicle") {
            if (!entity.vehicle || !matchEntity(value, *entity.vehicle, env)) return false;
        } else if (key == "equipment") {
            if (!value.is_object()) return false;
            for (auto const& [slot, pred] : value.items()) {
                if (slot != "mainhand" || !entity.mainhand || !matchItem(pred, *entity.mainhand, env)) return false;
            }
        } else if (key == "type_specific") {
            if (!matchTypeSpecific(value, entity)) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool matchConditions(Json const& conditions, Context const& ctx, Env const& env) {
    if (conditions.is_null()) return true;
    if (conditions.is_object()) return ctx.self && matchEntity(conditions, *ctx.self, env); // inline form
    if (!conditions.is_array()) return false;
    for (auto const& c : conditions) {
        auto type = withNamespace(c.value("condition", std::string{}));
        if (type == "minecraft:entity_properties") {
            if (c.value("entity", std::string{"this"}) != "this" || !ctx.self) return false;
            if (!matchEntity(c.value("predicate", Json{}), *ctx.self, env)) return false;
        } else if (type == "minecraft:location_check") {
            if (!ctx.block) return false;
            auto pred  = c.value("predicate", Json::object());
            auto block = pred.value("block", Json{});
            if (!block.is_null()) {
                if (auto b = block.find("blocks"); b != block.end() && !matchIds(*b, bareId(ctx.block->id), env.blockTag)) return false;
                if (auto s = block.find("state"); s != block.end() && s->is_object()) {
                    for (auto const& [name, want] : s->items()) {
                        // Bedrock names TF's states with the namespace ("twilightforest:regenerate")
                        auto have = ctx.block->states.find(name);
                        if (have == ctx.block->states.end())
                            for (auto it = ctx.block->states.begin(); it != ctx.block->states.end(); ++it)
                                if (it.key().size() > name.size() && it.key().ends_with(":" + name)) have = it;
                        if (have == ctx.block->states.end()) return false;
                        if (want.is_object()) return false; // ranges are not used by TF
                        auto w = scalarText(want), h = scalarText(*have);
                        // Bedrock keeps boolean states as bools or 0/1 integers
                        if (w == "true" && h == "1") h = "true";
                        if (w == "false" && h == "0") h = "false";
                        if (w != h) return false;
                    }
                }
            }
            for (auto const& [key, _] : pred.items())
                if (key != "block") return false;
        } else if (type == "minecraft:match_tool") {
            if (!ctx.tool || !matchItem(c.value("predicate", Json::object()), *ctx.tool, env)) return false;
        } else if (type == "minecraft:all_of") {
            if (!matchConditions(c.value("terms", Json::array()), ctx, env)) return false;
        } else if (type == "minecraft:any_of") {
            auto terms = c.value("terms", Json::array());
            if (!std::any_of(terms.begin(), terms.end(), [&](Json const& t) { return matchConditions(Json::array({t}), ctx, env); })) return false;
        } else if (type == "minecraft:inverted") {
            if (matchConditions(Json::array({c.value("term", Json{})}), ctx, env)) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool matchInventory(Json const& conditions, std::vector<ItemView> const& inventory, Env const& env) {
    auto items = conditions.is_object() ? conditions.value("items", Json::array()) : Json::array();
    if (!items.is_array() || items.empty()) return false;
    for (auto const& pred : items) {
        bool any = std::any_of(inventory.begin(), inventory.end(), [&](ItemView const& i) { return matchItem(pred, i, env); });
        if (!any) return false;
    }
    return true;
}

std::string idIn(Json const& data, std::initializer_list<char const*> keys) {
    if (!data.is_object()) return {};
    for (auto const* k : keys) {
        auto it = data.find(k);
        if (it == data.end()) continue;
        if (it->is_string()) return bareId(it->get<std::string>());
        if (it->is_object())
            for (auto const* inner : {"id", "name", "item", "type"})
                if (auto v = it->find(inner); v != it->end() && v->is_string()) return bareId(v->get<std::string>());
    }
    return {};
}

bool matchTfTrigger(std::string const& trigger, Json const& conditions, Json const& data, Env const& env) {
    Json const& c = conditions.is_object() ? conditions : Json::object();
    if (trigger == "twilightforest:hurt_boss") {
        // HurtBossTrigger(player, boss): hurt_entity tested against the boss
        EntityView boss;
        boss.type = idIn(data, {"entity", "hurt_entity", "boss", "type"});
        if (auto h = c.find("hurt_entity"); h != c.end()) return matchConditions(*h, Context{&boss}, env);
        return true;
    }
    if (trigger == "twilightforest:kill_bug") {
        // KillBugTrigger(player, BlockState bug): bug == the block's state's block
        auto want = c.find("bug");
        if (want == c.end() || want->is_null()) return true;
        auto bug = idIn(data, {"bug", "block"});
        return want->is_string() && bareId(want->get<std::string>()) == bug;
    }
    if (trigger == "twilightforest:drink_from_flask") {
        // doses (default 0..4), seconds (default exactly 8), potion (required)
        Json doses   = c.contains("doses") ? c["doses"] : Json{{"min", 0}, {"max", 4}};
        Json seconds = c.contains("seconds") ? c["seconds"] : Json(8);
        if (!data.contains("doses") || !data["doses"].is_number() || !data.contains("seconds") || !data["seconds"].is_number()) return false;
        if (!matchInts(doses, data["doses"].get<long long>()) || !matchInts(seconds, data["seconds"].get<long long>())) return false;
        auto want = c.value("potion", std::string{});
        return !want.empty() && withNamespace(want) == idIn(data, {"potion"});
    }
    if (trigger == "twilightforest:uncraft_item") {
        auto pred = c.find("item");
        if (pred == c.end() || pred->is_null()) return true;
        ItemView item;
        item.id = idIn(data, {"item", "stack"});
        if (auto it = data.find("item"); it != data.end() && it->is_object()) item.count = it->value("count", 1);
        return matchItem(*pred, item, env);
    }
    // make_tf_portal, broke_glass_sword, consume_hydra_chop_on_low_hunger, add_modifier,
    // complete_quest_ram, placed_on_trophy_pedestal, kill_all_phantoms, activate_ghast_trap,
    // structure_cleared: only the player predicate (checked by the caller)
    return true;
}

bool sameRecipe(std::string const& javaId, std::string const& craftedId) {
    auto j = withNamespace(javaId), c = withNamespace(craftedId);
    if (j == c) return true;
    if (c.size() <= j.size() + 1 || c.compare(0, j.size(), j) != 0 || c[j.size()] != '_') return false;
    return std::all_of(c.begin() + static_cast<long>(j.size()) + 1, c.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

} // namespace gf::core::progression

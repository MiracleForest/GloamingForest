// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/advancements/predicate/ItemColorPredicate.java
//   src/main/java/gloamingforest/advancements/DrinkFromFlaskTrigger.java
//   src/main/java/gloamingforest/advancements/KillBugTrigger.java
//   src/main/java/gloamingforest/advancements/UncraftItemTrigger.java
//   src/main/java/gloamingforest/advancements/HurtBossTrigger.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// The criteria engine's pure half (U14): the conditions of upstream's 45 advancements, evaluated over
// plain views of the game objects. The predicate formats are the Java data formats upstream's
// generated advancement JSON uses (1.21.1):
//
// - item predicate {"items": id | [ids] | "#tag", "count": ints, "predicates": {"gloamingforest:color": {"color"?}}}
// - entity predicate {"type": id | "#tag", "flags": {"is_baby"}, "location": {"dimension", "biomes",
//   "structures"}, "vehicle": entity, "equipment": {"mainhand": item}, "type_specific":
//   {"type": "minecraft:player", "advancements": {id: bool}, "stats": [{"stat", "type", "value"}]}}
// - context-aware predicate lists [{"condition": "minecraft:entity_properties", "entity": "this",
//   "predicate": entity}, {"condition": "minecraft:location_check", "predicate": {"block":
//   {"blocks", "state"}}}, {"condition": "minecraft:match_tool", "predicate": item}]
// - ints bounds: a number (exactly) or {"min", "max"}
//
// Tags are answered by the caller (`Env`), so this file needs no game data.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::core::progression {

using Json = nlohmann::json;

struct ItemView {
    std::string        id;    ///< "gloamingforest:naga_scale"
    int                count = 1;
    std::optional<int> dyedColor; ///< minecraft:dyed_color (0xRRGGBB), when the stack has one
};

struct EntityView {
    std::string                 type;      ///< "gloamingforest:naga", "minecraft:player"
    bool                        isBaby = false;
    std::string                 dimension; ///< "gloamingforest:gloaming_forest", "minecraft:overworld"
    std::string                 biome;     ///< "gloamingforest:final_plateau"
    std::vector<std::string>    structures; ///< structures whose pieces hold the entity's position
    std::shared_ptr<EntityView> vehicle;   ///< what the entity rides
    std::optional<ItemView>     mainhand;
    // players only (type_specific minecraft:player)
    std::set<std::string>            advancements; ///< completed advancements
    std::map<std::string, long long> stats;        ///< "gloamingforest:e115_slices_eaten" -> value
};

struct BlockView {
    std::string id;                     ///< "gloamingforest:experiment_115"
    Json        states = Json::object(); ///< {"regenerate": true, ...} (any JSON scalar)
};

/// Tag membership (ids without '#').
struct Env {
    std::function<bool(std::string const& tag, std::string const& id)> itemTag;
    std::function<bool(std::string const& tag, std::string const& id)> entityTag;
    std::function<bool(std::string const& tag, std::string const& id)> blockTag;
    std::function<bool(std::string const& tag, std::string const& id)> biomeTag;
};

/// MinMaxBounds.Ints: a number matches exactly, {"min", "max"} inclusively, null / absent always.
[[nodiscard]] bool matchInts(Json const& bounds, long long value);
/// A HolderSet of ids: "id", "#tag", or a list of ids.
[[nodiscard]] bool matchIds(Json const& set, std::string const& id, std::function<bool(std::string const&, std::string const&)> const& tag);

/// ItemColorPredicate (gloamingforest:color): {"color": int} (absent: -1, any dyed colour).
[[nodiscard]] bool matchItemColor(Json const& predicate, ItemView const& item);
/// An ItemPredicate.
[[nodiscard]] bool matchItem(Json const& predicate, ItemView const& item, Env const& env);
/// An EntityPredicate (only the fields upstream's advancements use; unknown fields fail closed).
[[nodiscard]] bool matchEntity(Json const& predicate, EntityView const& entity, Env const& env);

/// What a context-aware predicate list can test.
struct Context {
    EntityView const* self  = nullptr; ///< "this"
    BlockView const*  block = nullptr; ///< location_check
    ItemView const*   tool  = nullptr; ///< match_tool
};
/// A context-aware predicate: a list of loot conditions (all must pass) or one entity predicate
/// (the older inline form, tested against `self`).
[[nodiscard]] bool matchConditions(Json const& conditions, Context const& ctx, Env const& env);

/// InventoryChangeTrigger: every item predicate is matched by some stack of `inventory` (one
/// stack may match several predicates, as Java's ItemPredicate list check on a single change).
[[nodiscard]] bool matchInventory(Json const& conditions, std::vector<ItemView> const& inventory, Env const& env);

/// The trigger payloads of upstream's triggers (other units fire them through progression::trigger;
/// the keys are documented in docs/units/progression.md). True when the criterion's
/// conditions accept the payload.
[[nodiscard]] bool matchTfTrigger(std::string const& trigger, Json const& conditions, Json const& data, Env const& env);

/// The id a payload names under one of `keys` (a string, or an object with "id" / "name" / "item").
[[nodiscard]] std::string idIn(Json const& data, std::initializer_list<char const*> keys);

/// The recipes a recipe_crafted criterion is resolved against.
struct RecipeIndex {
    std::function<bool(std::string const& id)> isJava;    ///< a Java data recipe (data/<ns>/recipe)
    std::function<bool(std::string const& id)> isBedrock; ///< a recipe identifier the game loaded (BP)
};
/// recipe_crafted: F1 splits a Java recipe of tag ingredients into numbered Bedrock layouts
/// <id>_0 .. <id>_<n-1> ("gloamingforest:equipment/travellers_belt_3") and then writes no BP
/// recipe <id>. A crafted id counts as the Java recipe when it is the Java id, or one of those
/// layouts: <java id>_<digits>, the Java recipe exists and was split (no BP recipe of its own id,
/// a layout _0), and the crafted id is no Java recipe of its own.
[[nodiscard]] bool sameRecipe(std::string const& javaId, std::string const& craftedId, RecipeIndex const& index);

} // namespace gf::core::progression

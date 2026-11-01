// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/loot/LootingEnchantNumberProvider.java
//   src/main/java/twilightforest/loot/MultiplayerBasedAdditionLootFunction.java
//   src/main/java/twilightforest/loot/MultiplayerBasedNumberProvider.java
//   src/main/java/twilightforest/loot/conditions/ModExistsCondition.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The loot table interpreter (contract "loot", F10): Minecraft 1.21.1's loot table format as TF's
// 634 tables use it, with Java's evaluation order and random calls (LootTable#getRandomItemsRaw,
// LootPool#addRandomItems / addRandomItem, the entry containers, number providers, conditions and
// functions), plus TF's twilightforest:multiplayer_rolls / looting_rolls number providers, the
// twilightforest:multiplayer_addition function and the twilightforest:mod_exists condition.
// Conditions and functions of other units (is_minion, giant_pick_used, uncrafting_table_enabled,
// ...) come from gf::loot::registerCondition / registerFunction; an unknown one warns once and
// fails (conditions) or does nothing (functions).

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantResult.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/BlockData.h"
#include "feature/enchant/Enchant.h"
#include "feature/inventory/Inventory.h"
#include "feature/loot/JavaRandom.h"
#include "feature/loot/LootInternal.h"
#include "feature/loot/LootRegistry.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::loot {

namespace {

// ---- helpers -------------------------------------------------------------------------------------

std::mutex& warnMutex() {
    static std::mutex m;
    return m;
}

void warnOnce(std::string const& what) {
    static std::set<std::string> warned;
    std::lock_guard              lock(warnMutex());
    if (warned.insert(what).second) log().warn("loot: {}", what);
}

std::string withNs(std::string id) {
    if (!id.empty() && id[0] == '#') id = id.substr(1);
    return id.find(':') == std::string::npos ? "minecraft:" + id : id;
}

/// Java enchantment ids -> Bedrock enchant types.
std::unordered_map<std::string, Enchant::Type> const& enchantMap() {
    using T = Enchant::Type;
    static std::unordered_map<std::string, T> const m{
        {"minecraft:protection", T::Protection},
        {"minecraft:fire_protection", T::FireProtection},
        {"minecraft:feather_falling", T::FeatherFalling},
        {"minecraft:blast_protection", T::BlastProtection},
        {"minecraft:projectile_protection", T::ProjectileProtection},
        {"minecraft:thorns", T::Thorns},
        {"minecraft:respiration", T::Respiration},
        {"minecraft:depth_strider", T::DepthStrider},
        {"minecraft:aqua_affinity", T::AquaAffinity},
        {"minecraft:sharpness", T::Sharpness},
        {"minecraft:smite", T::Smite},
        {"minecraft:bane_of_arthropods", T::BaneOfArthropods},
        {"minecraft:knockback", T::Knockback},
        {"minecraft:fire_aspect", T::FireAspect},
        {"minecraft:looting", T::Looting},
        {"minecraft:efficiency", T::Efficiency},
        {"minecraft:silk_touch", T::SilkTouch},
        {"minecraft:unbreaking", T::Unbreaking},
        {"minecraft:fortune", T::Fortune},
        {"minecraft:power", T::Power},
        {"minecraft:punch", T::Punch},
        {"minecraft:flame", T::Flame},
        {"minecraft:infinity", T::Infinity},
        {"minecraft:luck_of_the_sea", T::LuckOfTheSea},
        {"minecraft:lure", T::Lure},
        {"minecraft:frost_walker", T::FrostWalker},
        {"minecraft:mending", T::Mending},
        {"minecraft:binding_curse", T::CurseOfBinding},
        {"minecraft:vanishing_curse", T::CurseOfVanishing},
        {"minecraft:impaling", T::Impaling},
        {"minecraft:riptide", T::Riptide},
        {"minecraft:loyalty", T::Loyalty},
        {"minecraft:channeling", T::Channeling},
        {"minecraft:multishot", T::Multishot},
        {"minecraft:piercing", T::Piercing},
        {"minecraft:quick_charge", T::QuickCharge},
        {"minecraft:soul_speed", T::SoulSpeed},
        {"minecraft:swift_sneak", T::SwiftSneak},
        {"minecraft:wind_burst", T::WindBurst},
        {"minecraft:density", T::Density},
        {"minecraft:breach", T::Breach},
    };
    return m;
}

/// Java potion ids -> Bedrock potion aux values.
std::unordered_map<std::string, int> const& potionMap() {
    static std::unordered_map<std::string, int> const m{
        {"minecraft:water", 0},           {"minecraft:mundane", 1},          {"minecraft:thick", 3},
        {"minecraft:awkward", 4},         {"minecraft:night_vision", 5},     {"minecraft:long_night_vision", 6},
        {"minecraft:invisibility", 7},    {"minecraft:long_invisibility", 8}, {"minecraft:leaping", 9},
        {"minecraft:long_leaping", 10},   {"minecraft:strong_leaping", 11},  {"minecraft:fire_resistance", 12},
        {"minecraft:long_fire_resistance", 13}, {"minecraft:swiftness", 14}, {"minecraft:long_swiftness", 15},
        {"minecraft:strong_swiftness", 16}, {"minecraft:slowness", 17},      {"minecraft:long_slowness", 18},
        {"minecraft:water_breathing", 19}, {"minecraft:long_water_breathing", 20}, {"minecraft:healing", 21},
        {"minecraft:strong_healing", 22}, {"minecraft:harming", 23},          {"minecraft:strong_harming", 24},
        {"minecraft:poison", 25},         {"minecraft:long_poison", 26},      {"minecraft:strong_poison", 27},
        {"minecraft:regeneration", 28},   {"minecraft:long_regeneration", 29}, {"minecraft:strong_regeneration", 30},
        {"minecraft:strength", 31},       {"minecraft:long_strength", 32},    {"minecraft:strong_strength", 33},
        {"minecraft:weakness", 34},       {"minecraft:long_weakness", 35},    {"minecraft:turtle_master", 37},
        {"minecraft:long_turtle_master", 38}, {"minecraft:strong_turtle_master", 39}, {"minecraft:slow_falling", 40},
        {"minecraft:long_slow_falling", 41}, {"minecraft:strong_slowness", 42}, {"minecraft:wind_charged", 43},
        {"minecraft:weaving", 44},        {"minecraft:oozing", 45},           {"minecraft:infested", 46},
    };
    return m;
}

/// Java effect ids -> Bedrock suspicious stew aux values (set_stew_effect).
std::unordered_map<std::string, int> const& stewMap() {
    static std::unordered_map<std::string, int> const m{
        {"minecraft:night_vision", 0}, {"minecraft:jump_boost", 1}, {"minecraft:weakness", 2},  {"minecraft:blindness", 3},
        {"minecraft:poison", 4},       {"minecraft:saturation", 6}, {"minecraft:fire_resistance", 7}, {"minecraft:regeneration", 8},
        {"minecraft:wither", 9},
    };
    return m;
}

int stackCount(ItemStack const& s) { return game::items::countOf(s); }

ItemStack withCount(ItemStack const& s, int n) { return n <= 0 ? ItemStack{} : game::items::withCount(s, std::min(n, 255)); }

int maxStack(ItemStack const& s) { return game::items::isEmpty(s) ? 64 : std::max(1, static_cast<int>(s.getMaxStackSize())); }

// ---- evaluation state ------------------------------------------------------------------------------

struct Eval {
    LootContext const&       ctx;
    RandomSource&            random;
    std::vector<std::string> visited; ///< tables being rolled (loop guard)
};

Actor* entityParam(Eval const& e, std::string const& which) {
    if (which == "this") return e.ctx.thisEntity;
    if (which == "attacker" || which == "killer") return e.ctx.attacker;
    if (which == "direct_attacker" || which == "direct_killer") return e.ctx.direct;
    if (which == "attacking_player" || which == "killer_player" || which == "last_damage_player") return e.ctx.player;
    return nullptr;
}

ItemStack const* mainhandOf(Actor* a) {
    if (!a) return nullptr;
    auto const& s = a->getCarriedItem();
    return &s;
}

float numberFloat(Json const& n, Eval& e);
int   numberInt(Json const& n, Eval& e);

std::string typeOf(Json const& j) {
    auto t = j.value("type", std::string{});
    if (!t.empty()) return withNs(t);
    if (j.contains("min") || j.contains("max")) return "minecraft:uniform";
    if (j.contains("n") && j.contains("p")) return "minecraft:binomial";
    if (j.contains("value")) return "minecraft:constant";
    return {};
}

int binomial(Json const& n, Eval& e) {
    int   trials = numberInt(n.value("n", Json(0)), e);
    float p      = numberFloat(n.value("p", Json(0.5)), e);
    int   count  = 0;
    for (int i = 0; i < trials; ++i)
        if (e.random.nextFloat() < p) ++count;
    return count;
}

float numberFloat(Json const& n, Eval& e) {
    if (n.is_number()) return n.get<float>();
    if (!n.is_object()) return 0.f;
    auto t = typeOf(n);
    if (t == "minecraft:constant") return n.value("value", 0.f);
    if (t == "minecraft:uniform") return nextFloatBetween(e.random, numberFloat(n.value("min", Json(0)), e), numberFloat(n.value("max", Json(0)), e));
    if (t == "minecraft:binomial") return static_cast<float>(binomial(n, e));
    if (t == "twilightforest:multiplayer_rolls") {
        Json def = n.contains("default_rolls") ? n["default_rolls"] : Json{{"type", "minecraft:uniform"}, {"min", n.value("min", 0)}, {"max", n.value("max", 0)}};
        Json per = n.contains("per_player_rolls") ? n["per_player_rolls"] : Json(0);
        // MultiplayerBasedNumberProvider#getFloat
        if (adjustsLootRolls()) {
            int qualified = qualifiedPlayers(e.ctx.thisEntity);
            if (qualified > 0) {
                float total = numberFloat(def, e);
                for (int i = 0; i < qualified - 1; ++i) total += std::max(0.f, numberFloat(per, e));
                return total;
            }
        }
        return numberFloat(def, e);
    }
    if (t == "twilightforest:looting_rolls") {
        // LootingEnchantNumberProvider#getFloat: the attacker's level of the enchantment + base
        Json  base = n.value("base_value", Json(0));
        auto  ench = withNs(n.value("enchantment", std::string{"minecraft:looting"}));
        if (auto const* held = mainhandOf(e.ctx.attacker)) return static_cast<float>(enchantLevel(*held, ench)) + numberFloat(base, e);
        return numberFloat(base, e);
    }
    if (t == "minecraft:score" || t == "minecraft:storage" || t == "minecraft:enchantment_level") {
        warnOnce("number provider " + t + " is not supported (0)");
        return 0.f;
    }
    warnOnce("unknown number provider " + t);
    return 0.f;
}

int numberInt(Json const& n, Eval& e) {
    if (n.is_number()) return static_cast<int>(std::floor(n.get<double>()));
    if (n.is_object()) {
        auto t = typeOf(n);
        // UniformGenerator#getInt: Mth.nextInt over the int bounds; BinomialDistributionGenerator#getInt
        if (t == "minecraft:uniform") return nextIntBetween(e.random, numberInt(n.value("min", Json(0)), e), numberInt(n.value("max", Json(0)), e));
        if (t == "minecraft:binomial") return binomial(n, e);
        if (t == "minecraft:constant") return static_cast<int>(std::floor(n.value("value", 0.0)));
    }
    return static_cast<int>(std::floor(numberFloat(n, e))); // NumberProvider#getInt
}

/// LevelBasedValue#calculate.
float levelValue(Json const& v, int level) {
    if (v.is_number()) return v.get<float>();
    if (!v.is_object()) return 0.f;
    auto t = withNs(v.value("type", std::string{"minecraft:linear"}));
    if (t == "minecraft:linear") return v.value("base", 0.f) + v.value("per_level_above_first", 0.f) * static_cast<float>(level - 1);
    if (t == "minecraft:levels_squared") return static_cast<float>(level * level) + v.value("added", 0.f);
    if (t == "minecraft:clamped") return std::clamp(levelValue(v.value("value", Json(0)), level), v.value("min", 0.f), v.value("max", 0.f));
    if (t == "minecraft:fraction") {
        float d = levelValue(v.value("denominator", Json(1)), level);
        return d == 0 ? 0.f : levelValue(v.value("numerator", Json(0)), level) / d;
    }
    if (t == "minecraft:lookup") {
        auto values = v.value("values", Json::array());
        if (level - 1 >= 0 && level - 1 < static_cast<int>(values.size())) return values[level - 1].get<float>();
        return levelValue(v.value("fallback", Json(0)), level);
    }
    return 0.f;
}

// ---- item predicates (match_tool, equipment) -------------------------------------------------------

bool itemIn(std::string const& itemId, Json const& items) {
    auto one = [&](std::string s) {
        if (!s.empty() && s[0] == '#') return tags::has(tags::Registry::Item, withNs(s.substr(1)), itemId);
        return withNs(s) == itemId;
    };
    if (items.is_string()) return one(items.get<std::string>());
    if (items.is_array())
        for (auto const& i : items)
            if (i.is_string() && one(i.get<std::string>())) return true;
    return false;
}

bool enchantmentsMatch(ItemStack const& stack, Json const& list) {
    if (!list.is_array()) return true;
    for (auto const& req : list) {
        auto ids    = req.value("enchantments", Json{});
        auto levels = req.value("levels", Json{});
        int  min    = levels.is_number() ? levels.get<int>() : levels.is_object() ? levels.value("min", 1) : 1;
        int  max    = levels.is_number() ? levels.get<int>() : levels.is_object() ? levels.value("max", 255) : 255;
        std::vector<std::string> candidates;
        if (ids.is_string()) {
            auto s = ids.get<std::string>();
            if (!s.empty() && s[0] == '#') candidates = tags::members(tags::Registry::Enchantment, withNs(s.substr(1)));
            else candidates.push_back(withNs(s));
            if (candidates.empty() && s == "#minecraft:smelts_loot") candidates.push_back("minecraft:fire_aspect");
        } else if (ids.is_array()) {
            for (auto const& s : ids) candidates.push_back(withNs(s.get<std::string>()));
        }
        bool any = false;
        if (candidates.empty()) any = game::items::isEmpty(stack) ? false : static_cast<bool>(const_cast<ItemStack&>(stack).isEnchanted());
        for (auto const& id : candidates) {
            int lvl = enchantLevel(stack, id);
            if (lvl >= min && lvl <= max && lvl > 0) any = true;
        }
        if (!any) return false;
    }
    return true;
}

bool itemPredicate(ItemStack const* stack, Json const& p) {
    if (!p.is_object()) return true;
    ItemStack const& s  = stack ? *stack : game::items::empty();
    auto             id = game::items::idOf(s);
    if (p.contains("items") && (id.empty() || !itemIn(id, p["items"]))) return false;
    if (p.contains("tag") && (id.empty() || !tags::has(tags::Registry::Item, withNs(p.value("tag", std::string{})), id))) return false;
    if (p.contains("count")) {
        auto c = p["count"];
        int  n = stackCount(s);
        if (c.is_number() ? n != c.get<int>() : (n < c.value("min", 0) || n > c.value("max", 1 << 30))) return false;
    }
    if (auto preds = p.find("predicates"); preds != p.end() && preds->is_object()) {
        if (preds->contains("minecraft:enchantments") && !enchantmentsMatch(s, (*preds)["minecraft:enchantments"])) return false;
        if (preds->contains("minecraft:stored_enchantments") && !enchantmentsMatch(s, (*preds)["minecraft:stored_enchantments"])) return false;
    }
    return true;
}

bool entityPredicate(Actor* a, Json const& p, Eval& e) {
    if (!p.is_object() || p.empty()) return a != nullptr;
    if (!a) return false;
    if (p.contains("type")) {
        auto t = p["type"];
        auto id = a->getTypeName();
        bool ok = false;
        if (t.is_string()) {
            auto s = t.get<std::string>();
            ok     = (!s.empty() && s[0] == '#') ? tags::has(tags::Registry::EntityType, withNs(s.substr(1)), id) : withNs(s) == id;
        } else if (t.is_array()) {
            for (auto const& x : t) ok = ok || withNs(x.get<std::string>()) == id;
        }
        if (!ok) return false;
    }
    if (auto flags = p.find("flags"); flags != p.end() && flags->is_object()) {
        if (flags->contains("is_on_fire") && a->isOnFire() != (*flags)["is_on_fire"].get<bool>()) return false;
        if (flags->contains("is_baby") && a->isBaby() != (*flags)["is_baby"].get<bool>()) return false;
        if (flags->contains("is_sneaking") && a->isSneaking() != (*flags)["is_sneaking"].get<bool>()) return false;
        if (flags->contains("is_sprinting") && a->isSprinting() != (*flags)["is_sprinting"].get<bool>()) return false;
        if (flags->contains("is_swimming") && a->isSwimming() != (*flags)["is_swimming"].get<bool>()) return false;
    }
    if (auto eq = p.find("equipment"); eq != p.end() && eq->is_object()) {
        if (eq->contains("mainhand") && !itemPredicate(mainhandOf(a), (*eq)["mainhand"])) return false;
        if (eq->contains("offhand") && !itemPredicate(&a->getOffhandSlot(), (*eq)["offhand"])) return false;
    }
    if (auto ts = p.find("type_specific"); ts != p.end() && ts->is_object()) {
        auto t = withNs(ts->value("type", std::string{}));
        if (t == "minecraft:slime" && ts->contains("size")) {
            // Bedrock slimes keep their size in the variant / "size" property; small = 1
            int want = (*ts)["size"].is_number() ? (*ts)["size"].get<int>() : (*ts)["size"].value("min", 1);
            (void)want;
            warnOnce("entity predicate type_specific slime size is approximated (true)");
        } else if (!t.empty()) {
            warnOnce("entity predicate type_specific " + t + " is not supported (true)");
        }
    }
    (void)e;
    return true;
}

bool condition(Json const& c, Eval& e);

bool allOf(Json const& list, Eval& e) {
    if (!list.is_array()) return true;
    for (auto const& c : list)
        if (!condition(c, e)) return false;
    return true;
}

int toolLevel(Eval const& e, std::string const& enchant) { return e.ctx.tool ? enchantLevel(*e.ctx.tool, withNs(enchant)) : 0; }

/// The EXPLOSION_RADIUS parameter (block drops of an explosion; LootRegistry.h's scope).
std::optional<float> explosionRadius(Eval const&) { return currentExplosionRadius(); }

bool blockStateMatches(Block const* block, Json const& props) {
    if (!block) return false;
    auto states = game::statesOf(*block);
    for (auto const& [name, want] : props.items()) {
        std::string wantText = want.is_string() ? want.get<std::string>() : want.dump();
        std::string have;
        for (auto const& key : {name, "twilightforest:" + name, "minecraft:" + name}) {
            if (auto it = states.find(key); it != states.end()) {
                have = it->second;
                break;
            }
        }
        if (want.is_object()) {
            // range {min, max}
            try {
                int v = std::stoi(have);
                if (v < std::stoi(want.value("min", std::string{"-2147483648"})) || v > std::stoi(want.value("max", std::string{"2147483647"}))) return false;
            } catch (...) {
                return false;
            }
            continue;
        }
        if (have != wantText) return false;
    }
    return true;
}

bool canPerformAbility(std::string const& ability, ItemStack const* tool) {
    if (!tool) return false;
    auto id = game::items::idOf(*tool);
    if (id.empty()) return false;
    if (ability.rfind("shears_", 0) == 0) return id == "minecraft:shears" || tags::has(tags::Registry::Item, "c:tools/shear", id);
    if (ability.rfind("axe_", 0) == 0) return tags::has(tags::Registry::Item, "minecraft:axes", id) || id.ends_with("_axe");
    if (ability.rfind("shovel_", 0) == 0) return tags::has(tags::Registry::Item, "minecraft:shovels", id) || id.ends_with("_shovel");
    if (ability.rfind("hoe_", 0) == 0) return tags::has(tags::Registry::Item, "minecraft:hoes", id) || id.ends_with("_hoe");
    if (ability.rfind("sword_", 0) == 0) return tags::has(tags::Registry::Item, "minecraft:swords", id) || id.ends_with("_sword");
    if (ability.rfind("shield_", 0) == 0) return id == "minecraft:shield";
    return false;
}

bool condition(Json const& c, Eval& e) {
    if (!c.is_object()) return true;
    auto id = withNs(c.value("condition", std::string{}));
    if (auto custom = customCondition(id)) return (*custom)(e.ctx, c, e.random);
    if (id == "minecraft:survives_explosion") {
        if (auto r = explosionRadius(e); r && *r > 0) return e.random.nextFloat() <= 1.f / *r;
        return true;
    }
    if (id == "minecraft:random_chance") return e.random.nextFloat() < numberFloat(c.value("chance", Json(1)), e);
    if (id == "minecraft:random_chance_with_enchanted_bonus") {
        int lvl = 0;
        if (auto const* held = mainhandOf(e.ctx.attacker)) lvl = enchantLevel(*held, withNs(c.value("enchantment", std::string{"minecraft:looting"})));
        float chance = lvl > 0 ? levelValue(c.value("enchanted_chance", Json(0)), lvl) : c.value("unenchanted_chance", 0.f);
        return e.random.nextFloat() < chance;
    }
    if (id == "minecraft:table_bonus") {
        auto chances = c.value("chances", Json::array());
        if (chances.empty()) return false;
        int lvl = toolLevel(e, c.value("enchantment", std::string{}));
        return e.random.nextFloat() < chances[std::min<std::size_t>(lvl, chances.size() - 1)].get<float>();
    }
    if (id == "minecraft:killed_by_player") return e.ctx.player != nullptr;
    if (id == "minecraft:match_tool") return itemPredicate(e.ctx.tool, c.value("predicate", Json::object()));
    if (id == "minecraft:block_state_property") {
        auto const* block = e.ctx.block;
        if (!block || withNs(c.value("block", std::string{})) != game::blockId(*block)) return false;
        return blockStateMatches(block, c.value("properties", Json::object()));
    }
    if (id == "minecraft:entity_properties") return entityPredicate(entityParam(e, c.value("entity", std::string{"this"})), c.value("predicate", Json::object()), e);
    if (id == "minecraft:damage_source_properties") {
        auto p = c.value("predicate", Json::object());
        if (p.contains("source_entity") && !entityPredicate(e.ctx.attacker, p["source_entity"], e)) return false;
        if (p.contains("direct_entity") && !entityPredicate(e.ctx.direct, p["direct_entity"], e)) return false;
        if (auto tagsList = p.find("tags"); tagsList != p.end() && tagsList->is_array()) {
            for (auto const& t : *tagsList) {
                bool in = tags::has(tags::Registry::DamageType, withNs(t.value("id", std::string{})), e.ctx.damageType);
                if (in != t.value("expected", true)) return false;
            }
        }
        return true;
    }
    if (id == "minecraft:inverted") return !condition(c.value("term", Json::object()), e);
    if (id == "minecraft:any_of" || id == "minecraft:alternative") {
        for (auto const& t : c.value("terms", Json::array()))
            if (condition(t, e)) return true;
        return false;
    }
    if (id == "minecraft:all_of") return allOf(c.value("terms", Json::array()), e);
    if (id == "minecraft:value_check") {
        int  v     = numberInt(c.value("value", Json(0)), e);
        auto range = c.value("range", Json{});
        if (range.is_number()) return v == range.get<int>();
        if (!range.is_object()) return true;
        return v >= numberInt(range.value("min", Json(std::numeric_limits<int>::min())), e)
            && v <= numberInt(range.value("max", Json(std::numeric_limits<int>::max())), e);
    }
    if (id == "minecraft:reference") {
        warnOnce("condition minecraft:reference (predicate files) is not supported (true)");
        return true;
    }
    if (id == "neoforge:can_item_perform_ability") return canPerformAbility(c.value("ability", std::string{}), e.ctx.tool);
    warnOnce("unknown loot condition " + id + " (fails)");
    return false;
}

// ---- functions -----------------------------------------------------------------------------------

void enchantStack(ItemStack& stack, std::string const& javaId, int level) {
    if (level <= 0) return;
    if (game::items::idOf(stack) == "minecraft:book") {
        if (auto book = game::items::make("minecraft:enchanted_book", stackCount(stack))) stack = *book;
    }
    int t = bedrockEnchant(javaId);
    if (t >= 0) {
        EnchantmentInstance inst;
        inst.mEnchantType = static_cast<Enchant::Type>(t);
        inst.mLevel       = level;
        EnchantUtils::applyEnchant(stack, inst, true);
    } else {
        enchant::set(stack, javaId, level); // TF's enchantments (gf::enchant, F9)
    }
}

ItemStack function(ItemStack stack, Json const& f, Eval& e);

ItemStack functions(ItemStack stack, Json const& list, Eval& e) {
    if (!list.is_array()) return stack;
    for (auto const& f : list) stack = function(std::move(stack), f, e);
    return stack;
}

ItemStack function(ItemStack stack, Json const& f, Eval& e) {
    if (!f.is_object()) return stack;
    // LootItemConditionalFunction: the function's own conditions
    if (!allOf(f.value("conditions", Json::array()), e)) return stack;
    auto id = withNs(f.value("function", std::string{}));
    if (auto custom = customFunction(id)) return (*custom)(std::move(stack), e.ctx, f, e.random);
    if (game::items::isEmpty(stack)) return stack;
    if (id == "minecraft:set_count") {
        int base = f.value("add", false) ? stackCount(stack) : 0;
        return withCount(stack, base + numberInt(f.value("count", Json(1)), e));
    }
    if (id == "minecraft:limit_count") {
        auto limit = f.value("limit", Json::object());
        int  n     = stackCount(stack);
        if (limit.is_number()) return withCount(stack, std::min(n, limit.get<int>()));
        if (limit.contains("min")) n = std::max(n, numberInt(limit["min"], e));
        if (limit.contains("max")) n = std::min(n, numberInt(limit["max"], e));
        return withCount(stack, n);
    }
    if (id == "minecraft:explosion_decay") {
        if (auto r = explosionRadius(e); r && *r > 0) {
            float chance = 1.f / *r;
            int   kept   = 0;
            for (int i = 0, n = stackCount(stack); i < n; ++i)
                if (e.random.nextFloat() <= chance) ++kept;
            return withCount(stack, kept);
        }
        return stack;
    }
    if (id == "minecraft:apply_bonus") {
        // ApplyBonusCount#run: only with a tool; the formulas draw as Java's do (uniform and
        // binomial draw even at level 0)
        if (!e.ctx.tool) return stack;
        int  lvl     = toolLevel(e, f.value("enchantment", std::string{}));
        auto formula = withNs(f.value("formula", std::string{}));
        auto params  = f.value("parameters", Json::object());
        int  n       = stackCount(stack);
        if (formula == "minecraft:ore_drops") {
            if (lvl > 0) {
                int i = e.random.nextInt(lvl + 2) - 1;
                if (i < 0) i = 0;
                n *= i + 1;
            }
        } else if (formula == "minecraft:uniform_bonus_count") {
            n += e.random.nextInt(params.value("bonusMultiplier", 1) * lvl + 1);
        } else if (formula == "minecraft:binomial_with_bonus_count") {
            for (int i = 0; i < lvl + params.value("extra", 0); ++i)
                if (e.random.nextFloat() < params.value("probability", 0.f)) ++n;
        }
        return withCount(stack, n);
    }
    if (id == "minecraft:enchanted_count_increase" || id == "minecraft:looting_enchant") {
        int lvl = 0;
        if (auto const* held = mainhandOf(e.ctx.attacker)) lvl = enchantLevel(*held, withNs(f.value("enchantment", std::string{"minecraft:looting"})));
        if (lvl == 0) return stack;
        float add = static_cast<float>(lvl) * numberFloat(f.value("count", Json(1)), e);
        int   n   = stackCount(stack) + static_cast<int>(std::lround(add));
        int   lim = f.value("limit", 0);
        if (lim > 0 && n > lim) n = lim;
        return withCount(stack, n);
    }
    if (id == "minecraft:set_enchantments") {
        bool add = f.value("add", false);
        for (auto const& [ench, lv] : f.value("enchantments", Json::object()).items()) {
            int level = numberInt(lv, e);
            if (add) level += enchantLevel(stack, withNs(ench));
            enchantStack(stack, withNs(ench), level);
        }
        return stack;
    }
    if (id == "minecraft:enchant_randomly") {
        std::vector<std::string> options;
        auto                     opt = f.value("options", Json{});
        if (opt.is_string()) {
            auto s = opt.get<std::string>();
            options = (!s.empty() && s[0] == '#') ? tags::members(tags::Registry::Enchantment, withNs(s.substr(1))) : std::vector<std::string>{withNs(s)};
        } else if (opt.is_array()) {
            for (auto const& o : opt) options.push_back(withNs(o.get<std::string>()));
        }
        if (options.empty())
            for (auto const& [javaId, _] : enchantMap()) options.push_back(javaId);
        // only_compatible (Java's default): enchantments the item can take
        if (f.value("only_compatible", true) && game::items::idOf(stack) != "minecraft:book") {
            std::erase_if(options, [&](std::string const& o) {
                int t = bedrockEnchant(o);
                if (t < 0) return false; // TF's own (gf::enchant decides)
                EnchantmentInstance inst;
                inst.mEnchantType = static_cast<Enchant::Type>(t);
                inst.mLevel       = 1;
                auto r            = EnchantUtils::canEnchant(stack, inst, false);
                return r.result == EnchantResultType::Fail || r.result == EnchantResultType::Conflict;
            });
        }
        if (options.empty()) return stack;
        std::sort(options.begin(), options.end());
        auto pick = options[static_cast<std::size_t>(e.random.nextInt(static_cast<int>(options.size())))];
        int  t    = bedrockEnchant(pick);
        int  max  = 1;
        if (t >= 0) {
            static constexpr int maxLevels[] = {4, 4, 4, 4, 4, 3, 3, 3, 1, 5, 5, 5, 2, 2, 3, 5, 1, 3, 3, 5, 2, 1, 1, 3, 3, 2, 1, 1, 1, 5, 3, 3, 1, 1, 4, 3, 3, 3, 3, 5, 4, 3};
            if (t < static_cast<int>(std::size(maxLevels))) max = maxLevels[t];
        }
        enchantStack(stack, pick, nextIntBetween(e.random, 1, max));
        return stack;
    }
    if (id == "minecraft:enchant_with_levels") {
        int levels = numberInt(f.value("levels", Json(1)), e);
        if (game::items::idOf(stack) == "minecraft:book")
            if (auto book = game::items::make("minecraft:enchanted_book", stackCount(stack))) stack = *book;
        if (auto level = ll::service::getLevel()) EnchantUtils::randomlyEnchant(level->getIRandom(), stack, levels, 0, false);
        return stack;
    }
    if (id == "minecraft:set_potion") {
        auto it = potionMap().find(withNs(f.value("id", std::string{})));
        if (it == potionMap().end()) {
            warnOnce("set_potion: unknown potion " + f.value("id", std::string{}));
            return stack;
        }
        if (auto s = game::items::make(game::items::idOf(stack), stackCount(stack), it->second)) return *s;
        return stack;
    }
    if (id == "minecraft:set_stew_effect") {
        auto effects = f.value("effects", Json::array());
        if (effects.empty()) return stack;
        auto pick = effects[static_cast<std::size_t>(e.random.nextInt(static_cast<int>(effects.size())))];
        (void)numberInt(pick.value("duration", Json(160)), e); // Java rolls the duration too
        auto it = stewMap().find(withNs(pick.value("type", std::string{})));
        if (it != stewMap().end())
            if (auto s = game::items::make(game::items::idOf(stack), stackCount(stack), it->second)) return *s;
        return stack;
    }
    if (id == "minecraft:set_damage") {
        if (!stack.isDamageableItem()) return stack;
        auto* item = stack.mItem.get();
        if (!item) return stack;
        int   maxDamage = item->getMaxDamage();
        float fraction  = numberFloat(f.value("damage", Json(1)), e);
        if (f.value("add", false)) fraction += 1.f - static_cast<float>(stack.getDamageValue()) / static_cast<float>(std::max(1, maxDamage));
        fraction = std::clamp(fraction, 0.f, 1.f);
        stack.setDamageValue(static_cast<short>(std::floor((1.f - fraction) * static_cast<float>(maxDamage))));
        return stack;
    }
    if (id == "minecraft:set_name") {
        auto name = f.value("name", Json{});
        std::string text;
        if (name.is_string()) text = name.get<std::string>();
        else if (name.is_object() && name.contains("translate")) {
            auto key = name.value("translate", std::string{});
            text     = std::string(gf::translate(key, key));
        } else if (name.is_object()) text = name.value("text", std::string{});
        if (!text.empty()) stack.setCustomName(Bedrock::Safety::RedactableString(text));
        return stack;
    }
    if (id == "minecraft:furnace_smelt") {
        auto level = ll::service::getLevel();
        if (!level) return stack;
        auto result = level->getRecipes().getFurnaceRecipeResult(stack, HashedString("furnace"));
        if (result.isNull()) return stack;
        auto smelted = game::items::make(result.getTypeName(), stackCount(stack), result.getAuxValue());
        return smelted ? *smelted : stack;
    }
    if (id == "minecraft:copy_components") {
        // The block's components (blockdata "components", a gf::inventory container) into the
        // item's GloamingForest data "components" (block owners read them back on placement).
        if (f.value("source", std::string{}) != "block_entity" || !e.ctx.region) return stack;
        BlockPos pos{static_cast<int>(std::floor(e.ctx.origin.x)), static_cast<int>(std::floor(e.ctx.origin.y)), static_cast<int>(std::floor(e.ctx.origin.z))};
        // the block is usually gone when its drops roll: its block data document as it was erased
        auto        gpos   = game::globalPos(*e.ctx.region, pos);
        auto const* whole  = blockdata::get(gpos);
        Json const* erased = whole ? nullptr : recentlyErased(gpos);
        Json const& src    = whole ? *whole : erased ? *erased : Json::object();
        Json const  doc    = src.is_object() ? src.value("components", Json::object()) : Json::object();
        Json        comps  = game::items::getData(stack, "components");
        if (!comps.is_object()) comps = Json::object();
        for (auto const& inc : f.value("include", Json::array())) {
            auto key = withNs(inc.get<std::string>());
            if (key == "minecraft:container") {
                if (auto h = inventory::find(*e.ctx.region, pos); h && h->get) {
                    Json items = Json::array();
                    for (int i = 0; i < h->slots; ++i) items.push_back(game::items::toJson(h->get(gpos, i)));
                    comps[key] = items;
                } else if (src.is_object() && src.contains("inventory") && src["inventory"].is_object()) {
                    comps[key] = src["inventory"].value("items", Json::array()); // a removed BlockData container
                }
            } else if (doc.is_object() && doc.contains(key)) {
                comps[key] = doc[key];
            }
        }
        if (!comps.empty()) game::items::setData(stack, "components", comps);
        return stack;
    }
    if (id == "minecraft:copy_state") {
        auto const* block = e.ctx.block;
        if (!block) return stack;
        auto states = game::statesOf(*block);
        Json bs     = Json::object();
        for (auto const& p : f.value("properties", Json::array())) {
            auto name = p.get<std::string>();
            for (auto const& key : {name, "twilightforest:" + name, "minecraft:" + name})
                if (auto it = states.find(key); it != states.end()) bs[name] = it->second;
        }
        if (!bs.empty()) game::items::setData(stack, "block_state", bs);
        return stack;
    }
    if (id == "twilightforest:multiplayer_addition") {
        // MultiplayerBasedAdditionLootFunction#run
        if (adjustsLootRolls()) {
            int qualified = qualifiedPlayers(e.ctx.thisEntity);
            if (qualified > 1) {
                int  participating = qualified - 1;
                Json per           = f.contains("extra_count_per_player") ? f["extra_count_per_player"] : f.value("value", Json(0));
                int  extra         = numberInt(per, e) * participating;
                int  n             = std::clamp(stackCount(stack) + extra, 0, maxStack(stack));
                log().debug("{} extra players participated in a fight, dropping {} extra {}", participating, extra, game::items::idOf(stack));
                return withCount(stack, n);
            }
        }
        return stack;
    }
    if (id == "minecraft:set_contents") {
        warnOnce("set_contents is not supported (the item keeps no contents)");
        return stack;
    }
    warnOnce("unknown loot function " + id + " (skipped)");
    return stack;
}

// ---- entries ------------------------------------------------------------------------------------

using Sink = std::function<void(ItemStack)>;

void rollTable(Json const& table, std::string const& tableId, Eval& e, Sink const& out);

/// A leaf entry chosen from a pool (LootPoolEntry), with its container's functions.
struct Leaf {
    Json const* entry   = nullptr;
    std::string itemId; ///< for expanded tag entries
};

int weightOf(Json const& entry, float luck) {
    return std::max(static_cast<int>(std::floor(static_cast<float>(entry.value("weight", 1)) + static_cast<float>(entry.value("quality", 0)) * luck)), 0);
}

/// A member of a JSON object by reference (an empty array when missing): leaves keep pointers
/// into the table, so the entries must never be copies.
Json const& listOf(Json const& obj, char const* key) {
    static Json const empty = Json::array();
    if (!obj.is_object()) return empty;
    auto it = obj.find(key);
    return it != obj.end() && it->is_array() ? *it : empty;
}

/// LootPoolEntryContainer#expand: true when the entry (or a child) ran.
bool expand(Json const& entry, Eval& e, std::vector<Leaf>& out) {
    if (!entry.is_object() || !allOf(listOf(entry, "conditions"), e)) return false;
    auto type = withNs(entry.value("type", std::string{"minecraft:item"}));
    if (type == "minecraft:alternatives") {
        for (auto const& child : listOf(entry, "children"))
            if (expand(child, e, out)) return true;
        return false;
    }
    if (type == "minecraft:group") {
        for (auto const& child : listOf(entry, "children")) expand(child, e, out);
        return true;
    }
    if (type == "minecraft:sequence") {
        for (auto const& child : listOf(entry, "children"))
            if (!expand(child, e, out)) return false;
        return true;
    }
    if (type == "minecraft:tag" && entry.value("expand", false)) {
        for (auto const& item : tags::members(tags::Registry::Item, withNs(entry.value("name", std::string{})))) out.push_back({&entry, item});
        return true;
    }
    out.push_back({&entry, {}});
    return true;
}

/// LootPoolSingletonContainer#createItemStack.
void create(Leaf const& leaf, Eval& e, Sink const& out) {
    Json const& entry = *leaf.entry;
    auto        type  = withNs(entry.value("type", std::string{"minecraft:item"}));
    Json const  fns   = entry.value("functions", Json::array());
    Sink        decorated = [&](ItemStack s) { out(functions(std::move(s), fns, e)); };
    if (type == "minecraft:item" || (type == "minecraft:tag" && !leaf.itemId.empty())) {
        auto id = leaf.itemId.empty() ? withNs(entry.value("name", std::string{})) : leaf.itemId;
        if (auto s = game::items::make(id, 1)) decorated(*s);
        else warnOnce("unknown item " + id);
        return;
    }
    if (type == "minecraft:tag") {
        for (auto const& id : tags::members(tags::Registry::Item, withNs(entry.value("name", std::string{}))))
            if (auto s = game::items::make(id, 1)) decorated(*s);
        return;
    }
    if (type == "minecraft:loot_table") {
        auto value = entry.contains("value") ? entry["value"] : entry.value("name", Json{});
        if (value.is_string()) {
            auto sub = withNs(value.get<std::string>());
            if (auto const* t = table(sub)) rollTable(*t, sub, e, decorated);
            else warnOnce("unknown nested loot table " + sub);
        } else if (value.is_object()) {
            rollTable(value, "(inline)", e, decorated);
        }
        return;
    }
    if (type == "minecraft:empty") return;
    if (type == "minecraft:dynamic") {
        warnOnce("dynamic loot entry is not supported");
        return;
    }
    warnOnce("unknown loot entry type " + type);
}

/// LootPool#addRandomItem.
void addRandomItem(Json const& pool, Eval& e, Sink const& out) {
    std::vector<Leaf> leaves;
    for (auto const& entry : listOf(pool, "entries")) expand(entry, e, leaves);
    std::vector<std::pair<Leaf, int>> weighted;
    int                               total = 0;
    for (auto const& l : leaves) {
        int w = weightOf(*l.entry, e.ctx.luck);
        if (w > 0) {
            weighted.push_back({l, w});
            total += w;
        }
    }
    if (total == 0 || weighted.empty()) return;
    if (weighted.size() == 1) {
        create(weighted.front().first, e, out);
        return;
    }
    int j = e.random.nextInt(total);
    for (auto const& [leaf, w] : weighted) {
        j -= w;
        if (j < 0) {
            create(leaf, e, out);
            return;
        }
    }
}

/// LootTable#getRandomItemsRaw.
void rollTable(Json const& table, std::string const& tableId, Eval& e, Sink const& out) {
    if (std::find(e.visited.begin(), e.visited.end(), tableId) != e.visited.end() && tableId != "(inline)") {
        warnOnce("loop in loot table " + tableId);
        return;
    }
    e.visited.push_back(tableId);
    Json const tableFns = table.value("functions", Json::array());
    Sink       tableOut = [&](ItemStack s) { out(functions(std::move(s), tableFns, e)); };
    for (auto const& pool : listOf(table, "pools")) {
        if (!allOf(pool.value("conditions", Json::array()), e)) continue;
        Json const poolFns = pool.value("functions", Json::array());
        Sink       poolOut = [&](ItemStack s) { tableOut(functions(std::move(s), poolFns, e)); };
        int        rolls   = numberInt(pool.value("rolls", Json(1)), e)
                  + static_cast<int>(std::floor(numberFloat(pool.value("bonus_rolls", Json(0)), e) * e.ctx.luck));
        for (int i = 0; i < rolls; ++i) addRandomItem(pool, e, poolOut);
    }
    e.visited.pop_back();
}

} // namespace

int bedrockEnchant(std::string const& javaId) {
    auto it = enchantMap().find(withNs(javaId));
    return it == enchantMap().end() ? -1 : static_cast<int>(it->second);
}

int enchantLevel(ItemStack const& stack, std::string const& javaId) {
    if (game::items::isEmpty(stack)) return 0;
    int t = bedrockEnchant(javaId);
    if (t >= 0) return EnchantUtils::getEnchantLevel(static_cast<Enchant::Type>(t), stack);
    return enchant::level(stack, withNs(javaId));
}

std::vector<ItemStack> rollRaw(Json const& t, std::string const& tableId, LootContext const& ctx, RandomSource& random) {
    Eval                   e{ctx, random, {}};
    std::vector<ItemStack> out;
    // LootTable.createStackSplitter: stacks above the max stack size are split
    rollTable(t, tableId, e, [&](ItemStack s) {
        if (game::items::isEmpty(s)) return;
        int n = stackCount(s), max = maxStack(s);
        if (n <= max) {
            out.push_back(std::move(s));
            return;
        }
        while (n > 0) {
            int part = std::min(n, max);
            out.push_back(withCount(s, part));
            n -= part;
        }
    });
    return out;
}

} // namespace gf::loot

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
// Bedrock mappings: number providers round as Java's NumberProvider#getInt (Math.round); random
// bounds and chances are evaluated before their draw, in Java's order; set_contents fills a
// Bedrock bundle's dynamic container (minecraft:bundle_contents; other components go to the item
// data "components"); enchant_with_levels runs Java's EnchantmentHelper#selectEnchantment on the
// loot random with 1.21.1's enchantment definitions (weights, cost ranges, primary / supported
// items, exclusive sets) and TF's 4, and #minecraft:on_random_loot as vanilla's data lists it;
// the item sets are the enchantable item tags when gf::tags has them, else the Bedrock item's
// enchant slots with TF's tag changes; enchant_randomly picks from the options (absent: the whole
// registry) in registry order; set_enchantments sets the levels (0 removes); entity predicates'
// type_specific slime size reads the Bedrock variant (1, 2, 4) of slimes, magma cubes and TF's
// maze slime; set_name keeps a translatable name's key in the item data ("name_key") and the
// server language's text as the custom name.

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/item/EnchantSlot.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/containers/IContainerRegistryAccess.h"
#include "mc/world/containers/IContainerRegistryTracker.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/components/StorageItemComponent.h"
#include "mc/world/item/components/StorageWeightLimitItemComponent.h"
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
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/combat/CombatInternal.h"
#include "feature/common/BlockData.h"
#include "feature/enchant/Enchant.h"
#include "feature/inventory/Inventory.h"
#include "feature/inventory/InventoryInternal.h"
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
    if (t == "minecraft:uniform") {
        // UniformGenerator#getFloat: min, then max, then the draw (Java's evaluation order)
        float min = numberFloat(n.value("min", Json(0)), e);
        float max = numberFloat(n.value("max", Json(0)), e);
        return nextFloatBetween(e.random, min, max);
    }
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

} // namespace

int javaRound(float value) {
    // Math.round(float): floor(value + 1/2), exact in double (a float plus 0.5 has no rounding
    // error there), saturated to the int range; NaN -> 0
    if (std::isnan(value)) return 0;
    double r = std::floor(static_cast<double>(value) + 0.5);
    if (r <= static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    if (r >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    return static_cast<int>(r);
}

namespace {

int numberInt(Json const& n, Eval& e) {
    // NumberProvider#getInt is Math.round(getFloat) unless overridden: ConstantValue (plain numbers
    // too), TF's MultiplayerBasedNumberProvider and LootingEnchantNumberProvider inherit it
    if (n.is_number()) return javaRound(n.get<float>());
    if (n.is_object()) {
        auto t = typeOf(n);
        // UniformGenerator#getInt: Mth.nextInt over the int bounds (min, then max, then the draw);
        // BinomialDistributionGenerator#getInt
        if (t == "minecraft:uniform") {
            int min = numberInt(n.value("min", Json(0)), e);
            int max = numberInt(n.value("max", Json(0)), e);
            return nextIntBetween(e.random, min, max);
        }
        if (t == "minecraft:binomial") return binomial(n, e);
        if (t == "minecraft:constant") return javaRound(n.value("value", 0.f));
    }
    return javaRound(numberFloat(n, e)); // NumberProvider#getInt
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

/// MinMaxBounds.Ints#matches: a number is exact, {min, max} a closed range (either may be missing).
bool intInRange(Json const& range, int v) {
    if (range.is_number()) return v == range.get<int>();
    if (!range.is_object()) return true;
    if (range.contains("min") && range["min"].is_number() && v < range["min"].get<int>()) return false;
    if (range.contains("max") && range["max"].is_number() && v > range["max"].get<int>()) return false;
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
        if (t == "minecraft:slime") {
            // SlimePredicate: a Slime (Java's Slime class: slimes, magma cubes, TF's maze slime)
            // whose size is in range. Bedrock keeps a slime's size in its variant (1, 2, 4), and
            // so does TF's maze slime; a size that can't be read fails.
            auto id = a->getTypeName();
            if (id != "minecraft:slime" && id != "minecraft:magma_cube" && id != "twilightforest:maze_slime") return false;
            int size = slimeSize(*a);
            if (size <= 0) return false;
            if (ts->contains("size") && !intInRange((*ts)["size"], size)) return false;
        } else if (!t.empty()) {
            warnOnce("entity predicate type_specific " + t + " is not supported (fails)");
            return false;
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
    if (id == "minecraft:random_chance") {
        // LootItemRandomChanceCondition#test: the chance first, then the draw
        float chance = numberFloat(c.value("chance", Json(1)), e);
        return e.random.nextFloat() < chance;
    }
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
                // gf::combat's damage type tags (TF's memberships built in, then gf::tags)
                bool in = !e.ctx.damageType.empty() && combat::damageTypeHasTag(e.ctx.damageType, withNs(t.value("id", std::string{})));
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

/// A book becomes an enchanted book: SetEnchantmentsFunction's transmuteCopy keeps the count
/// (`keepCount`), the enchanting functions' new ItemStack(Items.ENCHANTED_BOOK) is one book.
void transmuteBook(ItemStack& stack, bool keepCount) {
    if (game::items::idOf(stack) != "minecraft:book") return;
    if (auto book = game::items::make("minecraft:enchanted_book", keepCount ? stackCount(stack) : 1)) stack = *book;
}

/// ItemEnchantments.Mutable#set: the level, clamped to 0..255; 0 removes the enchantment.
void setEnchantLevel(ItemStack& stack, std::string const& javaId, int level) {
    level = std::clamp(level, 0, 255);
    int t = bedrockEnchant(javaId);
    if (t < 0) {
        enchant::set(stack, javaId, level); // TF's enchantments (gf::enchant, F9; 0 removes)
        return;
    }
    auto type = static_cast<Enchant::Type>(t);
    if (EnchantUtils::hasEnchant(type, stack)) {
        // the others stay as they are
        auto others = stack.constructItemEnchantsFromUserData().getAllEnchants();
        EnchantUtils::removeEnchants(stack);
        for (auto const& inst : others)
            if (inst.mEnchantType != type) EnchantUtils::applyEnchant(stack, inst, true);
    }
    if (level <= 0) return;
    EnchantmentInstance inst;
    inst.mEnchantType = type;
    inst.mLevel       = level;
    EnchantUtils::applyEnchant(stack, inst, true);
}

/// ItemStack#enchant (ItemEnchantments.Mutable#upgrade): the higher of the two levels.
void upgradeEnchant(ItemStack& stack, std::string const& javaId, int level) {
    if (level <= 0 || enchantLevel(stack, javaId) >= level) return;
    setEnchantLevel(stack, javaId, level);
}

// ---- Java's enchantments (1.21.1 Enchantments#bootstrap and TF's 4 data files) ---------------------

/// The item sets of the enchantments (the item tags minecraft:enchantable/* and TF's).
enum class Cat {
    None, Armor, FootArmor, LegArmor, ChestArmor, HeadArmor, Sword, SharpWeapon, Weapon, FireAspect, Mining, MiningLoot,
    Durability, Bow, Crossbow, Trident, Fishing, Mace, Equippable, Vanishing, BlockAndChain, Scepters
};

char const* tagOf(Cat c) {
    switch (c) {
    case Cat::Armor: return "minecraft:enchantable/armor";
    case Cat::FootArmor: return "minecraft:enchantable/foot_armor";
    case Cat::LegArmor: return "minecraft:enchantable/leg_armor";
    case Cat::ChestArmor: return "minecraft:enchantable/chest_armor";
    case Cat::HeadArmor: return "minecraft:enchantable/head_armor";
    case Cat::Sword: return "minecraft:enchantable/sword";
    case Cat::SharpWeapon: return "minecraft:enchantable/sharp_weapon";
    case Cat::Weapon: return "minecraft:enchantable/weapon";
    case Cat::FireAspect: return "minecraft:enchantable/fire_aspect";
    case Cat::Mining: return "minecraft:enchantable/mining";
    case Cat::MiningLoot: return "minecraft:enchantable/mining_loot";
    case Cat::Durability: return "minecraft:enchantable/durability";
    case Cat::Bow: return "minecraft:enchantable/bow";
    case Cat::Crossbow: return "minecraft:enchantable/crossbow";
    case Cat::Trident: return "minecraft:enchantable/trident";
    case Cat::Fishing: return "minecraft:enchantable/fishing";
    case Cat::Mace: return "minecraft:enchantable/mace";
    case Cat::Equippable: return "minecraft:enchantable/equippable";
    case Cat::Vanishing: return "minecraft:enchantable/vanishing";
    case Cat::BlockAndChain: return "twilightforest:enchantable/block_and_chain";
    case Cat::Scepters: return "twilightforest:scepters";
    default: return "";
    }
}

/// The item's set when the item tags are not loaded: the Bedrock enchant slots of the item (what
/// the vanilla tags list), with TF's changes to the vanilla tags (data/minecraft/tags/item/
/// enchantable: the block and chain mines, the fiery and ice swords take no fire aspect, the
/// phantom armour no curse of vanishing).
bool inCategoryBySlots(ItemStack const& stack, std::string const& id, Cat c) {
    using S         = SharedTypes::EnchantSlot;
    auto* item      = stack.mItem.get();
    int   slots     = item ? item->getEnchantSlot() : 0;
    auto  has       = [&](S s) { return (slots & static_cast<int>(s)) != 0; };
    bool  damageable = item && item->isDamageable();
    switch (c) {
    case Cat::Armor: return has(S::GArmor);
    case Cat::FootArmor: return has(S::ArmorFeet);
    case Cat::LegArmor: return has(S::ArmorLegs);
    case Cat::ChestArmor: return has(S::ArmorTorso);
    case Cat::HeadArmor: return has(S::ArmorHead);
    case Cat::Sword: return has(S::Sword);
    case Cat::SharpWeapon: return has(S::Sword) || has(S::Axe);
    case Cat::Weapon: return has(S::Sword) || has(S::Axe) || has(S::HeavyWeapon);
    case Cat::FireAspect: return (has(S::Sword) || has(S::HeavyWeapon)) && id != "twilightforest:fiery_sword" && id != "twilightforest:ice_sword";
    case Cat::Mining: return has(S::GDigging) || has(S::Shears) || id == "twilightforest:block_and_chain";
    case Cat::MiningLoot: return has(S::GDigging) || id == "twilightforest:block_and_chain";
    case Cat::Durability: return damageable;
    case Cat::Bow: return has(S::Bow);
    case Cat::Crossbow: return has(S::Crossbow);
    case Cat::Trident: return has(S::Spear);
    case Cat::Fishing: return has(S::FishingRod);
    case Cat::Mace: return has(S::HeavyWeapon);
    case Cat::Equippable: return has(S::GArmor) || has(S::Elytra) || has(S::CosmeticHead);
    case Cat::Vanishing:
        return (damageable || has(S::Compass) || has(S::CosmeticHead) || has(S::GArmor) || has(S::Elytra)) && id != "twilightforest:phantom_helmet"
            && id != "twilightforest:phantom_chestplate";
    case Cat::BlockAndChain: return id == "twilightforest:block_and_chain";
    case Cat::Scepters:
        return id == "twilightforest:twilight_scepter" || id == "twilightforest:lifedrain_scepter" || id == "twilightforest:zombie_scepter"
            || id == "twilightforest:fortification_scepter";
    default: return false;
    }
}

/// The item sets already asked during one enchanting function (CategoryMemo): the stack is the same
/// for the whole call, so each set is decided once.
thread_local std::map<Cat, bool>* tCategoryMemo = nullptr;
struct CategoryMemo {
    CategoryMemo() : mPrevious(tCategoryMemo) { tCategoryMemo = &mMemo; }
    ~CategoryMemo() { tCategoryMemo = mPrevious; }
    CategoryMemo(CategoryMemo const&)            = delete;
    CategoryMemo& operator=(CategoryMemo const&) = delete;

private:
    std::map<Cat, bool>  mMemo;
    std::map<Cat, bool>* mPrevious;
};

/// Whether the stack is in the item set: the item tag (gf::tags) when it is loaded, else its slots.
bool inCategory(ItemStack const& stack, Cat c) {
    auto id = game::items::idOf(stack);
    if (id.empty() || c == Cat::None) return false;
    if (tCategoryMemo)
        if (auto it = tCategoryMemo->find(c); it != tCategoryMemo->end()) return it->second;
    char const* tag = tagOf(c);
    bool        in  = tags::has(tags::Registry::Item, tag, id) || (tags::members(tags::Registry::Item, tag).empty() && inCategoryBySlots(stack, id, c));
    if (tCategoryMemo) (*tCategoryMemo)[c] = in;
    return in;
}

/// An Enchantment's definition: weight, levels, costs (Enchantment.dynamicCost(base, per level);
/// a constant cost has per level 0), supported / primary items and the exclusive set.
struct JavaEnchantment {
    char const*              id;
    int                      weight;
    int                      maxLevel;
    int                      minBase, minPer;
    int                      maxBase, maxPer;
    Cat                      supported;
    Cat                      primary; ///< Cat::None: the supported items
    std::vector<char const*> exclusive;

    [[nodiscard]] int  minCost(int level) const { return minBase + minPer * (level - 1); }
    [[nodiscard]] int  maxCost(int level) const { return maxBase + maxPer * (level - 1); }
    [[nodiscard]] bool canEnchant(ItemStack const& s) const { return inCategory(s, supported); }
    /// Enchantment#isPrimaryItem: supported, and in the primary items when there are any.
    [[nodiscard]] bool isPrimaryItem(ItemStack const& s) const { return canEnchant(s) && (primary == Cat::None || inCategory(s, primary)); }
};

/// Every enchantment, in the order of the registry (data-driven registries load sorted by
/// ResourceLocation: path, then namespace).
std::vector<JavaEnchantment> const& javaEnchantments() {
    static std::vector<JavaEnchantment> const v = [] {
        std::vector<char const*> const armorX{"minecraft:protection", "minecraft:fire_protection", "minecraft:blast_protection", "minecraft:projectile_protection"};
        std::vector<char const*> const bootsX{"minecraft:frost_walker", "minecraft:depth_strider"};
        std::vector<char const*> const bowX{"minecraft:infinity", "minecraft:mending"};
        std::vector<char const*> const crossbowX{"minecraft:multishot", "minecraft:piercing"};
        std::vector<char const*> const damageX{"minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:impaling", "minecraft:density", "minecraft:breach"};
        std::vector<char const*> const miningX{"minecraft:fortune", "minecraft:silk_touch"};
        std::vector<char const*> const riptideX{"minecraft:loyalty", "minecraft:channeling"};
        std::vector<JavaEnchantment> list{
            {"minecraft:protection", 10, 4, 1, 11, 12, 11, Cat::Armor, Cat::None, armorX},
            {"minecraft:fire_protection", 5, 4, 10, 8, 18, 8, Cat::Armor, Cat::None, armorX},
            {"minecraft:feather_falling", 5, 4, 5, 6, 11, 6, Cat::FootArmor, Cat::None, {}},
            {"minecraft:blast_protection", 2, 4, 5, 8, 13, 8, Cat::Armor, Cat::None, armorX},
            {"minecraft:projectile_protection", 5, 4, 3, 6, 9, 6, Cat::Armor, Cat::None, armorX},
            {"minecraft:respiration", 2, 3, 10, 10, 40, 10, Cat::HeadArmor, Cat::None, {}},
            {"minecraft:aqua_affinity", 2, 1, 1, 0, 41, 0, Cat::HeadArmor, Cat::None, {}},
            {"minecraft:thorns", 1, 3, 10, 20, 60, 20, Cat::Armor, Cat::ChestArmor, {}},
            {"minecraft:depth_strider", 2, 3, 10, 10, 25, 10, Cat::FootArmor, Cat::None, bootsX},
            {"minecraft:frost_walker", 2, 2, 10, 10, 25, 10, Cat::FootArmor, Cat::None, bootsX},
            {"minecraft:binding_curse", 1, 1, 25, 0, 50, 0, Cat::Equippable, Cat::None, {}},
            {"minecraft:soul_speed", 1, 3, 10, 10, 25, 10, Cat::FootArmor, Cat::None, {}},
            {"minecraft:swift_sneak", 1, 3, 25, 25, 75, 25, Cat::LegArmor, Cat::None, {}},
            {"minecraft:sharpness", 10, 5, 1, 11, 21, 11, Cat::SharpWeapon, Cat::Sword, damageX},
            {"minecraft:smite", 5, 5, 5, 8, 25, 8, Cat::Weapon, Cat::Sword, damageX},
            {"minecraft:bane_of_arthropods", 5, 5, 5, 8, 25, 8, Cat::Weapon, Cat::Sword, damageX},
            {"minecraft:knockback", 5, 2, 5, 20, 55, 20, Cat::Sword, Cat::None, {}},
            {"minecraft:fire_aspect", 2, 2, 10, 20, 60, 20, Cat::FireAspect, Cat::None, {}},
            {"minecraft:looting", 2, 3, 15, 9, 65, 9, Cat::Sword, Cat::None, {}},
            {"minecraft:sweeping_edge", 2, 3, 5, 9, 20, 9, Cat::Sword, Cat::None, {}},
            {"minecraft:efficiency", 10, 5, 1, 10, 51, 10, Cat::Mining, Cat::None, {}},
            {"minecraft:silk_touch", 1, 1, 15, 0, 65, 0, Cat::MiningLoot, Cat::None, miningX},
            {"minecraft:unbreaking", 5, 3, 5, 8, 55, 8, Cat::Durability, Cat::None, {}},
            {"minecraft:fortune", 2, 3, 15, 9, 65, 9, Cat::MiningLoot, Cat::None, miningX},
            {"minecraft:power", 10, 5, 1, 10, 16, 10, Cat::Bow, Cat::None, {}},
            {"minecraft:punch", 2, 2, 12, 20, 37, 20, Cat::Bow, Cat::None, {}},
            {"minecraft:flame", 2, 1, 20, 0, 50, 0, Cat::Bow, Cat::None, {}},
            {"minecraft:infinity", 1, 1, 20, 0, 50, 0, Cat::Bow, Cat::None, bowX},
            {"minecraft:luck_of_the_sea", 2, 3, 15, 9, 65, 9, Cat::Fishing, Cat::None, {}},
            {"minecraft:lure", 2, 3, 15, 9, 65, 9, Cat::Fishing, Cat::None, {}},
            {"minecraft:loyalty", 5, 3, 12, 7, 50, 0, Cat::Trident, Cat::None, {}},
            {"minecraft:impaling", 2, 5, 1, 8, 21, 8, Cat::Trident, Cat::None, damageX},
            {"minecraft:riptide", 2, 3, 17, 7, 50, 0, Cat::Trident, Cat::None, riptideX},
            {"minecraft:channeling", 1, 1, 25, 0, 50, 0, Cat::Trident, Cat::None, {}},
            {"minecraft:multishot", 2, 1, 20, 0, 50, 0, Cat::Crossbow, Cat::None, crossbowX},
            {"minecraft:quick_charge", 5, 3, 12, 20, 50, 0, Cat::Crossbow, Cat::None, {}},
            {"minecraft:piercing", 10, 4, 1, 10, 50, 0, Cat::Crossbow, Cat::None, crossbowX},
            {"minecraft:density", 5, 5, 5, 8, 25, 8, Cat::Mace, Cat::None, damageX},
            {"minecraft:breach", 2, 4, 15, 9, 65, 9, Cat::Mace, Cat::None, damageX},
            {"minecraft:wind_burst", 2, 3, 15, 9, 65, 9, Cat::Mace, Cat::None, {}},
            {"minecraft:mending", 2, 1, 25, 25, 75, 25, Cat::Durability, Cat::None, {}},
            {"minecraft:vanishing_curse", 1, 1, 25, 0, 50, 0, Cat::Vanishing, Cat::None, {}},
            // TF's (data/twilightforest/enchantment)
            {"twilightforest:chill_aura", 1, 3, 5, 9, 20, 9, Cat::Armor, Cat::ChestArmor, {"minecraft:thorns", "twilightforest:fire_react"}},
            {"twilightforest:destruction", 1, 3, 5, 9, 20, 9, Cat::BlockAndChain, Cat::None, {}},
            {"twilightforest:fire_react", 1, 3, 5, 9, 20, 9, Cat::Armor, Cat::ChestArmor, {"minecraft:thorns", "twilightforest:chill_aura"}},
            {"twilightforest:renewal", 1, 1, 5, 9, 20, 9, Cat::Scepters, Cat::None, {}},
        };
        std::sort(list.begin(), list.end(), [](JavaEnchantment const& a, JavaEnchantment const& b) {
            std::string_view x = a.id, y = b.id;
            auto              px = x.substr(x.find(':') + 1), py = y.substr(y.find(':') + 1);
            return px != py ? px < py : x < y;
        });
        return list;
    }();
    return v;
}

JavaEnchantment const* javaEnchantment(std::string const& id) {
    for (auto const& e : javaEnchantments())
        if (id == e.id) return &e;
    return nullptr;
}

/// Enchantment#getMaxLevel of a Java enchantment id (1 when unknown).
int maxEnchantLevel(std::string const& javaId) {
    auto const* e = javaEnchantment(javaId);
    return e ? e->maxLevel : 1;
}

/// Enchantment#canEnchant (only_compatible): the stack is one of the enchantment's supported items.
bool canEnchantWith(ItemStack const& stack, std::string const& javaId) {
    auto const* e = javaEnchantment(javaId);
    return e && e->canEnchant(stack);
}

/// The enchantments of a tag as vanilla 1.21.1's data lists them (TF adds none), in the tag's order.
std::vector<std::string> builtinEnchantmentTag(std::string const& tag) {
    static std::vector<std::string> const nonTreasure{
        "minecraft:protection",     "minecraft:fire_protection", "minecraft:feather_falling", "minecraft:blast_protection",
        "minecraft:projectile_protection", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:thorns",
        "minecraft:depth_strider",  "minecraft:sharpness",       "minecraft:smite",           "minecraft:bane_of_arthropods",
        "minecraft:knockback",      "minecraft:fire_aspect",     "minecraft:looting",         "minecraft:sweeping_edge",
        "minecraft:efficiency",     "minecraft:silk_touch",      "minecraft:unbreaking",      "minecraft:fortune",
        "minecraft:power",          "minecraft:punch",           "minecraft:flame",           "minecraft:infinity",
        "minecraft:luck_of_the_sea", "minecraft:lure",           "minecraft:loyalty",         "minecraft:impaling",
        "minecraft:riptide",        "minecraft:channeling",      "minecraft:multishot",       "minecraft:quick_charge",
        "minecraft:piercing",       "minecraft:density",         "minecraft:breach",
    };
    if (tag == "minecraft:non_treasure" || tag == "minecraft:in_enchanting_table") return nonTreasure;
    if (tag == "minecraft:on_random_loot") {
        auto v = nonTreasure;
        for (char const* id : {"minecraft:binding_curse", "minecraft:vanishing_curse", "minecraft:frost_walker", "minecraft:mending"}) v.emplace_back(id);
        return v;
    }
    return {};
}

/// The enchantments of an `options` value (a tag, an id or a list; absent: the whole registry).
std::vector<JavaEnchantment const*> enchantmentPool(Json const& options) {
    std::vector<std::string> ids;
    if (options.is_string()) {
        auto s = options.get<std::string>();
        if (!s.empty() && s[0] == '#') {
            // vanilla's tags in their own order (gf::tags lists members sorted, and the weighted
            // pick walks the list in order); other tags from gf::tags
            auto tag = withNs(s.substr(1));
            ids      = builtinEnchantmentTag(tag);
            if (ids.empty()) ids = tags::members(tags::Registry::Enchantment, tag);
            if (ids.empty()) warnOnce("enchantment tag #" + tag + " has no members");
        } else {
            ids.push_back(withNs(s));
        }
    } else if (options.is_array()) {
        for (auto const& o : options)
            if (o.is_string()) ids.push_back(withNs(o.get<std::string>()));
    } else {
        for (auto const& e : javaEnchantments()) ids.emplace_back(e.id);
    }
    std::vector<JavaEnchantment const*> pool;
    for (auto const& id : ids) {
        if (auto const* e = javaEnchantment(id)) pool.push_back(e);
        else warnOnce("unknown enchantment " + id);
    }
    return pool;
}

/// Item#getEnchantmentValue (a book: 1).
int enchantmentValue(ItemStack const& stack) {
    if (game::items::idOf(stack) == "minecraft:book") return 1;
    auto* item = stack.mItem.get();
    return item ? item->getEnchantValue() : 0;
}

/// Enchantment#areCompatible.
bool compatible(JavaEnchantment const& a, JavaEnchantment const& b) {
    if (&a == &b) return false;
    auto in = [](JavaEnchantment const& x, JavaEnchantment const& y) {
        return std::any_of(x.exclusive.begin(), x.exclusive.end(), [&](char const* id) { return std::string_view(id) == y.id; });
    };
    return !in(a, b) && !in(b, a);
}

using Pick = std::pair<JavaEnchantment const*, int>;

/// WeightedRandom#getRandomItem.
std::optional<Pick> weightedPick(RandomSource& random, std::vector<Pick> const& list) {
    int total = 0;
    for (auto const& [e, _] : list) total += e->weight;
    if (total <= 0) return std::nullopt;
    int i = random.nextInt(total);
    for (auto const& p : list) {
        i -= p.first->weight;
        if (i < 0) return p;
    }
    return std::nullopt;
}

/// EnchantmentHelper#selectEnchantment (1.21.1): the enchantability bonus and the +-15 %
/// spread, the first pick from the enchantments whose cost range holds the level (the highest
/// such level; primary items only, a book takes all), then more picks while nextInt(50) <= level,
/// each dropping the ones incompatible with the last pick and halving the level.
std::vector<std::pair<std::string, int>> selectFrom(RandomSource& random, ItemStack const& stack, int level, std::vector<JavaEnchantment const*> const& pool) {
    std::vector<std::pair<std::string, int>> out;
    int                                      value = enchantmentValue(stack);
    if (value <= 0) return out;
    CategoryMemo memo;
    level += 1 + random.nextInt(value / 4 + 1) + random.nextInt(value / 4 + 1);
    float f = (random.nextFloat() + random.nextFloat() - 1.0f) * 0.15f;
    level   = std::max(1, javaRound(static_cast<float>(level) + static_cast<float>(level) * f));
    // getAvailableEnchantmentResults
    bool              book = game::items::idOf(stack) == "minecraft:book";
    std::vector<Pick> list;
    for (auto const* e : pool) {
        if (!book && !e->isPrimaryItem(stack)) continue;
        for (int l = e->maxLevel; l >= 1; --l) {
            if (level >= e->minCost(l) && level <= e->maxCost(l)) {
                list.emplace_back(e, l);
                break;
            }
        }
    }
    if (list.empty()) return out;
    std::vector<Pick> chosen;
    if (auto p = weightedPick(random, list)) chosen.push_back(*p);
    while (random.nextInt(50) <= level) {
        if (!chosen.empty()) {
            auto const* last = chosen.back().first;
            std::erase_if(list, [&](Pick const& p) { return !compatible(*last, *p.first); });
        }
        if (list.empty()) break;
        if (auto p = weightedPick(random, list)) chosen.push_back(*p);
        level /= 2;
    }
    for (auto const& [e, l] : chosen) out.emplace_back(e->id, l);
    return out;
}

ItemStack function(ItemStack stack, Json const& f, Eval& e);
/// SetContainerContents#run (defined with the entries below).
ItemStack setContents(ItemStack stack, Json const& f, Eval& e);

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
        if (limit.is_number()) return withCount(stack, limit.get<int>()); // IntRange.exactly(n): clamp(v, n, n)
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
        int   n   = stackCount(stack) + javaRound(add); // stack.grow(Math.round(f))
        int   lim = f.value("limit", 0);
        if (lim > 0 && n > lim) n = lim;
        return withCount(stack, n);
    }
    if (id == "minecraft:set_enchantments") {
        // SetEnchantmentsFunction#run: a book becomes an enchanted book first; each level is set
        // (clamped to 0..255, 0 removes), added to the current one with `add`
        transmuteBook(stack, true);
        bool add = f.value("add", false);
        for (auto const& [ench, lv] : f.value("enchantments", Json::object()).items()) {
            int level = numberInt(lv, e);
            if (add) level += enchantLevel(stack, withNs(ench));
            setEnchantLevel(stack, withNs(ench), level);
        }
        return stack;
    }
    if (id == "minecraft:enchant_randomly") {
        // the options' enchantments (absent: the whole registry, vanilla's and TF's 4, in registry
        // order)
        std::vector<std::string> options;
        for (auto const* ench : enchantmentPool(f.value("options", Json{}))) options.emplace_back(ench->id);
        // only_compatible (Java's default): enchantments the item can take (a book takes any)
        if (f.value("only_compatible", true) && game::items::idOf(stack) != "minecraft:book") {
            CategoryMemo memo;
            std::erase_if(options, [&](std::string const& o) { return !canEnchantWith(stack, o); });
        }
        if (options.empty()) return stack;
        // Util.getRandomSafe: list.get(random.nextInt(size)), then Mth.nextInt(min, max level);
        // EnchantRandomlyFunction#enchantItem: a book becomes an enchanted book
        auto pick  = options[static_cast<std::size_t>(e.random.nextInt(static_cast<int>(options.size())))];
        int  level = nextIntBetween(e.random, 1, maxEnchantLevel(pick));
        transmuteBook(stack, false);
        upgradeEnchant(stack, pick, level);
        return stack;
    }
    if (id == "minecraft:enchant_with_levels") {
        // EnchantWithLevelsFunction#run -> EnchantmentHelper#enchantItem: Java's selection on the
        // loot random (selectEnchantment over the options' enchantments, Java's costs and weights),
        // then the book becomes an enchanted book and every pick is applied
        int  levels = numberInt(f.value("levels", Json(1)), e);
        auto picks  = selectFrom(e.random, stack, levels, enchantmentPool(f.value("options", Json{})));
        transmuteBook(stack, false);
        for (auto const& [ench, level] : picks) upgradeEnchant(stack, ench, level);
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
        // Bedrock item names are plain text: a translatable name keeps its key in the item data
        // ("name_key", for the viewer's own language in the client mod's tooltips) and the
        // server language's text as the custom name every client shows
        auto name = f.value("name", Json{});
        std::string text;
        if (name.is_string()) text = name.get<std::string>();
        else if (name.is_object() && name.contains("translate")) {
            auto key = name.value("translate", std::string{});
            text     = std::string(gf::translate(key, key));
            game::items::setData(stack, "name_key", key);
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
                    for (auto const& s : inventory::contents(gpos)) items.push_back(game::items::toJson(s)); // one read
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
    if (id == "minecraft:set_contents") return setContents(std::move(stack), f, e);
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
    if (type == "minecraft:tag" && !leaf.itemId.empty()) {
        // TagEntry#expandTag: an EntryBase that emits new ItemStack(holder) without the entry's
        // functions (only the pool's and the table's apply)
        if (auto s = game::items::make(leaf.itemId, 1)) out(*s);
        else warnOnce("unknown item " + leaf.itemId);
        return;
    }
    Json const  fns   = entry.value("functions", Json::array());
    Sink        decorated = [&](ItemStack s) { out(functions(std::move(s), fns, e)); };
    if (type == "minecraft:item") {
        auto id = withNs(entry.value("name", std::string{}));
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

/// LootTable.createStackSplitter: stacks above the max stack size are split.
Sink splitter(Sink const& out) {
    return [out](ItemStack s) {
        if (game::items::isEmpty(s)) return;
        int n = stackCount(s), max = maxStack(s);
        if (n <= max) {
            out(std::move(s));
            return;
        }
        while (n > 0) {
            int part = std::min(n, max);
            out(withCount(s, part));
            n -= part;
        }
    };
}

/// Puts `contents` into a Bedrock storage item (the bundle): what the game does for a bundle
/// that gets its first item, a new dynamic container in the level's container registry named by
/// the item's bundle id tag. The game tracks it once a player holds the bundle and saves the
/// contents with the item. False when the item has no storage component.
bool fillStorageItem(ItemStack& stack, std::vector<ItemStack> const& contents) {
    auto  level = ll::service::getLevel();
    auto* item  = stack.mItem.get();
    if (!level || !item) return false;
    auto* storage = static_cast<StorageItemComponent*>(item->getComponent(HashedString("minecraft:storage_item")));
    if (!storage) return false;
    auto* weight = static_cast<StorageWeightLimitItemComponent*>(item->getComponent(HashedString("minecraft:storage_weight_limit")));
    auto  id     = level->getContainerRegistryTracker()->generateNewID();
    FullContainerName name;
    name.mName      = ContainerEnumName::DynamicContainer;
    name.mDynamicId = std::optional<uint>(id);
    auto access     = level->getContainerRegistryAccess();
    access->tryCreateEntry(storage, weight, name);
    auto backing = access->getBackingContainer(name);
    if (!backing) return false;
    int slot = 0;
    for (auto const& s : contents) {
        if (game::items::isEmpty(s)) continue;
        if (slot >= backing->getContainerSize()) {
            warnOnce("set_contents: more stacks than the storage item's slots (the rest is dropped)");
            break;
        }
        backing->setItem(slot++, s);
    }
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->putInt(std::string(kBundleIdTag), static_cast<int>(id));
    stack.setUserData(std::move(tag));
    return true;
}

/// The dynamic container of a storage item (selftests): its stacks, nullopt without a bundle id.
std::optional<std::vector<ItemStack>> storageItemContents(ItemStack const& stack) {
    auto level = ll::service::getLevel();
    auto key   = std::string(kBundleIdTag);
    if (!level || !stack.mUserData || !stack.mUserData->contains(key)) return std::nullopt;
    FullContainerName name;
    name.mName      = ContainerEnumName::DynamicContainer;
    name.mDynamicId = std::optional<uint>(static_cast<uint>(static_cast<int>((*stack.mUserData)[key].get<IntTag>())));
    auto backing    = level->getContainerRegistryAccess()->getBackingContainer(name);
    if (!backing) return std::nullopt;
    std::vector<ItemStack> out;
    for (int i = 0; i < backing->getContainerSize(); ++i)
        if (!game::items::isEmpty(backing->getItem(i))) out.push_back(backing->getItem(i));
    return out;
}

ItemStack setContents(ItemStack stack, Json const& f, Eval& e) {
    // SetContainerContents#run: every entry is expanded (conditions, alternatives, groups) and
    // every leaf creates its stacks (no weighted pick), through the stack splitter
    std::vector<ItemStack> contents;
    Sink                   add = splitter([&](ItemStack s) { contents.push_back(std::move(s)); });
    for (auto const& entry : listOf(f, "entries")) {
        std::vector<Leaf> leaves;
        expand(entry, e, leaves);
        for (auto const& leaf : leaves) create(leaf, e, add);
    }
    auto component = withNs(f.value("component", std::string{}));
    if (component == "minecraft:bundle_contents" && fillStorageItem(stack, contents)) return stack;
    // other components (minecraft:container, minecraft:charged_projectiles) and items without a
    // Bedrock storage: GloamingForest's item data "components", as copy_components keeps them
    Json items = Json::array();
    for (auto const& s : contents) items.push_back(game::items::toJson(s));
    Json comps = game::items::getData(stack, "components");
    if (!comps.is_object()) comps = Json::object();
    comps[component] = items;
    game::items::setData(stack, "components", comps);
    if (component == "minecraft:bundle_contents") warnOnce("set_contents: " + game::items::idOf(stack) + " has no Bedrock storage (contents kept in the item data)");
    return stack;
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
    rollTable(t, tableId, e, splitter([&](ItemStack s) { out.push_back(std::move(s)); }));
    return out;
}

std::optional<std::vector<ItemStack>> bundleContents(ItemStack const& stack) { return storageItemContents(stack); }

int slimeSize(Actor const& actor) { return actor.getVariant(); }

std::vector<std::pair<std::string, int>> selectEnchantment(RandomSource& random, ItemStack const& stack, int level, Json const& options) {
    return selectFrom(random, stack, level, enchantmentPool(options));
}

bool entityMatches(Actor* actor, Json const& predicate) {
    LootContext ctx;
    JavaRandom  random(1);
    Eval        e{ctx, random, {}};
    return entityPredicate(actor, predicate, e);
}

} // namespace gf::loot

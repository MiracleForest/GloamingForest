// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/command/SinisterSpawnerCommand.java
//   src/main/java/gloamingforest/block/WroughtIronFenceBlock.java
//   src/main/java/gloamingforest/init/TFParticleType.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the argument rules of /gloamingforest sinister_spawner and the fence shape
// rules (pure, gtested).

#include "core/lichdecor/Arguments.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>

namespace gf::core::lichdecor {

namespace {

bool nsChar(char c) { return c == '_' || c == '-' || c == '.' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
bool pathChar(char c) { return nsChar(c) || c == '/'; }

/// The option types: required fields and whether HashSet compares them by value.
struct OptionType {
    std::vector<std::string> fields;
    bool                     valueEqual;
};

std::map<std::string, OptionType> const& optionTypes() {
    static std::map<std::string, OptionType> const k{
        {"minecraft:dust", {{"color", "scale"}, false}},
        {"minecraft:dust_color_transition", {{"from_color", "to_color", "scale"}, false}},
        {"minecraft:block", {{"block_state"}, false}},
        {"minecraft:block_marker", {{"block_state"}, false}},
        {"minecraft:falling_dust", {{"block_state"}, false}},
        {"minecraft:dust_pillar", {{"block_state"}, false}},
        {"minecraft:item", {{"item"}, false}},
        {"minecraft:vibration", {{"destination", "arrival_in_ticks"}, false}},
        {"minecraft:sculk_charge", {{"roll"}, true}},
        {"minecraft:shriek", {{"delay"}, false}},
        {"minecraft:entity_effect", {{"color"}, false}},
        {"gloamingforest:magic_effect", {{"color"}, false}},
        {"gloamingforest:fallen_leaf", {{"r", "g", "b"}, true}},
    };
    return k;
}

} // namespace

bool validResourceLocation(std::string const& id) {
    auto        colon = id.find(':');
    std::string ns    = colon == std::string::npos ? "minecraft" : id.substr(0, colon);
    std::string path  = colon == std::string::npos ? id : id.substr(colon + 1);
    if (path.find(':') != std::string::npos) return false;
    return std::all_of(ns.begin(), ns.end(), nsChar) && std::all_of(path.begin(), path.end(), pathChar);
}

std::string withDefaultNamespace(std::string const& id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

std::vector<std::string> const& particleRegistry() {
    static std::vector<std::string> const k = [] {
        std::vector<std::string> v;
        for (char const* p :
             {"angry_villager", "block", "block_marker", "bubble", "cloud", "crit", "damage_indicator", "dragon_breath", "dripping_lava", "falling_lava",
              "landing_lava", "dripping_water", "falling_water", "dust", "dust_color_transition", "effect", "elder_guardian", "enchanted_hit", "enchant",
              "end_rod", "entity_effect", "explosion_emitter", "explosion", "gust", "small_gust", "gust_emitter_large", "gust_emitter_small", "sonic_boom",
              "falling_dust", "firework", "fishing", "flame", "infested", "cherry_leaves", "sculk_soul", "sculk_charge", "sculk_charge_pop", "soul_fire_flame",
              "soul", "flash", "happy_villager", "composter", "heart", "instant_effect", "item", "vibration", "item_slime", "item_cobweb", "item_snowball",
              "large_smoke", "lava", "mycelium", "note", "poof", "portal", "rain", "smoke", "white_smoke", "sneeze", "spit", "squid_ink", "sweep_attack",
              "totem_of_undying", "underwater", "splash", "witch", "bubble_pop", "current_down", "bubble_column_up", "nautilus", "dolphin",
              "campfire_cosy_smoke", "campfire_signal_smoke", "dripping_honey", "falling_honey", "landing_honey", "falling_nectar", "falling_spore_blossom",
              "ash", "crimson_spore", "warped_spore", "spore_blossom_air", "dripping_obsidian_tear", "falling_obsidian_tear", "landing_obsidian_tear",
              "reverse_portal", "white_ash", "small_flame", "snowflake", "dripping_dripstone_lava", "falling_dripstone_lava", "dripping_dripstone_water",
              "falling_dripstone_water", "glow_squid_ink", "glow", "wax_on", "wax_off", "electric_spark", "scrape", "shriek", "egg_crack", "dust_plume",
              "trial_spawner_detection", "trial_spawner_detection_ominous", "vault_connection", "dust_pillar", "ominous_spawning", "raid_omen", "trial_omen"})
            v.push_back(std::string("minecraft:") + p);
        for (char const* p : {"large_flame", "leaf_rune", "boss_tear", "ghast_trap", "protection", "snow", "snow_warning", "extended_snow_warning",
                              "snow_guardian", "ice_beam", "annihilate", "perfect_dodge", "double_jump", "huge_smoke", "firefly", "wandering_firefly",
                              "particle_spawner_firefly", "fallen_leaf", "dim_flame", "ominous_flame", "sorting_particle", "transformation_particle",
                              "log_core_particle", "cloud_puff", "magic_effect", "angry_lich", "gloaming_orb", "shield_break", "drying_rack"})
            v.push_back(std::string("gloamingforest:") + p);
        std::sort(v.begin(), v.end());
        return v;
    }();
    return k;
}

bool isRegisteredParticle(std::string const& id) {
    auto const& r = particleRegistry();
    return std::binary_search(r.begin(), r.end(), id);
}

std::optional<std::vector<std::string>> snbtKeys(std::string const& text, ArgError* error) {
    auto fail = [&](char const* key, char const* msg) -> std::optional<std::vector<std::string>> {
        if (error) *error = {key, msg};
        return std::nullopt;
    };
    std::size_t i  = 0;
    auto        ws = [&] {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };
    auto unquotedChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '+'; };
    auto readString   = [&](std::string& out) -> bool {
        if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
            char q = text[i++];
            while (i < text.size() && text[i] != q) {
                if (text[i] == '\\' && i + 1 < text.size()) ++i;
                out += text[i++];
            }
            if (i >= text.size()) return false;
            ++i;
            return true;
        }
        std::size_t start = i;
        while (i < text.size() && unquotedChar(text[i])) ++i;
        out = text.substr(start, i - start);
        return !out.empty();
    };
    // skips one value (nested compounds / lists / strings)
    std::function<bool()> skipValue = [&]() -> bool {
        ws();
        if (i >= text.size()) return false;
        if (text[i] == '{' || text[i] == '[') {
            char open = text[i], close = open == '{' ? '}' : ']';
            int  depth = 0;
            while (i < text.size()) {
                char c = text[i];
                if (c == '"' || c == '\'') {
                    std::string dummy;
                    if (!readString(dummy)) return false;
                    continue;
                }
                if (c == open) ++depth;
                else if (c == close && --depth == 0) {
                    ++i;
                    return true;
                }
                ++i;
            }
            return false;
        }
        std::string v;
        return readString(v);
    };
    ws();
    if (i >= text.size() || text[i] != '{') return fail("argument.nbt.expected.compound", "Expected compound tag");
    ++i;
    std::vector<std::string> keys;
    ws();
    if (i < text.size() && text[i] == '}') {
        ++i;
    } else {
        for (;;) {
            ws();
            std::string key;
            if (!readString(key)) return fail("argument.nbt.expected.key", "Expected key");
            ws();
            if (i >= text.size() || text[i] != ':') return fail("parsing.expected", "Expected ':'");
            ++i;
            if (!skipValue()) return fail("argument.nbt.expected.value", "Expected value");
            keys.push_back(key);
            ws();
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            if (i < text.size() && text[i] == '}') {
                ++i;
                break;
            }
            return fail("parsing.expected", "Expected '}'");
        }
    }
    ws();
    if (i != text.size()) return fail("argument.nbt.trailing", "Unexpected trailing data");
    return keys;
}

std::optional<ParticleArg> parseParticle(std::string const& text, ArgError* error) {
    auto        brace = text.find('{');
    std::string id    = brace == std::string::npos ? text : text.substr(0, brace);
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) id.pop_back();
    if (id.empty() || !validResourceLocation(id)) {
        if (error) *error = {"argument.id.invalid", "Invalid ID"};
        return std::nullopt;
    }
    id = withDefaultNamespace(id);
    if (!isRegisteredParticle(id)) {
        if (error) *error = {"particle.notFound", "Unknown particle: " + id};
        return std::nullopt;
    }
    std::string options = brace == std::string::npos ? std::string() : text.substr(brace);
    std::vector<std::string> keys;
    if (!options.empty()) {
        auto k = snbtKeys(options, error);
        if (!k) return std::nullopt;
        keys = *k;
    }
    auto it = optionTypes().find(id);
    if (it == optionTypes().end()) return ParticleArg{id, options, true}; // SimpleParticleType: unit codec
    std::string missing;
    for (auto const& f : it->second.fields) {
        if (std::find(keys.begin(), keys.end(), f) != keys.end()) continue;
        if (!missing.empty()) missing += "; ";
        missing += "No key " + f + " in MapLike[" + (options.empty() ? std::string("{}") : options) + "]";
    }
    if (!missing.empty()) {
        if (error) *error = {"particle.invalidOptions", "Can't parse particle options: " + missing};
        return std::nullopt;
    }
    return ParticleArg{id, options, it->second.valueEqual};
}

std::optional<LootArg> parseLoot(std::string const& text, bool (*exists)(std::string const&), ArgError* error) {
    std::string t = text;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    if (!t.empty() && t.front() == '{') {
        // an inline table: SNBT; any compound decodes (LootTable's fields are all optional)
        if (!snbtKeys(t, error)) return std::nullopt;
        return LootArg{{}, true};
    }
    if (t.empty() || t.find_first_of(" {}[]\"'") != std::string::npos) {
        if (error) *error = {"argument.resource_or_id.invalid", "Invalid id or tag"};
        return std::nullopt;
    }
    if (!validResourceLocation(t)) {
        if (error) *error = {"argument.id.invalid", "Invalid ID"};
        return std::nullopt;
    }
    std::string id = withDefaultNamespace(t);
    if (exists && !exists(id)) {
        if (error)
            *error = {"argument.resource_or_id.failed_to_parse",
                      "Failed to parse structure: Failed to get element ResourceKey[minecraft:loot_table / " + id + "]"};
        return std::nullopt;
    }
    return LootArg{id, false};
}

char const* fenceSideName(FenceSide s) {
    switch (s) {
    case FenceSide::None: return "none";
    case FenceSide::Middle: return "middle";
    case FenceSide::Bottom: return "bottom";
    case FenceSide::Top: return "top";
    case FenceSide::Full: return "full";
    }
    return "none";
}

std::optional<FenceSide> fenceSideOf(std::string const& name) {
    for (auto s : {FenceSide::None, FenceSide::Middle, FenceSide::Bottom, FenceSide::Top, FenceSide::Full})
        if (name == fenceSideName(s)) return s;
    return std::nullopt;
}

char const* postStateName(PostState s) {
    switch (s) {
    case PostState::None: return "none";
    case PostState::Post: return "post";
    case PostState::Capped: return "capped";
    }
    return "none";
}

std::optional<PostState> postStateOf(std::string const& name) {
    for (auto s : {PostState::None, PostState::Post, PostState::Capped})
        if (name == postStateName(s)) return s;
    return std::nullopt;
}

bool shouldBePost(FenceSide n, FenceSide e, FenceSide s, FenceSide w) {
    bool north = n == FenceSide::None, south = s == FenceSide::None, east = e == FenceSide::None, west = w == FenceSide::None;
    return (north && south && east && west) || north != south || east != west || (!north && !east);
}

FenceSide fenceSide(bool above, bool below) {
    if (above && below) return FenceSide::Middle;
    if (!above && below) return FenceSide::Top;
    if (above) return FenceSide::Bottom;
    return FenceSide::Full;
}

} // namespace gf::core::lichdecor

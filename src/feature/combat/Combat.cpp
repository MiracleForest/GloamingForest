// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFDamageTypes.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "combat" (F10): the single hurt hook of the mod (rule R8) and upstream's damage types.
//
// - One damage pipeline: `onIncoming` handlers (higher priority first) see every damage before it
//   applies (armour not applied yet) and may change the amount or cancel it; `onDealt` handlers
//   hear about damage that was applied, with the health it took. It runs from Actor::hurt (every
//   normal damage path) and from Mob::_hurt when that is called without Actor::hurt (LeviLamina's
//   Actor::hurtByCause, game code calling the virtual directly), once per damage (a guard lets the
//   inner _hurt of a hurt in progress through). Non-mob actors (boats, item entities, hanging
//   entities) are only seen through Actor::hurt.
// - A player hurt by a type whose DamageEffects are not "hurt" hears the effect's sound in place
//   of the hurt sound (Player#getHurtSound: burning, freezing, upstream's pinch): the game's hurt sound
//   for that hurt (Player::getCustomHurtSound) becomes Bedrock's matching player hurt sound, or
//   silence for upstream's pinch, which gf::fx plays.
// - Damage types: upstream's 40 (TFDamageTypes#bootstrap: message id, exhaustion, effects) and
//   Minecraft 1.21.1's 47 (data/minecraft/damage_type, with NeoForge's neoforge:poison) are built
//   in; <mod>/data/<ns>/damage_type/*.json (F1's import) override and add. Bedrock's own damage is
//   named by Java's ids ("minecraft:player_attack", "minecraft:arrow", ...) from its cause (and the
//   attacker / projectile), and gf::combat::hurt with a vanilla type uses that type's Bedrock cause.
// - Damage type tags: the data through gf::tags (F9: upstream's memberships, the units' fragments) plus
//   Minecraft 1.21.1's own tags with NeoForge 21.1's additions and neoforge: tags (is_physical,
//   is_magic, ...), built in (Bedrock has no data for them), nested tags resolved.
// - gf::combat::hurt(victim, amount, type, attacker, direct) builds the Bedrock damage source:
//   the cause of an upstream type follows its tags (bypasses_armor -> Magic, is_fire -> Fire,
//   is_explosion -> EntityExplosion, is_fall -> Fall, is_projectile -> Projectile, ...),
//   no_knockback turns the knockback off, the type's exhaustion is added to a hurt player, and the
//   death message of an upstream type is Java's (DamageSource#getLocalizedDeathMessage:
//   "death.attack.<msgId>", ".player" with the kill credit, ".item" with a named weapon) through one
//   ActorDamageSource#getDeathMessage hook; a vanilla type keeps Bedrock's message for its cause.

#include "feature/combat/Combat.h"
#include "feature/ext/combat/Handles.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <unordered_map>
#include <variant>

#include "nlohmann/json.hpp"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageByActorSource.h"
#include "mc/world/actor/ActorDamageByChildActorSource.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/actor/ActorEvent.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"

#include "feature/combat/CombatInternal.h"
#include "feature/death/DeathInternal.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::combat {

using Cause = SharedTypes::Legacy::ActorDamageCause;

namespace {

// ---- damage types --------------------------------------------------------------------------------

/// TFDamageTypes#bootstrap.
std::map<std::string, DamageTypeSpec> builtinTypes() {
    struct Row {
        char const* path;
        char const* msg;
        float       exhaustion;
        char const* effects;
    };
    static constexpr Row rows[] = {
        {"ghast_tear", "gloamingforest.ghastTear", 0.0f, "hurt"},
        {"hydra_bite", "gloamingforest.hydraBite", 0.0f, "hurt"},
        {"hydra_fire", "gloamingforest.hydraFire", 0.0f, "hurt"},
        {"hydra_mortar", "onFire", 0.0f, "burning"},
        {"lich_bolt", "gloamingforest.lichBolt", 0.0f, "hurt"},
        {"lich_bomb", "gloamingforest.lichBomb", 0.0f, "hurt"},
        {"chilling_breath", "gloamingforest.chillingBreath", 0.0f, "freezing"},
        {"squish", "gloamingforest.squish", 0.0f, "hurt"},
        {"thrown_axe", "gloamingforest.thrownAxe", 0.0f, "hurt"},
        {"thrown_pickaxe", "gloamingforest.thrownPickaxe", 0.0f, "hurt"},
        {"thorns", "gloamingforest.thorns", 0.1f, "hurt"},
        {"oreberry", "gloamingforest.oreberry", 0.1f, "hurt"},
        {"knightmetal", "gloamingforest.knightmetal", 0.1f, "hurt"},
        {"fiery", "gloamingforest.fiery", 0.1f, "burning"},
        {"fire_jet", "gloamingforest.fireJet", 0.1f, "burning"},
        {"reactor", "gloamingforest.reactor", 0.1f, "hurt"},
        {"slider", "gloamingforest.slider", 0.1f, "hurt"},
        {"thrown_block", "gloamingforest.thrownBlock", 0.1f, "hurt"},
        {"axing", "gloamingforest.axing", 0.1f, "hurt"},
        {"slam", "gloamingforest.axing", 0.1f, "hurt"},
        {"yeeted", "gloamingforest.yeeted", 0.1f, "hurt"},
        {"ant", "gloamingforest.ant", 0.1f, "hurt"},
        {"haunt", "gloamingforest.haunt", 0.1f, "hurt"},
        {"clamped", "gloamingforest.clamped", 0.1f, "gloamingforest:pinch"},
        {"scorched", "gloamingforest.scorched", 0.1f, "burning"},
        {"frozen", "gloamingforest.frozen", 0.1f, "freezing"},
        {"spiked", "gloamingforest.spiked", 0.1f, "hurt"},
        {"leaf_brain", "gloamingforest.leafBrain", 0.1f, "hurt"},
        {"lost_words", "gloamingforest.lostWords", 0.1f, "hurt"},
        {"schooled", "gloamingforest.schooled", 0.1f, "hurt"},
        {"snowball_fight", "gloamingforest.snowballFight", 0.1f, "freezing"},
        {"gloaming_scepter", "indirectMagic", 0.0f, "hurt"},
        {"lifedrain", "gloamingforest.lifedrain", 0.0f, "hurt"},
        {"expired", "gloamingforest.expired", 0.0f, "hurt"},
        {"falling_ice", "fallingBlock", 0.1f, "hurt"},
        {"moonworm", "gloamingforest.moonworm", 0.0f, "hurt"},
        {"acid_rain", "gloamingforest.acid_rain", 0.0f, "hurt"},
        {"ominous_fire", "gloamingforest.ominous", 0.1f, "burning"},
        {"failed_challenge", "gloamingforest.failedChallenge", 0.0f, "hurt"},
        {"stale_sandwich", "gloamingforest.stale_sandwich", 0.0f, "hurt"},
    };
    std::map<std::string, DamageTypeSpec> out;
    for (auto const& r : rows) {
        DamageTypeSpec s;
        s.id         = std::string("gloamingforest:") + r.path;
        s.messageId  = r.msg;
        s.exhaustion = r.exhaustion;
        s.effects    = r.effects;
        out[s.id]    = s;
    }
    // Minecraft 1.21.1's damage types (data/minecraft/damage_type/*.json: message_id, exhaustion,
    // effects, scaling, death_message_type) and NeoForge 21.1's neoforge:poison (its poison effect's
    // damage). Bedrock has no data for them; Bedrock's own message names their deaths except where
    // `bedrock` gives its key (the Bedrock cause would name it otherwise).
    struct VanillaRow {
        char const* id;
        char const* msg;
        float       exhaustion;
        char const* effects;
        char const* scaling;
        char const* deathMessageType;
        char const* bedrock;
    };
    static constexpr char const* kNp  = "when_caused_by_living_non_player";
    static constexpr char const* kAll = "always";
    static constexpr VanillaRow vanilla[] = {
        {"minecraft:arrow", "arrow", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:bad_respawn_point", "badRespawnPoint", 0.1f, "hurt", kAll, "intentional_game_design", ""},
        {"minecraft:cactus", "cactus", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:campfire", "inFire", 0.1f, "burning", kNp, "default", ""},
        {"minecraft:cramming", "cramming", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:dragon_breath", "dragonBreath", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:drown", "drown", 0.0f, "drowning", kNp, "default", ""},
        {"minecraft:dry_out", "dryout", 0.1f, "hurt", kNp, "default", "dehydration"},
        {"minecraft:explosion", "explosion", 0.1f, "hurt", kAll, "default", ""},
        {"minecraft:fall", "fall", 0.0f, "hurt", kNp, "fall_variants", ""},
        {"minecraft:falling_anvil", "anvil", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:falling_block", "fallingBlock", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:falling_stalactite", "fallingStalactite", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:fireball", "fireball", 0.1f, "burning", kNp, "default", ""},
        {"minecraft:fireworks", "fireworks", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:fly_into_wall", "flyIntoWall", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:freeze", "freeze", 0.0f, "freezing", kNp, "default", ""},
        {"minecraft:generic", "generic", 0.0f, "hurt", kNp, "default", "generic"},
        {"minecraft:generic_kill", "genericKill", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:hot_floor", "hotFloor", 0.1f, "burning", kNp, "default", ""},
        {"minecraft:in_fire", "inFire", 0.1f, "burning", kNp, "default", ""},
        {"minecraft:in_wall", "inWall", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:indirect_magic", "indirectMagic", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:lava", "lava", 0.1f, "burning", kNp, "default", ""},
        {"minecraft:lightning_bolt", "lightningBolt", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:magic", "magic", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:mob_attack", "mob", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:mob_attack_no_aggro", "mob", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:mob_projectile", "mob", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:on_fire", "onFire", 0.0f, "burning", kNp, "default", ""},
        {"minecraft:out_of_world", "outOfWorld", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:outside_border", "outsideBorder", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:player_attack", "player", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:player_explosion", "explosion.player", 0.1f, "hurt", kAll, "default", ""},
        {"minecraft:sonic_boom", "sonic_boom", 0.0f, "hurt", kAll, "default", ""},
        {"minecraft:spit", "mob", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:stalagmite", "stalagmite", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:starve", "starve", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:sting", "sting", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:sweet_berry_bush", "sweetBerryBush", 0.1f, "poking", kNp, "default", "sweetBerry"},
        {"minecraft:thorns", "thorns", 0.1f, "thorns", kNp, "default", ""},
        {"minecraft:thrown", "thrown", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:trident", "trident", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:unattributed_fireball", "onFire", 0.1f, "burning", kNp, "default", ""},
        {"minecraft:wind_charge", "mob", 0.1f, "hurt", kNp, "default", ""},
        {"minecraft:wither", "wither", 0.0f, "hurt", kNp, "default", ""},
        {"minecraft:wither_skull", "witherSkull", 0.1f, "hurt", kNp, "default", ""},
        {"neoforge:poison", "magic", 0.0f, "hurt", kNp, "default", ""},
    };
    for (auto const& r : vanilla) {
        DamageTypeSpec s;
        s.id               = r.id;
        s.messageId        = r.msg;
        s.exhaustion       = r.exhaustion;
        s.effects          = r.effects;
        s.scaling          = r.scaling;
        s.deathMessageType = r.deathMessageType;
        s.vanilla          = true;
        s.bedrockMessageId = r.bedrock;
        out[s.id]          = s;
    }
    return out;
}

/// Minecraft 1.21.1's own damage type tags (data/minecraft/tags/damage_type) with NeoForge 21.1's
/// additions and its neoforge: tags (upstream's code asks neoforge:is_physical / is_magic /
/// is_environment / is_technical): the game data Bedrock does not have. Members without a namespace
/// are minecraft:, "#..." are nested tags. upstream's memberships (its generated tag data:
/// minecraft:is_fire holds gloamingforest:hydra_fire, ...) and every unit's union fragments come
/// from the data through F9's gf::tags.
std::map<std::string, std::vector<std::string>> const& vanillaTags() {
    static std::map<std::string, std::vector<std::string>> const t = [] {
        static char const* const kPoison = "#neoforge:is_poison";
        std::map<std::string, std::vector<std::string>> raw{
            {"minecraft:always_hurts_ender_dragons", {"#is_explosion"}},
            {"minecraft:always_kills_armor_stands", {"arrow", "trident", "fireball", "wither_skull", "wind_charge"}},
            {"minecraft:always_most_significant_fall", {"out_of_world"}},
            {"minecraft:always_triggers_silverfish", {"magic", kPoison}},
            {"minecraft:avoids_guardian_thorns", {"magic", "thorns", "#is_explosion", kPoison}},
            {"minecraft:burn_from_stepping", {"campfire", "hot_floor"}},
            {"minecraft:burns_armor_stands", {"on_fire"}},
            {"minecraft:bypasses_armor", {"on_fire", "in_wall", "cramming", "drown", "fly_into_wall", "generic", "wither", "dragon_breath", "starve", "fall", "freeze", "stalagmite", "magic", "indirect_magic", "out_of_world", "generic_kill", "sonic_boom", "outside_border", kPoison}},
            {"minecraft:bypasses_effects", {"starve"}},
            {"minecraft:bypasses_enchantments", {"sonic_boom"}},
            {"minecraft:bypasses_invulnerability", {"out_of_world", "generic_kill"}},
            {"minecraft:bypasses_resistance", {"out_of_world", "generic_kill"}},
            {"minecraft:bypasses_shield", {"#bypasses_armor", "falling_anvil", "falling_stalactite"}},
            {"minecraft:bypasses_wolf_armor", {"#bypasses_invulnerability", "cramming", "drown", "dry_out", "freeze", "in_wall", "indirect_magic", "magic", "outside_border", "starve", "thorns", "wither", kPoison}},
            {"minecraft:can_break_armor_stand", {"player_attack", "player_explosion"}},
            {"minecraft:damages_helmet", {"falling_anvil", "falling_block", "falling_stalactite"}},
            {"minecraft:ignites_armor_stands", {"in_fire", "campfire"}},
            {"minecraft:is_drowning", {"drown"}},
            {"minecraft:is_explosion", {"fireworks", "explosion", "player_explosion", "bad_respawn_point"}},
            {"minecraft:is_fall", {"fall", "stalagmite"}},
            {"minecraft:is_fire", {"in_fire", "campfire", "on_fire", "lava", "hot_floor", "unattributed_fireball", "fireball"}},
            {"minecraft:is_freezing", {"freeze"}},
            {"minecraft:is_lightning", {"lightning_bolt"}},
            {"minecraft:is_player_attack", {"player_attack"}},
            {"minecraft:is_projectile", {"arrow", "trident", "mob_projectile", "unattributed_fireball", "fireball", "wither_skull", "thrown", "wind_charge"}},
            {"minecraft:no_anger", {"mob_attack_no_aggro"}},
            {"minecraft:no_impact", {"drown"}},
            {"minecraft:no_knockback", {"explosion", "player_explosion", "bad_respawn_point", "in_fire", "lightning_bolt", "on_fire", "lava", "hot_floor", "in_wall", "cramming", "drown", "starve", "cactus", "fall", "fly_into_wall", "out_of_world", "generic", "magic", "wither", "dragon_breath", "dry_out", "sweet_berry_bush", "freeze", "stalagmite", "outside_border", "generic_kill", "campfire", kPoison}},
            {"minecraft:panic_causes", {"#panic_environmental_causes", "arrow", "dragon_breath", "explosion", "fireball", "fireworks", "indirect_magic", "magic", "mob_attack", "mob_projectile", "player_attack", "player_explosion", "sonic_boom", "sting", "thrown", "trident", "unattributed_fireball", "wind_charge", "wither", "wither_skull", kPoison}},
            {"minecraft:panic_environmental_causes", {"cactus", "freeze", "hot_floor", "in_fire", "lava", "lightning_bolt", "on_fire"}},
            {"minecraft:witch_resistant_to", {"magic", "indirect_magic", "sonic_boom", "thorns", kPoison}},
            {"minecraft:wither_immune_to", {"drown"}},
            {"neoforge:is_environment", {"in_fire", "on_fire", "lava", "hot_floor", "drown", "starve", "dry_out", "freeze", "lightning_bolt", "cactus", "stalagmite", "falling_stalactite", "falling_block", "falling_anvil", "cramming", "fly_into_wall", "sweet_berry_bush", "in_wall"}},
            {"neoforge:is_magic", {"magic", "indirect_magic", "thorns", "dragon_breath", kPoison, "#neoforge:is_wither"}},
            {"neoforge:is_physical", {"cactus", "stalagmite", "falling_stalactite", "falling_block", "falling_anvil", "cramming", "fly_into_wall", "sweet_berry_bush", "fall", "sting", "mob_attack", "player_attack", "mob_attack_no_aggro", "arrow", "thrown", "trident", "mob_projectile", "sonic_boom", "in_wall", "generic"}},
            {"neoforge:is_poison", {"neoforge:poison"}},
            {"neoforge:is_technical", {"generic_kill", "outside_border", "out_of_world"}},
            {"neoforge:is_wither", {"wither", "wither_skull"}},
            {"neoforge:no_flinch", {}},
        };
        // the full ids: "arrow" -> "minecraft:arrow", "#is_fire" -> "#minecraft:is_fire"
        for (auto& [_, members] : raw)
            for (auto& m : members) {
                bool tag  = !m.empty() && m[0] == '#';
                auto name = tag ? m.substr(1) : m;
                if (name.find(':') == std::string::npos) name = "minecraft:" + name;
                m = tag ? "#" + name : name;
            }
        return raw;
    }();
    return t;
}

/// damageTypeHasTag with the nesting depth (a nested tag of the built-in table, its members from
/// the data and the table again).
bool hasTagAt(std::string const& id, std::string const& tag, int depth) {
    if (tags::has(tags::Registry::DamageType, tag, id)) return true; // the data (upstream's, the units' fragments)
    auto const& v  = vanillaTags();
    auto        it = v.find(tag);
    if (it == v.end()) return false;
    for (auto const& m : it->second) {
        if (m == id) return true;
        if (m[0] == '#' && depth < 8 && hasTagAt(id, m.substr(1), depth + 1)) return true;
    }
    return false;
}

/// The Bedrock counterpart of a vanilla damage type (Java's DamageSources -> the ActorDamageCause
/// Bedrock's own damage of that kind has: its armour, enchantment, resistance and immunity rules,
/// and its death message); nullopt for any other type.
std::optional<SharedTypes::Legacy::ActorDamageCause> vanillaCause(std::string const& id, bool hasAttacker) {
    using C = SharedTypes::Legacy::ActorDamageCause;
    static std::map<std::string, C> const m{
        {"minecraft:arrow", C::Projectile},
        {"minecraft:bad_respawn_point", C::BlockExplosion},
        {"minecraft:cactus", C::Contact},
        {"minecraft:campfire", C::Campfire},
        {"minecraft:cramming", C::Suffocation},
        {"minecraft:dragon_breath", C::Magic},
        {"minecraft:drown", C::Drowning},
        {"minecraft:dry_out", C::Suffocation},
        {"minecraft:fall", C::Fall},
        {"minecraft:falling_anvil", C::Anvil},
        {"minecraft:falling_block", C::FallingBlock},
        {"minecraft:falling_stalactite", C::Stalactite},
        {"minecraft:fireball", C::Fire}, // is_fire and is_projectile: fire immunity decides (a blaze)
        {"minecraft:fireworks", C::Fireworks},
        {"minecraft:fly_into_wall", C::FlyIntoWall},
        {"minecraft:freeze", C::Freezing},
        {"minecraft:generic", C::Magic}, // bypasses armour only
        {"minecraft:generic_kill", C::Override},
        {"minecraft:hot_floor", C::Magma},
        {"minecraft:in_fire", C::Fire},
        {"minecraft:in_wall", C::Suffocation},
        {"minecraft:indirect_magic", C::Magic},
        {"minecraft:lava", C::Lava},
        {"minecraft:lightning_bolt", C::Lightning},
        {"minecraft:magic", C::Magic},
        {"minecraft:mob_attack", C::EntityAttack},
        {"minecraft:mob_attack_no_aggro", C::EntityAttack},
        {"minecraft:mob_projectile", C::Projectile},
        {"minecraft:on_fire", C::FireTick},
        {"minecraft:out_of_world", C::Void},
        {"minecraft:outside_border", C::Magic},
        {"minecraft:player_attack", C::EntityAttack},
        {"minecraft:player_explosion", C::EntityExplosion},
        {"minecraft:sonic_boom", C::SonicBoom},
        {"minecraft:spit", C::Projectile},
        {"minecraft:stalagmite", C::Stalagmite},
        {"minecraft:starve", C::Starve},
        {"minecraft:sting", C::EntityAttack},
        {"minecraft:sweet_berry_bush", C::Contact},
        {"minecraft:thorns", C::Thorns},
        {"minecraft:thrown", C::Projectile},
        {"minecraft:trident", C::Projectile},
        {"minecraft:unattributed_fireball", C::Fire},
        {"minecraft:wind_charge", C::Projectile},
        {"minecraft:wither", C::Wither},
        {"minecraft:wither_skull", C::Projectile},
        {"neoforge:poison", C::Magic},
    };
    // Explosion#getDamageSource: an explosion with a causing entity (a creeper) is the entity's
    if (id == "minecraft:explosion") return hasAttacker ? C::EntityExplosion : C::BlockExplosion;
    auto it = m.find(id);
    if (it == m.end()) return std::nullopt;
    return it->second;
}

struct Types {
    std::mutex                            mutex;
    bool                                  loaded = false;
    std::map<std::string, DamageTypeSpec> types;
};

Types& types() {
    static Types t;
    return t;
}

void loadTypes(Types& t) {
    t.loaded = true;
    t.types  = builtinTypes();
    std::filesystem::path data;
    try {
        data = Mod::getInstance().getSelf().getModDir() / "data";
    } catch (...) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(data, ec)) return;
    for (auto const& ns : std::filesystem::directory_iterator(data, ec)) {
        auto root = ns.path() / "damage_type";
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (auto const& f : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (f.path().extension() != ".json") continue;
            try {
                std::ifstream  in(f.path());
                auto           j = nlohmann::json::parse(in, nullptr, true, true);
                DamageTypeSpec s;
                s.id = ns.path().filename().string() + ":" + std::filesystem::relative(f.path(), root, ec).replace_extension().generic_string();
                s.messageId        = j.value("message_id", s.id);
                s.exhaustion       = j.value("exhaustion", 0.f);
                s.effects          = j.value("effects", std::string{"hurt"});
                s.scaling          = j.value("scaling", s.scaling);
                s.deathMessageType = j.value("death_message_type", s.deathMessageType);
                if (auto old = t.types.find(s.id); old != t.types.end()) {
                    // a data file of a vanilla type: still Bedrock's own damage
                    s.vanilla          = old->second.vanilla;
                    s.bedrockMessageId = old->second.bedrockMessageId;
                }
                t.types[s.id] = std::move(s);
            } catch (std::exception const& ex) {
                log().warn("combat: bad damage type {}: {}", f.path().string(), ex.what());
            }
        }
    }
}

// ---- handlers ------------------------------------------------------------------------------------

template <class Fn>
struct Entry {
    int           priority;
    std::uint64_t seq; ///< registration order, also the handle of ext::remove
    Fn            fn;
};

struct Handlers {
    std::mutex                    mutex;
    std::vector<Entry<IncomingFn>> incoming;
    std::vector<Entry<DealtFn>>    dealt;
    std::uint64_t                 seq = 0;
};

Handlers& handlers() {
    static Handlers h;
    return h;
}

template <class Fn>
std::uint64_t addSorted(std::vector<Entry<Fn>>& list, int priority, Fn fn) {
    auto id = ++handlers().seq;
    list.push_back({priority, id, std::move(fn)});
    std::stable_sort(list.begin(), list.end(), [](auto const& a, auto const& b) { return a.priority > b.priority; });
    return id;
}

// ---- the upstream damage being applied / the marks for death messages -------------------------------------

/// gf::combat::hurt in progress (the hook reads the upstream type from here).
struct Pending {
    Actor*      victim = nullptr;
    std::string type;
    bool        converted = false; ///< a handler changed the type: the converted hurt did the rest
};
thread_local std::vector<Pending> tPending;

struct Mark {
    std::string   type;
    std::int64_t  attacker = 0; ///< ActorUniqueID raw, 0 = none
    std::int64_t  direct   = 0;
    std::uint64_t tick     = 0;
};

std::mutex& marksMutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::int64_t, Mark>& marks() {
    static std::unordered_map<std::int64_t, Mark> m;
    return m;
}

std::uint64_t nowTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
}

Actor* fetch(std::int64_t raw) {
    if (!raw) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = raw;
    return level->fetchEntity(id, false);
}

void mark(Actor& victim, std::string const& type, Actor* attacker, Actor* direct) {
    auto            now = nowTick();
    std::lock_guard lock(marksMutex());
    if (marks().size() > 512) std::erase_if(marks(), [now](auto const& e) { return now > e.second.tick + 1; });
    marks()[victim.getOrCreateUniqueID().rawID] = Mark{
        type,
        attacker ? attacker->getOrCreateUniqueID().rawID : 0,
        direct ? direct->getOrCreateUniqueID().rawID : 0,
        now,
    };
}

std::optional<Mark> markOf(Actor& victim) {
    std::lock_guard lock(marksMutex());
    auto            it = marks().find(victim.getOrCreateUniqueID().rawID);
    if (it == marks().end()) return std::nullopt;
    if (nowTick() > it->second.tick + 1) {
        marks().erase(it);
        return std::nullopt;
    }
    return it->second;
}

void restoreMark(Actor& victim, std::optional<Mark> const& previous) {
    std::lock_guard lock(marksMutex());
    auto            id = victim.getOrCreateUniqueID().rawID;
    if (previous) marks()[id] = *previous;
    else marks().erase(id);
}

/// The Java damage type of Bedrock's own damage (Minecraft 1.21.1's DamageSources as Java's game
/// code picks them for the same event).
std::string vanillaTypeOf(Actor& victim, ActorDamageSource const& source) {
    auto [attacker, direct] = entitiesOf(source);
    (void)victim;
    switch (source.mCause) {
    case Cause::EntityAttack:
    case Cause::RamAttack:  // Goat: mobAttack
    case Cause::Charging:   // a charging mob's hit
    case Cause::MaceSmash:  // 1.21.1's mace hits with playerAttack (no mace_smash type yet)
        if (attacker && attacker->isPlayer()) return "minecraft:player_attack";
        if (attacker && attacker->getTypeName() == "minecraft:bee") return "minecraft:sting"; // Bee#doHurtTarget
        return "minecraft:mob_attack";
    case Cause::Projectile: {
        std::string d = direct ? direct->getTypeName() : std::string{};
        if (d == "minecraft:arrow") return "minecraft:arrow";
        if (d == "minecraft:thrown_trident") return "minecraft:trident";
        if (d == "minecraft:fireball" || d == "minecraft:small_fireball") return attacker ? "minecraft:fireball" : "minecraft:unattributed_fireball";
        if (d == "minecraft:wither_skull" || d == "minecraft:wither_skull_dangerous") return "minecraft:wither_skull";
        if (d == "minecraft:wind_charge_projectile" || d == "minecraft:breeze_wind_charge_projectile" || d == "minecraft:wind_charge" || d == "minecraft:breeze_wind_charge")
            return "minecraft:wind_charge";
        if (d == "minecraft:llama_spit") return "minecraft:spit";            // LlamaSpit#onHitEntity
        if (d == "minecraft:shulker_bullet") return "minecraft:mob_projectile"; // ShulkerBullet#onHitEntity
        return "minecraft:thrown";
    }
    case Cause::Temperature: return "minecraft:on_fire"; // SnowGolem#aiStep in a warm biome
    case Cause::Override: return "minecraft:generic_kill";
    case Cause::Suffocation: return "minecraft:in_wall";
    case Cause::Fall: return "minecraft:fall";
    case Cause::Fire: return "minecraft:in_fire";
    case Cause::FireTick: return "minecraft:on_fire";
    case Cause::Lava: return "minecraft:lava";
    case Cause::Drowning: return "minecraft:drown";
    case Cause::BlockExplosion: return "minecraft:explosion";
    case Cause::EntityExplosion: return attacker && attacker->isPlayer() ? "minecraft:player_explosion" : "minecraft:explosion";
    case Cause::Void: return "minecraft:out_of_world";
    case Cause::SelfDestruct: return "minecraft:generic_kill";
    case Cause::Magic: return attacker ? "minecraft:indirect_magic" : "minecraft:magic";
    case Cause::Wither: return "minecraft:wither";
    case Cause::Starve: return "minecraft:starve";
    case Cause::Anvil: return "minecraft:falling_anvil";
    case Cause::Thorns: return "minecraft:thorns";
    case Cause::FallingBlock: return "minecraft:falling_block";
    case Cause::FlyIntoWall: return "minecraft:fly_into_wall";
    case Cause::Magma: return "minecraft:hot_floor";
    case Cause::Fireworks: return "minecraft:fireworks";
    case Cause::Lightning: return "minecraft:lightning_bolt";
    case Cause::Freezing: return "minecraft:freeze";
    case Cause::Stalactite: return "minecraft:falling_stalactite";
    case Cause::Stalagmite: return "minecraft:stalagmite";
    case Cause::SonicBoom: return "minecraft:sonic_boom";
    case Cause::Campfire:
    case Cause::SoulCampfire: return "minecraft:campfire";
    case Cause::Contact: return "minecraft:cactus";
    default: return "minecraft:generic";
    }
}

float healthOf(Actor& a) { return static_cast<float>(a.getHealth()); }

/// Recursion guard: the attacks whose handlers run now. A hurt started inside the handlers runs the
/// handlers again, as Java's nested damage events do (a handler that cancels a mob's hit and deals
/// it again as an upstream damage type: EntityUtil#properlyApplyCustomDamageSource, a part forwarding to
/// its parent, a reflected blast); only the same attack again (victim, type, attacker and direct
/// entity) on top of itself is applied as is, and so is anything nested deeper than kMaxNesting on
/// one victim, so two handlers that keep answering each other cannot recurse without end.
///
/// A conversion (a hurt of the same victim by the same attacker and direct entity started from an
/// onIncoming handler of the hit: EntityUtil#properlyApplyCustomDamageSource, Wraith#doHurtTarget)
/// continues that hit: the incoming handlers that already ran on it (their changes are in the amount
/// the converter hands on) do not run again, the converter and the ones after it see the new type.
/// A handler that changes DamageInfo::type converts the hit the same way without the nested call.
struct Attack {
    Actor const* victim;
    std::string  type;
    Actor const* attacker;
    Actor const* direct;
    bool         incoming = false;         ///< the scope runs the onIncoming handlers
    std::vector<std::uint64_t> ran;        ///< the incoming handlers done with this hit (registration ids)
    float const* amount = nullptr;         ///< the hit's amount as its handlers left it so far
};
thread_local std::vector<Attack> tInHandlers;
constexpr int                    kMaxNesting = 8;

struct HandlerScope {
    explicit HandlerScope(Attack attack) { tInHandlers.push_back(std::move(attack)); }
    ~HandlerScope() { tInHandlers.pop_back(); }
    HandlerScope(HandlerScope const&)            = delete;
    HandlerScope& operator=(HandlerScope const&) = delete;
};

/// The incoming handlers a conversion of the innermost hit of the same victim, attacker and direct
/// entity skips (nullptr: not a conversion). A conversion deals the hit again: the amount its
/// handlers left (an extra hit of another amount from a handler is a hit of its own).
std::vector<std::uint64_t> const* convertedFrom(Attack const& attack, float damage) {
    for (auto it = tInHandlers.rbegin(); it != tInHandlers.rend(); ++it) {
        if (it->victim != attack.victim) continue;
        if (it->incoming && it->attacker == attack.attacker && it->direct == attack.direct && it->amount && std::abs(*it->amount - damage) < 1e-4f)
            return &it->ran;
        return nullptr; // the innermost hit of this victim is another one
    }
    return nullptr;
}

/// Whether `attack` must skip the handlers: the same attack is in its handlers already, or the
/// victim's handler nesting is too deep.
bool inHandlers(Attack const& attack) {
    int depth = 0;
    for (auto const& a : tInHandlers) {
        if (a.victim != attack.victim) continue;
        if (a.type == attack.type && a.attacker == attack.attacker && a.direct == attack.direct) return true;
        if (++depth >= kMaxNesting) return true;
    }
    return false;
}

/// The victims inside a hook's damage pipeline (Actor::hurt or a direct Mob::_hurt): the inner
/// Mob::_hurt of a hurt the pipeline already runs passes straight through.
thread_local std::vector<Actor const*> tInPipeline;

struct PipelineScope {
    explicit PipelineScope(Actor const& a) { tInPipeline.push_back(&a); }
    ~PipelineScope() { tInPipeline.pop_back(); }
    PipelineScope(PipelineScope const&)            = delete;
    PipelineScope& operator=(PipelineScope const&) = delete;
};

bool inPipeline(Actor const& a) { return std::find(tInPipeline.begin(), tInPipeline.end(), &a) != tInPipeline.end(); }

/// A player's hurt in progress with a damage type whose DamageEffects are not "hurt": the game's
/// hurt sound for it is replaced (Player::getCustomHurtSound hook).
struct EffectHurt {
    Actor const* victim;
    std::string  effects;
    Cause        cause;
    bool         replaced = false;
};
thread_local std::vector<EffectHurt> tEffectHurts;

struct EffectHurtScope {
    EffectHurtScope(Actor const& victim, std::string effects, Cause cause) { tEffectHurts.push_back({&victim, std::move(effects), cause}); }
    ~EffectHurtScope() { tEffectHurts.pop_back(); }
    EffectHurtScope(EffectHurtScope const&)            = delete;
    EffectHurtScope& operator=(EffectHurtScope const&) = delete;
    [[nodiscard]] bool replaced() const { return tEffectHurts.back().replaced; }
};

/// The damage type's effects of `victim` when they replace its hurt sound ("" for none).
std::string effectsFor(Actor const& victim, std::string const& typeId) {
    if (!victim.isPlayer()) return {};
    auto const* spec = damageType(typeId);
    if (!spec || spec->effects.empty() || spec->effects == "hurt") return {};
    return spec->effects;
}

std::atomic<int> gHurtSoundsReplaced{0};

/// Player#getHurtSound on Bedrock: the client picks a hurt player's sound from the damage cause the
/// server's Hurt actor event carries (Player::getCustomHurtSound: fire, freezing and drowning have
/// their own player hurt sounds). While a hurt whose DamageEffects are burning, freezing, drowning
/// or thorns runs, the event carries the cause of that effect, so every client (vanilla too) plays
/// the effect's sound in place of the plain hurt sound. Only an event whose data is the hurt's own
/// cause is rewritten. upstream's pinch has no cause of its own: gf::fx plays it (playDamageEffect).
std::optional<int> hurtEventCause(Actor const& actor, int data) {
    for (auto it = tEffectHurts.rbegin(); it != tEffectHurts.rend(); ++it) {
        if (it->victim != &actor) continue;
        if (data != static_cast<int>(it->cause)) return std::nullopt;
        auto const&          fx = it->effects;
        std::optional<Cause> cause;
        // a fire cause of the game's own (a campfire, magma, lava) already sounds like burning
        bool burningCause = it->cause == Cause::Fire || it->cause == Cause::FireTick || it->cause == Cause::Lava || it->cause == Cause::Magma
                         || it->cause == Cause::Campfire || it->cause == Cause::SoulCampfire;
        if (fx == "burning" && burningCause) return std::nullopt;
        if (fx == "burning") cause = Cause::Fire;
        else if (fx == "freezing") cause = Cause::Freezing;
        else if (fx == "drowning") cause = Cause::Drowning;
        else if (fx == "thorns") cause = Cause::Thorns;
        if (!cause) return std::nullopt;
        it->replaced = true;
        ++gHurtSoundsReplaced;
        return static_cast<int>(*cause);
    }
    return std::nullopt;
}

/// The damage effect's sound through gf::fx: upstream's pinch always (no damage cause sounds like it; the
/// client's plain hurt sound plays with it), the vanilla effects only when the Hurt event could not
/// carry their cause. An effect the cause already sounds like (fire, freezing, drowning, thorns) is
/// not played twice.
void playDamageEffect(Actor& victim, std::string const& typeId, Cause cause, bool replaced) {
    auto effects = effectsFor(victim, typeId);
    if (effects.empty() || (replaced && effects != "gloamingforest:pinch")) return;
    std::string event;
    if (effects == "burning") {
        if (cause == Cause::Fire || cause == Cause::FireTick || cause == Cause::Lava || cause == Cause::Magma || cause == Cause::Campfire
            || cause == Cause::SoulCampfire)
            return;
        event = "minecraft:entity.player.hurt_on_fire";
    } else if (effects == "freezing") {
        if (cause == Cause::Freezing) return;
        event = "minecraft:entity.player.hurt_freeze";
    } else if (effects == "drowning") {
        if (cause == Cause::Drowning) return;
        event = "minecraft:entity.player.hurt_drown";
    } else if (effects == "thorns") {
        if (cause == Cause::Thorns) return;
        event = "minecraft:enchant.thorns.hit";
    } else if (effects == "poking") {
        event = "minecraft:entity.player.hurt_sweet_berry_bush";
    } else if (effects == "gloamingforest:pinch") {
        event = "gloamingforest:entity.gloamingforest.pinch_beetle.attack"; // TFSounds.PINCH_BEETLE_ATTACK
    } else {
        return;
    }
    // LivingEntity#playHurtSound: the sound volume (1) and the voice pitch of an adult
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    float pitch = (unit(rng) - unit(rng)) * 0.2f + 1.0f;
    fx::sound(victim.getDimension(), event, victim.getPosition(), 1.f, pitch);
}

} // namespace

// ---- internal API --------------------------------------------------------------------------------

std::optional<int> hurtEventOverride(Actor const& actor, int data) { return hurtEventCause(actor, data); }

int hurtSoundsReplaced() { return gHurtSoundsReplaced.load(); }

DamageTypeSpec const* damageType(std::string const& id) {
    auto&           t = types();
    std::lock_guard lock(t.mutex);
    if (!t.loaded) loadTypes(t);
    auto it = t.types.find(id);
    return it == t.types.end() ? nullptr : &it->second;
}

std::vector<std::string> damageTypeIds() {
    auto&           t = types();
    std::lock_guard lock(t.mutex);
    if (!t.loaded) loadTypes(t);
    std::vector<std::string> out;
    for (auto const& [id, _] : t.types) out.push_back(id);
    return out;
}

bool damageTypeHasTag(std::string const& id, std::string const& tag) { return hasTagAt(id, tag, 0); }

Cause causeFor(std::string const& id, bool hasAttacker) {
    if (auto vanilla = vanillaCause(id, hasAttacker)) return *vanilla;
    if (damageTypeHasTag(id, "minecraft:bypasses_invulnerability")) return Cause::Override;
    if (damageTypeHasTag(id, "minecraft:bypasses_armor") && !damageTypeHasTag(id, "minecraft:is_fall")) return Cause::Magic;
    if (damageTypeHasTag(id, "minecraft:is_fire")) return Cause::Fire;
    if (damageTypeHasTag(id, "minecraft:is_explosion")) return Cause::EntityExplosion;
    if (damageTypeHasTag(id, "minecraft:is_fall")) return Cause::Fall;
    if (damageTypeHasTag(id, "minecraft:is_freezing")) return Cause::Freezing;
    if (damageTypeHasTag(id, "minecraft:is_projectile")) return Cause::Projectile;
    if (auto const* spec = damageType(id); spec && spec->messageId == "fallingBlock") return Cause::FallingBlock;
    return hasAttacker ? Cause::EntityAttack : Cause::Contact;
}

std::pair<Actor*, Actor*> entitiesOf(ActorDamageSource const& source) {
    if (!source.isEntitySource()) return {nullptr, nullptr};
    auto level = ll::service::getLevel();
    if (!level) return {nullptr, nullptr};
    // ActorDamageByChildActorSource(child, owner): getEntityUniqueID is the owner (Java's
    // DamageSource#getEntity), getDamagingEntityUniqueID the child (the projectile, #getDirectEntity);
    // a plain ActorDamageByActorSource names one entity, both
    Actor* attacker = level->fetchEntity(source.getEntityUniqueID(), false);
    // (a child that is gone already leaves no direct entity; a shooterless one is its own causer)
    Actor* direct   = source.isChildEntitySource() ? level->fetchEntity(source.getDamagingEntityUniqueID(), false) : attacker;
    if (!attacker) attacker = direct;
    return {attacker, direct};
}

std::string typeOf(Actor& victim, ActorDamageSource const& source) {
    for (auto it = tPending.rbegin(); it != tPending.rend(); ++it)
        if (it->victim == &victim) return it->type;
    return vanillaTypeOf(victim, source);
}

std::string nameParam(Actor& actor) {
    if (actor.isPlayer()) return static_cast<Player&>(actor).getRealName();
    auto const& tag = actor.getNameTag();
    if (!tag.empty()) return tag;
    std::string type = actor.getTypeName();
    if (type.rfind("minecraft:", 0) == 0) type = type.substr(10);
    return "%entity." + type + ".name";
}

std::pair<std::string, std::vector<std::string>> deathMessageFor(Actor& victim, std::string const& deadName, std::optional<Cause> killingCause) {
    auto m = markOf(victim);
    if (!m) return {};
    // the killing damage must be the upstream one (not other damage in the same tick)
    if (killingCause && *killingCause != causeFor(m->type, m->attacker != 0)) return {};
    auto const* spec = damageType(m->type);
    if (!spec) return {};
    // a vanilla type: Bedrock's own message for its cause (its lang names the deaths of its own
    // damage), or Bedrock's key for it where the cause would name it otherwise
    if (spec->vanilla) {
        if (spec->bedrockMessageId.empty()) return {};
        return {"death.attack." + spec->bedrockMessageId, {deadName}};
    }
    std::string key      = "death.attack." + spec->messageId;
    Actor*      attacker = fetch(m->attacker);
    Actor*      direct   = fetch(m->direct);
    if (!m->attacker && !m->direct) {
        // Java: the kill credit (LivingEntity#getKillCredit: the player within 100 ticks of its
        // hit, else the last mob) -> ".player"
        Actor* killer = victim.mLastHurtByPlayerTime > 0 ? fetch(victim.mLastHurtByPlayerId->rawID) : nullptr;
        if (!killer && victim.mLastHurtByMobTime > 0) killer = fetch(victim.mLastHurtByMobId->rawID);
        if (killer && killer != &victim) return {key + ".player", {deadName, nameParam(*killer)}};
        return {key, {deadName}};
    }
    Actor* named = attacker ? attacker : direct;
    if (!named) return {key, {deadName}}; // the entities are gone: the plain message
    std::string name = nameParam(*named);
    if (attacker && attacker->hasCategory(ActorCategory::Mob)) {
        auto const& held = static_cast<Mob*>(attacker)->getCarriedItem();
        if (!held.isNull()) {
            auto custom = held.getCustomName();
            if (!custom.empty()) return {key + ".item", {deadName, name, custom}};
        }
    }
    return {key, {deadName, name}};
}

// ---- contract -------------------------------------------------------------------------------------

void onIncoming(int priority, IncomingFn fn) {
    std::lock_guard lock(handlers().mutex);
    addSorted(handlers().incoming, priority, std::move(fn));
}

void onDealt(int priority, DealtFn fn) {
    std::lock_guard lock(handlers().mutex);
    addSorted(handlers().dealt, priority, std::move(fn));
}

namespace ext {
HandlerHandle addIncoming(int priority, IncomingFn fn) {
    if (!fn) return 0;
    std::lock_guard lock(handlers().mutex);
    return addSorted(handlers().incoming, priority, std::move(fn));
}
HandlerHandle addDealt(int priority, DealtFn fn) {
    if (!fn) return 0;
    std::lock_guard lock(handlers().mutex);
    return addSorted(handlers().dealt, priority, std::move(fn));
}
bool remove(HandlerHandle handle) {
    if (handle == 0) return false;
    // the removed function is destroyed outside the lock (its captures may hold anything)
    std::vector<IncomingFn> goneIn;
    std::vector<DealtFn>    goneDealt;
    {
        std::lock_guard lock(handlers().mutex);
        auto            take = [&](auto& list, auto& gone) {
            for (auto it = list.begin(); it != list.end(); ++it)
                if (it->seq == handle) {
                    gone.push_back(std::move(it->fn));
                    list.erase(it);
                    return true;
                }
            return false;
        };
        if (!take(handlers().incoming, goneIn)) take(handlers().dealt, goneDealt);
    }
    return !goneIn.empty() || !goneDealt.empty();
}
} // namespace ext

bool hurt(Actor& victim, float amount, std::string const& typeId, Actor* attacker, Actor* direct) {
    if (victim.isClientSide()) return false;
    if (!damageType(typeId)) log().warn("combat: unknown damage type {} (applied as generic damage)", typeId);
    if (!attacker && direct) attacker = direct;
    auto cause    = causeFor(typeId, attacker != nullptr);
    auto previous = markOf(victim);
    mark(victim, typeId, attacker, direct && direct != attacker ? direct : nullptr);
    HurtParameters params{};
    params.mKnockback = (attacker || direct) && !damageTypeHasTag(typeId, "minecraft:no_knockback");
    params.mIgnition  = false;
    tPending.push_back({&victim, typeId});
    ActorHurtResult result{};
    try {
        if (attacker && direct && direct != attacker) {
            ActorDamageByChildActorSource source(*direct, *attacker, cause);
            result = victim.hurt(source, amount, params);
        } else if (attacker) {
            ActorDamageByActorSource source(*attacker, cause);
            result = victim.hurt(source, amount, params);
        } else {
            ActorDamageSource source(cause, std::string{});
            result = victim.hurt(source, amount, params);
        }
    } catch (...) {
        tPending.pop_back();
        throw;
    }
    bool converted = tPending.back().converted;
    tPending.pop_back();
    bool applied = static_cast<bool>(result);
    if (!applied) restoreMark(victim, previous); // a refused hurt names no death
    if (applied && !converted && victim.isPlayer()) { // a converted hit took its new type's exhaustion
        auto const* spec = damageType(typeId);
        if (spec && spec->exhaustion > 0) static_cast<Player&>(victim).causeFoodExhaustion(spec->exhaustion);
    }
    return applied;
}

} // namespace gf::combat

// ---- hooks ---------------------------------------------------------------------------------------

namespace gf::combat {
namespace {

/// The damage pipeline around the game's hurt (`origin(amount)`): the onIncoming handlers, the
/// hurt, the survivor's health of a cancelled death, the damage effect sound and the onDealt
/// handlers. Runs once per damage: Actor::hurt (every normal path) and Mob::_hurt when something
/// calls it directly (LeviLamina's hurtByCause, game code skipping Actor::hurt).
template <class Origin>
ActorHurtResult runPipeline(Actor& self, ActorDamageSource const& source, float damage, Origin&& origin) {
    PipelineScope pipeline(self);
    // the game writes the lethal health after `die`: a death the pipeline cancelled gets the
    // survivor's health back once the hurt returns
    auto plain = [&](float amount) {
        auto r = origin(amount);
        gf::death::reapplySurvival(self);
        return r;
    };
    std::vector<Entry<IncomingFn>> incoming;
    std::vector<DealtFn>           dealt;
    {
        std::lock_guard lock(handlers().mutex);
        incoming = handlers().incoming;
        for (auto const& e : handlers().dealt) dealt.push_back(e.fn);
    }
    // nothing to tell and no effect sound (players only): the game's hurt as is
    if (incoming.empty() && dealt.empty() && !self.isPlayer()) return plain(damage);
    auto [attacker, direct] = entitiesOf(source);
    DamageInfo info{self, typeOf(self, source), attacker, direct, damage};
    Attack     attack{&self, info.type, attacker, direct};
    if (inHandlers(attack)) return plain(damage);
    // a conversion continues its hit: the handlers done with it are not run again
    std::vector<std::uint64_t> skip;
    if (auto const* ran = convertedFrom(attack, damage)) skip = *ran;
    std::string const originalType = info.type;
    if (!incoming.empty()) {
        Attack running = attack;
        running.incoming = true;
        running.ran      = skip;
        running.amount   = &info.amount;
        tInHandlers.push_back(std::move(running));
        struct Pop {
            ~Pop() { tInHandlers.pop_back(); }
        } pop;
        for (auto const& e : incoming) {
            if (std::find(skip.begin(), skip.end(), e.seq) != skip.end()) continue;
            Verdict v = Verdict::Pass;
            try {
                v = e.fn(info);
            } catch (std::exception const& ex) {
                gf::log().error("combat: incoming handler failed: {}", ex.what());
            }
            tInHandlers.back().ran.push_back(e.seq);
            if (v == Verdict::Cancel) {
                ActorHurtResult cancelled{};
                cancelled.mHurtOrDamage         = std::variant<bool, float>(false);
                cancelled.mShouldAllowKnockback = false;
                return cancelled;
            }
        }
        // a handler changed the damage type: the hit lands as that type (its cause, knockback,
        // death message, exhaustion and onDealt), the incoming handlers done with it skipped
        if (info.type != originalType) {
            if (!damageType(info.type)) log().warn("combat: a handler changed the damage to the unknown type {}", info.type);
            // the entities the converted hurt reports (gf::combat::hurt's damage source)
            Actor* a = info.attacker ? info.attacker : info.direct;
            tInHandlers.back().attacker = a;
            tInHandlers.back().direct   = a && info.direct && info.direct != a ? info.direct : a;
            // the gf::combat::hurt this hit runs in (if any) leaves the exhaustion to the new type
            if (!tPending.empty() && tPending.back().victim == &self) tPending.back().converted = true;
            bool landed = hurt(self, info.amount, info.type, info.attacker, info.direct);
            ActorHurtResult converted{};
            converted.mHurtOrDamage         = std::variant<bool, float>(landed);
            converted.mShouldAllowKnockback = landed;
            return converted;
        }
    }
    float before   = healthOf(self);
    bool  replaced = false;
    auto  result   = [&] {
        auto effects = effectsFor(self, info.type);
        if (effects.empty()) return plain(info.amount);
        EffectHurtScope scope(self, std::move(effects), source.mCause); // the Hurt event goes out during the hurt
        auto            r = plain(info.amount);
        replaced          = scope.replaced();
        return r;
    }();
    if (!result) return result;
    playDamageEffect(self, info.type, source.mCause, replaced);
    if (dealt.empty()) return result;
    float after = healthOf(self);
    DamageInfo applied{self, info.type, info.attacker, info.direct, info.amount};
    if (auto const* f = std::get_if<float>(&*result.mHurtOrDamage); f && *f > 0) applied.amount = *f;
    else if (before > after) applied.amount = before - after;
    HandlerScope scope(std::move(attack));
    for (auto const& fn : dealt) {
        try {
            fn(applied);
        } catch (std::exception const& ex) {
            gf::log().error("combat: dealt handler failed: {}", ex.what());
        }
    }
    return result;
}

} // namespace
} // namespace gf::combat

LL_TYPE_INSTANCE_HOOK(
    CombatHurtHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::hurt,
    ActorHurtResult,
    ActorDamageSource const& source,
    float                    damage,
    HurtParameters const&    params
) {
    if (isClientSide()) return origin(source, damage, params);
    return gf::combat::runPipeline(*this, source, damage, [&](float amount) { return origin(source, amount, params); });
}

// Damage that reaches a mob's (or player's) _hurt without Actor::hurt: the same pipeline. The
// _hurt inside a hurt the pipeline already runs passes through.
LL_TYPE_INSTANCE_HOOK(
    CombatMobDirectHurtHook,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$_hurt,
    ActorHurtResult,
    ActorDamageSource const& source,
    float                    damage,
    HurtParameters const&    params
) {
    if (isClientSide() || gf::combat::inPipeline(*this)) return origin(source, damage, params);
    return gf::combat::runPipeline(*this, source, damage, [&](float amount) { return origin(source, amount, params); });
}

using GfDeathMessage = std::pair<std::string, std::vector<std::string>>;
LL_TYPE_INSTANCE_HOOK(
    CombatDeathMessageHook,
    ll::memory::HookPriority::Normal,
    ActorDamageSource,
    &ActorDamageSource::getDeathMessage,
    GfDeathMessage,
    std::string const& deadName,
    Actor*             dead
) {
    if (dead) {
        auto tf = gf::combat::deathMessageFor(*dead, deadName, mCause);
        if (!tf.first.empty()) return tf;
    }
    return origin(deadName, dead);
}

// The Hurt actor event of a player whose hurt has DamageEffects: the cause it carries picks the
// client's hurt sound (Player::getCustomHurtSound runs on the client).
LL_TYPE_INSTANCE_HOOK(
    CombatHurtEventHook,
    ll::memory::HookPriority::Normal,
    Level,
    &Level::$broadcastActorEvent,
    void,
    Actor&                         actor,
    ActorEvent                     eventId,
    int                            data,
    std::optional<Vec3> const&     fireAtPosition
) {
    if (eventId == ActorEvent::Hurt)
        if (auto cause = gf::combat::hurtEventOverride(actor, data)) data = *cause;
    origin(actor, eventId, data, fireAtPosition);
}

namespace gf::combat {
namespace {
class CombatModule {
public:
    void install() {
        CombatHurtHook::hook();
        CombatMobDirectHurtHook::hook();
        CombatDeathMessageHook::hook();
        CombatHurtEventHook::hook();
    }
    void uninstall() {
        CombatHurtEventHook::unhook();
        CombatDeathMessageHook::unhook();
        CombatMobDirectHurtHook::unhook();
        CombatHurtHook::unhook();
    }
};
} // namespace
} // namespace gf::combat

GF_REGISTER_MODULE_ORDERED(gf::combat::CombatModule, gf::ModuleOrder::Contracts);

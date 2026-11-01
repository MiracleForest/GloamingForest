// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFDamageTypes.java
//   src/main/java/twilightforest/data/tags/DamageTypeTagGenerator.java (the generated tag data)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "combat" (F10): the single hurt hook of the mod (rule R8) and TF's damage types.
//
// - One damage pipeline: `onIncoming` handlers (higher priority first) see every damage before it
//   applies (armour not applied yet) and may change the amount or cancel it; `onDealt` handlers
//   hear about damage that was applied, with the health it took. It runs from Actor::hurt (every
//   normal damage path) and from Mob::_hurt when that is called without Actor::hurt (LeviLamina's
//   Actor::hurtByCause, game code calling the virtual directly), once per damage (a guard lets the
//   inner _hurt of a hurt in progress through). Non-mob actors (boats, item entities, hanging
//   entities) are only seen through Actor::hurt.
// - A player hurt by a type whose DamageEffects are not "hurt" plays the effect's sound
//   (Player#getHurtSound: burning, freezing, TF's pinch) through gf::fx.
// - Damage types: TF's 40 (TFDamageTypes#bootstrap: message id, exhaustion, effects) are built
//   in; <mod>/data/<ns>/damage_type/*.json (F1's import) override and add. Vanilla damage is
//   named by Java's ids ("minecraft:player_attack", "minecraft:fall", ...) from the Bedrock cause.
// - gf::combat::hurt(victim, amount, type, attacker, direct) builds the Bedrock damage source:
//   the cause follows the type's tags (bypasses_armor -> Magic, is_fire -> Fire, is_explosion ->
//   EntityExplosion, is_fall -> Fall, is_projectile -> Projectile, ...), no_knockback turns the
//   knockback off, the type's exhaustion is added to a hurt player, and the death message is
//   Java's (DamageSource#getLocalizedDeathMessage: "death.attack.<msgId>", ".player" with the kill
//   credit, ".item" with a named weapon) through one ActorDamageSource#getDeathMessage hook.

#include "feature/combat/Combat.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
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
        {"ghast_tear", "twilightforest.ghastTear", 0.0f, "hurt"},
        {"hydra_bite", "twilightforest.hydraBite", 0.0f, "hurt"},
        {"hydra_fire", "twilightforest.hydraFire", 0.0f, "hurt"},
        {"hydra_mortar", "onFire", 0.0f, "burning"},
        {"lich_bolt", "twilightforest.lichBolt", 0.0f, "hurt"},
        {"lich_bomb", "twilightforest.lichBomb", 0.0f, "hurt"},
        {"chilling_breath", "twilightforest.chillingBreath", 0.0f, "freezing"},
        {"squish", "twilightforest.squish", 0.0f, "hurt"},
        {"thrown_axe", "twilightforest.thrownAxe", 0.0f, "hurt"},
        {"thrown_pickaxe", "twilightforest.thrownPickaxe", 0.0f, "hurt"},
        {"thorns", "twilightforest.thorns", 0.1f, "hurt"},
        {"oreberry", "twilightforest.oreberry", 0.1f, "hurt"},
        {"knightmetal", "twilightforest.knightmetal", 0.1f, "hurt"},
        {"fiery", "twilightforest.fiery", 0.1f, "burning"},
        {"fire_jet", "twilightforest.fireJet", 0.1f, "burning"},
        {"reactor", "twilightforest.reactor", 0.1f, "hurt"},
        {"slider", "twilightforest.slider", 0.1f, "hurt"},
        {"thrown_block", "twilightforest.thrownBlock", 0.1f, "hurt"},
        {"axing", "twilightforest.axing", 0.1f, "hurt"},
        {"slam", "twilightforest.axing", 0.1f, "hurt"},
        {"yeeted", "twilightforest.yeeted", 0.1f, "hurt"},
        {"ant", "twilightforest.ant", 0.1f, "hurt"},
        {"haunt", "twilightforest.haunt", 0.1f, "hurt"},
        {"clamped", "twilightforest.clamped", 0.1f, "twilightforest:pinch"},
        {"scorched", "twilightforest.scorched", 0.1f, "burning"},
        {"frozen", "twilightforest.frozen", 0.1f, "freezing"},
        {"spiked", "twilightforest.spiked", 0.1f, "hurt"},
        {"leaf_brain", "twilightforest.leafBrain", 0.1f, "hurt"},
        {"lost_words", "twilightforest.lostWords", 0.1f, "hurt"},
        {"schooled", "twilightforest.schooled", 0.1f, "hurt"},
        {"snowball_fight", "twilightforest.snowballFight", 0.1f, "freezing"},
        {"twilight_scepter", "indirectMagic", 0.0f, "hurt"},
        {"lifedrain", "twilightforest.lifedrain", 0.0f, "hurt"},
        {"expired", "twilightforest.expired", 0.0f, "hurt"},
        {"falling_ice", "fallingBlock", 0.1f, "hurt"},
        {"moonworm", "twilightforest.moonworm", 0.0f, "hurt"},
        {"acid_rain", "twilightforest.acid_rain", 0.0f, "hurt"},
        {"ominous_fire", "twilightforest.ominous", 0.1f, "burning"},
        {"failed_challenge", "twilightforest.failedChallenge", 0.0f, "hurt"},
        {"stale_sandwich", "twilightforest.stale_sandwich", 0.0f, "hurt"},
    };
    std::map<std::string, DamageTypeSpec> out;
    for (auto const& r : rows) {
        DamageTypeSpec s;
        s.id         = std::string("twilightforest:") + r.path;
        s.messageId  = r.msg;
        s.exhaustion = r.exhaustion;
        s.effects    = r.effects;
        out[s.id]    = s;
    }
    return out;
}

/// TF's memberships in the vanilla / NeoForge damage type tags (the generated tag data).
std::map<std::string, std::set<std::string>> const& builtinTags() {
    static std::map<std::string, std::set<std::string>> const t = [] {
        auto tf = [](std::initializer_list<char const*> paths) {
            std::set<std::string> s;
            for (auto p : paths) s.insert(std::string("twilightforest:") + p);
            return s;
        };
        std::map<std::string, std::set<std::string>> m;
        m["minecraft:always_most_significant_fall"] = tf({"expired"});
        m["minecraft:avoids_guardian_thorns"]       = tf({"ghast_tear", "hydra_fire", "hydra_mortar", "slam", "yeeted"});
        m["minecraft:bypasses_armor"] = tf({"ghast_tear", "lich_bolt", "lich_bomb", "slam", "yeeted", "leaf_brain", "lost_words", "schooled", "lifedrain", "expired", "acid_rain", "ominous_fire"});
        m["minecraft:bypasses_enchantments"]     = tf({"falling_ice"});
        m["minecraft:bypasses_invulnerability"] = tf({"expired"});
        m["minecraft:bypasses_resistance"]      = tf({"expired"});
        m["minecraft:bypasses_wolf_armor"] = tf({"lich_bolt", "lich_bomb", "frozen", "leaf_brain", "lost_words", "schooled", "lifedrain", "expired", "acid_rain"});
        m["minecraft:damages_helmet"]      = tf({"ghast_tear", "thrown_block"});
        m["minecraft:ignites_armor_stands"] = tf({"hydra_fire", "hydra_mortar", "fire_jet", "scorched"});
        m["minecraft:is_explosion"]         = tf({"lich_bomb"});
        m["minecraft:is_fall"]              = tf({"yeeted"});
        m["minecraft:is_fire"]              = tf({"hydra_fire", "hydra_mortar", "fiery", "fire_jet", "scorched"});
        m["minecraft:is_projectile"] = tf({"lich_bolt", "lich_bomb", "thrown_axe", "thrown_pickaxe", "thrown_block", "leaf_brain", "lost_words", "schooled", "snowball_fight", "twilight_scepter", "lifedrain"});
        m["minecraft:no_anger"]     = tf({"slam"});
        m["minecraft:no_knockback"] = tf({"ghast_tear", "thorns", "oreberry", "fiery", "fire_jet", "reactor", "clamped", "moonworm", "acid_rain", "ominous_fire"});
        m["minecraft:panic_environmental_causes"] = tf({"ominous_fire"});
        m["minecraft:witch_resistant_to"]         = tf({"lich_bolt", "lich_bomb", "leaf_brain", "lost_words", "schooled", "acid_rain"});
        m["minecraft:wither_immune_to"]           = tf({"ominous_fire"});
        m["neoforge:is_environment"] = tf({"thorns", "oreberry", "knightmetal", "fiery", "fire_jet", "reactor", "slider", "falling_ice", "acid_rain"});
        m["neoforge:is_magic"] = tf({"lich_bolt", "lich_bomb", "chilling_breath", "reactor", "haunt", "frozen", "leaf_brain", "lost_words", "schooled", "snowball_fight", "twilight_scepter", "lifedrain", "acid_rain", "ominous_fire"});
        m["neoforge:is_physical"] = tf({"ghast_tear", "hydra_bite", "squish", "thrown_axe", "thrown_pickaxe", "thorns", "oreberry", "fiery", "fire_jet", "slider", "thrown_block", "axing", "slam", "yeeted", "ant", "clamped", "spiked", "falling_ice", "moonworm"});
        m["neoforge:is_technical"]               = tf({"expired"});
        m["twilightforest:breaks_lich_shields"] = {"twilightforest:lich_bolt", "twilightforest:twilight_scepter", "minecraft:magic", "minecraft:indirect_magic", "minecraft:sonic_boom"};
        // vanilla members TF relies on (vanilla's own tags)
        m["minecraft:is_fire"].insert({"minecraft:in_fire", "minecraft:on_fire", "minecraft:lava", "minecraft:hot_floor", "minecraft:fireball", "minecraft:unattributed_fireball", "minecraft:campfire"});
        m["minecraft:is_projectile"].insert({"minecraft:arrow", "minecraft:trident", "minecraft:mob_projectile", "minecraft:fireball", "minecraft:wither_skull", "minecraft:thrown", "minecraft:unattributed_fireball", "minecraft:wind_charge"});
        m["minecraft:is_fall"].insert({"minecraft:fall", "minecraft:stalagmite", "minecraft:ender_pearl"});
        m["minecraft:is_explosion"].insert({"minecraft:explosion", "minecraft:player_explosion", "minecraft:fireworks", "minecraft:bad_respawn_point"});
        m["minecraft:bypasses_armor"].insert({"minecraft:magic", "minecraft:indirect_magic", "minecraft:wither", "minecraft:starve", "minecraft:drown", "minecraft:fall", "minecraft:out_of_world", "minecraft:generic_kill", "minecraft:freeze", "minecraft:in_wall", "minecraft:cramming", "minecraft:fly_into_wall", "minecraft:sonic_boom", "minecraft:dragon_breath", "minecraft:on_fire", "minecraft:stalagmite", "minecraft:outside_border"});
        m["minecraft:is_freezing"] = {"minecraft:freeze"};
        m["minecraft:is_drowning"] = {"minecraft:drown"};
        m["minecraft:is_lightning"] = {"minecraft:lightning_bolt"};
        return m;
    }();
    return t;
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
                t.types[s.id]      = std::move(s);
            } catch (std::exception const& ex) {
                log().warn("combat: bad damage type {}: {}", f.path().string(), ex.what());
            }
        }
    }
}

// ---- handlers ------------------------------------------------------------------------------------

template <class Fn>
struct Entry {
    int priority;
    int seq;
    Fn  fn;
};

struct Handlers {
    std::mutex                    mutex;
    std::vector<Entry<IncomingFn>> incoming;
    std::vector<Entry<DealtFn>>    dealt;
    int                           seq = 0;
};

Handlers& handlers() {
    static Handlers h;
    return h;
}

template <class Fn>
void addSorted(std::vector<Entry<Fn>>& list, int priority, Fn fn) {
    list.push_back({priority, ++handlers().seq, std::move(fn)});
    std::stable_sort(list.begin(), list.end(), [](auto const& a, auto const& b) { return a.priority > b.priority; });
}

// ---- the TF damage being applied / the marks for death messages -------------------------------------

/// gf::combat::hurt in progress (the hook reads the TF type from here).
struct Pending {
    Actor*      victim = nullptr;
    std::string type;
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

std::string vanillaTypeOf(Actor& victim, ActorDamageSource const& source) {
    auto [attacker, direct] = entitiesOf(source);
    (void)victim;
    switch (source.mCause) {
    case Cause::EntityAttack:
    case Cause::RamAttack: return attacker && attacker->isPlayer() ? "minecraft:player_attack" : "minecraft:mob_attack";
    case Cause::Projectile: {
        std::string d = direct ? direct->getTypeName() : std::string{};
        if (d == "minecraft:arrow") return "minecraft:arrow";
        if (d == "minecraft:thrown_trident") return "minecraft:trident";
        if (d == "minecraft:fireball" || d == "minecraft:small_fireball") return attacker ? "minecraft:fireball" : "minecraft:unattributed_fireball";
        if (d == "minecraft:wither_skull") return "minecraft:wither_skull";
        if (d == "minecraft:wind_charge" || d == "minecraft:breeze_wind_charge") return "minecraft:wind_charge";
        if (d == "minecraft:llama_spit" || d == "minecraft:shulker_bullet") return "minecraft:mob_projectile";
        return "minecraft:thrown";
    }
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
    case Cause::MaceSmash: return "minecraft:mace_smash";
    case Cause::Contact: return "minecraft:cactus";
    default: return "minecraft:generic";
    }
}

float healthOf(Actor& a) { return static_cast<float>(a.getHealth()); }

/// Recursion guard: the victims whose handlers run now; damage to one of them from inside its
/// own handlers is applied as is (other victims get their handlers, as Java's nested events).
thread_local std::vector<Actor const*> tInHandlers;

struct HandlerScope {
    explicit HandlerScope(Actor const& a) { tInHandlers.push_back(&a); }
    ~HandlerScope() { tInHandlers.pop_back(); }
    HandlerScope(HandlerScope const&)            = delete;
    HandlerScope& operator=(HandlerScope const&) = delete;
};

bool inHandlers(Actor const& a) { return std::find(tInHandlers.begin(), tInHandlers.end(), &a) != tInHandlers.end(); }

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

/// Player#getHurtSound: the damage type's DamageEffects sound (TF's types: burning, freezing and
/// TF's pinch). Bedrock plays its own hurt sound for the cause: an effect the cause already
/// sounds like (fire, freezing, drowning, thorns) is not played twice.
void playDamageEffect(Actor& victim, std::string const& typeId, Cause cause) {
    if (!victim.isPlayer()) return;
    auto const* spec = damageType(typeId);
    if (!spec || spec->effects.empty() || spec->effects == "hurt") return;
    std::string event;
    auto const& fx = spec->effects;
    if (fx == "burning") {
        if (cause == Cause::Fire || cause == Cause::FireTick || cause == Cause::Lava || cause == Cause::Magma) return;
        event = "minecraft:entity.player.hurt_on_fire";
    } else if (fx == "freezing") {
        if (cause == Cause::Freezing) return;
        event = "minecraft:entity.player.hurt_freeze";
    } else if (fx == "drowning") {
        if (cause == Cause::Drowning) return;
        event = "minecraft:entity.player.hurt_drown";
    } else if (fx == "thorns") {
        if (cause == Cause::Thorns) return;
        event = "minecraft:enchant.thorns.hit";
    } else if (fx == "poking") {
        event = "minecraft:entity.player.hurt_sweet_berry_bush";
    } else if (fx == "twilightforest:pinch") {
        event = "twilightforest:entity.twilightforest.pinch_beetle.attack"; // TFSounds.PINCH_BEETLE_ATTACK
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

bool damageTypeHasTag(std::string const& id, std::string const& tag) {
    auto const& b  = builtinTags();
    auto        it = b.find(tag);
    if (it != b.end() && it->second.contains(id)) return true;
    return tags::has(tags::Registry::DamageType, tag, id);
}

Cause causeFor(std::string const& id, bool hasAttacker) {
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
    Actor* direct   = level->fetchEntity(source.getEntityUniqueID(), false);
    Actor* attacker = level->fetchEntity(source.getDamagingEntityUniqueID(), false);
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
    // the killing damage must be the TF one (not other damage in the same tick)
    if (killingCause && *killingCause != causeFor(m->type, m->attacker != 0)) return {};
    auto const* spec = damageType(m->type);
    if (!spec) return {};
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
    tPending.pop_back();
    bool applied = static_cast<bool>(result);
    if (!applied) restoreMark(victim, previous); // a refused hurt names no death
    if (applied && victim.isPlayer()) {
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
    if (inHandlers(self)) return plain(damage);
    std::vector<IncomingFn> incoming;
    std::vector<DealtFn>    dealt;
    {
        std::lock_guard lock(handlers().mutex);
        for (auto const& e : handlers().incoming) incoming.push_back(e.fn);
        for (auto const& e : handlers().dealt) dealt.push_back(e.fn);
    }
    // nothing to tell and no effect sound (players only): the game's hurt as is
    if (incoming.empty() && dealt.empty() && !self.isPlayer()) return plain(damage);
    auto [attacker, direct] = entitiesOf(source);
    DamageInfo info{self, typeOf(self, source), attacker, direct, damage};
    if (!incoming.empty()) {
        HandlerScope scope(self);
        for (auto const& fn : incoming) {
            Verdict v = Verdict::Pass;
            try {
                v = fn(info);
            } catch (std::exception const& ex) {
                gf::log().error("combat: incoming handler failed: {}", ex.what());
            }
            if (v == Verdict::Cancel) {
                ActorHurtResult cancelled{};
                cancelled.mHurtOrDamage         = std::variant<bool, float>(false);
                cancelled.mShouldAllowKnockback = false;
                return cancelled;
            }
        }
    }
    float before = healthOf(self);
    auto  result = plain(info.amount);
    if (!result) return result;
    playDamageEffect(self, info.type, source.mCause);
    if (dealt.empty()) return result;
    float after = healthOf(self);
    DamageInfo applied{self, info.type, info.attacker, info.direct, info.amount};
    if (auto const* f = std::get_if<float>(&*result.mHurtOrDamage); f && *f > 0) applied.amount = *f;
    else if (before > after) applied.amount = before - after;
    HandlerScope scope(self);
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

namespace gf::combat {
namespace {
class CombatModule {
public:
    void install() {
        CombatHurtHook::hook();
        CombatMobDirectHurtHook::hook();
        CombatDeathMessageHook::hook();
    }
    void uninstall() {
        CombatDeathMessageHook::unhook();
        CombatMobDirectHurtHook::unhook();
        CombatHurtHook::unhook();
    }
};
} // namespace
} // namespace gf::combat

GF_REGISTER_MODULE_ORDERED(gf::combat::CombatModule, gf::ModuleOrder::Contracts);

// Selftests of F10's vanilla damage types and vanilla sound events (prefix "combat.", testPos dx
// 6078..6079):
//   - combat.vanilla_damage_types: Minecraft 1.21.1's 47 damage types (and NeoForge's
//     neoforge:poison) are known with Mojang's data, their tags (vanilla's own, NeoForge's additions
//     and neoforge: tags, nested tags) answer, and each has its Bedrock cause;
//   - combat.vanilla_damage_sources: Bedrock's own damage is named by the Java type of the same
//     event (the hurt pipeline's DamageInfo), the causes round-trip, gf::combat::hurt applies a
//     vanilla type with its cause (no "unknown damage type"), and a vanilla type's death keeps
//     Bedrock's message (or Bedrock's key where its cause would name it otherwise);
//   - combat.fx_vanilla_sounds: the vanilla sound events Java plays for TF (AbstractArrow's hits, the
//     DamageEffects sounds, the events TF's code names) map to Bedrock's sound names.

#include <algorithm>
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageByActorSource.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/combat/Combat.h"
#include "feature/combat/CombatInternal.h"
#include "feature/fx/FxInternal.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using Cause = SharedTypes::Legacy::ActorDamageCause;
namespace c = gf::combat;

/// The actor the test's incoming handler watches (0: none); what it saw. The handler cancels the
/// damage it sees (nothing is hurt), unless `sLetThrough`.
std::int64_t sWatched     = 0;
std::string  sSeen;
bool         sLetThrough  = false;
bool         sRegistered  = false;

struct WatchScope {
    explicit WatchScope(Actor& a) { sWatched = a.getOrCreateUniqueID().rawID; }
    ~WatchScope() {
        sWatched    = 0;
        sLetThrough = false;
    }
    WatchScope(WatchScope const&)            = delete;
    WatchScope& operator=(WatchScope const&) = delete;
};

void registerWatcher() {
    if (sRegistered) return;
    sRegistered = true;
    // above every unit's handler (Highest + 1000): it sees the damage first and cancels it
    c::onIncoming(c::priority::Highest + 1000, [](c::DamageInfo& d) {
        if (sWatched == 0 || d.victim.getOrCreateUniqueID().rawID != sWatched) return c::Verdict::Pass;
        sSeen = d.type;
        return sLetThrough ? c::Verdict::Pass : c::Verdict::Cancel;
    });
}

/// The Java type the hurt pipeline names a Bedrock source of `cause` (no entity) on `victim`.
std::string typeSeen(Actor& victim, Cause cause) {
    sSeen.clear();
    ActorDamageSource source(cause, std::string{});
    HurtParameters    params{};
    (void)victim.hurt(source, 1.f, params);
    return sSeen;
}

/// The Java type of a Bedrock source of `cause` caused by `attacker`.
std::string typeSeenFrom(Actor& victim, Actor& attacker, Cause cause) {
    sSeen.clear();
    ActorDamageByActorSource source(attacker, cause);
    HurtParameters           params{};
    (void)victim.hurt(source, 1.f, params);
    return sSeen;
}

} // namespace

GF_REGISTER_SELFTEST("combat.vanilla_damage_types", [](Context& ctx) {
    // the types (data/minecraft/damage_type of 1.21.1)
    int minecraft = 0;
    for (auto const& id : c::damageTypeIds())
        if (id.rfind("minecraft:", 0) == 0) ++minecraft;
    ctx.check(minecraft == 47, "Minecraft 1.21.1's 47 damage types are known (" + std::to_string(minecraft) + ")");
    auto const* arrow  = c::damageType("minecraft:arrow");
    auto const* fall   = c::damageType("minecraft:fall");
    auto const* boom   = c::damageType("minecraft:player_explosion");
    auto const* berry  = c::damageType("minecraft:sweet_berry_bush");
    auto const* poison = c::damageType("neoforge:poison");
    ctx.require(arrow && fall && boom && berry && poison, "arrow, fall, player_explosion, sweet_berry_bush and neoforge:poison are known");
    ctx.check(arrow->messageId == "arrow" && arrow->exhaustion > 0.09f && arrow->vanilla, "arrow: message id arrow, exhaustion 0.1, a vanilla type");
    ctx.check(fall->deathMessageType == "fall_variants" && fall->exhaustion == 0.f, "fall: fall_variants, no exhaustion");
    ctx.check(boom->messageId == "explosion.player" && boom->scaling == "always", "player_explosion: explosion.player, scaling always");
    ctx.check(berry->effects == "poking" && c::damageType("minecraft:thorns")->effects == "thorns" && c::damageType("minecraft:lava")->effects == "burning",
              "the DamageEffects: poking, thorns, burning");
    ctx.check(poison->messageId == "magic", "neoforge:poison: NeoForge's poison effect damage, message id magic");
    ctx.check(!c::damageType("twilightforest:hydra_bite")->vanilla, "TF's types are not vanilla");
    // the tags: vanilla's own members, NeoForge's additions, nested tags (with TF's data members)
    struct Row {
        char const* tag;
        char const* member;
        bool        in;
    };
    static constexpr Row rows[] = {
        {"minecraft:is_projectile", "minecraft:arrow", true},
        {"minecraft:is_projectile", "minecraft:spit", false},          // llama spit is no projectile type in 1.21.1
        {"minecraft:bypasses_armor", "minecraft:generic", true},
        {"minecraft:bypasses_armor", "minecraft:arrow", false},
        {"minecraft:bypasses_armor", "neoforge:poison", true},          // NeoForge: #neoforge:is_poison
        {"minecraft:bypasses_shield", "minecraft:fall", true},          // #minecraft:bypasses_armor
        {"minecraft:bypasses_shield", "minecraft:falling_anvil", true},
        {"minecraft:bypasses_shield", "twilightforest:lich_bolt", true}, // nested, TF's member of bypasses_armor (data)
        {"minecraft:always_hurts_ender_dragons", "minecraft:fireworks", true},
        {"minecraft:always_hurts_ender_dragons", "twilightforest:lich_bomb", true}, // #is_explosion, TF's member (data)
        {"minecraft:bypasses_wolf_armor", "minecraft:generic_kill", true}, // #bypasses_invulnerability
        {"minecraft:no_knockback", "minecraft:cactus", true},
        {"minecraft:no_knockback", "minecraft:player_attack", false},
        {"minecraft:panic_causes", "minecraft:lava", true},             // #panic_environmental_causes
        {"minecraft:is_fall", "minecraft:stalagmite", true},
        {"minecraft:is_fall", "minecraft:ender_pearl", false},          // a 1.21.2 type
        {"minecraft:witch_resistant_to", "minecraft:sonic_boom", true},
        {"neoforge:is_physical", "minecraft:arrow", true},
        {"neoforge:is_physical", "minecraft:magic", false},
        {"neoforge:is_magic", "minecraft:wither_skull", true},          // #neoforge:is_wither
        {"neoforge:is_magic", "neoforge:poison", true},                 // #neoforge:is_poison
        {"neoforge:is_magic", "twilightforest:chilling_breath", true},  // TF's data
        {"neoforge:is_environment", "minecraft:sweet_berry_bush", true},
        {"neoforge:is_technical", "minecraft:out_of_world", true},
    };
    for (auto const& r : rows)
        ctx.check(c::damageTypeHasTag(r.member, r.tag) == r.in, std::string(r.member) + (r.in ? " is in " : " is not in ") + r.tag);
    // the Bedrock causes of the vanilla types
    struct CauseRow {
        char const* id;
        Cause       cause;
    };
    static constexpr CauseRow causes[] = {
        {"minecraft:arrow", Cause::Projectile},        {"minecraft:trident", Cause::Projectile},     {"minecraft:thrown", Cause::Projectile},
        {"minecraft:player_attack", Cause::EntityAttack}, {"minecraft:mob_attack", Cause::EntityAttack}, {"minecraft:sting", Cause::EntityAttack},
        {"minecraft:in_fire", Cause::Fire},            {"minecraft:on_fire", Cause::FireTick},       {"minecraft:lava", Cause::Lava},
        {"minecraft:hot_floor", Cause::Magma},         {"minecraft:campfire", Cause::Campfire},      {"minecraft:fall", Cause::Fall},
        {"minecraft:drown", Cause::Drowning},          {"minecraft:in_wall", Cause::Suffocation},    {"minecraft:cactus", Cause::Contact},
        {"minecraft:sweet_berry_bush", Cause::Contact}, {"minecraft:magic", Cause::Magic},           {"minecraft:indirect_magic", Cause::Magic},
        {"minecraft:wither", Cause::Wither},           {"minecraft:starve", Cause::Starve},          {"minecraft:freeze", Cause::Freezing},
        {"minecraft:out_of_world", Cause::Void},       {"minecraft:generic_kill", Cause::Override},  {"minecraft:lightning_bolt", Cause::Lightning},
        {"minecraft:falling_anvil", Cause::Anvil},     {"minecraft:falling_block", Cause::FallingBlock}, {"minecraft:falling_stalactite", Cause::Stalactite},
        {"minecraft:stalagmite", Cause::Stalagmite},   {"minecraft:fly_into_wall", Cause::FlyIntoWall}, {"minecraft:sonic_boom", Cause::SonicBoom},
        {"minecraft:thorns", Cause::Thorns},           {"minecraft:fireworks", Cause::Fireworks},    {"minecraft:player_explosion", Cause::EntityExplosion},
        {"minecraft:fireball", Cause::Fire},           {"neoforge:poison", Cause::Magic},
    };
    for (auto const& r : causes)
        ctx.check(c::causeFor(r.id, true) == r.cause, std::string(r.id) + " -> cause " + std::to_string(static_cast<int>(r.cause)) + " (got "
                                                          + std::to_string(static_cast<int>(c::causeFor(r.id, true))) + ")");
    ctx.check(c::causeFor("minecraft:explosion", false) == Cause::BlockExplosion && c::causeFor("minecraft:explosion", true) == Cause::EntityExplosion,
              "an explosion is a block explosion, or the causing entity's");
    // TF's types still follow their tags
    ctx.check(c::causeFor("twilightforest:lich_bolt", true) == Cause::Magic && c::causeFor("twilightforest:hydra_fire", true) == Cause::Fire
                  && c::causeFor("twilightforest:expired", false) == Cause::Override && c::causeFor("twilightforest:hydra_bite", true) == Cause::EntityAttack,
              "TF's types keep the causes their tags call for");
});

GF_REGISTER_SELFTEST("combat.vanilla_damage_sources", [](Context& ctx) -> ll::coro::CoroTask<> {
    registerWatcher();
    auto pos = ctx.testPos(6078);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    {
        WatchScope watch(p);
        // Bedrock's own damage, named by the Java type of the same event (the handler cancels it)
        struct Row {
            Cause       cause;
            char const* type;
        };
        static constexpr Row rows[] = {
            {Cause::Fall, "minecraft:fall"},           {Cause::Lava, "minecraft:lava"},           {Cause::Magma, "minecraft:hot_floor"},
            {Cause::Fire, "minecraft:in_fire"},        {Cause::FireTick, "minecraft:on_fire"},    {Cause::Campfire, "minecraft:campfire"},
            {Cause::SoulCampfire, "minecraft:campfire"}, {Cause::Drowning, "minecraft:drown"},    {Cause::Suffocation, "minecraft:in_wall"},
            {Cause::Contact, "minecraft:cactus"},      {Cause::Magic, "minecraft:magic"},         {Cause::Wither, "minecraft:wither"},
            {Cause::Starve, "minecraft:starve"},       {Cause::Freezing, "minecraft:freeze"},     {Cause::Void, "minecraft:out_of_world"},
            {Cause::Lightning, "minecraft:lightning_bolt"}, {Cause::Anvil, "minecraft:falling_anvil"}, {Cause::FallingBlock, "minecraft:falling_block"},
            {Cause::Stalactite, "minecraft:falling_stalactite"}, {Cause::Stalagmite, "minecraft:stalagmite"}, {Cause::FlyIntoWall, "minecraft:fly_into_wall"},
            {Cause::SonicBoom, "minecraft:sonic_boom"}, {Cause::Thorns, "minecraft:thorns"},      {Cause::Fireworks, "minecraft:fireworks"},
            {Cause::BlockExplosion, "minecraft:explosion"}, {Cause::Temperature, "minecraft:on_fire"}, {Cause::Override, "minecraft:generic_kill"},
            {Cause::SelfDestruct, "minecraft:generic_kill"},
        };
        for (auto const& r : rows) {
            auto seen = typeSeen(ctx.player(), r.cause);
            ctx.check(seen == r.type, "cause " + std::to_string(static_cast<int>(r.cause)) + " is " + r.type + " (got " + seen + ")");
            ctx.check(c::damageType(seen) != nullptr, seen + " is a known damage type");
        }
        // the round trip: the type's Bedrock cause is the cause it came from
        static constexpr Cause roundTrip[] = {Cause::Fall, Cause::Lava, Cause::Magma, Cause::Fire, Cause::FireTick, Cause::Campfire, Cause::Drowning,
                                              Cause::Suffocation, Cause::Contact, Cause::Magic, Cause::Wither, Cause::Starve, Cause::Freezing, Cause::Void,
                                              Cause::Lightning, Cause::Anvil, Cause::FallingBlock, Cause::Stalactite, Cause::Stalagmite,
                                              Cause::FlyIntoWall, Cause::SonicBoom, Cause::Thorns, Cause::Fireworks, Cause::BlockExplosion, Cause::Override};
        for (auto cause : roundTrip) {
            auto seen = typeSeen(ctx.player(), cause);
            ctx.check(c::causeFor(seen, false) == cause, seen + " goes back to cause " + std::to_string(static_cast<int>(cause)));
        }
        // with an attacker: a player's hit, a bee's sting, a creeper's explosion
        auto& other = ctx.spawnPlayer("gf_combat_attacker");
        ctx.check(typeSeenFrom(ctx.player(), other, Cause::EntityAttack) == "minecraft:player_attack", "a player's hit is player_attack (got " + sSeen + ")");
        ctx.check(typeSeenFrom(ctx.player(), other, Cause::EntityExplosion) == "minecraft:player_explosion", "a player's explosion is player_explosion (got " + sSeen + ")");
        auto* region = ctx.region();
        Vec3  at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y) + 2.f, static_cast<float>(pos.z) + .5f};
        auto* bee    = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:bee"), nullptr, at, false, false, false);
        if (bee) {
            ctx.check(typeSeenFrom(ctx.player(), *bee, Cause::EntityAttack) == "minecraft:sting", "a bee's attack is sting (got " + sSeen + ")");
            bee->remove();
        } else {
            ctx.info("no bee could be spawned: sting is not checked");
        }
        // gf::combat::hurt with a vanilla type: known (no "unknown damage type"), its own cause
        sSeen.clear();
        (void)c::hurt(ctx.player(), 1.f, "minecraft:arrow");
        ctx.check(sSeen == "minecraft:arrow", "gf::combat::hurt passes the vanilla type to the handlers (got " + sSeen + ")");
    }
    // the death message of a vanilla type: Bedrock's own for its cause, or Bedrock's key where the
    // cause would name it otherwise (generic: "%1$s died", Magic would say "killed by magic")
    co_await ctx.ticks(25); // (the hurt cooldown)
    {
        auto& v = ctx.player();
        WatchScope watch(v);
        sLetThrough = true;
        bool hit    = c::hurt(v, 1.f, "minecraft:cactus");
        auto msg    = c::deathMessageFor(v, "Victim");
        ctx.check(!hit || msg.first.empty(), "minecraft:cactus keeps Bedrock's death message (got " + msg.first + ")");
    }
    co_await ctx.ticks(25);
    {
        auto& v = ctx.player();
        WatchScope watch(v);
        sLetThrough = true;
        bool hit    = c::hurt(v, 1.f, "minecraft:generic");
        auto msg    = c::deathMessageFor(v, "Victim");
        ctx.check(hit, "the generic damage applied");
        ctx.check(!hit || (msg.first == "death.attack.generic" && msg.second.size() == 1 && msg.second[0] == "Victim"),
                  "minecraft:generic dies with Bedrock's death.attack.generic (got " + msg.first + ")");
    }
});

GF_REGISTER_SELFTEST("combat.fx_vanilla_sounds", [](Context& ctx) {
    namespace fx = gf::fx;
    struct Row {
        char const* event;
        char const* bedrock;
    };
    static constexpr Row rows[] = {
        // AbstractArrow for TF's arrows (the projectile kit)
        {"minecraft:entity.arrow.hit", "random.bowhit"},
        {"minecraft:entity.arrow.hit_player", "random.orb"},
        {"minecraft:entity.arrow.shoot", "random.bow"},
        {"minecraft:entity.item.pickup", "random.pop"},
        // DamageEffects of a hurt player (the combat pipeline)
        {"minecraft:enchant.thorns.hit", "damage.thorns"},
        {"minecraft:entity.player.hurt_sweet_berry_bush", "block.sweet_berry_bush.hurt"},
        {"minecraft:entity.player.hurt_on_fire", "mob.player.hurt_on_fire"},
        {"minecraft:entity.player.hurt_freeze", "mob.player.hurt_freeze"},
        {"minecraft:entity.player.hurt_drown", "mob.player.hurt_drown"},
        // events TF's code names whose earlier names were no Bedrock sound
        {"minecraft:block.candle.break", "dig.candle"},
        {"minecraft:block.lily_pad.place", "dig.grass"},
        {"minecraft:entity.zombie.attack_iron_door", "random.anvil_land"},
    };
    for (auto const& r : rows) {
        auto name = fx::resolveSound(r.event);
        ctx.check(name == r.bedrock, std::string(r.event) + " plays " + r.bedrock + " (got '" + name + "')");
    }
});

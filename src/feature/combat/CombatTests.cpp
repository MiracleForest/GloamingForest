// Selftests of F10 (prefix "combat.", testPos dx 6000..6099): the hurt hook and damage types, death
// messages, the death pipeline (lethal, keepers, drops, respawn), the loot interpreter with TF's
// functions, containers (loot placement, the BlockData backend and its persistence over a world
// reload), the progression store / trigger bus / events, gamerules and fx.
//
// Handlers registered by a test cannot be removed (the contracts have no removal), so every test
// handler only acts while its test runs (a static "active" id) and is registered once.

#include <algorithm>
#include <map>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#ifndef GF_CLIENT
#include "mc/world/item/components/StorageItemComponentTags.h"
#endif
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"

#include "feature/combat/Combat.h"
#include "feature/combat/CombatInternal.h"
#include "feature/death/Death.h"
#include "feature/death/DeathInternal.h"
#include "feature/fx/Fx.h"
#include "feature/fx/FxInternal.h"
#include "feature/gamerules/GameRules.h"
#include "feature/gamerules/GameRulesInternal.h"
#include "feature/inventory/Inventory.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/JavaRandom.h"
#include "feature/loot/Loot.h"
#include "feature/loot/LootInternal.h"
#include "feature/loot/LootRegistry.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using JsonT = nlohmann::json;
using gf::core::GlobalPos;
namespace db    = gf::game::db;
namespace items = gf::game::items;

/// The actor a test's handlers act on (0: none, the handlers pass).
std::int64_t gActive = 0;

struct ActiveScope {
    explicit ActiveScope(Actor& a) { gActive = a.getOrCreateUniqueID().rawID; }
    ~ActiveScope() { gActive = 0; }
};

bool isActive(Actor const& a) { return gActive != 0 && const_cast<Actor&>(a).getOrCreateUniqueID().rawID == gActive; }

float healthOf(Player& p) { return static_cast<float>(p.getHealth()); }

int countIn(std::vector<ItemStack> const& list, std::string const& id) {
    int n = 0;
    for (auto const& s : list)
        if (items::idOf(s) == id) n += items::countOf(s);
    return n;
}

int countInInventory(Player& p, std::string const& id) {
    int   n   = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (items::idOf(inv.getItem(i)) == id) n += items::countOf(inv.getItem(i));
    return n;
}

/// Item entities within `radius` blocks of `pos` (same dimension).
int itemEntitiesNear(GlobalPos const& pos, float radius) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    int n = 0;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || !a->isType(ActorType::ItemEntity) || static_cast<int>(a->getDimensionId()) != pos.dim) continue;
        auto const& at = a->getPosition();
        float       dx = at.x - (pos.x + .5f), dy = at.y - (pos.y + .5f), dz = at.z - (pos.z + .5f);
        if (dx * dx + dy * dy + dz * dz <= radius * radius) ++n;
    }
    return n;
}

} // namespace

// ---- gamerules -----------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.gamerules", [](Context& ctx) {
    namespace gr = gf::gamerules;
    ctx.check(gr::getBool("tfEnforcedProgression") == gr::getBool("tfEnforcedProgression"), "tfEnforcedProgression is known");
    ctx.check(std::holds_alternative<int>(gr::get("playersTfPortalDefaultDelay")), "playersTfPortalDefaultDelay is an int rule");
    gr::registerBool("gfTestBoolRule", true);
    gr::registerInt("gfTestIntRule", 7);
    ctx.check(gr::getBool("gfTestBoolRule") && gr::getInt("gfTestIntRule") == 7, "defaults apply until set");
    ctx.check(gr::set("gfTestIntRule", 12) && gr::getInt("gfTestIntRule") == 12, "an int rule is set");
    ctx.check(!gr::set("gfTestIntRule", true), "a value of the other type is refused");
    ctx.check(!gr::set("noSuchRule", true) && !gr::getBool("noSuchRule"), "unknown rules are refused / false");
    ctx.check(gr::set("gfTestBoolRule", false) && !gr::getBool("gfTestBoolRule"), "a bool rule is set");
    auto stored = db::get("gf:gamerules:values");
    ctx.check(stored && stored->value("gfTestIntRule", 0) == 12, "values are stored in the world DB");
    ctx.check(gr::parse("gfTestIntRule", "34").has_value() && !gr::parse("gfTestIntRule", "3x").has_value(), "command values parse");
    ctx.check(gr::snapshot().contains("tfEnforcedProgression"), "the snapshot lists TF's rules");
    // the test rules leave no trace (registry and stored values)
    gr::unregisterRule("gfTestIntRule");
    gr::unregisterRule("gfTestBoolRule");
    ctx.check(!gr::snapshot().contains("gfTestIntRule") && !db::get("gf:gamerules:values")->contains("gfTestIntRule"), "the test rules are gone again");
});

// ---- fx ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.fx", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace fx = gf::fx;
    ctx.check(fx::resolveParticle("minecraft:flame").effect == "minecraft:basic_flame_particle", "vanilla particle types map to Bedrock effects");
    ctx.check(fx::resolveParticle("minecraft:no_such_particle").effect.empty(), "unknown particles are silent");
    ctx.check(fx::resolveParticle("twilightforest:firefly").native, "TF particles go to companions natively");
    ctx.check(fx::resolveSound("minecraft:entity.ghast.shoot") == "mob.ghast.fireball", "vanilla sound events map to Bedrock names");
    ctx.check(fx::resolveSound("entity.twilightforest.no_such_sound").empty(), "unknown TF sound events are silent");
    auto pos = ctx.testPos(6000);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto  before = fx::counters();
    auto& dim    = p.getDimension();
    fx::particle(dim, "minecraft:flame", p.getPosition(), {}, 2);
    fx::sound(dim, "minecraft:entity.ghast.shoot", p.getPosition(), 1.f, 1.f);
    fx::soundTo(p, "minecraft:entity.generic.explode", 1.f, 1.f);
    auto after = fx::counters();
    ctx.check(after.particlePackets >= before.particlePackets + 2, "particles were sent to the player nearby");
    ctx.check(after.soundPackets >= before.soundPackets + 2, "sounds were sent (in range and to one player)");
    // companions get TF particles over fx.particle
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    fx::particle(dim, "twilightforest:firefly", p.getPosition(), Vec3{0, 0.1f, 0}, 1, JsonT{{"colour", 1}});
    auto msgs = gf::net::takeTestMessages(p);
    gf::net::setTestCompanion(p, false);
    bool got = std::any_of(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "fx.particle" && m.payload.value("id", std::string{}) == "twilightforest:firefly"; });
    ctx.check(got, "a companion got fx.particle for a TF particle");
});

// ---- combat --------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.damage_types", [](Context& ctx) {
    namespace c = gf::combat;
    using Cause = SharedTypes::Legacy::ActorDamageCause;
    int tf      = 0;
    for (auto const& id : c::damageTypeIds())
        if (id.rfind("twilightforest:", 0) == 0) ++tf;
    ctx.check(tf >= 40, "TF's 40 damage types are known (" + std::to_string(tf) + ")");
    auto const* bite = c::damageType("twilightforest:hydra_bite");
    ctx.require(bite != nullptr, "hydra_bite is known");
    ctx.check(bite->messageId == "twilightforest.hydraBite", "message ids follow TFDamageTypes");
    ctx.check(c::damageType("twilightforest:slam")->messageId == "twilightforest.axing", "slam reuses the axing message");
    ctx.check(c::damageType("twilightforest:thorns")->exhaustion > 0.09f, "exhaustion follows TFDamageTypes");
    ctx.check(c::causeFor("twilightforest:lich_bolt", true) == Cause::Magic, "bypasses_armor -> Magic");
    ctx.check(c::causeFor("twilightforest:hydra_fire", true) == Cause::Fire, "is_fire -> Fire");
    ctx.check(c::causeFor("twilightforest:yeeted", true) == Cause::Fall, "is_fall -> Fall");
    ctx.check(c::causeFor("twilightforest:expired", false) == Cause::Override, "bypasses_invulnerability -> Override");
    ctx.check(c::causeFor("twilightforest:thrown_axe", true) == Cause::Projectile, "is_projectile -> Projectile");
    ctx.check(c::causeFor("twilightforest:hydra_bite", true) == Cause::EntityAttack, "plain damage with an attacker -> EntityAttack");
    ctx.check(c::damageTypeHasTag("twilightforest:clamped", "minecraft:no_knockback"), "TF's tag memberships are built in");
});

GF_REGISTER_SELFTEST("combat.hurt_hook", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace c = gf::combat;
    static std::string           sSeenType;
    static float                 sDealt   = -1;
    static bool                  sCancel  = false;
    static bool                  sDoubled = false;
    static bool                  registered = false;
    if (!registered) {
        registered = true;
        c::onIncoming(c::priority::High, [](c::DamageInfo& d) {
            if (!isActive(d.victim)) return c::Verdict::Pass;
            sSeenType = d.type;
            if (sCancel) return c::Verdict::Cancel;
            if (sDoubled) d.amount *= 2;
            return c::Verdict::Pass;
        });
        c::onDealt(c::priority::Normal, [](c::DamageInfo const& d) {
            if (isActive(d.victim)) sDealt = d.amount;
        });
    }
    auto pos = ctx.testPos(6001);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    ActiveScope active(p);
    float before = healthOf(p);
    sDoubled = true;
    sDealt   = -1;
    bool hit = c::hurt(p, 2.f, "twilightforest:hydra_bite");
    sDoubled = false;
    ctx.check(hit, "gf::combat::hurt applied the damage");
    ctx.check(sSeenType == "twilightforest:hydra_bite", "onIncoming saw the TF damage type (got " + sSeenType + ")");
    ctx.check(sDealt >= 3.9f, "onDealt heard the doubled amount (got " + std::to_string(sDealt) + ")");
    ctx.check(healthOf(p) <= before - 3.9f, "the doubled damage was applied");
    sCancel     = true;
    float mid   = healthOf(p);
    bool  again = c::hurt(p, 3.f, "twilightforest:squish");
    sCancel     = false;
    ctx.check(!again && healthOf(p) == mid, "a cancelled hurt does nothing");
    // death message of the last TF damage: TF's key with the victim's name
    auto msg = c::deathMessageFor(p, "Victim");
    ctx.check(msg.first == "death.attack.twilightforest.hydraBite" || msg.first == "death.attack.twilightforest.hydraBite.player", "TF death message key (got " + msg.first + ")");
    ctx.check(!msg.second.empty() && msg.second[0] == "Victim", "the victim's name is the first parameter");
    // vanilla damage gets Java's type id
    sSeenType.clear();
    {
        ActorDamageSource fall(SharedTypes::Legacy::ActorDamageCause::Fall, std::string{});
        HurtParameters    hp{};
        (void)p.hurt(fall, 1.f, hp);
    }
    ctx.check(sSeenType == "minecraft:fall", "vanilla fall damage is minecraft:fall (got " + sSeenType + ")");
});

// ---- death pipeline --------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.lethal", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool registered = false;
    static int  saves      = 0;
    if (!registered) {
        registered = true;
        // a charm-of-life like handler: survive with 8 health
        gf::death::onLethal(gf::combat::priority::High, [](Actor& victim, gf::combat::DamageInfo const&) {
            if (!isActive(victim)) return false;
            ++saves;
            gf::death::setHealth(victim, 8.f);
            return true;
        });
    }
    auto pos = ctx.testPos(6002);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID();
    {
        ActiveScope active(p);
        saves = 0;
        gf::combat::hurt(p, 1000.f, "twilightforest:expired");
    }
    co_await ctx.ticks(2);
    auto level = ll::service::getLevel();
    auto* again = level ? level->getPlayer(id) : nullptr;
    ctx.require(again != nullptr, "the player is still in the level");
    ctx.check(saves == 1, "the lethal handler ran once (" + std::to_string(saves) + ")");
    ctx.check(again->isAlive() && again->getHealth() == 8, "the fixture player survived lethal damage with the handler's health (" + std::to_string(again->getHealth()) + ")");
});

GF_REGISTER_SELFTEST("combat.keepers", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool registered = false;
    static int  dropsSeen  = 0;
    static int  respawns   = 0;
    if (!registered) {
        registered = true;
        // keeps the fixture item (as charms / the kept_on_death tag keep their stacks)
        gf::death::registerKeeper(gf::combat::priority::Normal, [](Player& p, ItemStack& s) {
            return isActive(p) && items::idOf(s) == "gf_test:fixture_item";
        });
        // takes the cobblestone out of the drops (as the keepsake casket does)
        gf::death::onDropsRemaining([](Player& p, std::vector<ItemStack>& drops) {
            if (!isActive(p)) return;
            for (auto& s : drops)
                if (items::idOf(s) == "minecraft:cobblestone") {
                    dropsSeen += items::countOf(s);
                    s = ItemStack{};
                }
        });
        gf::death::onRespawn([](Player& p) {
            if (isActive(p)) ++respawns;
        });
    }
    auto pos = ctx.testPos(6003);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = ctx.spawnPlayer("gf_combat_keeper");
    auto  id = sp.getOrCreateUniqueID();
    auto  fixture = items::make("gf_test:fixture_item", 5);
    auto  cobble  = items::make("minecraft:cobblestone", 9);
    ctx.require(fixture && cobble, "fixture item and cobblestone exist");
    ctx.give(sp, *fixture);
    ctx.give(sp, *cobble);
    dropsSeen = 0;
    respawns  = 0;
    {
        ActiveScope active(sp);
        gf::combat::hurt(sp, 1000.f, "twilightforest:expired");
    }
    co_await ctx.ticks(2);
    auto level = ll::service::getLevel();
    auto* p    = level ? level->getPlayer(id) : nullptr;
    ctx.require(p != nullptr, "the player is still in the level");
    auto kept = db::get("gf:combat:kept:" + p->getUuid().asString());
    bool keptFixture = kept && kept->is_array() && std::any_of(kept->begin(), kept->end(), [](JsonT const& e) {
        return e.value("item", JsonT::object()).value("id", std::string{}) == "gf_test:fixture_item";
    });
    ctx.check(keptFixture, "the keeper's stack is stored through death");
    ctx.check(countInInventory(*p, "gf_test:fixture_item") == 0, "the kept stack left the inventory");
    ctx.check(dropsSeen == 9, "the drops handler took the cobblestone (" + std::to_string(dropsSeen) + ")");
    // respawn: the kept items come back
    {
        ActiveScope active(*p);
        static_cast<SimulatedPlayer&>(*p).simulateRespawn();
    }
    co_await ctx.ticks(10);
    p = level->getPlayer(id);
    ctx.require(p != nullptr, "the player respawned");
    ctx.check(countInInventory(*p, "gf_test:fixture_item") == 5, "the respawn returned the kept stack (" + std::to_string(countInInventory(*p, "gf_test:fixture_item")) + ")");
    ctx.check(!db::get("gf:combat:kept:" + p->getUuid().asString()), "the kept record is gone");
    ctx.check(respawns >= 1, "onRespawn ran");
});

// ---- loot ----------------------------------------------------------------------------------------

namespace {

/// A table using every TF loot extension with constant numbers (exact expected counts).
JsonT tfFunctionsTable() {
    return JsonT::parse(R"({
      "type": "minecraft:entity",
      "pools": [
        { "rolls": { "type": "twilightforest:multiplayer_rolls", "default_rolls": 1, "per_player_rolls": 1 },
          "entries": [ { "type": "minecraft:item", "name": "minecraft:stick" } ] },
        { "rolls": 1,
          "entries": [ { "type": "minecraft:item", "name": "minecraft:diamond",
            "functions": [ { "function": "minecraft:set_count", "count": 2 },
                           { "function": "twilightforest:multiplayer_addition", "extra_count_per_player": 2 } ] } ] },
        { "rolls": { "type": "twilightforest:looting_rolls", "enchantment": "minecraft:looting", "base_value": 1 },
          "entries": [ { "type": "minecraft:item", "name": "minecraft:emerald" } ] },
        { "rolls": 1, "conditions": [ { "condition": "twilightforest:mod_exists", "mod_id": "twilightforest" } ],
          "entries": [ { "type": "minecraft:item", "name": "minecraft:gold_ingot" } ] },
        { "rolls": 1, "conditions": [ { "condition": "twilightforest:mod_exists", "mod_id": "examplemod" } ],
          "entries": [ { "type": "minecraft:item", "name": "minecraft:iron_ingot" } ] }
      ]
    })");
}

/// Vanilla structure of the interpreter: alternatives, nested tables, weights, conditions.
JsonT structureTable() {
    return JsonT::parse(R"({
      "type": "minecraft:chest",
      "pools": [
        { "rolls": 1, "entries": [ { "type": "minecraft:alternatives", "children": [
            { "type": "minecraft:item", "name": "minecraft:apple", "conditions": [ { "condition": "minecraft:killed_by_player" } ] },
            { "type": "minecraft:item", "name": "minecraft:bread" } ] } ] },
        { "rolls": 2, "entries": [ { "type": "minecraft:loot_table", "value": "gf_test:combat/nested" } ] },
        { "rolls": 1, "entries": [ { "type": "minecraft:item", "name": "minecraft:coal", "weight": 0 },
                                   { "type": "minecraft:item", "name": "minecraft:flint", "weight": 5,
                                     "functions": [ { "function": "minecraft:set_count", "count": { "type": "minecraft:uniform", "min": 3, "max": 5 } },
                                                    { "function": "minecraft:limit_count", "limit": { "max": 4 } } ] } ] },
        { "rolls": 1, "entries": [ { "type": "minecraft:item", "name": "minecraft:snowball",
            "functions": [ { "function": "minecraft:set_count", "count": 70 } ] } ] },
        { "rolls": 1, "conditions": [ { "condition": "minecraft:inverted", "term": { "condition": "minecraft:random_chance", "chance": 1.0 } } ],
          "entries": [ { "type": "minecraft:item", "name": "minecraft:iron_ingot" } ] }
      ]
    })");
}

} // namespace

GF_REGISTER_SELFTEST("combat.loot_tf_functions", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    loot::registerTable("gf_test:combat/tf_functions", tfFunctionsTable());
    auto pos = ctx.testPos(6004);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    // the attacker holds a looting III sword
    auto sword = items::make("minecraft:iron_sword", 1);
    ctx.require(sword.has_value(), "iron sword");
    EnchantmentInstance looting;
    looting.mEnchantType = Enchant::Type::Looting;
    looting.mLevel       = 3;
    EnchantUtils::applyEnchant(*sword, looting, true);
    ctx.check(loot::enchantLevel(*sword, "minecraft:looting") == 3, "the sword has looting III");
    p.setCarriedItem(*sword);
    loot::LootContext lc;
    lc.region     = ctx.region();
    lc.origin     = p.getPosition();
    lc.thisEntity = &p;
    lc.attacker   = &p;
    lc.player     = &p;
    lc.seed       = 1234;
    // three qualified players with an adjuster that adds loot
    loot::setTestParticipants(3);
    loot::setTestAdjustsLoot(true);
    auto many = loot::roll("gf_test:combat/tf_functions", lc);
    loot::setTestAdjustsLoot(false);
    auto plain = loot::roll("gf_test:combat/tf_functions", lc);
    loot::setTestParticipants(-1);
    loot::setTestAdjustsLoot(std::nullopt);
    p.setCarriedItem(ItemStack{});
    ctx.check(countIn(many, "minecraft:stick") == 3, "multiplayer_rolls: 1 + (3 - 1) x 1 rolls (got " + std::to_string(countIn(many, "minecraft:stick")) + ")");
    ctx.check(countIn(many, "minecraft:diamond") == 6, "multiplayer_addition: 2 + (3 - 1) x 2 (got " + std::to_string(countIn(many, "minecraft:diamond")) + ")");
    ctx.check(countIn(many, "minecraft:emerald") == 4, "looting_rolls: looting 3 + base 1 (got " + std::to_string(countIn(many, "minecraft:emerald")) + ")");
    ctx.check(countIn(many, "minecraft:gold_ingot") == 1 && countIn(many, "minecraft:iron_ingot") == 0, "mod_exists: twilightforest yes, examplemod no");
    ctx.check(countIn(plain, "minecraft:stick") == 1 && countIn(plain, "minecraft:diamond") == 2, "without the loot adjuster the default rolls and counts apply");
    loot::registerTable("gf_test:combat/tf_functions", nullptr);
});

GF_REGISTER_SELFTEST("combat.loot_interpreter", [](Context& ctx) {
    namespace loot = gf::loot;
    loot::registerTable("gf_test:combat/nested", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:string"}]}]})"));
    loot::registerTable("gf_test:combat/structure", structureTable());
    loot::LootContext lc;
    lc.seed  = 42;
    auto a   = loot::roll("gf_test:combat/structure", lc);
    auto b   = loot::roll("gf_test:combat/structure", lc);
    ctx.check(countIn(a, "minecraft:bread") == 1 && countIn(a, "minecraft:apple") == 0, "alternatives: the first child whose conditions pass");
    ctx.check(countIn(a, "minecraft:string") == 2, "nested loot_table entries roll the other table");
    int flint = countIn(a, "minecraft:flint");
    ctx.check(flint >= 3 && flint <= 4 && countIn(a, "minecraft:coal") == 0, "weights (0 never) and set_count / limit_count (got " + std::to_string(flint) + ")");
    int snow = 0, stacks = 0;
    for (auto const& s : a)
        if (items::idOf(s) == "minecraft:snowball") {
            snow += items::countOf(s);
            ++stacks;
        }
    ctx.check(snow == 70 && stacks == 5, "stacks over the max stack size are split (70 snowballs in 16-stacks: " + std::to_string(stacks) + " stacks)");
    ctx.check(countIn(a, "minecraft:iron_ingot") == 0, "inverted random_chance 1.0 never passes");
    bool same = a.size() == b.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = items::idOf(a[i]) == items::idOf(b[i]) && items::countOf(a[i]) == items::countOf(b[i]);
    ctx.check(same, "a seeded roll is reproducible");
    // Java's LegacyRandomSource sequence (java.util.Random(42).nextInt(10) == 0, then 3)
    gf::loot::JavaRandom jr(42);
    int first = jr.nextInt(10), second = jr.nextInt(10);
    ctx.check(first == 0 && second == 3, "JavaRandom matches java.util.Random (got " + std::to_string(first) + ", " + std::to_string(second) + ")");
    // the F0 fixture chest table (TF number provider and function shapes)
    loot::LootContext fixtureCtx;
    fixtureCtx.seed = 7;
    auto fx         = loot::roll("gf_test:chests/fixture_chest", fixtureCtx);
    int  n          = countIn(fx, "gf_test:fixture_item") + countIn(fx, "minecraft:stick");
    ctx.check(!fx.empty() && n >= 1 && n <= 12, "the fixture chest table rolls (" + std::to_string(fx.size()) + " stacks)");
    // survives_explosion inside an explosion of radius 1000 almost never passes
    loot::registerTable("gf_test:combat/explosion", JsonT::parse(R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:survives_explosion"}],"entries":[{"type":"minecraft:item","name":"minecraft:dirt"}]}]})"));
    int survived = 0;
    {
        loot::ExplosionScope scope(1000.f);
        for (int i = 0; i < 20; ++i) {
            loot::LootContext e;
            e.seed = 100 + i;
            survived += countIn(loot::roll("gf_test:combat/explosion", e), "minecraft:dirt");
        }
    }
    ctx.check(survived <= 1, "survives_explosion uses 1 / radius (" + std::to_string(survived) + " of 20)");
    loot::registerTable("gf_test:combat/explosion", nullptr);
    loot::registerTable("gf_test:combat/structure", nullptr);
    loot::registerTable("gf_test:combat/nested", nullptr);
});

GF_REGISTER_SELFTEST("combat.loot_global_modifier", [](Context& ctx) {
    namespace loot = gf::loot;
    static bool active     = false;
    static bool registered = false;
    if (!registered) {
        registered = true;
        loot::addGlobalModifier("gf_test:combat_double", [](std::vector<ItemStack>& l, loot::LootContext const&, std::string const& tableId) {
            if (!active || tableId != "gf_test:combat/modified") return;
            std::vector<ItemStack> more = l;
            l.insert(l.end(), more.begin(), more.end());
        });
    }
    loot::registerTable("gf_test:combat/modified", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:clay_ball"}]}]})"));
    active = true;
    auto l = loot::roll("gf_test:combat/modified", {});
    active = false;
    ctx.check(countIn(l, "minecraft:clay_ball") == 2, "a global modifier changed the roll");
    loot::registerTable("gf_test:combat/modified", nullptr);
});

GF_REGISTER_SELFTEST("combat.place_container", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    loot::registerTable("gf_test:combat/chest", JsonT::parse(R"({"type":"minecraft:chest","pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:set_count","count":12}]}]}]})"));
    auto pos = ctx.testPos(6005);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    ctx.cleanupBlock(pos);
    ctx.require(loot::placeContainer(*region, bp, "minecraft:chest", 2, "gf_test:combat/chest", 99), "a chest with a loot table was placed");
    ctx.check(ctx.blockAt(pos) == "minecraft:chest", "the chest is there");
    auto pending = loot::pendingVanilla(*region, bp);
    ctx.check(pending && pending->first == "gf_test:combat/chest" && pending->second == 99, "the table waits in the chest's loot field");
    // the game unpacks on first open: do what it does
    auto* actor = region->getBlockEntity(bp);
    ctx.require(actor && actor->getType() == BlockActorType::Chest, "a chest block actor");
    auto* chest = static_cast<ChestBlockActor*>(actor);
    auto* c     = chest->getContainer();
    ctx.require(c != nullptr, "the chest's container");
    auto& p = ctx.player();
    static_cast<RandomizableBlockActorContainerBase*>(actor)->unPackLootTable(p.getLevel(), *c, p.getDimensionId(), &p);
    int diamonds = 0, used = 0;
    for (int i = 0; i < c->getContainerSize(); ++i) {
        if (items::idOf(c->getItem(i)) == "minecraft:diamond") {
            diamonds += items::countOf(c->getItem(i));
            ++used;
        }
    }
    ctx.check(diamonds == 12, "the Java table filled the chest (" + std::to_string(diamonds) + " diamonds)");
    ctx.check(used >= 2, "LootTable#fill spread the stack over several slots (" + std::to_string(used) + ")");
    ctx.check(!loot::pendingVanilla(*region, bp), "the loot field was cleared");
    for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
    loot::registerTable("gf_test:combat/chest", nullptr);
});

// ---- inventory -----------------------------------------------------------------------------------

namespace {
constexpr char kContainerBlock[] = "gf_test:fixture_block";
constexpr char kPersistKey[]     = "gf:combat:container_probe";

void declareFixture() {
    gf::inventory::declare({.block = kContainerBlock, .rows = 3, .slots = 27, .title = "container.chest", .skin = ""});
}

GlobalPos persistPos(Context& ctx, BlockSource const& region) {
    auto pos = ctx.testPos(6010);
    pos.y    = std::max(pos.y - 3, region.getMinHeight() + 1); // under the ground layer, as core.persist_*
    return pos;
}
} // namespace

GF_REGISTER_SELFTEST("combat.inventory_handler", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    declareFixture();
    auto pos = ctx.testPos(6006);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "fixture block placed");
    auto* region = ctx.region();
    auto  h      = inv::find(*region, gf::game::blockPos(pos));
    ctx.require(h.has_value() && h->slots == 27, "the declared block is a 27-slot container");
    auto cobble = items::make("minecraft:cobblestone", 100);
    ctx.require(cobble.has_value(), "cobblestone");
    auto rest = h->insert(pos, *items::make("minecraft:cobblestone", 64), false);
    rest      = h->insert(pos, *items::make("minecraft:cobblestone", 40), false);
    ctx.check(items::isEmpty(rest), "insert takes everything that fits");
    ctx.check(items::countOf(h->get(pos, 0)) == 64 && items::countOf(h->get(pos, 1)) == 40, "insert merges, then fills empty slots");
    auto taken = h->extract(pos, 1, 15, false);
    ctx.check(items::countOf(taken) == 15 && items::countOf(h->get(pos, 1)) == 25, "extract takes from a slot");
    auto sim = h->extract(pos, 0, 64, true);
    ctx.check(items::countOf(sim) == 64 && items::countOf(h->get(pos, 0)) == 64, "a simulated extract changes nothing");
    // a pending loot table rolls on first access
    gf::loot::registerTable("gf_test:combat/box", JsonT::parse(R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:emerald","functions":[{"function":"minecraft:set_count","count":3}]}]}]})"));
    h->set(pos, 0, ItemStack{});
    h->set(pos, 1, ItemStack{});
    h->setLootTable(pos, "gf_test:combat/box", 5);
    ctx.check(inv::pendingLoot(pos).has_value(), "the loot table is pending");
    int emeralds = 0;
    for (int i = 0; i < h->slots; ++i)
        if (items::idOf(h->get(pos, i)) == "minecraft:emerald") emeralds += items::countOf(h->get(pos, i));
    ctx.check(emeralds == 3 && !inv::pendingLoot(pos), "the table rolled into the container on access");
    gf::loot::registerTable("gf_test:combat/box", nullptr);
    for (int i = 0; i < h->slots; ++i) h->set(pos, i, ItemStack{});
    inv::unregisterBlock(kContainerBlock);
});

GF_REGISTER_SELFTEST("combat.container_persist_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    declareFixture();
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the overworld is available");
    auto pos = persistPos(ctx, *region);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    region = ctx.region();
    auto bp = gf::game::blockPos(pos);
    auto was = ctx.blockAt(pos);
    if (auto prev = db::get(kPersistKey)) was = prev->value("was", was);
    auto const* block = gf::game::blockWith(kContainerBlock);
    ctx.require(block != nullptr, "the fixture block exists");
    ctx.require(&region->getBlock(bp) == block || gf::game::setBlock(*region, bp, *block), "place the container");
    auto h = gf::inventory::find(*region, bp);
    ctx.require(h.has_value(), "the container handler");
    std::random_device rd;
    int                n = 2 + static_cast<int>(rd() % 60);
    h->set(pos, 13, *items::make("minecraft:gold_ingot", n));
    db::put(kPersistKey, {{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"count", n}, {"was", was}});
    ctx.info("container marker written count=" + std::to_string(n));
    gf::inventory::unregisterBlock(kContainerBlock);
});

GF_REGISTER_SELFTEST("combat.container_persist_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto marker = db::get(kPersistKey);
    if (!marker) {
        ctx.info("no container marker (combat.container_persist_write did not run before): nothing to check");
        co_return;
    }
    db::erase(kPersistKey);
    declareFixture();
    auto      at = (*marker)["pos"];
    GlobalPos pos{at[0].get<int>(), at[1].get<int>(), at[2].get<int>(), at[3].get<int>()};
    int       n  = marker->value("count", -1);
    ctx.require(co_await ctx.ensureLoaded(pos), "marker area loaded");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    auto  h      = gf::inventory::find(*region, bp);
    ctx.check(h.has_value(), "the container block survived the reload");
    if (h) {
        auto s = h->get(pos, 13);
        ctx.check(items::idOf(s) == "minecraft:gold_ingot" && items::countOf(s) == n, "the container kept its items (got " + items::idOf(s) + " x" + std::to_string(items::countOf(s)) + ")");
        if (items::countOf(s) == n) ctx.info("container marker verified count=" + std::to_string(n));
        h->set(pos, 13, ItemStack{});
    }
    auto was = marker->value("was", std::string{});
    if (auto const* original = was.empty() ? nullptr : gf::game::blockWith(was)) gf::game::setBlock(*region, bp, *original);
    else gf::game::clearBlock(*region, bp);
    gf::inventory::unregisterBlock(kContainerBlock);
});

// ---- progression ---------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.progression", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace pr = gf::progression;
    static JsonT        sData;
    static int         sGranted  = 0;
    static int         sInvFired = 0;
    static int         sTicks    = 0;
    static bool        registered = false;
    if (!registered) {
        registered = true;
        pr::onTrigger("gf_test:fixture_trigger", [](Player& p, JsonT const& data) {
            if (isActive(p)) sData = data;
        });
        pr::onGranted([](Player& p, std::string const& adv) {
            if (isActive(p) && adv == "gf_test:fixture_advancement") ++sGranted;
        });
        pr::events::onInventoryChanged([](Player& p) {
            if (isActive(p)) ++sInvFired;
        });
        pr::events::onTick([](Player& p) {
            if (isActive(p)) ++sTicks;
        });
    }
    auto pos = ctx.testPos(6007);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID();
    ActiveScope active(p);
    sData = nullptr;
    sGranted = sInvFired = sTicks = 0;
    pr::trigger(p, "gf_test:fixture_trigger", {{"bug", "twilightforest:firefly"}});
    ctx.check(sData.value("bug", std::string{}) == "twilightforest:firefly", "the trigger reached the fixture listener with its data");
    pr::revoke(p, "gf_test:fixture_advancement");
    ctx.check(!pr::has(p, "gf_test:fixture_advancement"), "not granted yet");
    pr::grant(p, "gf_test:fixture_advancement", "fixture_criterion");
    ctx.check(pr::has(p, "gf_test:fixture_advancement") && pr::done(p, "gf_test:fixture_advancement", "fixture_criterion"), "grant completes an advancement without a definition");
    pr::grant(p, "gf_test:fixture_advancement");
    ctx.check(sGranted == 1, "onGranted fired once");
    auto stored = db::get("gf:combat:progress:" + p.getUuid().asString());
    ctx.check(stored && stored->dump().find("gf_test:fixture_advancement") != std::string::npos, "progress is stored in the world DB");
    pr::revoke(p, "gf_test:fixture_advancement");
    ctx.check(!pr::has(p, "gf_test:fixture_advancement"), "revoke takes it back");
    pr::showMissing(p, "gf_test:fixture_advancement"); // a vanilla toast for the simulated player
    ctx.give("minecraft:stick", 1);
    co_await ctx.ticks(25);
    auto* again = ll::service::getLevel()->getPlayer(id);
    ctx.require(again != nullptr, "the player is still there");
    ctx.check(sInvFired >= 1, "onInventoryChanged fired");
    ctx.check(sTicks >= 1, "onTick fired within a second");
});

// ---- entity drops --------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("combat.entity_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool        registered = false;
    static int         sFixture   = 0;
    static bool        sKiller    = false;
    static std::string sType;
    static int         sAdded     = 0;
    if (!registered) {
        registered = true;
        // the death pipeline's drops: gf_test:boss rolls gf_test:entities/boss (the F0 fixture table)
        gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const& lc) {
            if (gActive == 0 || dead.getTypeName() != "gf_test:boss") return;
            sFixture = countIn(drops, "gf_test:fixture_item");
            sKiller  = lc.player != nullptr && lc.player->getOrCreateUniqueID().rawID == gActive;
            sType    = lc.damageType;
            if (auto bonus = items::make("minecraft:stick", 1)) drops.push_back(*bonus); // handlers may add
            sAdded = countIn(drops, "minecraft:stick");
            drops.clear(); // and take everything: nothing is left in the shared test row
        });
    }
    auto pos = ctx.testPos(6008);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto& p      = ctx.player();
    Vec3  at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    auto* mob = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("gf_test:boss"), nullptr, at, false, false, false);
    ctx.require(mob != nullptr, "the fixture boss spawned");
    sFixture = 0;
    sAdded   = 0;
    sKiller  = false;
    sType.clear();
    {
        ActiveScope active(p);
        gf::combat::hurt(*mob, 1000.f, "twilightforest:thrown_axe", &p);
        co_await ctx.ticks(2); // the drop pass runs on the next tick, while the test is active
    }
    ctx.check(sFixture == 1, "the Java table gf_test:entities/boss dropped the fixture item (" + std::to_string(sFixture) + ")");
    ctx.check(sAdded == 1, "a handler added a stack to the drops");
    ctx.check(sKiller, "the loot context names the killing player");
    ctx.check(sType == "twilightforest:thrown_axe", "the loot context has the damage type (" + sType + ")");
    ctx.check(itemEntitiesNear(ctx.testPos(6008), 4) == 0, "the handler took every drop: no item entity is left");
});

// Vanilla mobs: the handlers see the game's own death loot (Bedrock's loot table) and may change it.
GF_REGISTER_SELFTEST("combat.entity_drops_vanilla", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool registered = false;
    static int  sBeef      = 0;
    static int  sCalls     = 0;
    if (!registered) {
        registered = true;
        gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const&) {
            if (gActive == 0 || dead.getTypeName() != "minecraft:cow") return;
            ++sCalls;
            sBeef = countIn(drops, "minecraft:beef") + countIn(drops, "minecraft:cooked_beef");
            drops.clear(); // Java's LivingDropsEvent: the handler takes the drops
        });
    }
    auto pos = ctx.testPos(6013);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto& p      = ctx.player();
    Vec3  at{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
    auto* cow = region->getLevel().getSpawner().spawnMob(*region, ActorDefinitionIdentifier("minecraft:cow"), nullptr, at, false, false, false);
    ctx.require(cow != nullptr, "a cow spawned");
    if (cow->isBaby()) {
        cow->remove();
        ctx.info("the cow was a calf (no loot): nothing to check");
        co_return;
    }
    sBeef  = 0;
    sCalls = 0;
    {
        ActiveScope active(p);
        gf::combat::hurt(*cow, 1000.f, "twilightforest:thrown_axe", &p);
        co_await ctx.ticks(2); // the drop pass runs on the next tick, while the test is active
    }
    co_await ctx.ticks(2);
    ctx.check(sCalls == 1, "the drops handlers ran once for the vanilla mob (" + std::to_string(sCalls) + ")");
    ctx.check(sBeef >= 1, "the handler saw Bedrock's own loot (" + std::to_string(sBeef) + " beef)");
    ctx.check(itemEntitiesNear(pos, 4) == 0, "the drops the handler took are gone from the world");
});

// ---- loot: Java's number rounding, set_contents ---------------------------------------------------

GF_REGISTER_SELFTEST("combat.loot_number_rounding", [](Context& ctx) {
    namespace loot = gf::loot;
    // NumberProvider#getInt = Math.round(getFloat): half up
    ctx.check(loot::javaRound(0.5f) == 1 && loot::javaRound(2.5f) == 3 && loot::javaRound(-0.5f) == 0 && loot::javaRound(-2.5f) == -2,
              "Math.round rounds half up");
    ctx.check(loot::javaRound(0.49999997f) == 0 && loot::javaRound(1.4f) == 1, "Math.round below one half rounds down");
    // multiplayer_rolls with per_player_rolls 1.5 and 2 qualified players: 1 + 1.5 = 2.5 -> 3 rolls;
    // constant rolls 1.5 -> 2; binomial n 5.5 -> 6 trials (p 1); set_count 2.5 -> 3
    loot::registerTable("gf_test:combat/rounding", JsonT::parse(R"({
      "pools": [
        { "rolls": { "type": "twilightforest:multiplayer_rolls", "default_rolls": 1, "per_player_rolls": 1.5 },
          "entries": [ { "type": "minecraft:item", "name": "minecraft:stick" } ] },
        { "rolls": 1.5, "entries": [ { "type": "minecraft:item", "name": "minecraft:flint" } ] },
        { "rolls": { "type": "minecraft:binomial", "n": 5.5, "p": 1.0 }, "entries": [ { "type": "minecraft:item", "name": "minecraft:clay_ball" } ] },
        { "rolls": 1, "entries": [ { "type": "minecraft:item", "name": "minecraft:brick",
            "functions": [ { "function": "minecraft:set_count", "count": 2.5 } ] } ] }
      ]
    })"));
    loot::setTestParticipants(2);
    loot::setTestAdjustsLoot(true);
    loot::LootContext lc;
    lc.seed = 99;
    auto l  = loot::roll("gf_test:combat/rounding", lc);
    loot::setTestParticipants(-1);
    loot::setTestAdjustsLoot(std::nullopt);
    loot::registerTable("gf_test:combat/rounding", nullptr);
    ctx.check(countIn(l, "minecraft:stick") == 3, "per_player_rolls 1.5 with 2 players: 3 rolls (got " + std::to_string(countIn(l, "minecraft:stick")) + ")");
    ctx.check(countIn(l, "minecraft:flint") == 2, "constant rolls 1.5: 2 rolls (got " + std::to_string(countIn(l, "minecraft:flint")) + ")");
    ctx.check(countIn(l, "minecraft:clay_ball") == 6, "binomial n 5.5: 6 trials (got " + std::to_string(countIn(l, "minecraft:clay_ball")) + ")");
    ctx.check(countIn(l, "minecraft:brick") == 3, "set_count 2.5: 3 (got " + std::to_string(countIn(l, "minecraft:brick")) + ")");
});

GF_REGISTER_SELFTEST("combat.loot_set_contents", [](Context& ctx) {
    namespace loot = gf::loot;
    // the shape of TF's entities/questing_ram_rewards: a bundle filled by set_contents with an item
    // entry and a nested loot table entry (every pool of it rolls into the bundle)
    loot::registerTable("gf_test:combat/bundle_blocks", JsonT::parse(R"({"pools":[
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:coal_block"}]},
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:iron_block"}]}]})"));
    loot::registerTable("gf_test:combat/bundle", JsonT::parse(R"({"pools":[
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:bundle","functions":[
          {"function":"minecraft:set_contents","component":"minecraft:bundle_contents","entries":[
            {"type":"minecraft:item","name":"minecraft:emerald"},
            {"type":"minecraft:loot_table","value":"gf_test:combat/bundle_blocks"}]}]}]},
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:chest","functions":[
          {"function":"minecraft:set_contents","component":"minecraft:container","entries":[
            {"type":"minecraft:item","name":"minecraft:apple"}]}]}]}]})"));
    loot::LootContext lc;
    lc.seed = 5;
    auto l  = loot::roll("gf_test:combat/bundle", lc);
    loot::registerTable("gf_test:combat/bundle", nullptr);
    loot::registerTable("gf_test:combat/bundle_blocks", nullptr);
    ItemStack const* bundle = nullptr;
    ItemStack const* chest  = nullptr;
    for (auto const& s : l) {
        if (items::idOf(s) == "minecraft:bundle") bundle = &s;
        if (items::idOf(s) == "minecraft:chest") chest = &s;
    }
    ctx.require(bundle != nullptr, "the bundle dropped");
#ifndef GF_CLIENT
    ctx.check(std::string(StorageItemComponentTags::bundleIDTag()) == loot::kBundleIdTag,
              "the bundle id tag is the game's (" + std::string(StorageItemComponentTags::bundleIDTag()) + ")");
#endif
    auto inside = loot::bundleContents(*bundle);
    ctx.require(inside.has_value(), "the bundle has a Bedrock storage container");
    ctx.check(countIn(*inside, "minecraft:emerald") == 1 && countIn(*inside, "minecraft:coal_block") == 1 && countIn(*inside, "minecraft:iron_block") == 1,
              "the bundle holds the item entry and every pool of the nested table (" + std::to_string(inside->size()) + " stacks)");
    ctx.require(chest != nullptr, "the chest item dropped");
    auto comps = items::getData(*chest, "components");
    ctx.check(comps.is_object() && comps.value("minecraft:container", JsonT::array()).size() == 1, "other components keep their contents in the item data");
});

// ---- inventory: the server decides what a screen shows and changes ----------------------------------

GF_REGISTER_SELFTEST("combat.inventory_screen_auth", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    declareFixture();
    auto a = ctx.testPos(6011);
    auto b = ctx.testPos(6012);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    ctx.require(ctx.setBlock(a, kContainerBlock) && ctx.setBlock(b, kContainerBlock), "two containers placed");
    auto* region = ctx.region();
    auto  ha     = inv::find(*region, gf::game::blockPos(a));
    auto  hb     = inv::find(*region, gf::game::blockPos(b));
    ctx.require(ha && hb, "both are containers");
    ha->set(a, 0, *items::make("minecraft:emerald", 3));
    hb->set(b, 0, *items::make("minecraft:diamond", 5));
    auto& p = ctx.player();
    gf::net::setTestCompanion(p, true);
    (void)gf::net::takeTestMessages(p);
    ctx.require(inv::openChestLike(p, a, 3, "container.chest"), "the server opened container A");
    auto ctxOf = [](GlobalPos const& pos, int rows, std::string const& title) {
        return JsonT{{"pos", {pos.dim, pos.x, pos.y, pos.z}}, {"rows", rows}, {"title", title}};
    };
    auto click = JsonT{{"op", "click"}, {"slot", 0}, {"button", "left"}, {"shift", true}};
    // a forged context: container B was never opened for this player
    gf::net::injectFromClient(p, "screen.action", {{"type", "inventory.chest"}, {"context", ctxOf(b, 3, "container.chest")}, {"action", click}});
    ctx.check(items::countOf(hb->get(b, 0)) == 5 && countInInventory(p, "minecraft:diamond") == 0, "an action on a container the server did not open is refused");
    auto msgs    = gf::net::takeTestMessages(p);
    bool refused = std::any_of(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "screen.close"; });
    ctx.check(refused, "the forged screen is closed");
    // the opened container, with forged rows and title: the server's own are used
    ctx.require(inv::openChestLike(p, a, 3, "container.chest"), "container A opened again");
    (void)gf::net::takeTestMessages(p);
    gf::net::injectFromClient(p, "screen.action", {{"type", "inventory.chest"}, {"context", ctxOf(a, 6, "forged.title")}, {"action", click}});
    ctx.check(items::isEmpty(ha->get(a, 0)) && countInInventory(p, "minecraft:emerald") == 3, "the opened container's quick move works");
    msgs             = gf::net::takeTestMessages(p);
    JsonT updateData = nullptr;
    for (auto const& m : msgs)
        if (m.channel == "screen.update") updateData = m.payload.value("data", JsonT{});
    ctx.check(updateData.is_object() && updateData.value("rows", 0) == 3 && updateData.value("title", std::string{}) != "forged.title",
              "rows and title come from the server's record (" + (updateData.is_object() ? updateData.dump() : std::string("no update")) + ")");
    gf::net::setTestCompanion(p, false);
    hb->set(b, 0, ItemStack{});
    inv::unregisterBlock(kContainerBlock);
});

// Vanilla clients' chest screen: the chest minecart proxy mirrors the storage both ways.
GF_REGISTER_SELFTEST("combat.inventory_vanilla_proxy", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace inv = gf::inventory;
    declareFixture();
    auto pos = ctx.testPos(6014);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, kContainerBlock), "a 27-slot container placed");
    auto* region = ctx.region();
    auto  h      = inv::find(*region, gf::game::blockPos(pos));
    ctx.require(h.has_value(), "the container handler");
    h->set(pos, 0, *items::make("minecraft:emerald", 4));
    auto& p      = ctx.player();
    auto  id     = p.getOrCreateUniqueID();
    int   before = inv::vanillaProxyCount();
    ctx.require(inv::openChestLike(p, pos, 3, "container.chest"), "a vanilla player opened it");
    ctx.check(inv::vanillaProxyCount() == before + 1, "a chest minecart proxy stands in for the container");
    auto* proxy = inv::vanillaProxyContainer(pos);
    ctx.require(proxy != nullptr, "the proxy's container");
    ctx.check(items::countOf(proxy->getItem(0)) == 4, "the proxy shows the stored slots");
    co_await ctx.ticks(6); // the screen opens after the proxy reached the client
    proxy = inv::vanillaProxyContainer(pos);
    ctx.require(proxy != nullptr, "the proxy is still open after the screen opened (" + std::to_string(inv::vanillaProxyCount()) + " open)");
    proxy->setItem(5, *items::make("minecraft:gold_ingot", 7)); // as a click in the game's screen
    h->set(pos, 6, *items::make("minecraft:apple", 2));        // other code changes the storage
    co_await ctx.ticks(7); // the storage is read every 5 ticks
    proxy = inv::vanillaProxyContainer(pos);
    ctx.require(proxy != nullptr, "the proxy is still open");
    ctx.check(items::countOf(h->get(pos, 5)) == 7, "a change in the screen reaches the storage");
    ctx.check(items::countOf(proxy->getItem(6)) == 2, "a change of the storage reaches the screen");
    // the player closes the screen: the proxy goes, the storage keeps everything
    auto* again = ll::service::getLevel()->getPlayer(id);
    ctx.require(again != nullptr, "the player is still there");
    static_cast<ServerPlayer&>(*again).doDeleteContainerManager(false);
    co_await ctx.ticks(3);
    ctx.check(inv::vanillaProxyContainer(pos) == nullptr && inv::vanillaProxyCount() == before, "the proxy is removed when the screen closes");
    ctx.check(items::countOf(h->get(pos, 0)) == 4 && items::countOf(h->get(pos, 5)) == 7 && items::countOf(h->get(pos, 6)) == 2, "the storage kept every slot");
    ctx.check(itemEntitiesNear(pos, 4) == 0, "nothing dropped from the proxy");
    for (int i = 0; i < h->slots; ++i) h->set(pos, i, ItemStack{});
    inv::unregisterBlock(kContainerBlock);
});

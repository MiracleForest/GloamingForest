// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U9 (artifacts): selftests (artifacts.*, testPos dx 900-999, rule R12). docs/spec/artifacts.md 5.
#include <cmath>
#include <string>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/platform/UUID.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/inventory/Inventory.h"
#include "feature/item/Item.h"
#include "feature/stats/Stats.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::artifacts {

namespace ext {
int give3ShieldsRuns();
}

namespace {

using gf::selftest::Context;
namespace items = game::items;
namespace ia    = gf::feature::common::interactions;

Vec3 at(core::GlobalPos const& p, float dy = 0.0f) { return Vec3{p.x + 0.5f, p.y + dy, p.z + 0.5f}; }

ItemStack make(std::string const& id, int count = 1, int aux = 0) { return items::make(id, count, aux).value_or(items::empty()); }

bool hasEffect(Actor const& a, char const* name) {
    auto const* e = MobEffect::getByName(name);
    return e && a.getEffect(e->mId) != nullptr;
}

int ampOf(Actor const& a, char const* name) {
    auto const* e = MobEffect::getByName(name);
    auto const* i = e ? a.getEffect(e->mId) : nullptr;
    return i ? static_cast<int>(i->mAmplifier) : -1;
}

void clearInventory(Player& p) {
    for (int i = 0; i < 36; ++i) p.getInventory().setItem(i, items::empty());
    for (int i = 0; i < 4; ++i) p.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i), items::empty());
    p.setOffhandSlot(items::empty());
}

struct DifficultyScope {
    SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
    DifficultyScope() {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
        }
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

struct ConfigScope {
    explicit ConfigScope(ArtifactsConfig const& c) { overrideConfig(c); }
    ~ConfigScope() { restoreConfig(); }
};

} // namespace

// Scepter durabilities (upstream: 99, 99, 9, 9), the charge tooltip and hurtButDontBreak.
GF_REGISTER_SELFTEST("artifacts.scepter_durability", [](Context& ctx) {
    std::pair<char const*, int> const want[] = {{kGloamingScepter, 99}, {kLifedrainScepter, 99}, {kZombieScepter, 9}, {kFortificationScepter, 9}};
    for (auto [id, n] : want) {
        auto s = make(id);
        ctx.require(!items::isEmpty(s), std::string("the item exists: ") + id);
        ctx.check(charges(s) == n, std::string(id) + " has " + std::to_string(n) + " charges (" + std::to_string(charges(s)) + ")");
        for (int i = 0; i < n + 5; ++i) hurtButDontBreak(s, 1);
        ctx.check(charges(s) == 0 && !items::isEmpty(s), std::string(id) + " empties but never breaks");
    }
});

// GloamingWandBolt: a bolt shot at a mob deals 6 gloaming_scepter damage.
GF_REGISTER_SELFTEST("artifacts.gloaming_bolt", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy;
    co_await ctx.flatArea(900, 16);
    auto  c      = ctx.testPos(906, 2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    Mob* target = spawnMob(*region, "minecraft:iron_golem", at(c.offset(4, 1, 0)));
    ctx.require(target != nullptr, "a target");
    auto tid = uidOf(*target);
    co_await ctx.ticks(2);
    auto& p = ctx.player();
    p.teleport(at(c.offset(-2, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    auto* t = actorById(tid);
    ctx.require(t != nullptr, "the target is there");
    float before = static_cast<float>(t->getHealth());
    static_cast<SimulatedPlayer&>(ctx.player()).simulateLookAt(t->getPosition() + Vec3{0, 1.0f, 0}, sim::LookDuration::Instant);
    co_await ctx.ticks(1);
    Actor* bolt = shootWandBolt(ctx.player(), 0.0f);
    ctx.require(bolt != nullptr, "a wand bolt was shot");
    bool hit = false;
    for (int i = 0; i < 40 && !hit; ++i) {
        co_await ctx.ticks(1);
        t   = actorById(tid);
        hit = t && static_cast<float>(t->getHealth()) <= before - 6.0f;
    }
    ctx.check(hit, "the bolt dealt 6 damage");
    if (auto* g = actorById(tid)) g->remove();
});

// LifedrainScepterItem#onUseTick: 1 lifedrain damage per 5 use ticks, Slowness III, a charge.
GF_REGISTER_SELFTEST("artifacts.lifedrain", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy;
    co_await ctx.flatArea(910, 16);
    auto  c      = ctx.testPos(916, 2);
    auto* region = ctx.region();
    Mob*  target = spawnMob(*region, "minecraft:iron_golem", at(c.offset(3, 1, 0)));
    ctx.require(target != nullptr, "a target");
    auto tid = uidOf(*target);
    auto& p  = ctx.player();
    p.teleport(at(c.offset(0, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(3);
    auto* t = actorById(tid);
    ctx.require(t != nullptr, "the target is there");
    static_cast<SimulatedPlayer&>(ctx.player()).simulateLookAt(t->getEyePos(), sim::LookDuration::Instant);
    co_await ctx.ticks(2);
    auto&     player = ctx.player();
    ItemStack s      = make(kLifedrainScepter);
    ctx.check(lookTarget(player) != nullptr, "the scepter's ray finds the target");
    int before = actorById(tid)->getHealth();
    lifedrainTick(player, s, 72000 - 5);
    t = actorById(tid);
    ctx.require(t != nullptr, "the target lives");
    ctx.check(t->getHealth() == before - 1, "1 lifedrain damage (" + std::to_string(before) + " -> " + std::to_string(t->getHealth()) + ")");
    ctx.check(ampOf(*t, "slowness") == 2, "Slowness III");
    ctx.check(charges(s) == 98, "one charge used");
    t->remove();
});

// LoyalZombie: 40 HP, tamed by the summoner, decays without Strength, renewed by rotten flesh.
GF_REGISTER_SELFTEST("artifacts.loyal_zombie", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy;
    co_await ctx.flatArea(920, 16);
    auto  c      = ctx.testPos(926, 2);
    auto& p      = ctx.player();
    p.teleport(at(c.offset(-2, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    auto* region = ctx.region();
    Mob*  z      = spawnMob(*region, kLoyalZombie, at(c.offset(2, 1, 0)));
    ctx.require(z != nullptr, "a loyal zombie");
    // LoyalZombie#removeWhenFarAway: only an untamed one despawns (BP group gf:untamed)
    static HashedString const despawn("minecraft:despawn");
    Mob* wild = spawnMob(*region, kLoyalZombie, at(c.offset(4, 1, 2)));
    ctx.require(wild != nullptr, "an untamed loyal zombie");
    auto wildId = uidOf(*wild);
    tameLoyalZombie(*z, ctx.player());
    auto zid = uidOf(*z);
    co_await ctx.ticks(1);
    if (auto* w = actorById(wildId)) {
        ctx.check(w->hasComponent(despawn), "an untamed loyal zombie despawns far from players");
        w->remove();
    }
    z = static_cast<Mob*>(actorById(zid));
    ctx.require(z != nullptr, "the tamed zombie is there");
    ctx.check(!z->hasComponent(despawn), "a tamed loyal zombie never despawns");
    ctx.check(z->getMaxHealth() == 40, "40 max health (" + std::to_string(z->getMaxHealth()) + ")");
    ctx.check(z->getOwnerId().rawID == uidOf(ctx.player()), "owned by the summoner");
    co_await ctx.ticks(45);
    z = static_cast<Mob*>(actorById(zid));
    ctx.require(z != nullptr, "the zombie is there");
    ctx.check(z->getHealth() <= 36, "no Strength: 2 expired damage every 20 ticks (" + std::to_string(z->getHealth()) + ")");
    // the owner feeds it rotten flesh
    auto& player = ctx.player();
    player.setSelectedItem(make("minecraft:rotten_flesh", 2));
    ItemStack held = player.getCarriedItem();
    std::string zidType = kLoyalZombie, fleshId = "minecraft:rotten_flesh";
    ia::EntityContext ec{player, held, fleshId, *z, zidType, {0, 0, 0}, false};
    (void)ia::dispatchInteract(ec);
    co_await ctx.ticks(1);
    z = static_cast<Mob*>(actorById(zid));
    ctx.require(z != nullptr, "the zombie is still there");
    ctx.check(ampOf(*z, "strength") == 1, "Strength II renewed");
    z->remove();
    player.setSelectedItem(items::empty());
});

// FortificationShieldAttachment: 5 temporary shields absorb hits (one per invulnerability window),
// expire after 240 ticks, count gf_shields_broken; /gloamingforest shield; give_3_shields.
GF_REGISTER_SELFTEST("artifacts.shields", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    setShields(p, 0, true);
    setShields(p, 0, false);
    setShields(p, 5, true);
    ctx.check(shieldsOf(p).left() == 5, "5 temporary shields");
    long long stat = gf::stats::value(p, "gloamingforest:gf_shields_broken");
    int       hp   = p.getHealth();
    bool      ok   = gf::combat::hurt(p, 4.0f, "minecraft:generic");
    ctx.check(!ok && p.getHealth() == hp, "the hit was absorbed");
    ctx.check(shieldsOf(p).left() == 4, "one shield broke (" + std::to_string(shieldsOf(p).left()) + ")");
    ctx.check(gf::stats::value(p, "gloamingforest:gf_shields_broken") == stat + 1, "gf_shields_broken counted");
    (void)gf::combat::hurt(p, 4.0f, "minecraft:generic");
    ctx.check(shieldsOf(ctx.player()).left() == 4, "a hit inside the invulnerability window breaks none");
    co_await ctx.ticks(245);
    ctx.check(shieldsOf(ctx.player()).temporary == 3, "a temporary shield expired after 240 ticks (" + std::to_string(shieldsOf(ctx.player()).temporary) + ")");
    setShields(ctx.player(), 0, true);
    // the subcommand (by name) and the function
    auto name = ctx.player().getRealName();
    auto r    = gf::selftest::runServerCommand("gloamingforest shield \"" + name + "\" add 2 false");
    ctx.check(static_cast<bool>(r), "/gloamingforest shield runs");
    ctx.check(shieldsOf(ctx.player()).permanent == 2, "2 permanent shields added (" + std::to_string(shieldsOf(ctx.player()).permanent) + ")");
    int runs = ext::give3ShieldsRuns();
    auto f   = gf::selftest::runServerCommand("execute as \"" + name + "\" at @s run function gloamingforest/give_3_shields");
    ctx.check(static_cast<bool>(f), "the function runs");
    ctx.check(ext::give3ShieldsRuns() == runs + 1 && shieldsOf(ctx.player()).permanent == 5, "give_3_shields added 3 permanent shields");
    setShields(ctx.player(), 0, false);
});

// CharmEvents#handleCharmOfLife: charm II full health and effects, charm I health 8.
GF_REGISTER_SELFTEST("artifacts.charm_of_life", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    clearInventory(p);
    p.getInventory().setItem(5, make(kLife2));
    p.getInventory().setItem(6, make(kLife1));
    (void)gf::combat::hurt(p, 1000.0f, "minecraft:generic");
    co_await ctx.ticks(1);
    auto& q = ctx.player();
    ctx.check(q.isAlive() && q.getHealth() == q.getMaxHealth(), "charm II: alive at full health (" + std::to_string(q.getHealth()) + ")");
    ctx.check(ampOf(q, "regeneration") == 3 && hasEffect(q, "resistance") && hasEffect(q, "fire_resistance"), "charm II: Regeneration IV, Resistance, Fire Resistance");
    ctx.check(items::isEmpty(q.getInventory().getItem(5)), "charm II consumed");
    co_await ctx.ticks(12);
    (void)gf::combat::hurt(ctx.player(), 1000.0f, "minecraft:generic");
    co_await ctx.ticks(1);
    auto& r = ctx.player();
    ctx.check(r.isAlive() && r.getHealth() == 8, "charm I: alive at 8 health (" + std::to_string(r.getHealth()) + ")");
    ctx.check(items::isEmpty(r.getInventory().getItem(6)), "charm I consumed");
    clearInventory(r);
});

// CharmEvents#handleCharmOfKeeping on a real inventory, the casket placement and the return.
GF_REGISTER_SELFTEST("artifacts.keeping", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(940, 16);
    auto  c = ctx.testPos(946, 2);
    auto& p = ctx.player();
    p.teleport(at(c.offset(0, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    auto& player = ctx.player();
    (void)test::kept(player);
    gf::game::db::erase("gf:artifacts:kept:" + player.getUuid().asString());
    clearInventory(player);
    auto& inv = player.getInventory();
    inv.setItem(0, make("minecraft:iron_sword"));
    inv.setItem(1, make(kCasket, 2));
    inv.setItem(2, make(kKeeping2));
    inv.setItem(20, make("minecraft:dirt", 7));
    player.setArmor(SharedTypes::Legacy::ArmorSlot::Head, make("minecraft:iron_helmet"));
    test::keep(player);
    ctx.check(items::isEmpty(inv.getItem(0)) && items::isEmpty(inv.getItem(2)), "charm II: the hotbar is kept (and the charm used)");
    ctx.check(items::idOf(inv.getItem(1)) == kCasket && items::countOf(inv.getItem(1)) == 1, "one casket stays for placing");
    ctx.check(items::countOf(inv.getItem(20)) == 7, "outside the hotbar: not kept");
    ctx.check(items::isEmpty(player.getArmor(SharedTypes::Legacy::ArmorSlot::Head)), "armour kept");
    auto rec = test::kept(player);
    ctx.check(rec.is_object() && rec.value("charm", std::string{}) == kKeeping2, "the consumed charm is remembered");
    // the drops: the casket, the dirt
    std::vector<ItemStack> drops{inv.getItem(1), inv.getItem(20)};
    test::stock(player, drops);
    bool taken = items::isEmpty(drops[0]) && items::isEmpty(drops[1]);
    ctx.check(taken, "the casket took the drops");
    // find the casket
    auto* region = ctx.region();
    std::optional<BlockPos> casket;
    for (int dy = -1; dy <= 3 && !casket; ++dy) {
        BlockPos q = game::blockPosOf(player.getFeetPos());
        q.y += dy;
        if (game::blockIdAt(*region, q) == kCasket) casket = q;
    }
    ctx.require(casket.has_value(), "a keepsake casket was placed");
    auto h = gf::inventory::find(*region, *casket);
    ctx.require(h.has_value(), "the casket is a container");
    auto gpos = game::globalPos(*region, *casket);
    ctx.check(h->slots == 45, "45 slots");
    ctx.check(items::idOf(h->get(gpos, 20 - 9 + 9)) == "minecraft:dirt", "main slot 20 at casket slot 20 (rows 9..35 first)");
    auto info = casketInfo(*region, *casket);
    ctx.check(info && info->name.find("'s ") != std::string::npos, "the casket is named after the player");
    ctx.cleanupBlock(gpos);
    // respawn: the kept stacks come back to their slots
    clearInventory(player);
    test::returnStored(player);
    ctx.check(items::idOf(inv.getItem(0)) == "minecraft:iron_sword", "the sword is back in slot 0");
    ctx.check(items::idOf(player.getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == "minecraft:iron_helmet", "the helmet is back");
    clearInventory(player);
});

// The keepsake casket block: UUID locking, the charm III repair, the damage roll bounds.
GF_REGISTER_SELFTEST("artifacts.casket", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(950, 16);
    auto  c = ctx.testPos(956, 2);
    auto& p = ctx.player();
    p.teleport(at(c.offset(0, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    ArtifactsConfig cfg;
    cfg.casketUUIDLocking = true;
    ConfigScope scope(cfg);
    std::vector<ItemStack> contents(45, items::empty());
    contents[0] = make("minecraft:stone", 3);
    ctx.require(placeCasket(ctx.player(), contents, 0), "a casket was placed");
    auto*                   region = ctx.region();
    std::optional<BlockPos> casket;
    for (int dy = -1; dy <= 3 && !casket; ++dy) {
        BlockPos q = game::blockPosOf(ctx.player().getFeetPos());
        q.y += dy;
        if (game::blockIdAt(*region, q) == kCasket) casket = q;
    }
    ctx.require(casket.has_value(), "the casket is there");
    auto info = casketInfo(*region, *casket);
    ctx.check(info && info->ownerUuid == ctx.player().getUuid().asString(), "locked to its owner");
    ctx.check(canOpenCasket(ctx.player(), *region, *casket), "the owner opens it");
    auto& other = ctx.spawnPlayer("casket_other");
    ctx.check(!canOpenCasket(other, *region, *casket), "another player may not");
    // a charm of keeping III repairs a damaged casket one level
    auto gpos = game::globalPos(*region, *casket);
    auto states = game::statesOf(region->getBlock(*casket));
    Json j{{"gloamingforest:facing", states["gloamingforest:facing"]}, {"gloamingforest:multilogged", "air"}, {"gloamingforest:damage", 2}};
    game::setBlock(*region, *casket, *game::blockWithStates(kCasket, j));
    auto& player = ctx.player();
    player.setSelectedItem(make(kKeeping3));
    ItemStack held = player.getCarriedItem();
    std::string itemId = kKeeping3, blockId = kCasket;
    ia::UseOnContext uc{player, held, itemId, region->getBlock(*casket), blockId, gpos, 1, {0.5, 1.0, 0.5}, false, true};
    (void)ia::dispatchUseOn(uc);
    ctx.check(game::intState(region->getBlock(*casket), "gloamingforest:damage").value_or(-1) == 1, "repaired to damage 1");
    player.setSelectedItem(items::empty());
    ctx.cleanupBlock(gpos);
});

// BrittleFlaskItem: fill, drink (effect, crack, breakage), break at the third dose; drink_from_flask.
GF_REGISTER_SELFTEST("artifacts.flask", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    clearInventory(p);
    ItemStack flask  = make(kBrittleFlask);
    for (int i = 0; i < 3; ++i) {
        ItemStack potion = make("minecraft:potion", 1, 14); // swiftness (potions do not stack)
        ctx.check(fillFlask(p, flask, potion, true), "dose " + std::to_string(i + 1) + " went in");
        ctx.check(items::isEmpty(potion), "the potion was used up");
    }
    ItemStack more = make("minecraft:potion", 1, 14);
    ctx.check(!fillFlask(p, flask, more, true), "no fourth dose");
    ctx.check(flaskOf(flask).doses == 3 && flaskOf(flask).potion == 14, "3 doses of swiftness");
    drinkFlask(p, flask);
    ctx.check(hasEffect(p, "speed"), "Speed applied");
    ctx.check(flaskOf(flask).doses == 2 && flaskOf(flask).breakage == 1, "2 doses left, cracked");
    ctx.check(flaskTracking(p).dosesDrank == 1, "drink_from_flask: 1 dose");
    drinkFlask(p, flask);
    ctx.check(flaskTracking(p).dosesDrank == 2, "drink_from_flask: 2 doses in a row");
    drinkFlask(p, flask);
    ctx.check(items::isEmpty(flask), "the brittle flask broke at its third dose");
    // a strong harming dose: 6 << 1 damage
    co_await ctx.ticks(12);
    ItemStack greater = make(kGreaterFlask);
    ItemStack harm    = make("minecraft:potion", 1, 24);
    ctx.require(fillFlask(p, greater, harm, true), "a strong harming dose");
    int hp = ctx.player().getHealth();
    drinkFlask(ctx.player(), greater);
    ctx.check(ctx.player().getHealth() <= hp - 12 || !ctx.player().isAlive(), "12 failed_challenge damage");
    ctx.check(flaskOf(greater).breakage == 0 && flaskOf(greater).empty(), "the greater flask does not crack");
    clearInventory(ctx.player());
});

// PocketWatchItem: hidden Speed and Jump Boost from the hotbar, Haste and no Mining Fatigue held;
// MoonDialItem's phase key.
GF_REGISTER_SELFTEST("artifacts.pocket_watch", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    clearInventory(p);
    p.getInventory().setItem(p.getSelectedItemSlot(), make(kPocketWatch));
    co_await ctx.ticks(3);
    auto& q = ctx.player();
    ctx.check(hasEffect(q, "speed") && hasEffect(q, "jump_boost"), "Speed and Jump Boost");
    ctx.check(hasEffect(q, "haste"), "Haste while held");
    auto const* f = MobEffect::getByName("mining_fatigue");
    if (f) {
        MobEffectInstance inst(f->mId, EffectDuration{200});
        q.addEffect(inst);
    }
    co_await ctx.ticks(2);
    ctx.check(!hasEffect(ctx.player(), "mining_fatigue"), "no Mining Fatigue while held");
    clearInventory(ctx.player());
    auto key = moonPhaseKeyFor(&ctx.player());
    ctx.check(key.size() == 1 && key[0] >= '0' && key[0] <= '7', "a moon phase in the overworld (" + key + ")");
});

// The four custom recipes and the cloth washing.
GF_REGISTER_SELFTEST("artifacts.recipes", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.check(scepterRepairs().size() == 4, "4 scepter repair recipes loaded (" + std::to_string(scepterRepairs().size()) + ")");
    ItemStack scepter = make(kGloamingScepter);
    for (int i = 0; i < 20; ++i) hurtButDontBreak(scepter, 1);
    ItemStack pearl = make("minecraft:ender_pearl");
    auto      out   = test::craft("scepter", {&scepter, &pearl, &pearl}, "gloamingforest:equipment/gloaming_scepter");
    ctx.check(out && out->getDamageValue() == 2, "two pearls restore 18 charges");
    ctx.check(test::craft("scepter", {&scepter, &pearl, &pearl, &pearl}, "gloamingforest:equipment/gloaming_scepter").has_value(), "a third pearl tops it up");
    ctx.check(!test::craft("scepter", {&scepter, &pearl, &pearl, &pearl, &pearl}, "gloamingforest:equipment/gloaming_scepter"), "a fourth pearl would overshoot by a whole pearl");
    ItemStack essence = make(kEssence);
    auto      full    = test::craft("essence", {&essence, &scepter});
    ctx.check(full && full->getDamageValue() == 0, "an essence repairs fully");
    ItemStack casket = make(kCasket);
    items::setData(casket, kDataCasketDamage, 2);
    ItemStack charm = make(kKeeping3);
    auto      fixed = test::craft("casket", {&casket, &charm});
    ctx.check(fixed && items::getData(*fixed, kDataCasketDamage) == 1, "a charm III repairs a casket one level");
    ItemStack helmet = make("minecraft:iron_helmet");
    ItemStack cloth  = make(kEmperorsCloth);
    auto      clothed = test::craft("cloth", {&cloth, &helmet});
    ctx.require(clothed && hasCloth(*clothed), "the cloth goes on a helmet");
    ctx.check(items::getData(*clothed, "gloamingforest:emperors_cloth").is_object(), "as upstream's gloamingforest:emperors_cloth component (the key U7's no-template smithing writes)");
    ItemStack legacy = make("minecraft:iron_boots");
    items::setData(legacy, "emperors_cloth", true);
    ctx.check(hasCloth(legacy), "a stack clothed before audit round 2 still counts");
    ctx.check(!test::craft("cloth", {&cloth, &*clothed}), "not twice");
    // washing it off in a water cauldron
    co_await ctx.flatArea(970, 8);
    auto c = ctx.testPos(972, 2);
    ctx.setBlock(c.offset(0, 1, 0), "minecraft:cauldron", Json{{"fill_level", 6}, {"cauldron_liquid", "water"}});
    auto& player = ctx.player();
    player.setSelectedItem(*clothed);
    ItemStack const& held = player.getCarriedItem(); // the hand slot itself, as the dispatch gives it
    std::string itemId = "minecraft:iron_helmet", blockId = "minecraft:cauldron";
    auto* region = ctx.region();
    auto  cpos   = c.offset(0, 1, 0);
    ia::UseOnContext uc{player, held, itemId, region->getBlock(game::blockPos(cpos)), blockId, cpos, 1, {0.5, 1.0, 0.5}, false, true};
    (void)ia::dispatchUseOn(uc);
    ctx.check(!hasCloth(ctx.player().getCarriedItem()), "the cloth was washed off");
    ctx.check(game::intState(region->getBlock(game::blockPos(cpos)), "fill_level").value_or(-1) == 4, "the cauldron lost a level");
    ctx.player().setSelectedItem(items::empty());
    ctx.cleanupBlock(cpos);
});

// OreScannerComponent: a range 0 scan over 50 ticks counts the filter block in its chunk.
GF_REGISTER_SELFTEST("artifacts.ore_meter", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(980, 16);
    auto c = ctx.testPos(982, 2);
    auto& p = ctx.player();
    p.teleport(at(c.offset(0, 1, 0)), static_cast<DimensionType>(c.dim));
    co_await ctx.ticks(2);
    auto&    player = ctx.player();
    BlockPos feet   = game::blockPosOf(player.getFeetPos());
    int      cx = feet.x >> 4, cz = feet.z >> 4;
    // three emerald blocks in the scanned columns of the player's chunk (local 1..3)
    std::vector<core::GlobalPos> placed;
    for (int i = 1; i <= 3; ++i) {
        core::GlobalPos q{c.dim, cx * 16 + i, feet.y - 1, cz * 16 + 2};
        ctx.setBlock(q, "minecraft:emerald_block");
        placed.push_back(q);
    }
    ItemStack meter = make(kOreMeter);
    items::setData(meter, kDataOreRange, 0);
    items::setData(meter, kDataOreFilter, "minecraft:emerald_block");
    beginOreScan(player, meter);
    int ticks = 0;
    while (!items::getData(meter, kDataOreScanner).is_null() && ticks < 200) {
        tickOreMeter(meter, ctx.player());
        ++ticks;
    }
    ctx.check(ticks == 50, "the scan took 50 ticks (" + std::to_string(ticks) + ")");
    auto data = items::getData(meter, kDataOreData);
    ctx.require(data.is_object(), "results were stored");
    int n = data["counts"].value("minecraft:emerald_block", -1);
    ctx.check(n == 3, "3 emerald blocks counted (" + std::to_string(n) + ")");
    ctx.check(data.value("total", 0) == 15 * 15 * ctx.region()->getMaxHeight(), "the volume is 15 x 15 x the build height");
    for (auto const& q : placed) ctx.cleanupBlock(q);
});

} // namespace gf::feature::artifacts



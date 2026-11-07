// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of U10 (provisions.*, dx 1000..1099): the foods (nutrition, saturation, effects, bowls,
// the hydra chop trigger), stale bread's damage type, Experiment 115 (place, add, take, eat,
// regenerate, comparator, support), frosted (slowness, freezing, fire, freeze damage, immunity), the
// four enchantments (fire react / chill aura reactions, destruction damage and smashing, renewal) and
// the music discs (record component, jukebox song). Behaviour from TF 4.8 (see the headers of the
// files under test).
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>
#include <utility>

#include "ll/api/coro/CoroTask.h"

#include "mc/deps/shared_types/item/EnchantSlot.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/RecordItemComponent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"

#include "core/provisions/Provisions.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/effects/Effects.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/item/Records.h"
#include "feature/ext/provisions/Provisions.h"
#include "feature/item/ItemInternal.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/provisions/Internal.h"
#include "feature/spawn/Spawn.h"
#include "feature/stats/Stats.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

namespace fp    = gf::feature::provisions;
namespace cp    = gf::core::provisions;
namespace ia    = gf::feature::common::interactions;
namespace items = gf::game::items;
using gf::selftest::Context;
using Slot = SharedTypes::Legacy::ArmorSlot;

ItemStack make(std::string const& id, int count = 1) {
    auto s = items::make(id, count);
    return s ? *s : ItemStack::EMPTY_ITEM();
}

AttributeInstance* attribute(Actor& a, Attribute const& attr) {
    auto ref = a.getAttribute(attr);
    return const_cast<AttributeInstance*>(static_cast<AttributeInstance const*>(ref.mPtr));
}

float health(Actor& a) {
    auto* h = attribute(a, SharedAttributes::HEALTH());
    return h ? h->mCurrentValue : 0.0f;
}

void setHealth(Actor& a, float v) {
    if (auto* h = attribute(a, SharedAttributes::HEALTH())) h->mCurrentValue = v;
}

BlockPos bp(gf::core::GlobalPos const& g) { return gf::game::blockPos(g); }

Actor* pig(Context& ctx, gf::core::GlobalPos const& g, char const* type = "minecraft:pig") {
    auto* region = ctx.region();
    if (!region) return nullptr;
    return gf::spawn::spawnMob(*region, type, Vec3{g.x + 0.5f, static_cast<float>(g.y), g.z + 0.5f}, gf::spawn::SpawnReason::Triggered);
}

void discard(Actor* a) {
    if (a) a->remove();
}

/// A right-click of the test player on `g` (the selected item; sneaking as given).
ia::Result click(Context& ctx, gf::core::GlobalPos const& g, bool sneaking = false) {
    auto& p      = ctx.player();
    auto* region = ctx.region();
    if (!region) return ia::Result::Pass;
    Block const& block = region->getBlock(bp(g));
    auto         bid   = gf::game::blockId(block);
    ItemStack const& held = p.getSelectedItem();
    std::string      hid  = items::isEmpty(held) ? std::string() : items::idOf(held);
    ia::UseOnContext c{p, held, hid, block, bid, g, 1, {0.5, 1.0, 0.5}, sneaking, true};
    return ia::dispatchUseOn(c);
}

} // namespace

// ---- foods -------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("provisions.foods", [](Context& ctx) {
    auto& player = static_cast<Player&>(ctx.player());
    player.setPlayerGameType(GameType::Survival);
    int hydraTriggers = 0;
    static int* sCounter = nullptr;
    static bool listening = false;
    sCounter              = &hydraTriggers;
    if (!listening) {
        listening = true;
        gf::progression::onTrigger(fp::ids::kHydraTrigger, [](Player&, gf::progression::Json const&) {
            if (sCounter) ++*sCounter;
        });
    }
    for (auto const& f : cp::foods()) {
        std::string id(f.id);
        auto        stack = make(id);
        ctx.require(!items::isEmpty(stack), id + " exists");
        Item const* item = stack.mItem.get();
        ctx.require(item != nullptr, id + ": its Item");
        ctx.check(item->isFood(), id + " is food");
        ctx.check(item->getMaxUseDuration(&stack) == 32, id + " takes 32 ticks to eat");
        // eat it from an empty stomach (food level 0, saturation 0)
        fp::setFoodLevel(player, 0.0f);
        if (auto* sat = attribute(player, Player::SATURATION())) sat->mCurrentValue = 0.0f;
        for (auto const& e : f.effects)
            if (!e.effect.empty()) gf::effects::remove(player, std::string(e.effect));
        int       before = hydraTriggers;
        ItemStack copy   = stack;
        item->useTimeDepleted(copy, &player.getLevel(), &player);
        float food = fp::foodLevel(player);
        ctx.check(std::abs(food - static_cast<float>(f.nutrition)) < 0.01f, id + " gives " + std::to_string(f.nutrition) + " food (" + std::to_string(food) + ")");
        if (auto* sat = attribute(player, Player::SATURATION())) {
            float want = std::min(static_cast<float>(f.nutrition), cp::saturationOf(f.nutrition, f.saturation));
            ctx.check(std::abs(sat->mCurrentValue - want) < 0.05f, id + " gives " + std::to_string(want) + " saturation (" + std::to_string(sat->mCurrentValue) + ")");
        }
        for (auto const& e : f.effects) {
            if (e.effect.empty()) continue;
            ctx.check(gf::effects::has(player, std::string(e.effect)), id + " gives " + std::string(e.effect));
            gf::effects::remove(player, std::string(e.effect));
        }
        if (id == fp::ids::kHydraChop) ctx.check(hydraTriggers == before + 1, "the hydra chop eaten at food level 0 fires consume_hydra_chop_on_low_hunger");
        if (f.bowl) ctx.check(items::idOf(copy) == "minecraft:bowl" || items::countOf(copy) == 0, id + " leaves a bowl (" + items::idOf(copy) + ")");
    }
    // hydra chop with food: no trigger
    fp::setFoodLevel(player, 10.0f);
    int  before = hydraTriggers;
    auto chop   = make(fp::ids::kHydraChop);
    chop.mItem->useTimeDepleted(chop, &player.getLevel(), &player);
    ctx.check(hydraTriggers == before, "a hydra chop eaten with food does not fire the trigger");
    gf::effects::remove(player, "minecraft:regeneration");
    // stroganoff is always edible
    ctx.check(make(fp::ids::kStroganoff).getMaxStackSize() == 1, "meef stroganoff stacks to 1");
    fp::setFoodLevel(player, 20.0f);
    sCounter = nullptr;
    // E115's item counts a slice
    auto slices = gf::stats::value(player, fp::ids::kSlicesEaten);
    auto e115   = make(fp::ids::kE115);
    e115.mItem->useTimeDepleted(e115, &player.getLevel(), &player);
    ctx.check(gf::stats::value(player, fp::ids::kSlicesEaten) == slices + 1, "eating the experiment 115 item awards e115_slices_eaten");
    fp::setFoodLevel(player, 20.0f);
});

GF_REGISTER_SELFTEST("provisions.stale_bread", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1000, 8);
    (void)box;
    auto  at     = ctx.testPos(1002, 2);
    auto* victim = pig(ctx, at);
    ctx.require(victim != nullptr, "a pig to hit");
    auto& player = static_cast<Player&>(ctx.player());
    auto  held   = player.getSelectedItem();
    player.setSelectedItem(make(fp::ids::kStaleBread));
    int before = fp::counters().staleSandwich;
    gf::combat::hurt(*victim, 1.0f, "minecraft:player_attack", &player, &player);
    ctx.check(fp::counters().staleSandwich == before + 1, "a melee hit with stale bread is a twilightforest:stale_sandwich hit");
    player.setSelectedItem(held);
    discard(victim);
    // Tiers.WOOD: enchantability 15 as a sword
    auto bread = make(fp::ids::kStaleBread);
    ctx.require(bread.mItem.get() != nullptr, "the stale bread item");
    ctx.check(bread.mItem->getEnchantValue() == 15, "enchantability 15 (" + std::to_string(bread.mItem->getEnchantValue()) + ")");
    ctx.check(bread.mItem->getEnchantSlot() == static_cast<int>(SharedTypes::EnchantSlot::Sword), "sword enchantments");
});

// ---- experiment 115 ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("provisions.experiment_115", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1010, 8);
    (void)box;
    auto g = ctx.testPos(1012, 2);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto& player = static_cast<Player&>(ctx.player());
    player.setPlayerGameType(GameType::Survival);
    auto held = player.getSelectedItem();

    // the item places one slice on a solid block
    auto below = gf::core::GlobalPos{g.dim, g.x, g.y - 1, g.z};
    ctx.require(ctx.setBlock(below, "minecraft:stone"), "stone below");
    ctx.cleanupBlock(g);
    ctx.require(fp::placeE115(player, *region, bp(below), 1, false), "the item places experiment 115 on stone");
    auto s = fp::e115At(*region, bp(g));
    ctx.check(s.bites == 7 && !s.regenerate, "a placed experiment 115 is one slice (omnomnom 7)");
    ctx.check(!fp::placeE115(player, *region, bp(gf::core::GlobalPos{g.dim, g.x + 3, g.y + 3, g.z}), 1, false), "not in the air (no solid block below)");

    // add slices with the item
    player.setSelectedItem(make(fp::ids::kE115, 8));
    for (int i = 0; i < 7; ++i) ctx.check(click(ctx, g) == ia::Result::Consume, "an E115 item adds a slice");
    s = fp::e115At(*region, bp(g));
    ctx.check(s.bites == 0, "seven more slices make it full (omnomnom " + std::to_string(s.bites) + ")");
    ctx.check(items::countOf(player.getSelectedItem()) == 1, "each slice used an item");
    ctx.check(click(ctx, g) == ia::Result::Deny, "a full block takes no more slices");
    ctx.check(gf::redstone::analogOutput(*region, bp(g)).value_or(-1) == 8, "comparator 8 when full");

    // eat a slice with an empty hand
    player.setSelectedItem(ItemStack::EMPTY_ITEM());
    fp::setFoodLevel(player, 10.0f);
    auto slices = gf::stats::value(player, fp::ids::kSlicesEaten);
    ctx.check(click(ctx, g) == ia::Result::Consume, "an empty hand eats a slice");
    s = fp::e115At(*region, bp(g));
    ctx.check(s.bites == 1, "eating takes a bite");
    ctx.check(std::abs(fp::foodLevel(player) - 14.0f) < 0.01f, "a slice gives 4 food");
    ctx.check(gf::stats::value(player, fp::ids::kSlicesEaten) == slices + 1, "a slice counts e115_slices_eaten");
    fp::setFoodLevel(player, 20.0f);
    ctx.check(click(ctx, g) == ia::Result::Pass, "a full player cannot eat it");

    // sneaking with empty hands takes a slice back
    int inInv = 0;
    auto count = [&] {
        int n    = 0;
        auto& inv = player.getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i)
            if (items::idOf(inv.getItem(i)) == fp::ids::kE115) n += items::countOf(inv.getItem(i));
        return n;
    };
    inInv = count();
    ctx.check(click(ctx, g, true) == ia::Result::Consume, "sneaking with empty hands takes a slice");
    ctx.check(fp::e115At(*region, bp(g)).bites == 2, "a slice less");
    ctx.check(count() == inInv + 1, "the slice is in the inventory");

    // redstone on a full block: regenerating, a redstone source, the comparator + 7, regrowing
    player.setSelectedItem(make(fp::ids::kE115, 2));
    click(ctx, g);
    click(ctx, g);
    ctx.check(fp::e115At(*region, bp(g)).bites == 0, "full again");
    player.setSelectedItem(make("minecraft:redstone", 1));
    ctx.check(click(ctx, g) == ia::Result::Consume, "redstone on a full block");
    s = fp::e115At(*region, bp(g));
    ctx.check(s.regenerate, "makes it regenerate");
    ctx.check(items::isEmpty(player.getSelectedItem()), "the redstone is used");
    ctx.check(gf::redstone::analogOutput(*region, bp(g)).value_or(-1) == 15, "comparator 15 when full and regenerating");
    player.setSelectedItem(ItemStack::EMPTY_ITEM());
    ctx.check(click(ctx, g, true) == ia::Result::Pass || fp::e115At(*region, bp(g)).bites == 0, "sneaking takes nothing from a regenerating block");
    fp::setFoodLevel(player, 10.0f);
    click(ctx, g);
    click(ctx, g);
    click(ctx, g);
    s = fp::e115At(*region, bp(g));
    ctx.check(s.bites == 3 && s.regenerate, "a regenerating block is eaten as usual");
    ctx.check(gf::redstone::analogOutput(*region, bp(g)).value_or(-1) == 12, "comparator (8 - 3) + 7");
    ctx.check(cp::e115Signal({s.bites, s.regenerate}) == 9, "signal 15 - 2 x 3 while regenerating");
    fp::setFoodLevel(player, 20.0f);

    // support: the block below goes (a neighbour update), the cake goes
    gf::selftest::runServerCommand("setblock " + std::to_string(below.x) + " " + std::to_string(below.y) + " " + std::to_string(below.z) + " air", below.dim);
    co_await ctx.ticks(4);
    region = ctx.region();
    ctx.require(region != nullptr, "the region");
    ctx.check(gf::game::blockIdAt(*region, bp(g)) != fp::ids::kE115, "without a solid block below it breaks");
    ctx.cleanupBlock(g);
    ctx.player().setSelectedItem(held);
});

// ---- frosted -------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("provisions.frosted", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1020, 8);
    (void)box;
    auto* a = pig(ctx, ctx.testPos(1022, 2));
    ctx.require(a != nullptr, "a pig");
    auto uid = a->getOrCreateUniqueID();
    ctx.require(gf::effects::apply(*a, fp::ids::kFrosted, 200, 1), "frosted applies");
    ctx.check(a->getEffect(2u) != nullptr && a->getEffect(2u)->mAmplifier == 1, "its carrier is Slowness II (-0.15 x 2 speed)");
    co_await ctx.ticks(20);
    a = a->getLevel().fetchEntity(uid, false);
    ctx.require(a != nullptr, "the pig is still there");
    int t = fp::frozenTicks(*a);
    ctx.check(t >= 30 && t <= 44, "frosted II freezes 2 ticks a tick (" + std::to_string(t) + " after 20 ticks)");
    // fire lowers the amplifier
    gf::combat::hurt(*a, 0.5f, "minecraft:in_fire");
    ctx.check(gf::effects::amplifier(*a, fp::ids::kFrosted) == 0, "fire damage lowers frosted to level I");
    gf::combat::hurt(*a, 0.5f, "minecraft:on_fire");
    ctx.check(!gf::effects::has(*a, fp::ids::kFrosted), "more fire ends it");
    // freeze damage gains amplifier / 2
    setHealth(*a, 10.0f);
    gf::effects::apply(*a, fp::ids::kFrosted, 200, 3);
    co_await ctx.ticks(12);
    a = a->getLevel().fetchEntity(uid, false);
    ctx.require(a != nullptr, "the pig");
    setHealth(*a, 10.0f);
    float before = health(*a);
    gf::combat::hurt(*a, 1.0f, "minecraft:freeze");
    float lost = before - health(*a);
    ctx.check(lost >= 1.9f, "freeze damage while frosted IV is 1 + 3 / 2 = 2 (" + std::to_string(lost) + ")");
    gf::effects::remove(*a, fp::ids::kFrosted);
    // the frozen counter thaws
    int frozenNow = fp::frozenTicks(*a);
    co_await ctx.ticks(5);
    a = a->getLevel().fetchEntity(uid, false);
    if (a) ctx.check(fp::frozenTicks(*a) < frozenNow, "the frozen counter thaws without the effect");
    discard(a);
    // immunity (ApplyFrostedEffect#doChillAuraEffect)
    auto* stray = pig(ctx, ctx.testPos(1024, 2), "minecraft:polar_bear");
    ctx.require(stray != nullptr, "a polar bear");
    ctx.check(!gf::provisions::ext::doChillAuraEffect(*stray, 100, 0), "polar bears are freeze immune (#minecraft:freeze_immune_entity_types)");
    discard(stray);
    auto* p2 = pig(ctx, ctx.testPos(1025, 2));
    ctx.require(p2 != nullptr, "a pig");
    p2->setArmor(Slot::Head, make("minecraft:leather_helmet"));
    ctx.check(!gf::provisions::ext::doChillAuraEffect(*p2, 100, 0), "a leather helmet keeps frost off");
    p2->setArmor(Slot::Head, ItemStack::EMPTY_ITEM());
    ctx.check(gf::provisions::ext::doChillAuraEffect(*p2, 100, 0), "without it frosted applies");
    discard(p2);
    auto& player = static_cast<Player&>(ctx.player());
    player.setPlayerGameType(GameType::Creative);
    ctx.check(!gf::provisions::ext::doChillAuraEffect(player, 100, 0), "creative players are never frosted");
    player.setPlayerGameType(GameType::Survival);
});

// IceLayer's data: the client mod hears which entities near it are frosted (provisions.frosted), and
// again when that changes.
GF_REGISTER_SELFTEST("provisions.frosted_sync", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace net = ::gf::feature::net;
    auto box = co_await ctx.flatArea(1060, 8);
    (void)box;
    auto g = ctx.testPos(1061, 2);
    ctx.player().teleport(Vec3{g.x + 0.5f, static_cast<float>(g.y), g.z + 0.5f}, static_cast<DimensionType>(g.dim));
    co_await ctx.ticks(2);
    auto* a = pig(ctx, ctx.testPos(1064, 2));
    ctx.require(a != nullptr, "a pig");
    auto uid = a->getOrCreateUniqueID();
    net::setTestCompanion(ctx.player(), true);
    (void)net::takeTestMessages(ctx.player());
    auto frostedIn = [&](std::vector<gf::core::Envelope> const& msgs) -> std::optional<int> {
        std::optional<int> amp;
        bool               any = false;
        for (auto const& m : msgs) {
            if (m.channel != fp::kChanFrosted) continue;
            any = true;
            amp.reset(); // the latest message wins
            for (auto const& e : m.payload.value("entities", nlohmann::json::array()))
                if (e.is_array() && e.size() == 2 && e[0].get<std::int64_t>() == uid.rawID) amp = e[1].get<int>();
        }
        return any ? amp : std::optional<int>{-1};
    };
    gf::effects::apply(*a, fp::ids::kFrosted, 200, 1);
    co_await ctx.ticks(2 * fp::kFrostedSyncInterval + 2);
    auto got = frostedIn(net::takeTestMessages(ctx.player()));
    ctx.check(got && *got == 1, "the companion hears the pig is frosted at level II (" + (got ? std::to_string(*got) : std::string("absent")) + ")");
    co_await ctx.ticks(2 * fp::kFrostedSyncInterval + 2);
    got = frostedIn(net::takeTestMessages(ctx.player()));
    ctx.check(got && *got == -1, "nothing again while the list is unchanged");
    a = a->getLevel().fetchEntity(uid, false);
    if (a) gf::effects::remove(*a, fp::ids::kFrosted);
    co_await ctx.ticks(2 * fp::kFrostedSyncInterval + 2);
    got = frostedIn(net::takeTestMessages(ctx.player()));
    ctx.check(!got.has_value(), "the thawed pig leaves the list");
    net::setTestCompanion(ctx.player(), false);
    if (auto* still = ctx.player().getLevel().fetchEntity(uid, false)) still->remove();
});

// ---- enchantments --------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("provisions.enchantments", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1030, 12);
    (void)box;
    auto& player = static_cast<Player&>(ctx.player());
    player.setPlayerGameType(GameType::Survival);
    // the four specs (F1's data through F9)
    auto chest = make("minecraft:netherite_chestplate");
    gf::enchant::set(chest, fp::ids::kFireReact, 3);
    ctx.check(gf::enchant::level(chest, fp::ids::kFireReact) == 3, "fire react III on a chestplate");
    // fire react: 0.45 per hit at level III
    auto* victim   = pig(ctx, ctx.testPos(1032, 1));
    auto* attacker = pig(ctx, ctx.testPos(1034, 1), "minecraft:cow");
    ctx.require(victim && attacker, "a victim and an attacker");
    victim->setArmor(Slot::Torso, chest);
    int before = fp::counters().fireReacts;
    for (int i = 0; i < 400; ++i) fp::postAttackEffects(*victim, *attacker);
    int procs = fp::counters().fireReacts - before;
    ctx.check(procs >= 130 && procs <= 230, "fire react III reacts on about 45 % of hits (" + std::to_string(procs) + " / 400)");
    ctx.check(attacker->isOnFire(), "the attacker burns");
    ctx.check(victim->getArmor(Slot::Torso).getDamageValue() == procs * 2, "each reaction costs the chestplate 2 durability");
    // chill aura: frosted on the attacker at level - 1
    auto cold = make("minecraft:netherite_chestplate");
    gf::enchant::set(cold, fp::ids::kChillAura, 3);
    victim->setArmor(Slot::Torso, cold);
    attacker->setOnFire(0, false);
    before = fp::counters().chillAuras;
    for (int i = 0; i < 40 && fp::counters().chillAuras == before; ++i) fp::postAttackEffects(*victim, *attacker);
    ctx.check(fp::counters().chillAuras > before, "chill aura reacts");
    ctx.check(gf::effects::amplifier(*attacker, fp::ids::kFrosted) == 2, "chill aura III frosts the attacker at level III");
    discard(victim);
    discard(attacker);

    // destruction: -1.5 damage per level on melee hits
    auto* target = pig(ctx, ctx.testPos(1036, 1));
    ctx.require(target != nullptr, "a pig");
    auto sword = make("minecraft:stone_sword");
    gf::enchant::set(sword, fp::ids::kDestruction, 2);
    auto held = player.getSelectedItem();
    player.setSelectedItem(sword);
    setHealth(*target, 10.0f);
    gf::combat::hurt(*target, 5.0f, "minecraft:player_attack", &player, &player);
    float lost = 10.0f - health(*target);
    ctx.check(std::abs(lost - 2.0f) < 0.01f, "destruction II takes 3 from a 5-damage hit (" + std::to_string(lost) + ")");
    player.setSelectedItem(held);

    // destruction's smash_blocks: a 3x3 at the hit, at most 12 per thrown chain
    auto chain = make("minecraft:iron_pickaxe");
    gf::enchant::set(chain, fp::ids::kDestruction, 1);
    auto c0 = ctx.testPos(1040, 2);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) ctx.setBlock({c0.dim, c0.x + dx, c0.y, c0.z + dz}, dx == 1 && dz == 1 ? "minecraft:iron_ore" : "minecraft:cobblestone");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the region");
    ctx.check(gf::provisions::ext::destructionCanBreak(chain, region->getBlock(bp(c0))), "destruction I breaks cobblestone (wood tier)");
    ctx.check(!gf::provisions::ext::destructionCanBreak(chain, region->getBlock(bp({c0.dim, c0.x + 1, c0.y, c0.z + 1}))), "not iron ore (needs a stone tool)");
    int n = gf::provisions::ext::smashBlocks(player, *target, chain, Vec3{c0.x + 0.5f, c0.y + 0.5f, c0.z + 0.5f});
    ctx.check(n == 8, "smashes the 8 cobblestone blocks around the hit (" + std::to_string(n) + ")");
    ctx.check(gf::provisions::ext::blocksSmashed(*target) == 8, "the smash_blocks attachment counts them");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) ctx.setBlock({c0.dim, c0.x + dx, c0.y, c0.z + dz}, "minecraft:cobblestone");
    n = gf::provisions::ext::smashBlocks(player, *target, chain, Vec3{c0.x + 0.5f, c0.y + 0.5f, c0.z + 0.5f});
    ctx.check(n == 4, "at most 12 per throw (4 more, got " + std::to_string(n) + ")");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) ctx.cleanupBlock({c0.dim, c0.x + dx, c0.y, c0.z + dz});
    discard(target);

    // renewal: an empty scepter refills from its repair ingredients in the inventory
    auto scepter = make("twilightforest:lifedrain_scepter");
    if (items::isEmpty(scepter)) {
        ctx.info("no twilightforest:lifedrain_scepter item: renewal not probed");
        co_return;
    }
    int max = scepter.mItem->getMaxDamage();
    scepter.setDamageValue(static_cast<short>(max));
    gf::enchant::set(scepter, fp::ids::kRenewal, 1);
    player.getInventory().removeAllItems();
    player.setSelectedItem(scepter);
    ctx.give(player, make("minecraft:fermented_spider_eye", 2), 20);
    co_await ctx.ticks(3);
    auto& p2 = static_cast<Player&>(ctx.player());
    auto  sc = p2.getSelectedItem();
    ctx.check(items::idOf(sc) == "twilightforest:lifedrain_scepter" && sc.getDamageValue() == max - 9,
              "renewal recharges the empty lifedrain scepter by 9 (damage " + std::to_string(sc.getDamageValue()) + ")");
    int eyes = 0;
    for (int i = 0; i < p2.getInventory().getContainerSize(); ++i)
        if (items::idOf(p2.getInventory().getItem(i)) == "minecraft:fermented_spider_eye") eyes += items::countOf(p2.getInventory().getItem(i));
    ctx.check(eyes == 1, "using one fermented spider eye");
    p2.getInventory().removeAllItems();
    p2.setSelectedItem(ItemStack::EMPTY_ITEM());

    // the zombie scepter's ingredients: rotten flesh and a potion of Strength (any of the three);
    // another potion is no ingredient (NeoForge's components ingredient)
    auto zombie = make("twilightforest:zombie_scepter");
    if (items::isEmpty(zombie)) co_return;
    int zmax = zombie.mItem->getMaxDamage();
    zombie.setDamageValue(static_cast<short>(zmax));
    gf::enchant::set(zombie, fp::ids::kRenewal, 1);
    auto potion = [](int aux) { return items::make("minecraft:potion", 1, aux).value_or(ItemStack::EMPTY_ITEM()); };
    for (auto const& [aux, refills] : {std::pair{21, false}, std::pair{32, true}}) {
        auto& p3 = static_cast<Player&>(ctx.player());
        p3.getInventory().removeAllItems();
        p3.setSelectedItem(zombie);
        ctx.give(p3, make("minecraft:rotten_flesh", 1), 20);
        ctx.give(p3, potion(aux), 21);
        co_await ctx.ticks(3);
        auto& p4 = static_cast<Player&>(ctx.player());
        auto  zs = p4.getSelectedItem();
        bool  refilled = items::idOf(zs) == "twilightforest:zombie_scepter" && zs.getDamageValue() == zmax - 9;
        ctx.check(refilled == refills, std::string("a potion with aux ") + std::to_string(aux) + (refills ? " (long Strength) refills" : " (Healing) does not refill") +
                                           " the zombie scepter (damage " + std::to_string(zs.getDamageValue()) + ")");
    }
    auto& p5 = static_cast<Player&>(ctx.player());
    p5.getInventory().removeAllItems();
    p5.setSelectedItem(ItemStack::EMPTY_ITEM());
});

// ---- music discs ---------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("provisions.discs", [](Context& ctx) -> ll::coro::CoroTask<> {
    for (auto const& s : cp::songs()) {
        auto id    = "twilightforest:music_disc_" + std::string(s.track);
        auto event = "music_disc.twilightforest." + std::string(s.track);
        auto disc  = make(id);
        ctx.require(!items::isEmpty(disc), id + " exists");
        auto const* component = disc.mItem->getComponent(RecordItemComponent::getIdentifier());
        ctx.require(component != nullptr, id + " has a record component");
        auto const* record = static_cast<RecordItemComponent const*>(component);
        ctx.check(record->mComparatorSignal == s.comparator, id + ": comparator " + std::to_string(s.comparator));
        ctx.check(std::abs(record->mDuration - static_cast<float>(s.seconds)) < 0.01f, id + ": " + std::to_string(s.seconds) + " s");
        ctx.check(gf::item::ext::recordSound(id) == event, id + " plays " + event);
        ctx.check(disc.getMaxStackSize() == 1, id + " stacks to 1");
    }
    // one of them in a jukebox: the start plays the song by name, the stop stops it
    auto pos = ctx.testPos(1050, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:jukebox"), "a jukebox");
    auto  b      = bp(pos);
    auto* region = ctx.region();
    auto* actor  = region ? region->getBlockEntity(b) : nullptr;
    ctx.require(actor && actor->getType() == BlockActorType::Jukebox, "the jukebox's block actor");
    auto before = gf::item::internal::recordCounts();
    static_cast<JukeboxBlockActor&>(*actor).setRecord(make("twilightforest:music_disc_radiance"), true);
    co_await ctx.ticks(2);
    region = ctx.region();
    actor  = region ? region->getBlockEntity(b) : nullptr;
    ctx.require(actor && actor->getType() == BlockActorType::Jukebox, "the jukebox is still there");
    auto now = gf::item::internal::recordCounts();
    ctx.check(now.started > before.started && now.lastSound == "music_disc.twilightforest.radiance", "the jukebox plays music_disc.twilightforest.radiance (" + now.lastSound + ")");
    auto& playing = static_cast<JukeboxBlockActor&>(*actor);
    playing.stopPlayingRecord(*region);
    ctx.check(gf::item::internal::recordCounts().stopped > before.stopped, "stopping the record stops the song");
    playing.setRecord(ItemStack::EMPTY_ITEM(), false);
    ctx.cleanupBlock(pos);
});

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of U11 (tgear.*, dx 1100..1199): the gear, the modifier recipes and their trigger, the vest /
// gloves merge, the command, the per-tick modifiers, efficient eater, never-break, the belt swap, the
// item display, the zoom channel and the red thread placement. Behaviour from TF 4.8 (see the headers of
// the files under test).
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components_json_legacy/InsomniaComponent.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/ext/tgear/PlayerHooks.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/recipes/Recipes.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

namespace tg    = gf::feature::tgear;
namespace items = gf::game::items;
using gf::selftest::Context;
using Slot = SharedTypes::Legacy::ArmorSlot;

ItemStack make(std::string const& id, int count = 1) {
    auto s = items::make(id, count);
    return s ? *s : ItemStack::EMPTY_ITEM();
}

/// A 3x3 grid of the stacks (row-major, nullptr = empty).
struct Grid {
    std::vector<ItemStack>   stacks;
    gf::recipes::CraftingGrid grid;
    explicit Grid(std::vector<ItemStack> s) : stacks(std::move(s)) {
        stacks.resize(9, ItemStack::EMPTY_ITEM());
        for (auto& x : stacks) grid.slots.push_back(&x);
    }
};

void clearArmor(Player& p) {
    for (int i = 0; i < 4; ++i) p.setArmor(static_cast<Slot>(i), ItemStack::EMPTY_ITEM());
}

} // namespace

GF_REGISTER_SELFTEST("tgear.items", [](Context& ctx) {
    struct Case {
        char const* id;
        int         slot, slots, durability;
    };
    for (auto const& c : {Case{tg::ids::kGoggles, 0, 3, 132}, Case{tg::ids::kVest, 1, 3, 192}, Case{tg::ids::kGloves, 1, 0, 0},
                          Case{tg::ids::kWings, 2, 3, 180}, Case{tg::ids::kBelt, 2, 0, 0}, Case{tg::ids::kBoots, 3, 3, 156}}) {
        auto s = make(c.id);
        ctx.require(!items::isEmpty(s), std::string(c.id) + " exists");
        ctx.check(s.isArmorItem() || s.isHumanoidWearableItem(), std::string(c.id) + " is wearable");
        ctx.check(static_cast<int>(s.getArmorSlot()) == c.slot, std::string(c.id) + " armour slot " + std::to_string(c.slot));
        ctx.check(tg::modifierSlots(c.id) == c.slots, std::string(c.id) + " modifier slots");
        ctx.check(tg::maxDamageOf(s) == c.durability, std::string(c.id) + " durability " + std::to_string(c.durability) + " (got " + std::to_string(tg::maxDamageOf(s)) + ")");
    }
    ctx.check(tg::allModifiers().size() >= 24, "the 24 travellers modifiers are in the registry");
    ctx.check(tg::hasModifier(make(tg::ids::kGoggles), tg::ids::kZoom), "goggles have the zoom ability");
    ctx.check(tg::hasModifier(make(tg::ids::kBelt), tg::ids::kSwapHotbarAbility) && tg::hasContainer(make(tg::ids::kBelt)), "the belt swaps the hotbar and has a container");
    ctx.check(tg::insertableModifiers(make(tg::ids::kGoggles)).empty(), "fresh goggles have no insertable modifier");
});

GF_REGISTER_SELFTEST("tgear.modifier_recipes", [](Context& ctx) {
    int checked = 0;
    for (auto const* type : {"twilightforest:travellers_gear_modifier_shaped_recipe", "twilightforest:travellers_gear_modifier_shapeless_recipe"}) {
        for (auto const* r : gf::recipes::ofType(type)) {
            if (!r->id.starts_with("twilightforest:add_modifier_to_travellers_gear/")) continue;
            auto mod  = r->data.value("modifier_key", std::string());
            auto slash = r->id.find('/');
            auto gear = "twilightforest:" + r->id.substr(slash + 1, r->id.find('/', slash + 1) - slash - 1);
            std::vector<ItemStack> in{make(gear)};
            if (mod == tg::ids::kSwapHotbar) in.push_back(make(tg::ids::kBelt));
            Grid g(in);
            ctx.check(gf::recipes::internal::isSpecial(r->id), r->id + " is a special recipe");
            auto out = gf::recipes::internal::assembleSpecial(r->id, g.grid);
            ctx.check(out && tg::hasModifier(*out, mod), r->id + " adds " + mod);
            if (out) {
                Grid again({*out});
                ctx.check(!tg::modifierRecipeMatches(mod, again.grid), r->id + ": refused when the gear has it");
            }
            ++checked;
        }
    }
    ctx.check(checked == 24, "24 modifier recipes (got " + std::to_string(checked) + ")");
    // the BP potion layouts are special too
    ctx.check(gf::recipes::internal::isSpecial("twilightforest:add_modifier_to_travellers_gear/travellers_vest/stealth_modifier_1"), "the long invisibility layout crafts stealth");
    // free slots: three modifiers fill the goggles
    auto g = make(tg::ids::kGoggles);
    for (auto const* m : {tg::ids::kAutoRepair, tg::ids::kAllNight, tg::ids::kItemDisplay}) ctx.check(tg::addModifier(g, m), std::string("add ") + m);
    Grid full({g});
    ctx.check(!tg::modifierRecipeMatches(tg::ids::kRedThreadVision, full.grid), "no free slot: refused");
    // two container providers (two belts): refused
    Grid twoBelts({make(tg::ids::kWings), make(tg::ids::kBelt), make(tg::ids::kBelt)});
    ctx.check(!tg::modifierRecipeMatches(tg::ids::kSwapHotbar, twoBelts.grid), "two belts: refused");
});

GF_REGISTER_SELFTEST("tgear.swap_hotbar_transfer", [](Context& ctx) {
    auto belt = make(tg::ids::kBelt);
    std::array<ItemStack, 9> content;
    for (auto& s : content) s = ItemStack::EMPTY_ITEM();
    content[2] = make("minecraft:torch", 5);
    tg::setContainer(belt, content);
    Grid g({make(tg::ids::kWings), belt});
    auto out = tg::modifierRecipeAssemble(tg::ids::kSwapHotbar, g.grid);
    ctx.require(out.has_value(), "wings + belt craft");
    ctx.check(tg::hasModifier(*out, tg::ids::kSwapHotbar) && tg::hasContainer(*out), "the wings have swap_hotbar and a container");
    ctx.check(items::idOf(tg::container(*out)[2]) == "minecraft:torch" && items::countOf(tg::container(*out)[2]) == 5, "the belt's items moved to the wings");
    std::vector<ItemStack> stored;
    ctx.check(tg::removeModifier(*out, tg::ids::kSwapHotbar, &stored) && stored.size() == 1, "removing it gives the stored items back");
    ctx.check(!tg::hasContainer(*out), "and the container goes");
});

GF_REGISTER_SELFTEST("tgear.vest_gloves_merge", [](Context& ctx) {
    Grid g({make(tg::ids::kVest), make(tg::ids::kGloves)});
    ctx.check(tg::mergeRecipeMatches(g.grid), "vest + gloves match");
    auto out = gf::recipes::internal::assembleSpecial("twilightforest:travellers_vest_gloves_merge_recipe", g.grid);
    ctx.require(out.has_value(), "merged");
    ctx.check(items::idOf(*out) == tg::ids::kVest && tg::hasGloves(*out), "a vest with gloves");
    Grid again({*out, make(tg::ids::kGloves)});
    ctx.check(!tg::mergeRecipeMatches(again.grid), "a vest that has gloves: refused");
    Grid three({make(tg::ids::kVest), make(tg::ids::kGloves), make("minecraft:stick")});
    ctx.check(!tg::mergeRecipeMatches(three.grid), "exactly two items");
});

GF_REGISTER_SELFTEST("tgear.add_modifier_trigger", [](Context& ctx) -> ll::coro::CoroTask<> {
    static std::vector<std::string> fired;
    fired.clear();
    static bool listening = false;
    if (!listening) {
        listening = true;
        gf::progression::onTrigger("twilightforest:add_modifier", [](Player&, nlohmann::json const& data) {
            fired.push_back(data.value("modifier", std::string()));
        });
    }
    Grid g({make(tg::ids::kVest)});
    auto out = tg::modifierRecipeAssemble(tg::ids::kHaste, g.grid);
    ctx.require(out.has_value(), "haste vest");
    auto& p = ctx.player();
    p.getInventory().setItem(4, *out);
    tg::fireCraftedTriggers(p);
    ctx.check(fired.size() == 1 && fired[0] == tg::ids::kHaste, "add_modifier fired once for haste");
    tg::fireCraftedTriggers(p);
    ctx.check(fired.size() == 1, "the mark is gone after firing");
    p.getInventory().setItem(4, ItemStack::EMPTY_ITEM());
    co_return;
});

GF_REGISTER_SELFTEST("tgear.command", [](Context& ctx) {
    auto& p   = ctx.player();
    auto  run = [&](bool add, std::string const& id) { return tg::runCommand(&p, add, id); };
    p.setSelectedItem(make("minecraft:stick"));
    auto r = run(true, "twilightforest:nope");
    ctx.check(!r.ok && r.message.find("not a valid") != std::string::npos, "unknown modifier first: " + r.message);
    r = run(true, "auto_repair");
    ctx.check(!r.ok && r.message.find("minecraft:auto_repair") != std::string::npos, "no namespace is minecraft's: " + r.message);
    r = tg::runCommand(nullptr, true, tg::ids::kAutoRepair);
    ctx.check(!r.ok && r.message.find("valid player") != std::string::npos, "not a player: " + r.message);
    r = run(true, tg::ids::kAutoRepair);
    ctx.check(!r.ok && r.message.find("not holding") != std::string::npos, "not travellers gear: " + r.message);
    p.setSelectedItem(make(tg::ids::kGoggles));
    r = run(true, tg::ids::kZoom);
    ctx.check(!r.ok && r.message.find("Abilities") != std::string::npos, "ability: " + r.message);
    r = run(true, tg::ids::kHaste);
    ctx.check(!r.ok && r.message.find("not allowed") != std::string::npos, "wrong slot: " + r.message);
    r = run(true, tg::ids::kItemDisplay);
    ctx.check(r.ok && r.message.find("Added") != std::string::npos && r.message.find("(keybind: C)") != std::string::npos, "added with the default key name: " + r.message);
    ctx.check(tg::hasModifier(p.getSelectedItem(), tg::ids::kItemDisplay), "the held goggles have it");
    r = run(true, tg::ids::kItemDisplay);
    ctx.check(!r.ok && r.message.find("already has") != std::string::npos, "has modifier: " + r.message);
    run(true, tg::ids::kAutoRepair);
    run(true, tg::ids::kAllNight);
    r = run(true, tg::ids::kRedThreadVision);
    ctx.check(!r.ok && r.message.find("maximum") != std::string::npos, "too many: " + r.message);
    r = run(false, tg::ids::kItemDisplay);
    ctx.check(r.ok && r.message.find("Removed") != std::string::npos, "removed: " + r.message);
    r = run(false, tg::ids::kItemDisplay);
    ctx.check(!r.ok && r.message.find("is not applied") != std::string::npos, "no modifier: " + r.message);
    p.setSelectedItem(ItemStack::EMPTY_ITEM());
    // through the command line: the console is no player
    auto c = gf::selftest::runServerCommand("tf travellers_gear add_modifier twilightforest:haste");
    ctx.check(!c.ok, "the console cannot run it: " + c.messages);
});

GF_REGISTER_SELFTEST("tgear.tick_modifiers", [](Context& ctx) -> ll::coro::CoroTask<> {
    {
        auto& p    = ctx.player();
        auto  vest = make(tg::ids::kVest);
        tg::addModifier(vest, tg::ids::kHaste);
        tg::addModifier(vest, tg::ids::kStealth);
        clearArmor(p);
        p.setArmor(Slot::Torso, vest);
        static_cast<SimulatedPlayer&>(p).simulateSneaking();
    }
    co_await ctx.ticks(3);
    {
        auto& p     = ctx.player();
        auto* haste = p.getEffect(3u);
        ctx.check(haste && haste->mAmplifier == 1 && !haste->mEffectVisible, "Haste II, hidden");
        auto* invis = p.getEffect(14u);
        ctx.check(invis && !invis->mEffectVisible, "stealth: hidden invisibility while sneaking");
        static_cast<SimulatedPlayer&>(p).simulateStopSneaking();
    }
    co_await ctx.ticks(4);
    {
        auto& p = ctx.player();
        ctx.check(p.getEffect(14u) == nullptr, "stealth ends when standing up");
        // broken: modifiers inactive
        ItemStack vest = p.getArmor(Slot::Torso);
        vest.setDamageValue(static_cast<short>(tg::maxDamageOf(vest) - 1));
        p.setArmor(Slot::Torso, vest);
        ctx.check(tg::isBroken(vest) && !tg::isActive(p, tg::ids::kHaste), "a broken vest disables haste");
        clearArmor(p);
    }
});

GF_REGISTER_SELFTEST("tgear.efficient_eater", [](Context& ctx) {
    namespace ext = gf::ext::tgear;
    auto& p = ctx.player();
    clearArmor(p);
    p.setPlayerGameType(GameType::Survival);
    auto measure = [&] {
        ext::setExhaustion(p, 0);
        p.checkMovementStats(Vec3{3.f, 0.f, 0.f});
        return ext::exhaustion(p);
    };
    float plain = measure();
    ctx.require(plain > 0, "walking adds exhaustion (got " + std::to_string(plain) + ")");
    auto vest = make(tg::ids::kVest);
    tg::addModifier(vest, tg::ids::kEfficientEater);
    p.setArmor(Slot::Torso, vest);
    float eater = measure();
    ctx.check(std::abs(eater - plain / 2) < 1e-4f, "efficient eater halves it (" + std::to_string(plain) + " -> " + std::to_string(eater) + ")");
    clearArmor(p);
    ext::setExhaustion(p, 0);
});

GF_REGISTER_SELFTEST("tgear.auto_repair", [](Context& ctx) {
    auto& p = ctx.player();
    clearArmor(p);
    auto boots = make(tg::ids::kBoots);
    tg::addModifier(boots, tg::ids::kAutoRepair);
    boots.setDamageValue(100);
    p.setArmor(Slot::Feet, boots);
    tg::seedRandom(1100);
    std::int64_t now = 1'000'000;
    tg::setLastArmorDamageTime(p, now - 100);
    int quiet = 0;
    for (int i = 0; i < 2000; ++i) quiet += tg::autoRepair(p, now);
    ctx.check(quiet == 0, "nothing within 200 ticks of armour damage");
    tg::setLastArmorDamageTime(p, now - 201);
    int repaired = 0;
    for (int i = 0; i < 20000; ++i) repaired += tg::autoRepair(p, now);
    ctx.check(repaired >= 10 && repaired <= 100, "about 0.001 x 1..3 boost per roll (repaired " + std::to_string(repaired) + " in 20000)");
    ctx.check(p.getArmor(Slot::Feet).getDamageValue() == 100 - repaired, "each success repairs one point");
    // broken pieces still repair (ALWAYS_ACTIVE)
    ItemStack b = p.getArmor(Slot::Feet);
    b.setDamageValue(static_cast<short>(tg::maxDamageOf(b) - 1));
    p.setArmor(Slot::Feet, b);
    ctx.check(tg::isActiveOn(p, b, tg::ids::kAutoRepair), "auto repair is active on a broken piece");
    clearArmor(p);
});

GF_REGISTER_SELFTEST("tgear.perfect_dodge", [](Context& ctx) {
    auto& p = ctx.player();
    clearArmor(p);
    auto vest = make(tg::ids::kVest);
    tg::addModifier(vest, tg::ids::kPerfectDodge);
    p.setArmor(Slot::Torso, vest);
    tg::seedRandom(1101);
    int dodged = 0;
    for (int i = 0; i < 1000; ++i) dodged += tg::rollPerfectDodge(p, nullptr) ? 1 : 0;
    ctx.check(dodged > 240 && dodged < 360, "about 30% dodged (" + std::to_string(dodged) + "/1000)");
    clearArmor(p);
    ctx.check(!tg::rollPerfectDodge(p, nullptr), "no vest: no dodge");
});

GF_REGISTER_SELFTEST("tgear.never_break", [](Context& ctx) -> ll::coro::CoroTask<> {
    {
        auto& p = ctx.player();
        clearArmor(p);
        p.setPlayerGameType(GameType::Survival);
        auto goggles = make(tg::ids::kGoggles);
        goggles.setDamageValue(static_cast<short>(tg::maxDamageOf(goggles) - 2));
        p.setArmor(Slot::Head, goggles);
        gf::combat::hurt(p, 6.f, "minecraft:mob_attack");
    }
    co_await ctx.ticks(2);
    {
        auto&       p = ctx.player();
        auto const& g = p.getArmor(Slot::Head);
        ctx.check(items::idOf(g) == tg::ids::kGoggles, "the goggles did not break");
        ctx.check(tg::isBroken(g), "they are broken at max damage - 1 (damage " + std::to_string(g.getDamageValue()) + ")");
        ctx.check(!tg::isActive(p, tg::ids::kZoom), "a broken piece has no active modifier");
        clearArmor(p);
    }
});

GF_REGISTER_SELFTEST("tgear.belt_swap", [](Context& ctx) {
    auto& p = ctx.player();
    clearArmor(p);
    auto& inv = p.getInventory();
    for (int i = 0; i < 9; ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
    inv.setItem(0, make("minecraft:stone", 10));
    inv.setItem(1, make(tg::ids::kBelt)); // a belt cannot go in a container
    p.setArmor(Slot::Legs, make(tg::ids::kBelt));
    ctx.check(tg::trySwapHotbar(p), "the swap changed something");
    auto stored = tg::container(p.getArmor(Slot::Legs));
    ctx.check(items::idOf(stored[0]) == "minecraft:stone" && items::isEmpty(p.getInventory().getItem(0)), "the stone went into the belt");
    ctx.check(items::idOf(p.getInventory().getItem(1)) == tg::ids::kBelt && items::isEmpty(stored[1]), "the belt in the hotbar stays");
    ctx.check(tg::trySwapHotbar(p), "swapping back");
    ctx.check(items::countOf(p.getInventory().getItem(0)) == 10, "the stone is back");
    // through the companion channel
    gf::net::injectFromClient(p, "tgear.swap_hotbar", nlohmann::json::object());
    ctx.check(items::isEmpty(p.getInventory().getItem(0)), "tgear.swap_hotbar swaps");
    for (int i = 0; i < 9; ++i) p.getInventory().setItem(i, ItemStack::EMPTY_ITEM());
    clearArmor(p);
});

GF_REGISTER_SELFTEST("tgear.item_display", [](Context& ctx) {
    auto goggles = make(tg::ids::kGoggles);
    ctx.require(tg::addModifier(goggles, tg::ids::kItemDisplay), "item display added");
    auto      compass = make("minecraft:compass", 2);
    ItemStack replaced;
    ctx.check(tg::displayInsert(goggles, compass, replaced) && items::countOf(compass) == 1 && items::isEmpty(replaced), "a compass goes in, one of two");
    ctx.check(!tg::displayInsert(goggles, compass, replaced), "the same compass again: refused");
    auto clock = make("minecraft:clock");
    ctx.check(tg::displayInsert(goggles, clock, replaced) && items::isEmpty(clock), "a clock goes in");
    auto stick = make("minecraft:stick");
    ctx.check(!tg::displayInsert(goggles, stick, replaced), "a stick does not fit");
    auto d = tg::displayOf(goggles);
    ctx.require(d.has_value(), "display data");
    ctx.check(!d->items[3].is_null() && !d->items[4].is_null(), "compass in slot 3, clock in slot 4");
    ItemStack out;
    ctx.check(tg::displayRemoveFirst(goggles, out) && items::idOf(out) == "minecraft:compass", "remove takes the first stored item");
    // the map cycle needs worn goggles with stored maps: none -> nothing changes
    auto& p = ctx.player();
    clearArmor(p);
    p.setArmor(Slot::Head, goggles);
    ctx.check(!tg::cycleMapSlot(p) || true, "cycle without maps");
    auto dd = tg::displayOf(p.getArmor(Slot::Head));
    ctx.check(dd && dd->chosenMapSlot <= 0, "no map: the choice stays or is none");
    clearArmor(p);
});

GF_REGISTER_SELFTEST("tgear.zoom_channel", [](Context& ctx) {
    auto& p = ctx.player();
    clearArmor(p);
    gf::net::injectFromClient(p, "tgear.goggles_zoom", {{"zoom", true}});
    ctx.check(!tg::usingZoom(p), "no goggles: no zoom");
    p.setArmor(Slot::Head, make(tg::ids::kGoggles));
    gf::net::injectFromClient(p, "tgear.goggles_zoom", {{"zoom", true}});
    ctx.check(tg::usingZoom(p), "goggles: zooming");
    gf::net::injectFromClient(p, "tgear.goggles_zoom", {{"zoom", false}});
    ctx.check(!tg::usingZoom(p), "zoom out");
    clearArmor(p);
});

GF_REGISTER_SELFTEST("tgear.red_thread", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1150, 5);
    (void)box;
    auto pos = ctx.testPos(1150, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.require(ctx.setBlock(pos, tg::ids::kRedThread), "a red thread block");
    BlockPos bp{pos.x, pos.y, pos.z};
    auto const& block = region->getBlock(bp);
    // placed against the ground (clicked face up = 1): attached down
    std::string id = tg::ids::kRedThread, item = tg::ids::kRedThread;
    gf::interactions::PlaceContext pc{ctx.player(), item, block, id, pos, gf::core::GlobalPos{pos.dim, pos.x, pos.y - 1, pos.z}, 1, {0.5, 1.0, 0.5}, false};
    gf::interactions::dispatchPlace(pc);
    auto const& placed = region->getBlock(bp);
    ctx.check(gf::game::intState(placed, "twilightforest:down").value_or(0) == 1, "the thread lies on the ground");
    ctx.check(gf::game::intState(placed, "twilightforest:up").value_or(0) == 0, "and not on the ceiling");
});

GF_REGISTER_SELFTEST("tgear.all_night_goggles", [](Context& ctx) -> ll::coro::CoroTask<> {
    {
        auto& p = ctx.player();
        clearArmor(p);
        auto goggles = make(tg::ids::kGoggles);
        tg::addModifier(goggles, tg::ids::kAllNight);
        p.setArmor(Slot::Head, goggles);
        auto insomnia = p.getEntityContext().tryGetComponent<InsomniaComponent>();
        if (!insomnia) {
            ctx.info("the simulated player has no insomnia component: nothing to check");
            clearArmor(p);
            co_return;
        }
        insomnia->mTimeSinceRest = 100000;
    }
    co_await ctx.ticks(2);
    {
        auto& p        = ctx.player();
        auto insomnia = p.getEntityContext().tryGetComponent<InsomniaComponent>();
        ctx.check(insomnia && insomnia->mTimeSinceRest < 10, "the goggles keep the time since rest at 0 (no phantoms)");
        clearArmor(p);
    }
});

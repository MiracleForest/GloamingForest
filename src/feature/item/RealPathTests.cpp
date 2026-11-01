// Real-path selftests of F9 (prefix "item.real_", testPos dx 5940..5949): the client e2e crafts and
// combines through the game's own screens, between an "arm" test that registers the fixture
// behaviour and gives the real players the inputs, and a "check" test that reads what the craft
// put into their inventories and takes everything back. On a server without real players (the
// 'full' run) arm gives nothing and check only unregisters.
//
// - Special crafting: the test BP recipe gf_test:fixture_item (two sticks in a column, assets/test)
//   is registered as a special recipe: the matcher wants exactly two sticks, the assembler makes two
//   fixture items carrying item data ("special"), the remainder is a stick in the first stick's slot
//   (getRemainingItems). The client puts the sticks into a crafting table and takes the result: the
//   request runs CraftHandlerCrafting -> _initCraftResults, Bedrock's own recipe check included.
// - Anvil: an iron sword and an enchanted book, both with the fixture enchantment at level I, are
//   combined in a real anvil: the sword must come out at level II.

#include <algorithm>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/enchant/Enchant.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/item/Item.h"
#include "feature/recipes/Recipes.h"
#include "feature/recipes/RecipesInternal.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace items = gf::game::items;
namespace rc    = gf::recipes;

constexpr char kFixture[] = "gf_test:fixture_item"; ///< the item and the BP recipe id

/// Every real (non-simulated) player.
template <class Fn>
int forRealPlayers(Fn&& fn) {
    int  n     = 0;
    auto level = ll::service::getLevel();
    if (!level) return 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        fn(p);
        ++n;
        return true;
    });
    return n;
}

int countOf(Player& p, std::string const& id, bool (*pred)(ItemStack const&) = nullptr) {
    int   n   = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (items::idOf(s) == id && (!pred || pred(s))) n += items::countOf(s);
    }
    auto& ui = p.mPlayerUIContainer.get();
    for (int i = static_cast<int>(PlayerUISlot::Crafting2x2Input1); i <= static_cast<int>(PlayerUISlot::Crafting3x3Input9); ++i) {
        auto const& s = ui.getItem(i);
        if (items::idOf(s) == id && (!pred || pred(s))) n += items::countOf(s);
    }
    return n;
}

void clearOf(Player& p, std::string const& id) {
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (items::idOf(inv.getItem(i)) == id) inv.setItem(i, ItemStack::EMPTY_ITEM());
    p.sendInventory(false);
}

} // namespace

GF_REGISTER_SELFTEST("item.real_craft_arm", [](Context& ctx) {
    rc::registerSpecialCrafting(
        kFixture,
        [](rc::CraftingGrid const& g) {
            int sticks = 0, other = 0;
            for (auto const* s : g.slots) {
                if (!s || items::isEmpty(*s)) continue;
                (items::idOf(*s) == "minecraft:stick" ? sticks : other)++;
            }
            return sticks == 2 && other == 0;
        },
        [](rc::CraftingGrid const&) {
            auto out = items::make(kFixture, 2).value_or(ItemStack::EMPTY_ITEM());
            gf::item::setData(out, "special", true);
            return out;
        },
        [](rc::CraftingGrid const& g) {
            std::vector<ItemStack> rem(g.slots.size(), ItemStack::EMPTY_ITEM());
            for (std::size_t i = 0; i < g.slots.size(); ++i)
                if (g.slots[i] && items::idOf(*g.slots[i]) == "minecraft:stick") {
                    rem[i] = items::make("minecraft:stick").value_or(ItemStack::EMPTY_ITEM());
                    break;
                }
            return rem;
        }
    );
    int n = forRealPlayers([](Player& p) {
        clearOf(p, "minecraft:stick");
        clearOf(p, kFixture);
        auto& inv = p.getInventory();
        // one stick in each of the first two hotbar slots, the third free for the result
        inv.setItem(0, items::make("minecraft:stick").value_or(ItemStack::EMPTY_ITEM()));
        inv.setItem(1, items::make("minecraft:stick").value_or(ItemStack::EMPTY_ITEM()));
        inv.setItem(2, ItemStack::EMPTY_ITEM());
        p.sendInventory(false);
        gf::log().info("item: real craft armed for {}", p.getRealName());
    });
    ctx.info("real players armed for the crafting table: " + std::to_string(n));
});

GF_REGISTER_SELFTEST("item.real_craft_check", [](Context& ctx) {
    int n = forRealPlayers([&](Player& p) {
        int special = countOf(p, kFixture, [](ItemStack const& s) { return gf::item::getData(s, "special") == true; });
        int sticks  = countOf(p, "minecraft:stick");
        ctx.check(special == 2, p.getRealName() + " crafted two fixture items with the assembler's item data (got " + std::to_string(special) + ")");
        ctx.check(sticks == 1, p.getRealName() + " kept the remainder stick (got " + std::to_string(sticks) + ")");
        gf::log().info("item: real craft checked for {}: {} special items, {} remainder", p.getRealName(), special, sticks);
        clearOf(p, kFixture);
        clearOf(p, "minecraft:stick");
    });
    rc::internal::unregisterSpecial(kFixture);
    ctx.info("real players checked: " + std::to_string(n));
});

GF_REGISTER_SELFTEST("item.real_anvil_arm", [](Context& ctx) {
    gf::enchant::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .minCost = {5, 9}, .nameKey = "enchantment.gf_test.fixture_react"});
    int n = forRealPlayers([](Player& p) {
        auto& inv   = p.getInventory();
        auto  sword = items::make("minecraft:iron_sword");
        auto  book  = items::make("minecraft:enchanted_book");
        if (!sword || !book) return;
        gf::enchant::set(*sword, "gf_test:fixture_react", 1);
        gf::enchant::set(*book, "gf_test:fixture_react", 1);
        inv.setItem(0, *sword);
        inv.setItem(1, *book);
        inv.setItem(2, ItemStack::EMPTY_ITEM());
        p.addLevels(30);
        p.sendInventory(false);
        gf::log().info("item: real anvil armed for {}", p.getRealName());
    });
    ctx.info("real players armed for the anvil: " + std::to_string(n));
});

GF_REGISTER_SELFTEST("item.real_anvil_check", [](Context& ctx) {
    int n = forRealPlayers([&](Player& p) {
        int   best = 0;
        auto& inv  = p.getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i)
            if (items::idOf(inv.getItem(i)) == "minecraft:iron_sword") best = std::max(best, gf::enchant::level(inv.getItem(i), "gf_test:fixture_react"));
        ctx.check(best == 2, p.getRealName() + "'s sword came out of the anvil with the custom enchantment at II (got " + std::to_string(best) + ")");
        gf::log().info("item: real anvil checked for {}: fixture_react {}", p.getRealName(), best);
        clearOf(p, "minecraft:iron_sword");
        clearOf(p, "minecraft:enchanted_book");
    });
    gf::enchant::internal::unregisterType("gf_test:fixture_react");
    ctx.info("real players checked: " + std::to_string(n));
});

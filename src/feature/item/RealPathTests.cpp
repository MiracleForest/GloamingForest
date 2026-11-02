// Real-path selftests of F9 (prefix "item.real_", testPos dx 5940..5949): the client e2e crafts and
// combines through the game's own screens, between an "arm" test that registers the fixture
// behaviour and gives the real players the inputs, and a "check" test that reads what the craft
// put into their inventories and takes everything back. On a server without real players (the
// 'full' run) arm gives nothing and check only unregisters.
//
// - Special crafting: the test BP recipe gf_test:fixture_item (two sticks in a column, assets/test)
//   is registered as a special recipe: the matcher wants exactly two sticks, the assembler makes two
//   fixture items carrying item data ("special"), the remainder is a stick in the first stick's slot
//   (getRemainingItems). A data recipe of the same id and of a code-only TF type
//   (twilightforest:casket_repair_recipe, as TF's special recipes have) shadows the BP recipe in the
//   data index meanwhile, as TF's data does for its special recipes. The client puts the sticks into
//   a crafting table and takes the result: the request runs CraftHandlerCrafting ->
//   _initCraftResults, Bedrock's own recipe check included.
// - Anvil: an iron sword and an enchanted book, both with the fixture enchantment at level I, are
//   combined in a real anvil: the sword must come out at level II.

#include <algorithm>
#include <map>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
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

/// The special craft count of each player when item.real_craft_arm ran.
std::map<std::int64_t, int>& armedAt() {
    static std::map<std::int64_t, int> m;
    return m;
}

/// The experience level of a player.
int levelsOf(Player const& p) {
    auto        ref  = p.getAttribute(Player::LEVEL());
    auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr);
    return inst ? static_cast<int>(inst->mCurrentValue) : 0;
}

/// Each real player's level before item.real_anvil_arm (restored by item.real_anvil_check).
std::map<std::int64_t, int>& levelsBefore() {
    static std::map<std::int64_t, int> m;
    return m;
}

void clearOf(Player& p, std::string const& id) {
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (items::idOf(inv.getItem(i)) == id) inv.setItem(i, ItemStack::EMPTY_ITEM());
    p.sendInventory(false);
}

} // namespace

GF_REGISTER_SELFTEST("item.real_craft_arm", [](Context& ctx) {
    rc::internal::setTestDataRecipe(kFixture, {{"type", "twilightforest:casket_repair_recipe"}, {"category", "misc"}});
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
        armedAt()[p.getOrCreateUniqueID().rawID] = rc::internal::specialCraftCount(p.getOrCreateUniqueID().rawID);
        gf::log().info("item: real craft armed for {}", p.getRealName());
    });
    ctx.info("real players armed for the crafting table: " + std::to_string(n));
});

GF_REGISTER_SELFTEST("item.real_craft_check", [](Context& ctx) {
    int n = forRealPlayers([&](Player& p) {
        auto uid = p.getOrCreateUniqueID().rawID;
        if (rc::internal::specialCraftCount(uid) == armedAt()[uid]) {
            // a glob run ('item.*'): nothing was crafted since item.real_craft_arm
            ctx.info(p.getRealName() + " made no special craft since the arm test");
        } else {
            int special = countOf(p, kFixture, [](ItemStack const& s) { return gf::item::getData(s, "special") == true; });
            int sticks  = countOf(p, "minecraft:stick");
            auto route  = rc::internal::lastSpecialRoute(uid);
            ctx.check(special == 2, p.getRealName() + " crafted two fixture items with the assembler's item data (got " + std::to_string(special) + ")");
            ctx.check(sticks == 1, p.getRealName() + " kept the remainder stick (got " + std::to_string(sticks) + ")");
            ctx.info("the crafting hook found the special recipe by the " + route);
            gf::log().info("item: real craft checked for {}: {} special items, {} remainder (found by the {})", p.getRealName(), special, sticks, route);
        }
        clearOf(p, kFixture);
        clearOf(p, "minecraft:stick");
    });
    rc::internal::unregisterSpecial(kFixture);
    rc::internal::setTestDataRecipe(kFixture, nlohmann::json());
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
        // 30 levels for the merge; item.real_anvil_check gives the player its own back
        auto uid = p.getOrCreateUniqueID().rawID;
        if (!levelsBefore().contains(uid)) levelsBefore()[uid] = levelsOf(p);
        p.addLevels(30 - levelsOf(p));
        p.sendInventory(false);
        gf::log().info("item: real anvil armed for {}", p.getRealName());
    });
    ctx.info("real players armed for the anvil: " + std::to_string(n));
});

GF_REGISTER_SELFTEST("item.real_anvil_check", [](Context& ctx) {
    int n = forRealPlayers([&](Player& p) {
        int   best = 0, books = 0;
        auto& inv  = p.getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i) {
            if (items::idOf(inv.getItem(i)) == "minecraft:iron_sword") best = std::max(best, gf::enchant::level(inv.getItem(i), "gf_test:fixture_react"));
            if (items::idOf(inv.getItem(i)) == "minecraft:enchanted_book") ++books;
        }
        if (best == 1 && books == 1) {
            // a glob run ('item.*'): the sword and the book were never combined
            ctx.info(p.getRealName() + " combined nothing since the arm test");
        } else {
            ctx.check(best == 2, p.getRealName() + "'s sword came out of the anvil with the custom enchantment at II (got " + std::to_string(best) + ")");
        }
        gf::log().info("item: real anvil checked for {}: fixture_react {} ({} engine merges, {} form merges)", p.getRealName(), best,
                       gf::enchant::internal::anvilMergeCount(p), gf::enchant::internal::anvilFallbackCount(p));
        clearOf(p, "minecraft:iron_sword");
        clearOf(p, "minecraft:enchanted_book");
        if (auto it = levelsBefore().find(p.getOrCreateUniqueID().rawID); it != levelsBefore().end()) {
            p.addLevels(it->second - levelsOf(p));
            levelsBefore().erase(it);
        }
    });
    gf::enchant::internal::unregisterType("gf_test:fixture_react");
    ctx.info("real players checked: " + std::to_string(n));
});

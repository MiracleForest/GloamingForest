// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/UncraftingTableBlock.java
//   src/main/java/twilightforest/inventory/UncraftingMenu.java (stillValid)
//   src/main/java/twilightforest/network/UncraftingGuiPacket.java
//   src/main/java/twilightforest/loot/conditions/UncraftingTableEnabledCondition.java
//   src/main/java/twilightforest/item/recipe/UncraftingRecipe.java (Serializer)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The uncrafting table block (U13): use opens the menu (the client mod's native screen
// "uncraftmaps.uncrafting", a form flow for vanilla players), the interactions stat, the disabled
// message, the powered state with the amethyst chime, the item tooltip, the loot condition and the
// uncrafting recipe loader.
//
// Vanilla fallback (best effort, README deviation): a form lists the inventory stacks that have an
// uncrafting recipe; picking one shows its ingredients (broken parts marked), the level cost and the
// cycle buttons, and "Uncraft" takes the ingredients as the menu would (the levels, the input count,
// the uncraft_item trigger). "Repair" puts the input's broken parts from the inventory into the
// assembly grid over the shown recipe and takes the result (the combined recipe of TF's screen, with
// the enchantments and the recrafting cost); free recrafting into another recipe needs the client
// mod's screen.
#include <algorithm>
#include <array>
#include <map>
#include <optional>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/common/Screens.h"
#include "feature/ext/item/Recipes.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/loot/Loot.h"
#include "feature/net/Net.h"
#include "feature/recipes/Recipes.h"
#include "feature/stats/Stats.h"
#include "feature/ui/Ui.h"
#include "feature/uncraftmaps/Config.h"
#include "feature/uncraftmaps/Menu.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::uncraftmaps {

namespace {

namespace ia    = feature::common::interactions;
namespace items = game::items;
using core::GlobalPos;

constexpr char kActivateSound[] = "block.twilightforest.uncrafting_table.activate";

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

BlockSource* regionOf(GlobalPos const& pos) {
    return game::regionOf(pos.dim);
}

/// AbstractContainerMenu#stillValid(access, player, block): the table is there and the player within
/// 8 blocks of its centre (Player#canInteractWithBlock(pos, 4.0) in 1.21: the interaction range + 4).
bool stillValid(Player& player, GlobalPos const& pos) {
    if (config().disableUncraftingTable) return false;
    if (static_cast<int>(player.getDimensionId()) != pos.dim) return false;
    auto* region = regionOf(pos);
    if (!region || game::blockIdAt(*region, game::blockPos(pos)) != kTable) return false;
    auto const& eye   = player.getPosition();
    float       range = (player.isCreative() ? 5.f : 4.5f) + 4.f;
    float       dx    = eye.x - std::clamp(eye.x, static_cast<float>(pos.x), static_cast<float>(pos.x) + 1.f);
    float       dy    = eye.y - std::clamp(eye.y, static_cast<float>(pos.y), static_cast<float>(pos.y) + 1.f);
    float       dz    = eye.z - std::clamp(eye.z, static_cast<float>(pos.z), static_cast<float>(pos.z) + 1.f);
    return dx * dx + dy * dy + dz * dz < range * range;
}

std::optional<GlobalPos> posOf(Json const& ctx) {
    auto p = ctx.value("pos", Json());
    if (!p.is_array() || p.size() != 4) return std::nullopt;
    try {
        return GlobalPos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
    } catch (...) {
        return std::nullopt;
    }
}
Json contextOf(GlobalPos const& pos) { return {{"pos", {pos.dim, pos.x, pos.y, pos.z}}}; }

/// The menu the player has open for the table of `ctx` (nullptr: none or no longer valid).
MenuState* menuFor(Player& player, Json const& ctx) {
    auto* s  = openMenu(player);
    auto  at = posOf(ctx);
    if (!s || !at || !(s->pos == *at)) return nullptr;
    if (!stillValid(player, s->pos)) return nullptr;
    return s;
}

Click clickTypeOf(std::string const& t) {
    if (t == "quick_move") return Click::QuickMove;
    if (t == "swap") return Click::Swap;
    if (t == "clone") return Click::Clone;
    if (t == "throw") return Click::Throw;
    if (t == "pickup_all") return Click::PickupAll;
    return Click::Pickup;
}

// ---- the vanilla form flow ---------------------------------------------------------------------------

void openForm(Player& player, GlobalPos const& pos);

std::string stackLabel(Player const& player, ItemStack const& s) {
    (void)player;
    return items::displayName(s) + (items::countOf(s) > 1 ? " x" + std::to_string(items::countOf(s)) : std::string());
}

/// The recipe page of inventory slot `slot` (the input), with the cycle counters.
void openRecipeForm(Player& player, GlobalPos const& pos, int slot, core::uncraftmaps::Cycles cycles) {
    if (!stillValid(player, pos)) return;
    auto stack = ItemStack(player.getInventory().getItem(slot));
    if (items::isEmpty(stack)) return openForm(player, pos);
    MenuState state;
    state.pos    = pos;
    state.cycles = cycles;
    Menu menu(player, state);
    state.input = stack;
    menu.slotsChanged(Menu::Changed::Input);
    std::string body = stackLabel(player, stack) + "\n\n";
    if (menu.inputBanned() || !state.ghost) {
        body += "§c" + trFor(player, "container.twilightforest.uncrafting_table.disabled_item", "Uncrafting this item is disabled.") + "§r";
    } else {
        for (int r = 0; r < 3; ++r) {
            std::string row;
            for (int c = 0; c < 3; ++c) {
                auto const& s = state.uncrafting[static_cast<std::size_t>(r * 3 + c)];
                if (!row.empty()) row += " | ";
                if (items::isEmpty(s)) row += "-";
                else row += (state.marked[static_cast<std::size_t>(r * 3 + c)] ? "§c" : "") + items::displayName(s) + "§r";
            }
            body += row + "\n";
        }
        int cost = state.uncraftingCost;
        if (config().disableUncrafting) body += "\n§4" + trFor(player, "container.twilightforest.uncrafting_table.uncrafting_disabled", "Uncrafting is disabled via config") + "§r";
        if (cost > 0) body += "\n" + trFor(player, "gf.uncraftmaps.form.cost", "Cost: {} levels", cost);
    }
    // repairing: the broken parts from the inventory over the ghost recipe (the page has read the ghost)
    std::optional<RepairPlan> repair = planRepair(player, slot, state, menu);
    if (repair)
        body += "\n" + trFor(player, "gf.uncraftmaps.form.repair", "Repair with the broken parts from your inventory: {} ({} levels)", items::displayName(repair->result), repair->cost);
    std::vector<ui::FormButton> buttons;
    auto                        reopen = [pos, slot](core::uncraftmaps::Cycles c) {
        return [pos, slot, c](Player& p) { openRecipeForm(p, pos, slot, c); };
    };
    if (state.ghost && !menu.inputBanned()) {
        buttons.push_back({trFor(player, "gui.twilightforest.uncrafting_jei", "Uncrafting"), "", [pos, slot, cycles, stack](Player& p) {
                               if (!stillValid(p, pos)) return;
                               auto now = ItemStack(p.getInventory().getItem(slot));
                               if (!items::sameItemSameData(now, stack) || items::countOf(now) != items::countOf(stack)) return openRecipeForm(p, pos, slot, cycles);
                               MenuState st;
                               st.pos    = pos;
                               st.cycles = cycles;
                               Menu m(p, st);
                               st.input = now;
                               m.slotsChanged(Menu::Changed::Input);
                               int take = -1;
                               for (int i = 0; i < 9; ++i)
                                   if (!items::isEmpty(st.uncrafting[static_cast<std::size_t>(i)]) && !st.marked[static_cast<std::size_t>(i)]) {
                                       take = kSlotUncrafting + i;
                                       break;
                                   }
                               if (take >= 0) m.clicked(take, 0, Click::Pickup);
                               if (items::isEmpty(st.carried)) {
                                   p.sendMessage("§c" + trFor(p, "gf.uncraftmaps.form.failed", "You can't uncraft this now."));
                                   return openRecipeForm(p, pos, slot, cycles);
                               }
                               // the input's remainder back into its slot, the ingredients to the player
                               p.getInventory().setItem(slot, st.input);
                               st.input = ItemStack{};
                               m.removed(); // the cursor stack and the assembly grid go to the player
                               log().info("uncraftmaps: {} uncrafted {} with the form", p.getRealName(), items::idOf(now));
                               openForm(p, pos);
                           }});
        buttons.push_back({trFor(player, "container.twilightforest.uncrafting_table.cycle_next_uncraft", "Next Uncrafting Recipe"), "", [=](Player& p) {
                               auto c = cycles;
                               ++c.unrecipe;
                               reopen(c)(p);
                           }});
        buttons.push_back({trFor(player, "container.twilightforest.uncrafting_table.cycle_back_uncraft", "Previous Uncrafting Recipe"), "", [=](Player& p) {
                               auto c = cycles;
                               --c.unrecipe;
                               reopen(c)(p);
                           }});
        if (!config().disableIngredientSwitching) {
            buttons.push_back({trFor(player, "container.twilightforest.uncrafting_table.cycle_next_ingredient", "Next Uncrafting Ingredients"), "", [=](Player& p) {
                                   auto c = cycles;
                                   ++c.ingredients;
                                   reopen(c)(p);
                               }});
            buttons.push_back({trFor(player, "container.twilightforest.uncrafting_table.cycle_back_ingredient", "Previous Uncrafting Ingredients"), "", [=](Player& p) {
                                   auto c = cycles;
                                   --c.ingredients;
                                   reopen(c)(p);
                               }});
        }
    }
    if (repair) {
        buttons.push_back({trFor(player, "gf.uncraftmaps.form.repair_button", "Repair"), "", [pos, slot, cycles, stack](Player& p) {
                               if (!stillValid(p, pos)) return;
                               auto now = ItemStack(p.getInventory().getItem(slot));
                               if (!items::sameItemSameData(now, stack) || items::countOf(now) != items::countOf(stack)) return openRecipeForm(p, pos, slot, cycles);
                               std::string why;
                               if (!repairInSlot(p, slot, cycles, &why)) {
                                   p.sendMessage("§c"
                                                 + (why == "levels" ? trFor(p, "gf.uncraftmaps.form.repair_levels", "You don't have enough levels to repair this.")
                                                                    : trFor(p, "gf.uncraftmaps.form.repair_failed", "You can't repair this now.")));
                                   return openRecipeForm(p, pos, slot, cycles);
                               }
                               openForm(p, pos);
                           }});
    }
    buttons.push_back({trFor(player, "gui.back", "Back"), "", [pos](Player& p) { openForm(p, pos); }});
    ui::form(player, trFor(player, "container.twilightforest.uncrafting_table", "Uncrafting Table"), body, std::move(buttons));
}

/// The list page: every inventory stack the table can uncraft.
void openForm(Player& player, GlobalPos const& pos) {
    if (!stillValid(player, pos)) return;
    std::vector<ui::FormButton> buttons;
    auto&                       inv = player.getInventory();
    for (int slot = 0; slot < 36; ++slot) {
        auto const& s = inv.getItem(slot);
        if (items::isEmpty(s)) continue;
        if (Menu::recipesFor(s).empty()) continue;
        buttons.push_back({stackLabel(player, s), "", [pos, slot](Player& p) { openRecipeForm(p, pos, slot, {}); }});
    }
    std::string body = buttons.empty() ? trFor(player, "gf.uncraftmaps.form.empty", "Nothing in your inventory can be uncrafted.")
                                       : trFor(player, "gf.uncraftmaps.form.pick", "Pick an item to uncraft:");
    if (config().disableUncrafting) body += "\n§4" + trFor(player, "container.twilightforest.uncrafting_table.uncrafting_disabled", "Uncrafting is disabled via config") + "§r";
    ui::form(player, trFor(player, "container.twilightforest.uncrafting_table", "Uncrafting Table"), body, std::move(buttons));
}

// ---- the uncrafting recipe loader --------------------------------------------------------------------

/// UncraftingRecipe.Serializer: cost (-1), input, input_count (1), the shaped pattern.
std::optional<recipes::CraftingRecipe> loadUncrafting(std::string const& id, Json const& j) {
    recipes::CraftingRecipe r;
    r.id   = id;
    r.type = "twilightforest:uncrafting";
    r.data = j;
    auto p = j.find("pattern");
    auto k = j.find("key");
    if (p == j.end() || !p->is_array() || p->empty() || k == j.end() || !k->is_object() || !j.contains("input")) return std::nullopt;
    std::map<char, recipes::Ingredient> key;
    for (auto const& [c, v] : k->items())
        if (c.size() == 1) key[c[0]] = recipes::ext::parseIngredient(v);
    std::vector<std::string> rows;
    for (auto const& row : *p)
        if (row.is_string()) rows.push_back(row.get<std::string>());
    r.height = static_cast<int>(rows.size());
    for (auto const& row : rows) r.width = std::max(r.width, static_cast<int>(row.size()));
    for (auto const& row : rows)
        for (int x = 0; x < r.width; ++x) {
            char c = x < static_cast<int>(row.size()) ? row[static_cast<std::size_t>(x)] : ' ';
            r.ingredients.push_back(c == ' ' ? recipes::Ingredient{} : key[c]);
        }
    auto input = recipes::ext::parseIngredient(j["input"]);
    if (input.empty()) return std::nullopt; // Ingredient.CODEC_NONEMPTY
    r.resultItem  = input.items.empty() ? std::string() : input.items.front();
    r.resultCount = j.value("input_count", 1);
    if (!j.contains("cost")) r.data["cost"] = -1;
    return r;
}

// ---- the powered state -------------------------------------------------------------------------------

/// UncraftingTableBlock#neighborChanged: the powered state follows the signal; switching on above an
/// amethyst block chimes.
void updatePowered(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (game::blockId(block) != kTable) return;
    bool powered = redstone::power(region, pos) > 0;
    bool was     = game::intState(block, "twilightforest:powered").value_or(0) != 0;
    if (powered == was) return;
    if (powered && game::blockIdAt(region, {pos.x, pos.y - 1, pos.z}) == "minecraft:amethyst_block")
        fx::sound(region.getDimension(), kActivateSound, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 0.5f, 1.0f);
    if (auto const* next = game::blockWithStates(kTable, {{"twilightforest:powered", powered}})) game::setBlock(region, pos, *next);
}

} // namespace

// ---- the vanilla form's repair ------------------------------------------------------------------

/// Fills `st.assembly` from the inventory (taking nothing yet) and computes the result; nullopt (the
/// assembly grid empty again) when the input is not damaged, a broken part is missing from the
/// inventory, the combined grid makes nothing, or the filled cells alone form the recipe (TF then
/// crafts normally from the assembly grid and keeps the input: no repair).
std::optional<RepairPlan> planRepair(Player& player, int inputSlot, MenuState& st, Menu& menu) {
    auto fail = [&]() -> std::optional<RepairPlan> {
        for (auto& a : st.assembly) a = ItemStack{};
        st.result = ItemStack{};
        return std::nullopt;
    };
    if (!st.ghost || menu.inputBanned() || items::isEmpty(st.input) || st.input.getDamageValue() <= 0) return fail();
    RepairPlan         plan;
    std::map<int, int> used; // inventory slot -> items planned
    auto&              inv    = player.getInventory();
    bool               filled = false;
    plan.fromSlot.fill(-1);
    for (int i = 0; i < 9; ++i) {
        auto const& part = st.uncrafting[static_cast<std::size_t>(i)];
        if (items::isEmpty(part) || !st.marked[static_cast<std::size_t>(i)]) continue;
        // the same item and data as the ghost part (a plain stack: never a renamed, enchanted or
        // other variant one)
        int found = -1;
        for (int s = 0; s < 36 && found < 0; ++s) {
            if (s == inputSlot) continue;
            auto const& have = inv.getItem(s);
            if (!items::isEmpty(have) && items::sameItemSameData(have, part) && items::countOf(have) > used[s]) found = s;
        }
        if (found < 0) return fail();
        ++used[found];
        plan.fromSlot[static_cast<std::size_t>(i)] = found;
        st.assembly[static_cast<std::size_t>(i)]   = items::withCount(ItemStack(inv.getItem(found)), 1);
        filled                                      = true;
    }
    if (!filled) return fail();
    menu.slotsChanged(Menu::Changed::Assembly);
    if (items::isEmpty(st.result)) return fail();
    for (auto const* r : Menu::craftingRecipesFor(st.assembly))
        if (items::sameItemSameData(Menu::assemble(*r), st.result)) return fail();
    plan.result = st.result;
    plan.cost   = st.recraftingCost;
    return plan;
}

bool repairInSlot(Player& p, int slot, core::uncraftmaps::Cycles cycles, std::string* why) {
    auto now = ItemStack(p.getInventory().getItem(slot));
    MenuState st;
    st.cycles = cycles;
    Menu m(p, st);
    st.input = now;
    m.slotsChanged(Menu::Changed::Input);
    auto plan = planRepair(p, slot, st, m);
    if (!plan) {
        if (why) *why = "parts";
        st.input = ItemStack{};
        return false;
    }
    // the parts leave the inventory for the assembly grid
    auto& inv = p.getInventory();
    for (int s : plan->fromSlot) {
        if (s < 0) continue;
        auto have = ItemStack(inv.getItem(s));
        inv.setItem(s, items::countOf(have) > 1 ? items::withCount(have, items::countOf(have) - 1) : ItemStack{});
    }
    m.clicked(kSlotResult, 0, Click::Pickup);
    if (items::isEmpty(st.carried)) {
        // not taken (the levels): the parts go back, the input stays in its slot
        if (why) *why = "levels";
        st.input = ItemStack{};
        m.removed();
        return false;
    }
    inv.setItem(slot, st.input);
    st.input = ItemStack{};
    m.removed(); // the result and what is left in the assembly grid go to the player
    log().info("uncraftmaps: {} repaired {} with the form", p.getRealName(), items::idOf(now));
    return true;
}

// ---- module ------------------------------------------------------------------------------------------

class TableModule {
public:
    void install() {
        static bool once = false;
        if (!once) {
            once = true;
            registerOnce();
        }
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            if (++mTicks % 5 != 0) return;
            auto level = ll::service::getLevel();
            if (!level) return;
            // AbstractContainerMenu#stillValid every tick: the screen closes when it fails
            std::vector<ActorUniqueID> gone;
            level->forEachPlayer([&](Player& p) {
                if (auto* s = openMenu(p); s && !stillValid(p, s->pos)) gone.push_back(p.getOrCreateUniqueID());
                return true;
            });
            for (auto id : gone) {
                if (auto* p = level->getPlayer(id)) {
                    screens::close(*p, kScreen);
                    endMenu(*p);
                }
            }
        });
        mLeave = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) { endMenu(ev.self()); });
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mLeave);
    }

private:
    static void registerOnce() {
        // UncraftingTableBlock#useWithoutItem
        ia::onUseOn({.block = kTable}, [](ia::UseOnContext const& c) {
            if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass; // the item is used instead
            if (!c.firstEvent) return ia::Result::Consume;
            if (config().disableUncraftingTable) {
                actionBar(c.player, trFor(c.player, "block.twilightforest.uncrafting_table.disabled", "This block has been disabled."));
                return ia::Result::Pass;
            }
            auto& player = c.player;
            auto  pos    = c.pos;
            beginMenu(player, pos);
            if (!screens::open(player, kScreen, contextOf(pos))) {
                endMenu(player);
                return ia::Result::Consume;
            }
            stats::award(player, "twilightforest:uncrafting_table_interactions");
            log().info("uncraftmaps: {} opened the uncrafting table at {} {} {}", player.getRealName(), pos.x, pos.y, pos.z);
            return ia::Result::Consume;
        });

        screens::registerType({
            .id   = kScreen,
            .data = [](Player& player, Json const& ctx) -> std::optional<Json> {
                auto* s = menuFor(player, ctx);
                if (!s) {
                    endMenu(player);
                    return std::nullopt;
                }
                return menuData(player, *s);
            },
            .action =
                [](Player& player, Json const& ctx, Json const& action) {
                    auto* s = menuFor(player, ctx);
                    if (!s) return;
                    Menu menu(player, *s);
                    auto op = action.value("op", std::string{});
                    if (op == "click") {
                        menu.clicked(action.value("slot", -1), action.value("button", 0), clickTypeOf(action.value("type", std::string{"pickup"})));
                    } else if (op == "button") {
                        menu.guiButton(action.value("type", -1));
                    }
                    player.sendInventory(false);
                },
            .form =
                [](Player& player, Json const& ctx) {
                    // vanilla players: the form flow (no transient menu: nothing waits in it)
                    endMenu(player);
                    if (auto at = posOf(ctx)) openForm(player, *at);
                },
            .closed = [](Player& player, Json const&) { endMenu(player); },
        });

        // UncraftingGuiPacket (the client mod's cycle buttons and mouse wheel)
        net::on(kGuiChannel, [](Player& player, Json const& msg) {
            auto* s = openMenu(player);
            if (!s || !stillValid(player, s->pos)) return;
            Menu(player, *s).guiButton(msg.value("type", -1));
            screens::refresh(player, kScreen, contextOf(s->pos));
        });

        // the powered state and the amethyst chime
        blockevents::onNeighborChanged(kTable, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { updatePowered(region, pos); });
        blockevents::onPlaced(kTable, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { updatePowered(region, pos); });

        // UncraftingTableBlock#appendHoverText
        gf::item::addTooltip(kTable, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
            if (!config().disableUncraftingTable) return;
            auto text = viewer ? trFor(*viewer, "block.twilightforest.uncrafting_table.disabled", "This block has been disabled.")
                               : tr("block.twilightforest.uncrafting_table.disabled", "This block has been disabled.");
            lines.push_back("§c" + text);
        });

        // UncraftingTableEnabledCondition
        loot::registerCondition("twilightforest:uncrafting_table_enabled", [](loot::LootContext const&, Json const&, loot::RandomSource&) {
            return !config().disableUncraftingTable;
        });

        recipes::registerType("twilightforest:uncrafting", loadUncrafting);
    }

    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mLeave;
    std::uint64_t          mTicks = 0;
};

/// The redstone signal of a table (selftests).
void refreshPowered(BlockSource& region, BlockPos const& pos) { updatePowered(region, pos); }

} // namespace gf::feature::uncraftmaps

GF_REGISTER_MODULE(gf::feature::uncraftmaps::TableModule);

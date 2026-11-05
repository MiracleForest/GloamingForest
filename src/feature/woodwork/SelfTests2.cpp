// Unit U4 (woodwork) selftests, part 2 (woodwork.*, dx 400..499, rule R12): trapped chest signal,
// signs, every recipe and its unlock, fuel, the boat's crash landing, the client mod's rack sync,
// the display entity's twins and drops, a vine in a waterlogged hollow log.

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/UnlockedRecipesServerComponent.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/RecipeUnlockingRequirement.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/ext/woodwork/Fuel.h"
#include "feature/families/Signs.h"
#include "feature/inventory/Inventory.h"
#include "feature/net/Net.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/JavaIds.h"
#include "feature/woodwork/DryingRack.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace ww   = gf::feature::woodwork;
namespace rack = gf::feature::woodwork::rack;
namespace ia   = gf::interactions;
using Facing   = ScriptModuleMinecraft::ScriptFacing;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) { return p.offset(dx, dy, dz); }
SimulatedPlayer& simPlayer(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

constexpr int kDx[6] = {0, 0, 0, 0, -1, 1};
constexpr int kDz[6] = {0, 0, -1, 1, 0, 0};

void stand(Context& ctx, GlobalPos const& p, int face, std::string const& itemId) {
    auto& sp = simPlayer(ctx);
    Vec3  pos{p.x + 0.5f, float(p.y), p.z + 0.5f};
    if (face >= 2) {
        pos.x += kDx[face] * 2.0f;
        pos.z += kDz[face] * 2.0f;
    } else {
        pos.z += 2.0f;
        if (face == 1) pos.y += 1.0f;
    }
    sp.teleport(pos, sp.getDimensionId());
    sp.simulateLookAt(gf::game::blockPos(p), sim::LookDuration::Instant);
    if (itemId.empty()) sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    else if (auto item = gf::game::items::make(itemId, 16)) sp.simulateSetItem(*item, true, 0);
}

void useOn(Context& ctx, GlobalPos const& p, Facing face, Vec3 hit) {
    auto&    sp   = simPlayer(ctx);
    BlockPos bp   = gf::game::blockPos(p);
    auto&    held = const_cast<ItemStack&>(sp.getSelectedItem());
    sp.mGameMode->useItemOn(held, bp, static_cast<uchar>(face), Vec3{bp.x + hit.x, bp.y + hit.y, bp.z + hit.z}, HandSlot::Mainhand, nullptr, true);
}

ll::coro::CoroTask<> click(Context& ctx, GlobalPos p, Facing face, Vec3 hit, std::string itemId) {
    stand(ctx, p, static_cast<int>(face), itemId);
    co_await ctx.ticks(8);
    useOn(ctx, p, face, hit);
    co_await ctx.ticks(2);
}

ll::coro::CoroTask<std::optional<GlobalPos>> place(Context& ctx, GlobalPos against, Facing face, Vec3 hit, std::string itemId) {
    stand(ctx, against, static_cast<int>(face), itemId);
    co_await ctx.ticks(8);
    std::optional<GlobalPos> placed;
    auto id = ia::onPlace({}, [&placed](ia::PlaceContext const& c) { placed = c.pos; }, ia::priority::Highest + 1000);
    useOn(ctx, against, face, hit);
    ia::remove(id);
    co_await ctx.ticks(2);
    co_return placed;
}

std::vector<ItemActor*> itemsNear(Context& ctx, GlobalPos const& p, float r = 5.f) {
    std::vector<ItemActor*> out;
    auto*                   region = ctx.region();
    if (!region) return out;
    AABB box{Vec3{p.x - r, p.y - 3.f, p.z - r}, Vec3{p.x + r + 1, p.y + 8.f, p.z + r + 1}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item") out.push_back(static_cast<ItemActor*>(a));
    return out;
}

int countNear(Context& ctx, GlobalPos const& p, std::string const& itemId) {
    int n = 0;
    for (auto* a : itemsNear(ctx, p))
        if (gf::game::items::idOf(a->item()) == itemId) n += a->item().mCount;
    return n;
}

void clearItemsNear(Context& ctx, GlobalPos const& p) {
    for (auto* a : itemsNear(ctx, p)) a->remove();
}

std::vector<Actor*> actorsNear(Context& ctx, GlobalPos const& p, char const* type, float r = 4.f) {
    std::vector<Actor*> out;
    auto*               region = ctx.region();
    if (!region) return out;
    AABB box{Vec3{p.x - r, p.y - 4.f, p.z - r}, Vec3{p.x + r + 1, p.y + 10.f, p.z + r + 1}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

int dustSignal(Context& ctx, GlobalPos const& p) {
    auto* region = ctx.region();
    if (!region) return -1;
    return gf::game::intState(region->getBlock(gf::game::blockPos(p)), "redstone_signal").value_or(-1);
}

/// The player counts as a client mod user while the guard lives.
struct CompanionGuard {
    Context& ctx;
    explicit CompanionGuard(Context& c) : ctx(c) {
        gf::feature::net::setTestCompanion(ctx.player(), true);
        (void)gf::feature::net::takeTestMessages(ctx.player());
    }
    ~CompanionGuard() { gf::feature::net::setTestCompanion(ctx.player(), false); }
    CompanionGuard(CompanionGuard const&)            = delete;
    CompanionGuard& operator=(CompanionGuard const&) = delete;
};

struct RecipeRow {
    char const* id;
    char const* type;
    char const* result;
    char const* advancement;
};
constexpr RecipeRow kRecipes[] = {
#include "feature/woodwork/RecipeTable.inc"
};

/// The game's recipes whose id is `id` or one of its Bedrock variants `id_<n>` (F1 writes one per
/// ingredient choice).
std::vector<Recipe const*> gameRecipes(std::string const& id) {
    std::vector<Recipe const*> out;
    auto                       level = ll::service::getLevel();
    if (!level) return out;
    Recipes& recipes = level->getRecipes();
    for (auto const& [tag, byId] : recipes.mRecipes.get()) {
        for (auto const& [rid, recipe] : byId) {
            if (!recipe) continue;
            bool variant = rid.size() > id.size() + 1 && rid.starts_with(id) && rid[id.size()] == '_'
                        && rid.find_first_not_of("0123456789", id.size() + 1) == std::string::npos;
            if (rid == id || variant) out.push_back(recipe.get());
        }
    }
    return out;
}

} // namespace

// ---- chests --------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.trapped_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TrappedChestBlock: the players looking inside are its signal (F11's trapped_chest family)
    (void)co_await ctx.flatArea(430, 5);
    auto t0 = ctx.testPos(430, 2);
    ctx.require(co_await ctx.ensureLoaded(t0), "test area loaded");
    int n = 0;
    for (auto const* id : {"twilightforest:twilight_oak_trapped_chest", "twilightforest:sorting_trapped_chest"}) {
        auto t = at(t0, 3 * n++, 0, 0);
        ctx.require(ctx.setBlock(t, id, {{"twilightforest:facing", "north"}}), std::string(id));
        ctx.require(ctx.setBlock(at(t, 1, 0, 0), "minecraft:redstone_wire"), "dust beside it");
        co_await ctx.ticks(2);
        ctx.check(dustSignal(ctx, at(t, 1, 0, 0)) == 0, std::string(id) + ": no signal while closed");
        {
            CompanionGuard companion(ctx);
            co_await click(ctx, t, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, "");
            co_await ctx.ticks(3);
            ctx.check(dustSignal(ctx, at(t, 1, 0, 0)) >= 1, std::string(id) + ": an open trapped chest powers the dust (" + std::to_string(dustSignal(ctx, at(t, 1, 0, 0))) + ")");
            gf::feature::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
            co_await ctx.ticks(3);
            ctx.check(dustSignal(ctx, at(t, 1, 0, 0)) == 0, std::string(id) + ": no signal once closed");
        }
        gf::game::clearBlock(*ctx.region(), gf::game::blockPos(at(t, 1, 0, 0)));
        gf::game::clearBlock(*ctx.region(), gf::game::blockPos(t));
        co_await ctx.ticks(2);
    }
});

// ---- signs --------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.signs", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the TF signs place from their items (standing, on a wall, hanging) and keep their text
    (void)co_await ctx.flatArea(435, 5);
    auto p = ctx.testPos(435, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto s = co_await place(ctx, at(p, 0, -1, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "twilightforest:twilight_oak_sign");
    ctx.require(s.has_value(), "a twilight oak sign placed");
    ctx.check(ctx.blockAt(*s) == "twilightforest:twilight_oak_sign", "a standing sign on the ground (" + ctx.blockAt(*s) + ")");
    ctx.check(gf::signs::setText(*s, {"Twilight", "Forest"}) && gf::signs::text(*s).size() >= 2 && gf::signs::text(*s)[0] == "Twilight", "the sign keeps its text");
    ctx.check(gf::signs::setText(*s, {"back"}, false) && gf::signs::text(*s, false)[0] == "back", "and the text of its back");
    // the twilight oak wall sign keeps TF's id (twilight_wall_sign) and its text (its placement on
    // a wall is the families kit's, tested there)
    auto wall = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(wall, "minecraft:stone"), "a wall");
    std::optional<GlobalPos> w = at(p, 1, 0, 0);
    ctx.require(ctx.setBlock(*w, "twilightforest:twilight_wall_sign", {{"twilightforest:facing", "west"}}), "the twilight wall sign exists");
    ctx.check(gf::signs::setText(*w, {"wall"}) && gf::signs::text(*w)[0] == "wall", "the wall sign keeps its text");    auto roof = at(p, 0, 2, 2);
    ctx.require(ctx.setBlock(roof, "minecraft:stone"), "a roof");
    auto h = co_await place(ctx, roof, Facing::Down, Vec3{0.5f, 0.0f, 0.5f}, "twilightforest:canopy_hanging_sign");
    ctx.check(h && ctx.blockAt(*h) == "twilightforest:canopy_hanging_sign", "under a block a hanging sign (" + (h ? ctx.blockAt(*h) : std::string("-")) + ")");
    if (h) ctx.check(gf::signs::setText(*h, {"hanging"}) && gf::signs::text(*h)[0] == "hanging", "the hanging sign keeps its text");
    for (auto const& q : {std::optional<GlobalPos>(s), w, h})
        if (q) ctx.cleanupBlock(*q);
    ctx.cleanupBlock(wall);
    ctx.cleanupBlock(roof);
    clearItemsNear(ctx, p);
});

// ---- recipes ------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.recipes_all", [](Context& ctx) {
    // every recipe of Appendix A loads and makes its result; the crafting and stonecutting ones are
    // the game's recipes with the unlock F1 wrote from their advancement (has_* items; in_water)
    int missing = 0, wrong = 0, unlock = 0, game = 0;
    std::string firstBad;
    for (auto const& r : kRecipes) {
        auto const* recipe = gf::recipes::byId(r.id);
        auto        result = gf::tags::javaids::primary("item", r.result);
        if (!recipe) {
            ++missing;
            if (firstBad.empty()) firstBad = std::string(r.id) + " not loaded";
            continue;
        }
        if (recipe->resultItem != result && recipe->resultItem != r.result) {
            ++wrong;
            if (firstBad.empty()) firstBad = std::string(r.id) + " makes " + recipe->resultItem + ", not " + result;
        }
        if (std::string(r.type) == "drying") continue;
        auto list = gameRecipes(r.id);
        if (list.empty()) {
            ++game;
            if (firstBad.empty()) firstBad = std::string(r.id) + " is no game recipe";
            continue;
        }
        for (auto const* g : list) {
            auto const& req = g->mUnlockingRequirement.get();
            if (req.mContext == RecipeUnlockingRequirement::UnlockingContext::None && req.mIngredients.get().empty()) {
                ++unlock;
                if (firstBad.empty()) firstBad = std::string(r.id) + " has no unlock";
            }
        }
    }
    ctx.info(std::to_string(std::size(kRecipes)) + " recipes checked");
    ctx.check(std::size(kRecipes) == 230, "230 recipes in the table (" + std::to_string(std::size(kRecipes)) + ")");
    ctx.check(missing == 0 && wrong == 0 && game == 0 && unlock == 0,
              "every recipe loads, makes its result and unlocks (missing " + std::to_string(missing) + ", wrong " + std::to_string(wrong) + ", not in game " + std::to_string(game)
                  + ", no unlock " + std::to_string(unlock) + (firstBad.empty() ? std::string() : "; " + firstBad) + ")");
});

GF_REGISTER_SELFTEST("woodwork.recipe_unlock", [](Context& ctx) -> ll::coro::CoroTask<> {
    // RecipeUnlockingRequirement: a canopy log in the inventory unlocks the canopy planks (Java's
    // has_log criterion of recipes/building_blocks/wood/canopy_planks)
    auto& player = ctx.player();
    player.getInventory().removeAllItems();
    auto unlocked = player.getEntityContext().tryGetComponent<UnlockedRecipesServerComponent>();
    ctx.require(static_cast<bool>(unlocked), "the player has the game's unlocked recipes");
    auto ids = gameRecipes("twilightforest:wood/canopy_planks");
    ctx.require(!ids.empty(), "the canopy planks recipes");
    bool before = false;
    for (auto const* r : ids) before |= unlocked->mUnlockedRecipes.get().contains(r->mRecipeId.get());
    if (before) ctx.info("the canopy planks were unlocked before (an earlier run)");
    ctx.give("twilightforest:canopy_log", 1);
    bool after = false;
    for (int i = 0; i < 40 && !after; ++i) {
        co_await ctx.ticks(1);
        auto u = ctx.player().getEntityContext().tryGetComponent<UnlockedRecipesServerComponent>();
        for (auto const* r : ids) after |= u && u->mUnlockedRecipes.get().contains(r->mRecipeId.get());
    }
    ctx.check(after, "a canopy log unlocks the canopy planks recipe");
    ctx.player().getInventory().removeAllItems();
});

// ---- fuel ----------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.fuel", [](Context& ctx) {
    // Java's fuel values (vanilla fuel tags, TF's banister row, HollowLogItem), as a part of coal's
    auto burn = [](char const* id) -> float {
        auto s = gf::game::items::make(id);
        return s ? FurnaceBlockActor::getBurnDuration(*s, 200.0f) : -1.0f;
    };
    float coal = burn("minecraft:coal");
    ctx.require(coal > 0, "coal burns");
    struct Fuel {
        char const* id;
        int         ticks;
    };
    for (auto const& f : {Fuel{"twilightforest:canopy_log", 300}, Fuel{"twilightforest:stripped_dark_wood", 300}, Fuel{"twilightforest:twilight_oak_planks", 300},
                          Fuel{"twilightforest:mining_slab", 150}, Fuel{"twilightforest:time_stairs", 300}, Fuel{"twilightforest:sorting_fence", 300},
                          Fuel{"twilightforest:transformation_fence_gate", 300}, Fuel{"twilightforest:mangrove_trapdoor", 300},
                          Fuel{"twilightforest:dark_pressure_plate", 300}, Fuel{"twilightforest:canopy_button", 100}, Fuel{"twilightforest:canopy_door", 200},
                          Fuel{"twilightforest:canopy_sign", 200}, Fuel{"twilightforest:canopy_hanging_sign", 800}, Fuel{"twilightforest:canopy_boat", 1200},
                          Fuel{"twilightforest:canopy_chest_boat", 1200}, Fuel{"twilightforest:oak_banister", 300}, Fuel{"twilightforest:hollow_oak_log", 300},
                          Fuel{"twilightforest:hollow_canopy_log", 300}}) {
        float b = burn(f.id);
        ctx.check(std::abs(b - coal * f.ticks / 1600.0f) < 0.02f * coal, std::string(f.id) + " burns " + std::to_string(f.ticks) + " ticks (" + std::to_string(b) + " / coal " + std::to_string(coal) + ")");
    }
    ctx.check(!gf::woodwork_ext::fuel::burnTicks("twilightforest:canopy_chest"), "a TF chest is no Java fuel (only Blocks.CHEST is)");
});

// ---- boats --------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.boat_crash", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Boat#checkFallDamage: a boat landing on the ground after more than 3 blocks breaks into 3
    // planks of its wood and 2 sticks; one that falls into water does not
    (void)co_await ctx.flatArea(445, 5);
    auto c = ctx.testPos(445, 2);
    ctx.require(co_await ctx.ensureLoaded(c), "test area loaded");
    clearItemsNear(ctx, c);
    auto r = gf::selftest::runServerCommand("summon twilightforest:boat " + std::to_string(c.x) + " " + std::to_string(c.y + 7) + " " + std::to_string(c.z) + " 0 0 twilightforest:canopy", ctx.dimension());
    ctx.require(r.ok, "a canopy boat summoned 7 blocks up: " + r.messages);
    bool gone = false;
    for (int i = 0; i < 80 && !gone; ++i) {
        co_await ctx.ticks(1);
        gone = actorsNear(ctx, c, "twilightforest:boat").empty();
    }
    ctx.check(gone, "the boat broke on landing");
    co_await ctx.ticks(2);
    ctx.check(countNear(ctx, c, "twilightforest:canopy_planks") == 3, "3 canopy planks dropped (" + std::to_string(countNear(ctx, c, "twilightforest:canopy_planks")) + ")");
    ctx.check(countNear(ctx, c, "minecraft:stick") == 2, "2 sticks dropped (" + std::to_string(countNear(ctx, c, "minecraft:stick")) + ")");
    ctx.check(countNear(ctx, c, "twilightforest:canopy_boat") == 0, "no boat item");
    clearItemsNear(ctx, c);
    // a short drop does not break it
    r = gf::selftest::runServerCommand("summon twilightforest:boat " + std::to_string(c.x) + " " + std::to_string(c.y + 2) + " " + std::to_string(c.z) + " 0 0 twilightforest:mining", ctx.dimension());
    ctx.require(r.ok, "a mining boat summoned 2 blocks up");
    co_await ctx.ticks(40);
    auto boats = actorsNear(ctx, c, "twilightforest:boat");
    ctx.check(boats.size() == 1, "a 2 block drop leaves the boat whole");
    for (auto* b : boats) b->remove();
    clearItemsNear(ctx, c);
});

// ---- the drying rack's display and the client mod's copy ------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.rack_sync", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(425, 5);
    auto p = ctx.testPos(425, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(p, "twilightforest:canopy_drying_rack", {{"twilightforest:facing", "east"}}), "a rack");
    co_await ctx.ticks(1);
    auto h = gf::inventory::find(*ctx.region(), gf::game::blockPos(p));
    ctx.require(h.has_value(), "the rack's storage");
    {
        CompanionGuard companion(ctx);
        h->set(p, 0, *gf::game::items::make("minecraft:kelp"));
        co_await ctx.ticks(2);
        // the companion is told what the rack shows: the item, the facing, its display entity
        bool told = false, hidden = false;
        for (auto const& e : gf::feature::net::takeTestMessages(ctx.player())) {
            if (e.channel != "woodwork.rack") continue;
            for (auto const& r : e.payload.value("racks", nlohmann::json::array())) {
                auto pos = r.value("pos", nlohmann::json());
                if (pos == nlohmann::json::array({p.dim, p.x, p.y, p.z}) && r.value("facing", "") == "east" && gf::game::items::idOf(gf::game::items::fromJson(r["item"])) == "minecraft:kelp") {
                    told   = true;
                    hidden = r.value("display", std::int64_t{0}) == rack::displayIdAt(p) && rack::displayIdAt(p) != 0;
                }
            }
        }
        ctx.check(told, "the client mod gets the rack's item and facing (woodwork.rack)");
        ctx.check(hidden, "and the display entity it hides");
        // one display, whatever refreshes and twins
        for (int i = 0; i < 3; ++i) rack::refreshDisplay(p);
        auto displays = actorsNear(ctx, p, "gloamingforest:drying_rack_item", 1.f);
        ctx.check(displays.size() == 1, "one display entity (" + std::to_string(displays.size()) + ")");
        // a twin standing in the rack (a display that loaded late) goes
        auto r = gf::selftest::runServerCommand("summon gloamingforest:drying_rack_item " + std::to_string(p.x + 0.5) + " " + std::to_string(p.y + 0.5) + " " + std::to_string(p.z + 0.5), ctx.dimension());
        if (!r.ok) ctx.info("the display is not summonable (is_summonable false): " + r.messages);
        rack::refreshDisplay(p);
        co_await ctx.ticks(2);
        displays = actorsNear(ctx, p, "gloamingforest:drying_rack_item", 1.f);
        ctx.check(displays.size() == 1, "still one display after a twin appeared (" + std::to_string(displays.size()) + ")");
        // the display never drops the rack's item, even killed
        clearItemsNear(ctx, p);
        for (auto* d : displays) d->kill();
        co_await ctx.ticks(3);
        ctx.check(countNear(ctx, p, "minecraft:kelp") == 0, "a killed display drops nothing");
        // a missing display gets a new one once it stayed missing past the load grace (an actor
        // loads after its chunk; the racks are checked every 2 s)
        bool back = false;
        for (int i = 0; i < 20 && !back; ++i) {
            rack::refreshDisplay(p);
            co_await ctx.ticks(10);
            back = actorsNear(ctx, p, "gloamingforest:drying_rack_item", 1.f).size() == 1;
        }
        ctx.check(back, "the rack shows its item again");
        (void)gf::feature::net::takeTestMessages(ctx.player());
        h->set(p, 0, ItemStack{});
        co_await ctx.ticks(2);
        bool gone = false;
        for (auto const& e : gf::feature::net::takeTestMessages(ctx.player()))
            if (e.channel == "woodwork.rack")
                for (auto const& g : e.payload.value("gone", nlohmann::json::array())) gone |= g == nlohmann::json::array({p.dim, p.x, p.y, p.z});
        ctx.check(gone, "an emptied rack is withdrawn from the client mod");
        ctx.check(actorsNear(ctx, p, "gloamingforest:drying_rack_item", 1.f).empty(), "and its display goes");
    }
    clearItemsNear(ctx, p);
    ctx.cleanupBlock(p);
});

// ---- hollow logs --------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.hollow_log_water", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a vine in a waterlogged vertical hollow log: Java's climbable VINE state holds no water; a
    // ladder keeps it (ladder_waterlogged); a click into the shaft from above faces the inner wall
    // the look reaches
    (void)co_await ctx.flatArea(415, 5);
    auto p = ctx.testPos(415, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto const* oak = ww::hollowLogOf("twilightforest:hollow_oak_log");
    ctx.require(oak != nullptr, "the oak set");
    auto const* water = gf::game::blockWith("minecraft:water");
    ctx.require(ctx.setBlock(p, oak->vertical) && water, "a vertical hollow log");
    ctx.region()->setExtraBlock(gf::game::blockPos(p), *water, 3);
    co_await ctx.ticks(2);
    ctx.require(ww::waterSourceAt(*ctx.region(), gf::game::blockPos(p)), "it is waterlogged");
    co_await click(ctx, p, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:vine");
    ctx.check(ctx.blockAt(p) == oak->climbable, "a vine makes it climbable");
    ctx.check(!ww::waterAt(*ctx.region(), gf::game::blockPos(p)), "the vine variant holds no water");
    // the player stands south of the log looking down into it: the ray meets the north inner wall,
    // whose face points south
    ctx.check(ww::stateOf(ctx.region()->getBlock(gf::game::blockPos(p)), "twilightforest:facing") == "south", "the vine faces the inner wall the look reaches (south)");
    clearItemsNear(ctx, p);
    ctx.require(ctx.setBlock(p, oak->vertical), "the log again");
    ctx.region()->setExtraBlock(gf::game::blockPos(p), *water, 3);
    co_await ctx.ticks(2);
    co_await click(ctx, p, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:ladder");
    ctx.check(ww::stateOf(ctx.region()->getBlock(gf::game::blockPos(p)), "twilightforest:variant") == "ladder_waterlogged", "a ladder in water is ladder_waterlogged");
    ctx.check(ww::waterSourceAt(*ctx.region(), gf::game::blockPos(p)), "and keeps its water");
    if (auto const* air = gf::game::blockWith("minecraft:air")) ctx.region()->setExtraBlock(gf::game::blockPos(p), *air, 3);
    ctx.cleanupBlock(p);
    clearItemsNear(ctx, p);
});

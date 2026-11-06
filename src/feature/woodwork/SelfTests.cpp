// Unit U4 (woodwork) selftests (woodwork.*, dx 400..499, rule R12): log stripping, banisters,
// drying racks, hollow logs, shapes, chests, recipes. Clicks go through the game's GameMode as a
// player's do (the interactions dispatch, then vanilla).

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <variant>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "mc/legacy/ActorUniqueID.h"
#include "feature/inventory/Inventory.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "feature/woodwork/DryingRack.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace ww   = gf::feature::woodwork;
namespace rack = gf::feature::woodwork::rack;
namespace ia   = gf::interactions;
using Facing   = ScriptModuleMinecraft::ScriptFacing;

constexpr char kTf[] = "twilightforest:";

GlobalPos at(GlobalPos p, int dx, int dy, int dz) { return p.offset(dx, dy, dz); }

SimulatedPlayer& simPlayer(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

std::string st(Context& ctx, GlobalPos const& p, std::string const& name) {
    auto* region = ctx.region();
    if (!region) return {};
    return ww::stateOf(region->getBlock(gf::game::blockPos(p)), name);
}

/// FacingID offsets (0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
constexpr int kDx[6] = {0, 0, 0, 0, -1, 1};
constexpr int kDz[6] = {0, 0, -1, 1, 0, 0};

/// Puts `itemId` (`count`; "" = an empty hand) in hotbar slot 0 and stands the player two blocks
/// off face `face` of `p` (south of it for the top / bottom), looking at `look` (default: `p`).
void stand(Context& ctx, GlobalPos const& p, int face, std::string const& itemId, int count = 16, std::optional<Vec3> look = std::nullopt) {
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
    if (look) sp.simulateLookAt(*look, sim::LookDuration::Instant);
    else sp.simulateLookAt(gf::game::blockPos(p), sim::LookDuration::Instant);
    if (itemId.empty()) {
        sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    } else if (auto item = gf::game::items::make(itemId, count)) {
        sp.simulateSetItem(*item, true, 0);
    }
}

constexpr int kClickGap = 8;

/// GameMode::useItemOn on face `face` of `p` at face-local `hit`, as the network handler runs one.
void useOn(Context& ctx, GlobalPos const& p, Facing face, Vec3 hit) {
    auto&    sp   = simPlayer(ctx);
    BlockPos bp   = gf::game::blockPos(p);
    auto&    held = const_cast<ItemStack&>(sp.getSelectedItem());
    sp.mGameMode->useItemOn(held, bp, static_cast<uchar>(face), Vec3{bp.x + hit.x, bp.y + hit.y, bp.z + hit.z}, HandSlot::Mainhand, nullptr, true);
}

ll::coro::CoroTask<> click(Context& ctx, GlobalPos p, Facing face, Vec3 hit, std::string itemId, int count = 16) {
    stand(ctx, p, static_cast<int>(face), itemId, count);
    co_await ctx.ticks(kClickGap);
    useOn(ctx, p, face, hit);
    co_await ctx.ticks(2);
}

/// Places `itemId` against face `face` of `against` (the player stands off that face, or at
/// `standAt`); the position the game placed it at.
ll::coro::CoroTask<std::optional<GlobalPos>> place(Context& ctx, GlobalPos against, Facing face, Vec3 hit, std::string itemId, std::optional<Vec3> standAt = std::nullopt) {
    stand(ctx, against, static_cast<int>(face), itemId);
    if (standAt) {
        auto& sp = simPlayer(ctx);
        sp.teleport(*standAt, sp.getDimensionId());
        sp.simulateLookAt(gf::game::blockPos(against), sim::LookDuration::Instant);
    }
    co_await ctx.ticks(kClickGap);
    std::optional<GlobalPos> placed;
    auto id = ia::onPlace({}, [&placed](ia::PlaceContext const& c) { placed = c.pos; }, ia::priority::Highest + 1000);
    useOn(ctx, against, face, hit);
    ia::remove(id);
    co_await ctx.ticks(2);
    co_return placed;
}

std::vector<Actor*> itemsNear(Context& ctx, GlobalPos const& p) {
    std::vector<Actor*> out;
    auto*               region = ctx.region();
    if (!region) return out;
    AABB box{Vec3{p.x - 5.f, p.y - 3.f, p.z - 5.f}, Vec3{p.x + 6.f, p.y + 4.f, p.z + 6.f}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item") out.push_back(a);
    return out;
}

/// Whether an item entity of `itemId` lies near `p`.
bool droppedNear(Context& ctx, GlobalPos const& p, std::string const& itemId) {
    for (auto* a : itemsNear(ctx, p)) {
        if (gf::game::items::idOf(static_cast<ItemActor*>(a)->item()) == itemId) return true;
    }
    return false;
}

void clearItemsNear(Context& ctx, GlobalPos const& p) {
    for (auto* a : itemsNear(ctx, p)) a->remove();
}

/// How many of `itemId` the test player carries.
int carried(Context& ctx, std::string const& itemId) {
    int   n   = 0;
    auto& inv = ctx.player().getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (gf::game::items::idOf(s) == itemId) n += s.mCount;
    }
    return n;
}

/// The collision boxes of the block at `p`, relative to the block (0..1).
std::vector<AABB> collisionBoxes(Context& ctx, GlobalPos const& p) {
    std::vector<AABB> boxes;
    auto*             region = ctx.region();
    if (!region) return boxes;
    BlockPos     bp = gf::game::blockPos(p);
    Block const& b  = region->getBlock(bp);
    AABB         all{Vec3{bp.x - 1.f, bp.y - 1.f, bp.z - 1.f}, Vec3{bp.x + 2.f, bp.y + 2.f, bp.z + 2.f}};
    b.getBlockType().addCollisionShapes(b, *region, bp, &all, boxes, {});
    for (auto& box : boxes) {
        box.min = Vec3{box.min.x - bp.x, box.min.y - bp.y, box.min.z - bp.z};
        box.max = Vec3{box.max.x - bp.x, box.max.y - bp.y, box.max.z - bp.z};
    }
    return boxes;
}

AABB outline(Context& ctx, GlobalPos const& p) {
    auto*        region = ctx.region();
    BlockPos     bp     = gf::game::blockPos(p);
    Block const& b      = region->getBlock(bp);
    AABB         buf;
    AABB         o = b.getBlockType().getOutline(b, *region, bp, buf);
    return AABB{Vec3{o.min.x - bp.x, o.min.y - bp.y, o.min.z - bp.z}, Vec3{o.max.x - bp.x, o.max.y - bp.y, o.max.z - bp.z}};
}

bool near(float a, float b) { return std::abs(a - b) < 0.01f; }

std::string boxText(AABB const& b) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "[%.3f %.3f %.3f .. %.3f %.3f %.3f]", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
    return buf;
}

bool anyBoxContains(std::vector<AABB> const& boxes, Vec3 const& v) {
    return std::any_of(boxes.begin(), boxes.end(), [&](AABB const& b) {
        return v.x > b.min.x && v.x < b.max.x && v.y > b.min.y && v.y < b.max.y && v.z > b.min.z && v.z < b.max.z;
    });
}

} // namespace

// ---- logs ---------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.strip_logs", [](Context& ctx) -> ll::coro::CoroTask<> {
    // AxeItem.STRIPPABLES (RegistrationEvents): every TF log and wood strips, keeping the axis
    (void)co_await ctx.flatArea(400, 5);
    auto p = ctx.testPos(400, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    for (auto w : ww::kTfWoods) {
        for (auto const* kind : {"_log", "_wood"}) {
            std::string id       = std::string(kTf) + std::string(w) + kind;
            std::string stripped = std::string(kTf) + "stripped_" + std::string(w) + kind;
            ctx.require(ctx.setBlock(p, id, {{"twilightforest:axis", "x"}}), id + " placed on its side");
            co_await click(ctx, p, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:iron_axe", 1);
            ctx.check(ctx.blockAt(p) == stripped, "the axe strips " + id + " (" + ctx.blockAt(p) + ")");
            ctx.check(st(ctx, p, "twilightforest:axis") == "x", stripped + " keeps the axis");
        }
    }
});

// ---- banisters ----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.banister", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(410, 5);
    auto ground = ctx.testPos(410, 2).offset(0, -1, 0);
    ctx.require(co_await ctx.ensureLoaded(ground), "test area loaded");
    constexpr char kId[] = "twilightforest:canopy_banister";
    // getStateForPlacement: the player's horizontal direction, tall
    auto placed = co_await place(ctx, ground, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, kId);
    ctx.require(placed && ctx.blockAt(*placed) == kId, "a banister placed from its item");
    auto b = *placed;
    ctx.check(st(ctx, b, "twilightforest:facing") == "north", "it faces the way the player looks (north): " + st(ctx, b, "twilightforest:facing"));
    ctx.check(st(ctx, b, "twilightforest:shape") == "tall", "it is tall");
    // an axe cycles short -> tall -> connected -> short; back at tall the extension toggles
    std::vector<std::pair<std::string, std::string>> expect{{"connected", "false"}, {"short", "false"}, {"tall", "true"}, {"connected", "true"}};
    for (auto const& [shape, extended] : expect) {
        co_await click(ctx, b, Facing::South, Vec3{0.5f, 0.9f, 1.0f}, "minecraft:stone_axe", 1);
        ctx.check(st(ctx, b, "twilightforest:shape") == shape && st(ctx, b, "twilightforest:extended") == extended,
                  "axe cycle -> " + shape + "/" + extended + " (" + st(ctx, b, "twilightforest:shape") + "/" + st(ctx, b, "twilightforest:extended") + ")");
    }
    // a banister placed under another one is connected; placing a non-short banister below an
    // extended one un-extends it
    auto upper = at(b, 0, 2, 0);
    ctx.require(ctx.setBlock(upper, kId, {{"twilightforest:shape", "tall"}, {"twilightforest:extended", true}}), "an extended banister above");
    co_await ctx.ticks(1);
    auto wallAt = at(b, 0, 1, 1);
    ctx.require(ctx.setBlock(wallAt, "minecraft:stone"), "a stone to place against");
    auto lower = co_await place(ctx, wallAt, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, kId);
    ctx.require(lower && *lower == at(b, 0, 1, 0), "a banister placed between them");
    co_await ctx.ticks(2);
    ctx.check(st(ctx, *lower, "twilightforest:shape") == "connected", "under a banister it is connected (" + st(ctx, *lower, "twilightforest:shape") + ")");
    ctx.check(st(ctx, upper, "twilightforest:extended") == "false", "the extended banister above un-extends");
    // with a non-short banister below, the axe never extends
    for (int i = 0; i < 3; ++i) co_await click(ctx, upper, Facing::South, Vec3{0.5f, 0.9f, 1.0f}, "minecraft:stone_axe", 1);
    ctx.check(st(ctx, upper, "twilightforest:shape") == "tall" && st(ctx, upper, "twilightforest:extended") == "false", "over a non-short banister a full cycle stays un-extended");
    ctx.cleanupBlock(upper);
    ctx.cleanupBlock(*lower);
    ctx.cleanupBlock(b);
});

// ---- drying racks -------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.drying_rack", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(420, 5);
    auto p = ctx.testPos(420, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(rack::recipeFor(*gf::game::items::make("minecraft:kelp")) != nullptr, "the drying recipes are loaded");
    ctx.check(gf::recipes::ofType("twilightforest:drying").size() >= 35, "35 drying recipes (" + std::to_string(gf::recipes::ofType("twilightforest:drying").size()) + ")");
    constexpr char kRack[] = "twilightforest:oak_drying_rack";
    ctx.require(ctx.setBlock(p, kRack, {{"twilightforest:facing", "north"}}), "a drying rack");
    co_await ctx.ticks(1);
    // an empty hand on an empty rack does nothing
    co_await click(ctx, p, Facing::South, Vec3{0.5f, 0.9f, 1.0f}, "");
    ctx.check(rack::at(p) && gf::game::items::isEmpty(rack::at(p)->item), "an empty hand leaves the rack empty");
    // kelp: one item goes on, 3600 ticks
    co_await click(ctx, p, Facing::South, Vec3{0.5f, 0.9f, 1.0f}, "minecraft:kelp", 4);
    auto r = rack::at(p);
    ctx.require(r && gf::game::items::idOf(r->item) == "minecraft:kelp" && r->item.mCount == 1, "one kelp on the rack");
    ctx.check(carried(ctx, "minecraft:kelp") == 3, "the player gave one kelp (" + std::to_string(carried(ctx, "minecraft:kelp")) + " left)");
    ctx.check(r->totalDryTime == 3600 && r->drying, "kelp dries in 3600 ticks (" + std::to_string(r->totalDryTime) + ")");
    co_await ctx.ticks(10);
    r = rack::at(p);
    ctx.check(r && r->dryTime >= 8 && r->dryTime <= 14, "dry_time counts every tick (" + std::to_string(r ? r->dryTime : -1) + ")");
    // any click takes it back
    co_await click(ctx, p, Facing::South, Vec3{0.5f, 0.9f, 1.0f}, "minecraft:stick", 1);
    ctx.check(rack::at(p) && gf::game::items::isEmpty(rack::at(p)->item), "a click takes the kelp back");
    ctx.check(carried(ctx, "minecraft:kelp") >= 1, "the kelp went to the player");
    // an item without a recipe: the default 6000 ticks, not drying
    co_await click(ctx, p, Facing::South, Vec3{0.5f, 0.9f, 1.0f}, "minecraft:stick", 1);
    r = rack::at(p);
    ctx.check(r && gf::game::items::idOf(r->item) == "minecraft:stick" && r->totalDryTime == rack::kDefaultDryingTime && !r->drying, "no recipe: 6000 ticks, not drying");
    // the automation handler: extract works when not drying
    auto h = gf::inventory::find(*ctx.region(), gf::game::blockPos(p));
    ctx.require(h.has_value(), "the rack is an inventory");
    ctx.check(h->slots == 1, "one slot");
    auto took = h->extract(p, 0, 64, false);
    ctx.check(gf::game::items::idOf(took) == "minecraft:stick", "an idle rack gives its item to automation");
    // coral dries in 40 ticks; extraction is refused while drying; an insert into a full rack is refused
    auto coral = gf::game::items::make("minecraft:tube_coral", 3);
    ctx.require(coral.has_value(), "tube coral");
    auto rest = h->insert(p, *coral, false);
    ctx.check(rest.mCount == 2, "automation inserts one item");
    ctx.check(h->insert(p, *coral, true).mCount == 3, "a full rack takes nothing");
    ctx.check(gf::game::items::isEmpty(h->extract(p, 0, 1, false)), "no extraction while drying");
    co_await ctx.ticks(45);
    r = rack::at(p);
    ctx.check(r && gf::game::items::idOf(r->item) == "minecraft:dead_tube_coral", "tube coral dried into dead tube coral after 40 ticks (" + (r ? gf::game::items::idOf(r->item) : std::string("-")) + ")");
    // waterlogging drops the item, the rack stays empty; no drying in water
    auto coral2 = gf::game::items::make("minecraft:brain_coral");
    h->set(p, 0, *coral2);
    clearItemsNear(ctx, p);
    auto const* water = gf::game::blockWith("minecraft:water");
    ctx.require(water != nullptr, "water");
    ctx.region()->setExtraBlock(gf::game::blockPos(p), *water, 3);
    co_await ctx.ticks(3);
    r = rack::at(p);
    ctx.check(ctx.blockAt(p) == kRack && r && gf::game::items::isEmpty(r->item), "a water source in the rack empties it, the rack stays");
    ctx.check(droppedNear(ctx, p, "minecraft:brain_coral"), "the brain coral dropped");
    clearItemsNear(ctx, p);
    h->set(p, 0, *coral2);
    co_await ctx.ticks(50);
    r = rack::at(p);
    ctx.check(r && gf::game::items::idOf(r->item) == "minecraft:brain_coral" && !r->drying, "nothing dries while waterlogged");
    // draining drops it again
    if (auto const* air = gf::game::blockWith("minecraft:air")) ctx.region()->setExtraBlock(gf::game::blockPos(p), *air, 3);
    co_await ctx.ticks(3);
    ctx.check(droppedNear(ctx, p, "minecraft:brain_coral"), "losing the water drops the item");
    clearItemsNear(ctx, p);
    // breaking the rack drops what it holds
    h->set(p, 0, *gf::game::items::make("minecraft:wet_sponge"));
    r = rack::at(p);
    ctx.check(r && r->totalDryTime == 2400, "a wet sponge dries in 2400 ticks (" + std::to_string(r ? r->totalDryTime : -1) + ")");
    gf::game::clearBlock(*ctx.region(), gf::game::blockPos(p));
    co_await ctx.ticks(3);
    ctx.check(droppedNear(ctx, p, "minecraft:wet_sponge"), "a broken rack drops its item");
    clearItemsNear(ctx, p);
    // placement faces the player's horizontal direction
    auto placed = co_await place(ctx, at(p, 0, -1, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "twilightforest:dark_drying_rack");
    ctx.check(placed && ctx.blockAt(*placed) == "twilightforest:dark_drying_rack" && st(ctx, *placed, "twilightforest:facing") == "north",
              "a placed rack faces the player's direction (" + (placed ? st(ctx, *placed, "twilightforest:facing") : std::string("-")) + ")");
    if (placed) ctx.cleanupBlock(*placed);
});

// ---- hollow logs --------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.hollow_logs", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(440, 5);
    auto p = ctx.testPos(440, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto const* oak = ww::hollowLogOf("twilightforest:hollow_oak_log");
    ctx.require(oak != nullptr, "the oak hollow log set");
    // HollowLogItem: the top face places the vertical log, a side face the horizontal one
    auto v = co_await place(ctx, at(p, 0, -1, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, oak->item);
    ctx.require(v && ctx.blockAt(*v) == oak->vertical, "the item on a top face places the vertical log (" + (v ? ctx.blockAt(*v) : std::string("-")) + ")");
    auto hpos = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(hpos, "minecraft:planks"), "a wall");
    auto h = co_await place(ctx, hpos, Facing::West, Vec3{0.0f, 0.5f, 0.5f}, oak->item);
    ctx.require(h && ctx.blockAt(*h) == oak->horizontal, "the item on a side face places the horizontal log (" + (h ? ctx.blockAt(*h) : std::string("-")) + ")");
    ctx.check(st(ctx, *h, "twilightforest:axis") == "x", "along the clicked face's axis (" + st(ctx, *h, "twilightforest:axis") + ")");
    ctx.cleanupBlock(hpos);
    auto hl = *h; // at p + (1, 0, 0), axis x: the tunnel opens west and east
    // moss patch -> moss, grass -> moss_and_grass, shears -> empty (+ drops)
    Vec3 inside{0.0f, 0.5f, 0.5f};
    co_await click(ctx, hl, Facing::West, inside, "twilightforest:moss_patch", 2);
    ctx.check(st(ctx, hl, "twilightforest:variant") == "moss", "a moss patch carpets the log (" + st(ctx, hl, "twilightforest:variant") + ")");
    ctx.check(carried(ctx, "twilightforest:moss_patch") == 1, "one moss patch used");
    co_await click(ctx, hl, Facing::West, inside, "minecraft:short_grass", 2);
    ctx.check(st(ctx, hl, "twilightforest:variant") == "moss_and_grass", "grass on the moss (" + st(ctx, hl, "twilightforest:variant") + ")");
    clearItemsNear(ctx, hl);
    co_await click(ctx, hl, Facing::West, inside, "minecraft:shears", 1);
    ctx.check(st(ctx, hl, "twilightforest:variant") == "empty", "shears clear it");
    ctx.check(droppedNear(ctx, hl, "twilightforest:moss_patch") && droppedNear(ctx, hl, "minecraft:short_grass"), "the moss patch and the grass drop");
    clearItemsNear(ctx, hl);
    // snowball -> snow, shovel -> empty + a snowball
    co_await click(ctx, hl, Facing::West, inside, "minecraft:snowball", 2);
    ctx.check(st(ctx, hl, "twilightforest:variant") == "snow", "a snowball fills it with snow");
    co_await click(ctx, hl, Facing::West, inside, "minecraft:iron_shovel", 1);
    ctx.check(st(ctx, hl, "twilightforest:variant") == "empty" && droppedNear(ctx, hl, "minecraft:snowball"), "a shovel digs the snow out (a snowball drops)");
    clearItemsNear(ctx, hl);
    // a click outside the tunnel (the top face) does nothing
    co_await click(ctx, hl, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "twilightforest:moss_patch", 2);
    ctx.check(st(ctx, hl, "twilightforest:variant") == "empty", "a click outside the tunnel is not inside");
    // vertical: vine -> climbable vine, shears -> back; ladder -> climbable ladder facing the clicked face
    auto vl = *v;
    co_await click(ctx, vl, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:vine", 2);
    ctx.check(ctx.blockAt(vl) == oak->climbable && st(ctx, vl, "twilightforest:variant") == "vine", "a vine makes it climbable (" + ctx.blockAt(vl) + ")");
    ctx.check(st(ctx, vl, "twilightforest:facing") == "south", "a top click faces away from the player (south: " + st(ctx, vl, "twilightforest:facing") + ")");
    clearItemsNear(ctx, vl);
    co_await click(ctx, vl, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:shears", 1);
    ctx.check(ctx.blockAt(vl) == oak->vertical && droppedNear(ctx, vl, "minecraft:vine"), "shears take the vine back (" + ctx.blockAt(vl) + ")");
    clearItemsNear(ctx, vl);
    co_await click(ctx, vl, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:ladder", 2);
    ctx.check(ctx.blockAt(vl) == oak->climbable && st(ctx, vl, "twilightforest:variant") == "ladder", "a ladder makes it climbable (" + st(ctx, vl, "twilightforest:variant") + ")");
    // the climbable log climbs (#minecraft:climbable, BlockProperty::Climbable)
    ctx.check(gf::tags::has(gf::tags::Registry::Block, "minecraft:climbable", oak->climbable), "the climbable log is in #minecraft:climbable");
    auto const* cb = gf::game::blockWith(oak->climbable);
    ctx.check(cb && (static_cast<std::uint64_t>(cb->getBlockType().mProperties) & static_cast<std::uint64_t>(BlockProperty::Climbable)) != 0, "the climbable log's block type is climbable");
    clearItemsNear(ctx, vl);
    co_await click(ctx, vl, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:shears", 1);
    ctx.check(ctx.blockAt(vl) == oak->vertical && droppedNear(ctx, vl, "minecraft:ladder"), "shears take the ladder back");
    clearItemsNear(ctx, vl);
    ctx.cleanupBlock(vl);
    ctx.cleanupBlock(hl);
});

// ---- shapes -------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.shapes", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)co_await ctx.flatArea(460, 5);
    auto p = ctx.testPos(460, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // DryingRackBlock.SHAPES: the bar against the facing side
    struct RackCase {
        char const* facing;
        float       minX, minZ, maxX, maxZ;
    };
    for (auto const& c : {RackCase{"north", 0, 0, 1, 0.25f}, RackCase{"south", 0, 0.75f, 1, 1}, RackCase{"west", 0, 0, 0.25f, 1}, RackCase{"east", 0.75f, 0, 1, 1}}) {
        ctx.require(ctx.setBlock(p, "twilightforest:oak_drying_rack", {{"twilightforest:facing", c.facing}}), "a rack");
        auto boxes = collisionBoxes(ctx, p);
        bool ok    = boxes.size() == 1 && near(boxes[0].min.x, c.minX) && near(boxes[0].min.z, c.minZ) && near(boxes[0].max.x, c.maxX) && near(boxes[0].max.z, c.maxZ)
               && near(boxes[0].min.y, 0.75f);
        ctx.check(ok, std::string("a ") + c.facing + " rack's bar " + (boxes.empty() ? std::string("none") : boxText(boxes[0])));
        AABB o = outline(ctx, p);
        ctx.check(near(o.min.z, c.minZ) && near(o.max.x, c.maxX), std::string("a ") + c.facing + " rack's outline " + boxText(o));
    }
    // BanisterBlock: a north tall banister has its rail at z 0..4 px, y 12..16 px
    ctx.require(ctx.setBlock(p, "twilightforest:oak_banister", {{"twilightforest:facing", "north"}, {"twilightforest:shape", "tall"}}), "a banister");
    auto boxes = collisionBoxes(ctx, p);
    ctx.check(boxes.size() == 3 && anyBoxContains(boxes, Vec3{0.5f, 0.9f, 0.1f}) && !anyBoxContains(boxes, Vec3{0.5f, 0.5f, 0.5f}) && !anyBoxContains(boxes, Vec3{0.5f, 0.9f, 0.9f}),
              "a north tall banister: rail and posts on the north side (" + std::to_string(boxes.size()) + " boxes)");
    ctx.require(ctx.setBlock(p, "twilightforest:oak_banister", {{"twilightforest:facing", "east"}, {"twilightforest:shape", "short"}}), "a banister");
    boxes = collisionBoxes(ctx, p);
    ctx.check(anyBoxContains(boxes, Vec3{0.9f, 0.4f, 0.5f}) && !anyBoxContains(boxes, Vec3{0.1f, 0.4f, 0.5f}), "an east short banister: its rail on the east side");
    // hollow logs: the tunnel is free, the walls collide
    ctx.require(ctx.setBlock(p, "twilightforest:hollow_oak_log_horizontal", {{"twilightforest:axis", "x"}}), "a horizontal hollow log");
    boxes = collisionBoxes(ctx, p);
    ctx.check(!anyBoxContains(boxes, Vec3{0.5f, 0.5f, 0.5f}) && anyBoxContains(boxes, Vec3{0.5f, 0.5f, 0.02f}) && anyBoxContains(boxes, Vec3{0.5f, 0.02f, 0.5f}),
              "an x hollow log: free inside, walls at z 0 and the floor");
    ctx.require(ctx.setBlock(p, "twilightforest:hollow_oak_log_horizontal", {{"twilightforest:axis", "z"}}), "a horizontal hollow log");
    boxes = collisionBoxes(ctx, p);
    ctx.check(!anyBoxContains(boxes, Vec3{0.5f, 0.5f, 0.5f}) && anyBoxContains(boxes, Vec3{0.02f, 0.5f, 0.5f}), "a z hollow log: walls at x 0");
    ctx.require(ctx.setBlock(p, "twilightforest:hollow_oak_log_vertical"), "a vertical hollow log");
    boxes = collisionBoxes(ctx, p);
    ctx.check(!anyBoxContains(boxes, Vec3{0.5f, 0.5f, 0.5f}) && anyBoxContains(boxes, Vec3{0.02f, 0.5f, 0.5f}), "a vertical hollow log: an open shaft");
    ctx.require(ctx.setBlock(p, "twilightforest:hollow_oak_log_climbable", {{"twilightforest:facing", "south"}, {"twilightforest:variant", "ladder"}}), "a laddered hollow log");
    boxes = collisionBoxes(ctx, p);
    ctx.check(anyBoxContains(boxes, Vec3{0.5f, 0.5f, 0.15f}) && !anyBoxContains(boxes, Vec3{0.5f, 0.5f, 0.5f}), "a south ladder: its plate on the north side (LADDER_SOUTH)");
    ctx.cleanupBlock(p);
});

// ---- canopy bookshelf ---------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.bookshelf", [](Context& ctx) -> ll::coro::CoroTask<> {
    // #minecraft:enchantment_power_provider: a canopy bookshelf on the table's ring counts like a
    // vanilla bookshelf (EnchantUtils::getBookCasePositions), unless something solid stands between
    (void)co_await ctx.flatArea(450, 5);
    auto t = ctx.testPos(450, 2);
    ctx.require(co_await ctx.ensureLoaded(t), "test area loaded");
    ctx.require(ctx.setBlock(t, "minecraft:enchanting_table"), "an enchanting table");
    auto vanillaShelf = at(t, -2, 0, 0);
    auto canopyShelf  = at(t, 2, 0, 0);
    ctx.require(ctx.setBlock(vanillaShelf, "minecraft:bookshelf") && ctx.setBlock(canopyShelf, "twilightforest:canopy_bookshelf"), "two bookshelves");
    BlockPos tb = gf::game::blockPos(t);
    auto     list = EnchantUtils::getBookCasePositions(*ctx.region(), Vec3{float(tb.x), float(tb.y), float(tb.z)});
    std::string seen;
    for (auto const& v : list) seen += "(" + std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z) + ")";
    ctx.info("bookcase positions: " + seen);
    ctx.check(list.size() == 2, "the table counts both bookshelves (" + std::to_string(list.size()) + ")");
    ctx.check(gf::feature::woodwork::tfBookshelvesAround(*ctx.region(), tb).size() == 1, "one TF bookshelf around the table");
    ctx.require(ctx.setBlock(at(t, 1, 0, 0), "minecraft:stone"), "a stone between");
    ctx.check(gf::feature::woodwork::tfBookshelvesAround(*ctx.region(), tb).empty(), "a solid block between blocks it (enchantment_power_transmitter)");
    ctx.check(gf::tags::has(gf::tags::Registry::Block, "minecraft:enchantment_power_provider", "twilightforest:canopy_bookshelf"), "the canopy bookshelf provides enchantment power");
});

// ---- chests and recipes -------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("woodwork.chest_pairs", [](Context& ctx) -> ll::coro::CoroTask<> {
    // ChestBlock.getStateForPlacement (F11's chest family): a second chest beside the first pairs
    (void)co_await ctx.flatArea(470, 5);
    auto ground = ctx.testPos(470, 2).offset(0, -1, 0);
    ctx.require(co_await ctx.ensureLoaded(ground), "test area loaded");
    for (auto const* id : {"twilightforest:canopy_chest", "twilightforest:dark_trapped_chest"}) {
        auto standAt = Vec3{ground.x + 1.0f, float(ground.y + 1), ground.z + 3.5f};
        auto a       = co_await place(ctx, ground, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, id, standAt);
        auto b       = co_await place(ctx, at(ground, 1, 0, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, id, standAt);
        ctx.require(a && b, std::string(id) + ": two chests placed");
        auto ta = st(ctx, *a, "twilightforest:type");
        auto tb = st(ctx, *b, "twilightforest:type");
        ctx.check((ta == "left" && tb == "right") || (ta == "right" && tb == "left"), std::string(id) + " pairs (" + ta + ", " + tb + ")");
        auto h = gf::inventory::find(*ctx.region(), gf::game::blockPos(*a));
        ctx.check(h && h->slots == 27, std::string(id) + " is a 27-slot container");
        gf::game::clearBlock(*ctx.region(), gf::game::blockPos(*a));
        gf::game::clearBlock(*ctx.region(), gf::game::blockPos(*b));
        co_await ctx.ticks(2);
    }
});

GF_REGISTER_SELFTEST("woodwork.recipes", [](Context& ctx) {
    // a sample of every recipe kind U4 owns (R4): wood set crafting, stonecutting, drying
    for (auto const* id : {"twilightforest:wood/canopy_planks", "twilightforest:wood/dark_banister", "twilightforest:wood/mining_chest", "twilightforest:drying/kelp",
                           "twilightforest:drying/sponge", "twilightforest:tanned_leather", "twilightforest:sorting_boat"}) {
        ctx.check(gf::recipes::byId(id) != nullptr, std::string("recipe ") + id + " is loaded");
    }
    struct Dry {
        char const* input;
        char const* result;
        int         time;
    };
    for (auto const& d : {Dry{"minecraft:kelp", "minecraft:dried_kelp", 3600}, Dry{"minecraft:mud", "minecraft:clay", 7200}, Dry{"minecraft:wet_sponge", "minecraft:sponge", 2400},
                          Dry{"twilightforest:treated_leather", "twilightforest:tanned_leather", 6000}, Dry{"minecraft:slime_ball", "twilightforest:gelatinous_slime_drop", 6000},
                          Dry{"minecraft:bread", "twilightforest:stale_bread", 6000}, Dry{"minecraft:oak_sapling", "minecraft:deadbush", 7200},
                          Dry{"minecraft:fire_coral_fan", "minecraft:dead_fire_coral_fan", 40}}) {
        auto in = gf::game::items::make(d.input);
        if (!in) {
            ctx.check(false, std::string("item ") + d.input);
            continue;
        }
        auto const* r = rack::recipeFor(*in);
        ctx.check(r && r->resultItem == d.result, std::string(d.input) + " dries into " + d.result + " (" + (r ? r->resultItem : std::string("none")) + ")");
        if (r) ctx.check(r->data.value("filter_time", 0) == d.time, std::string(d.input) + " dries in " + std::to_string(d.time) + " ticks");
    }
});

// ---- boats --------------------------------------------------------------------------------------------

namespace {
int woodProperty(Actor& a) {
    auto props = a.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return -1;
    auto v = props->mPropertyContainer->getValueVariant(HashedString::computeHash("twilightforest:wood"));
    if (!v) return -2;
    auto const* i = std::get_if<int>(&*v);
    return i ? *i : -3;
}

std::vector<Actor*> boatsNear(Context& ctx, GlobalPos const& p, char const* type) {
    std::vector<Actor*> out;
    auto*               region = ctx.region();
    if (!region) return out;
    AABB box{Vec3{p.x - 4.f, p.y - 3.f, p.z - 4.f}, Vec3{p.x + 5.f, p.y + 4.f, p.z + 5.f}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == type) out.push_back(a);
    return out;
}
} // namespace

GF_REGISTER_SELFTEST("woodwork.boats", [](Context& ctx) -> ll::coro::CoroTask<> {
    // BoatItem#use on water: a TF boat of the item's wood; riding it; breaking it drops the TF item
    (void)co_await ctx.flatArea(490, 5);
    auto c = ctx.testPos(490, 2);
    ctx.require(co_await ctx.ensureLoaded(c), "test area loaded");
    // a 3 x 3 pond in a ring of planks
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dz = -2; dz <= 2; ++dz) {
            bool rim = std::abs(dx) == 2 || std::abs(dz) == 2;
            ctx.require(ctx.setBlock(at(c, dx, 0, dz), rim ? "minecraft:planks" : "minecraft:water"), "the pond");
        }
    }
    co_await ctx.ticks(2);
    struct Kind {
        char const* item;
        char const* entity;
        int         wood;
    };
    for (auto const& k : {Kind{"twilightforest:canopy_boat", "twilightforest:boat", 1}, Kind{"twilightforest:sorting_chest_boat", "twilightforest:chest_boat", 7}}) {
        auto& sp = simPlayer(ctx);
        sp.teleport(Vec3{c.x + 0.5f, float(c.y + 1), c.z + 3.5f}, sp.getDimensionId());
        sp.simulateLookAt(Vec3{c.x + 0.5f, c.y + 0.9f, c.z + 0.5f}, sim::LookDuration::Instant);
        auto item = gf::game::items::make(k.item, 1);
        ctx.require(item.has_value(), std::string(k.item) + " exists");
        sp.simulateSetItem(*item, true, 0);
        co_await ctx.ticks(kClickGap);
        // the player uses the item as a client does (the game's item use, then the interactions)
        simPlayer(ctx).simulateUseItemInSlot(0);
        co_await ctx.ticks(2);
        auto boats = boatsNear(ctx, c, k.entity);
        ctx.require(boats.size() == 1, std::string(k.item) + " placed one " + k.entity + " (" + std::to_string(boats.size()) + ")");
        ctx.check(carried(ctx, k.item) == 0, "the boat item was used up");
        Actor* boat = boats[0];
        ctx.check(woodProperty(*boat) == k.wood, std::string(k.entity) + " has wood " + std::to_string(k.wood) + " (" + std::to_string(woodProperty(*boat)) + ")");
        auto   pos  = boat->getPosition();
        ctx.check(std::abs(pos.x - (c.x + 0.5f)) < 1.6f && std::abs(pos.z - (c.z + 0.5f)) < 1.6f, "the boat floats in the pond");
        // the player gets in
        auto& p2 = ctx.player();
        p2.mGameMode->interact(*boat, boat->getPosition(), HandSlot::Mainhand);
        co_await ctx.ticks(2);
        boats = boatsNear(ctx, c, k.entity);
        ctx.require(!boats.empty(), "the boat is still there");
        ctx.check(ctx.player().getVehicle() == boats[0], "the player rides the boat");
        // out, and a few hits break it
        ctx.player().stopRiding(true, false, false, false);
        co_await ctx.ticks(2);
        clearItemsNear(ctx, c);
        for (int i = 0; i < 10 && !boatsNear(ctx, c, k.entity).empty(); ++i) {
            auto bs = boatsNear(ctx, c, k.entity);
            ctx.player().mGameMode->attack(*bs[0], bs[0]->getPosition());
            co_await ctx.ticks(3);
        }
        ctx.check(boatsNear(ctx, c, k.entity).empty(), "the boat broke");
        co_await ctx.ticks(2);
        ctx.check(droppedNear(ctx, c, k.item) || carried(ctx, k.item) == 1, std::string("it dropped ") + k.item + " (or the player picked it up)");
        bool vanillaCarried = false;
        for (auto const* v : {"minecraft:oak_boat", "minecraft:oak_chest_boat"}) vanillaCarried |= carried(ctx, v) > 0;
        ctx.check(!vanillaCarried, "the player picked up no vanilla boat");
        bool vanilla = false;
        for (auto* a : itemsNear(ctx, c)) {
            auto id = gf::game::items::idOf(static_cast<ItemActor*>(a)->item());
            if (id.starts_with("minecraft:") && id.ends_with("boat")) vanilla = true;
        }
        for (auto* a : itemsNear(ctx, at(c, 0, 0, 2))) {
            auto id = gf::game::items::idOf(static_cast<ItemActor*>(a)->item());
            if (id.starts_with("minecraft:") && id.ends_with("boat")) vanilla = true;
        }
        ctx.check(!vanilla, "no vanilla boat item dropped");
        clearItemsNear(ctx, c);
        clearItemsNear(ctx, at(c, 0, 0, 2));
    }
    // a boat summoned with its wood's spawn event (commands, the client scenario)
    auto r = gf::selftest::runServerCommand("summon twilightforest:boat " + std::to_string(c.x) + " " + std::to_string(c.y) + " " + std::to_string(c.z) + " 0 0 twilightforest:mining", ctx.dimension());
    ctx.check(r.ok, "summon with the wood's event: " + r.messages);
    co_await ctx.ticks(2);
    auto summoned = boatsNear(ctx, c, "twilightforest:boat");
    ctx.check(summoned.size() == 1 && woodProperty(*summoned[0]) == 6, "the summoned boat is a mining boat (" + (summoned.empty() ? std::string("none") : std::to_string(woodProperty(*summoned[0]))) + ")");
    for (auto* b : summoned) b->remove();
});

// ---- a showcase for the client scenario's pictures -----------------------------------------------------

namespace {
/// Blocks the showcase put down, removed once the deadline tick passed (the 'full' run leaves
/// nothing behind; the client scenario photographs them first).
struct Showcase {
    std::vector<GlobalPos>  blocks;
    std::int64_t            until = 0;
    ll::event::ListenerPtr  tick;
};
Showcase& showcase() {
    static Showcase s;
    return s;
}
std::int64_t serverTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::int64_t>(level->getCurrentServerTick().tickID) : 0;
}
void clearShowcase() {
    auto& s = showcase();
    for (auto const& p : s.blocks) {
        if (auto* region = gf::game::regionOf(p.dim)) {
            BlockPos bp = gf::game::blockPos(p);
            if (!region->hasChunksAt(bp, 0, false)) continue;
            if (auto h = gf::inventory::find(*region, bp); h && h->set) h->set(p, 0, ItemStack{});
            gf::game::clearBlock(*region, bp);
        }
    }
    s.blocks.clear();
    if (s.tick) ll::event::EventBus::getInstance().removeListener(s.tick);
    s.tick = nullptr;
}
} // namespace

GF_REGISTER_SELFTEST("woodwork.fixture_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    // five racks against a wall holding items (kelp drying), in the unit's row (dz 2..3), for the
    // client scenario to photograph; removed again after 60 seconds
    auto p = ctx.testPos(495, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearShowcase();
    static bool hooked = false;
    if (!std::exchange(hooked, true)) gf::game::db::onWorldClosed([] {
            auto& sc = showcase();
            sc.blocks.clear(); // another world's positions
            if (sc.tick) ll::event::EventBus::getInstance().removeListener(sc.tick);
            sc.tick = nullptr;
        });
    auto* region = ctx.region();
    char const* items[] = {"minecraft:kelp", "minecraft:rotten_flesh", "minecraft:oak_sapling", "twilightforest:treated_leather", "minecraft:oak_log"};
    auto const* rackBlock = gf::game::blockWithStates("twilightforest:oak_drying_rack", {{"twilightforest:facing", "south"}});
    auto const* wall      = gf::game::blockWith("minecraft:stone");
    ctx.require(region && rackBlock && wall, "blocks");
    auto& s = showcase();
    for (int i = 0; i < 5; ++i) {
        auto r = at(p, i, 1, 0);
        gf::game::setBlock(*region, gf::game::blockPos(at(r, 0, 0, 1)), *wall);
        gf::game::setBlock(*region, gf::game::blockPos(r), *rackBlock);
        s.blocks.push_back(r);
        s.blocks.push_back(at(r, 0, 0, 1));
        if (auto h = gf::inventory::find(*region, gf::game::blockPos(r)); h && h->set) {
            if (auto st = gf::game::items::make(items[i])) h->set(r, 0, *st);
        }
    }
    s.until = serverTick() + 20 * 60;
    s.tick  = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
        if (serverTick() < showcase().until) return;
        showcase().until = INT64_MAX;
        ia::nextTick([] { clearShowcase(); }); // not inside the listener being removed
    });
    co_await ctx.ticks(2);
    auto r0 = rack::at(at(p, 0, 1, 0));
    ctx.check(r0 && r0->drying, "the kelp is drying");
    gf::log().info("woodwork: showcase racks at {} {} {}", p.x, p.y + 1, p.z);
});


// ---- a rack across a restart (the server scenario runs a, restarts, runs b) ------------------------------

namespace {
constexpr char kPersistKey[] = "gf:woodwork:selftest_rack";
}

GF_REGISTER_SELFTEST("woodwork.persist_a_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(480, 4);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto* region = ctx.region();
    auto const* rackBlock = gf::game::blockWithStates("twilightforest:oak_drying_rack", {{"twilightforest:facing", "south"}});
    ctx.require(region && rackBlock, "a rack");
    gf::game::setBlock(*region, gf::game::blockPos(p), *rackBlock); // not restored: the read test removes it
    auto h = gf::inventory::find(*region, gf::game::blockPos(p));
    ctx.require(h.has_value(), "the rack's storage");
    h->set(p, 0, *gf::game::items::make("minecraft:kelp"));
    co_await ctx.ticks(20);
    auto r = rack::at(p);
    ctx.require(r && r->drying && r->dryTime >= 15, "the kelp is drying");
    gf::game::db::put(kPersistKey, {{"pos", {p.dim, p.x, p.y, p.z}}, {"dry_time", r->dryTime}});
    ctx.info("a drying rack written at " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z));
});

GF_REGISTER_SELFTEST("woodwork.persist_b_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto saved = gf::game::db::get(kPersistKey);
    if (!saved || !saved->is_object()) {
        ctx.info("no rack written by woodwork.persist_a_write: nothing to read");
        co_return;
    }
    auto const& a = (*saved)["pos"];
    GlobalPos   p{a[0].get<int>(), a[1].get<int>(), a[2].get<int>(), a[3].get<int>()};
    int         before = saved->value("dry_time", 0);
    ctx.require(co_await ctx.ensureLoaded(p), "the rack's area loaded");
    co_await ctx.ticks(10);
    auto r = rack::at(p);
    ctx.check(r && gf::game::items::idOf(r->item) == "minecraft:kelp", "the rack still holds its kelp");
    ctx.check(r && r->drying && r->dryTime > before, "it kept drying (" + std::to_string(r ? r->dryTime : -1) + " > " + std::to_string(before) + ")");
    ctx.check(r && r->totalDryTime == 3600, "and its total time");
    ctx.info("drying rack verified after the reload");
    gf::game::db::erase(kPersistKey);
    if (auto h = gf::inventory::find(*ctx.region(), gf::game::blockPos(p))) h->set(p, 0, ItemStack{});
    gf::game::clearBlock(*ctx.region(), gf::game::blockPos(p));
});

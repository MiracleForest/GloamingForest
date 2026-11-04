// Container screens for a real player (client e2e, tools/e2e/client/scenarios/combat.ps1; spike
// S3's screenshots): each test opens one screen for the first non-simulated player and leaves it
// open for the scenario to take a screenshot (the scenario closes it). Without a real player (the
// server harness, the 'full' run) they have nothing to show and pass.
//   combat.ui_loot_chest        a vanilla chest placed with a loot table (gf::loot::placeContainer):
//                               opening it rolls the Java table into the game's chest screen
//   combat.ui_native_container  the S3 probe block with a data-driven block actor and the injected
//                               game container: the game's container screen for a custom block
//   combat.ui_vanilla_container the vanilla clients' route for every client: the game's chest screen
//                               through the chest minecart proxy (27 slots)
//   combat.ui_chest_screen      a BlockData container: the client mod's chest screen (companions)
//                               or, on vanilla clients, the game's chest screen through the proxy
//   combat.ui_tf_double_chest   TF's twilight oak double chest (F11's families declare it a container
//                               of this contract) opened as a right click opens it: the client mod's
//                               54-slot "Large Chest" (the right half first); on vanilla clients the
//                               game's large chest screen (the minecart proxy behind a chest pair of
//                               the client)
//   combat.ui_tf_trapped_chest  TF's trapped chest, a right click: the client mod's chest screen, the
//                               game's chest screen through the proxy on vanilla clients
//   combat.ui_missing_toast     TF's MissingAdvancementToast for progress_lich (F12's toast on the
//                               client mod, the game's toast on vanilla clients)
//   combat.ui_fx_particles      TF's large_flame through gf::fx in front of the player for 5 s (the
//                               client mod: fx.particle, its native factory or resource pack effect;
//                               vanilla clients: the game's particle packets)
//   combat.ui_fx_portal         TF's portal particles (TFPortalBlock#animateTick: minecraft:portal) of a
//                               3-block row in front of the player for 5 s: they rise from the row and
//                               fall back onto it as Java's PortalParticle
//   combat.ui_fx_vanilla        every Java particle type gf::fx maps, in front of the player for 2 s
//                               (the content log must stay free of particle errors)
//   combat.ui_cleanup           removes what the tests above placed (the scenario runs it last)
//
// Rule R12 exception (client only): the blocks go right beside the real player, not in the test
// rows (the client must have the container in its loaded chunks and in reach, 8 blocks, for its
// screen to open). Nothing stays behind: every test first removes what an earlier one placed, the
// blocks are removed as soon as the player's screen is closed (or after five minutes), and
// combat.ui_cleanup removes them at once. Removing a block empties its container first (no drops),
// puts the block that was there back and takes back the fixture block's container declaration.

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <tuple>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/fx/FxInternal.h"
#include "feature/inventory/Inventory.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/Loot.h"
#include "feature/loot/LootInternal.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;

constexpr char kFixture[] = "gf_test:fixture_block";

Player* realPlayer() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Player* found = nullptr;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulatedPlayer() && !p.isClientSide()) {
            found = &p;
            return false;
        }
        return true;
    });
    return found;
}

/// A free spot next to the player, at its feet level.
BlockPos besidePlayer(Player& p, int dx, int dz) {
    auto const& at = p.getPosition();
    return BlockPos{static_cast<int>(std::floor(at.x)) + dx, static_cast<int>(std::floor(at.y - 1.62f)), static_cast<int>(std::floor(at.z)) + dz};
}

/// What the UI tests placed (the block that was there before) and whether they declared the
/// fixture block a container.
struct Placed {
    GlobalPos    pos;
    Block const* previous = nullptr;
};
struct UiState {
    std::vector<Placed> placed;
    bool                declaredFixture = false;
    std::uint64_t       watchGeneration = 0;
};
UiState& ui() {
    static UiState s;
    return s;
}

/// Places `block` at `bp` and remembers what was there.
bool place(BlockSource& region, BlockPos const& bp, Block const& block) {
    auto        pos      = gf::game::globalPos(region, bp);
    auto const* previous = &region.getBlock(bp);
    if (!gf::game::setBlock(region, bp, block)) return false;
    ui().placed.push_back({pos, previous});
    return true;
}

void declareFixture() {
    gf::inventory::declare({.block = kFixture, .rows = 3, .slots = 27, .title = "container.chest", .skin = ""});
    ui().declaredFixture = true;
}

/// Removes everything the UI tests placed: the containers are emptied first (nothing drops), the
/// blocks that were there come back, the fixture declaration goes.
int cleanupUi() {
    auto& s = ui();
    int   n = 0;
    for (auto it = s.placed.rbegin(); it != s.placed.rend(); ++it) {
        auto* region = gf::game::regionOf(it->pos.dim);
        auto  bp     = gf::game::blockPos(it->pos);
        if (!region || !region->hasChunksAt(bp, 0, false)) continue;
        if (gf::inventory::proxyOpen(it->pos)) gf::inventory::proxyEnd(it->pos);
        (void)gf::blockdata::take(it->pos); // no erase listeners: nothing drops
        if (auto* c = gf::inventory::nativeContainer(*region, bp))
            for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
        if (auto* actor = region->getBlockEntity(bp); actor && actor->getType() == BlockActorType::Chest) {
            if (auto* c = static_cast<ChestBlockActor*>(actor)->getContainer())
                for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, ItemStack{});
        }
        if (it->previous) gf::game::setBlock(*region, bp, *it->previous);
        else gf::game::clearBlock(*region, bp);
        ++n;
    }
    s.placed.clear();
    if (s.declaredFixture) {
        gf::inventory::unregisterBlock(kFixture);
        s.declaredFixture = false;
    }
    if (n) gf::log().info("combat ui: removed {} test block(s)", n);
    return n;
}

/// Whether `player` has a container screen open (the game's, the client mod's chest screen), or a
/// placed container still has an opener (a form).
bool screenOpen(Player& player) {
    if (player.mContainerManager.get() != nullptr || !gf::inventory::openedParts(player).empty()) return true;
    return std::any_of(ui().placed.begin(), ui().placed.end(), [](Placed const& p) { return gf::inventory::viewerCount(p.pos) > 0; });
}

/// Removes the test blocks once the player's screen is closed: watched from 5 s after the test
/// (the screen opens a few ticks later) for up to five minutes. A later UI test or
/// combat.ui_cleanup ends the watch.
void watchUntilClosed(std::int64_t playerId, std::uint64_t generation, int ticks) {
    gf::interactions::nextTick([playerId, generation, ticks] {
        if (ui().watchGeneration != generation || ui().placed.empty()) return;
        auto level = ll::service::getLevel();
        if (!level) return;
        ActorUniqueID id;
        id.rawID = playerId;
        auto* p  = level->getPlayer(id);
        if (ticks >= 100 && (!p || !screenOpen(*p) || ticks >= 6000)) {
            cleanupUi();
            return;
        }
        watchUntilClosed(playerId, generation, ticks + 1);
    });
}

/// Every UI test: the earlier test's blocks go first; this one's are watched.
void beginUiTest(Player& p) {
    cleanupUi();
    watchUntilClosed(p.getOrCreateUniqueID().rawID, ++ui().watchGeneration, 0);
}

} // namespace

// The block is placed first and opened a few ticks later: a container screen for a block the client
// has not received yet is refused by the client.
GF_REGISTER_SELFTEST("combat.ui_loot_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    auto  id     = p->getOrCreateUniqueID();
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, 2, 0);
    auto const* before = &region.getBlock(bp);
    ctx.require(gf::loot::placeContainer(region, bp, "minecraft:chest", 4, "gf_test:chests/fixture_chest", 20261102), "a loot chest beside the player");
    ui().placed.push_back({gf::game::globalPos(region, bp), before});
    co_await ctx.ticks(10);
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still there");
    auto& r2    = p->getDimensionBlockSource();
    auto* actor = r2.getBlockEntity(bp);
    ctx.require(actor && actor->getType() == BlockActorType::Chest, "the chest block actor");
    // what a right-click does (the game's use-on: the chest screen, the loot unpacked)
    auto& held   = const_cast<ItemStack&>(p->getSelectedItem());
    auto  result = p->mGameMode->useItemOn(held, bp, 1, Vec3{.5f, 1.f, .5f}, HandSlot::Mainhand, &r2.getBlock(bp), true);
    gf::log().info("combat ui: loot chest opened for {} (use-on {})", p->getRealName(), result.mSuccess ? "succeeded" : "refused");
});

GF_REGISTER_SELFTEST("combat.ui_native_container", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    if (!gf::game::blockWith("gf_s3:entity") || !gf::inventory::declared("gf_s3:entity")) {
        ctx.info("no S3 probe block in this run: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    auto  id     = p->getOrCreateUniqueID();
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, -2, 0);
    ctx.require(place(region, bp, *gf::game::blockWith("gf_s3:entity")), "the probe block beside the player");
    auto pos = gf::game::globalPos(region, bp);
    auto h   = gf::inventory::find(region, bp);
    ctx.require(h.has_value(), "the probe block is a container");
    ctx.require(gf::inventory::nativeContainer(region, bp) != nullptr, "it has the injected game container");
    h->set(pos, 0, *gf::game::items::make("minecraft:diamond", 7));
    h->set(pos, 4, *gf::game::items::make("minecraft:stick", 12));
    h->set(pos, 22, *gf::game::items::make("gf_test:fixture_item", 3));
    co_await ctx.ticks(10);
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still there");
    // spike S3: the game's own screen for the injected container (DataDrivenContainer type)
    int containerId = gf::inventory::openNativeScreen(*p, pos, 37);
    ctx.check(containerId >= 0, "the server opened the injected container (id " + std::to_string(containerId) + ")");
    gf::log().info("combat ui: native container opened for {}", p->getRealName());
});

// The vanilla clients' route for every client (S3): the game's chest screen through the chest
// minecart proxy, opened for the real player even when it runs the client mod.
GF_REGISTER_SELFTEST("combat.ui_vanilla_container", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    declareFixture();
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, 0, -2);
    ctx.require(place(region, bp, *gf::game::blockWith(kFixture)), "the fixture block beside the player");
    auto pos = gf::game::globalPos(region, bp);
    auto h   = gf::inventory::find(region, bp);
    ctx.require(h.has_value(), "the fixture block is a container");
    h->set(pos, 0, *gf::game::items::make("minecraft:gold_ingot", 9));
    h->set(pos, 13, *gf::game::items::make("minecraft:bread", 4));
    h->set(pos, 26, *gf::game::items::make("gf_test:fixture_item", 1));
    // as a player opens it: a moment later, once the chat that ran the test has closed (a client
    // refuses a container screen while another screen is up)
    auto id = p->getOrCreateUniqueID();
    co_await ctx.ticks(10);
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still there");
    ctx.require(gf::inventory::openVanillaContainer(*p, pos, gf::trFor(*p, "container.chest", "Chest")), "the proxy for the game's chest screen");
    gf::log().info("combat ui: vanilla container opened for {}", p->getRealName());
});

GF_REGISTER_SELFTEST("combat.ui_chest_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    // a BlockData container: the client mod's screen, or the proxy chest screen on a vanilla client
    beginUiTest(*p);
    declareFixture();
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, 0, 2);
    ctx.require(place(region, bp, *gf::game::blockWith(kFixture)), "the fixture block beside the player");
    auto pos = gf::game::globalPos(region, bp);
    auto h   = gf::inventory::find(region, bp);
    ctx.require(h.has_value(), "the fixture block is a container");
    h->set(pos, 0, *gf::game::items::make("minecraft:emerald", 5));
    h->set(pos, 10, *gf::game::items::make("minecraft:apple", 2));
    auto id = p->getOrCreateUniqueID();
    co_await ctx.ticks(10); // once the chat that ran the test has closed
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still there");
    ctx.require(gf::inventory::openChestLike(*p, pos, 3, "container.chest"), "the chest screen opened");
    gf::log().info("combat ui: chest screen opened for {}", p->getRealName());
});

namespace {

/// Fills the slots `stacks` names ({slot, item id, count}) of the container at `pos`.
void fill(GlobalPos const& pos, std::initializer_list<std::tuple<int, char const*, int>> stacks) {
    auto* region = gf::game::regionOf(pos.dim);
    auto  h      = region ? gf::inventory::find(*region, gf::game::blockPos(pos)) : std::nullopt;
    if (!h) return;
    for (auto const& [slot, id, count] : stacks)
        if (auto s = gf::game::items::make(id, count)) h->set(pos, slot, *s);
}

/// What a right click does: GameMode::useItemOn with the player's held item (the interactions
/// dispatch, then the inventory contract's chest handler).
bool useOn(Player& p, BlockPos const& bp) {
    auto& held   = const_cast<ItemStack&>(p.getSelectedItem());
    auto  result = p.mGameMode->useItemOn(held, bp, 1, Vec3{bp.x + .5f, bp.y + 1.f, bp.z + .5f}, HandSlot::Mainhand, nullptr, true);
    return result.mSuccess;
}

} // namespace

// TF's double chest, the halves placed north of the player facing it (south): a left half's partner
// is west of it (ChestBlock#getConnectedDirection).
GF_REGISTER_SELFTEST("combat.ui_tf_double_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    auto& region = p->getDimensionBlockSource();
    auto  lbp    = besidePlayer(*p, 1, -2);
    auto  rbp    = besidePlayer(*p, 0, -2);
    auto const* left  = gf::game::blockWithStates("twilightforest:twilight_oak_chest", {{"twilightforest:type", "left"}, {"twilightforest:facing", "south"}});
    auto const* right = gf::game::blockWithStates("twilightforest:twilight_oak_chest", {{"twilightforest:type", "right"}, {"twilightforest:facing", "south"}});
    ctx.require(left && right, "the twilight oak chest halves");
    ctx.require(place(region, lbp, *left) && place(region, rbp, *right), "a twilight oak double chest beside the player");
    auto lpos = gf::game::globalPos(region, lbp), rpos = gf::game::globalPos(region, rbp);
    ctx.require(gf::inventory::partsOf(lpos).size() == 2, "the halves pair");
    fill(rpos, {{0, "twilightforest:naga_scale", 7}, {4, "twilightforest:raven_feather", 12}, {13, "twilightforest:liveroot", 20}, {26, "twilightforest:torchberries", 5}});
    fill(lpos, {{0, "twilightforest:ironwood_ingot", 9}, {10, "twilightforest:steeleaf_ingot", 16}, {20, "twilightforest:fiery_ingot", 3}, {26, "twilightforest:arctic_fur", 4}});
    auto id = p->getOrCreateUniqueID();
    co_await ctx.ticks(10); // once the client has the blocks and the chat that ran the test has closed
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still there");
    bool used = useOn(*p, lbp);
    gf::log().info("combat ui: tf double chest opened for {} (use-on {})", p->getRealName(), used ? "succeeded" : "refused");
    ctx.check(gf::inventory::viewerCount(lpos) == 1 && gf::inventory::viewerCount(rpos) == 1, "the right click opened both halves");
});

// After the client e2e moved the right half's first stack (7 naga scales, screen slot 0) into the
// first hotbar slot with two clicks in the game's large chest screen (remote-vanilla): the item
// request reached the right half's storage through the 54-slot proxy.
GF_REGISTER_SELFTEST("combat.ui_tf_double_chest_taken", [](Context& ctx) {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to check");
        return;
    }
    std::optional<GlobalPos> right;
    for (auto const& placed : ui().placed)
        if (auto* region = gf::game::regionOf(placed.pos.dim);
            region && gf::game::blockIdAt(*region, gf::game::blockPos(placed.pos)) == "twilightforest:twilight_oak_chest"
            && gf::inventory::partsOf(placed.pos).size() == 2 && gf::inventory::partsOf(placed.pos).front() == placed.pos)
            right = placed.pos; // the right half: first of its double chest's parts
    ctx.require(right.has_value(), "the double chest of combat.ui_tf_double_chest is still there");
    auto* region = gf::game::regionOf(right->dim);
    auto  h      = gf::inventory::find(*region, gf::game::blockPos(*right));
    ctx.require(h.has_value(), "the right half is a container");
    ctx.check(gf::game::items::isEmpty(h->get(*right, 0)), "the right half's slot 0 is empty (" + gf::game::items::idOf(h->get(*right, 0)) + ")");
    auto& inv    = p->getInventory();
    int   scales = 0;
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (gf::game::items::idOf(inv.getItem(i)) == "twilightforest:naga_scale") scales += gf::game::items::countOf(inv.getItem(i));
    ctx.check(scales == 7, "the 7 naga scales are in the player's inventory (" + std::to_string(scales) + ")");
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (gf::game::items::idOf(inv.getItem(i)) == "twilightforest:naga_scale") inv.setItem(i, ItemStack{});
    p->sendInventory(false);
    gf::log().info("combat ui: tf double chest item request checked");
});

GF_REGISTER_SELFTEST("combat.ui_tf_trapped_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    auto&       region = p->getDimensionBlockSource();
    auto        bp     = besidePlayer(*p, 0, 2);
    auto const* chest  = gf::game::blockWithStates("twilightforest:canopy_trapped_chest", {{"twilightforest:facing", "north"}});
    ctx.require(chest != nullptr, "the canopy trapped chest");
    ctx.require(place(region, bp, *chest), "a canopy trapped chest beside the player");
    auto pos = gf::game::globalPos(region, bp);
    fill(pos, {{0, "twilightforest:knightmetal_ingot", 8}, {13, "twilightforest:carminite", 3}, {22, "twilightforest:naga_scale", 1}});
    auto id = p->getOrCreateUniqueID();
    co_await ctx.ticks(10);
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still there");
    bool used = useOn(*p, bp);
    gf::log().info("combat ui: tf trapped chest opened for {} (use-on {})", p->getRealName(), used ? "succeeded" : "refused");
    ctx.check(gf::inventory::viewerCount(pos) == 1, "the right click opened it");
});

GF_REGISTER_SELFTEST("combat.ui_missing_toast", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    auto id = p->getOrCreateUniqueID();
    co_await ctx.ticks(10); // once the chat that ran the test has closed
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still there");
    gf::progression::showMissing(*p, "twilightforest:progress_lich");
    gf::log().info("combat ui: missing advancement toast sent to {}", p->getRealName());
});

GF_REGISTER_SELFTEST("combat.ui_fx_particles", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    // 5 s of TF's large flame, 3 blocks in front of the eyes (16 a tick, spread as sendParticles)
    struct Emitter {
        static void run(std::int64_t playerId, int ticks) {
            if (ticks <= 0) return;
            gf::interactions::nextTick([playerId, ticks] {
                ActorUniqueID uid;
                uid.rawID = playerId;
                auto  level = ll::service::getLevel();
                auto* pl    = level ? level->getPlayer(uid) : nullptr;
                if (!pl) return;
                auto at = pl->getPosition() + pl->getViewVector(0.f) * 3.f;
                // "probe": the client mod logs its route once for this test (the scenario waits for it)
                gf::fx::particle(pl->getDimension(), "twilightforest:large_flame", at, Vec3{0, .05f, 0}, 16, nlohmann::json{{"spread", {.4, .4, .4}}, {"probe", "combat.ui_fx_particles"}});
                run(playerId, ticks - 1);
            });
        }
    };
    Emitter::run(p->getOrCreateUniqueID().rawID, 100);
    gf::log().info("combat ui: fx particles for {}", p->getRealName());
    co_return;
});

namespace {
/// Runs `emit(player)` for the real player `playerId` on each of the next `ticks` ticks.
void emitEachTick(std::int64_t playerId, int ticks, std::function<void(Player&)> emit) {
    if (ticks <= 0) return;
    gf::interactions::nextTick([playerId, ticks, emit = std::move(emit)]() mutable {
        ActorUniqueID uid;
        uid.rawID   = playerId;
        auto  level = ll::service::getLevel();
        auto* pl    = level ? level->getPlayer(uid) : nullptr;
        if (!pl) return;
        emit(*pl);
        emitEachTick(playerId, ticks - 1, std::move(emit));
    });
}
} // namespace

GF_REGISTER_SELFTEST("combat.ui_fx_portal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    // TFPortalBlock#animateTick for three portal blocks 4 blocks ahead at the feet, every tick (Java
    // picks a block now and then; every tick shows the path densely): 4 particles at the top of the
    // block, speed ((r - 0.5) / 2, r, (r - 0.5) / 2)
    auto rng = std::make_shared<std::mt19937>(std::random_device{}());
    emitEachTick(p->getOrCreateUniqueID().rawID, 100, [rng](Player& pl) {
        std::uniform_real_distribution<float> u(0.f, 1.f);
        auto view = pl.getViewVector(0.f);
        auto feet = pl.getFeetPos();
        Vec3 ahead{feet.x + view.x * 4.f, feet.y, feet.z + view.z * 4.f};
        Vec3 side{-view.z, 0.f, view.x};
        for (int b = -1; b <= 1; ++b) {
            BlockPos block{static_cast<int>(std::floor(ahead.x + side.x * b)), static_cast<int>(std::floor(ahead.y)) - 1,
                           static_cast<int>(std::floor(ahead.z + side.z * b))};
            for (int i = 0; i < 4; ++i) {
                Vec3 at{block.x + u(*rng), block.y + 1.0f, block.z + u(*rng)};
                Vec3 speed{(u(*rng) - 0.5f) * 0.5f, u(*rng), (u(*rng) - 0.5f) * 0.5f};
                gf::fx::particle(pl.getDimension(), "minecraft:portal", at, speed);
            }
        }
    });
    gf::log().info("combat ui: fx portal particles for {}", p->getRealName());
    co_return;
});

GF_REGISTER_SELFTEST("combat.ui_fx_vanilla", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    beginUiTest(*p);
    // every mapped Java particle type, 3 blocks ahead, with the velocity and params TF passes
    emitEachTick(p->getOrCreateUniqueID().rawID, 40, [](Player& pl) {
        auto at = pl.getPosition() + pl.getViewVector(0.f) * 3.f;
        for (auto const& id : gf::fx::vanillaParticleIds()) {
            nlohmann::json params = nlohmann::json::object();
            if (id == "minecraft:item") params = {{"item", "minecraft:bone"}};
            if (id == "minecraft:block" || id == "minecraft:falling_dust") params = {{"block", "minecraft:oak_leaves"}};
            if (id == "minecraft:entity_effect") params = {{"color", 0x7F33CC}};
            gf::fx::particle(pl.getDimension(), id, at, Vec3{0.f, 0.05f, 0.f}, 1, params);
        }
    });
    gf::log().info("combat ui: fx vanilla particles for {}", p->getRealName());
    co_return;
});

GF_REGISTER_SELFTEST("combat.ui_cleanup", [](Context& ctx) {
    ++ui().watchGeneration; // ends a watch
    int n = cleanupUi();
    ctx.check(ui().placed.empty() && !gf::inventory::declared(kFixture), "nothing the UI tests placed is left (" + std::to_string(n) + " removed)");
});

namespace {
struct UiWorldReset {
    UiWorldReset() {
        gf::game::db::onWorldClosed([] {
            ui().placed.clear(); // positions of the world that closed
            ++ui().watchGeneration;
            if (ui().declaredFixture) {
                gf::inventory::unregisterBlock(kFixture);
                ui().declaredFixture = false;
            }
        });
    }
} gUiWorldReset;
} // namespace

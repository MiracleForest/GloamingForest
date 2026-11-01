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

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"

#include "feature/inventory/Inventory.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/Loot.h"
#include "feature/loot/LootInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;

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

} // namespace

// The block is placed first and opened a few ticks later: a container screen for a block the client
// has not received yet is refused by the client.
GF_REGISTER_SELFTEST("combat.ui_loot_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto* p = realPlayer();
    if (!p) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    auto  id     = p->getOrCreateUniqueID();
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, 2, 0);
    ctx.require(gf::loot::placeContainer(region, bp, "minecraft:chest", 4, "gf_test:chests/fixture_chest", 20261102), "a loot chest beside the player");
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
    auto  id     = p->getOrCreateUniqueID();
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, -2, 0);
    ctx.require(gf::game::setBlock(region, bp, *gf::game::blockWith("gf_s3:entity")), "the probe block beside the player");
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
    gf::inventory::declare({.block = "gf_test:fixture_block", .rows = 3, .slots = 27, .title = "container.chest", .skin = ""});
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, 0, -2);
    ctx.require(gf::game::setBlock(region, bp, *gf::game::blockWith("gf_test:fixture_block")), "the fixture block beside the player");
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
    gf::inventory::declare({.block = "gf_test:fixture_block", .rows = 3, .slots = 27, .title = "container.chest", .skin = ""});
    auto& region = p->getDimensionBlockSource();
    auto  bp     = besidePlayer(*p, 0, 2);
    ctx.require(gf::game::setBlock(region, bp, *gf::game::blockWith("gf_test:fixture_block")), "the fixture block beside the player");
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

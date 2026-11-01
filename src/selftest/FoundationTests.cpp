// Adapted from LiteLDev/Levitate v0.1.0 (src/selftest/FoundationTests.cpp and
// src/feature/common/CommonTests.cpp), LGPL-3.0-or-later.
//
// Selftests of the foundation (F0, prefix "core.", testPos dx 5000..5099): world storage,
// translations, configs, blocks, simulated players, block data, interactions, redstone, the
// companion protocol, screens, click-and-hold, flat areas, volume hashes, boss bar capture, the
// contract stubs and the gf_test fixture pack.

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/BossEventPacket.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "core/Keys.h"
#include "core/Stub.h"
#include "feature/common/BlockData.h"
#include "feature/common/Hold.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/common/Screens.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/ModConfig.h"
#include "selftest/SelfTest.h"

namespace {
using gf::selftest::Context;
using JsonT = nlohmann::json;
using gf::core::GlobalPos;
namespace db = gf::game::db;
namespace ia = gf::interactions;

GlobalPos up(GlobalPos p, int dy) {
    p.y += dy;
    return p;
}
} // namespace

GF_REGISTER_SELFTEST("core.world_db", [](Context& ctx) {
    ctx.require(db::available(), "a world is loaded");
    std::string const key = "gf:common:probe";
    db::put(key, {{"value", 42}});
    auto back = db::get(key);
    ctx.check(back && back->value("value", 0) == 42, "saved JSON reads back");
    int seen = 0;
    db::forEachKey("gf:common:", [&seen](std::string_view suffix) {
        if (suffix == "probe") ++seen;
    });
    ctx.check(seen == 1, "forEachKey finds the key by prefix");
    db::erase(key);
    ctx.check(!db::get(key), "erased key is gone");
    ctx.check(!gf::core::validWorldKey("lv:common:probe") && gf::core::validWorldKey(key), "keys must be gf:<unit>:...");
});

GF_REGISTER_SELFTEST("core.i18n", [](Context& ctx) {
    ctx.check(gf::tr("gf.test.missing", "Fallback {}", 1) == "Fallback 1", "missing keys use the fallback");
    ctx.check(gf::tr("gf.test.missing", "Java %s and %2$s", "a", "b") == "Java a and b", "Java placeholders are formatted");
    ctx.check(gf::translate("gf.mod.name", "?", "en_US") == "GloamingForest", "en_US lang file is loaded");
    ctx.check(gf::translate("gf.mod.name", "?", "zh_CN") != "?", "zh_CN lang file is loaded");
    ctx.check(!gf::config::configDir().empty(), "config directory is set");
    ctx.check(gf::config::get<gf::ModConfig>().version >= 1, "mod config is loaded");
});

GF_REGISTER_SELFTEST("core.blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(5000);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:gold_block"), "set a vanilla block");
    co_await ctx.ticks(1);
    ctx.check(ctx.blockAt(pos) == "minecraft:gold_block", "block reads back");
    ctx.check(gf::game::blockWith("twilightforest:no_such_block") == nullptr, "unknown block ids give nullptr");
    auto const* stairs = gf::game::blockWithStates("minecraft:oak_stairs", {{"upside_down_bit", true}, {"weirdo_direction", 2}});
    ctx.require(stairs != nullptr, "states by name and value");
    auto states = gf::game::statesOf(*stairs);
    std::string got;
    for (auto const& [k, v] : states) got += k + "=" + v + " ";
    ctx.check(states["upside_down_bit"] == "true" && states["weirdo_direction"] == "2", "statesOf reads the states back (got " + got + ")");
    ctx.check(gf::game::blockWithStates("minecraft:oak_stairs", {{"no_such_state", 1}}) == nullptr, "unknown states are refused");
});

GF_REGISTER_SELFTEST("core.players", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& a = ctx.player();
    auto& b = ctx.spawnPlayer();
    co_await ctx.ticks(2);
    ctx.check(&a != static_cast<Player*>(&b), "two distinct simulated players");
    ctx.check(!a.getRealName().empty(), "the simulated player has a name");
    ctx.check(ctx.give("minecraft:diamond", 3) >= 0, "give(id, n) puts the item in the inventory");
    ctx.check(ctx.give("twilightforest:no_such_item") < 0, "give refuses unknown items");
});

GF_REGISTER_SELFTEST("core.block_data", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace bd = gf::blockdata;
    auto pos     = up(ctx.testPos(5001), 1);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "place stone");
    co_await ctx.ticks(1);
    auto& region = *ctx.region();
    auto  bp     = gf::game::blockPos(pos);

    bd::set(region, bp, "core", {{"probe", 42}});
    auto const* v = bd::get(region, bp, "core");
    ctx.check(v && v->value("probe", 0) == 42, "set(region, pos, key) then get() reads back");
    int seen = 0;
    bd::forEachInChunk(pos.dim, gf::core::chunkOf(pos.x), gf::core::chunkOf(pos.z), [&](GlobalPos const& p, JsonT const& d) {
        if (p == pos && d.contains("core")) ++seen;
    });
    ctx.check(seen == 1, "forEachInChunk finds the document");

    bd::flush();
    auto key = gf::core::blockDataChunkKey(pos.dim, gf::core::chunkOf(pos.x), gf::core::chunkOf(pos.z));
    auto doc = db::get(key);
    ctx.check(doc && doc->contains(gf::core::posKey(pos.x, pos.y, pos.z)), "flush() writes the chunk document to the world DB");

    static bool rotated; // the handler stays registered after the test: no captured locals
    rotated = false;
    bd::onRotate("core_rotation_probe", [](JsonT& value, int, int) { rotated = value == 1; });
    JsonT turning{{"core_rotation_probe", 1}};
    bd::rotate(turning, 1, 1);
    ctx.check(rotated, "onRotate turns the data of its key");

    // a block state change keeps it, a different block erases it
    ctx.setBlock(pos, "minecraft:stone");
    co_await ctx.ticks(1);
    ctx.check(bd::has(pos), "same block type keeps the document");
    bd::set(region, bp, "core", nullptr);
    ctx.check(!bd::has(pos), "removing the last key erases the document");
    bd::set(region, bp, "core", {{"probe", 1}});
    ctx.setBlock(pos, "minecraft:dirt");
    co_await ctx.ticks(1);
    ctx.check(!bd::has(pos), "replacing the block erases the document");
    bd::flush();
    auto after = db::get(key);
    ctx.check(!after || !after->contains(gf::core::posKey(pos.x, pos.y, pos.z)), "the erase is persisted");
});

GF_REGISTER_SELFTEST("core.interactions", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = up(ctx.testPos(5002), 1);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:gold_block"), "place a block");
    co_await ctx.ticks(1);
    auto&       p     = ctx.player();
    auto&       r     = *ctx.region();
    auto const& block = r.getBlock(gf::game::blockPos(pos));
    std::string order;
    auto        low   = ia::onUseOn({.block = "minecraft:gold_block"}, [&](ia::UseOnContext const&) { order += "low"; return ia::Result::Consume; }, ia::priority::Low);
    auto        high  = ia::onUseOn({.block = "minecraft:gold_block"}, [&](ia::UseOnContext const&) { order += "high,"; return ia::Result::Pass; }, ia::priority::High);
    auto        other = ia::onUseOn({.block = "minecraft:stone"}, [&](ia::UseOnContext const&) { order += "stone"; return ia::Result::Consume; });
    // tag matchers ask gf::tags (a stub until F9: nothing is tagged yet)
    auto tagged = ia::onUseOn(ia::Match::tag("twilightforest:core_probe_tag"), [&](ia::UseOnContext const&) { order += "tag"; return ia::Result::Consume; }, ia::priority::Highest);
    std::string       blockId = "minecraft:gold_block", itemId;
    auto const&       empty   = gf::game::items::empty();
    ia::UseOnContext  c{p, empty, itemId, block, blockId, pos, 1, {0.5, 1.0, 0.5}, false, true};
    auto              result = ia::dispatchUseOn(c);
    ctx.check(result == ia::Result::Consume && order == "high,low", "handlers run by priority until one takes the click (got " + order + ")");
    ctx.check(!ia::matches(ia::Match::tag("twilightforest:core_probe_tag"), blockId, itemId), "untagged blocks don't match a tag");
    ctx.check(ia::matches({.item = "minecraft:stick"}, blockId, "minecraft:stick") && !ia::matches({.emptyHand = true}, blockId, "minecraft:stick"), "item and empty-hand matchers");
    for (auto id : {low, high, other, tagged}) ia::remove(id);
    order.clear();
    ctx.check(ia::dispatchUseOn(c) == ia::Result::Pass && order.empty(), "removed handlers no longer run");
    bool ran = false;
    ia::nextTick([&ran] { ran = true; });
    co_await ctx.ticks(2);
    ctx.check(ran, "nextTick runs on the next tick");
});

GF_REGISTER_SELFTEST("core.redstone", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace rs = gf::redstone;
    auto pos     = up(ctx.testPos(5003), 1);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "place the probe block");
    auto& r  = *ctx.region();
    auto  bp = gf::game::blockPos(pos);
    co_await ctx.ticks(2);
    ctx.check(rs::power(r, bp) == 0, "no power without a source");
    int last  = -1;
    auto watch = rs::watch(pos, [&last](GlobalPos const&, int power) { last = power; });
    ctx.require(ctx.setBlock(up(pos, 1), "minecraft:redstone_block"), "place a redstone block on top");
    co_await ctx.ticks(6);
    ctx.check(rs::power(r, bp) == 15, "a redstone block powers its neighbour with 15");
    ctx.check(last == 15, "the watch saw the change");
    rs::unwatch(watch);
    rs::registerAnalog("minecraft:stone", [](BlockSource&, BlockPos const&, Block const&) -> std::optional<int> { return 7; });
    ctx.check(rs::analogOutput(r, bp) == 7, "registered comparator output reads back");
    rs::unregisterAnalog("minecraft:stone");
    ctx.check(rs::fractionToRedstoneLevel(0) == 0 && rs::fractionToRedstoneLevel(1) == 15, "fraction to signal");
});

GF_REGISTER_SELFTEST("core.net", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace net = gf::net;
    auto& p       = ctx.player();
    co_await ctx.ticks(1);
    ctx.check(!net::isCompanion(p), "a simulated player is no companion");
    ctx.check(!net::send(p, "core.probe", {{"x", 1}}), "nothing is sent to vanilla players");
    net::setTestCompanion(p, true);
    ctx.check(net::isCompanion(p) && net::companionProtocol(p) == gf::core::kProtocolVersion, "test companion");
    ctx.check(net::send(p, "core.probe", {{"x", 1}}), "companions get messages");
    auto msgs = net::takeTestMessages(p);
    ctx.check(msgs.size() == 1 && msgs[0].channel == "core.probe" && msgs[0].payload["x"] == 1, "the message was recorded");
    JsonT got;
    net::on("core.reply", [&got](Player&, JsonT const& msg) { got = msg; });
    net::injectFromClient(p, "core.reply", {{"y", 2}});
    ctx.check(got.value("y", 0) == 2, "client messages reach the channel handler");
    net::off("core.reply");
    net::setTestCompanion(p, false);
});

GF_REGISTER_SELFTEST("core.screens", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace net     = gf::net;
    namespace screens = gf::screens;
    static int actions, forms; // the type stays registered after the test: no captured locals
    actions = forms = 0;
    screens::registerType({
        .id     = "core.probe_screen",
        .data   = [](Player&, JsonT const& c) -> std::optional<JsonT> {
            if (c.value("open", true)) return JsonT{{"value", 1}};
            return std::nullopt;
        },
        .action = [](Player&, JsonT const&, JsonT const&) { ++actions; },
        .form   = [](Player&, JsonT const&) { ++forms; },
    });
    auto& p = ctx.player();
    co_await ctx.ticks(1);
    ctx.check(screens::open(p, "core.probe_screen") && forms == 1, "vanilla players get the form");
    ctx.check(!screens::open(p, "core.probe_screen", {{"open", false}}), "a refused screen does not open");
    net::setTestCompanion(p, true);
    ctx.check(screens::open(p, "core.probe_screen"), "companions get the native screen");
    auto msgs = net::takeTestMessages(p);
    ctx.check(msgs.size() == 1 && msgs[0].channel == "screen.open" && msgs[0].payload["data"]["value"] == 1, "screen.open carries the data");
    net::injectFromClient(p, "screen.action", {{"type", "core.probe_screen"}, {"context", JsonT::object()}, {"action", {{"op", "x"}}}});
    msgs = net::takeTestMessages(p);
    ctx.check(actions == 1 && msgs.size() == 1 && msgs[0].channel == "screen.update", "actions are answered with fresh data");
    net::setTestCompanion(p, false);
});

GF_REGISTER_SELFTEST("core.hold", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace net = gf::net;
    auto& p       = ctx.player();
    auto  pos     = ctx.testPos(5004);
    co_await ctx.ticks(1);
    int shorts = 0, holds = 0;
    ctx.check(!gf::hold::awaitHold(p, pos, [&](Player&) { ++shorts; }, [&](Player&) { ++holds; }), "vanilla players keep their own fallback");
    net::setTestCompanion(p, true);
    ctx.check(gf::hold::awaitHold(p, pos, [&](Player&) { ++shorts; }, [&](Player&) { ++holds; }), "companions are asked to watch the button");
    ctx.check(gf::hold::holdPending(p, pos), "the hold is pending");
    auto msgs = net::takeTestMessages(p);
    ctx.require(msgs.size() == 1 && msgs[0].channel == "hold.watch", "hold.watch was sent");
    net::injectFromClient(p, "hold.release", {{"token", msgs[0].payload["token"]}, {"state", "short"}});
    ctx.check(shorts == 1 && holds == 0 && !gf::hold::holdPending(p, pos), "a short click runs onShort");
    net::setTestCompanion(p, false);
});

GF_REGISTER_SELFTEST("core.flat_area", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(5010, 6, 4);
    ctx.check(box.maxX - box.minX == 5 && box.maxY - box.minY == 3, "the box has the asked size");
    auto empty = ctx.snapshotVolume(box);
    ctx.require(empty.size() == 16, "the hash has 16 hex digits");
    GlobalPos at{ctx.dimension(), box.minX + 2, box.minY, box.minZ + 3};
    ctx.require(ctx.setBlock(at, "minecraft:oak_log"), "place a block in the area");
    auto one = ctx.snapshotVolume(box);
    ctx.check(one != empty, "the hash sees the block");
    ctx.check(ctx.snapshotVolume(box) == one, "the hash is stable");
    ctx.check(ctx.blockAt({ctx.dimension(), box.minX, box.minY - 1, box.minZ}) == "minecraft:grass_block", "the ground is grass");
});

GF_REGISTER_SELFTEST("core.boss_capture", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& capture = ctx.captureBossEvent();
    auto& p       = ctx.player();
    co_await ctx.ticks(1);
    gf::selftest::BossEvent e;
    e.type    = "add";
    e.name    = "core.probe";
    e.percent = 0.5f;
    gf::selftest::BossEventCapture::record(e);
    auto events = capture.events();
    ctx.check(events.size() == 1 && events[0].name == "core.probe", "recorded events are captured");
    capture.clear();
    // a real packet through the game's packet sender
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::BossEvent);
    ctx.require(packet != nullptr, "a BossEventPacket can be created");
    auto& boss          = static_cast<BossEventPacket&>(*packet);
    boss.mBossID->rawID = p.getOrCreateUniqueID().rawID;
    boss.mEventType     = BossEventUpdateType::Add;
    boss.mName->set(std::string("core.packet"));
    boss.mHealthPercent = 0.25f;
    packet->sendTo(p);
    co_await ctx.ticks(1);
    events = capture.events();
    bool seen = false;
    for (auto const& ev : events) seen |= ev.name == "core.packet" && ev.type == "add" && ev.percent == 0.25f;
    ctx.check(seen, "a BossEventPacket sent to a player is captured (" + std::to_string(events.size()) + " events)");
    auto remove        = MinecraftPackets::createPacket(MinecraftPacketIds::BossEvent);
    auto& r            = static_cast<BossEventPacket&>(*remove);
    r.mBossID->rawID   = p.getOrCreateUniqueID().rawID;
    r.mEventType       = BossEventUpdateType::Remove;
    remove->sendTo(p);
});

GF_REGISTER_SELFTEST("core.stubs", [](Context& ctx) {
    gf::core::notImplemented("core.selftest_probe");
    gf::core::notImplemented("core.selftest_probe");
    int n = 0;
    for (auto const& s : gf::core::stubCalls()) n += s == "core.selftest_probe";
    ctx.check(n == 1, "a stub is reported once");
});

GF_REGISTER_SELFTEST("core.fixtures", [](Context& ctx) -> ll::coro::CoroTask<> {
    // assets/test: the gf_test fixture pack that W0b selftests use (never another unit's content)
    ctx.check(gf::game::blockWith("gf_test:fixture_block") != nullptr, "fixture block gf_test:fixture_block is registered");
    ctx.check(gf::game::items::make("gf_test:fixture_item").has_value(), "fixture item gf_test:fixture_item is registered");
    auto pos = up(ctx.testPos(5005), 1);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.check(ctx.setBlock(pos, "gf_test:fixture_block", {{"gf_test:level", 3}}), "the fixture block takes an integer state");
});

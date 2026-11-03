// Selftests of F12, server side ("ui.*", dx 6200..6299; rule R12). They run in both builds; the
// client build adds the client registry tests (client/ui/ClientTests.cpp).
//
// The fixture tests ("ui.fixture_*") drive the e2e scenarios. They act on a real player only when it
// carries the tag gf_ui_fixture (fixture::kPlayerTag; the client scenario adds it after its `ui.*`
// run): companions get the native fixture, vanilla players the form / DDUI fallback. Untagged
// players are never touched, so `ui.*` and the full run leave a developer's or A8's player alone.
// Without tagged players the tests act on a test companion or the test player and check what was
// recorded. The real-player steps put their blocks on the stage in this unit's row (testPos(6260),
// dz 0..4) and ui.fixture_clear puts the stage and the players back (position, held stack, helmet).
// Every registration a test makes is removed again (the tests pass in any order and any number of
// times in one session).

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/common/Screens.h"
#include "feature/ext/ui/Server.h"
#include "feature/fx/Fx.h"
#include "feature/movement/Movement.h"
#include "feature/net/Net.h"
#include "feature/ui/Fixture.h"
#include "feature/ui/Ui.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {
using gf::selftest::Context;
using Json     = nlohmann::json;
namespace net  = gf::net;
namespace fx   = gf::ui::fixture;
using ArmorSlot = SharedTypes::Legacy::ArmorSlot;

constexpr char kFixtureItem[]  = "gf_test:fixture_item";
constexpr char kFixtureBlock[] = "gf_test:fixture_block";
/// The stage of the real-player steps (fixture::prepareStage): dx 6260..6262 of this unit's row.
constexpr int kStageDx = 6261;
/// The world DB key of the stage (fixture::prepareStage).
constexpr char kStageKeyForTests[] = "gf:ui:fixture_stage";

/// The fixture is off in production builds (no gf_test packs): its tests only say so.
bool fixtureOff(Context& ctx) {
    if (fx::enabled()) return false;
    ctx.info("fixture off (no gf_test packs deployed): skipped");
    return true;
}

std::string names(std::vector<Player*> const& players) {
    std::string out;
    for (auto* p : players) out += (out.empty() ? "" : ", ") + p->getRealName();
    return out;
}

/// The stage for the tagged players (an empty list: nothing to prepare). Its origin is the feet
/// block of the player; the chest / frame goes two blocks south of it.
struct StageAt {
    BlockSource* region = nullptr;
    BlockPos     origin{};
};
ll::coro::CoroTask<std::optional<StageAt>> stageFor(Context& ctx, std::vector<Player*> const& players) {
    if (players.empty()) co_return std::nullopt;
    auto pos = ctx.testPos(kStageDx);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region for the stage");
    BlockPos origin = gf::game::blockPos(pos);
    ctx.require(fx::prepareStage(*region, origin), "the stage is prepared in the ui row");
    co_return StageAt{region, origin};
}

/// The tagged players again after a co_await (rule R16).
std::vector<Player*> fixturePlayersNow() { return fx::fixturePlayers(); }

/// Takes down a test frame (placeFrame at `pos`, its stone south of it) the way the fixture does:
/// emptied first, so ItemFrameBlockActor::onRemoved drops nothing; checks that no item lies around.
ll::coro::CoroTask<> removeFrame(Context& ctx, gf::core::GlobalPos pos) {
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto at     = gf::game::blockPos(pos);
    auto before = fx::itemEntitiesNear(*region, at, 3); // (items an older build dropped may still lie here)
    ctx.check(fx::emptyFrame(*region, at), "the frame is emptied");
    auto const* air = gf::game::blockWith("minecraft:air");
    ctx.require(air != nullptr, "minecraft:air");
    gf::game::setBlock(*region, at, *air);
    gf::game::setBlock(*region, {at.x, at.y, at.z + 1}, *air);
    co_await ctx.ticks(2);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto after = fx::itemEntitiesNear(*region, at, 3);
    ctx.check(after <= before, "nothing dropped where the frame was: " + std::to_string(before) + " item(s) before, " + std::to_string(after) + " after");
}

// ---- vanilla fallbacks ---------------------------------------------------------------------------

GF_REGISTER_SELFTEST("ui.title_skin", [](Context& ctx) {
    namespace ui = gf::ui;
    // no JSON-UI route can skin a single vanilla container on 1.26.51 (spike S5): plain titles
    static_assert(!ui::kJsonUiSkins);
    ctx.check(ui::containerTitleSkin("GloamingForest UI fixture", "ui/fixture_container") == "GloamingForest UI fixture", "a skin keeps the plain title");
    ctx.check(ui::containerTitleSkin("container.chest", "") == "container.chest", "no skin: the plain title");
});

GF_REGISTER_SELFTEST("ui.form_fallback", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p      = ctx.player();
    int   before = fx::formsShown();
    // a vanilla (simulated) player gets the form fallback of a screen type
    ctx.check(gf::screens::open(p, "ui.fixture"), "screen ui.fixture opens");
    ctx.check(fx::formsShown() == before + 1, "a vanilla player got the form fallback");
    // the contract's own form: buttons and close callback must not run without an answer
    auto clicked = std::make_shared<bool>(false), closed = std::make_shared<bool>(false);
    gf::ui::form(p, "ui.form_fallback", "body", {{.text = "A", .icon = "", .onClick = [clicked](Player&) { *clicked = true; }}}, [closed](Player&) { *closed = true; });
    co_await ctx.ticks(2);
    ctx.check(!*clicked && !*closed, "no answer from a simulated player");
    // tagged vanilla players (the remote-vanilla e2e client) get the fixture form; the scenario
    // closes it and waits for "ui form ... closed by"
    int real = 0;
    for (auto* rp : fx::fixturePlayers()) {
        if (net::isCompanion(*rp)) continue;
        fx::openFormFallback(*rp);
        ++real;
    }
    ctx.info("fixture form sent to " + std::to_string(real) + " fixture player(s)");
});

GF_REGISTER_SELFTEST("ui.ddui", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p      = ctx.player();
    auto  before = gf::ui::dduiSessions();
    Json  doc    = {
        {"title", "ui.ddui"},
        {"elements", Json::array({{{"type", "label"}, {"text", "x"}}, {{"type", "toggle"}, {"id", "t"}, {"text", "T"}}, {{"type", "button"}, {"id", "ok"}, {"text", "OK"}}})},
    };
    ctx.check(!gf::ui::ddui(p, "ui.ddui", doc, {}), "no DDUI for a simulated player: the caller shows a form");
    ctx.check(!gf::ui::ddui(p, "ui.ddui", Json::array(), {}), "a malformed document is refused");
    ctx.check(gf::ui::dduiSessions() == before, "a refused document keeps no session");
});

// The DDUI fallback for tagged vanilla players (remote-vanilla e2e: shown, then closed with ESC).
GF_REGISTER_SELFTEST("ui.fixture_ddui", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::fixturePlayers()) {
        if (net::isCompanion(*rp)) continue;
        bool shown = gf::ui::ddui(*rp, "ui.fixture", fx::dduiDocument(), [](Player& p, Json const& r) {
            gf::log().info("ui fixture: ddui response of {}: {}", p.getRealName(), r.dump());
        });
        if (shown) ++real;
        else ctx.info("DDUI not available for " + rp->getRealName());
    }
    ctx.info("fixture DDUI sent to " + std::to_string(real) + " fixture player(s)");
    (void)ctx.player();
    co_await ctx.ticks(1);
    ctx.check(!gf::ui::ddui(ctx.player(), "ui.fixture", fx::dduiDocument(), {}), "a simulated player gets no DDUI");
});

// ---- native screens (companions) -----------------------------------------------------------------

GF_REGISTER_SELFTEST("ui.screen_native", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    net::setTestCompanion(p, true);
    (void)net::takeTestMessages(p);
    int forms = fx::formsShown();
    ctx.check(gf::screens::open(p, "ui.fixture"), "screen opens for a companion");
    auto msgs = net::takeTestMessages(p);
    bool open = std::any_of(msgs.begin(), msgs.end(), [](auto const& m) {
        return m.channel == "screen.open" && m.payload.value("type", std::string{}) == "ui.fixture" && m.payload["data"].contains("items");
    });
    ctx.check(open, "the companion got screen.open with the fixture data");
    ctx.check(fx::formsShown() == forms, "no form for a companion");
    int clicks = fx::clicks();
    net::injectFromClient(p, "screen.action", {{"type", "ui.fixture"}, {"context", Json::object()}, {"action", {{"click", 4}}}});
    ctx.check(fx::clicks() == clicks + 1, "a native slot click reaches the screen type");
    msgs = net::takeTestMessages(p);
    ctx.check(std::any_of(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "screen.update"; }), "the screen got fresh data after the action");
    net::setTestCompanion(p, false);
});

// Reports the tagged players (read only; the client scenario checks its tag arrived).
GF_REGISTER_SELFTEST("ui.fixture_players", [](Context& ctx) {
    auto players = fx::fixturePlayers();
    ctx.info("fixture players: " + std::to_string(players.size()) + (players.empty() ? "" : " (" + names(players) + ")"));
});

GF_REGISTER_SELFTEST("ui.fixture_show", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    Json what = {{"show", true}, {"hud", true}, {"toast", true}, {"world", true}, {"screen", false}};
    int  real = 0;
    for (auto* rp : fx::fixturePlayers()) {
        if (fx::show(*rp, what)) ++real;
    }
    if (real > 0) ctx.info("fixture shown to " + std::to_string(real) + " companion(s)");
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    ctx.check(!fx::show(p, what), "nothing is sent to vanilla players");
    net::setTestCompanion(p, true);
    (void)net::takeTestMessages(p);
    ctx.check(fx::show(p, what), "a companion gets the fixture");
    auto msgs = net::takeTestMessages(p);
    ctx.check(msgs.size() == 1 && msgs[0].channel == "ui.fixture" && msgs[0].payload.value("hud", false), "the fixture message was recorded");
    net::setTestCompanion(p, false);
});

// Opens the native fixture screen for tagged companions (the client scenario screenshots it).
GF_REGISTER_SELFTEST("ui.fixture_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::fixturePlayers()) {
        if (!net::isCompanion(*rp)) continue;
        if (gf::screens::open(*rp, "ui.fixture")) ++real;
    }
    ctx.info("fixture screen opened for " + std::to_string(real) + " companion(s)");
    (void)ctx.player();
    co_await ctx.ticks(1);
    ctx.check(gf::screens::open(ctx.player(), "ui.fixture"), "the fixture screen type is registered");
});

// Turns on the fixture's fog / sky / cloud providers for tagged companions (client scenario).
GF_REGISTER_SELFTEST("ui.fixture_fog", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    int real = 0;
    for (auto* rp : fx::fixturePlayers()) {
        if (fx::show(*rp, {{"show", true}, {"fog", true}})) ++real;
    }
    ctx.info("fixture fog shown to " + std::to_string(real) + " companion(s)");
});

// The vanilla container fallback: a vanilla chest titled with containerTitleSkin (the plain title,
// spike S5) on the stage two blocks south of the tagged players (turned to it; the client scenario
// opens it), and at the test position, where the stored name is checked.
GF_REGISTER_SELFTEST("ui.fixture_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    auto title = gf::ui::containerTitleSkin(gf::tr("gf.ui.fixture.title", "GloamingForest UI fixture"), "ui/fixture_container");
    if (auto st = co_await stageFor(ctx, fixturePlayersNow())) {
        BlockPos chest{st->origin.x, st->origin.y, st->origin.z + 2};
        ctx.check(fx::placeNamedChest(*st->region, chest, title), "the stage chest is placed");
        auto players = fixturePlayersNow();
        for (auto* rp : players) fx::standOnStage(*rp, *st->region, st->origin, 0.5f); // the chest's middle
        ctx.info("named chest placed on the stage for " + names(players));
    }
    auto pos = ctx.testPos(6220);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.cleanupBlock(pos);
    ctx.require(fx::placeNamedChest(*region, gf::game::blockPos(pos), title), "the chest is placed and named");
    ctx.check(fx::chestName(*region, gf::game::blockPos(pos)) == title, "the chest keeps its title");
});

// A chest without a custom name at the same place (the client scenario compares the two).
GF_REGISTER_SELFTEST("ui.fixture_chest_plain", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    if (auto st = co_await stageFor(ctx, fixturePlayersNow())) {
        BlockPos chest{st->origin.x, st->origin.y, st->origin.z + 2};
        ctx.check(fx::placeNamedChest(*st->region, chest, ""), "the stage chest is placed");
        auto players = fixturePlayersNow();
        for (auto* rp : players) fx::standOnStage(*rp, *st->region, st->origin, 0.5f);
        ctx.info("plain chest placed on the stage for " + names(players));
    }
    auto pos = ctx.testPos(6250);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.cleanupBlock(pos);
    ctx.require(fx::placeNamedChest(*region, gf::game::blockPos(pos), ""), "the chest is placed");
    ctx.check(fx::chestName(*region, gf::game::blockPos(pos)).empty(), "a plain chest has no custom name");
});

// ItemContext::Fixed: an item frame with the fixture item one block above the stage floor, two
// blocks south of the tagged players (the client scenario screenshots it), and at the test position.
GF_REGISTER_SELFTEST("ui.fixture_frame", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    if (auto st = co_await stageFor(ctx, fixturePlayersNow())) {
        BlockPos const origin = st->origin;
        ctx.check(fx::placeFrame(*st->region, {origin.x, origin.y + 1, origin.z + 2}, kFixtureItem), "the stage frame is placed");
        auto players = fixturePlayersNow();
        for (auto* rp : players) {
            fx::standOnStage(*rp, *st->region, origin, 1.5f); // the frame's middle
            // the client's stage renderers first (a block renderer for the fixture block, a tint on the
            // game's foliage for the twilight oak leaves), then their blocks, so the client meshes them with both
            fx::show(*rp, {{"show", true}, {"stage", {origin.x, origin.y, origin.z}}});
        }
        ctx.info("item frame placed on the stage for " + names(players));
        co_await ctx.ticks(2);
        auto* region = ctx.region(); // again after the wait (rule R16)
        ctx.require(region != nullptr, "a region for the stage");
        auto const* block  = gf::game::blockWith(kFixtureBlock);
        // TF leaves with the "default_foliage" tint method (tools/families): their mesh asks the tint
        // resolver, so the tint registry runs (vanilla oak leaves are tinted without it)
        auto const* leaves = gf::game::blockWithStates("twilightforest:twilight_oak_leaves", Json{{"twilightforest:persistent", true}}); // no decay
        ctx.check(block && gf::game::setBlock(*region, {origin.x - 1, origin.y, origin.z + 2}, *block), "the fixture block (block renderer) is on the stage");
        ctx.check(leaves && gf::game::setBlock(*region, {origin.x + 1, origin.y, origin.z + 2}, *leaves), "the twilight oak leaves (tint on the game's foliage) are on the stage");
        ctx.info(
            "stage blocks: " + gf::game::blockId(region->getBlock({origin.x - 1, origin.y, origin.z + 2})) + " and " +
            gf::game::blockId(region->getBlock({origin.x + 1, origin.y, origin.z + 2}))
        );
        // two named armor stands behind them: the client fixture hides one name tag (render::hideNameTag)
        ctx.check(fx::placeNameTagStands(*region, origin) == 2, "the named armor stands are on the stage");
    }
    auto pos = ctx.testPos(6230, 1);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto at = gf::game::blockPos(pos);
    ctx.cleanupBlock(pos);
    ctx.cleanupBlock(gf::core::GlobalPos{pos.dim, pos.x, pos.y, pos.z + 1});
    ctx.require(fx::placeFrame(*region, at, kFixtureItem), "the frame is placed");
    ctx.check(fx::frameItem(*region, at) == kFixtureItem, "the frame holds the fixture item");
    co_await removeFrame(ctx, pos);
});

// The stage's named armor stands (the client fixture's name tag step) are placed and removed again,
// nothing dropped (dx 6236: the row, dz 0..4).
GF_REGISTER_SELFTEST("ui.fixture_name_stands", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    auto pos = ctx.testPos(6236, 0);
    co_await ctx.ensureLoaded(gf::core::GlobalPos{pos.dim, pos.x, pos.y, pos.z + 4});
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto     origin = gf::game::blockPos(pos);
    BlockPos min{origin.x - 1, origin.y - 1, origin.z};
    auto     before = fx::itemEntitiesNear(*region, {origin.x, origin.y, origin.z + 4}, 3);
    ctx.require(fx::placeNameTagStands(*region, origin) == 2, "two named armor stands are placed");
    ctx.check(fx::placeNameTagStands(*region, origin) == 2, "placing them again replaces the earlier ones");
    co_await ctx.ticks(2);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(fx::removeNameTagStands(*region, min) == 2, "both are removed (and no more than two were there)");
    co_await ctx.ticks(2);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(fx::removeNameTagStands(*region, min) == 0, "none is left");
    auto after = fx::itemEntitiesNear(*region, {origin.x, origin.y, origin.z + 4}, 3);
    ctx.check(after <= before, "nothing dropped: " + std::to_string(before) + " item(s) before, " + std::to_string(after) + " after");
});

// ItemContext::Head: the fixture block on the tagged players' heads (their helmet is kept and put
// back by ui.fixture_clear; the client scenario screenshots it in third person), and on the test
// player.
GF_REGISTER_SELFTEST("ui.fixture_head", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    auto block = gf::game::items::make(kFixtureBlock);
    ctx.require(block.has_value(), "the fixture block item (gf_test packs)");
    if (auto st = co_await stageFor(ctx, fixturePlayersNow())) {
        auto players = fixturePlayersNow();
        for (auto* rp : players) {
            fx::standOnStage(*rp, *st->region, st->origin, 1.62f); // level head: the front view looks the player in the face
            fx::setHead(*rp, *block);
        }
        ctx.info("fixture block put on " + names(players));
    }
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    p.setArmor(ArmorSlot::Head, *block);
    ctx.check(p.getArmor(ArmorSlot::Head).getTypeName() == kFixtureBlock, "the head slot holds the fixture block");
});

// Map decorations: a filled map in the tagged players' hands (the held stack is kept and put back by
// ui.fixture_clear) and the fixture decorations on it (companions draw them; the client scenario
// screenshots the held map).
GF_REGISTER_SELFTEST("ui.fixture_map", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    auto map = gf::game::items::make("minecraft:filled_map");
    ctx.require(map.has_value(), "minecraft:filled_map");
    if (auto st = co_await stageFor(ctx, fixturePlayersNow())) {
        int real = 0;
        for (auto* rp : fixturePlayersNow()) {
            // look down at the held map (Java and Bedrock draw it larger then)
            fx::standOnStage(*rp, *st->region, st->origin, -2.0f);
            fx::setHeld(*rp, *map); // the slot's stack is remembered first, whichever slot is selected
            if (fx::show(*rp, {{"show", true}, {"map", true}})) ++real;
        }
        ctx.info("fixture map given to " + std::to_string(real) + " companion(s)");
    }
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    net::setTestCompanion(p, true);
    (void)net::takeTestMessages(p);
    ctx.check(fx::show(p, {{"show", true}, {"map", true}}), "a companion gets the map fixture");
    auto msgs = net::takeTestMessages(p);
    ctx.check(msgs.size() == 1 && msgs[0].payload.value("map", false), "the map message was recorded");
    net::setTestCompanion(p, false);
});

// The held fixture map in an item frame on the stage (framed maps: the client scenario screenshots
// it); the test player has no map with an id, so only the frame is checked there.
GF_REGISTER_SELFTEST("ui.fixture_map_frame", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    if (auto st = co_await stageFor(ctx, fixturePlayersNow())) {
        int real = 0;
        for (auto* rp : fixturePlayersNow()) {
            auto const& held = rp->getSelectedItem();
            if (held.isNull() || held.getTypeName() != "minecraft:filled_map") continue;
            if (!fx::placeFrame(*st->region, {st->origin.x, st->origin.y + 1, st->origin.z + 2}, held)) continue;
            fx::standOnStage(*rp, *st->region, st->origin, 1.5f);
            ++real;
        }
        ctx.info("framed map placed on the stage for " + std::to_string(real) + " player(s)");
    }
    auto pos = ctx.testPos(6240, 1);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto at = gf::game::blockPos(pos);
    ctx.cleanupBlock(pos);
    ctx.cleanupBlock(gf::core::GlobalPos{pos.dim, pos.x, pos.y, pos.z + 1});
    auto map = gf::game::items::make("minecraft:filled_map");
    ctx.require(map.has_value() && fx::placeFrame(*region, at, *map), "a frame with a map is placed");
    ctx.check(fx::frameItem(*region, at) == "minecraft:filled_map", "the frame holds the map");
    co_await removeFrame(ctx, pos);
});

// Puts back what the fixture tests changed for tagged players: the stage blocks, the players' helmet,
// inventory slots and position, and the respawn step's gamerules (also what a session that ended
// before its clear left: world DB).
GF_REGISTER_SELFTEST("ui.fixture_clear", [](Context& ctx) {
    int blocks  = fx::clearStage();
    int players = fx::restorePlayers();
    int rules   = fx::restoreRespawnRules();
    ctx.info(
        "stage restored (" + std::to_string(blocks) + " block(s)), " + std::to_string(players) + " player(s) put back, " + std::to_string(rules) +
        " gamerule(s) put back; " + std::to_string(fx::savedPlayerCount()) + " player(s) not in the level stay remembered"
    );
});

// Turns the client fixture off again for tagged companions (the client scenario runs it after its
// screenshots).
GF_REGISTER_SELFTEST("ui.fixture_hide", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    int real = 0;
    for (auto* rp : fx::fixturePlayers()) {
        if (fx::show(*rp, {{"show", false}})) ++real;
    }
    ctx.info("fixture hidden for " + std::to_string(real) + " companion(s)");
});

// The undo of the real-player steps, with the test player standing in for a tagged one: its
// position, every inventory slot a step changed (also after it switched slots between two steps) and
// helmet come back, the stage blocks are restored and the stage's item frame drops nothing.
GF_REGISTER_SELFTEST("ui.fixture_restore", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    if (!fx::fixturePlayers().empty()) {
        ctx.info("fixture players present: their stage stays until ui.fixture_clear");
        co_return;
    }
    if (fx::savedPlayerCount() > 0) {
        ctx.info("a player of an earlier session waits to be put back (ui.fixture_clear): skipped");
        co_return;
    }
    auto pos = ctx.testPos(kStageDx);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    BlockPos origin = gf::game::blockPos(pos);
    auto     before = gf::game::blockIdAt(*region, {origin.x, origin.y, origin.z + 2});
    auto     sword  = gf::game::items::make("minecraft:iron_sword");
    auto     bread  = gf::game::items::make("minecraft:bread", 5);
    auto     helmet = gf::game::items::make("minecraft:iron_helmet");
    auto     block  = gf::game::items::make(kFixtureBlock);
    auto     map    = gf::game::items::make("minecraft:filled_map");
    ctx.require(sword && bread && helmet && block && map, "the items");
    {
        auto& p = ctx.player();
        p.setSelectedSlot(0);
        p.getInventory().setItem(0, *sword);
        p.getInventory().setItem(1, *bread);
        p.setArmor(ArmorSlot::Head, *helmet);
    }
    co_await ctx.ticks(1);
    Vec3        start;
    std::size_t items = 0;
    {
        auto& p = ctx.player();
        start   = p.getFeetPos();
        ctx.require(fx::prepareStage(*region, origin), "the stage is prepared");
        ctx.check(fx::placeNamedChest(*region, {origin.x, origin.y, origin.z + 2}, ""), "a stage chest");
        ctx.check(fx::placeFrame(*region, {origin.x, origin.y + 1, origin.z + 3}, kFixtureItem), "a stage frame with the fixture item");
        items = fx::itemEntitiesNear(*region, origin, 4);
        fx::standOnStage(p, *region, origin, 0.5f);
        fx::setHeld(p, *map); // slot 0
        fx::setHead(p, *block);
    }
    co_await ctx.ticks(1);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    {
        auto& p = ctx.player();
        p.setSelectedSlot(1); // the player switches slots between two steps
        fx::setHeld(p, *map); // slot 1
        fx::standOnStage(p, *region, origin, 1.5f);
    }
    ctx.check(fx::savedPlayerCount() >= 1, "the player is remembered");
    co_await ctx.ticks(2);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.check(fx::clearStage() > 0, "the stage blocks are put back");
    ctx.check(gf::game::blockIdAt(*region, {origin.x, origin.y, origin.z + 2}) == before, "the chest is gone again");
    ctx.check(fx::restorePlayers() == 1, "one player put back");
    co_await ctx.ticks(2);
    region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto dropped = fx::itemEntitiesNear(*region, origin, 4);
    ctx.check(dropped <= items, "the stage frame dropped nothing: " + std::to_string(items) + " item(s) before, " + std::to_string(dropped) + " after");
    auto& p = ctx.player();
    ctx.check(p.getInventory().getItem(0).getTypeName() == "minecraft:iron_sword", "slot 0 is back, got " + p.getInventory().getItem(0).getTypeName());
    ctx.check(p.getInventory().getItem(1).getTypeName() == "minecraft:bread" && p.getInventory().getItem(1).mCount == 5, "slot 1 is back, got " + p.getInventory().getItem(1).getTypeName());
    ctx.check(p.getArmor(ArmorSlot::Head).getTypeName() == "minecraft:iron_helmet", "the helmet is back");
    ctx.check(p.getFeetPos().distanceTo(start) < 1.0f, "the player is back where it was");
    ctx.check(fx::clearStage() == 0 && fx::restorePlayers() == 0 && fx::savedPlayerCount() == 0, "nothing left to put back");
});

// A stage an earlier session left elsewhere (here: far outside the loaded world) that cannot be put
// back keeps its snapshot: prepareStage refuses a new stage instead of overwriting it.
GF_REGISTER_SELFTEST("ui.fixture_stage_kept", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    if (!fx::fixturePlayers().empty() || gf::game::db::get(kStageKeyForTests)) {
        ctx.info("a stage is in use: skipped");
        co_return;
    }
    auto pos = ctx.testPos(kStageDx);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    Json far = {{"dim", 0}, {"blocks", Json::array({Json::array({2000000, 64, 2000000, "minecraft:stone", Json::object()})})}};
    gf::game::db::put(kStageKeyForTests, far);
    ctx.check(!fx::prepareStage(*region, gf::game::blockPos(pos)), "no new stage while an earlier one cannot be put back");
    auto kept = gf::game::db::get(kStageKeyForTests);
    ctx.check(kept && *kept == far, "the earlier stage's snapshot is kept");
    ctx.check(fx::clearStage() == 0, "it cannot be put back now either");
    fx::discardStage();
    ctx.check(!gf::game::db::get(kStageKeyForTests), "discarded");
});
// ---- input ---------------------------------------------------------------------------------------

namespace keytest {
int& seen() {
    static int n = 0;
    return n;
}
} // namespace keytest

GF_REGISTER_SELFTEST("ui.input_key", [](Context& ctx) -> ll::coro::CoroTask<> {
    // one handler for the process: gf::input has no removal and the test may run many times
    static bool registered = false;
    if (!registered) {
        registered = true;
        gf::input::onKey("ui.test_key", [](Player&, bool down) { keytest::seen() += down ? 1 : 10; });
    }
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    if (fx::enabled()) {
        net::setTestCompanion(p, true);
        int before = fx::keyPresses();
        net::injectFromClient(p, "input.key", {{"id", "ui.fixture"}, {"down", true}});
        net::injectFromClient(p, "input.key", {{"id", "ui.fixture"}, {"down", false}});
        ctx.check(fx::keyPresses() == before + 1, "a forwarded key press reaches its handler (releases are not presses)");
        net::setTestCompanion(p, false);
    }
    keytest::seen() = 0;
    gf::input::dispatchKey(p, "ui.test_key", true);
    gf::input::dispatchKey(p, "ui.test_key", false);
    gf::input::dispatchKey(p, "ui.unknown_key", true);
    ctx.check(keytest::seen() == 11, "press and release reach onKey handlers, got " + std::to_string(keytest::seen()));
});

// ---- movement ------------------------------------------------------------------------------------

struct EdgeLog {
    std::unordered_map<std::int64_t, std::vector<gf::movement::Edge>> byPlayer;
};
EdgeLog& edgeLog() {
    static EdgeLog l;
    static bool    registered = false;
    if (!registered) {
        registered = true;
        gf::movement::onInputEdge([](Player& p, gf::movement::Edge e) {
            auto it = edgeLog().byPlayer.find(p.getOrCreateUniqueID().rawID);
            if (it != edgeLog().byPlayer.end()) it->second.push_back(e);
        });
    }
    return l;
}

GF_REGISTER_SELFTEST("ui.movement_edges", [](Context& ctx) -> ll::coro::CoroTask<> {
    using gf::movement::Edge;
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID().rawID;
    auto& log = edgeLog();
    log.byPlayer[id].clear();
    gf::movement::InputSample s;
    gf::movement::feedInput(p, s); // baseline: nothing pressed, on the ground
    s.jump = true;
    gf::movement::feedInput(p, s);
    s.onGround = false;
    gf::movement::feedInput(p, s);
    s.jump = false;
    s.sneak = true;
    gf::movement::feedInput(p, s);
    s.onGround = true;
    s.sneak    = false;
    s.sprint   = true;
    gf::movement::feedInput(p, s);
    s.sprint = false;
    gf::movement::feedInput(p, s);
    gf::movement::feedInput(p, s); // no change: no edge
    std::vector<Edge> want{Edge::JumpPressed, Edge::LeftGround, Edge::JumpReleased, Edge::SneakPressed, Edge::SneakReleased, Edge::SprintStarted, Edge::Landed, Edge::SprintStopped};
    auto got = log.byPlayer[id];
    ctx.check(got.size() == want.size(), "eight edges, got " + std::to_string(got.size()));
    // order within one sample is fixed (jump, sneak, sprint, ground)
    std::multiset<int> a, b;
    for (auto e : got) a.insert(static_cast<int>(e));
    for (auto e : want) b.insert(static_cast<int>(e));
    ctx.check(a == b, "the expected edges");
    if (got.size() >= 2) ctx.check(got[0] == Edge::JumpPressed && got[1] == Edge::LeftGround, "jump, then the player leaves the ground");
    log.byPlayer.erase(id);
});

// On the ground comes from the auth input packet itself (its vertical collision and the y the client
// claims), not from the server's state before the packet is applied.
GF_REGISTER_SELFTEST("ui.movement_ground", [](Context& ctx) {
    using gf::movement::groundFromInput;
    // standing: collision below, no climb
    ctx.check(groundFromInput(true, 0.0f, false).value_or(false), "standing on the ground");
    // the jump tick: the client moved up, no collision
    ctx.check(!groundFromInput(false, 0.42f, false).value_or(true), "jumping leaves the ground");
    // falling: no collision
    ctx.check(!groundFromInput(false, -0.3f, false).value_or(true), "falling is in the air");
    // landing: collision while moving down
    ctx.check(groundFromInput(true, -0.2f, false).value_or(false), "landing");
    // a collision while the claimed y rose (a ceiling, a step up, standing up from sneaking): no
    // answer, the last state stays
    ctx.check(!groundFromInput(true, 0.1f, false).has_value() && !groundFromInput(true, 0.35f, false).has_value(), "a ceiling or a step up keeps the last state");
    // sneaking lowers the eye on the ground: still the ground
    ctx.check(groundFromInput(true, -0.35f, false).value_or(false), "sneaking on the ground");
    // a teleport or a respawn (jump of the position): no answer, the edge detector keeps its state
    ctx.check(!groundFromInput(true, -50.0f, false).has_value() && !groundFromInput(true, 0.0f, true).has_value(), "no ground change across a teleport");
});

GF_REGISTER_SELFTEST("ui.apply_motion", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    gf::movement::applyMotion(p, Vec3{0, 0.8f, 0});
    ctx.check(p.getPosDelta().y > 0.5f, "the server's motion of the player was set");
});

GF_REGISTER_SELFTEST("ui.movement_validate", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p      = ctx.player();
    auto  strict = p.getOrCreateUniqueID().rawID;
    auto  before = gf::movement::validatorCount();
    // rejects moves longer than 4 blocks of this test's player only; removed at the end
    gf::movement::validate("ui.test_rule", [strict](Player& who, Vec3 const& from, Vec3 const& to) {
        return who.getOrCreateUniqueID().rawID != strict || from.distanceTo(to) <= 4.f;
    });
    ctx.check(gf::movement::validatorCount() == before + 1, "the rule is registered");
    Vec3 at = p.getFeetPos();
    ctx.check(gf::movement::validateMove(p, at, at + Vec3{1, 0, 0}), "a short move passes");
    ctx.check(!gf::movement::validateMove(p, at, at + Vec3{0, 10, 0}), "a long move is rejected");
    ctx.check(gf::movement::removeValidator("ui.test_rule"), "the rule is removed");
    ctx.check(gf::movement::validatorCount() == before && !gf::movement::removeValidator("ui.test_rule"), "only once");
    ctx.check(gf::movement::validateMove(p, at, at + Vec3{0, 10, 0}) || before > 0, "without the rule the move passes");
});

// A server teleport, a respawn or a dimension change starts the validation afresh: the moves the
// client sent before it are not checked against the new place, and no correction is pending.
GF_REGISTER_SELFTEST("ui.movement_reset", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID().rawID;
    // tracks are kept with and without rules (a rule registered later starts from the last claim)
    gf::movement::resetTrack(p, gf::movement::ResetReason::Teleport);
    ctx.check(gf::movement::settling(p), "a teleport waits for the client to arrive");
    gf::movement::resetTrack(p, gf::movement::ResetReason::Respawn);
    ctx.check(gf::movement::settling(p), "a respawn too");
    gf::movement::resetTrack(p, gf::movement::ResetReason::Dimension);
    ctx.check(gf::movement::settling(p), "a dimension change too");
    ctx.check(gf::movement::pendingCorrections(id) == 0, "no correction is pending after a reset");
    gf::movement::forgetTrack(p);
    ctx.check(!gf::movement::settling(p), "forgotten");
});

// A client that never claims the new place (a dimension that loads for more than 100 packets) ends
// the wait from where the server put it: a distance rule then accepts the first real move there and
// rejects a late claim of the old place, instead of sending the player back to it.
GF_REGISTER_SELFTEST("ui.movement_arrive_timeout", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID().rawID;
    gf::movement::validate("ui.test_far_rule", [id](Player& who, Vec3 const& from, Vec3 const& to) {
        return who.getOrCreateUniqueID().rawID != id || from.distanceTo(to) <= 4.f;
    });
    Vec3 here = p.getFeetPos();
    Vec3 old  = here + Vec3{100, 0, 0}; // what a slow client keeps claiming
    gf::movement::resetTrack(p, gf::movement::ResetReason::Dimension);
    bool rejected = false;
    for (int i = 0; i < 100; ++i) rejected = gf::movement::claimMove(p, old).has_value() || rejected;
    ctx.check(!rejected && gf::movement::settling(p), "claims of the old place are skipped while the client loads");
    ctx.check(!gf::movement::claimMove(p, old).has_value() && !gf::movement::settling(p), "the wait ends after 100 packets");
    ctx.check(!gf::movement::claimMove(p, here + Vec3{1, 0, 0}).has_value(), "the first real move at the new place passes");
    auto back = gf::movement::claimMove(p, old);
    ctx.check(back.has_value() && back->distanceTo(here) < 2.f, "a late claim of the old place is rejected towards the new place");
    ctx.check(gf::movement::removeValidator("ui.test_far_rule"), "the rule is removed");
    gf::movement::forgetTrack(p);
});

// The real-client correction (client scenario, local and remote-mod): rule "ui.fixture_rule"
// rejects every horizontal move of the tagged players present now; the scenario walks and waits for
// the rejection and the correction, then ui.fixture_validate_off removes the rule and checks that
// the corrections kept the player near where it started.
struct StrictPlayers {
    std::unordered_map<std::int64_t, Vec3> start;
};
StrictPlayers& strictPlayers() {
    static StrictPlayers s;
    return s;
}

GF_REGISTER_SELFTEST("ui.fixture_validate", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    auto& s = strictPlayers();
    s.start.clear();
    for (auto* rp : fx::fixturePlayers()) s.start[rp->getOrCreateUniqueID().rawID] = rp->getFeetPos();
    gf::movement::validate("ui.fixture_rule", [](Player& p, Vec3 const& from, Vec3 const& to) {
        if (!strictPlayers().start.contains(p.getOrCreateUniqueID().rawID)) return true;
        float dx = to.x - from.x, dz = to.z - from.z;
        return dx * dx + dz * dz <= 0.05f * 0.05f;
    });
    ctx.info("ui.fixture_rule holds " + std::to_string(s.start.size()) + " fixture player(s) in place; corrections so far: " + std::to_string(gf::movement::corrections()));
});

GF_REGISTER_SELFTEST("ui.fixture_validate_off", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    gf::movement::removeValidator("ui.fixture_rule");
    auto& s = strictPlayers();
    for (auto* rp : fx::fixturePlayers()) {
        auto it = s.start.find(rp->getOrCreateUniqueID().rawID);
        if (it == s.start.end()) continue;
        Vec3  at = rp->getFeetPos();
        float dx = at.x - it->second.x, dz = at.z - it->second.z;
        float d  = std::sqrt(dx * dx + dz * dz);
        ctx.info(rp->getRealName() + " ended " + std::to_string(d) + " blocks from where the rule started");
        ctx.check(d < 1.5f, "the corrections kept " + rp->getRealName() + " in place (moved " + std::to_string(d) + ")");
    }
    s.start.clear();
    ctx.info("corrections so far: " + std::to_string(gf::movement::corrections()));
});

// The respawn step of the client scenario: the tagged players go to the stage (far from the spawn)
// and rule "ui.fixture_far_rule" rejects their moves longer than 4 blocks (a distance rule, as
// U12's); the scenario kills the player there and it respawns at the spawn: that jump must not be
// rejected nor corrected.
struct FarRule {
    std::size_t corrections = 0;
    bool        on          = false;
};
FarRule& farRule() {
    static FarRule f;
    return f;
}

GF_REGISTER_SELFTEST("ui.fixture_far", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    // the stage is far from the spawn: the respawn moves the player much more than 4 blocks; the
    // player respawns at once and keeps its inventory (gamerules kept and put back by
    // ui.fixture_far_off / ui.fixture_clear)
    if (auto st = co_await stageFor(ctx, fixturePlayersNow())) {
        for (auto* rp : fixturePlayersNow()) fx::standOnStage(*rp, *st->region, st->origin, 1.62f);
        ctx.check(fx::setRespawnRules(), "doimmediaterespawn and keepinventory are on");
    }
    std::set<std::int64_t> ids;
    for (auto* rp : fx::fixturePlayers()) ids.insert(rp->getOrCreateUniqueID().rawID);
    farRule() = {gf::movement::corrections(), true};
    gf::movement::validate("ui.fixture_far_rule", [ids](Player& p, Vec3 const& from, Vec3 const& to) {
        return !ids.contains(p.getOrCreateUniqueID().rawID) || from.distanceTo(to) <= 4.0f;
    });
    ctx.info("ui.fixture_far_rule watches " + std::to_string(ids.size()) + " fixture player(s); corrections so far: " + std::to_string(farRule().corrections));
});

GF_REGISTER_SELFTEST("ui.fixture_far_off", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    if (int rules = fx::restoreRespawnRules(); rules > 0) ctx.info(std::to_string(rules) + " gamerule(s) put back");
    bool was = gf::movement::removeValidator("ui.fixture_far_rule");
    if (!farRule().on || !was) {
        ctx.info("ui.fixture_far_rule was not on");
        return;
    }
    auto added = gf::movement::corrections() - farRule().corrections;
    farRule()  = {};
    ctx.info("corrections while ui.fixture_far_rule was on: " + std::to_string(added));
    ctx.check(added == 0, "no correction after the respawn, got " + std::to_string(added));
});

// Logs the next input edges of the tagged players (the client scenario jumps and waits for
// JumpPressed, LeftGround and Landed from the real client's auth input).
GF_REGISTER_SELFTEST("ui.fixture_edges", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    fx::logEdges(12);
    ctx.info("logging the next 12 input edges of fixture players");
});

// gf::fx::particle with the fixture's native particle: companions get F10's fx.particle with the
// call's velocity, count and params, and the client mod runs F12's factory with them (the client
// scenario waits for "particles: native factory of gf_test:fixture_particle ran (fx.particle, x2
// vel 0.250 0.500 -0.125)"); here the message a test companion gets is checked.
constexpr char kFixtureParticle[] = "gf_test:fixture_particle";
Vec3 const     kFxVelocity{0.25f, 0.5f, -0.125f};
Vec3 const     kTestVelocity{0.125f, 0.25f, 0.375f};

GF_REGISTER_SELFTEST("ui.fixture_fx", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::fixturePlayers()) {
        if (!net::isCompanion(*rp)) continue;
        gf::fx::particle(rp->getDimension(), kFixtureParticle, rp->getPosition() + Vec3{0, 0.5f, 1.5f}, kFxVelocity, 2, Json{{"ui_test", 7}});
        ++real;
    }
    ctx.info("fixture fx particle sent near " + std::to_string(real) + " companion(s)");
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    net::setTestCompanion(p, true);
    (void)net::takeTestMessages(p);
    // another velocity than the tagged players' call: a real companion near the test player gets this
    // one too (harmless: the factory only logs), and the client scenario waits for kFxVelocity
    gf::fx::particle(p.getDimension(), kFixtureParticle, p.getPosition(), kTestVelocity, 2, Json{{"ui_test", 7}});
    auto msgs = net::takeTestMessages(p);
    net::setTestCompanion(p, false);
    auto it = std::find_if(msgs.begin(), msgs.end(), [](auto const& m) { return m.channel == "fx.particle"; });
    ctx.require(it != msgs.end(), "a companion gets fx.particle");
    auto const& m = it->payload;
    ctx.check(m.value("id", std::string{}) == kFixtureParticle && m.value("count", 0) == 2, "the particle id and count");
    auto vel = m.value("vel", Json::array());
    ctx.check(vel.is_array() && vel.size() == 3 && std::abs(vel[0].get<float>() - 0.125f) < 1e-5f && std::abs(vel[1].get<float>() - 0.25f) < 1e-5f && std::abs(vel[2].get<float>() - 0.375f) < 1e-5f, "the velocity, got " + vel.dump());
    ctx.check(m.value("params", Json::object()).value("ui_test", 0) == 7, "the params");
});

} // namespace

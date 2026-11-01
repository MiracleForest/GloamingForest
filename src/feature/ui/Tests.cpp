// Selftests of F12, server side ("ui.*", dx 6200..6299; rule R12). They run in both builds; the
// client build adds the client registry tests (client/ui/ClientTests.cpp).
//
// The fixture tests ("ui.fixture_*") drive the e2e scenarios: with a real player connected (the
// client harness) they act on that player (companion: native fixture; vanilla: the form / DDUI
// fallback); in the server-only runs they act on a test companion or the test player and check
// what was recorded. Every registration a test makes is removed again (rule R12: the tests pass
// in any order and any number of times in one session).

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <unordered_map>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/common/Screens.h"
#include "feature/ext/ui/Server.h"
#include "feature/movement/Movement.h"
#include "feature/net/Net.h"
#include "feature/ui/Fixture.h"
#include "feature/ui/Ui.h"
#include "game/Blocks.h"
#include "game/Items.h"
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

/// The fixture is off in production builds (no gf_test packs): its tests only say so.
bool fixtureOff(Context& ctx) {
    if (fx::enabled()) return false;
    ctx.info("fixture off (no gf_test packs deployed): skipped");
    return true;
}

/// The feet block of a real player and the turn that makes it look south at height `dy` 2 blocks away.
struct Facing {
    BlockPos feet;
    Vec2     rot;
};
Facing faceSouth(Player& p, float targetHeight) {
    auto const& at = p.getPosition();
    BlockPos    feet{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y - 1.62f)), static_cast<int>(std::floor(at.z))};
    // the eye is 1.62 above the feet; the target `targetHeight` above them and 2 blocks away
    float pitch = static_cast<float>(std::atan2(1.62 - targetHeight, 2.0) * 180.0 / 3.14159265358979);
    return {feet, Vec2{pitch, 0.f}};
}
void standAndFace(Player& p, Facing const& f) {
    p.teleport(Vec3{static_cast<float>(f.feet.x) + .5f, static_cast<float>(f.feet.y), static_cast<float>(f.feet.z) + .5f}, p.getDimensionId(), f.rot);
}

// ---- vanilla fallbacks ---------------------------------------------------------------------------

GF_REGISTER_SELFTEST("ui.title_skin", [](Context& ctx) {
    namespace ui = gf::ui;
    // the title is the skin's texture path; the chest JSON-UI draws it and the RP texts name it (spike S5)
    auto title = ui::containerTitleSkin("gf.ui.fixture.title", "ui/fixture_chest");
    ctx.check(title == "textures/gui/ui/fixture_chest", "the title is the skin texture: " + title);
    auto back = ui::parseTitleSkin(title);
    ctx.check(back.skinId == "ui/fixture_chest" && back.title == title, "the skin reads back");
    ctx.check(ui::containerTitleSkin("k", "") == "k", "no skin: the plain title");
    ctx.check(ui::containerTitleSkin("k", "Bad Id") == "k" && ui::containerTitleSkin("k", "a.png") == "k" && ui::containerTitleSkin("k", "/abs") == "k", "ids that are no texture path keep the plain title");
    ctx.check(ui::validSkinId("guigoblintinkering") && ui::validSkinId("ui/fixture_chest") && !ui::validSkinId(""), "texture paths below textures/gui");
    ctx.check(ui::parseTitleSkin("container.chest").skinId.empty(), "plain titles carry no skin");
    ctx.check(ui::parseTitleSkin("textures/gui/").skinId.empty() && ui::parseTitleSkin("textures/gui/A B").skinId.empty(), "no skin without a valid id");
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
    // real vanilla players (the remote-vanilla e2e client) get the fixture form; the scenario
    // closes it and waits for "ui form ... closed by"
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (net::isCompanion(*rp)) continue;
        fx::openFormFallback(*rp);
        ++real;
    }
    ctx.info("fixture form sent to " + std::to_string(real) + " real vanilla player(s)");
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

// The DDUI fallback for real vanilla players (remote-vanilla e2e: shown, then closed with ESC).
GF_REGISTER_SELFTEST("ui.fixture_ddui", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (net::isCompanion(*rp)) continue;
        bool shown = gf::ui::ddui(*rp, "ui.fixture", fx::dduiDocument(), [](Player& p, Json const& r) {
            gf::log().info("ui fixture: ddui response of {}: {}", p.getRealName(), r.dump());
        });
        if (shown) ++real;
        else ctx.info("DDUI not available for " + rp->getRealName());
    }
    ctx.info("fixture DDUI sent to " + std::to_string(real) + " real vanilla player(s)");
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

GF_REGISTER_SELFTEST("ui.fixture_show", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    Json what = {{"show", true}, {"hud", true}, {"toast", true}, {"world", true}, {"screen", false}};
    int  real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (fx::show(*rp, what)) ++real;
    }
    if (real > 0) {
        ctx.info("fixture shown to " + std::to_string(real) + " companion(s)");
        co_return;
    }
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

// Opens the native fixture screen for real companions (the client scenario screenshots it).
GF_REGISTER_SELFTEST("ui.fixture_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (!net::isCompanion(*rp)) continue;
        if (gf::screens::open(*rp, "ui.fixture")) ++real;
    }
    ctx.info("fixture screen opened for " + std::to_string(real) + " companion(s)");
    (void)ctx.player();
    co_await ctx.ticks(1);
    ctx.check(gf::screens::open(ctx.player(), "ui.fixture"), "the fixture screen type is registered");
});

// Turns on the fixture's fog / sky / cloud providers for real companions (client scenario).
GF_REGISTER_SELFTEST("ui.fixture_fog", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (fx::show(*rp, {{"show", true}, {"fog", true}})) ++real;
    }
    ctx.info("fixture fog shown to " + std::to_string(real) + " companion(s)");
});

// The vanilla container skin: a vanilla chest titled with containerTitleSkin, for every real player
// two blocks south of its feet (the player is turned to it and the client scenario opens it), else
// at the test position, where only the stored name is checked.
GF_REGISTER_SELFTEST("ui.fixture_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    auto title = gf::ui::containerTitleSkin(gf::tr("gf.ui.fixture.title", "GloamingForest UI fixture"), "ui/fixture_chest");
    int  real  = 0;
    for (auto* rp : fx::realPlayers()) {
        auto f = faceSouth(*rp, 0.5f); // the chest's middle
        if (!fx::placeNamedChest(rp->getDimensionBlockSource(), {f.feet.x, f.feet.y, f.feet.z + 2}, title)) continue;
        ++real;
        standAndFace(*rp, f);
    }
    ctx.info("skinned chest placed for " + std::to_string(real) + " real player(s)");
    auto pos = ctx.testPos(6220);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.cleanupBlock(pos);
    ctx.require(fx::placeNamedChest(*region, gf::game::blockPos(pos), title), "the chest is placed and named");
    ctx.check(fx::chestName(*region, gf::game::blockPos(pos)) == title, "the chest keeps the skinned title");
    ctx.check(gf::ui::parseTitleSkin(fx::chestName(*region, gf::game::blockPos(pos))).skinId == "ui/fixture_chest", "the stored title carries the skin");
});

// A chest without a skin at the same place: the vanilla look stays (the skin image finds no texture).
GF_REGISTER_SELFTEST("ui.fixture_chest_plain", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        auto f = faceSouth(*rp, 0.5f);
        if (!fx::placeNamedChest(rp->getDimensionBlockSource(), {f.feet.x, f.feet.y, f.feet.z + 2}, "")) continue;
        ++real;
        standAndFace(*rp, f);
    }
    ctx.info("plain chest placed for " + std::to_string(real) + " real player(s)");
    auto pos = ctx.testPos(6250);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.cleanupBlock(pos);
    ctx.require(fx::placeNamedChest(*region, gf::game::blockPos(pos), ""), "the chest is placed");
    ctx.check(gf::ui::parseTitleSkin(fx::chestName(*region, gf::game::blockPos(pos))).skinId.empty(), "a plain chest carries no skin");
});

// ItemContext::Fixed: an item frame with the fixture item one block above a real player's feet, two
// blocks south (the client scenario screenshots it), else at the test position.
GF_REGISTER_SELFTEST("ui.fixture_frame", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        auto f = faceSouth(*rp, 1.5f); // the frame's middle
        if (!fx::placeFrame(rp->getDimensionBlockSource(), {f.feet.x, f.feet.y + 1, f.feet.z + 2}, kFixtureItem)) continue;
        ++real;
        standAndFace(*rp, f);
    }
    ctx.info("item frame placed for " + std::to_string(real) + " real player(s)");
    auto pos = ctx.testPos(6230, 1);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    auto at = gf::game::blockPos(pos);
    ctx.cleanupBlock(pos);
    ctx.cleanupBlock(gf::core::GlobalPos{pos.dim, pos.x, pos.y, pos.z + 1});
    ctx.require(fx::placeFrame(*region, at, kFixtureItem), "the frame is placed");
    ctx.check(fx::frameItem(*region, at) == kFixtureItem, "the frame holds the fixture item");
});

// ItemContext::Head: the fixture block on real players' heads (the client scenario screenshots it
// in third person), else on the test player.
GF_REGISTER_SELFTEST("ui.fixture_head", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    auto block = gf::game::items::make(kFixtureBlock);
    ctx.require(block.has_value(), "the fixture block item (gf_test packs)");
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        rp->setArmor(ArmorSlot::Head, *block);
        // level head: the third-person front view looks the player in the face
        rp->teleport(rp->getFeetPos(), rp->getDimensionId(), Vec2{0.f, rp->getRotation().y});
        ++real;
    }
    ctx.info("fixture block put on " + std::to_string(real) + " real player(s)");
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    p.setArmor(ArmorSlot::Head, *block);
    ctx.check(p.getArmor(ArmorSlot::Head).getTypeName() == kFixtureBlock, "the head slot holds the fixture block");
});

// Map decorations: a filled map in real players' hands and the fixture decorations on it
// (companions draw them; the client scenario screenshots the held map).
GF_REGISTER_SELFTEST("ui.fixture_map", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    auto map = gf::game::items::make("minecraft:filled_map");
    ctx.require(map.has_value(), "minecraft:filled_map");
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        rp->setSelectedItem(*map);
        rp->sendInventory(true);
        // look down at the held map (Java and Bedrock draw it larger then)
        rp->teleport(rp->getFeetPos(), rp->getDimensionId(), Vec2{60.f, rp->getRotation().y});
        if (fx::show(*rp, {{"show", true}, {"map", true}})) ++real;
    }
    ctx.info("fixture map given to " + std::to_string(real) + " companion(s)");
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

// The held fixture map in an item frame two blocks south of real players (framed maps: the client
// scenario screenshots it); the test player has no map with an id, so only the frame is checked.
GF_REGISTER_SELFTEST("ui.fixture_map_frame", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (fixtureOff(ctx)) co_return;
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        auto const& held = rp->getSelectedItem();
        if (held.isNull() || held.getTypeName() != "minecraft:filled_map") continue;
        auto f = faceSouth(*rp, 1.5f);
        if (!fx::placeFrame(rp->getDimensionBlockSource(), {f.feet.x, f.feet.y + 1, f.feet.z + 2}, held)) continue;
        ++real;
        standAndFace(*rp, f);
    }
    ctx.info("framed map placed for " + std::to_string(real) + " real player(s)");
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
});

// Clears what the fixture tests placed for real players (blocks, the fixture block on heads).
GF_REGISTER_SELFTEST("ui.fixture_clear", [](Context& ctx) {
    int n = fx::clearPlaced();
    int heads = 0;
    for (auto* rp : fx::realPlayers()) {
        if (rp->getArmor(ArmorSlot::Head).getTypeName() != kFixtureBlock) continue;
        rp->setArmor(ArmorSlot::Head, ItemStack::EMPTY_ITEM());
        ++heads;
    }
    ctx.info("removed " + std::to_string(n) + " fixture block(s) and " + std::to_string(heads) + " fixture head item(s)");
});

// Turns the client fixture off again (the client scenario runs it after its screenshots).
GF_REGISTER_SELFTEST("ui.fixture_hide", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (fx::show(*rp, {{"show", false}})) ++real;
    }
    ctx.info("fixture hidden for " + std::to_string(real) + " companion(s)");
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

// The real-client correction (client scenario, local and remote-mod): rule "ui.fixture_rule"
// rejects every horizontal move of the real players present now; the scenario walks and waits for
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
    for (auto* rp : fx::realPlayers()) s.start[rp->getOrCreateUniqueID().rawID] = rp->getFeetPos();
    gf::movement::validate("ui.fixture_rule", [](Player& p, Vec3 const& from, Vec3 const& to) {
        if (!strictPlayers().start.contains(p.getOrCreateUniqueID().rawID)) return true;
        float dx = to.x - from.x, dz = to.z - from.z;
        return dx * dx + dz * dz <= 0.05f * 0.05f;
    });
    ctx.info("ui.fixture_rule holds " + std::to_string(s.start.size()) + " real player(s) in place; corrections so far: " + std::to_string(gf::movement::corrections()));
});

GF_REGISTER_SELFTEST("ui.fixture_validate_off", [](Context& ctx) {
    if (fixtureOff(ctx)) return;
    gf::movement::removeValidator("ui.fixture_rule");
    auto& s = strictPlayers();
    for (auto* rp : fx::realPlayers()) {
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

} // namespace

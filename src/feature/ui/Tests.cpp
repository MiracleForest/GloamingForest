// Selftests of F12, server side ("ui.*", dx 6200..6299; rule R12). They run in both builds; the
// client build adds the client registry tests (client/ui/ClientTests.cpp).
//
// The fixture tests ("ui.fixture_*") drive the e2e scenarios: with a real player connected (the
// client harness) they act on that player (companion: native fixture; vanilla: the form
// fallback); in the server-only runs they act on a test companion and check the recorded messages.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <unordered_map>

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/common/Screens.h"
#include "feature/ext/ui/Server.h"
#include "feature/movement/Movement.h"
#include "feature/net/Net.h"
#include "feature/ui/Fixture.h"
#include "feature/ui/Ui.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {
using gf::selftest::Context;
using Json = nlohmann::json;
namespace net = gf::net;
namespace fx  = gf::ui::fixture;

// ---- vanilla fallbacks ---------------------------------------------------------------------------

GF_REGISTER_SELFTEST("ui.title_skin", [](Context& ctx) {
    auto t = gf::ui::containerTitleSkin("container.twilightforest.uncrafting_table", "guigoblintinkering");
    ctx.check(t.starts_with("gfskin:guigoblintinkering~"), "the title starts with the padded skin field: " + t);
    ctx.check(t.size() == 7 + gf::ui::kTitleSkinWidth + std::string("container.twilightforest.uncrafting_table").size(), "fixed-width skin field");
    auto back = gf::ui::parseTitleSkin(t);
    ctx.check(back.skinId == "guigoblintinkering" && back.titleKey == "container.twilightforest.uncrafting_table", "the JSON-UI reads the skin and the title back");
    ctx.check(gf::ui::parseTitleSkin("container.chest").skinId.empty(), "plain titles carry no skin");
    ctx.check(gf::ui::containerTitleSkin("k", std::string(gf::ui::kTitleSkinWidth + 1, 'a')) == "k", "a too long skin id leaves the title plain");
    ctx.check(gf::ui::containerTitleSkin("k", "a~b") == "k", "the padding character is refused in ids");
    auto sub = gf::ui::parseTitleSkin(gf::ui::containerTitleSkin("Name", "sub/dir"));
    ctx.check(sub.skinId == "sub/dir" && sub.titleKey == "Name", "skin paths with folders");
    // the resource pack's JSON-UI reads the same field: '%.<width>s' and '%.<7 + width>s'
    auto jsonUi = gf::Mod::getInstance().getSelf().getModDir() / "resource_packs" / "GloamingForest_RP" / "ui" / "chest_screen.json";
    std::ifstream in(jsonUi);
    std::string   text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ctx.require(!text.empty(), "the JSON-UI chest skin is deployed: " + jsonUi.string());
    auto field = "'%." + std::to_string(gf::ui::kTitleSkinWidth) + "s'";
    auto whole = "'%." + std::to_string(7 + gf::ui::kTitleSkinWidth) + "s'";
    ctx.check(text.find(field) != std::string::npos && text.find(whole) != std::string::npos, "the JSON-UI uses the skin field width " + field + " / " + whole);
});

GF_REGISTER_SELFTEST("ui.form_fallback", [](Context& ctx) -> ll::coro::CoroTask<> {
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
    auto& p   = ctx.player();
    Json  doc = {
        {"title", "ui.ddui"},
        {"elements", Json::array({{{"type", "label"}, {"text", "x"}}, {{"type", "toggle"}, {"id", "t"}, {"text", "T"}}, {{"type", "button"}, {"id", "ok"}, {"text", "OK"}}})},
    };
    ctx.check(!gf::ui::ddui(p, "ui.ddui", doc, {}), "no DDUI for a simulated player: the caller shows a form");
    ctx.check(!gf::ui::ddui(p, "ui.ddui", Json::array(), {}), "a malformed document is refused");
});

// ---- native screens (companions) -----------------------------------------------------------------

GF_REGISTER_SELFTEST("ui.screen_native", [](Context& ctx) -> ll::coro::CoroTask<> {
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
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (fx::show(*rp, {{"show", true}, {"fog", true}})) ++real;
    }
    ctx.info("fixture fog shown to " + std::to_string(real) + " companion(s)");
});

// A vanilla chest titled with containerTitleSkin (the JSON-UI skin of vanilla container
// screens): for every real player two blocks south of its feet (the client scenario faces south
// and opens it), else at the test position, where only the stored name is checked.
GF_REGISTER_SELFTEST("ui.fixture_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto title = gf::ui::containerTitleSkin(gf::tr("gf.ui.fixture.title", "GloamingForest UI fixture"), "ui/fixture_container");
    int  real  = 0;
    for (auto* rp : fx::realPlayers()) {
        auto const& at = rp->getPosition();
        BlockPos    feet{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y - 1.62f)), static_cast<int>(std::floor(at.z))};
        if (!fx::placeNamedChest(rp->getDimensionBlockSource(), {feet.x, feet.y, feet.z + 2}, title)) continue;
        ++real;
        // stand on the block centre and look at the chest (south, down to its middle: the eye is
        // 1.62 above the feet, the chest centre 0.5 above them and 2 blocks away)
        float pitch = static_cast<float>(std::atan2(1.62 - 0.5, 2.0) * 180.0 / 3.14159265358979);
        rp->teleport(Vec3{static_cast<float>(feet.x) + .5f, static_cast<float>(feet.y), static_cast<float>(feet.z) + .5f}, rp->getDimensionId(), Vec2{pitch, 0.f});
    }
    ctx.info("skinned chest placed for " + std::to_string(real) + " real player(s)");
    auto pos = ctx.testPos(6220);
    co_await ctx.ensureLoaded(pos);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "a region");
    ctx.cleanupBlock(pos);
    ctx.require(fx::placeNamedChest(*region, gf::game::blockPos(pos), title), "the chest is placed and named");
    ctx.check(fx::chestName(*region, gf::game::blockPos(pos)) == title, "the chest keeps the skinned title");
});

GF_REGISTER_SELFTEST("ui.fixture_chest_clear", [](Context& ctx) {
    int n = fx::clearChests();
    ctx.info("removed " + std::to_string(n) + " fixture chest(s)");
});

// Turns the client fixture off again (the client scenario runs it after its screenshots).
GF_REGISTER_SELFTEST("ui.fixture_hide", [](Context& ctx) {
    int real = 0;
    for (auto* rp : fx::realPlayers()) {
        if (fx::show(*rp, {{"show", false}})) ++real;
    }
    ctx.info("fixture hidden for " + std::to_string(real) + " companion(s)");
});

// ---- input ---------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("ui.input_key", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    net::setTestCompanion(p, true);
    int before = fx::keyPresses();
    net::injectFromClient(p, "input.key", {{"id", "ui.fixture"}, {"down", true}});
    net::injectFromClient(p, "input.key", {{"id", "ui.fixture"}, {"down", false}});
    ctx.check(fx::keyPresses() == before + 1, "a forwarded key press reaches its handler (releases are not presses)");
    static int seen;
    seen = 0;
    gf::input::onKey("ui.test_key", [](Player&, bool down) { seen += down ? 1 : 10; });
    gf::input::dispatchKey(p, "ui.test_key", true);
    gf::input::dispatchKey(p, "ui.test_key", false);
    gf::input::dispatchKey(p, "ui.unknown_key", true);
    ctx.check(seen == 11, "press and release reach onKey handlers");
    net::setTestCompanion(p, false);
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

std::set<std::int64_t>& strictPlayers() {
    static std::set<std::int64_t> s;
    return s;
}

GF_REGISTER_SELFTEST("ui.movement_validate", [](Context& ctx) -> ll::coro::CoroTask<> {
    static bool registered = false;
    if (!registered) {
        registered = true;
        // rejects moves longer than 4 blocks, only for players a test put on the list
        gf::movement::validate("ui.test_rule", [](Player& p, Vec3 const& from, Vec3 const& to) {
            if (!strictPlayers().contains(p.getOrCreateUniqueID().rawID)) return true;
            return from.distanceTo(to) <= 4.f;
        });
    }
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID().rawID;
    ctx.check(gf::movement::validatorCount() >= 1, "the rule is registered");
    Vec3 at = p.getPosition();
    ctx.check(gf::movement::validateMove(p, at, at + Vec3{0, 10, 0}), "other players' moves pass");
    strictPlayers().insert(id);
    ctx.check(gf::movement::validateMove(p, at, at + Vec3{1, 0, 0}), "a short move passes");
    ctx.check(!gf::movement::validateMove(p, at, at + Vec3{0, 10, 0}), "a long move is rejected");
    strictPlayers().erase(id);
});

} // namespace

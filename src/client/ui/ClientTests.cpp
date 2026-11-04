// Selftests of F12's client frameworks ("ui.client_*", client build only). They run on the server
// thread of the local world and check the registries the client thread reads. Every registration a
// test makes uses gf_test ids and is removed again (rule R12: nothing stays in the live registries).

#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacketPayload.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

#include "client/input/Input.h"
#include "client/render/Geo.h"
#include "client/render/Render.h"
#include "client/ui/Ui.h"
#include "client/world/ClientConfig.h"
#include "client/world/Sky.h"
#include "feature/ext/ui/Client.h"
#include "feature/fx/Fx.h"
#include "feature/net/Net.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using gf::selftest::Context;
using Json    = nlohmann::json;
namespace cl  = gf::client;
namespace geo = gf::client::render::geo;

GF_REGISTER_SELFTEST("ui.client_ui", [](Context& ctx) {
    // HUD layers: sorted by order, replaced by id, removable
    cl::hud::addLayer("ui.test_b", 20, [](auto&, float) {});
    cl::hud::addLayer("ui.test_a", 10, [](auto&, float) {});
    cl::hud::addLayer("ui.test_a", 30, [](auto&, float) {});
    auto layers = cl::ui::hudLayers();
    auto a = std::find(layers.begin(), layers.end(), "ui.test_a"), b = std::find(layers.begin(), layers.end(), "ui.test_b");
    ctx.check(a != layers.end() && b != layers.end() && b < a, "layers are ordered and re-registering moves a layer");
    ctx.check(std::count(layers.begin(), layers.end(), "ui.test_a") == 1, "one layer per id");
    cl::ui::removeHudLayer("ui.test_a");
    cl::ui::removeHudLayer("ui.test_b");
    layers = cl::ui::hudLayers();
    ctx.check(std::find(layers.begin(), layers.end(), "ui.test_a") == layers.end(), "layers can be removed");

    // toasts queue up with their style, and are cancelled again before they are drawn (gf_test keys:
    // nothing shows on the real client)
    auto before = cl::ui::toastCount();
    cl::toast::show({.iconItem = "minecraft:map", .titleKey = "gf_test.ui.toast_a", .bodyKey = "gf_test.ui.toast_body", .ms = 1});
    cl::toast::show({.iconItem = "minecraft:map", .titleKey = "gf_test.ui.toast_b", .bodyKey = "gf_test.ui.toast_body", .ms = 1}, cl::toast::styleOf(cl::toast::Kind::MissingAdvancement));
    ctx.check(cl::ui::toastCount() == before + 2, "two toasts were queued");
    auto styleA = cl::toast::styleOfToast("gf_test.ui.toast_a"), styleB = cl::toast::styleOfToast("gf_test.ui.toast_b");
    ctx.check(styleA && styleA->titleColor == 0xFFFFFF00u && styleA->bodyColor == 0xFFFFFFFFu, "LockedBiomeToast: title -256, body white");
    ctx.check(styleB && styleB->titleColor == 0xFFFFFFFFu && styleB->bodyColor == 0xFFFFFFFFu, "MissingAdvancementToast: white title and body");
    ctx.check(cl::toast::cancel("gf_test.ui.toast_a") == 1 && cl::toast::cancel("gf_test.ui.toast_b") == 1, "the toasts are cancelled");
    ctx.check(cl::ui::toastCount() == before && !cl::toast::styleOfToast("gf_test.ui.toast_a"), "no toast is left");
    // literal lines win over the keys (F10's MissingAdvancementToast: the advancement's title as text)
    cl::toast::show(cl::toast::LiteralToastSpec{{.iconItem = "minecraft:map", .titleKey = "gf_test.ui.toast_c", .bodyKey = "gf_test.ui.toast_body", .ms = 1}, {}, "gf_test literal body"});
    cl::toast::show(cl::toast::LiteralToastSpec{{.iconItem = "minecraft:grass_block", .ms = 1}, "gf_test literal title", "gf_test literal second"}, cl::toast::styleOf(cl::toast::Kind::MissingAdvancement));
    auto linesC = cl::toast::linesOfToast("gf_test.ui.toast_c"), linesD = cl::toast::linesOfToast("gf_test literal title");
    ctx.check(linesC && linesC->first == "gf_test.ui.toast_c" && linesC->second == "gf_test literal body", "a key without a translation shows as itself, the literal body wins over the body key");
    ctx.check(linesD && linesD->first == "gf_test literal title" && linesD->second == "gf_test literal second", "a toast with literal lines only");
    auto styleD = cl::toast::styleOfToast("gf_test literal title");
    ctx.check(styleD && styleD->titleColor == 0xFFFFFFFFu, "a toast is found by its literal title");
    ctx.check(cl::toast::cancel("gf_test.ui.toast_c") == 1 && cl::toast::cancel("gf_test literal title") == 1 && cl::toast::cancel("") == 0, "cancelled by key and by literal title (an empty title matches none)");
    ctx.check(cl::ui::toastCount() == before, "no toast is left");
    ctx.check(std::string_view(cl::toast::kFrameTexture) == "textures/ui/greyBorder" && cl::toast::kFrameSlice == 4 && cl::toast::kFrameSize == 16, "the toast frame is Bedrock's own toast texture, a 16 px nine-slice with 4 px corners");
    ctx.info(std::string("GUI hidden (F1): ") + (cl::hud::guiHidden() ? "yes" : "no"));

    // tooltips: lines of the item's providers, removed again at the end
    cl::tooltip::add("gf_test:fixture_item", [](ItemStack const& s, std::vector<std::string>& lines) {
        if (gf::game::items::getData(s, "ui_test").is_null()) return;
        lines.push_back("ui test line");
    });
    auto stick = gf::game::items::make("gf_test:fixture_item");
    ctx.require(stick.has_value(), "the fixture item (gf_test packs)");
    ctx.check(cl::ui::tooltipLines(*stick).empty(), "no line without the data");
    gf::game::items::setData(*stick, "ui_test", true);
    auto lines = cl::ui::tooltipLines(*stick);
    ctx.check(lines.size() == 1 && lines[0] == "ui test line", "the provider's line");
    ctx.check(cl::ui::tooltipLines(*gf::game::items::make("minecraft:dirt")).empty(), "other items get nothing");
    // the whole hover text (ContainerScreen tooltips) reads the client level: the client thread's,
    // so only the empty case here (the fixture screen's tooltip shows it in the e2e screenshots)
    ctx.check(cl::ui::hoverLines(ItemStack::EMPTY_ITEM()).empty(), "an empty stack has no hover text");
    ctx.check(cl::ui::removeTooltips("gf_test:fixture_item") && cl::ui::tooltipLines(*stick).empty(), "the provider is removed");

    // the fixture keys (development builds deploy the gf_test packs): "ui.fixture" from mod load is
    // bound by LeviLamina, "ui.fixture_late" from install comes from the raw key events
    auto keys = cl::keys::registered();
    ctx.info("registered keys: " + std::to_string(keys.size()));
    ctx.check(!cl::keys::down("ui.not_a_key") && !cl::keys::lateBound("ui.not_a_key"), "an unknown key is up and not late");
    if (std::find(keys.begin(), keys.end(), "ui.fixture_late") != keys.end()) {
        ctx.check(cl::keys::lateBound("ui.fixture_late"), "a key registered at install is late");
        ctx.check(!cl::keys::lateBound("ui.fixture"), "a key registered at mod load is bound");
    }
});

// ContainerScreen states (slot stacks, the title) live as long as their screen: a screen that is not
// the open one (covered, or not opened yet) keeps its stacks over many frames, and its state goes
// when the screen is destroyed. A screen whose widgets were cleared (its life token with them) falls
// back to the time rule.
GF_REGISTER_SELFTEST("ui.client_container_state", [](Context& ctx) -> ll::coro::CoroTask<> {
    std::size_t before = cl::ui::containerStateCount();
    auto        map    = gf::game::items::make("minecraft:map");
    ctx.require(map.has_value(), "minecraft:map");
    auto s = std::make_unique<cl::screen::ContainerScreen>("ui test", "", 176, 166);
    s->slots.push_back({0, 8, 18});
    cl::ui::setContainerItems(*s, Json{{"0", gf::game::items::toJson(*map)}});
    ctx.check(cl::ui::containerStateCount() == before + 1, "a state for the screen");
    co_await ctx.ticks(40); // 2 s: many frames and UI passes
    ctx.check(cl::ui::containerStateCount() == before + 1, "a live screen that is not the open one keeps its state");
    s.reset();
    ctx.check(cl::ui::containerStateCount() == before, "the state goes with its screen");

    // a subclass that clears `widgets` (the token with them) while the screen is not open
    auto o = std::make_unique<cl::screen::ContainerScreen>("ui test 2", "", 176, 166);
    o->widgets.clear();
    cl::ui::setContainerItems(*o, Json{{"0", gf::game::items::toJson(*map)}});
    ctx.check(cl::ui::containerStateCount() == before + 1, "a state for the screen without its token");
    o.reset(); // nothing tells F12: the time rule (10 s without being the open one) drops it
    for (int i = 0; i < 300 && cl::ui::containerStateCount() > before; ++i) co_await ctx.ticks(1);
    ctx.check(cl::ui::containerStateCount() == before, "the state of a screen without its token goes after the grace time");
});
GF_REGISTER_SELFTEST("ui.client_render", [](Context& ctx) {
    namespace r = cl::render;
    r::registerBlockRenderer("gf_test:ui_test_block", [](auto&, auto const&, auto const&, float) {}, {.throughWalls = true});
    cl::tint::registerBlock("gf_test:ui_test_block", [](BlockPos const& p) { return 0x102030u + static_cast<unsigned>(p.y & 0xFF); });
    r::registerEntityRenderer("gf_test:ui_test_entity", [](auto&, auto&, float) {});
    r::registerItemRenderer("gf_test:ui_test_item", [](auto&, auto const&, auto) {}, r::Gui | r::Ground | r::Fixed | r::Head);
    ctx.check(r::hasBlockRenderer("gf_test:ui_test_block"), "the block renderer is registered");
    ctx.check(!r::hasBlockRenderer("minecraft:stone"), "others are not");
    ctx.check(r::hasItemRenderer("gf_test:ui_test_item", r::Fixed) && r::hasItemRenderer("gf_test:ui_test_item", r::Head), "Fixed and Head item contexts are taken");
    ctx.check(!r::hasItemRenderer("gf_test:ui_test_item", r::FirstPerson), "contexts not asked for are not");
    auto t = r::tintOf("gf_test:ui_test_block", 1, 5, 2);
    ctx.check(t && *t == 0x102035u, "the tint resolver gets the absolute position");
    ctx.check(!r::tintOf("minecraft:grass_block", 0, 0, 0), "no tint for others");
    ctx.info("tracked block positions: " + std::to_string(r::trackedBlockCount()));
    ctx.check(r::unregisterBlockRenderer("gf_test:ui_test_block") && !r::hasBlockRenderer("gf_test:ui_test_block"), "block renderer removed");
    ctx.check(cl::tint::unregisterBlock("gf_test:ui_test_block") && !r::tintOf("gf_test:ui_test_block", 0, 0, 0), "tint removed");
    ctx.check(r::unregisterEntityRenderers("gf_test:ui_test_entity"), "entity renderer removed");
    ctx.check(r::unregisterItemRenderer("gf_test:ui_test_item") && !r::hasItemRenderer("gf_test:ui_test_item", r::Gui), "item renderer removed");
    ctx.check(!r::unregisterItemRenderer("gf_test:ui_test_item"), "only once");

    // a tint that builds on the game's colours: the game's foliage reaches the function; without a
    // world there is none (TF then takes its colour without a world)
    struct Colours final : cl::tint::GameColors {
        std::optional<unsigned> foliage() const override { return 0x48B518u; }
        std::optional<unsigned> grass() const override { return 0x91BD59u; }
        std::optional<unsigned> water() const override { return std::nullopt; }
    };
    cl::tint::registerBlock("gf_test:ui_test_leaves", [](BlockPos const& p, cl::tint::GameColors const& game) {
        auto f = game.foliage();
        return f ? (*f & 0xFEFEFEu) / 2 + static_cast<unsigned>(p.x & 1) : 0x112233u;
    });
    auto none = r::tintOf("gf_test:ui_test_leaves", 0, 0, 0);
    auto with = r::tintOf("gf_test:ui_test_leaves", 1, 0, 0, Colours{});
    ctx.check(none && *none == 0x112233u, "without a world the function gets no game colour");
    ctx.check(with && *with == (0x48B518u & 0xFEFEFEu) / 2 + 1, "the game's foliage colour reaches the function");
    // a plain TintFn keeps working next to it, and gets the absolute position only
    cl::tint::registerBlock("gf_test:ui_test_block", [](BlockPos const& p) { return 0x000100u * static_cast<unsigned>(p.z & 0xFF); });
    auto plain = r::tintOf("gf_test:ui_test_block", 0, 0, 3, Colours{});
    ctx.check(plain && *plain == 0x000300u, "a plain tint function ignores the game's colours");
    ctx.check(cl::tint::unregisterBlock("gf_test:ui_test_leaves") && cl::tint::unregisterBlock("gf_test:ui_test_block"), "tints removed");
});

// Name tags: hidden by entity type (all, or with a predicate) or by a predicate of any type, shown
// again by type or handle. Checked on the test player (no real entity's tag is hidden for longer than
// the test).
GF_REGISTER_SELFTEST("ui.client_name_tags", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace r = cl::render;
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto&        p    = ctx.player();
    std::int64_t self = p.getOrCreateUniqueID().rawID;
    auto         isMe = [self](Actor const& a) { return a.getOrCreateUniqueID().rawID == self; };
    // the rules of the player type are replaced below: another module's rule is put back at the end
    std::string const type  = p.getTypeName();
    auto const        saved = r::nameTagRule(type);
    if (saved) r::showNameTag(type);
    ctx.check(!r::nameTagHidden(p), "no rule hides the test player's name tag");
    r::hideNameTag(type, isMe);
    ctx.check(r::nameTagHidden(p), "hidden by its type and the predicate");
    r::hideNameTag(p.getTypeName(), [](Actor const&) { return false; });
    ctx.check(!r::nameTagHidden(p), "a later rule for the type replaces the earlier one");
    ctx.check(r::showNameTag(p.getTypeName()) && !r::showNameTag(p.getTypeName()), "the type's rule is removed once");
    r::hideNameTag("gf_test:ui_test_entity");
    ctx.check(!r::nameTagHidden(p), "a rule for another type leaves it alone");
    ctx.check(r::showNameTag("gf_test:ui_test_entity"), "removed");
    auto handle = r::hideNameTag(isMe);
    ctx.check(handle != 0 && r::nameTagHidden(p), "hidden by a predicate of any type");
    ctx.check(r::hideNameTag(r::NameTagFn{}) == 0, "an empty predicate is refused");
    ctx.check(r::showNameTag(handle) && !r::showNameTag(handle) && !r::nameTagHidden(p), "the predicate is removed by its handle, once");
    if (saved) r::hideNameTag(type, *saved);
    ctx.check(r::nameTagRule(type).has_value() == saved.has_value(), "the player type's rule from before the test is back");
});

GF_REGISTER_SELFTEST("ui.client_geo", [](Context& ctx) {
    Json file = Json::parse(R"({
        "format_version": "1.12.0",
        "minecraft:geometry": [{
            "description": {"identifier": "geometry.ui.test", "texture_width": 32, "texture_height": 32},
            "bones": [
                {"name": "root", "pivot": [0, 0, 0], "cubes": [{"origin": [-4, 0, -4], "size": [8, 2, 8], "uv": [0, 0]}]},
                {"name": "arm", "parent": "root", "pivot": [0, 2, 0], "rotation": [0, 90, 0],
                 "cubes": [{"origin": [0, 2, 0], "size": [4, 1, 1], "uv": {"north": {"uv": [0, 10], "uv_size": [4, 1]}}}]}
            ]
        }]
    })");
    std::string error;
    auto        models = geo::parse(file, &error);
    ctx.require(models.contains("geometry.ui.test"), "parsed: " + error);
    auto const& m = models.at("geometry.ui.test");
    ctx.check(m.quads.size() == 7, "six box faces and one per-face face, got " + std::to_string(m.quads.size()));
    // the box: 8 px = half a block on each side, mirrored x keeps it symmetric
    float minX = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int q = 0; q < 6; ++q)
        for (auto const& p : m.quads[static_cast<std::size_t>(q)].pos) minX = std::min(minX, p[0]), maxX = std::max(maxX, p[0]), maxY = std::max(maxY, p[1]);
    ctx.check(std::abs(minX + .25f) < 1e-4f && std::abs(maxX - .25f) < 1e-4f && std::abs(maxY - .125f) < 1e-4f, "the box spans -0.25..0.25 x 0..0.125");
    // the arm (x 0..4 px) turned 90 degrees about Y: it now lies along z
    auto const& arm = m.quads.back();
    float       ax0 = 1e9f, ax1 = -1e9f, az0 = 1e9f, az1 = -1e9f;
    for (auto const& p : arm.pos) ax0 = std::min(ax0, p[0]), ax1 = std::max(ax1, p[0]), az0 = std::min(az0, p[2]), az1 = std::max(az1, p[2]);
    ctx.check(az1 - az0 > .2f && ax1 - ax0 < 1e-3f, "the bone rotation turned the arm onto z");
    ctx.check(std::abs(arm.uv[0][0]) < 1e-5f && std::abs(arm.uv[0][1] - 10.f / 32) < 1e-5f && std::abs(arm.uv[1][0] - 4.f / 32) < 1e-5f, "per-face UV normalised");
    ctx.check(geo::parse(Json::object(), &error).empty() && !error.empty(), "a file without geometry is refused");
    ctx.check(geo::find("geometry.ui.not_there") == nullptr, "unknown ids are not found");
    ctx.info(std::string("fixture geometry ") + (geo::find("geometry.gf_ui.fixture") ? "loaded" : "missing"));
});

GF_REGISTER_SELFTEST("ui.client_world", [](Context& ctx) {
    // the 29 TF particle ids are declared; factories run instead of the data-driven particle
    auto decl = cl::particles::declared();
    ctx.check(decl.size() == 29, "29 TF particle ids declared");
    ctx.check(std::any_of(decl.begin(), decl.end(), [](auto const& d) { return d.first == "twilightforest:firefly"; }), "firefly is declared");
    static int spawned = 0;
    spawned            = 0;
    cl::particles::registerNative("gf_test:ui_test_particle", [](Vec3 const&, Vec3 const& v, Json const& p) { spawned += 1 + static_cast<int>(v.y) + p.value("n", 0); });
    ctx.check(cl::particles::spawn("gf_test:ui_test_particle", {0, 0, 0}, {0, 2, 0}, {{"n", 4}}) && spawned == 7, "the factory got position, velocity and params");
    ctx.check(!cl::particles::spawn("twilightforest:not_a_particle", {}, {}), "no factory: the caller spawns the data-driven particle");
    ctx.check(cl::particles::unregisterNative("gf_test:ui_test_particle") && !cl::particles::spawn("gf_test:ui_test_particle", {}, {}), "the factory is removed");

    // sky providers per dimension
    constexpr int kDim = 9999;
    ctx.check(cl::sky::providerOf(kDim) == nullptr, "no provider yet");
    cl::sky::setProvider(kDim, {.fog = [](std::string const& biome) -> std::optional<cl::sky::FogSpec> {
                              if (biome == "twilightforest:spooky_forest") return cl::sky::FogSpec{{1, 0, 0}, 0, 8};
                              return std::nullopt;
                          }});
    auto p = cl::sky::providerOf(kDim);
    ctx.check(p && p->fog && p->fog("twilightforest:spooky_forest")->end == 8 && !p->fog("plains"), "the fog provider answers per biome");
    cl::sky::clearProvider(kDim);
    ctx.check(cl::sky::providerOf(kDim) == nullptr, "providers can be removed");

    // maps: icons and extra decorations (gf_test ids, removed again)
    cl::maps::setIcon("gf_test:ui_test_icon", "textures/gui/ui/fixture_geo");
    ctx.check(cl::maps::iconOf("gf_test:ui_test_icon") == "textures/gui/ui/fixture_geo", "map icon registered");
    ctx.check(cl::maps::iconOf("twilightforest:unknown").empty(), "unknown icon");
    constexpr std::int64_t kMap = -987654321;
    cl::maps::setDecorations(kMap, {{.type = "gf_test:ui_test_icon", .x = 10, .z = -20, .rotation = 4, .name = "n"}});
    auto decs = cl::maps::decorationsOf(kMap);
    ctx.check(decs.size() == 1 && decs[0].x == 10 && decs[0].z == -20 && decs[0].rotation == 4, "decorations kept per map");
    cl::maps::setDecorations(kMap, {});
    ctx.check(cl::maps::decorationsOf(kMap).empty(), "an empty list removes them");
    // TF's decoration renderers: a conquered icon (red X) and the player icon on top
    cl::maps::setDecorations(
        kMap,
        {{.type = "gf_test:ui_test_icon", .x = 0, .z = 0, .rotation = 0, .name = ""}, {.type = "minecraft:player", .x = 2, .z = 2, .rotation = 8, .name = ""}},
        {{.conquered = true}}
    );
    auto styles = cl::maps::stylesOf(kMap);
    ctx.check(styles.size() == 2 && styles[0].conquered && !styles[0].topmost && !styles[1].conquered && !styles[1].topmost, "one style per decoration, missing ones plain");
    cl::maps::setDecorations(kMap, {{.type = "minecraft:player", .x = 2, .z = 2, .rotation = 8, .name = ""}}, {{.topmost = true}});
    ctx.check(cl::maps::stylesOf(kMap).size() == 1 && cl::maps::stylesOf(kMap)[0].topmost, "the player icon on top");
    cl::maps::setDecorations(kMap, {});
    ctx.check(cl::maps::stylesOf(kMap).empty(), "styles go with the decorations");
    // item frames: Java's showOnItemFrame (false for the vanilla player markers, true for TF's icons),
    // or the style's own onFrame
    ctx.check(!cl::maps::showOnItemFrame("minecraft:player") && !cl::maps::showOnItemFrame("minecraft:0") && !cl::maps::showOnItemFrame("minecraft:player_off_map"), "the player markers are not drawn on item frames");
    ctx.check(cl::maps::showOnItemFrame("minecraft:red_x") && cl::maps::showOnItemFrame("twilightforest:lich_tower") && cl::maps::showOnItemFrame("minecraft:frame"), "TF's icons, the red X and the frame marker are");
    ctx.check(!cl::maps::hasDecorations(kMap) && !cl::maps::decorated(kMap), "no decorations: none reported");
    cl::maps::setDecorations(kMap, {{.type = "minecraft:player", .x = 0, .z = 0, .rotation = 0, .name = ""}, {.type = "gf_test:ui_test_icon", .x = 0, .z = 0, .rotation = 0, .name = ""}}, {{}, {.onFrame = false}});
    auto snap = cl::maps::decorated(kMap);
    ctx.check(cl::maps::hasDecorations(kMap) && snap && snap->decorations.size() == 2 && snap->styles.size() == 2, "one snapshot with the decorations and their styles");
    ctx.check(snap && !snap->styles[0].onFrame && snap->styles[1].onFrame == false, "onFrame: the type's own unless the style says");
    cl::maps::setDecorations(kMap, {});
    ctx.check(snap && snap->decorations.size() == 2, "a snapshot stays valid after the map changed");
    ctx.check(!cl::maps::hasDecorations(kMap), "removed");
    // icons: registered textures (whole), vanilla icons as cells of the vanilla atlas
    auto own = cl::maps::iconFor("gf_test:ui_test_icon");
    ctx.check(own && own->texture == "textures/gui/ui/fixture_geo" && own->u0 == 0 && own->u1 == 1, "a registered icon is its whole texture");
    auto player = cl::maps::iconFor("minecraft:player"), x = cl::maps::iconFor("minecraft:red_x"), byIndex = cl::maps::iconFor("minecraft:4");
    ctx.check(player && player->texture == "textures/map/map_icons" && player->u0 == 0 && player->v0 == 0 && player->u1 == .25f, "the player icon is the atlas's first cell");
    ctx.check(x && byIndex && x->u0 == 0 && x->v0 == .25f && byIndex->v0 == .25f && x->v1 == .5f, "the red X is cell 4, by name and by index");
    ctx.check(!cl::maps::iconFor("minecraft:16") && !cl::maps::iconFor("twilightforest:unknown"), "no icon for others");
    ctx.check(cl::maps::removeIcon("gf_test:ui_test_icon") && cl::maps::iconOf("gf_test:ui_test_icon").empty(), "icon removed");
    ctx.check(!cl::maps::view(-123456789).has_value(), "an unknown map has no view");

    // camera: shakes count down only while the client config allows them; the latest call sets the
    // strength and a longer one extends the shake (a negligible intensity: the real camera barely moves)
    auto& cfg               = gf::config::mut<gf::client::ClientConfig>();
    bool  keep              = cfg.screenShakingEffect;
    cfg.screenShakingEffect = false;
    cl::camera::shake(1.0f, 10);
    ctx.check(cl::camera::shaking() == 0, "no shake with screenShakingEffect off");
    cfg.screenShakingEffect = true;
    cl::camera::shake(0.002f, 20);
    cl::camera::shake(0.001f, 5);
    ctx.check(cl::camera::shaking() > 5 && cl::camera::shakeIntensity() == 0.001f, "the latest intensity, the longer duration");
    cfg.screenShakingEffect = keep;
});

// Native particles reached from the server: gf::fx::particle for a companion (F10's fx.particle
// handler asks F12's factories through gf::fx::client::setNativeSink) with its velocity, count,
// params and spread, and SpawnParticleEffectPackets as the vanilla handler takes them (another
// dimension: dropped; bound to an actor: at the actor plus the offset). The factory only records:
// the local player sees nothing.
struct FxSeen {
    std::mutex        mutex;
    std::vector<Vec3> pos, vel;
    std::vector<Json> params;
};
FxSeen& fxSeen() {
    static FxSeen s;
    return s;
}
std::size_t fxCount() {
    std::lock_guard lock(fxSeen().mutex);
    return fxSeen().pos.size();
}
void fxClear() {
    std::lock_guard lock(fxSeen().mutex);
    fxSeen().pos.clear();
    fxSeen().vel.clear();
    fxSeen().params.clear();
}

/// The local player on the server side (a companion: the client mod's handshake), 0 when none.
std::int64_t companionId() {
    std::int64_t id    = 0;
    auto         level = ll::service::getLevel();
    if (!level) return 0;
    level->forEachPlayer([&id](Player& p) {
        if (p.isSimulated() || !gf::net::isCompanion(p)) return true;
        id = p.getOrCreateUniqueID().rawID;
        return false;
    });
    return id;
}

GF_REGISTER_SELFTEST("ui.client_fx_particle", [](Context& ctx) -> ll::coro::CoroTask<> {
    constexpr char kId[] = "gf_test:ui_fx_particle";
    auto           who   = companionId();
    if (who == 0) {
        ctx.info("no companion player in this world: skipped");
        co_return;
    }
    fxClear();
    cl::particles::registerNative(kId, [](Vec3 const& pos, Vec3 const& vel, Json const& params) {
        std::lock_guard lock(fxSeen().mutex);
        fxSeen().pos.push_back(pos);
        fxSeen().vel.push_back(vel);
        fxSeen().params.push_back(params);
    });
    struct Unregister {
        ~Unregister() { cl::particles::unregisterNative("gf_test:ui_fx_particle"); }
    } unregister;
    auto wait = [&ctx](std::size_t n) -> ll::coro::CoroTask<> {
        for (int i = 0; i < 60 && fxCount() < n; ++i) co_await ctx.ticks(1);
    };
    auto player = [who]() -> Player* {
        auto level = ll::service::getLevel();
        return level ? level->getPlayer(ActorUniqueID{who}) : nullptr; // rule R16
    };

    // gf::fx::particle: three particles, the velocity and params of the call
    Vec3 at{};
    {
        auto* p = player();
        ctx.require(p != nullptr, "the companion player");
        at = p->getPosition() + Vec3{0, 1, 0};
        gf::fx::particle(p->getDimension(), kId, at, Vec3{0.25f, 0.5f, -0.125f}, 3, Json{{"ui_test", 7}});
    }
    co_await wait(3);
    {
        std::lock_guard lock(fxSeen().mutex);
        auto const&     s = fxSeen();
        ctx.check(s.pos.size() == 3, "fx.particle ran the factory 3 times, got " + std::to_string(s.pos.size()));
        bool same = !s.pos.empty();
        for (std::size_t i = 0; i < s.pos.size(); ++i) {
            same = same && s.pos[i].distanceTo(at) < 1e-3f && s.vel[i].distanceTo(Vec3{0.25f, 0.5f, -0.125f}) < 1e-4f && s.params[i].value("ui_test", 0) == 7;
        }
        ctx.check(same, "each at the position with the call's velocity and params");
    }
    // params "spread": each particle offset (gaussian x distance), as gf::fx does for vanilla clients
    fxClear();
    {
        auto* p = player();
        ctx.require(p != nullptr, "the companion player");
        gf::fx::particle(p->getDimension(), kId, at, Vec3{}, 4, Json{{"spread", {0.5, 0.0, 0.5}}});
    }
    co_await wait(4);
    {
        std::lock_guard lock(fxSeen().mutex);
        auto const&     s     = fxSeen();
        bool            moved = false, flat = s.pos.size() == 4;
        for (auto const& v : s.pos) {
            moved = moved || std::abs(v.x - at.x) > 1e-4f || std::abs(v.z - at.z) > 1e-4f;
            flat  = flat && std::abs(v.y - at.y) < 1e-4f;
        }
        ctx.check(flat && moved, "four particles spread in x and z, none in y, got " + std::to_string(s.pos.size()));
    }

    // a SpawnParticleEffectPacket of another dimension is dropped, as the vanilla handler drops it
    fxClear();
    int dim = 0;
    {
        auto* p = player();
        ctx.require(p != nullptr, "the companion player");
        dim = static_cast<int>(p->getDimensionId());
        SpawnParticleEffectPacket other(SpawnParticleEffectPacketPayload(at, kId, static_cast<uchar>(dim == 0 ? 1 : 0), std::nullopt));
        other.sendTo(*p);
    }
    co_await ctx.ticks(10);
    ctx.check(fxCount() == 0, "a packet for another dimension runs no factory");
    // bound to the player: at the player's position plus the packet's offset
    Vec3 expect{};
    {
        auto* p = player();
        ctx.require(p != nullptr, "the companion player");
        expect = p->getPosition() + Vec3{0, 0.5f, 0};
        SpawnParticleEffectPacket bound(SpawnParticleEffectPacketPayload(Vec3{0, 0.5f, 0}, kId, static_cast<uchar>(dim), std::nullopt));
        bound.mActorId = ActorUniqueID{who};
        bound.sendTo(*p);
    }
    co_await wait(1);
    {
        std::lock_guard lock(fxSeen().mutex);
        auto const&     s = fxSeen();
        ctx.check(s.pos.size() == 1, "a packet bound to the player ran the factory once, got " + std::to_string(s.pos.size()));
        if (!s.pos.empty()) {
            ctx.check(s.pos[0].distanceTo(expect) < 1.5f, "at the player plus the offset (" + std::to_string(s.pos[0].distanceTo(expect)) + " blocks off)");
            ctx.check(s.params[0].value("actor", std::int64_t{0}) == who, "params carry the actor");
        }
    }
});

} // namespace

#endif // GF_CLIENT

// Selftests of F12's client frameworks ("ui.client_*", client build only). They run on the server
// thread of the local world and check the registries the client thread reads. Every registration a
// test makes uses gf_test ids and is removed again (rule R12: nothing stays in the live registries).

#ifdef GF_CLIENT

#include <algorithm>
#include <memory>

#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "client/input/Input.h"
#include "client/render/Geo.h"
#include "client/render/Render.h"
#include "client/ui/Ui.h"
#include "client/world/ClientConfig.h"
#include "client/world/Sky.h"
#include "feature/ext/ui/Client.h"
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

// ContainerScreen states (slot stacks, the title) go away once their screen is no longer open.
GF_REGISTER_SELFTEST("ui.client_container_state", [](Context& ctx) -> ll::coro::CoroTask<> {
    std::size_t before = cl::ui::containerStateCount();
    {
        cl::screen::ContainerScreen s("ui test", "", 176, 166);
        s.slots.push_back({0, 8, 18});
        cl::ui::setContainerItems(s, Json{{"0", gf::game::items::toJson(*gf::game::items::make("minecraft:map"))}});
        ctx.check(cl::ui::containerStateCount() >= 1, "a state for the screen"); // (states of closed screens may go meanwhile)
    } // the screen is gone without ever being open: the UI render passes drop its state
    for (int i = 0; i < 40 && cl::ui::containerStateCount() > before; ++i) co_await ctx.ticks(1);
    ctx.check(cl::ui::containerStateCount() <= before, "the state of a closed screen is dropped, " + std::to_string(cl::ui::containerStateCount()) + " left");
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

} // namespace

#endif // GF_CLIENT

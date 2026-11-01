// Selftests of F12's client frameworks ("ui.client_*", client build only). They run on the server
// thread of the local world and check the registries the client thread reads.

#ifdef GF_CLIENT

#include <algorithm>
#include <memory>

#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "client/input/Input.h"
#include "client/render/Geo.h"
#include "client/render/Render.h"
#include "client/ui/Ui.h"
#include "client/world/Sky.h"
#include "feature/ext/ui/Client.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using gf::selftest::Context;
using Json         = nlohmann::json;
namespace cl       = gf::client;
namespace geo      = gf::client::render::geo;

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

    // toasts queue up
    auto before = cl::ui::toastCount();
    cl::toast::show({.iconItem = "minecraft:map", .titleKey = "gf.ui.fixture.toast_title", .bodyKey = "gf.ui.fixture.toast_body", .ms = 1});
    ctx.check(cl::ui::toastCount() == before + 1, "a toast was queued");

    // tooltips: lines of the item's providers
    static bool registered = false;
    if (!registered) {
        registered = true;
        cl::tooltip::add("minecraft:stick", [](ItemStack const& s, std::vector<std::string>& lines) {
            if (gf::game::items::getData(s, "ui_test").is_null()) return;
            lines.push_back("ui test line");
        });
    }
    auto stick = gf::game::items::make("minecraft:stick");
    ctx.require(stick.has_value(), "a stick");
    ctx.check(cl::ui::tooltipLines(*stick).empty(), "no line without the data");
    gf::game::items::setData(*stick, "ui_test", true);
    auto lines = cl::ui::tooltipLines(*stick);
    ctx.check(lines.size() == 1 && lines[0] == "ui test line", "the provider's line");
    ctx.check(cl::ui::tooltipLines(*gf::game::items::make("minecraft:dirt")).empty(), "other items get nothing");

    // the fixture key (development builds deploy the gf_test packs)
    auto keys = cl::keys::registered();
    ctx.info("registered keys: " + std::to_string(keys.size()));
    ctx.check(!cl::keys::down("ui.not_a_key"), "an unknown key is up");
});

GF_REGISTER_SELFTEST("ui.client_render", [](Context& ctx) {
    static bool registered = false;
    if (!registered) {
        registered = true;
        cl::render::registerBlockRenderer("gf_test:fixture_block", [](auto&, auto const&, auto const&, float) {}, {.throughWalls = true});
        cl::tint::registerBlock("gf_test:fixture_block", [](BlockPos const& p) { return 0x102030u + static_cast<unsigned>(p.y & 0xFF); });
        cl::render::registerEntityRenderer("gf_test:boss", [](auto&, auto&, float) {});
        cl::render::registerItemRenderer("gf_test:fixture_item", [](auto&, auto const&, auto) {}, cl::render::Gui | cl::render::Ground);
    }
    ctx.check(cl::render::hasBlockRenderer("gf_test:fixture_block"), "the block renderer is registered");
    ctx.check(!cl::render::hasBlockRenderer("minecraft:stone"), "others are not");
    auto t = cl::render::tintOf("gf_test:fixture_block", 1, 5, 2);
    ctx.check(t && *t == 0x102035u, "the tint resolver gets the absolute position");
    ctx.check(!cl::render::tintOf("minecraft:grass_block", 0, 0, 0), "no tint for others");
    ctx.info("tracked block positions: " + std::to_string(cl::render::trackedBlockCount()));
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
    static bool registered = false;
    if (!registered) {
        registered = true;
        cl::particles::registerNative("gf_test:fixture_particle", [](Vec3 const&, Vec3 const& v, Json const& p) { spawned += 1 + static_cast<int>(v.y) + p.value("n", 0); });
    }
    spawned = 0;
    ctx.check(cl::particles::spawn("gf_test:fixture_particle", {0, 0, 0}, {0, 2, 0}, {{"n", 4}}) && spawned == 7, "the factory got position, velocity and params");
    ctx.check(!cl::particles::spawn("twilightforest:not_a_particle", {}, {}), "no factory: the caller spawns the data-driven particle");

    // sky providers per dimension
    constexpr int kDim = 9999;
    ctx.check(cl::sky::providerOf(kDim) == nullptr, "no provider yet");
    cl::sky::setProvider(kDim, {.fog = [](std::string const& biome) -> std::optional<cl::sky::FogSpec> {
                              if (biome == "twilightforest:spooky_forest") return cl::sky::FogSpec{{1, 0, 0}, 0, 8};
                              return std::nullopt;
                          }});
    auto const* p = cl::sky::providerOf(kDim);
    ctx.check(p && p->fog && p->fog("twilightforest:spooky_forest")->end == 8 && !p->fog("plains"), "the fog provider answers per biome");
    cl::sky::clearProvider(kDim);
    ctx.check(cl::sky::providerOf(kDim) == nullptr, "providers can be removed");

    // maps: icons and extra decorations
    cl::maps::setIcon("twilightforest:lich_tower_icon", "textures/map/lich_tower_icon");
    ctx.check(cl::maps::iconOf("twilightforest:lich_tower_icon") == "textures/map/lich_tower_icon", "map icon registered");
    ctx.check(cl::maps::iconOf("twilightforest:unknown").empty(), "unknown icon");
    ctx.check(!cl::maps::view(-123456789).has_value(), "an unknown map has no view");

    // camera: FOV modifiers compose in order; shakes follow the client config
    static bool fovRegistered = false;
    if (!fovRegistered) {
        fovRegistered = true;
        cl::camera::addFovModifier([](float f) { return f; }); // a neutral modifier stays harmless
    }
    ctx.check(std::abs(cl::camera::applyFovModifiers(1.0f) - 1.0f) < 1e-6f, "neutral modifiers keep the FOV");
});

} // namespace

#endif // GF_CLIENT

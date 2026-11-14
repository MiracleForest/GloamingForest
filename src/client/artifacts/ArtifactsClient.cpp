// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/overlay/ShieldOverlay.java
//   src/main/java/gloamingforest/client/event/OverlayHandler.java (fortification_shield_count, ore_meter_stats)
//   src/main/java/gloamingforest/client/event/ColorHandler.java
//   src/main/java/gloamingforest/client/event/ClientRegistrationEvents.java
//   src/main/java/gloamingforest/asmhooks/ArmorHooks.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client build only. upstream's two HUD overlays of U9 on F12's HUD layers:
//   - the fortification shield count ("artifacts.shields" from the server): up to 10
//     gloamingforest:fortification_shield sprites (9 x 9) at x = width / 2 - 91 + 8i above the
//     armour row, while shields are up, the player can be hurt (or the creative key is on) and the
//     indicator key is on (config section "artifacts", the client's copy);
//   - the ore meter's stats while one is held: the loading box ("Loading" and 1..3 dots) or the
//     results box at the top left (headers, then rows sorted by count: prettified columns name / dash
//     / count / ratio with headers, else "name - count (ratio%)"), padding 4, line height + 1,
//     background 0x9B000000. The rows come from the stack's ore_data, as the server wrote it.
// And two item looks:
//   - the potion flasks' icons (ColorHandler, ClientRegistrationEvents' breakage / potion_level
//     properties): Java's item model overrides with layer0 (the liquid) tinted by
//     PotionContents#getColor under layer1 (the glass), as prebuilt composites per doses, breakage and
//     colour (assets/pack/rp/textures/items/artifacts/upstream-derived, item atlas keys
//     gf_artifacts_<brittle|greater>_flask_*), chosen through U11's icon layers (feature/ext/tgear);
//   - worn armour with the emperor's cloth is not drawn (ArmorHooks#cancelArmorRendering, F12's hideArmor).
#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "core/artifacts/Artifacts.h"
#include "feature/artifacts/Artifacts.h"
#include "feature/ext/tgear/IconHooks.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::artifacts {

namespace ar    = gf::feature::artifacts;
namespace items = gf::game::items;
namespace icons = ::gf::ext::tgear::client;

namespace {

constexpr char kShieldSprite[] = "textures/gloamingforest/gui/sprites/fortification_shield";
constexpr unsigned kBox        = 0x9B000000u;
constexpr unsigned kWhite      = 0xFFFFFFFFu;
constexpr unsigned kGray       = 0xFFAAAAAAu;
constexpr float    kLine       = 10.0F; // font line height 9 + 1

std::atomic<int> gShields{0};

bool armorShown(LocalPlayer& p) {
    for (int i = 0; i < 4; ++i)
        if (!items::isEmpty(p.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i)))) return true;
    return false;
}

/// ShieldOverlay#render.
void drawShields(screen::Canvas& canvas, float) {
    auto* p = currentPlayer();
    int   n = gShields.load();
    if (!p || n <= 0 || hud::guiHidden()) return;
    auto const& cfg = ar::config();
    if (!cfg.fortificationShieldIndicator) return;
    if (p->isCreative() && !cfg.fortificationShieldIndicatorInCreative) return;
    auto [w, h]     = screen::guiSize();
    float leftHeight = 39.0F + 10.0F + (armorShown(*p) ? 10.0F : 0.0F); // the hearts and the armour row
    for (int i = 0; i < std::min(n, 10); ++i) {
        screen::Rect dst{w / 2.0F - 91.0F + static_cast<float>(i * 8) - canvas.originX(), h - leftHeight - canvas.originY(), 9.0F, 9.0F};
        if (!ui::blit(canvas, kShieldSprite, dst, 0, 0, 9, 9, 9, 9)) canvas.fill(dst, 0xFFB0B0FFu);
    }
}

std::string oreName(nlohmann::json const& data, std::string const& id) {
    auto names = data.value("names", nlohmann::json::object());
    if (names.contains(id) && names[id].is_string()) return names[id].get<std::string>();
    auto colon = id.find(':');
    return colon == std::string::npos ? id : id.substr(colon + 1);
}

/// OverlayHandler#renderOreMeterStats.
void drawOreMeter(screen::Canvas& canvas, float) {
    auto* p = currentPlayer();
    if (!p || hud::guiHidden()) return;
    ItemStack const* meter = nullptr;
    if (items::idOf(p->getCarriedItem()) == ar::kOreMeter) meter = &p->getCarriedItem();
    else if (items::idOf(p->getOffhandSlot()) == ar::kOreMeter) meter = &p->getOffhandSlot();
    if (!meter) return;
    float ox = -canvas.originX(), oy = -canvas.originY();
    auto  loadingJ = items::getData(*meter, ar::kDataOreLoading);
    if (!loadingJ.is_null()) {
        int         dots = (loadingJ.get<int>() / 5) % 3;
        std::string text = ctr("misc.gloamingforest.ore_meter_loading", "Loading") + std::string(static_cast<std::size_t>(dots + 1), '.');
        canvas.fill(screen::Rect{ox, oy, 56, 16}, kBox);
        canvas.text(ox + 4, oy + 4, text, kWhite, screen::Align::Left, false);
        return;
    }
    auto data = items::getData(*meter, ar::kDataOreData);
    if (!data.is_object()) return;
    auto chunk = data.value("chunk", nlohmann::json::array({0, 0}));
    int  total = data.value("total", 0);
    int  range = data.value("range", 1);
    std::vector<std::string> headers{
        ctr("misc.gloamingforest.ore_meter_range", "Radius: %s, Origin: [%s, %s]", std::to_string(range), chunk[0].dump(), chunk[1].dump()),
        ctr("misc.gloamingforest.ore_meter_total", "Total blocks scanned: %s", std::to_string(total)),
    };
    std::vector<std::pair<std::string, int>> rows;
    for (auto const& [k, v] : data.value("counts", nlohmann::json::object()).items()) rows.emplace_back(k, v.get<int>());
    std::stable_sort(rows.begin(), rows.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    struct Column {
        std::vector<std::pair<std::string, unsigned>> cells;
        float                                         width = 0;
        int                                           align = 0; // 0 left, 1 centre, 2 right
    };
    std::vector<Column> columns;
    auto measure = [&](Column& c) {
        for (auto const& [t, _] : c.cells) c.width = std::max(c.width, canvas.textWidth(t));
    };
    std::string sep = ctr("misc.gloamingforest.ore_meter_separator", "-");
    if (ar::config().prettifyOreMeterGui) {
        Column names, dash, counts, ratios, pad;
        names.cells.emplace_back(ctr("misc.gloamingforest.ore_meter_header_block", "Block"), kGray);
        dash.cells.emplace_back("", kWhite);
        counts.cells.emplace_back(ctr("misc.gloamingforest.ore_meter_header_count", "Count"), kGray);
        ratios.cells.emplace_back(ctr("misc.gloamingforest.ore_meter_header_ratio", "Ratio"), kGray);
        for (auto const& [id, n] : rows) {
            names.cells.emplace_back(oreName(data, id), kWhite);
            dash.cells.emplace_back(sep, kWhite);
            counts.cells.emplace_back(std::to_string(n), kWhite);
            ratios.cells.emplace_back(ctr("misc.gloamingforest.ore_meter_ratio", "(%s%%)", core::artifacts::oreRatio(n, total)), kWhite);
        }
        dash.align = 1, counts.align = 2, ratios.align = 2;
        pad.width  = canvas.textWidth(" ");
        for (auto* c : {&names, &dash, &counts, &ratios}) measure(*c);
        columns = {names, pad, dash, pad, counts, pad, ratios};
    } else {
        Column one;
        for (auto const& [id, n] : rows)
            one.cells.emplace_back(oreName(data, id) + " " + sep + " " + std::to_string(n) + " " + ctr("misc.gloamingforest.ore_meter_ratio", "(%s%%)", core::artifacts::oreRatio(n, total)), kWhite);
        measure(one);
        columns = {one};
    }
    float sum = 0, headerWidth = 0;
    std::size_t maxRows = 0;
    for (auto const& c : columns) sum += c.width, maxRows = std::max(maxRows, c.cells.size());
    for (auto const& t : headers) headerWidth = std::max(headerWidth, canvas.textWidth(t));
    float width = std::max(sum, headerWidth);
    float rowsN = static_cast<float>(headers.size() + maxRows);
    canvas.fill(screen::Rect{ox, oy, width + 8, rowsN * kLine + 6}, kBox);
    float x = ox + 4, y = oy + 4;
    for (auto const& t : headers) {
        canvas.text(x, y, t, kWhite, screen::Align::Left, false);
        y += kLine;
    }
    for (auto const& c : columns) {
        float cy = y;
        for (auto const& [t, color] : c.cells) {
            float tw = canvas.textWidth(t);
            float tx = c.align == 0 ? x : c.align == 1 ? x + (c.width - tw) / 2 : x + c.width - tw;
            canvas.text(tx, cy, t, color, screen::Align::Left, false);
            cy += kLine;
        }
        x += c.width;
    }
}

/// The flask contents of a stack: U9's item data, else upstream's component as an advancement icon carries
/// it ("gloamingforest:flask_contents" {potion: {potion: <Java id>}, doses, breakage, breakable}).
core::artifacts::Flask flaskLook(ItemStack const& stack) {
    auto f = ar::flaskOf(stack);
    if (!f.empty() || f.doses > 0 || f.breakage > 0) return f;
    auto j = items::getData(stack, "gloamingforest:flask_contents");
    if (!j.is_object()) return f;
    f.doses        = j.value("doses", 0);
    f.breakage     = j.value("breakage", 0);
    auto        pot  = j.value("potion", nlohmann::json::object());
    std::string java = pot.is_object() ? pot.value("potion", std::string{}) : std::string{};
    for (int aux = 0; aux < 64 && !java.empty(); ++aux)
        if (auto const* p = core::artifacts::potionByAux(aux); p && p->javaId == java) {
            f.potion = aux;
            break;
        }
    return f;
}

/// The brittle / greater flask models: potion_level = doses (overrides up to 3), breakage 0..2.
std::optional<icons::IconLayers> flaskIcon(ItemStack const& stack) {
    bool brittle  = items::idOf(stack) == ar::kBrittleFlask;
    auto f        = flaskLook(stack);
    int  doses    = std::clamp(f.doses, 0, 3);
    int  breakage = brittle ? std::clamp(f.breakage, 0, 2) : 0;
    if (doses == 0 || f.empty()) {
        if (breakage == 0) return std::nullopt; // the item's own icon (layer0 blank)
        return icons::IconLayers{.base = "gf_artifacts_brittle_flask_0_" + std::to_string(breakage)};
    }
    char hex[8];
    std::snprintf(hex, sizeof hex, "%06x", core::artifacts::potionColor(f.potion) & 0xFFFFFFu);
    std::string key = brittle ? "gf_artifacts_brittle_flask_" + std::to_string(doses) + "_" + std::to_string(breakage) + "_" + hex
                              : "gf_artifacts_greater_flask_" + std::to_string(doses) + "_" + hex;
    return icons::IconLayers{.base = std::move(key)};
}

} // namespace

class ArtifactsClientModule {
public:
    void install() {
        static bool const once = [] {
            hud::addLayer("artifacts.shields", 100, drawShields);
            hud::addLayer("artifacts.ore_meter", 9000, drawOreMeter);
            feature::net::client::on(ar::kChanShields, [](nlohmann::json const& msg) {
                gShields = msg.value("temporary", 0) + msg.value("permanent", 0);
            });
            feature::net::client::on(ar::kChanCharm, [](nlohmann::json const& msg) {
                log().info("artifacts: charm animation {}", msg.value("item", std::string{}));
            });
            icons::setIconLayers(ar::kBrittleFlask, flaskIcon);
            icons::setIconLayers(ar::kGreaterFlask, flaskIcon);
            // ArmorHooks#cancelArmorRendering: worn armour (the elytra too) wearing the cloth is not drawn
            render::hideArmor([](ItemStack const& s) { return ar::hasCloth(s); });
            return true;
        }();
        (void)once;
    }
    void uninstall() { gShields = 0; }
};

} // namespace gf::client::artifacts

GF_REGISTER_MODULE(gf::client::artifacts::ArtifactsClientModule);

#endif // GF_CLIENT

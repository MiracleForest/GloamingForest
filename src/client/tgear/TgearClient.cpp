// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/TravellersClientEvents.java
//   src/main/java/twilightforest/client/overlay/ItemDisplayOverlay.java
//   src/main/java/twilightforest/client/overlay/display/{ClockDisplay,CompassDisplay,MapDisplay,MoonDialDisplay,SimpleTextDisplay}.java
//   src/main/java/twilightforest/client/renderer/block/RedThreadRenderer.java
//   src/main/java/twilightforest/init/TFKeyBinds.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U11 in the client mod: TF's four key bindings (zoom Z, swap hotbar V, map cycle C, red thread
// vision unbound), the goggles' zoom (FOV x 0.3 while the key is held, synced with
// "tgear.goggles_zoom"), the item display HUD (ItemDisplayOverlay), red thread through walls while
// the vision is on or red thread is held (RedThreadRenderer's glow), and the modifier descriptions
// while Shift is held (TF's Shift tooltip; the rest of the tooltip is the server's lore).
#ifdef GF_CLIENT

#include <atomic>
#include <cmath>
#include <optional>
#include <vector>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Render.h"
#include "client/ui/Ui.h"
#include "client/world/Sky.h"
#include "core/tgear/Logic.h"
#include "feature/dimension/World.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "feature/tgear/Config.h"
#include "feature/tgear/Gear.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);

namespace gf::client::tgear {

namespace {

namespace tg    = feature::tgear;
namespace items = game::items;
using Slot      = SharedTypes::Legacy::ArmorSlot;

constexpr char kZoomKey[]      = "tgear.zoom";
constexpr char kSwapKey[]      = "tgear.swap_hotbar";
constexpr char kCycleKey[]     = "tgear.item_display_map_cycle";
constexpr char kThreadKey[]    = "tgear.red_thread_vision";
constexpr char kCategory[]     = "key.twilightforest.categories.travellers_gear";

struct ClientState {
    std::atomic<bool> zooming{false};     ///< is_using_goggles_zoom_modifier (local)
    std::atomic<float> zoomFactor{0.3f};
    std::atomic<bool> threadVision{true}; ///< travellers_goggles_red_thread_vision (default true, kept on death)
    std::atomic<bool> showThreads{false}; ///< red threads glow this frame
};

ClientState& st() {
    static ClientState s;
    return s;
}

// ---- zoom ----------------------------------------------------------------------------------------------

/// TravellersClientEvents#updateZoomState, per client tick: the key held (not scoping) with an
/// active zoom ability; a change is sent to the server, which plays the sound for everyone.
void tickZoom(Player& player) {
    auto const& head  = player.getArmor(Slot::Head);
    bool        zoom  = keys::down(kZoomKey) && !player.isScoping() &&tg::isActiveOn(player, head, tg::ids::kZoom);
    st().zoomFactor   = static_cast<float>(tg::numberOf(tg::ids::kZoom, 0.3));
    if (zoom == st().zooming.load()) return;
    st().zooming = zoom;
    feature::net::client::send("tgear.goggles_zoom", {{"zoom", zoom}});
    log().info("tgear: zoom {}", zoom ? "in" : "out");
}

void tickThreads(Player& player) {
    bool holding = items::idOf(player.getSelectedItem()) == tg::ids::kRedThread;
    bool vision  = st().threadVision.load() && tg::isActive(player, tg::ids::kRedThreadVision);
    st().showThreads = holding || vision;
}

// ---- keys ----------------------------------------------------------------------------------------------

void registerKeys() {
    keys::registerKey(kZoomKey, 'Z', kCategory, [] {});
    keys::registerKey(kSwapKey, 'V', kCategory, [] {
        // TravellersClientEvents#swapHotbar: only with the ability / modifier and a container
        auto* p = currentPlayer();
        if (!p || !feature::tgear::hasContainer(p->getArmor(Slot::Legs))) return;
        auto const& legs = p->getArmor(Slot::Legs);
        if (!(tg::hasModifier(legs, tg::ids::kSwapHotbar) || tg::hasModifier(legs, tg::ids::kSwapHotbarAbility))) return;
        feature::net::client::send("tgear.swap_hotbar", nlohmann::json::object());
        log().info("tgear: swap hotbar key");
    });
    keys::registerKey(kCycleKey, 'C', kCategory, [] {
        if (!currentPlayer()) return;
        feature::net::client::send("tgear.cycle_map_slot", nlohmann::json::object());
        log().info("tgear: map cycle key");
    });
    keys::registerKey(kThreadKey, 0, kCategory, [] {
        // toggleBooleanDataAttachment: only while the modifier is active
        auto* p = currentPlayer();
        if (!p || !tg::isActive(*p, tg::ids::kRedThreadVision)) return;
        st().threadVision = !st().threadVision.load();
        log().info("tgear: red thread vision {}", st().threadVision.load() ? "on" : "off");
    });
}

// ---- item display HUD (ItemDisplayOverlay) ---------------------------------------------------------------

constexpr float kLine = 9.f; // Java's font.lineHeight

struct Widget {
    core::tgear::DisplayType type;
    nlohmann::json           item;
    std::string              text;
    float                    width = 0, height = kLine;
};

bool natural(Dimension const& dim) {
    auto id = static_cast<int>(dim.getDimensionId());
    return id == 0 || feature::world::isTwilight(dim);
}

std::int64_t dayTime(Player const& player) {
    if (feature::world::isTwilight(player.getDimension()))
        if (auto fixed = feature::world::fixedTime()) return *fixed;
    return player.getLevel().getTime();
}

bool use24Hours() {
    auto const& cfg = config::get<tg::TgearConfig>().itemDisplay;
    if (cfg.followLocale) {
        auto loc = clientLocale();
        return !(loc == "en_US" || loc == "en_CA" || loc == "en_AU");
    }
    return cfg.twentyFourHourFormat;
}

void drawMap(screen::Canvas& c, float x0, float y0, std::int64_t mapId) {
    // the checkerboard background (100 x 100) and the map at scale 0.7075, offset 4.75
    c.fill({x0, y0, 100, 100}, 0xFF7F6A4C);
    c.fill({x0 + 2, y0 + 2, 96, 96}, 0xFFD8C8A0);
    auto view = maps::view(mapId);
    if (!view || view->pixels.size() < static_cast<std::size_t>(view->size * view->size)) return;
    float scale = 0.7075f * 128.f / static_cast<float>(view->size);
    float mx = x0 + 4.75f, my = y0 + 4.75f;
    for (int z = 0; z < view->size; ++z) {
        int run = 0;
        for (int x = 0; x <= view->size; ++x) {
            bool     end = x == view->size || view->pixels[z * view->size + x] != view->pixels[z * view->size + run];
            if (!end) continue;
            unsigned argb = view->pixels[z * view->size + run];
            if ((argb >> 24) != 0) c.fill({mx + run * scale, my + z * scale, (x - run) * scale, scale}, argb | 0xFF000000);
            run = x;
        }
    }
}

void drawHud(screen::Canvas& canvas, float) {
    if (hud::guiHidden()) return;
    auto* player = currentPlayer();
    if (!player) return;
    auto const& goggles = player->getArmor(Slot::Head);
    if (!tg::isActiveOn(*player, goggles, tg::ids::kItemDisplay)) return;
    auto d = tg::displayOf(goggles);
    if (!d) return;
    std::vector<Widget> widgets;
    float               widest = 0;
    for (int i = 0; i < core::tgear::kDisplaySlots; ++i) {
        auto const& item = d->items[i];
        if (item.is_null()) continue;
        auto type = core::tgear::kDisplayLayout[i];
        if (type == core::tgear::DisplayType::Map && i != d->chosenMapSlot) continue;
        if (tg::displayTypeOf(item.value("id", std::string())) != type) continue;
        Widget w{type, item};
        auto&  dim = player->getDimension();
        switch (type) {
        case core::tgear::DisplayType::Map: w.width = 100, w.height = 102; break;
        case core::tgear::DisplayType::Compass: {
            auto p = game::blockPosOf(player->getPosition());
            w.text  = std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z);
            w.width = canvas.textWidth(w.text);
            break;
        }
        case core::tgear::DisplayType::Clock: {
            w.text  = natural(dim) ? core::tgear::clockText(dayTime(*player), use24Hours())
                                   : ctr("travellers_gear.modifier.twilightforest.item_display.clock.unknown", "Time Unknown");
            w.width = canvas.textWidth(w.text) + (natural(dim) ? 10 : 0);
            break;
        }
        case core::tgear::DisplayType::MoonDial: {
            int phase = static_cast<int>((player->getLevel().getTime() / 24000 % 8 + 8) % 8);
            w.text    = natural(dim) ? ctr("item.twilightforest.moon_dial.phase_" + std::to_string(phase), "")
                                     : ctr("item.twilightforest.moon_dial.phase_unknown", "Moon phase indeterminate");
            w.width   = canvas.textWidth(w.text) + 10;
            break;
        }
        default: continue;
        }
        widest = std::max(widest, w.width);
        widgets.push_back(std::move(w));
    }
    if (widgets.empty()) return;
    // sorted by display position: MAP (TOP) first, the others in slot order
    std::stable_sort(widgets.begin(), widgets.end(), [](Widget const& a, Widget const& b) {
        return (a.type == core::tgear::DisplayType::Map) > (b.type == core::tgear::DisplayType::Map);
    });
    auto const& cfg   = config::get<tg::TgearConfig>().itemDisplay;
    float       scale = static_cast<float>(cfg.screenScale);
    float       x0    = static_cast<float>(cfg.screenOffsetX);
    float       y     = static_cast<float>(cfg.screenOffsetY);
    auto&       dim   = player->getDimension();
    for (auto const& w : widgets) {
        float half = std::floor(widest / 2);
        switch (w.type) {
        case core::tgear::DisplayType::Map:
            if (auto id = tg::mapIdOf(w.item)) drawMap(canvas, x0 + std::max(half - 50, 0.f) * scale, y, *id);
            break;
        case core::tgear::DisplayType::Compass: canvas.text(x0 + half * scale, y, w.text, 0xFFFFFFFF, screen::Align::Centre); break;
        case core::tgear::DisplayType::Clock:
        case core::tgear::DisplayType::MoonDial: {
            float tw   = canvas.textWidth(w.text);
            bool  icon = w.type == core::tgear::DisplayType::MoonDial || natural(dim);
            if (icon) {
                std::array<int, 2> uv;
                char const*        tex;
                float              texW;
                if (w.type == core::tgear::DisplayType::Clock) {
                    int k = core::tgear::clockFrame(dayTime(*player));
                    uv    = {k % 2 * 8, k / 2 % 2 * 8};
                    tex   = "textures/twilightforest/gui/time";
                    texW  = 16;
                } else {
                    uv   = core::tgear::moonIconUv(static_cast<int>((player->getLevel().getTime() / 24000 % 8 + 8) % 8));
                    tex  = "textures/twilightforest/gui/moon";
                    texW = 32;
                }
                ui::blit(canvas, tex, {x0 + (half - 5 - std::floor(tw / 2)) * scale, y, 8 * scale, 8 * scale}, static_cast<float>(uv[0]), static_cast<float>(uv[1]), 8, 8, texW, 16);
                canvas.text(x0 + std::max(0.f, half + 5 - std::floor(tw / 2)) * scale, y, w.text, 0xFFFFFFFF);
            } else {
                canvas.text(x0 + half * scale, y, w.text, 0xFFFFFFFF, screen::Align::Centre);
            }
            break;
        }
        default: break;
        }
        y += w.height * scale;
    }
}

// ---- red thread (RedThreadRenderer) ---------------------------------------------------------------------

constexpr char const* kFaceStates[6]     = {"twilightforest:down", "twilightforest:up", "twilightforest:north", "twilightforest:south", "twilightforest:west", "twilightforest:east"};
constexpr int         kFaceOffsets[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

bool threadFace(BlockSource& region, BlockPos const& p, int face) {
    auto const& b = region.getBlock(p);
    return game::blockId(b) == tg::ids::kRedThread && game::intState(b, kFaceStates[face]).value_or(0) != 0;
}

/// The thread on each face drawn as glowing lines through walls: the centre piece and its
/// connections to the same face of the neighbouring threads and to the block's other faces.
void drawThread(render::RenderContext& ctx, BlockPos const& pos, Block const& block, float) {
    if (!st().showThreads.load()) return;
    auto* player = currentPlayer();
    if (!player) return;
    auto&          region = player->getDimensionBlockSource();
    render::Lines  lines(ctx);
    constexpr unsigned kRed = 0xFFFF2020;
    for (int f = 0; f < 6; ++f) {
        if (game::intState(block, kFaceStates[f]).value_or(0) == 0) continue;
        Vec3 c{pos.x + 0.5f + kFaceOffsets[f][0] * 0.49f, pos.y + 0.5f + kFaceOffsets[f][1] * 0.49f, pos.z + 0.5f + kFaceOffsets[f][2] * 0.49f};
        // the 2 x 2 pixel centre piece
        for (int d = 0; d < 6; ++d) {
            if (d / 2 == f / 2) continue;
            Vec3 e{c.x + kFaceOffsets[d][0] / 16.f, c.y + kFaceOffsets[d][1] / 16.f, c.z + kFaceOffsets[d][2] / 16.f};
            lines.line(c, e, kRed);
            BlockPos n{pos.x + kFaceOffsets[d][0], pos.y + kFaceOffsets[d][1], pos.z + kFaceOffsets[d][2]};
            bool connect = threadFace(region, n, f) || game::intState(block, kFaceStates[d]).value_or(0) != 0;
            if (connect) lines.line(c, Vec3{c.x + kFaceOffsets[d][0] * 0.5f, c.y + kFaceOffsets[d][1] * 0.5f, c.z + kFaceOffsets[d][2] * 0.5f}, kRed);
        }
    }
}

// ---- Shift descriptions ---------------------------------------------------------------------------------

void describe(ItemStack const& stack, std::vector<std::string>& lines) {
    if (GetAsyncKeyState(0x10 /* VK_SHIFT */) >= 0) return;
    auto list = tg::abilityModifiers(stack);
    auto ins  = tg::insertableModifiers(stack);
    list.insert(list.end(), ins.begin(), ins.end());
    for (auto const& id : list) {
        auto m = tg::modifier(id);
        if (!m || !m->hasDescription) continue;
        auto colon = id.find(':');
        auto key   = "travellers_gear.modifier." + id.substr(0, colon) + "." + id.substr(colon + 1) + ".description";
        lines.push_back("§l" + ctr("travellers_gear.info_indent", "  ⤷ ") + "§r§7" + ctr(key, ""));
    }
}

class TgearClientModule {
public:
    void load() { registerKeys(); }
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        camera::addFovModifier([](float fov) { return st().zooming.load() ? fov * st().zoomFactor.load() : fov; });
        movement::onLocalTick(0, [](movement::MoveState&) {
            if (auto* p = currentPlayer()) {
                tickZoom(*p);
                tickThreads(*p);
            }
        });
        hud::addLayer("tgear.item_display", 100, drawHud);
        render::registerBlockRenderer(tg::ids::kRedThread, drawThread, render::Flags{.throughWalls = true});
        for (auto const* id : tg::kGearItems) tooltip::add(id, describe);
        // trackers' zoom state (the goggles' texture flip is not drawn: Bedrock's attachable keeps one
        // texture, docs/units/tgear.md)
        feature::net::client::on("tgear.goggles_zoom", [](nlohmann::json const&) {});
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::tgear

GF_REGISTER_MODULE(gf::client::tgear::TgearClientModule);

#endif // GF_CLIENT

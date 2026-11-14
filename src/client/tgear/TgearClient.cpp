// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/TravellersClientEvents.java
//   src/main/java/gloamingforest/client/overlay/ItemDisplayOverlay.java
//   src/main/java/gloamingforest/client/overlay/display/{ClockDisplay,CompassDisplay,MapDisplay,MoonDialDisplay,SimpleTextDisplay}.java
//   src/main/java/gloamingforest/client/renderer/block/RedThreadRenderer.java
//   src/main/java/gloamingforest/client/model/entity/RedThreadModel.java
//   src/main/java/gloamingforest/client/model/item/TravellersGearItemModel.java
//   src/main/java/gloamingforest/item/travellers_gear/TravellersArmorItem.java
//   src/main/java/gloamingforest/init/GloamingKeyBinds.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U11 in the client mod: upstream's four key bindings (zoom Z, swap hotbar V, map cycle C, red thread
// vision unbound), the goggles' zoom (FOV x 0.3 and the slower turn while the key is held, synced with
// "tgear.goggles_zoom"), the item display HUD (ItemDisplayOverlay), the red thread (RedThreadRenderer:
// the connections and corner connections, full bright through walls while the vision is on or red
// thread is held), the worn models' state (gloves on the vest, the belt on swap-hotbar wings, the
// goggles' "down" texture while zooming, the first-person glove overlay) as molang variables the
// attachables read, the gear's item icons (broken texture, modifier overlays: TravellersGearItemModel)
// and upstream's tooltip (Shift descriptions under each modifier).
#ifdef GF_CLIENT

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "glm/gtc/matrix_transform.hpp"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/blockactor/MovingBlockActorRenderer.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/shared_types/persona/ArmSizeType.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/player/SerializedSkinImpl.h"
#include "mc/world/actor/player/SerializedSkinRef.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Render.h"
#include "client/render/TexturePath.h"
#include "client/ui/Ui.h"
#include "client/world/Sky.h"
#include "core/tgear/Logic.h"
#include "feature/dimension/World.h"
#include "feature/ext/tgear/ClientHooks.h"
#include "feature/ext/tgear/IconHooks.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "feature/tgear/Config.h"
#include "feature/tgear/Gear.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);

namespace gf::client::tgear {

namespace {

namespace tg    = feature::tgear;
namespace items = game::items;
using Slot      = SharedTypes::Legacy::ArmorSlot;

constexpr char kZoomKey[]   = "tgear.zoom";
constexpr char kSwapKey[]   = "tgear.swap_hotbar";
constexpr char kCycleKey[]  = "tgear.item_display_map_cycle";
constexpr char kThreadKey[] = "tgear.red_thread_vision";
constexpr char kCategory[]  = "key.gloamingforest.categories.travellers_gear";

struct ClientState {
    std::atomic<bool> zooming{false};     ///< is_using_goggles_zoom_modifier (local)
    std::atomic<bool> threadVision{true}; ///< travellers_goggles_red_thread_vision (the server's, default true)
    std::atomic<bool> showThreads{false}; ///< red threads glow this frame
    std::mutex        mutex;
    std::unordered_map<std::int64_t, bool> trackedZoom; ///< other players' zoom (GogglesZoomPacket)
};

ClientState& st() {
    static ClientState s;
    return s;
}

bool shiftDown() { return GetAsyncKeyState(0x10 /* VK_SHIFT */) < 0; }

// ---- zoom ----------------------------------------------------------------------------------------------

/// TravellersClientEvents#slowZoomSensitivity: the sensitivity m becomes (0.5 - 1 / (6 m)) m / (zoom +
/// 0.05) and the turn (m * 0.6 + 0.2)^3 without vanilla's "* 8"; Bedrock's turn delta is scaled by the
/// ratio of the two turns at Java's default sensitivity (0.5).
float zoomTurnScale(float zoom) {
    constexpr double m      = 0.5;
    double           slowed = (0.5 - 1.0 / (6.0 * m)) * m / (zoom + 0.05);
    double           turn   = std::pow(slowed * 0.6 + 0.2, 3.0);
    double           normal = std::pow(m * 0.6 + 0.2, 3.0) * 8.0;
    return static_cast<float>(turn / normal);
}

/// The client's FOV without the variable (sprint / speed) modifiers: LevelRendererPlayer#getFov.
float baseFov() {
    try {
        auto* c = currentClient();
        auto* r = c ? c->getLevelRenderer() : nullptr;
        if (r) {
            auto const& lrp = r->mLevelRendererPlayer.get();
            if (lrp) {
                float f = lrp->getFov(1.f, false);
                if (f > 1.f && f < 179.f) return f;
            }
        }
    } catch (...) {}
    return 70.f;
}

/// TravellersClientEvents#updateZoomState, per client tick: the key held (not scoping) with an
/// active zoom ability; a change is sent to the server, which plays the sound for everyone and narrows
/// this player's camera FOV (ComputeFovModifierEvent: x zoom) through the vanilla camera. The turn
/// follows the state here.
void tickZoom(Player& player) {
    auto const& head = player.getArmor(Slot::Head);
    bool        zoom = keys::down(kZoomKey) && !player.isScoping() && tg::isActiveOn(player, head, tg::ids::kZoom);
    float       z    = static_cast<float>(tg::numberOf(tg::ids::kZoom, 0.3));
    ext::tgear::client::setTurnScale(zoom ? zoomTurnScale(z) : 1.f);
    if (zoom == st().zooming.load()) return;
    st().zooming = zoom;
    // player.playSound: the zooming player hears it at once, nobody else
    if (auto* c = currentClient())
        c->playUI(zoom ? "item.gloamingforest.travellers_goggles.zoom_in" : "item.gloamingforest.travellers_goggles.zoom_out", 1.f, 1.f);
    float fov    = baseFov() * z;
    feature::net::client::send("tgear.goggles_zoom", {{"zoom", zoom}, {"fov", fov}});
    log().info("tgear: zoom {} (fov x{} = {})", zoom ? "in" : "out", zoom ? z : 1.f, zoom ? fov : 0.f);
}
bool holdingThread(Player const& player) {
    return items::idOf(player.getSelectedItem()) == tg::ids::kRedThread || items::idOf(player.getOffhandSlot()) == tg::ids::kRedThread;
}

void tickThreads(Player& player) {
    bool vision      = st().threadVision.load() && tg::isActive(player, tg::ids::kRedThreadVision);
    st().showThreads = holdingThread(player) || vision;
}

// ---- worn models ------------------------------------------------------------------------------------------

/// renderGlovesInFirstPerson reads the client's firstPersonGloveOverlay: the server keeps it in the
/// player's worn-model state (feature/tgear/WornModel.cpp), so it is sent on a change and every
/// 10 seconds (a new world, a server that restarted).
void tickGloveOverlay() {
    static int  last  = -1;
    static long ticks = 0;
    bool        on    = gf::config::get<tg::TgearConfig>().firstPersonGloveOverlay;
    if (static_cast<int>(on) == last && ++ticks % 200 != 0) return;
    last = static_cast<int>(on);
    feature::net::client::send("tgear.glove_overlay", {{"on", on}});
}

// ---- stealth (TravellersClientEvents#handleStealth) -------------------------------------------------------

/// Every frame upstream makes each crouching stealth wearer invisible on the client at once (the server's
/// hidden Invisibility follows). Per client tick here: the players of the client's level that crouch with
/// active stealth are made invisible; once they stop, their invisibility goes back to their effects.
void tickStealth(Player& local) {
    static std::unordered_map<std::int64_t, bool> hidden; // client thread
    std::unordered_map<std::int64_t, bool>        now;
    local.getLevel().forEachPlayer([&](Player& p) {
        auto uid = p.getOrCreateUniqueID().rawID;
        if (p.isSneaking() && tg::isActive(p, tg::ids::kStealth)) {
            if (!p.isInvisible()) p.setInvisible(true);
            now[uid] = true;
        } else if (hidden.contains(uid)) {
            p.updateInvisibilityStatus();
        }
        return true;
    });
    hidden.swap(now);
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
        // toggleBooleanDataAttachment: only while the modifier is active; the attachment is the
        // server's (per player, kept on death)
        auto* p = currentPlayer();
        if (!p || !tg::isActive(*p, tg::ids::kRedThreadVision)) return;
        feature::net::client::send("tgear.thread_vision_toggle", nlohmann::json::object());
        log().info("tgear: red thread vision key");
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
    return id == 0 || gf::world::isGloaming(dim);
}

/// Level#getDayTime with the dimension type's fixed_time (the Gloaming Forest: 13000).
std::int64_t dayTime(Player const& player) {
    if (gf::world::isGloaming(player.getDimension()))
        if (auto fixed = gf::world::fixedTime()) return *fixed;
    return player.getLevel().getTime();
}

/// Level#getMoonPhase: dimensionType.moonPhase(getDayTime()) = (dayTime / 24000 % 8 + 8) % 8 over the
/// level's own day time: the dimension type's fixed_time only changes timeOfDay (ClockDisplay applies
/// it, MoonDialDisplay does not), so the Gloaming Forest follows the day count like the overworld.
int moonPhase(Player const& player) { return core::tgear::moonPhaseOf(player.getLevel().getTime()); }

/// MoonDialItem#getMoonPhase outside a natural dimension: "unknown_fools" on April 1st (local date).
bool aprilFools() {
    std::time_t now = std::time(nullptr);
    std::tm     local{};
    localtime_s(&local, &now);
    return local.tm_mon == 3 && local.tm_mday == 1;
}

bool use24Hours() {
    auto const& cfg = gf::config::get<tg::TgearConfig>().itemDisplay;
    if (cfg.followLocale) {
        auto loc = clientLocale();
        return !(loc == "en_US" || loc == "en_CA" || loc == "en_AU");
    }
    return cfg.twentyFourHourFormat;
}

/// MapDisplay: the map background (100 x 100, Bedrock's textures/map/map_background: Java's
/// map_background_checkerboard is a Java resource Bedrock does not have), then MapRenderer at 0.7075 /
/// 4.75: the map colours and its decorations (MapRenderer draws them over the map).
void drawMap(screen::Canvas& c, float x0, float y0, float scale, std::int64_t mapId) {
    if (!ui::blit(c, "textures/map/map_background", {x0, y0, 100 * scale, 100 * scale}, 0, 0, 64, 64, 64, 64)) {
        c.fill({x0, y0, 100 * scale, 100 * scale}, 0xFF7F6A4C);
        c.fill({x0 + 2 * scale, y0 + 2 * scale, 96 * scale, 96 * scale}, 0xFFD8C8A0);
    }
    auto view = maps::view(mapId);
    if (!view || view->pixels.size() < static_cast<std::size_t>(view->size * view->size)) return;
    float px = 0.7075f * 128.f / static_cast<float>(view->size) * scale;
    float mx = x0 + 4.75f * scale, my = y0 + 4.75f * scale;
    for (int z = 0; z < view->size; ++z) {
        int run = 0;
        for (int x = 0; x <= view->size; ++x) {
            bool end = x == view->size || view->pixels[z * view->size + x] != view->pixels[z * view->size + run];
            if (!end) continue;
            unsigned argb = view->pixels[z * view->size + run];
            if ((argb >> 24) != 0) c.fill({mx + run * px, my + z * px, (x - run) * px, px}, argb | 0xFF000000);
            run = x;
        }
    }
    // MapRenderer's decorations: an 8 x 8 map pixel icon centred at (x / 2 + 64, z / 2 + 64)
    float unit = 0.7075f * scale;
    std::vector<maps::Decoration> decorations = view->decorations;
    for (auto const& d : maps::decorationsOf(mapId)) decorations.push_back(d);
    for (auto const& d : decorations) {
        auto icon = maps::iconFor(d.type);
        if (!icon) continue;
        float cx = mx + (static_cast<float>(d.x) / 2.f + 64.f) * unit, cz = my + (static_cast<float>(d.z) / 2.f + 64.f) * unit;
        auto  size = ui::textureSize(icon->texture);
        float tw   = size ? static_cast<float>(size->first) : 16.f, th = size ? static_cast<float>(size->second) : 16.f;
        // MapRenderer: mulPose(ZP.rotationDegrees(rot * 360 / 16)) about the icon's centre
        auto& world = c.context().mScreenContext.camera.worldMatrixStack.get();
        auto  ref   = world.push(false);
        auto& m     = ref.mat->_m.get();
        glm::vec3 centre{c.originX() + cx, c.originY() + cz, 0.f};
        m = glm::translate(m, centre);
        m = glm::rotate(m, glm::radians(static_cast<float>(d.rotation) * 360.f / 16.f), glm::vec3{0.f, 0.f, 1.f});
        m = glm::translate(m, -centre);
        ui::blit(c, icon->texture, {cx - 4 * unit, cz - 4 * unit, 8 * unit, 8 * unit}, icon->u0 * tw, icon->v0 * th, (icon->u1 - icon->u0) * tw, (icon->v1 - icon->v0) * th, tw, th);
    }
}

/// CompassDisplay: the lodestone's position and Manhattan distance when the compass tracks one in this
/// dimension (the server stores it with the stored compass), else the player's block position.
std::string compassText(Player const& player, nlohmann::json const& item) {
    auto feet = game::blockPosOf(player.getFeetPos()); // player.blockPosition()
    if (auto const* l = item.contains("lodestone") ? &item["lodestone"] : nullptr; l && l->is_object() && l->value("dim", -1) == static_cast<int>(player.getDimensionId())) {
        int x = l->value("x", 0), y = l->value("y", 0), z = l->value("z", 0);
        int dist = std::abs(feet.x - x) + std::abs(feet.y - y) + std::abs(feet.z - z);
        auto pos = std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z);
        return ctr("travellers_gear.modifier.gloamingforest.item_display.compass.lodestone", "%s (%s blocks away)", pos, std::to_string(dist));
    }
    return std::to_string(feet.x) + ", " + std::to_string(feet.y) + ", " + std::to_string(feet.z);
}

void drawWidgets(screen::Canvas& canvas);

/// ItemDisplayOverlay at (screenOffsetX, screenOffsetY) in GUI pixels, whatever the HUD drew before it:
/// the HUD layers run after the whole hud_screen, and a control drawn last (the chat panel with its
/// lines) can leave its translation on the UI's world matrix (round 2: the
/// overlay drew over the chat at about (30, 132) GUI pixels). The overlay draws with the UI's own
/// world matrix without a translation, and its text is flushed inside it.
void drawHud(screen::Canvas& canvas, float) {
    if (hud::guiHidden()) return;
    auto&          ui    = canvas.context();
    ScreenContext& sc    = ui.mScreenContext;
    auto&          world = sc.camera.worldMatrixStack.get();
    auto           ref   = world.push(false);
    auto&          m     = ref.mat->_m.get();
    static std::atomic<bool> logged{false};
    if ((m[3][0] != 0.f || m[3][1] != 0.f) && !logged.exchange(true))
        log().info("tgear: the HUD's world matrix was offset by ({}, {}) when the item display drew", m[3][0], m[3][1]);
    m[3][0] = m[3][1] = m[3][2] = 0.f; // no translation left over from another control
    {
        screen::Canvas own(ui); // flushes its text here, inside that matrix
        drawWidgets(own);
    }
}

void drawWidgets(screen::Canvas& canvas) {
    auto* player = currentPlayer();
    if (!player) return;
    auto const& goggles = player->getArmor(Slot::Head);
    if (!tg::isActiveOn(*player, goggles, tg::ids::kItemDisplay)) return;
    auto d = tg::displayOf(goggles);
    if (!d) return;
    std::vector<Widget> widgets;
    float               widest = 0;
    auto&               dim    = player->getDimension();
    for (int i = 0; i < core::tgear::kDisplaySlots; ++i) {
        auto const& item = d->items[i];
        if (item.is_null()) continue;
        auto type = core::tgear::kDisplayLayout[i];
        if (type == core::tgear::DisplayType::Map && i != d->chosenMapSlot) continue;
        if (tg::displayTypeOf(item.value("id", std::string())) != type) continue;
        Widget w{type, item};
        switch (type) {
        case core::tgear::DisplayType::Map: w.width = 100, w.height = 102; break;
        case core::tgear::DisplayType::Compass:
            w.text  = compassText(*player, item);
            w.width = canvas.textWidth(w.text);
            break;
        case core::tgear::DisplayType::Clock:
            w.text  = natural(dim) ? core::tgear::clockText(dayTime(*player), use24Hours())
                                   : ctr("travellers_gear.modifier.gloamingforest.item_display.clock.unknown", "Time Unknown");
            w.width = canvas.textWidth(w.text) + (natural(dim) ? 10 : 0);
            break;
        case core::tgear::DisplayType::MoonDial:
            w.text  = natural(dim) ? ctr("item.gloamingforest.moon_dial.phase_" + std::to_string(moonPhase(*player)), "")
                                   : aprilFools() ? ctr("item.gloamingforest.moon_dial.phase_unknown_fools", "Moon phase indeterminate")
                                                  : ctr("item.gloamingforest.moon_dial.phase_unknown", "Moon phase indeterminate");
            w.width = canvas.textWidth(w.text) + 10;
            break;
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
    auto const& cfg   = gf::config::get<tg::TgearConfig>().itemDisplay;
    float       scale = static_cast<float>(cfg.screenScale);
    float       x0    = static_cast<float>(cfg.screenOffsetX);
    float       y     = static_cast<float>(cfg.screenOffsetY);
    for (auto const& w : widgets) {
        float half = std::floor(widest / 2);
        switch (w.type) {
        case core::tgear::DisplayType::Map:
            if (auto id = tg::mapIdOf(w.item)) drawMap(canvas, x0 + std::max(half - 50, 0.f) * scale, y, scale, *id);
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
                    tex   = "textures/gloamingforest/gui/time";
                    texW  = 16;
                } else {
                    uv   = core::tgear::moonIconUv(moonPhase(*player));
                    tex  = "textures/gloamingforest/gui/moon";
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

// ---- red thread (RedThreadRenderer + RedThreadModel) ------------------------------------------------------

constexpr char const* kFaceStates[6]     = {"gloamingforest:down", "gloamingforest:up", "gloamingforest:north", "gloamingforest:south", "gloamingforest:west", "gloamingforest:east"};
constexpr int         kFaceOffsets[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
enum Face { Down, Up, North, South, West, East };
enum Part { Center, PUp, PDown, PLeft, PRight };

/// RedThreadModel.create(): each part a 0-height box at y = 0.2 px (pixels; x / z ranges), with the UV
/// rectangle of Java's UP cube face in the model's 16 x 16 texture space (u + d + w, v + d)..(u + d + 2w, v).
struct PartBox {
    float x0, x1, z0, z1;
    float u0, v0, u1, v1;
};
constexpr PartBox kParts[5] = {
    {-1, 1, -1, 1, 4, 0, 6, 2},   // center texOffs(0, 0) 2 x 0 x 2
    {-1, 1, -8, -1, 9, 2, 11, 9}, // up texOffs(0, 2) 2 x 0 x 7
    {-1, 1, 1, 8, 13, 2, 15, 9},  // down texOffs(4, 2)
    {-8, -1, -1, 1, 9, 10, 16, 12}, // left texOffs(0, 10) 7 x 0 x 2
    {1, 8, -1, 1, 9, 14, 16, 16},   // right texOffs(0, 14)
};

/// RedThreadModel#getPart(face, direction).
Part partFor(int face, int dir) {
    switch (face) {
    case Down: return dir == North ? PUp : dir == East ? PRight : dir == South ? PDown : PLeft;
    case Up: return dir == North ? PUp : dir == West ? PRight : dir == South ? PDown : PLeft;
    case North: return dir == Up ? PUp : dir == East ? PRight : dir == Down ? PDown : PLeft;
    case South: return dir == Up ? PDown : dir == West ? PLeft : dir == Down ? PUp : PRight;
    case West: return dir == Up ? PLeft : dir == North ? PUp : dir == Down ? PRight : PDown;
    default: return dir == Up ? PRight : dir == South ? PDown : dir == Down ? PLeft : PUp; // East
    }
}

/// RedThreadRenderer: translate(getXYZ(face)), rotate Z (getZPDegrees), rotate X (getXPDegrees),
/// translate(0.5, 0, 0.5), applied to a model point (block units).
Vec3 placeOnFace(int face, Vec3 const& m) {
    float qx = m.x + 0.5f, qy = m.y, qz = m.z + 0.5f;
    float zdeg = face == East ? 90.f : face == Up ? 180.f : face == West ? 270.f : 0.f;
    float xdeg = face == North ? 90.f : face == South ? 270.f : 0.f;
    float ax = xdeg * 3.14159265f / 180.f, az = zdeg * 3.14159265f / 180.f;
    float y1 = qy * std::cos(ax) - qz * std::sin(ax), z1 = qy * std::sin(ax) + qz * std::cos(ax);
    float x2 = qx * std::cos(az) - y1 * std::sin(az), y2 = qx * std::sin(az) + y1 * std::cos(az);
    Vec3  xyz{face == East || face == Up ? 1.f : 0.f, face == West || face == Up || face == North ? 1.f : 0.f, face == South ? 1.f : 0.f};
    return Vec3{xyz.x + x2, xyz.y + y2, xyz.z + z1};
}

/// upstream's red_thread.png (32 x 32, ARGB per texel), read from the resource pack's upstream-derived texel table
/// (textures/gloamingforest/entity/tgear/upstream-derived/red_thread_texels.json): the connections are drawn
/// texel by texel as coloured quads (the colour materials draw vertex colours; the textured immediate
/// path sampled grey on 1.26.51, docs/units/tgear.md).
struct ThreadTexels {
    int                        width = 0, height = 0;
    std::vector<std::uint32_t> argb;
    [[nodiscard]] std::uint32_t at(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height ? argb[y * width + x] : 0; }
};

ThreadTexels const* threadTexels() {
    static ThreadTexels texels;
    static long         attempts = 0; // render thread only
    if (!texels.argb.empty()) return &texels;
    // looked up on the first draw, then again every 600 draws while missing (a pack not readable yet)
    if (attempts++ % 600 != 0) return nullptr;
    [] {
        std::error_code ec;
        auto            packs = gf::Mod::getInstance().getSelf().getModDir() / "resource_packs";
        for (auto it = std::filesystem::recursive_directory_iterator(packs, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (it->path().filename() != "red_thread_texels.json") continue;
            std::ifstream in(it->path());
            auto          j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_discarded()) continue;
            texels.width  = j.value("width", 0);
            texels.height = j.value("height", 0);
            auto hex      = j.value("argb", std::string());
            if (texels.width <= 0 || texels.height <= 0 || hex.size() != static_cast<std::size_t>(texels.width * texels.height * 8)) {
                texels.width = texels.height = 0;
                continue;
            }
            texels.argb.resize(static_cast<std::size_t>(texels.width * texels.height));
            for (std::size_t i = 0; i < texels.argb.size(); ++i) texels.argb[i] = static_cast<std::uint32_t>(std::stoul(hex.substr(i * 8, 8), nullptr, 16));
            break;
        }
        if (texels.argb.empty() && attempts == 1) log().warn("tgear: red_thread_texels.json not found in the resource packs: no red thread connections");
    }();
    return texels.argb.empty() ? nullptr : &texels;
}

/// A material of the switchable group, kept for the process (as F12's Lines: "wireframe" draws vertex
/// colours depth tested, "ui_fill_gradient" without a depth test).
mce::MaterialPtr const* colourMaterial(bool throughWalls) {
    static mce::MaterialPtr* tested = nullptr;
    static mce::MaterialPtr* always = nullptr;
    auto*& slot = throughWalls ? always : tested;
    if (!slot) slot = new mce::MaterialPtr(mce::RenderMaterialGroup::switchable(), HashedString(throughWalls ? "ui_fill_gradient" : "wireframe"));
    return slot->mRenderMaterialInfoPtr.get() ? slot : nullptr;
}

bool threadFace(BlockSource& region, BlockPos const& p, int face) {
    auto const& b = region.getBlock(p);
    return game::blockId(b) == tg::ids::kRedThread && game::intState(b, kFaceStates[face]).value_or(0) != 0;
}

BlockPos rel(BlockPos const& p, int dir) { return BlockPos{p.x + kFaceOffsets[dir][0], p.y + kFaceOffsets[dir][1], p.z + kFaceOffsets[dir][2]}; }

std::atomic<std::uint64_t> gThreadQuads{0};

/// RedThreadRenderer#render: per face of the block, the centre piece and the connections towards the
/// directions off the face's axis: to another face of the same block, to the same face of the next
/// thread, or round the corner to the thread on the face's side (unless a sturdy block is in the way).
/// The block's own geometry draws the centre pieces and the same-block connections (bone visibility by
/// the face states, vanilla clients too); the next-thread and corner connections are drawn here at the
/// block's light, depth tested. While the threads glow (red thread held, or red thread vision on: the
/// GLOW render type, full bright, depth "always") everything is drawn here, through walls.
void drawThread(render::RenderContext& ctx, BlockPos const& pos, Block const& block, float) {
    auto* player = currentPlayer();
    if (!player) return;
    auto const* texels = threadTexels();
    if (!texels) return;
    bool  glow     = st().showThreads.load();
    auto& region   = player->getDimensionBlockSource();
    auto* material = colourMaterial(glow);
    if (!material) return;
    // Java's packedLight: the block's own light (the dimension's brightness ramp at the block, sky
    // darkening included), or full bright for the glow
    float light = glow ? 1.f : std::clamp(region.getBrightness(pos), 0.05f, 1.f);
    float sx = static_cast<float>(texels->width) / 16.f, sy = static_cast<float>(texels->height) / 16.f; // texels per model uv unit
    auto&         tess  = ctx.screen.tessellator;
    bool          begun = false;
    std::uint64_t quads = 0;
    auto part = [&](int face, PartBox const& b) {
        float y = 0.2f / 16.f;
        float du = b.u1 - b.u0, dv = b.v1 - b.v0;
        int   tx0 = static_cast<int>(std::floor(std::min(b.u0, b.u1) * sx)), tx1 = static_cast<int>(std::ceil(std::max(b.u0, b.u1) * sx));
        int   ty0 = static_cast<int>(std::floor(std::min(b.v0, b.v1) * sy)), ty1 = static_cast<int>(std::ceil(std::max(b.v0, b.v1) * sy));
        for (int ty = ty0; ty < ty1; ++ty) {
            for (int tx = tx0; tx < tx1; ++tx) {
                std::uint32_t c = texels->at(tx, ty);
                if ((c >> 24) == 0) continue; // the cutout
                if (!begun) {
                    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
                    begun = true;
                }
                // the texel's rectangle on the part (u runs along x, v along z, as the model's UP face)
                float ua = static_cast<float>(tx) / sx, ub = static_cast<float>(tx + 1) / sx;
                float va = static_cast<float>(ty) / sy, vb = static_cast<float>(ty + 1) / sy;
                float xa = b.x0 + (ua - b.u0) / du * (b.x1 - b.x0), xb = b.x0 + (ub - b.u0) / du * (b.x1 - b.x0);
                float za = b.z0 + (va - b.v0) / dv * (b.z1 - b.z0), zb = b.z0 + (vb - b.v0) / dv * (b.z1 - b.z0);
                Vec3  q[4] = {placeOnFace(face, {xa / 16.f, y, za / 16.f}), placeOnFace(face, {xb / 16.f, y, za / 16.f}),
                              placeOnFace(face, {xb / 16.f, y, zb / 16.f}), placeOnFace(face, {xa / 16.f, y, zb / 16.f})};
                for (auto& v : q) v = Vec3{pos.x + v.x - ctx.camera.x, pos.y + v.y - ctx.camera.y, pos.z + v.z - ctx.camera.z};
                float r = static_cast<float>((c >> 16) & 0xFF) / 255.f * light, g = static_cast<float>((c >> 8) & 0xFF) / 255.f * light,
                      bl = static_cast<float>(c & 0xFF) / 255.f * light;
                tess.color(r, g, bl, 1.f);
                // both windings: seen from either side, as Java's no-cull cutout
                for (int i : {0, 1, 2, 3}) tess.vertex(q[i].x, q[i].y, q[i].z);
                for (int i : {3, 2, 1, 0}) tess.vertex(q[i].x, q[i].y, q[i].z);
                ++quads;
            }
        }
    };
    for (int f = 0; f < 6; ++f) {
        if (game::intState(block, kFaceStates[f]).value_or(0) == 0) continue;
        if (glow) part(f, kParts[Center]);
        for (int d = 0; d < 6; ++d) {
            if (d / 2 == f / 2) continue;
            bool same = game::intState(block, kFaceStates[d]).value_or(0) != 0;
            bool flag = same;
            if (!flag) flag = threadFace(region, rel(pos, d), f);
            if (!flag) {
                auto side    = rel(pos, d);
                bool blocked = region.getBlock(side).canProvideFullSupport(static_cast<uchar>(d ^ 1));
                flag         = !blocked && threadFace(region, rel(side, f), d ^ 1);
            }
            // the same-block connections are the block geometry's (bone visibility) unless glowing
            if (flag && (glow || !same)) part(f, kParts[partFor(f, d)]);
        }
    }
    if (!begun) return;
    MeshHelpers::renderMeshImmediately(ctx.screen, tess, *material, OffscreenCaptureDescription{});
    if (gThreadQuads.fetch_add(quads) == 0) log().info("tgear: red thread drawn ({} texel quads, {})", quads, glow ? "glow" : "lit");
}
// ---- item icons (TravellersGearItemModel, the red thread's "size" property) ------------------------------

/// The modifier_directory of a gear item ("" for the belt and the gloves: no overlays).
std::string modifierDirectory(std::string const& id) {
    if (id == tg::ids::kGoggles) return "goggles";
    if (id == tg::ids::kVest) return "vest";
    if (id == tg::ids::kWings) return "wings";
    if (id == tg::ids::kBoots) return "boots";
    return "";
}

/// TravellersGearItemModel in GUI slots: the "broken" base texture while broken, then one layer per
/// insertable modifier (from broken_modifier_directory while broken; a modifier without a texture is
/// skipped, as the missing sprite is) and the gloves layer of a vest with gloves (item atlas entries of
/// item_texture.d/tgear.json, drawn through ext/tgear's icon layers).
std::optional<ext::tgear::client::IconLayers> gearIcon(ItemStack const& stack) {
    auto id  = items::idOf(stack);
    auto dir = modifierDirectory(id);
    if (dir.empty()) return std::nullopt;
    bool                          broken = tg::isBroken(stack);
    ext::tgear::client::IconLayers layers;
    if (broken) layers.base = "gf_tgear_" + dir + "_broken";
    std::string prefix = "gf_tgear_mod_" + dir + (broken ? "_broken_" : "_");
    for (auto const& m : tg::insertableModifiers(stack)) layers.overlays.push_back(prefix + m.substr(m.find(':') + 1));
    if (id == tg::ids::kVest && tg::hasGloves(stack)) layers.overlays.push_back(prefix + "gloves");
    if (layers.base.empty() && layers.overlays.empty()) return std::nullopt;
    return layers;
}

/// ClientRegistrationEvents: the red thread item's gloamingforest:size property (count >= 32: 1, >= 16:
/// 0.5, >= 4: 0.25) picks red_thread_bundle_2 / _1 / _0.
std::optional<ext::tgear::client::IconLayers> threadIcon(ItemStack const& stack) {
    int n = items::countOf(stack);
    if (n >= 32) return ext::tgear::client::IconLayers{.base = "gf_tgear_red_thread_bundle_2"};
    if (n >= 16) return ext::tgear::client::IconLayers{.base = "gf_tgear_red_thread_bundle_1"};
    if (n >= 4) return ext::tgear::client::IconLayers{.base = "gf_tgear_red_thread_bundle_0"};
    return std::nullopt;
}

// ---- tooltip (TravellersArmorItem#appendHoverText) ----------------------------------------------------------

/// upstream's lines under the name (the server writes no lore for players with the client mod): the
/// abilities, each inserted modifier with its description lines while Shift is held, the empty slots,
/// the gloves' "Cosmetic", "Hold Shift for info" while Shift is up and something has a description,
/// and the stored items (the belt's container, the goggles' display; upstream's tooltip images).
void describe(ItemStack const& stack, std::vector<std::string>& lines) {
    auto locale = clientLocale();
    bool shift  = shiftDown();
    auto abilities = tg::abilityModifiers(stack);
    for (auto const& id : abilities)
        lines.push_back("§6" + core::formatMessageArgs(ctr("travellers_gear.ability", "Ability: %s"), "§7" + tg::modifierName(id, locale) + "§6"));
    auto inserted = tg::insertableModifiers(stack);
    for (auto const& id : inserted) {
        lines.push_back("§f- §7" + tg::modifierName(id, locale));
        if (!shift) continue;
        // every description component with its arguments (side_step: the left / right keybinds)
        auto description = tg::modifierDescription(id, [](std::string const& key, std::string const& fallback) { return ctr(key, fallback); });
        for (auto const& line : description) lines.push_back("§f§l" + ctr("travellers_gear.info_indent", "  ⤷ ") + "§r§f" + line);
    }
    int slots = tg::modifierSlots(items::idOf(stack));
    for (int i = static_cast<int>(inserted.size()); i < slots; ++i) lines.push_back("§f- §8" + ctr("travellers_gear.modifier.empty", "Empty"));
    if (items::idOf(stack) == tg::ids::kGloves) lines.push_back("§7" + ctr("item.gloamingforest.travellers_gloves.desc", "Cosmetic"));
    if (!shift) {
        bool described = false;
        for (auto const& list : {abilities, inserted})
            for (auto const& id : list)
                if (auto m = tg::modifier(id); m && m->hasDescription) described = true;
        if (described) lines.push_back("§f" + core::formatMessageArgs(ctr("travellers_gear.shift_info", "Hold %s for info"), "§eShift§f"));
    }
    if (tg::hasContainer(stack))
        for (auto const& s : tg::container(stack))
            if (!items::isEmpty(s)) lines.push_back("§7  " + items::displayName(s) + (items::countOf(s) > 1 ? " x" + std::to_string(items::countOf(s)) : ""));
    if (auto d = tg::displayOf(stack))
        for (int i = 0; i < core::tgear::kDisplaySlots; ++i)
            if (!d->items[i].is_null()) lines.push_back("§7  " + items::displayName(items::fromJson(d->items[i])) + (i == d->chosenMapSlot ? " §e*" : ""));
}

// ---- item display clicks (TravellersGogglesItem#overrideStackedOnOther / overrideOtherStackedOnMe) --------

/// The collections the server resolves (Display.cpp: the player's inventory, armour and off-hand, and
/// the open container's own slots).
bool handledCollection(std::string const& collection) {
    static constexpr char const* kNames[] = {"hotbar_items", "inventory_items", "combined_hotbar_and_inventory_items", "armor_items", "offhand_items", "container_items"};
    for (auto const* n : kNames)
        if (collection == n) return true;
    return false;
}

/// TravellersGogglesItem#overrideStackedOnOther / overrideOtherStackedOnMe: a secondary click with
/// goggles that carry a display (one item) on the cursor or in the slot is always the goggles' (true):
/// insert the other stack, take the first stored item out, or nothing at all (an empty cursor on goggles
/// with an empty display). The server does it ("tgear.display_click").
bool onSlotClick(std::string const& collection, int index, ItemStack const& slot, ItemStack const& cursor) {
    auto isGoggles = [](ItemStack const& s) { return !items::isEmpty(s) && items::idOf(s) == tg::ids::kGoggles && tg::displayOf(s) && items::countOf(s) == 1; };
    if (!handledCollection(collection) || !(isGoggles(slot) || isGoggles(cursor))) return false;
    feature::net::client::send("tgear.display_click", {{"collection", collection}, {"index", index}});
    log().info("tgear: item display click on {} {}", collection, index);
    return true;
}
class TgearClientModule {
public:
    void load() { registerKeys(); }
    void install() {
        ext::tgear::client::installHooks();
        ext::tgear::client::installIconHooks();
        static bool once = false;
        if (once) return;
        once = true;
        movement::onLocalTick(0, [](movement::MoveState&) {
            if (auto* p = currentPlayer()) {
                tickZoom(*p);
                tickThreads(*p);
                tickStealth(*p);
                tickGloveOverlay();
            }
        });
        hud::addLayer("tgear.item_display", 100, drawHud);
        ext::tgear::client::setSlotClickHandler(&onSlotClick);
        render::registerBlockRenderer(tg::ids::kRedThread, drawThread, render::Flags{.throughWalls = true});
        for (auto const* id : tg::kGearItems) {
            tooltip::add(id, describe);
            ext::tgear::client::setIconLayers(id, gearIcon);
        }
        ext::tgear::client::setIconLayers(tg::ids::kRedThread, threadIcon);
        // GogglesZoomPacket for the trackers (the goggles' "down" texture)
        feature::net::client::on("tgear.goggles_zoom", [](nlohmann::json const& msg) {
            std::lock_guard lock(st().mutex);
            st().trackedZoom[msg.value("player", std::int64_t{0})] = msg.value("zoom", false);
        });
        // the server's red thread vision attachment of this player
        feature::net::client::on("tgear.thread_vision", [](nlohmann::json const& msg) {
            st().threadVision = msg.value("on", true);
            log().info("tgear: red thread vision {}", st().threadVision.load() ? "on" : "off");
        });
    }
    void uninstall() {
        ext::tgear::client::uninstallIconHooks();
        ext::tgear::client::uninstallHooks();
    }
};

} // namespace

} // namespace gf::client::tgear

GF_REGISTER_MODULE(gf::client::tgear::TgearClientModule);

#endif // GF_CLIENT

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/TravellersClientEvents.java
//   src/main/java/twilightforest/client/overlay/ItemDisplayOverlay.java
//   src/main/java/twilightforest/client/overlay/display/{ClockDisplay,CompassDisplay,MapDisplay,MoonDialDisplay,SimpleTextDisplay}.java
//   src/main/java/twilightforest/client/renderer/block/RedThreadRenderer.java
//   src/main/java/twilightforest/client/model/entity/RedThreadModel.java
//   src/main/java/twilightforest/client/model/item/TravellersGearItemModel.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorItem.java
//   src/main/java/twilightforest/init/TFKeyBinds.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U11 in the client mod: TF's four key bindings (zoom Z, swap hotbar V, map cycle C, red thread
// vision unbound), the goggles' zoom (FOV x 0.3 and the slower turn while the key is held, synced with
// "tgear.goggles_zoom"), the item display HUD (ItemDisplayOverlay), the red thread (RedThreadRenderer:
// the connections and corner connections, full bright through walls while the vision is on or red
// thread is held), the worn models' state (gloves on the vest, the belt on swap-hotbar wings, the
// goggles' "down" texture while zooming, the first-person glove overlay) as molang variables the
// attachables read, the gear's item icons (broken texture, modifier overlays: TravellersGearItemModel)
// and TF's tooltip (Shift descriptions under each modifier).
#ifdef GF_CLIENT

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

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
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "feature/tgear/Config.h"
#include "feature/tgear/Gear.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
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
constexpr char kCategory[]  = "key.twilightforest.categories.travellers_gear";

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

// ---- stealth (TravellersClientEvents#handleStealth) -------------------------------------------------------

/// Every frame TF makes each crouching stealth wearer invisible on the client at once (the server's
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
    return id == 0 || gf::world::isTwilight(dim);
}

/// Level#getDayTime with the dimension type's fixed_time (the Twilight Forest: 13000).
std::int64_t dayTime(Player const& player) {
    if (gf::world::isTwilight(player.getDimension()))
        if (auto fixed = gf::world::fixedTime()) return *fixed;
    return player.getLevel().getTime();
}

/// Level#getMoonPhase: dimensionType.moonPhase(dayTime) = (dayTime / 24000 % 8 + 8) % 8, with the
/// fixed time of the Twilight Forest (always 0, Full Moon).
int moonPhase(Player const& player) { return static_cast<int>((dayTime(player) / 24000 % 8 + 8) % 8); }

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
        return ctr("travellers_gear.modifier.twilightforest.item_display.compass.lodestone", "%s (%s blocks away)", pos, std::to_string(dist));
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
                                   : ctr("travellers_gear.modifier.twilightforest.item_display.clock.unknown", "Time Unknown");
            w.width = canvas.textWidth(w.text) + (natural(dim) ? 10 : 0);
            break;
        case core::tgear::DisplayType::MoonDial:
            w.text  = natural(dim) ? ctr("item.twilightforest.moon_dial.phase_" + std::to_string(moonPhase(*player)), "")
                                   : ctr("item.twilightforest.moon_dial.phase_unknown", "Moon phase indeterminate");
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
                    tex   = "textures/twilightforest/gui/time";
                    texW  = 16;
                } else {
                    uv   = core::tgear::moonIconUv(moonPhase(*player));
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

// ---- red thread (RedThreadRenderer + RedThreadModel) ------------------------------------------------------

constexpr char const* kFaceStates[6]     = {"twilightforest:down", "twilightforest:up", "twilightforest:north", "twilightforest:south", "twilightforest:west", "twilightforest:east"};
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

using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

/// The moving-block renderer: its alpha-test material (entityCutout) and the terrain atlas, which holds
/// textures/twilightforest/entity/red_thread (terrain_texture.d/tgear.json: "tf_tgear_red_thread"), so
/// the texture is always loaded (round 2: a texture of its own drew black).
MovingBlockActorRenderer* movingRenderer(render::RenderContext& ctx) {
    if (!ctx.actors) return nullptr;
    BlockActorRenderDispatcher& dispatcher = ctx.actors->mBlockEntityRenderDispatcher;
    return static_cast<MovingBlockActorRenderer*>(dispatcher.mRenderers->operator[](BlockActorRendererId::MovingBlock).get());
}

/// The red thread's rectangle in the terrain atlas (u0, v0, u1, v1).
std::optional<std::array<float, 4>> threadAtlasUv() {
    static std::optional<std::array<float, 4>> uv;
    if (uv) return uv;
    auto const& t = BlockGraphics::getTextureUVCoordinateSet("tf_tgear_red_thread", 0, 0);
    if (t._u0 == t._u1 || t._v0 == t._v1) return std::nullopt;
    uv = std::array<float, 4>{t._u0, t._v0, t._u1, t._v1};
    return uv;
}

/// The GLOW render type: depth test "always", full bright, the red_thread texture with cutout. Bedrock has
/// no world cutout material without a depth test: the UI textured material (no depth test, unlit: the
/// texture times the vertex colour times the screen's shader colour, set to white for the draw).
mce::MaterialPtr const* glowMaterial() {
    static mce::MaterialPtr* m = nullptr;
    if (m) return m->mRenderMaterialInfoPtr.get() ? m : nullptr;
    m = new mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString("ui_textured"));
    if (!m->mRenderMaterialInfoPtr.get()) m = new mce::MaterialPtr(mce::RenderMaterialGroup::switchable(), HashedString("ui_textured"));
    if (!m->mRenderMaterialInfoPtr.get()) log().warn("tgear: no ui_textured material: red thread glow drawn depth tested");
    return m->mRenderMaterialInfoPtr.get() ? m : nullptr;
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
/// Normally the block's own geometry draws the centre pieces (vanilla clients too) and the connections
/// are drawn here with the cutout material at the block's light; while the threads glow everything is
/// drawn here, full bright and through walls.
void drawThread(render::RenderContext& ctx, BlockPos const& pos, Block const& block, float) {
    auto* player = currentPlayer();
    if (!player) return;
    bool  glow   = st().showThreads.load();
    auto& region = player->getDimensionBlockSource();
    auto* moving = movingRenderer(ctx);
    auto  atlas  = threadAtlasUv();
    if (!moving || !atlas) return;
    mce::MaterialPtr const* material = glow ? glowMaterial() : nullptr;
    bool                    lit      = !material;
    if (!material) material = &moving->mBlockMaterials[static_cast<int>(BlockRenderLayer::RenderlayerAlphatest)].get();
    auto&         tess  = ctx.screen.tessellator;
    bool          begun = false;
    std::uint64_t quads = 0;
    auto [au0, av0, au1, av1] = *atlas;
    // Java's packedLight: the block's own light, or full bright (15728880) for the glow
    Vec2 lightUv{1.f, 1.f};
    if (!glow) {
        auto light = region.getBrightnessPair(pos);
        lightUv    = Vec2{static_cast<float>(static_cast<unsigned char>(light.block->mValue)) / 15.f, static_cast<float>(static_cast<unsigned char>(light.sky->mValue)) / 15.f};
    }
    auto quad = [&](int face, PartBox const& b) {
        if (!begun) {
            tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
            begun = true;
        }
        tess.color(1.f, 1.f, 1.f, 1.f);
        float y = 0.2f / 16.f;
        Vec3  c[4] = {placeOnFace(face, {b.x0 / 16.f, y, b.z0 / 16.f}), placeOnFace(face, {b.x1 / 16.f, y, b.z0 / 16.f}),
                      placeOnFace(face, {b.x1 / 16.f, y, b.z1 / 16.f}), placeOnFace(face, {b.x0 / 16.f, y, b.z1 / 16.f})};
        auto  at = [&](float u, float v) { return Vec2{au0 + (au1 - au0) * u / 16.f, av0 + (av1 - av0) * v / 16.f}; };
        Vec2  t[4] = {at(b.u0, b.v0), at(b.u1, b.v0), at(b.u1, b.v1), at(b.u0, b.v1)};
        for (auto& v : c) v = Vec3{pos.x + v.x - ctx.camera.x, pos.y + v.y - ctx.camera.y, pos.z + v.z - ctx.camera.z};
        // every vertex carries its colour, its atlas UV and the lightmap UV (UV1: block / sky light; the
        // block materials sample the lightmap with it, a vertex without one reads the darkest texel:
        // round 2 / 3 drew the connections black); both windings, the cutout material culls
        auto vertex = [&](int i) {
            tess.color(1.f, 1.f, 1.f, 1.f);
            tess.mNextUV[0] = std::optional<glm::vec2>(glm::vec2{t[i].x, t[i].y});
            tess.tex2(lightUv);
            tess.vertex(c[i].x, c[i].y, c[i].z);
        };
        for (int i : {0, 1, 2, 3}) vertex(i);
        for (int i : {3, 2, 1, 0}) vertex(i);
        ++quads;
    };
    for (int f = 0; f < 6; ++f) {
        if (game::intState(block, kFaceStates[f]).value_or(0) == 0) continue;
        if (glow) quad(f, kParts[Center]);
        for (int d = 0; d < 6; ++d) {
            if (d / 2 == f / 2) continue;
            bool flag = game::intState(block, kFaceStates[d]).value_or(0) != 0;
            if (!flag) flag = threadFace(region, rel(pos, d), f);
            if (!flag) {
                auto side    = rel(pos, d);
                bool blocked = region.getBlock(side).canProvideFullSupport(static_cast<uchar>(d ^ 1));
                flag         = !blocked && threadFace(region, rel(side, f), d ^ 1);
            }
            if (flag) quad(f, kParts[partFor(f, d)]);
        }
    }
    if (!begun) return;
    TextureVariant variant{moving->mAtlasTexture.get()};
    if (lit) {
        // the block's light (Java's packedLight of the block entity), as the moving block draws
        render::setupLight(ctx, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, false);
        MeshHelpers::renderMeshImmediately(ctx.screen, tess, *material, variant, OffscreenCaptureDescription{});
    } else {
        // full bright: the texture as it is (white shader colour), put back afterwards
        ShaderColor& color = ctx.screen.currentShaderColor;
        ShaderColor  saved = color;
        color.color        = mce::Color{1.f, 1.f, 1.f, 1.f};
        color.dirty        = true;
        MeshHelpers::renderMeshImmediately(ctx.screen, tess, *material, variant, OffscreenCaptureDescription{});
        color       = saved;
        color.dirty = true;
    }
    if (gThreadQuads.fetch_add(quads) == 0) log().info("tgear: red thread drawn ({} quads, {})", quads, glow ? "glow" : "cutout");
}
// ---- item icons (TravellersGearItemModel) -----------------------------------------------------------------

/// The modifier_directory of a gear item ("" for the belt and the gloves: no overlays).
std::string modifierDirectory(std::string const& id) {
    if (id == tg::ids::kGoggles) return "goggles";
    if (id == tg::ids::kVest) return "vest";
    if (id == tg::ids::kWings) return "wings";
    if (id == tg::ids::kBoots) return "boots";
    return "";
}

/// The broken base texture (the model's "broken" material), then one overlay per insertable modifier and
/// the gloves overlay, from the broken directory while broken. Drawn over the slot's item icon.
void drawGearIcon(render::RenderContext& ctx, ItemStack const& stack, render::ItemContext) {
    if (!ctx.ui) return;
    auto id  = items::idOf(stack);
    auto dir = modifierDirectory(id);
    if (dir.empty()) return;
    bool                     broken = tg::isBroken(stack);
    std::vector<std::string> layers;
    if (broken) layers.push_back("textures/twilightforest/item/" + id.substr(id.find(':') + 1) + "_broken");
    std::string base = "textures/twilightforest/item/travellers_modifiers/" + dir + (broken ? "/broken/" : "/");
    for (auto const& m : tg::insertableModifiers(stack)) layers.push_back(base + m.substr(m.find(':') + 1));
    if (id == tg::ids::kVest && tg::hasGloves(stack)) layers.push_back(base + "gloves");
    if (layers.empty()) return;
    // the slot's absolute position (UIControl::mCachedPosition): no translation of another control
    auto& world = ctx.screen.camera.worldMatrixStack.get();
    auto  ref   = world.push(false);
    auto& m     = ref.mat->_m.get();
    m[3][0] = m[3][1] = m[3][2] = 0.f;
    screen::Canvas canvas(*ctx.ui);
    int            drawn = 0;
    for (auto const& t : layers) drawn += ui::blit(canvas, t, {ctx.guiX, ctx.guiY, ctx.guiSize, ctx.guiSize}, 0, 0, 16, 16, 16, 16) ? 1 : 0;
    static std::atomic<int> logged{0};
    if (drawn > 0 && logged.fetch_add(1) < 3) log().info("tgear: gear icon {} drawn at ({}, {}) size {}: {} of {} layers", id, ctx.guiX, ctx.guiY, ctx.guiSize, drawn, layers.size());
}

// ---- tooltip (TravellersArmorItem#appendHoverText) ----------------------------------------------------------

/// TF's lines under the name (the server writes no lore for players with the client mod): the
/// abilities, each inserted modifier with its description lines while Shift is held, the empty slots,
/// the gloves' "Cosmetic", "Hold Shift for info" while Shift is up and something has a description,
/// and the stored items (the belt's container, the goggles' display; TF's tooltip images).
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
        auto m = tg::modifier(id);
        if (!m || !m->hasDescription) continue;
        auto colon = id.find(':');
        auto key   = "travellers_gear.modifier." + id.substr(0, colon) + "." + id.substr(colon + 1) + ".description";
        lines.push_back("§f§l" + ctr("travellers_gear.info_indent", "  ⤷ ") + "§r§f" + ctr(key, ""));
    }
    int slots = tg::modifierSlots(items::idOf(stack));
    for (int i = static_cast<int>(inserted.size()); i < slots; ++i) lines.push_back("§f- §8" + ctr("travellers_gear.modifier.empty", "Empty"));
    if (items::idOf(stack) == tg::ids::kGloves) lines.push_back("§7" + ctr("item.twilightforest.travellers_gloves.desc", "Cosmetic"));
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
        static bool once = false;
        if (once) return;
        once = true;
        movement::onLocalTick(0, [](movement::MoveState&) {
            if (auto* p = currentPlayer()) {
                tickZoom(*p);
                tickThreads(*p);
                tickStealth(*p);
            }
        });
        hud::addLayer("tgear.item_display", 100, drawHud);
        ext::tgear::client::setSlotClickHandler(&onSlotClick);
        render::registerBlockRenderer(tg::ids::kRedThread, drawThread, render::Flags{.throughWalls = true});
        for (auto const* id : tg::kGearItems) {
            tooltip::add(id, describe);
            render::registerItemRenderer(id, drawGearIcon, render::Gui);
        }
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
    void uninstall() { ext::tgear::client::uninstallHooks(); }
};

} // namespace

} // namespace gf::client::tgear

GF_REGISTER_MODULE(gf::client::tgear::TgearClientModule);

#endif // GF_CLIENT

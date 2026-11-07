// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/LockedBiomeToast.java
//   src/main/java/twilightforest/client/MissingAdvancementToast.java
//   src/main/java/twilightforest/client/UncraftingScreen.java (container screen layout)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client half of "client.ui" (F12). Client build only.
//
// - ContainerScreen: TF's container GUIs (AbstractContainerScreen): the GUI texture ("skin") drawn
//   1:1 at the panel with Java's blit, the title at Java's (8, 6) in 0x404040, the container slots
//   at their Java positions and, as TF's textures include it, the player's inventory at
//   (8, height - 82) with its label at (8, height - 94).
// - hud: layers drawn after the vanilla HUD (ll AfterUIRenderEvent on "hud_screen").
// - toast: TF's toasts (LockedBiomeToast, MissingAdvancementToast) in Java's ToastComponent: 160 x
//   32 at the top right over every screen, 5 slots (a toast keeps the slot it got; more wait in a
//   queue), 600 ms slide in / out with the in / out sound (Bedrock's UI toast sound), item at
//   (6, 8), title at (25, 7) and body at (25, 18) in the toast's colours (LockedBiomeToast: -256 and
//   white, the contract's default; MissingAdvancementToast: white and white), hidden after `ms`
//   shown; nothing is drawn (and the queue waits) while the GUI is hidden (F1), as ToastComponent.
//   The frame is the texture Bedrock draws its own toasts on (textures/ui/greyBorder, the dark
//   advancement-toast look of Java's toast/advancement sprite, which is Mojang's and not reused) as
//   a nine-slice over the 160 x 32; each line is the spec's literal text, else its key translated.
// - tooltip: lines under the item name of every hover text (ItemStackBase::getFormattedHovertext);
//   ContainerScreen's hover tooltips show that whole hover text (Java's getTooltipFromItem).
// - textures: resource pack paths, or absolute file paths read from the disk (client/render/TexturePath.h).

#ifdef GF_CLIENT

#include "client/ui/Ui.h"

#include <algorithm>
#include <atomic>
#include <variant>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TextureLoadState.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/render/TexturePath.h"
#include "feature/ext/ui/Client.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client {

namespace {

/// A key from the mod's translations (assets/lang + TF's imported lang), then the game's own
/// (resource pack texts), else the key itself.
std::string translateAny(std::string const& key) {
    if (key.empty()) return {};
    auto mod = translateClient(key, "\x01");
    if (mod != "\x01") return mod;
    try {
        auto& i18n = getI18n();
        auto  lang = i18n.getCurrentLanguage();
        auto  game = i18n.get(key, {}, lang.get());
        if (!game.empty()) return game;
    } catch (...) {}
    return key;
}

// ---- textures ------------------------------------------------------------------------------------

struct TextureCache {
    std::mutex                                         mutex;
    std::unordered_map<std::string, mce::TexturePtr*>  textures; // kept for the process (never destroyed at exit)
    std::unordered_map<std::string, bool>              warned;
};
TextureCache& textures() {
    static auto* c = new TextureCache;
    return *c;
}

/// The texture as the UI loads it (`ui`: the UI render context of this frame, which owns the
/// textures image controls draw with; without one, the client's texture group).
mce::TexturePtr* textureOf(std::string const& path, MinecraftUIRenderContext* ui) {
    auto&           cache = textures();
    std::lock_guard lock(cache.mutex);
    auto            key = (ui ? "ui|" : "group|") + path; // each loader keeps its own handle
    if (auto it = cache.textures.find(key); it != cache.textures.end()) return it->second;
    mce::TexturePtr* t = nullptr;
    if (ui) {
        t = new mce::TexturePtr(ui->getTexture(render::textureLocation(path), false));
    } else {
        auto* c = currentClient();
        if (!c) return nullptr;
        auto group = c->getTextureGroup();
        if (!group) return nullptr;
        t = new mce::TexturePtr(group->getTexture(render::textureLocation(path), false, std::nullopt, cg::TextureSetLayerType::Color));
    }
    cache.textures.emplace(key, t);
    return t;
}

BedrockTextureData const* dataOf(mce::TexturePtr const& t) { return t.mClientTexture.get(); }

bool loaded(BedrockTextureData const& d) {
    return (static_cast<unsigned>(d.mTextureLoadState) & static_cast<unsigned>(TextureLoadState::LoadedBit)) != 0;
}

/// How blit draws (spike S5 compares them in the fixture HUD): the UI's image batch flushed with
/// "ui_flush" or "ui_textured_and_glcolor", or a textured mesh with the "ui_textured" material.
[[maybe_unused]] constexpr int kBlitFlush = 0; // the default flush of the image batch
constexpr int    kBlitFlushGlColor = 1;
constexpr int    kBlitMesh         = 2;
std::atomic<int> gBlitMethod{kBlitMesh};

/// A UI material (common group first, then the switchable one), kept for the process.
mce::MaterialPtr const* uiMaterial(char const* name) {
    static std::mutex                               m;
    static std::map<std::string, mce::MaterialPtr*> cache;
    std::lock_guard                                 lock(m);
    auto&                                           slot = cache[name];
    if (!slot) {
        slot = new mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString(name));
        if (!slot->mRenderMaterialInfoPtr.get()) slot = new mce::MaterialPtr(mce::RenderMaterialGroup::switchable(), HashedString(name));
        if (!slot->mRenderMaterialInfoPtr.get()) log().warn("ui: no material {}", name);
    }
    return slot->mRenderMaterialInfoPtr.get() ? slot : nullptr;
}

std::optional<std::pair<int, int>> sizeOf(mce::TexturePtr const* t) {
    if (!t) return std::nullopt;
    auto const* data = dataOf(*t);
    if (!data || data->mIsMissingTexture == IsMissingTexture::Yes) return std::nullopt;
    auto const& d = data->mTextureDescription.get();
    if (d.mWidth == 0 || d.mHeight == 0) return std::nullopt;
    return std::pair{static_cast<int>(d.mWidth), static_cast<int>(d.mHeight)};
}

} // namespace

namespace ui {

bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW, float texH) {
    return blit(canvas, texture, dst, u, v, w, h, texW, texH, Blend::Normal);
}

bool blitSprite(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, Blend blend) {
    auto size = sizeOf(textureOf(texture, &canvas.context()));
    if (!size) {
        // still loading or missing: blit tells which (and logs a missing one once)
        return blit(canvas, texture, dst, 0, 0, 1, 1, 1, 1, blend);
    }
    auto [tw, th] = *size;
    return blit(canvas, texture, dst, 0, 0, static_cast<float>(tw), static_cast<float>(th), static_cast<float>(tw), static_cast<float>(th), blend);
}

bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW, float texH, Blend blend) {
    auto* t = textureOf(texture, &canvas.context());
    if (!t) return false;
    auto const* data = dataOf(*t);
    if (data && data->mIsMissingTexture != IsMissingTexture::Yes && !loaded(*data)) return false; // still loading
    if (!data || data->mIsMissingTexture == IsMissingTexture::Yes) {
        auto&           cache = textures();
        std::lock_guard lock(cache.mutex);
        if (!cache.warned[texture]) {
            cache.warned[texture] = true;
            log().warn("ui: texture {} is missing", texture);
        }
        return false;
    }
    auto& ctx = canvas.context();
    float x0 = canvas.originX() + dst.x, y0 = canvas.originY() + dst.y, x1 = x0 + dst.w, y1 = y0 + dst.h;
    float u0 = u / texW, v0 = v / texH, u1 = (u + w) / texW, v1 = (v + h) / texH;
    int   method = blend == Blend::Invert ? kBlitMesh : gBlitMethod.load();
    if (method == kBlitMesh) {
        // a textured quad in GUI pixels with a UI material (the UI's matrices map GUI pixels); the
        // inverting blend is the game's crosshair material (ui.material: ui_crosshair)
        auto const* material = uiMaterial(blend == Blend::Invert ? "ui_crosshair" : "ui_textured");
        if (!material) return false;
        if (blend == Blend::Invert) {
            static std::atomic<bool> told{false};
            if (!told.exchange(true)) log().info("ui: sprite drawn with the inverting crosshair blend ({})", texture);
        }
        ScreenContext& sc   = ctx.mScreenContext;
        Tessellator&   tess = sc.tessellator;
        tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 8, false);
        tess.color(1.f, 1.f, 1.f, 1.f);
        // both windings: whichever way the UI's projection turns them
        tess.simpleQuad(Vec3{x0, y1, 0}, Vec2{u0, v1}, Vec3{x1, y1, 0}, Vec2{u1, v1}, Vec3{x1, y0, 0}, Vec2{u1, v0}, Vec3{x0, y0, 0}, Vec2{u0, v0});
        tess.simpleQuad(Vec3{x0, y0, 0}, Vec2{u0, v0}, Vec3{x1, y0, 0}, Vec2{u1, v0}, Vec3{x1, y1, 0}, Vec2{u1, v1}, Vec3{x0, y1, 0}, Vec2{u0, v1});
        std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> tex{*t};
        MeshHelpers::renderMeshImmediately(sc, tess, *material, tex, OffscreenCaptureDescription{});
        return true;
    }
    ctx.drawImage(data->mClientTexture.get(), glm::vec2{x0, y0}, glm::vec2{dst.w, dst.h}, glm::vec2{u0, v0}, glm::vec2{u1 - u0, v1 - v0}, false);
    static HashedString const flush("ui_flush");
    static HashedString const glcolor("ui_textured_and_glcolor");
    ctx.flushImages(mce::Color{1, 1, 1, 1}, 1.0f, method == kBlitFlushGlColor ? glcolor : flush);
    return true;
}

bool blitNineSlice(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float texW, float texH, float slice) {
    // columns and rows: the corners `slice` pixels on both sides, 1:1; the rest stretched
    float const sw = std::min(slice, dst.w / 2), sh = std::min(slice, dst.h / 2);
    float const xs[3]{dst.x, dst.x + sw, dst.x + dst.w - sw}, ws[3]{sw, dst.w - 2 * sw, sw};
    float const ys[3]{dst.y, dst.y + sh, dst.y + dst.h - sh}, hs[3]{sh, dst.h - 2 * sh, sh};
    float const us[3]{0, slice, texW - slice}, uw[3]{slice, texW - 2 * slice, slice};
    float const vs[3]{0, slice, texH - slice}, vh[3]{slice, texH - 2 * slice, slice};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (ws[col] <= 0 || hs[row] <= 0) continue;
            // false only when nothing was drawn (the texture is not loaded), never half a frame
            if (!blit(canvas, texture, {xs[col], ys[row], ws[col], hs[row]}, us[col], vs[row], uw[col], vh[row], texW, texH) && row == 0 && col == 0) return false;
        }
    }
    return true;
}

int blitMethod() { return gBlitMethod.load(); }
void setBlitMethod(int method) { gBlitMethod = method; }

std::optional<std::pair<int, int>> textureSize(std::string const& texture) { return sizeOf(textureOf(texture, nullptr)); }

} // namespace ui

// ---- container screens ---------------------------------------------------------------------------

namespace screen {

namespace {
constexpr unsigned kTitleColour = 0xFF404040; // Java's 4210752
constexpr unsigned kSlotHover   = 0x80FFFFFF; // AbstractContainerScreen.renderSlotHighlight
/// Per-screen state the (immutable) ContainerScreen header has no room for: the slot stacks and
/// the title once the skin draws it at Java's place (the framework's centred title is then off).
struct ScreenSide {
    std::map<int, std::shared_ptr<ItemStack>> items;
    std::string                               title;
    /// The screen's LifeToken widget is alive: the state goes when the screen is destroyed. False
    /// once the widget went while its screen lived on (a subclass cleared `widgets`): then the state
    /// goes after kOrphanGrace without its screen being the open one.
    bool                                  tracked = false;
    std::chrono::steady_clock::time_point seen{}; ///< last set, drawn or found open
};
struct ContainerItems {
    std::mutex                                             mutex;
    std::unordered_map<ContainerScreen const*, ScreenSide> items;
    std::unordered_map<ContainerScreen const*, int>        tokens; ///< live LifeTokens per screen
};
ContainerItems& containerItems() {
    static ContainerItems c;
    return c;
}
constexpr auto kOrphanGrace = std::chrono::seconds(10);

/// An invisible widget every ContainerScreen holds (the first of `widgets`): the screen's widgets
/// are destroyed with it, so its destructor tells when the screen is gone (the ContainerScreen
/// header has no destructor of ours).
class LifeToken final : public Widget {
public:
    explicit LifeToken(ContainerScreen const* owner) : Widget(Rect{0, 0, 0, 0}), mOwner(owner) {
        visible = false;
        enabled = false;
        auto&           ci = containerItems();
        std::lock_guard lock(ci.mutex);
        ++ci.tokens[mOwner];
    }
    ~LifeToken() override {
        try {
            auto&           ci = containerItems();
            std::lock_guard lock(ci.mutex);
            if (auto t = ci.tokens.find(mOwner); t != ci.tokens.end() && --t->second <= 0) ci.tokens.erase(t);
            if (ci.tokens.contains(mOwner)) return;
            auto it = ci.items.find(mOwner);
            if (it == ci.items.end()) return;
            // the framework releases its screen before destroying it, so a screen that is still the
            // open one lives on (a subclass cleared its widgets): the time rule from now on
            if (static_cast<Screen const*>(mOwner) == current()) {
                it->second.tracked = false;
                it->second.seen    = std::chrono::steady_clock::now();
            } else ci.items.erase(it);
        } catch (...) {}
    }
    void render(Canvas&, float, float) override {}

private:
    ContainerScreen const* mOwner;
};

Rect slotRect(float x, float y) { return Rect{x, y, 16, 16}; }

/// Java's player inventory inside a TF skin: main rows at y = height - 82, hotbar at height - 24.
Rect inventoryRect(ContainerScreen const& s, int slot) {
    float top = s.height - 82;
    if (slot >= 9) return slotRect(8 + static_cast<float>((slot - 9) % 9) * 18, top + static_cast<float>((slot - 9) / 9) * 18);
    return slotRect(8 + static_cast<float>(slot) * 18, s.height - 24);
}

bool skinHasInventory(ContainerScreen const& s) { return !s.showInventory && s.height >= 166; }

/// The states whose screen lost its LifeToken while it lived (see ScreenSide::tracked): they go once
/// the screen has not been the open one for kOrphanGrace. States of live screens (tracked) stay
/// however long the screen is covered or waits to be opened.
void pruneContainerStates() {
    auto&           ci = containerItems();
    std::lock_guard lock(ci.mutex);
    if (ci.items.empty()) return;
    Screen const* open = current();
    auto          now  = std::chrono::steady_clock::now();
    for (auto it = ci.items.begin(); it != ci.items.end();) {
        bool isOpen = static_cast<Screen const*>(it->first) == open;
        if (isOpen) it->second.seen = now;
        if (!it->second.tracked && !isOpen && now - it->second.seen > kOrphanGrace) it = ci.items.erase(it);
        else ++it;
    }
}

/// The state of `s` (tracked while its LifeToken lives). Under the lock.
ScreenSide& sideOf(ContainerItems& ci, ContainerScreen const& s) {
    auto& side   = ci.items[&s];
    side.tracked = ci.tokens.contains(&s);
    side.seen    = std::chrono::steady_clock::now();
    return side;
}
} // namespace

ContainerScreen::ContainerScreen(std::string title, std::string skin, float width, float height)
: Screen(std::move(title), width, height),
  skin(std::move(skin)) {
    {
        // a screen that lost its token may have lived at this address: start without its state
        auto&           ci = containerItems();
        std::lock_guard lock(ci.mutex);
        ci.items.erase(this);
    }
    add<LifeToken>(this);
}

void ContainerScreen::renderExtra(Canvas& c, float mx, float my) {
    std::vector<std::string> tip;
    bool                     skinned = false;
    if (!skin.empty()) {
        auto size = sizeOf(textureOf(skin, &c.context())).value_or(std::pair{256, 256});
        skinned   = ui::blit(c, skin, {0, 0, width, height}, 0, 0, width, height, static_cast<float>(size.first), static_cast<float>(size.second));
    }
    std::string shownTitle;
    {
        auto&           ci   = containerItems();
        std::lock_guard lock(ci.mutex);
        auto&           side = sideOf(ci, *this);
        if (skinned && !title.empty()) side.title = std::exchange(title, std::string{}); // drawn here from now on
        if (!skinned && title.empty() && !side.title.empty()) title = side.title;        // skin gone: the framework's title
        shownTitle = side.title.empty() ? title : side.title;
    }
    if (skinned) {
        // the skin covers the framework's panel: title and widgets again on top of it (Java order)
        c.text(8, 6, shownTitle, kTitleColour, Align::Left, false);
        if (skinHasInventory(*this)) c.text(8, height - 94, translateAny("container.inventory"), kTitleColour, Align::Left, false);
        for (auto const& w : widgets) {
            if (w->visible) w->render(c, mx, my);
        }
    }
    {
        auto&           ci = containerItems();
        std::lock_guard lock(ci.mutex);
        auto            it = ci.items.find(this);
        for (auto const& sp : slots) {
            auto r = slotRect(sp.x, sp.y);
            if (it != ci.items.end()) {
                if (auto s = it->second.items.find(sp.index); s != it->second.items.end() && s->second && !s->second->isNull()) {
                    c.item(*s->second, r.x, r.y, true);
                    if (r.contains(mx, my)) tip = ui::hoverLines(*s->second);
                }
            }
            if (r.contains(mx, my)) c.fill(r, kSlotHover);
        }
    }
    if (skinned && skinHasInventory(*this)) {
        if (auto* player = currentPlayer()) {
            auto& inv = player->getInventory();
            for (int slot = 0; slot < 36; ++slot) {
                auto        r = inventoryRect(*this, slot);
                auto const& s = inv.getItem(slot);
                if (!s.isNull() && s.mCount > 0) {
                    c.item(s, r.x, r.y, true);
                    if (r.contains(mx, my)) tip = ui::hoverLines(s);
                }
                if (r.contains(mx, my)) c.fill(r, kSlotHover);
            }
        }
    }
    if (!tip.empty()) c.tooltip(mx, my, tip);
}

void registerScreen(std::string const& type, Factory factory) { registerServerScreen(type, std::move(factory)); }

} // namespace screen

namespace ui {

void setContainerItems(screen::ContainerScreen& s, nlohmann::json const& items) {
    auto&           ci = screen::containerItems();
    std::lock_guard lock(ci.mutex);
    // the state goes with the screen (its LifeToken; a reused address starts afresh in the constructor)
    auto& side = screen::sideOf(ci, s);
    auto& map  = side.items;
    map.clear();
    if (!items.is_object()) return;
    for (auto const& [k, v] : items.items()) {
        try {
            map[std::stoi(k)] = screen::iconOf(v);
        } catch (...) {}
    }
}

std::size_t containerStateCount() {
    auto&           ci = screen::containerItems();
    std::lock_guard lock(ci.mutex);
    return ci.items.size();
}

int containerSlotAt(screen::ContainerScreen const& s, float mx, float my) {
    for (auto const& sp : s.slots) {
        if (screen::slotRect(sp.x, sp.y).contains(mx, my)) return sp.index;
    }
    return -1;
}

int skinInventorySlotAt(screen::ContainerScreen const& s, float mx, float my) {
    if (!screen::skinHasInventory(s)) return -1;
    for (int slot = 0; slot < 36; ++slot) {
        if (screen::inventoryRect(s, slot).contains(mx, my)) return slot;
    }
    return -1;
}

} // namespace ui

// ---- HUD layers -------------------------------------------------------------------------------------

namespace hud {
namespace {
struct Layer {
    std::string id;
    int         order;
    DrawFn      fn;
};
struct Layers {
    std::mutex         mutex;
    std::vector<Layer> list; ///< sorted by order
};
Layers& layers() {
    static Layers l;
    return l;
}
} // namespace

void addLayer(std::string const& id, int order, DrawFn fn) {
    if (!fn) return;
    auto&           l = layers();
    std::lock_guard lock(l.mutex);
    std::erase_if(l.list, [&id](Layer const& x) { return x.id == id; });
    l.list.push_back({id, order, std::move(fn)});
    std::stable_sort(l.list.begin(), l.list.end(), [](Layer const& a, Layer const& b) { return a.order < b.order; });
}
} // namespace hud

namespace ui {
std::vector<std::string> hudLayers() {
    auto&                    l = hud::layers();
    std::lock_guard          lock(l.mutex);
    std::vector<std::string> out;
    for (auto const& x : l.list) out.push_back(x.id);
    return out;
}
void removeHudLayer(std::string const& id) {
    auto&           l = hud::layers();
    std::lock_guard lock(l.mutex);
    std::erase_if(l.list, [&id](hud::Layer const& x) { return x.id == id; });
}
} // namespace ui

// ---- toasts -----------------------------------------------------------------------------------------

namespace toast {
namespace {
using Clock                  = std::chrono::steady_clock;
constexpr float kToastWidth  = 160;
constexpr float kToastHeight = 32;   // one slot
constexpr long  kSlideMs     = 600;  // ToastInstance: the slide in / out
constexpr int   kSlots       = kSlotCount; // ToastComponent.occupiedSlots (shared: claimSlot)
/// Bedrock's UI toast sound (Java's ui.toast.in / ui.toast.out have no Bedrock counterpart; the
/// hide plays it lower).
constexpr char  kToastSound[] = "random.toast";

/// A toast on screen (Java's ToastInstance): it keeps its slot until it has slid out.
struct Shown {
    LiteralToastSpec           spec;
    Style                      style;
    std::shared_ptr<ItemStack> icon;
    std::string                title, body;
    int                        index         = 0;
    long                       animationTime = -1; ///< ms on the toast clock; -1 before the first frame
    long                       visibleTime   = 0;
    bool                       show          = true; ///< Visibility.SHOW / HIDE
};
struct Queued {
    LiteralToastSpec spec;
    Style            style;
};
struct Toasts {
    std::mutex         mutex;
    std::vector<Shown> visible;
    std::deque<Queued> queued;
    bool               occupied[kSlots]{}; ///< slots of F12's own toasts
    bool               claimed[kSlots]{};  ///< slots other drawers claimed (claimSlot)
    bool               logged       = false;
    bool               hiddenLogged = false;
};
Toasts& toasts() {
    static Toasts t;
    return t;
}

long nowMs() {
    static auto const start = Clock::now();
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

void playSound(bool show) {
    if (auto* c = currentClient()) {
        try {
            c->playUI(kToastSound, 1.0f, show ? 1.0f : 0.8f);
        } catch (...) {}
    }
}

/// Java's blitSprite(toast/advancement, 0, 0, 160, 32): the game's own toast frame (kFrameTexture) as
/// a nine-slice; while it loads (or should it be missing), its colours as plain fills.
void drawBackground(screen::Canvas& c, float x, float y) {
    static std::atomic<bool> logged{false};
    if (ui::blitNineSlice(c, kFrameTexture, {x, y, kToastWidth, kToastHeight}, kFrameSize, kFrameSize, kFrameSlice)) {
        if (!logged.exchange(true)) log().info("ui: toast frame drawn with {}", kFrameTexture);
        return;
    }
    c.fill({x + 2, y, kToastWidth - 4, kToastHeight}, 0xFF000000);
    c.fill({x, y + 2, kToastWidth, kToastHeight - 4}, 0xFF000000);
    c.fill({x + 1, y + 1, kToastWidth - 2, kToastHeight - 2}, 0xFF555555);
    c.fill({x + 3, y + 3, kToastWidth - 6, kToastHeight - 6}, 0xFF212121);
}

/// What a toast's line shows: the literal text, else the key translated on this client.
std::string lineOf(std::string const& literal, std::string const& key) { return literal.empty() ? translateAny(key) : literal; }

/// Whether a toast answers to `title` (its key or its literal title).
bool named(LiteralToastSpec const& t, std::string const& title) { return !title.empty() && (t.spec.titleKey == title || t.title == title); }

/// ToastInstance.getVisibility: f = clamp(t / 600)², 1 - f while hiding.
float visibility(Shown const& s, long now) {
    float f = std::clamp(static_cast<float>(now - s.animationTime) / static_cast<float>(kSlideMs), 0.0f, 1.0f);
    f *= f;
    return s.show ? f : 1 - f;
}

/// ToastInstance.render; true when the toast has slid out.
bool renderOne(screen::Canvas& c, float guiWidth, Shown& s, long now) {
    if (s.animationTime == -1) {
        s.animationTime = now;
        playSound(true);
    }
    if (s.show && now - s.animationTime <= kSlideMs) s.visibleTime = now;
    float x = guiWidth - kToastWidth * visibility(s, now);
    float y = static_cast<float>(s.index) * kToastHeight;
    drawBackground(c, x, y);
    if (s.icon) c.item(*s.icon, x + 6, y + 8);
    c.text(x + 25, y + 7, s.title, s.style.titleColor, screen::Align::Left, false);
    c.text(x + 25, y + 18, s.body, s.style.bodyColor, screen::Align::Left, false);
    // the toast's own render: hidden once it was shown `ms` (LockedBiomeToast: timer >= 10000)
    bool show = now - s.visibleTime < std::max(0, s.spec.spec.ms);
    if (show != s.show) {
        s.animationTime = now - static_cast<long>((1.0f - visibility(s, now)) * static_cast<float>(kSlideMs));
        s.show          = show;
        playSound(show);
    }
    return !s.show && now - s.animationTime > kSlideMs;
}

/// ToastComponent.render: draws the visible toasts, frees the slots of those gone, then moves
/// queued toasts into free slots (drawn from the next frame).
void render(screen::Canvas& c, float guiWidth) {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    // Java's ToastComponent.render does nothing while options.hideGui (the queue waits too)
    if (hud::guiHidden()) {
        if (!t.hiddenLogged && !(t.visible.empty() && t.queued.empty())) {
            t.hiddenLogged = true;
            log().info("ui: toasts not drawn while the GUI is hidden");
        }
        return;
    }
    t.hiddenLogged = false;
    long const now = nowMs();
    for (auto it = t.visible.begin(); it != t.visible.end();) {
        if (renderOne(c, guiWidth, *it, now)) {
            t.occupied[it->index] = false;
            it                    = t.visible.erase(it);
            continue;
        }
        if (!t.logged) {
            t.logged = true;
            log().info("ui: toast drawn: {}", it->title);
        }
        ++it;
    }
    while (!t.queued.empty()) {
        int free = -1;
        for (int i = 0; i < kSlots && free < 0; ++i)
            if (!t.occupied[i] && !t.claimed[i]) free = i;
        if (free < 0) break;
        Shown s;
        s.spec  = std::move(t.queued.front().spec);
        s.style = t.queued.front().style;
        s.index = free;
        t.queued.pop_front();
        if (!s.spec.spec.iconItem.empty()) {
            if (auto st = game::items::make(s.spec.spec.iconItem)) s.icon = std::make_shared<ItemStack>(std::move(*st));
        }
        s.title          = lineOf(s.spec.title, s.spec.spec.titleKey);
        s.body           = lineOf(s.spec.body, s.spec.spec.bodyKey);
        t.occupied[free] = true;
        t.visible.push_back(std::move(s));
    }
}
} // namespace

int claimSlot() {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    for (int i = 0; i < kSlots; ++i) {
        if (!t.occupied[i] && !t.claimed[i]) {
            t.claimed[i] = true;
            return i;
        }
    }
    return -1;
}

void releaseSlot(int slot) {
    if (slot < 0 || slot >= kSlots) return;
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    t.claimed[slot] = false; // F12's own slots (occupied) are not a claimer's to free
}

int slotsTaken() {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    int n = 0;
    for (int i = 0; i < kSlots; ++i) n += t.occupied[i] || t.claimed[i] ? 1 : 0;
    return n;
}

void show(ToastSpec const& spec) { show(spec, Style{}); }

void show(ToastSpec const& spec, Style style) { show(LiteralToastSpec{spec, {}, {}}, style); }

void show(LiteralToastSpec const& toast, Style style) {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    t.queued.push_back({toast, style});
    t.logged = false;
}

std::size_t cancel(std::string const& title) {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    std::size_t     n = std::erase_if(t.queued, [&](Queued const& q) { return named(q.spec, title); });
    for (auto it = t.visible.begin(); it != t.visible.end();) {
        if (!named(it->spec, title)) {
            ++it;
            continue;
        }
        t.occupied[it->index] = false;
        it                    = t.visible.erase(it);
        ++n;
    }
    return n;
}

std::optional<Style> styleOfToast(std::string const& title) {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    for (auto const& s : t.visible)
        if (named(s.spec, title)) return s.style;
    for (auto const& q : t.queued)
        if (named(q.spec, title)) return q.style;
    return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> linesOfToast(std::string const& title) {
    std::optional<LiteralToastSpec> waiting;
    {
        auto&           t = toasts();
        std::lock_guard lock(t.mutex);
        for (auto const& s : t.visible)
            if (named(s.spec, title)) return std::pair{s.title, s.body};
        for (auto const& q : t.queued) {
            if (named(q.spec, title)) {
                waiting = q.spec;
                break;
            }
        }
    }
    if (!waiting) return std::nullopt;
    return std::pair{lineOf(waiting->title, waiting->spec.titleKey), lineOf(waiting->body, waiting->spec.bodyKey)};
}
} // namespace toast

namespace hud {
bool guiHidden() {
    auto* c = currentClient();
    if (!c) return false;
    try {
        return c->getOptions().getHideHud();
    } catch (...) {
        return false;
    }
}
} // namespace hud

namespace ui {
std::size_t toastCount() {
    auto&           t = toast::toasts();
    std::lock_guard lock(t.mutex);
    return t.visible.size() + t.queued.size();
}
} // namespace ui

// ---- tooltips ---------------------------------------------------------------------------------------

namespace tooltip {
namespace {
struct Tooltips {
    std::mutex                                           mutex;
    std::unordered_map<std::string, std::vector<TooltipFn>> fns;
};
Tooltips& tips() {
    static Tooltips t;
    return t;
}
} // namespace

void add(std::string const& itemId, TooltipFn fn) {
    if (!fn) return;
    auto&           t = tips();
    std::lock_guard lock(t.mutex);
    t.fns[itemId].push_back(std::move(fn));
}
} // namespace tooltip

namespace ui {
std::vector<std::string> tooltipLines(ItemStack const& stack) {
    std::vector<std::string>        lines;
    std::vector<tooltip::TooltipFn> fns;
    {
        auto&           t = tooltip::tips();
        std::lock_guard lock(t.mutex);
        auto            it = t.fns.find(stack.getTypeName());
        if (it == t.fns.end()) return lines;
        fns = it->second;
    }
    for (auto& fn : fns) {
        try {
            fn(stack, lines);
        } catch (std::exception const& e) {
            log().error("ui: tooltip of {} failed: {}", stack.getTypeName(), e.what());
        }
    }
    return lines;
}

bool removeTooltips(std::string const& itemId) {
    auto&           t = tooltip::tips();
    std::lock_guard lock(t.mutex);
    return t.fns.erase(itemId) > 0;
}

std::vector<std::string> hoverLines(ItemStack const& stack) {
    std::vector<std::string> lines;
    if (stack.isNull()) return lines;
    auto* c     = currentClient();
    auto* level = c ? c->getLevel() : nullptr;
    std::string text;
    if (level) {
        try {
            text = stack.getFormattedHovertext(*level, false).mUnredactedString; // the hook adds the providers' lines
        } catch (...) {}
    }
    if (text.empty()) text = stack.getName();
    std::size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}
} // namespace ui

namespace {
bool hasTooltips(std::string const& id) {
    auto&           t = tooltip::tips();
    std::lock_guard lock(t.mutex);
    return t.fns.contains(id);
}

void insertLines(std::string& text, std::vector<std::string> const& lines) {
    std::string add;
    for (auto const& l : lines) add += "\n" + l;
    auto nl = text.find('\n');
    text    = nl == std::string::npos ? text + add : text.substr(0, nl) + add + text.substr(nl);
}
} // namespace

} // namespace gf::client

// Lines under the item name of every hover text. `this` may be an ItemInstance: the lines are
// computed on a stack of our own with the same item, count, aux and user data.
LL_TYPE_INSTANCE_HOOK(
    UiTooltipHook,
    ll::memory::HookPriority::Normal,
    ItemStackBase,
    &ItemStackBase::getFormattedHovertext,
    Bedrock::Safety::RedactableString,
    Level& level,
    bool   showCategory
) {
    auto r = origin(level, showCategory);
    try {
        if (isNull()) return r;
        auto id = getTypeName();
        if (!gf::client::hasTooltips(id)) return r;
        auto stack = gf::game::items::make(id, std::max<int>(1, mCount), getAuxValue());
        if (!stack) return r;
        if (mUserData) stack->setUserData(mUserData->clone());
        auto lines = gf::client::ui::tooltipLines(*stack);
        if (lines.empty()) return r;
        gf::client::insertLines(r.mUnredactedString, lines);
        if (r.mRedactedString) gf::client::insertLines(*r.mRedactedString, lines);
    } catch (...) {}
    return r;
}

namespace gf::client::ui {
namespace {

void renderHud(MinecraftUIRenderContext& ctx) {
    std::vector<hud::Layer> list;
    {
        auto&           l = hud::layers();
        std::lock_guard lock(l.mutex);
        list = l.list;
    }
    if (list.empty()) return;
    auto*          c       = currentClient();
    float          partial = c ? c->getFrameAlpha() : 0.0f;
    screen::Canvas canvas(ctx);
    for (auto& layer : list) {
        canvas.setOrigin(0, 0);
        try {
            layer.fn(canvas, partial);
        } catch (std::exception const& e) {
            log().error("ui: HUD layer {} failed: {}", layer.id, e.what());
        }
    }
}

/// Toasts over every screen (Java draws them after the screen): on the top scene only.
void renderToasts(MinecraftUIRenderContext& ctx) {
    auto [gw, gh] = screen::guiSize();
    screen::Canvas canvas(ctx);
    toast::render(canvas, gw);
}

/// The scene whose cancelled BeforeUIRenderEvent drew the toasts this frame (UI thread).
ScreenView const*& toastsDrawnFor() {
    static ScreenView const* view = nullptr;
    return view;
}

bool isTopScene(ScreenView& view) {
    auto* c = currentClient();
    return c && view.getScreenName() == c->getScreenName();
}

class UiModule {
public:
    // render listeners at mod load, before the game's threads run (see screen::ScreenModule);
    // lowest priority: drawn after everything else draws on the scene
    void load() {
        if (!mListeners.empty()) return;
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::AfterUIRenderEvent>(
            [](ll::event::AfterUIRenderEvent& ev) {
                try {
                    screen::pruneContainerStates();
                    if (ev.screenView().getScreenName() == "hud_screen") renderHud(ev.uiRenderContext());
                    // not twice when the cancelled BeforeUIRenderEvent of this scene drew them
                    bool drawn          = toastsDrawnFor() == &ev.screenView();
                    toastsDrawnFor()    = nullptr;
                    if (!drawn && isTopScene(ev.screenView())) renderToasts(ev.uiRenderContext());
                } catch (...) {}
            },
            ll::event::EventPriority::Lowest
        ));
        // a screen of the client screen framework draws in the host scene's BeforeUIRenderEvent and
        // cancels the scene: the toasts then go on top of it there
        mListeners.push_back(bus.emplaceListener<ll::event::BeforeUIRenderEvent>(
            [](ll::event::BeforeUIRenderEvent& ev) {
                toastsDrawnFor() = nullptr;
                try {
                    if (ev.isCancelled() && isTopScene(ev.screenView())) {
                        renderToasts(ev.uiRenderContext());
                        toastsDrawnFor() = &ev.screenView();
                    }
                } catch (...) {}
            },
            ll::event::EventPriority::Lowest
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            auto&           t = toast::toasts();
            std::lock_guard lock(t.mutex);
            t.visible.clear();
            t.queued.clear();
            // F12's own slots only: a claimer gives its claims back itself (it drops its toasts too)
            std::fill(std::begin(t.occupied), std::end(t.occupied), false);
        }));
        UiTooltipHook::hook();
    }
    void install() { load(); }
    void uninstall() {
        UiTooltipHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace
} // namespace gf::client::ui

GF_REGISTER_MODULE_ORDERED(gf::client::ui::UiModule, gf::ModuleOrder::Contracts);

#endif // GF_CLIENT

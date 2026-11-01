// Client half of "client.ui" (F12). Client build only.
//
// - ContainerScreen: TF's container GUIs (AbstractContainerScreen): the GUI texture ("skin") drawn
//   1:1 at the panel with Java's blit, the title at Java's (8, 6) in 0x404040, the container slots
//   at their Java positions and, as TF's textures include it, the player's inventory at
//   (8, height - 82) with its label at (8, height - 94).
// - hud: layers drawn after the vanilla HUD (ll AfterUIRenderEvent on "hud_screen").
// - toast: TF's toasts (LockedBiomeToast, MissingAdvancementToast): Java's ToastComponent layout
//   (160 x 32, top right, 600 ms slide in / out, at most 5 at once, item at (6, 8), title at
//   (25, 7) in -256, body at (25, 18) in white). The background is a redraw of the advancement
//   frame (Mojang's sprite is not reused).
// - tooltip: lines under the item name of every hover text (ItemStackBase::getFormattedHovertext).

#ifdef GF_CLIENT

#include "client/ui/Ui.h"

#include <algorithm>
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
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/file/PathView.h"
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

mce::TexturePtr* textureOf(std::string const& path) {
    auto&           cache = textures();
    std::lock_guard lock(cache.mutex);
    if (auto it = cache.textures.find(path); it != cache.textures.end()) return it->second;
    auto* c = currentClient();
    if (!c) return nullptr;
    auto group = c->getTextureGroup();
    if (!group) return nullptr;
    auto* t = new mce::TexturePtr(group->getTexture(ResourceLocation(Core::PathView(path)), false, std::nullopt, cg::TextureSetLayerType::Color));
    cache.textures.emplace(path, t);
    return t;
}

BedrockTextureData const* dataOf(mce::TexturePtr const& t) { return t.mClientTexture.get(); }

} // namespace

namespace ui {

bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW, float texH) {
    auto* t = textureOf(texture);
    if (!t) return false;
    auto const* data = dataOf(*t);
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
    glm::vec2 pos{canvas.originX() + dst.x, canvas.originY() + dst.y};
    glm::vec2 size{dst.w, dst.h};
    glm::vec2 uv{u / texW, v / texH};
    glm::vec2 uvSize{w / texW, h / texH};
    ctx.drawImage(data->mClientTexture.get(), pos, size, uv, uvSize, false);
    static HashedString const flush("ui_flush");
    ctx.flushImages(mce::Color{1, 1, 1, 1}, 1.0f, flush);
    return true;
}

std::optional<std::pair<int, int>> textureSize(std::string const& texture) {
    auto* t = textureOf(texture);
    if (!t) return std::nullopt;
    auto const* data = dataOf(*t);
    if (!data || data->mIsMissingTexture == IsMissingTexture::Yes) return std::nullopt;
    auto const& d = data->mTextureDescription.get();
    if (d.mWidth == 0 || d.mHeight == 0) return std::nullopt;
    return std::pair{static_cast<int>(d.mWidth), static_cast<int>(d.mHeight)};
}

} // namespace ui

// ---- container screens ---------------------------------------------------------------------------

namespace screen {

namespace {
constexpr unsigned kTitleColour = 0xFF404040; // Java's 4210752
constexpr unsigned kSlotHover   = 0x80FFFFFF; // AbstractContainerScreen.renderSlotHighlight
struct ContainerItems {
    std::mutex                                                                        mutex;
    std::unordered_map<ContainerScreen const*, std::map<int, std::shared_ptr<ItemStack>>> items;
};
ContainerItems& containerItems() {
    static ContainerItems c;
    return c;
}

Rect slotRect(float x, float y) { return Rect{x, y, 16, 16}; }

/// Java's player inventory inside a TF skin: main rows at y = height - 82, hotbar at height - 24.
Rect inventoryRect(ContainerScreen const& s, int slot) {
    float top = s.height - 82;
    if (slot >= 9) return slotRect(8 + static_cast<float>((slot - 9) % 9) * 18, top + static_cast<float>((slot - 9) / 9) * 18);
    return slotRect(8 + static_cast<float>(slot) * 18, s.height - 24);
}

bool skinHasInventory(ContainerScreen const& s) { return !s.showInventory && s.height >= 166; }
} // namespace

ContainerScreen::ContainerScreen(std::string title, std::string skin, float width, float height)
: Screen(std::move(title), width, height),
  skin(std::move(skin)) {}

void ContainerScreen::renderExtra(Canvas& c, float mx, float my) {
    std::vector<std::string> tip;
    bool                     skinned = false;
    if (!skin.empty()) {
        auto size = ui::textureSize(skin).value_or(std::pair{256, 256});
        skinned   = ui::blit(c, skin, {0, 0, width, height}, 0, 0, width, height, static_cast<float>(size.first), static_cast<float>(size.second));
    }
    if (skinned) {
        // the skin covers the framework's panel: title and widgets again on top of it (Java order)
        c.text(8, 6, title, kTitleColour, Align::Left, false);
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
                if (auto s = it->second.find(sp.index); s != it->second.end() && s->second && !s->second->isNull()) {
                    c.item(*s->second, r.x, r.y, true);
                    if (r.contains(mx, my)) tip = {s->second->getName()};
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
                    if (r.contains(mx, my)) tip = {s.getName()};
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
    // forget screens that were closed (their pointers may be reused)
    auto* now = screen::current();
    for (auto it = ci.items.begin(); it != ci.items.end();) {
        if (it->first != &s && it->first != now) it = ci.items.erase(it);
        else ++it;
    }
    auto& map = ci.items[&s];
    map.clear();
    if (!items.is_object()) return;
    for (auto const& [k, v] : items.items()) {
        try {
            map[std::stoi(k)] = screen::iconOf(v);
        } catch (...) {}
    }
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
using Clock                   = std::chrono::steady_clock;
constexpr float kToastWidth   = 160;
constexpr float kToastHeight  = 32;
constexpr int   kSlideMs      = 600; // ToastInstance: SLIDE_ANIMATION_DURATION_MS
constexpr int   kMaxVisible   = 5;   // ToastComponent: 5 slots of 32 pixels

struct Shown {
    ToastSpec                  spec;
    std::shared_ptr<ItemStack> icon;
    std::string                title, body;
    Clock::time_point          start{};
    bool                       started = false;
};
struct Toasts {
    std::mutex        mutex;
    std::deque<Shown> queue; ///< the first kMaxVisible are on screen
    bool              logged = false;
};
Toasts& toasts() {
    static Toasts t;
    return t;
}

void drawBackground(screen::Canvas& c, float x, float y) {
    // a redraw of the advancement toast frame: dark body, light rim, darker inner border
    c.fill({x + 1, y, kToastWidth - 2, kToastHeight}, 0xFF1F1F1F);
    c.fill({x, y + 1, kToastWidth, kToastHeight - 2}, 0xFF1F1F1F);
    c.frame({x + 1, y + 1, kToastWidth - 2, kToastHeight - 2}, 0xFF6B6B6B);
    c.frame({x + 2, y + 2, kToastWidth - 4, kToastHeight - 4}, 0xFF0F0F0F);
}

void render(screen::Canvas& c, float guiWidth) {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    auto            now = Clock::now();
    int             slot = 0;
    for (auto it = t.queue.begin(); it != t.queue.end() && slot < kMaxVisible;) {
        auto& s = *it;
        if (!s.started) {
            s.started = true;
            s.start   = now;
            if (!s.spec.iconItem.empty()) {
                if (auto st = game::items::make(s.spec.iconItem)) s.icon = std::make_shared<ItemStack>(std::move(*st));
            }
            s.title = translateAny(s.spec.titleKey);
            s.body  = translateAny(s.spec.bodyKey);
        }
        auto  ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.start).count();
        int   show = std::max(0, s.spec.ms);
        if (ms >= kSlideMs + show + kSlideMs) {
            it = t.queue.erase(it);
            continue;
        }
        // ToastInstance.getVisibility: f = clamp(t / 600)², 1 - f while hiding
        float f;
        if (ms < kSlideMs) f = static_cast<float>(ms) / kSlideMs;
        else if (ms < kSlideMs + show) f = 1;
        else f = 1 - static_cast<float>(ms - kSlideMs - show) / kSlideMs;
        f      = std::clamp(f, 0.0f, 1.0f);
        f      = ms < kSlideMs + show ? f * f : 1 - (1 - f) * (1 - f);
        float x = guiWidth - kToastWidth * f;
        float y = static_cast<float>(slot) * kToastHeight;
        drawBackground(c, x, y);
        if (s.icon) c.item(*s.icon, x + 6, y + 8);
        c.text(x + 25, y + 7, s.title, 0xFFFFFF00, screen::Align::Left, false);
        c.text(x + 25, y + 18, s.body, 0xFFFFFFFF, screen::Align::Left, false);
        if (!t.logged) {
            t.logged = true;
            log().info("ui: toast drawn: {}", s.title);
        }
        ++slot;
        ++it;
    }
}
} // namespace

void show(ToastSpec const& spec) {
    auto&           t = toasts();
    std::lock_guard lock(t.mutex);
    t.queue.push_back(Shown{spec, nullptr, {}, {}, {}, false});
    t.logged = false;
}
} // namespace toast

namespace ui {
std::size_t toastCount() {
    auto&           t = toast::toasts();
    std::lock_guard lock(t.mutex);
    return t.queue.size();
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
    auto* c       = currentClient();
    float partial = c ? c->getFrameAlpha() : 0.0f;
    auto [gw, gh] = screen::guiSize();
    screen::Canvas canvas(ctx);
    for (auto& layer : list) {
        canvas.setOrigin(0, 0);
        try {
            layer.fn(canvas, partial);
        } catch (std::exception const& e) {
            log().error("ui: HUD layer {} failed: {}", layer.id, e.what());
        }
    }
    canvas.setOrigin(0, 0);
    toast::render(canvas, gw);
}

class UiModule {
public:
    // render listeners at mod load, before the game's threads run (see screen::ScreenModule)
    void load() {
        if (!mListeners.empty()) return;
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
            if (ev.screenView().getScreenName() != "hud_screen") return;
            try {
                renderHud(ev.uiRenderContext());
            } catch (...) {}
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            auto&           t = toast::toasts();
            std::lock_guard lock(t.mutex);
            t.queue.clear();
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

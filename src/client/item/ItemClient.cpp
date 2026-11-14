// Adapted from LiteLDev/Levitate v0.1.0 (src/client/assembly/TransitionalBar.cpp), LGPL-3.0-or-later.
//
// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// The effect rows translate Minecraft's Gui#renderEffects and EffectRenderingInventoryScreen as upstream
// relies on them for its effect (frosted) through
//   src/main/java/gloamingforest/potions/FrostedEffect.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The client mod's part of F9 (client build only):
// - the synced player data (traits.sync, effects.sync, stats.sync) behind ext/item/Client.h;
// - item bars (item::setBar): drawn over the inventory slot after the item batch, as Java's
//   isBarVisible / getBarWidth (round(13 * fraction)) / getBarColor, black underlay. The bar is
//   computed from the slot's real stack (the container model of the slot's collection, its item
//   data included), not the renderer's copy (built from #item_id_aux without data). The batch hook
//   runs at High priority, so the bars go on top of the GUI item renderers drawn after the same
//   batch (F12's client/render: Java draws the decorations last);
// - name colours of upstream's rarities (ext/item/Item.h, TFRarityEnumExtension's GLOAMING dark green,
//   with ItemStack#getRarity's bump for enchanted stacks): the hover text's formatting prefix;
// - the custom effects (upstream's frosted) on the HUD exactly where Java's Gui#renderEffects puts them
//   (24 px backgrounds from the right edge, 25 px apart, beneficial on the top row, the others
//   26 px lower, the icon blinking in the last 200 ticks), counting every active effect as Java
//   does, and the effect list next to the inventory as EffectRenderingInventoryScreen draws it
//   (120 x 32 panels with name and duration, 33 px apart, compact 32 x 32 icons when there is no
//   room). The vanilla carrier of a custom effect is hidden from Bedrock's own effect HUD
//   (HudMobEffectsRenderer) while it is the custom effect's instance.
// GUI textures are drawn as UI meshes ("ui_textured"): the UI image batch renders black on 1.26.51
// (spike S5).
#ifdef GF_CLIENT

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <numbers>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/UIPropertyBag.h"
#include "mc/client/gui/controls/renderers/HudMobEffectsRenderer.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core/utility/PropertyBag.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/json/Value.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TextureLoadState.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/models/ContainerModel.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/screen/base/Screen.h"
#include "feature/effects/EffectsInternal.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/ext/item/Client.h"
#include "feature/ext/item/Item.h"
#include "feature/item/ItemInternal.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::item {

namespace {

using Clock = std::chrono::steady_clock;

struct Synced {
    std::mutex                                mutex;
    std::map<std::string, bool>               traits;
    std::vector<effects::ext::client::Active> effects;
    Clock::time_point                         effectsAt{}; ///< when effects.sync arrived
    std::map<std::string, long long>          stats;
};

Synced& synced() {
    static Synced s;
    return s;
}

// ---- GUI textures (UI meshes, spike S5) ---------------------------------------------------------------

struct TextureCache {
    std::mutex                                        mutex;
    std::unordered_map<std::string, mce::TexturePtr*> textures; // kept for the process
    std::unordered_map<std::string, bool>             warned;
};
TextureCache& textureCache() {
    static auto* c = new TextureCache;
    return *c;
}

mce::TexturePtr* textureOf(MinecraftUIRenderContext& ui, std::string const& path) {
    auto&           cache = textureCache();
    std::lock_guard lock(cache.mutex);
    if (auto it = cache.textures.find(path); it != cache.textures.end()) return it->second;
    auto* t = new mce::TexturePtr(ui.getTexture(ResourceLocation(Core::PathView(path)), false));
    cache.textures.emplace(path, t);
    return t;
}

mce::MaterialPtr const* uiTextured() {
    static mce::MaterialPtr* material = [] {
        auto* m = new mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString("ui_textured"));
        if (!m->mRenderMaterialInfoPtr.get()) m = new mce::MaterialPtr(mce::RenderMaterialGroup::switchable(), HashedString("ui_textured"));
        return m;
    }();
    return material->mRenderMaterialInfoPtr.get() ? material : nullptr;
}

/// Java's GuiGraphics#blit of a whole texture into (x, y, w, h) GUI pixels, with alpha.
bool blit(MinecraftUIRenderContext& ctx, std::string const& texture, float x, float y, float w, float h, float alpha = 1.0f) {
    auto* t = textureOf(ctx, texture);
    if (!t) return false;
    auto const* data = t->mClientTexture.get();
    if (!data || data->mIsMissingTexture == IsMissingTexture::Yes) {
        auto&           cache = textureCache();
        std::lock_guard lock(cache.mutex);
        if (!cache.warned[texture]) {
            cache.warned[texture] = true;
            log().warn("item: texture {} is missing", texture);
        }
        return false;
    }
    if ((static_cast<unsigned>(data->mTextureLoadState) & static_cast<unsigned>(TextureLoadState::LoadedBit)) == 0) return false;
    auto const* material = uiTextured();
    if (!material) return false;
    ScreenContext& sc   = ctx.mScreenContext;
    Tessellator&   tess = sc.tessellator;
    float          x1 = x + w, y1 = y + h;
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 8, false);
    tess.color(1.f, 1.f, 1.f, alpha);
    tess.simpleQuad(Vec3{x, y1, 0}, Vec2{0, 1}, Vec3{x1, y1, 0}, Vec2{1, 1}, Vec3{x1, y, 0}, Vec2{1, 0}, Vec3{x, y, 0}, Vec2{0, 0});
    tess.simpleQuad(Vec3{x, y, 0}, Vec2{0, 0}, Vec3{x1, y, 0}, Vec2{1, 0}, Vec3{x1, y1, 0}, Vec2{1, 1}, Vec3{x, y1, 0}, Vec2{0, 1});
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> tex{*t};
    MeshHelpers::renderMeshImmediately(sc, tess, *material, tex, OffscreenCaptureDescription{});
    return true;
}

/// A key from the mod's translations, then the game's (resource pack texts), else the fallback.
std::string translate(std::string const& key, std::string const& fallback) {
    auto mod = translateClient(key, "\x01");
    if (mod != "\x01") return mod;
    try {
        auto& i18n = getI18n();
        auto  lang = i18n.getCurrentLanguage();
        auto  game = i18n.get(key, {}, lang.get());
        if (!game.empty() && game != key) return game;
    } catch (...) {
    }
    return fallback;
}

/// "fixture_frosted" -> "Fixture Frosted" (the fallback of a missing translation).
std::string titleCase(std::string const& path) {
    std::string out;
    bool        up = true;
    for (char c : path) {
        if (c == '_') {
            out += ' ';
            up = true;
        } else {
            out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            up = false;
        }
    }
    return out;
}

// ---- bars ----------------------------------------------------------------------------------------------

struct PendingBar {
    float    x, y, size, fraction;
    unsigned rgb;
};
std::vector<PendingBar>& pendingBars() {
    static std::vector<PendingBar> v;
    return v;
}

void drawBar(MinecraftUIRenderContext& ctx, PendingBar const& b) {
    // Java's GuiGraphics#renderItemDecorations: a 13 x 2 black box at (2, 13), the bar on its top row
    int   width = static_cast<int>(std::lround(13.f * b.fraction));
    float px    = b.size / 16.f;
    float r = static_cast<float>((b.rgb >> 16) & 0xFF) / 255.f, g = static_cast<float>((b.rgb >> 8) & 0xFF) / 255.f,
          bl = static_cast<float>(b.rgb & 0xFF) / 255.f;
    RectangleArea back{b.x + 2 * px, b.x + 15 * px, b.y + 13 * px, b.y + 15 * px};
    ctx.fillRectangle(back, mce::Color{0.f, 0.f, 0.f, 1.f}, 1.f);
    if (width > 0) {
        RectangleArea bar{b.x + 2 * px, b.x + static_cast<float>(2 + width) * px, b.y + 13 * px, b.y + 14 * px};
        ctx.fillRectangle(bar, mce::Color{r, g, bl, 1.f}, 1.f);
    }
}

/// The slot of an item renderer: the collection name and index of the JSON UI grid item it is in
/// (the renderer control or its nearest ancestors carry them in their property bags).
std::optional<std::pair<std::string, int>> collectionOf(UIControl& owner) {
    std::string                name;
    int                        index = -1;
    UIControl*                 c     = &owner;
    std::shared_ptr<UIControl> hold;
    for (int depth = 0; c && depth < 8 && (name.empty() || index < 0); ++depth) {
        if (auto const& bag = c->mPropertyBag; bag) {
            Json::Value const& v = bag->mJsonValue.get();
            if (v.isObject()) {
                for (auto const* k : {"#collection_index", "collection_index"})
                    if (index < 0 && v.isMember(k)) index = v[k].asInt(-1);
                for (auto const* k : {"#collection_name", "collection_name"})
                    if (name.empty() && v.isMember(k)) name = v[k].asString("");
            }
        }
        hold = c->mParent.lock();
        c    = hold.get();
    }
    if (name.empty() || index < 0) return std::nullopt;
    return std::pair{name, index};
}

/// The local player's real stack of a collection slot (with its user data).
std::optional<ItemStack> slotStack(std::string const& name, int index) {
    auto* lp = currentPlayer();
    if (!lp) return std::nullopt;
    if (auto const& mgr = lp->mContainerManager; mgr) {
        auto const& containers = mgr->mContainers.get();
        if (auto it = containers.find(name); it != containers.end() && it->second && index < it->second->getContainerSize())
            return it->second->getItemStack(index);
    }
    auto& inv = lp->getInventory();
    if (name == "hotbar_items" || name == "combined_hotbar_and_inventory_items") return inv.getItem(index);
    if (name == "inventory_items") return inv.getItem(9 + index);
    if (name == "armor_items" && index >= 0 && index < 4) return lp->getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(index));
    if (name == "offhand_items") return lp->getOffhandSlot();
    return std::nullopt;
}

// ---- effects (Java's Gui#renderEffects and EffectRenderingInventoryScreen) -------------------------------

struct Shown {
    std::string id;
    bool        custom    = false;
    int         amplifier = 0;
    int         duration  = 0; ///< remaining ticks, -1 infinite
    bool        ambient   = false;
    bool        beneficial = false;
    unsigned    color     = 0;
    std::string icon; ///< resource pack texture ("" : none)
    std::string name; ///< translated
};

unsigned rgbOf(mce::Color const& c) {
    auto ch = [](float v) { return static_cast<unsigned>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

/// Whether an effect instance of the local player is a custom effect's carrier.
bool isCarrier(MobEffectInstance const& inst, std::vector<effects::ext::client::Active> const& custom) {
    for (auto const& a : custom) {
        auto spec = effects::internal::specOf(a.id);
        if (!spec || spec->carrier.empty()) continue;
        auto vid = effects::internal::vanillaId(spec->carrier);
        if (vid && inst.mId == *vid && inst.mAmplifier == a.amplifier && inst.mEffectVisible == !spec->hideCarrierParticles) return true;
    }
    return false;
}

std::vector<effects::ext::client::Active> customNow() {
    auto&           s = synced();
    std::lock_guard lock(s.mutex);
    auto            elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s.effectsAt).count() / 50);
    std::vector<effects::ext::client::Active> out;
    for (auto a : s.effects) {
        a.ticks -= elapsed;
        if (a.ticks > 0) out.push_back(a);
    }
    return out;
}

/// The local player's effects as Java lists them (vanilla ones except the custom effects' carriers,
/// then the custom ones), sorted as MobEffectInstance#compareTo.
std::vector<Shown> shownEffects(LocalPlayer& lp) {
    std::vector<Shown> out;
    auto               custom = customNow();
    for (auto const& inst : lp.getAllEffects()) {
        if (inst.mId == 0 || isCarrier(inst, custom)) continue;
        auto        bedrockName = MobEffect::getNameById(inst.mId);
        auto const* effect      = MobEffect::getByName(bedrockName);
        if (!effect) continue;
        Shown s;
        s.id         = "minecraft:" + bedrockName;
        s.amplifier  = inst.mAmplifier;
        s.duration   = inst.mDuration->mValue;
        s.ambient    = inst.mAmbient;
        s.beneficial = !effect->mIsHarmful;
        s.color      = rgbOf(effect->mColor);
        std::string iconName = effect->mIconName;
        if (iconName.starts_with("textures/")) s.icon = iconName;
        s.name = translate(effect->mDescriptionId, MobEffect::getNameById(inst.mId));
        out.push_back(std::move(s));
    }
    for (auto const& a : custom) {
        auto spec = effects::internal::specOf(a.id);
        if (!spec) continue;
        Shown s;
        s.id         = a.id;
        s.custom     = true;
        s.amplifier  = a.amplifier;
        s.duration   = a.ticks;
        s.beneficial = spec->beneficial;
        s.color      = spec->color & 0xFFFFFF;
        s.icon       = effects::internal::iconOf(a.id);
        s.name       = translate(spec->nameKey, titleCase(a.id.substr(a.id.find(':') + 1)));
        out.push_back(std::move(s));
    }
    // MobEffectInstance#compareTo: non-ambient first, then finite first, then by duration (all
    // durations over 32147 ticks compare equal), then by colour; ambient ones by colour only
    auto key = [](Shown const& s) {
        if (s.ambient) return std::tuple{1, 0, 0, static_cast<int>(s.color)};
        int infinite = s.duration < 0 ? 1 : 0;
        return std::tuple{0, infinite, infinite ? 0 : std::min(s.duration, 32148), static_cast<int>(s.color)};
    };
    std::stable_sort(out.begin(), out.end(), [&](Shown const& a, Shown const& b) { return key(a) < key(b); });
    return out;
}

/// Java's effect_background sprites (a raised panel; the beacon's ambient one has a blue rim).
void panel(screen::Canvas& c, float x, float y, float w, float h, bool ambient) {
    c.fill({x + 1, y, w - 2, h}, 0xFF000000u);
    c.fill({x, y + 1, w, h - 2}, 0xFF000000u);
    c.bevel({x + 1, y + 1, w - 2, h - 2}, ambient ? 0xFF8DB3C6u : 0xFFC6C6C6u, 0xFFFFFFFFu, 0xFF555555u, false);
}

/// Java's MobEffectInstance blink in the last 200 ticks (Gui#renderEffects).
float blinkAlpha(int k) {
    if (k < 0 || k > 200) return 1.0f;
    int   l = 10 - k / 20;
    float f = std::clamp(static_cast<float>(k) / 10.0f / 5.0f * 0.5f, 0.0f, 0.5f)
            + std::cos(static_cast<float>(k) * std::numbers::pi_v<float> / 5.0f) * std::clamp(static_cast<float>(l) / 10.0f * 0.25f, 0.0f, 0.25f);
    return f;
}

void drawHudEffects(MinecraftUIRenderContext& ctx) {
    auto* lp = currentPlayer();
    if (!lp) return;
    auto list = shownEffects(*lp);
    if (std::none_of(list.begin(), list.end(), [](Shown const& s) { return s.custom; })) return;
    auto [gw, gh] = screen::guiSize();
    screen::Canvas canvas(ctx);
    int            beneficial = 0, other = 0;
    // Ordering.natural().reverse(): the longest first
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        float x = gw, y = 1;
        if (it->beneficial) x -= 25.0f * static_cast<float>(++beneficial);
        else {
            x -= 25.0f * static_cast<float>(++other);
            y += 26;
        }
        if (!it->custom) continue; // Bedrock draws the vanilla ones in its own effect HUD
        panel(canvas, x, y, 24, 24, it->ambient);
        float alpha = it->ambient ? 1.0f : blinkAlpha(it->duration);
        blit(ctx, it->icon, x + 3, y + 3, 18, 18, alpha);
    }
}

/// MobEffectUtil#formatDuration -> StringUtil#formatTickDuration (20 ticks a second): "%02d:%02d",
/// "%02d:%02d:%02d" from an hour ("03:00", "01:00:00"); effect.duration.infinite ("∞") for infinite.
std::string formatDuration(int ticks) {
    if (ticks < 0) return translate("effect.duration.infinite", "∞");
    int  seconds = ticks / 20;
    int  h = seconds / 3600, m = seconds / 60 % 60, sec = seconds % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", h, m, sec);
    else std::snprintf(buf, sizeof buf, "%02d:%02d", m, sec);
    return buf;
}

void drawInventoryEffects(MinecraftUIRenderContext& ctx) {
    auto* lp = currentPlayer();
    if (!lp) return;
    auto list = shownEffects(*lp);
    if (std::none_of(list.begin(), list.end(), [](Shown const& s) { return s.custom; })) return;
    auto [gw, gh] = screen::guiSize();
    // EffectRenderingInventoryScreen: right of the 176 x 166 inventory panel, which Bedrock's
    // inventory shows with the recipe book open (Java's RecipeBookComponent#updateScreenPosition:
    // leftPos = 177 + (width - 176 - 200) / 2)
    float left = gw >= 379 ? std::floor(177 + (gw - 176 - 200) / 2) : std::floor((gw - 176) / 2);
    float x    = left + 176 + 2;
    float w = gw - x;
    if (w < 32) return;
    bool  wide = w >= 120;
    float step = list.size() > 5 ? std::floor(132.0f / static_cast<float>(list.size() - 1)) : 33.0f;
    float y    = std::floor((gh - 166) / 2);
    screen::Canvas canvas(ctx);
    for (auto const& e : list) {
        panel(canvas, x, y, wide ? 120 : 32, 32, e.ambient);
        if (!e.icon.empty()) blit(ctx, e.icon, x + (wide ? 6 : 7), y + 7, 18, 18);
        if (wide) {
            auto name = e.name;
            if (e.amplifier >= 1 && e.amplifier <= 9)
                name += " " + translate("enchantment.level." + std::to_string(e.amplifier + 1), enchant::internal::levelName(e.amplifier + 1));
            canvas.text(x + 28, y + 6, name, 0xFFFFFFFFu);
            canvas.text(x + 28, y + 16, formatDuration(e.duration), 0xFF7F7F7Fu);
        }
        y += step;
    }
}

} // namespace

} // namespace gf::client::item

// ---- the synced data (ext/item/Client.h) -------------------------------------------------------------

namespace gf::traits::ext::client {
bool get(std::string const& traitId) {
    auto&           s = gf::client::item::synced();
    std::lock_guard lock(s.mutex);
    auto            it = s.traits.find(traitId);
    return it != s.traits.end() && it->second;
}
} // namespace gf::traits::ext::client

namespace gf::effects::ext::client {
std::vector<Active> active() { return gf::client::item::customNow(); }
int                 amplifier(std::string const& effectId) {
    for (auto const& a : active())
        if (a.id == effectId) return a.amplifier;
    return -1;
}
} // namespace gf::effects::ext::client

namespace gf::stats::ext::client {
std::map<std::string, long long> values() {
    auto&           s = gf::client::item::synced();
    std::lock_guard lock(s.mutex);
    return s.stats;
}
} // namespace gf::stats::ext::client

// ---- hooks -----------------------------------------------------------------------------------------------

namespace ci = gf::client::item;

LL_TYPE_INSTANCE_HOOK(
    ItemBarRenderHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::$render,
    void,
    MinecraftUIRenderContext& ctx,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    origin(ctx, client, owner, pass);
    try {
        ItemStack const& shown = *mItemInstance;
        if (shown.isNull() || !shown.mItem) return;
        if (!gf::item::internal::hasBar(shown.mItem.get())) return;
        // the slot's real stack (the renderer's copy has no item data)
        std::optional<ItemStack> real;
        if (auto slot = ci::collectionOf(owner))
            if (auto s = ci::slotStack(slot->first, slot->second); s && !s->isNull() && s->mItem && s->mItem.get() == shown.mItem.get()) real = std::move(s);
        auto bar = gf::item::internal::bar(real ? *real : shown);
        if (!bar) return;
        glm::vec2 pos  = *owner.mCachedPosition;
        glm::vec2 size = *owner.mSize;
        // items are drawn in a shared mesh batch: the bar goes on top once the batch is done
        ci::pendingBars().push_back({pos.x, pos.y, std::min(size.x, size.y), bar->fraction, bar->rgb});
    } catch (...) {
    }
}

LL_TYPE_INSTANCE_HOOK(
    ItemBarBatchHook,
    ll::memory::HookPriority::High,
    MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$endSharedMeshBatch,
    void,
    ::ComponentRenderBatch& renderBatch
) {
    origin(renderBatch); // the item batch and the GUI item renderers hooked after it (lower priority)
    if (ci::pendingBars().empty()) return;
    auto bars = std::move(ci::pendingBars());
    ci::pendingBars().clear();
    try {
        for (auto const& b : bars) ci::drawBar(*this, b);
    } catch (...) {
    }
}

// The name colour of an item with a rarity (upstream's GLOAMING: dark green). As Java's
// ItemStack#getDisplayName / getTooltipLines, a custom name keeps the rarity's colour and is italic.
LL_TYPE_INSTANCE_HOOK(
    ItemRarityPrefixHook,
    ll::memory::HookPriority::Normal,
    ItemStackBase,
    &ItemStackBase::_getHoverFormattingPrefix,
    std::string
) {
    auto prefix = origin();
    try {
        if (isNull() || !mItem) return prefix;
        if (gf::item::ext::rarityOf(mItem->mFullName->getString()) == gf::item::ext::Rarity::Common) return prefix;
        std::string out = gf::item::ext::rarityColor(gf::item::ext::rarityOf(static_cast<ItemStackBase const&>(*this)));
        if (!getCustomName().empty()) out += "\xC2\xA7" "o";
        return out;
    } catch (...) {
        return prefix;
    }
}

// Bedrock's own effect HUD: a custom effect's carrier is not shown (the custom effect's icon is,
// in Java's place); the instance is swapped out for the draw only.
LL_TYPE_INSTANCE_HOOK(
    ItemEffectHudHook,
    ll::memory::HookPriority::Normal,
    HudMobEffectsRenderer,
    &HudMobEffectsRenderer::$render,
    void,
    MinecraftUIRenderContext& renderContext,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    std::vector<std::pair<std::size_t, MobEffectInstance>> hidden;
    std::vector<MobEffectInstance>*                        list = nullptr;
    try {
        auto custom = ci::customNow();
        if (auto* lp = gf::client::currentPlayer(); lp && !custom.empty()) {
            list = &lp->_getAllEffectsNonConst();
            for (std::size_t i = 0; i < list->size(); ++i) {
                auto& inst = (*list)[i];
                if (inst.mId == 0 || !ci::isCarrier(inst, custom)) continue;
                hidden.emplace_back(i, inst);
                inst = MobEffectInstance::NO_EFFECT();
            }
        }
    } catch (...) {
    }
    origin(renderContext, client, owner, pass);
    if (list)
        for (auto& [i, inst] : hidden)
            if (i < list->size()) (*list)[i] = inst;
}

namespace gf::client::item {
namespace {

void onSync(std::string const& channel, nlohmann::json const& msg) {
    auto&           s = synced();
    std::lock_guard lock(s.mutex);
    if (channel == "traits.sync") {
        s.traits.clear();
        if (auto it = msg.find("traits"); it != msg.end() && it->is_object())
            for (auto& [k, v] : it->items())
                if (v.is_boolean()) s.traits[k] = v.get<bool>();
    } else if (channel == "effects.sync") {
        s.effects.clear();
        s.effectsAt = Clock::now();
        if (auto it = msg.find("effects"); it != msg.end() && it->is_array())
            for (auto const& e : *it)
                if (e.is_object()) s.effects.push_back({e.value("id", std::string()), e.value("amplifier", 0), e.value("ticks", 0)});
    } else if (channel == "stats.sync") {
        s.stats.clear();
        if (auto it = msg.find("stats"); it != msg.end() && it->is_object())
            for (auto& [k, v] : it->items())
                if (v.is_number_integer()) s.stats[k] = v.get<long long>();
    }
}

class ItemClientModule {
public:
    // channels and render listeners at mod load, before the game's threads run
    void load() {
        if (mLoaded) return;
        mLoaded = true;
        for (auto const* ch : {"traits.sync", "effects.sync", "stats.sync"}) {
            std::string channel = ch;
            net::client::on(channel, [channel](nlohmann::json const& msg) { onSync(channel, msg); });
        }
        net::client::onConnected([] {
            auto&           s = synced();
            std::lock_guard lock(s.mutex);
            s.traits.clear();
            s.effects.clear();
            s.stats.clear();
        });
        mRender = ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
            try {
                auto name = ev.screenView().getScreenName();
                auto* c   = currentClient();
                auto  top = c ? c->getScreenName() : std::string();
                // Gui#renderEffects: the HUD row unless an inventory screen shows the list
                if (name == "hud_screen" && !top.starts_with("inventory_screen")) drawHudEffects(ev.uiRenderContext());
                else if (name.starts_with("inventory_screen")) drawInventoryEffects(ev.uiRenderContext());
            } catch (...) {
            }
        });
    }
    void install() {
        load();
        ItemBarRenderHook::hook();
        ItemBarBatchHook::hook();
        ItemRarityPrefixHook::hook();
        ItemEffectHudHook::hook();
    }
    void uninstall() {
        ItemEffectHudHook::unhook();
        ItemRarityPrefixHook::unhook();
        ItemBarBatchHook::unhook();
        ItemBarRenderHook::unhook();
    }

private:
    bool                   mLoaded = false;
    ll::event::ListenerPtr mRender;
};

} // namespace
} // namespace gf::client::item

GF_REGISTER_MODULE(gf::client::item::ItemClientModule);

#endif // GF_CLIENT

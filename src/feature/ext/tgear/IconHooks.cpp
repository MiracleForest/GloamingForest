// GloamingForest - U11's GUI icon layers (IconHooks.h): ItemRenderer::renderGuiItemInChunk.
#ifdef GF_CLIENT

#include "feature/ext/tgear/IconHooks.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/geometry_atlas/ItemHandleData.h"
#include "mc/client/gui/geometry_atlas/ItemHandleImpl.h"
#include "mc/client/gui/screens/ItemRendererData.h"
#include "mc/client/gui/screens/ItemRendererDataHelpers.h"
#include "mc/client/gui/screens/UIItemRenderInfo.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/world/item/ItemIconManager.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ResolvedItemIconInfo.h"
#include "mc/world/item/components/ComponentItem.h"

#include "mod/Log.h"

namespace gf::ext::tgear::client {

namespace {

using Registry = std::unordered_map<std::string, IconFn>;

std::mutex                       gMutex;
std::shared_ptr<Registry const>  gRegistry = std::make_shared<Registry const>();
std::atomic<std::uint64_t>       gLayered{0};
std::atomic<std::uint64_t>       gHandles{0};
std::atomic<std::uint64_t>       gInfos{0};
std::atomic<bool>                gGuiSeen{false}, gInfoSeen{false}, gHandleSeen{false}; ///< first call of each hook (diagnostics)
thread_local int                 gDepth = 0; ///< inside our own origin calls

std::shared_ptr<Registry const> registry() {
    std::lock_guard lock(gMutex);
    return gRegistry;
}

/// The item atlas rectangle of `key` into `out` (false, `out` empty, when the atlas has no such entry: a
/// zero-size rectangle). TextureUVCoordinateSet has no copy constructor of its own: default-constructed,
/// then assigned.
bool atlasUv(std::string const& key, std::optional<TextureUVCoordinateSet>& out) {
    out.reset();
    try {
        TextureUVCoordinateSet const& uv = ItemIconManager::getTextureUVCoordinateSet(key, 0);
        if (uv._u0 == uv._u1 || uv._v0 == uv._v1) return false;
        out.emplace();
        *out = uv;
        return true;
    } catch (...) {
        out.reset();
        return false;
    }
}

std::optional<IconLayers> layersOf(ItemStack const& item) {
    try {
        auto reg = registry();
        if (auto it = reg->find(item.getTypeName()); it != reg->end() && it->second) return it->second(item);
    } catch (...) {}
    return std::nullopt;
}

/// A key of the layers (the geometry atlas caches a slot icon per item handle hash).
std::uint64_t layersHash(IconLayers const& l) {
    std::uint64_t h = std::hash<std::string>{}(l.base);
    for (auto const& o : l.overlays) h = h * 1099511628211ull ^ std::hash<std::string>{}(o);
    return h;
}

void warnOnce(std::string const& key) {
    static std::mutex                      m;
    static std::unordered_map<std::string, bool> seen;
    std::lock_guard                         lock(m);
    if (seen.emplace(key, true).second) gf::log().warn("tgear: no item atlas entry '{}' (icon layer skipped)", key);
}

/// One line per (pass, upstream item id), at most 24 in all: which GUI pass draws which upstream item and whether it has layers
/// (diagnostics of the icon passes on this game version).
void tracePass(char const* pass, ItemStackBase const& item, bool layered) {
    static std::atomic<int> left{40};
    if (left.load(std::memory_order_relaxed) <= 0 || item.isNull()) return;
    std::string id = item.getTypeName();
    if (id.rfind("gloamingforest:", 0) != 0) return;
    static std::mutex                      m;
    static std::unordered_map<std::string, bool> seen;
    {
        std::lock_guard lock(m);
        if (!seen.emplace(std::string(pass) + "|" + id + (layered ? "|1" : "|0"), true).second) return;
    }
    if (left.fetch_sub(1) <= 0) return;
    gf::log().info("tgear: {} draws {} ({})", pass, id, layered ? "layered" : "no layers");
}

} // namespace

void setIconLayers(std::string const& itemId, IconFn fn) {
    std::lock_guard lock(gMutex);
    auto            next = std::make_shared<Registry>(*gRegistry);
    (*next)[itemId]      = std::move(fn);
    gRegistry            = std::move(next);
}

std::uint64_t layeredIcons() { return gLayered.load(); }

} // namespace gf::ext::tgear::client

namespace {

namespace tc = gf::ext::tgear::client;

LL_TYPE_INSTANCE_HOOK(
    TgearGuiIconHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::renderGuiItemInChunk,
    void,
    ::BaseActorRenderContext&                        renderContext,
    ::ItemRenderChunkType                            itemRenderChunkType,
    ::ItemStack const&                               item,
    float                                            x,
    float                                            y,
    float                                            lightMultiplier,
    float                                            alphaMultiplier,
    float                                            scale,
    int                                              frame,
    bool                                             allowAnimation,
    int                                              zOrder,
    ::std::optional<::TextureUVCoordinateSet> const& customIconUVCoords
) {
    auto call = [&](std::optional<TextureUVCoordinateSet> const& uv) {
        origin(renderContext, itemRenderChunkType, item, x, y, lightMultiplier, alphaMultiplier, scale, frame, allowAnimation, zOrder, uv);
    };
    if (!tc::gGuiSeen.load(std::memory_order_relaxed) && !tc::gGuiSeen.exchange(true)) gf::log().info("tgear: GUI icon pass reached (renderGuiItemInChunk, first item {})", item.isNull() ? std::string("none") : item.getTypeName());
    if (tc::gDepth > 0 || item.isNull()) return call(customIconUVCoords);
    auto layers = tc::layersOf(item);
    tc::tracePass("renderGuiItemInChunk", item, layers.has_value());
    if (!layers) return call(customIconUVCoords);
    ++tc::gDepth;
    try {
        // the base: our atlas entry, else whatever icon the caller resolved (its own custom UVs)
        std::optional<TextureUVCoordinateSet> base;
        if (!layers->base.empty() && !tc::atlasUv(layers->base, base)) tc::warnOnce(layers->base);
        if (base) call(base);
        else call(customIconUVCoords);
        // the overlays in the same batch: drawn after the base, so on top of it
        std::optional<TextureUVCoordinateSet> uv;
        for (auto const& key : layers->overlays) {
            if (!tc::atlasUv(key, uv)) {
                tc::warnOnce(key);
                continue;
            }
            origin(renderContext, itemRenderChunkType, item, x, y, lightMultiplier, alphaMultiplier, scale, frame, false, zOrder, uv);
        }
        if (tc::gLayered.fetch_add(1) < 3)
            gf::log().info("tgear: icon of {} drawn with {} overlay layer(s){}", item.getTypeName(), layers->overlays.size(), layers->base.empty() ? "" : " on " + layers->base);
    } catch (...) {}
    --tc::gDepth;
}

// The base icon wherever the game resolves an item's icon (slots, hand, ground: Java's item model applies
// everywhere): ComponentItem::getIconInfo with the layers' base entry.
LL_TYPE_INSTANCE_HOOK(
    TgearIconInfoHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$getIconInfo,
    ::ResolvedItemIconInfo,
    ::ItemStackBase const& item,
    int                    newAnimationFrame,
    bool                   inInventoryPane
) {
    auto info = origin(item, newAnimationFrame, inInventoryPane);
    try {
        if (!tc::gInfoSeen.load(std::memory_order_relaxed) && !tc::gInfoSeen.exchange(true)) gf::log().info("tgear: icon info pass reached (ComponentItem::getIconInfo, first item {})", item.isNull() ? std::string("none") : item.getTypeName());
        if (item.isNull()) return info;
        auto layers = tc::layersOf(static_cast<ItemStack const&>(item));
        tc::tracePass("getIconInfo", item, layers.has_value());
        if (!layers || layers->base.empty()) return info;
        info.mIconName.get() = layers->base;
        info.mIconFrame      = 0;
        if (tc::gInfos.fetch_add(1) == 0) gf::log().info("tgear: icon of {} resolved to {}", item.getTypeName(), layers->base);
    } catch (...) {}
    return info;
}

// The UI's geometry atlas draws a slot icon once per item handle and caches the tile by the handle's
// hash, which knows the item, not its layers: the layers' key is mixed into the hash, so a broken vest
// and a plain one, or two different sets of modifiers, get tiles of their own.
LL_TYPE_INSTANCE_HOOK(
    TgearItemHandleHook,
    ll::memory::HookPriority::Normal,
    GeometryAtlas::ItemHandleImpl,
    &GeometryAtlas::ItemHandleImpl::$ctor,
    void*,
    ::GeometryAtlas::ItemHandleData&& data
) {
    void* self = origin(std::move(data));
    try {
        if (!tc::gHandleSeen.load(std::memory_order_relaxed) && !tc::gHandleSeen.exchange(true)) gf::log().info("tgear: icon tile pass reached (GeometryAtlas::ItemHandleImpl)");
        std::optional<tc::IconLayers> layers;
        auto visit = [&](InventoryItemRenderer& r) {
            ItemStack const& stack = r.mItemInstance;
            if (!stack.isNull()) layers = tc::layersOf(stack);
        };
        this->$visitItem(visit);
        if (layers) {
            auto& d  = mData.get();
            d.mHash  = d.mHash * 1099511628211ull ^ tc::layersHash(*layers);
            if (tc::gHandles.fetch_add(1) == 0) gf::log().info("tgear: icon tile of a layered item keyed by its layers");
        }
    } catch (...) {}
    return self;
}

// Diagnostics: which GUI paths see the upstream items of the slots (the hotbar and the inventory slots reach none of the
// hooks above on 1.26.51): InventoryItemRenderer::getItemRenderInfo and ItemRendererDataHelpers::setItemRendererBindings.
LL_TYPE_STATIC_HOOK(
    TgearRenderInfoProbeHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::getItemRenderInfo,
    ::UIItemRenderInfo,
    ::ItemStack const& item
) {
    try {
        tc::tracePass("getItemRenderInfo", item, tc::layersOf(item).has_value());
    } catch (...) {}
    return origin(item);
}

LL_STATIC_HOOK(
    TgearBindingsProbeHook,
    ll::memory::HookPriority::Normal,
    &ItemRendererDataHelpers::setItemRendererBindings,
    void,
    ::ItemRendererData&    itemData,
    ::ItemStackBase const& item
) {
    try {
        tc::tracePass("setItemRendererBindings", item, tc::layersOf(static_cast<ItemStack const&>(item)).has_value());
    } catch (...) {}
    origin(itemData, item);
}

// Diagnostics: which upstream items the GUI's other item path draws (ItemRenderer::renderGuiItemNew).
LL_TYPE_INSTANCE_HOOK(
    TgearGuiNewProbeHook,
    ll::memory::HookPriority::Normal,
    ItemRenderer,
    &ItemRenderer::renderGuiItemNew,
    void,
    ::BaseActorRenderContext& renderContext,
    ::ItemStack const&        item,
    int                       frame,
    float                     x,
    float                     y,
    bool                      renderEnchantmentFoil,
    float                     transparency,
    float                     lightMultiplier,
    float                     scale,
    int                       zOrder
) {
    try {
        tc::tracePass("renderGuiItemNew", item, tc::layersOf(item).has_value());
    } catch (...) {}
    origin(renderContext, item, frame, x, y, renderEnchantmentFoil, transparency, lightMultiplier, scale, zOrder);
}

} // namespace

namespace gf::ext::tgear::client {

void installIconHooks() {
    int gui = TgearGuiIconHook::hook(), handle = TgearItemHandleHook::hook(), info = TgearIconInfoHook::hook();
    TgearGuiNewProbeHook::hook();
    TgearRenderInfoProbeHook::hook();
    TgearBindingsProbeHook::hook();
    static std::atomic<bool> told{false};
    if (!told.exchange(true)) gf::log().info("tgear: icon hooks installed (gui {}, tile {}, info {})", gui, handle, info);
}
void uninstallIconHooks() {
    TgearGuiNewProbeHook::unhook();
    TgearRenderInfoProbeHook::unhook();
    TgearBindingsProbeHook::unhook();
    TgearIconInfoHook::unhook();
    TgearItemHandleHook::unhook();
    TgearGuiIconHook::unhook();
}

} // namespace gf::ext::tgear::client

#endif // GF_CLIENT

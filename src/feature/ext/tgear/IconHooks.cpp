// GloamingForest - U11's GUI icon layers (IconHooks.h): ItemRenderer::renderGuiItemInChunk.
#ifdef GF_CLIENT

#include "feature/ext/tgear/IconHooks.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/geometry_atlas/ItemHandleData.h"
#include "mc/client/gui/geometry_atlas/ItemHandleImpl.h"
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
    if (tc::gDepth > 0 || item.isNull()) return call(customIconUVCoords);
    auto layers = tc::layersOf(item);
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
        if (item.isNull()) return info;
        auto layers = tc::layersOf(static_cast<ItemStack const&>(item));
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

} // namespace

namespace gf::ext::tgear::client {

void installIconHooks() {
    TgearGuiIconHook::hook();
    TgearItemHandleHook::hook();
    TgearIconInfoHook::hook();
}
void uninstallIconHooks() {
    TgearIconInfoHook::unhook();
    TgearItemHandleHook::unhook();
    TgearGuiIconHook::unhook();
}

} // namespace gf::ext::tgear::client

#endif // GF_CLIENT

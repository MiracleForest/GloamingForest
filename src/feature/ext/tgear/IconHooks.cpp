// GloamingForest - U11's GUI icon layers (IconHooks.h): ItemRenderer::renderGuiItemInChunk.
#ifdef GF_CLIENT

#include "feature/ext/tgear/IconHooks.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/world/item/ItemIconManager.h"
#include "mc/world/item/ItemStack.h"

#include "mod/Log.h"

namespace gf::ext::tgear::client {

namespace {

using Registry = std::unordered_map<std::string, IconFn>;

std::mutex                       gMutex;
std::shared_ptr<Registry const>  gRegistry = std::make_shared<Registry const>();
std::atomic<std::uint64_t>       gLayered{0};
thread_local int                 gDepth = 0; ///< inside our own origin calls

std::shared_ptr<Registry const> registry() {
    std::lock_guard lock(gMutex);
    return gRegistry;
}

/// The item atlas rectangle of `key` (nullopt when the atlas has no such entry: a zero-size rectangle).
std::optional<TextureUVCoordinateSet> atlasUv(std::string const& key) {
    try {
        TextureUVCoordinateSet const& uv = ItemIconManager::getTextureUVCoordinateSet(key, 0);
        if (uv._u0 == uv._u1 || uv._v0 == uv._v1) return std::nullopt;
        return uv;
    } catch (...) {
        return std::nullopt;
    }
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
    if (tc::gDepth > 0 || customIconUVCoords.has_value() || item.isNull()) return call(customIconUVCoords);
    std::optional<tc::IconLayers> layers;
    try {
        auto reg = tc::registry();
        if (auto it = reg->find(item.getTypeName()); it != reg->end() && it->second) layers = it->second(item);
    } catch (...) {}
    if (!layers) return call(customIconUVCoords);
    ++tc::gDepth;
    try {
        std::optional<TextureUVCoordinateSet> base;
        if (!layers->base.empty()) {
            base = tc::atlasUv(layers->base);
            if (!base) tc::warnOnce(layers->base);
        }
        call(base);
        // the overlays in the same batch: drawn after the base, so on top of it
        for (auto const& key : layers->overlays) {
            auto uv = tc::atlasUv(key);
            if (!uv) {
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

} // namespace

namespace gf::ext::tgear::client {

void installIconHooks() { TgearGuiIconHook::hook(); }
void uninstallIconHooks() { TgearGuiIconHook::unhook(); }

} // namespace gf::ext::tgear::client

#endif // GF_CLIENT

// A textured, translucent quad on the HUD canvas (contract extension of "client.ui" by F4, see
// HudSprite.h). Client build only.

#ifdef GF_CLIENT

#include "feature/ext/dimension/HudSprite.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/MinecraftGame.h"
#include "mc/client/gui/screens/ScreenContext.h"
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
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TextureLoadState.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/ShaderColor.h"

#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::hud::ext {

namespace {

using Clock = std::chrono::steady_clock;

/// A texture as the UI loaded it. Only a loaded texture stays; one that is still loading or
/// reported missing is asked for again after a back-off (0.5 s, doubling up to 8 s).
struct Entry {
    mce::TexturePtr* texture  = nullptr; ///< owned (freed on the draw thread)
    bool             loaded   = false;
    bool             missing  = false; ///< the last request reported the missing texture
    int              attempts = 0;
    Clock::time_point requestedAt{};
};

struct Cache {
    std::mutex                             mutex;
    std::unordered_map<std::string, Entry> textures;
    std::unordered_map<std::string, bool>  warned;
    mce::MaterialPtr*                      material       = nullptr;
    bool                                   materialLooked = false;
    /// Bumped when the textures the cache holds may be gone (the level was left, the resource
    /// packs changed); the draw thread drops them on its next frame.
    std::atomic<int> generation{0};
    int              seenGeneration = 0;
};
Cache& cache() {
    static auto* c = new Cache; // never destroyed: the renderer is gone at process exit
    return *c;
}

void invalidate() { cache().generation.fetch_add(1, std::memory_order_relaxed); }

/// On the draw thread, with the lock held: everything the cache held goes.
void dropAll(Cache& c) {
    for (auto& [path, e] : c.textures) delete e.texture;
    c.textures.clear();
    delete c.material;
    c.material       = nullptr;
    c.materialLooked = false;
}

std::chrono::milliseconds backoff(int attempts) {
    return std::chrono::milliseconds(500 << std::clamp(attempts - 1, 0, 4));
}

BedrockTextureData const* readyData(mce::TexturePtr const& t) {
    auto const* data = t.mClientTexture.get();
    if (!data || data->mIsMissingTexture == IsMissingTexture::Yes) return nullptr;
    bool loaded = (static_cast<unsigned>(data->mTextureLoadState) & static_cast<unsigned>(TextureLoadState::LoadedBit)) != 0;
    return loaded ? data : nullptr;
}

/// The texture's data once the UI has loaded it (the UI render context of this frame owns the
/// textures image controls draw with); nullptr while it loads or when it is missing.
BedrockTextureData const* textureOf(std::string const& path, MinecraftUIRenderContext& ui, mce::TexturePtr** out) {
    auto&           c = cache();
    std::lock_guard lock(c.mutex);
    if (int g = c.generation.load(std::memory_order_relaxed); g != c.seenGeneration) {
        dropAll(c);
        c.seenGeneration = g;
    }
    auto& e   = c.textures[path];
    auto  now = Clock::now();
    if (e.texture) {
        if (auto const* d = readyData(*e.texture)) {
            e.loaded   = true;
            e.attempts = 0;
            *out       = e.texture;
            return d;
        }
        // a texture that was loaded and is not any more (unloaded by the renderer) is asked for
        // at once; one that never loaded waits for its back-off
        if (!e.loaded && now - e.requestedAt < backoff(e.attempts)) return nullptr;
        delete e.texture;
        e.texture = nullptr;
        e.loaded  = false;
    }
    e.texture     = new mce::TexturePtr(ui.getTexture(ResourceLocation(Core::PathView(path)), e.missing));
    e.requestedAt = now;
    ++e.attempts;
    if (auto const* d = readyData(*e.texture)) {
        e.loaded   = true;
        e.attempts = 0;
        e.missing  = false;
        *out       = e.texture;
        return d;
    }
    auto const* data = e.texture->mClientTexture.get();
    e.missing        = data && data->mIsMissingTexture == IsMissingTexture::Yes;
    if (e.missing && !c.warned[path]) {
        c.warned[path] = true;
        log().warn("hud sprite: texture {} is missing (asked for again later)", path);
    }
    return nullptr;
}

/// The UI material that multiplies the texture by the shader colour (glcolor).
mce::MaterialPtr const* material() {
    auto&           c = cache();
    std::lock_guard lock(c.mutex);
    if (!c.materialLooked) {
        c.materialLooked = true;
        for (char const* name : {"ui_textured_and_glcolor", "ui_textured"}) {
            for (auto* group : {&mce::RenderMaterialGroup::common(), &mce::RenderMaterialGroup::switchable()}) {
                auto* m = new mce::MaterialPtr(*group, HashedString(name));
                if (m->mRenderMaterialInfoPtr.get()) {
                    c.material = m;
                    log().info("hud sprite: drawing with the UI material {}", name);
                    break;
                }
                delete m;
            }
            if (c.material) break;
        }
        if (!c.material) log().warn("hud sprite: no UI material to draw textures with");
    }
    return c.material;
}

} // namespace

int spriteFrames(screen::Canvas& canvas, std::string const& texture) {
    mce::TexturePtr* t    = nullptr;
    auto const*      data = textureOf(texture, canvas.context(), &t);
    if (!data) return 0;
    auto const& d = data->mTextureDescription.get();
    if (d.mWidth == 0 || d.mHeight < d.mWidth) return d.mWidth == 0 ? 0 : 1;
    return static_cast<int>(d.mHeight / d.mWidth);
}

bool blitSprite(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float alpha, int frame) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f) return true;
    mce::TexturePtr* t    = nullptr;
    auto const*      data = textureOf(texture, canvas.context(), &t);
    if (!data || !t) return false;
    auto const& desc   = data->mTextureDescription.get();
    int         frames = desc.mWidth == 0 ? 0 : (desc.mHeight < desc.mWidth ? 1 : static_cast<int>(desc.mHeight / desc.mWidth));
    auto const* m      = material();
    if (frames <= 0 || !m) return false;
    frame          = ((frame % frames) + frames) % frames;
    float x0       = canvas.originX() + dst.x, y0 = canvas.originY() + dst.y, x1 = x0 + dst.w, y1 = y0 + dst.h;
    float v0       = static_cast<float>(frame) / static_cast<float>(frames);
    float v1       = static_cast<float>(frame + 1) / static_cast<float>(frames);
    auto& ctx      = canvas.context();
    ScreenContext& sc   = ctx.mScreenContext;
    Tessellator&   tess = sc.tessellator;
    // the alpha: the shader colour (glcolor materials) and the vertex colour
    ShaderColor& shader = sc.currentShaderColor;
    mce::Color   keep   = shader.color;
    shader.color        = mce::Color{1.0f, 1.0f, 1.0f, alpha};
    shader.dirty        = true;
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 4, false);
    tess.color(1.0f, 1.0f, 1.0f, alpha);
    // one quad, in the order the game's own screen blits use (bottom left, bottom right, top
    // right, top left: ScreenRenderer#blit). UI materials draw both faces, so a second quad of the
    // other winding would blend the sprite twice (alpha 1 - (1 - a)^2 instead of a)
    tess.simpleQuad(Vec3{x0, y1, 0}, Vec2{0, v1}, Vec3{x1, y1, 0}, Vec2{1, v1}, Vec3{x1, y0, 0}, Vec2{1, v0}, Vec3{x0, y0, 0}, Vec2{0, v0});
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> tex{*t};
    MeshHelpers::renderMeshImmediately(sc, tess, *m, tex, OffscreenCaptureDescription{});
    shader.color = keep;
    shader.dirty = true;
    return true;
}

// The resource packs changed: every texture and material may have been reloaded.
LL_TYPE_INSTANCE_HOOK(
    HudSpriteResourcePacksHook,
    ll::memory::HookPriority::Normal,
    MinecraftGame,
    &MinecraftGame::$onActiveResourcePacksChanged,
    void,
    ResourcePackManager& mgr
) {
    origin(mgr);
    invalidate();
}

class HudSpriteModule {
public:
    void install() {
        HudSpriteResourcePacksHook::hook();
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { invalidate(); });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mExit);
        HudSpriteResourcePacksHook::unhook();
    }

private:
    ll::event::ListenerPtr mExit;
};

} // namespace gf::client::hud::ext

GF_REGISTER_MODULE(gf::client::hud::ext::HudSpriteModule);

#endif // GF_CLIENT

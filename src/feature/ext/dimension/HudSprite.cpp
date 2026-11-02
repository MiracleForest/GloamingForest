// A textured, translucent quad on the HUD canvas (contract extension of "client.ui" by F4, see
// HudSprite.h). Client build only.

#ifdef GF_CLIENT

#include "feature/ext/dimension/HudSprite.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <variant>

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

namespace gf::client::hud::ext {

namespace {

struct Cache {
    std::mutex                                        mutex;
    std::unordered_map<std::string, mce::TexturePtr*> textures; ///< kept for the process (never destroyed at exit)
    std::unordered_map<std::string, bool>             warned;
    mce::MaterialPtr*                                 material = nullptr;
    bool                                              materialLooked = false;
};
Cache& cache() {
    static auto* c = new Cache;
    return *c;
}

/// The texture as the UI loads it (the UI render context of this frame owns the textures image
/// controls draw with).
mce::TexturePtr* textureOf(std::string const& path, MinecraftUIRenderContext& ui) {
    auto&           c = cache();
    std::lock_guard lock(c.mutex);
    if (auto it = c.textures.find(path); it != c.textures.end()) return it->second;
    auto* t = new mce::TexturePtr(ui.getTexture(ResourceLocation(Core::PathView(path)), false));
    c.textures.emplace(path, t);
    return t;
}

BedrockTextureData const* readyData(mce::TexturePtr const& t, std::string const& path) {
    auto const* data = t.mClientTexture.get();
    if (data && data->mIsMissingTexture != IsMissingTexture::Yes) {
        bool loaded = (static_cast<unsigned>(data->mTextureLoadState) & static_cast<unsigned>(TextureLoadState::LoadedBit)) != 0;
        return loaded ? data : nullptr;
    }
    auto&           c = cache();
    std::lock_guard lock(c.mutex);
    if (!c.warned[path]) {
        c.warned[path] = true;
        log().warn("hud sprite: texture {} is missing", path);
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
    auto* t = textureOf(texture, canvas.context());
    if (!t) return 0;
    auto const* data = readyData(*t, texture);
    if (!data) return 0;
    auto const& d = data->mTextureDescription.get();
    if (d.mWidth == 0 || d.mHeight < d.mWidth) return d.mWidth == 0 ? 0 : 1;
    return static_cast<int>(d.mHeight / d.mWidth);
}

bool blitSprite(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float alpha, int frame) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f) return true;
    int frames = spriteFrames(canvas, texture);
    if (frames <= 0) return false;
    auto* t = textureOf(texture, canvas.context());
    auto const* m = material();
    if (!t || !m) return false;
    frame      = ((frame % frames) + frames) % frames;
    float x0   = canvas.originX() + dst.x, y0 = canvas.originY() + dst.y, x1 = x0 + dst.w, y1 = y0 + dst.h;
    float v0   = static_cast<float>(frame) / static_cast<float>(frames);
    float v1   = static_cast<float>(frame + 1) / static_cast<float>(frames);
    auto& ctx  = canvas.context();
    ScreenContext& sc   = ctx.mScreenContext;
    Tessellator&   tess = sc.tessellator;
    // the alpha: the shader colour (glcolor materials) and the vertex colour
    ShaderColor& shader = sc.currentShaderColor;
    mce::Color   keep   = shader.color;
    shader.color        = mce::Color{1.0f, 1.0f, 1.0f, alpha};
    shader.dirty        = true;
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 8, false);
    tess.color(1.0f, 1.0f, 1.0f, alpha);
    // both windings: whichever way the UI's projection turns them
    tess.simpleQuad(Vec3{x0, y1, 0}, Vec2{0, v1}, Vec3{x1, y1, 0}, Vec2{1, v1}, Vec3{x1, y0, 0}, Vec2{1, v0}, Vec3{x0, y0, 0}, Vec2{0, v0});
    tess.simpleQuad(Vec3{x0, y0, 0}, Vec2{0, v0}, Vec3{x1, y0, 0}, Vec2{1, v0}, Vec3{x1, y1, 0}, Vec2{1, v1}, Vec3{x0, y1, 0}, Vec2{0, v1});
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> tex{*t};
    MeshHelpers::renderMeshImmediately(sc, tess, *m, tex, OffscreenCaptureDescription{});
    shader.color = keep;
    shader.dirty = true;
    return true;
}

} // namespace gf::client::hud::ext

#endif // GF_CLIENT

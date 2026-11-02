// Block families (F11), client mod: GUI textures in a native screen (SignBoard.h). The texture
// handles and the draw follow the UI framework of F12 (spike S5: a "ui_textured" mesh quad; the
// image batch draws GUI textures black on this game version).

#ifdef GF_CLIENT

#include "client/families/SignBoard.h"

#include <map>
#include <mutex>
#include <unordered_map>
#include <variant>

#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/file/PathView.h"
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

#include "mod/Log.h"

namespace gf::client::families {

namespace {

struct Textures {
    std::mutex                                        mutex;
    std::unordered_map<std::string, mce::TexturePtr*> handles; // kept for the process (never destroyed at exit)
    std::unordered_map<std::string, bool>             warned;
};
Textures& textures() {
    static auto* t = new Textures;
    return *t;
}

/// The texture as the UI loads it (the UI render context owns the textures its images draw with).
mce::TexturePtr* textureOf(std::string const& path, MinecraftUIRenderContext& ui) {
    auto&           t = textures();
    std::lock_guard lock(t.mutex);
    if (auto it = t.handles.find(path); it != t.handles.end()) return it->second;
    auto* handle = new mce::TexturePtr(ui.getTexture(ResourceLocation(Core::PathView(path)), false));
    t.handles.emplace(path, handle);
    return handle;
}

BedrockTextureData const* dataOf(mce::TexturePtr const& t) { return t.mClientTexture.get(); }

bool loaded(BedrockTextureData const& d) {
    return (static_cast<unsigned>(d.mTextureLoadState) & static_cast<unsigned>(TextureLoadState::LoadedBit)) != 0;
}

bool missing(BedrockTextureData const* d) { return !d || d->mIsMissingTexture == IsMissingTexture::Yes; }

void warnMissing(std::string const& path) {
    auto&           t = textures();
    std::lock_guard lock(t.mutex);
    if (t.warned[path]) return;
    t.warned[path] = true;
    log().warn("families: texture {} is missing", path);
}

/// The UI's textured material (the common group first, then the switchable one), kept for the
/// process.
mce::MaterialPtr const* uiTextured() {
    static mce::MaterialPtr* material = [] {
        auto* m = new mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString("ui_textured"));
        if (!m->mRenderMaterialInfoPtr.get()) m = new mce::MaterialPtr(mce::RenderMaterialGroup::switchable(), HashedString("ui_textured"));
        if (!m->mRenderMaterialInfoPtr.get()) log().warn("families: no ui_textured material");
        return m;
    }();
    return material->mRenderMaterialInfoPtr.get() ? material : nullptr;
}

} // namespace

std::optional<std::pair<int, int>> textureSize(screen::Canvas& canvas, std::string const& texture) {
    auto*       t    = textureOf(texture, canvas.context());
    auto const* data = t ? dataOf(*t) : nullptr;
    if (missing(data) || !loaded(*data)) return std::nullopt;
    auto const& d = data->mTextureDescription.get();
    if (d.mWidth == 0 || d.mHeight == 0) return std::nullopt;
    return std::pair{static_cast<int>(d.mWidth), static_cast<int>(d.mHeight)};
}

bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW, float texH) {
    auto* t = textureOf(texture, canvas.context());
    if (!t) return false;
    auto const* data = dataOf(*t);
    if (!missing(data) && !loaded(*data)) return false; // still loading
    if (missing(data)) {
        warnMissing(texture);
        return false;
    }
    auto const* material = uiTextured();
    if (!material) return false;
    float x0 = canvas.originX() + dst.x, y0 = canvas.originY() + dst.y, x1 = x0 + dst.w, y1 = y0 + dst.h;
    float u0 = u / texW, v0 = v / texH, u1 = (u + w) / texW, v1 = (v + h) / texH;
    // a textured quad in GUI pixels (the UI's matrices map GUI pixels), both windings: whichever
    // way the UI's projection turns them
    ScreenContext& sc   = canvas.context().mScreenContext;
    Tessellator&   tess = sc.tessellator;
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 8, false);
    tess.color(1.f, 1.f, 1.f, 1.f);
    tess.simpleQuad(Vec3{x0, y1, 0}, Vec2{u0, v1}, Vec3{x1, y1, 0}, Vec2{u1, v1}, Vec3{x1, y0, 0}, Vec2{u1, v0}, Vec3{x0, y0, 0}, Vec2{u0, v0});
    tess.simpleQuad(Vec3{x0, y0, 0}, Vec2{u0, v0}, Vec3{x1, y0, 0}, Vec2{u1, v0}, Vec3{x1, y1, 0}, Vec2{u1, v1}, Vec3{x0, y1, 0}, Vec2{u0, v1});
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> tex{*t};
    MeshHelpers::renderMeshImmediately(sc, tess, *material, tex, OffscreenCaptureDescription{});
    return true;
}

} // namespace gf::client::families

#endif // GF_CLIENT

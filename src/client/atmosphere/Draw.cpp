// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/renderer/TFWeatherRenderer.java (renderEffect)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere), client: immediate meshes with the engine's materials (Draw.h).

#ifdef GF_CLIENT

#include "client/atmosphere/Draw.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <variant>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/particle/ParticleEngine.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/options/GraphicsMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/ShaderColor.h"

#include "client/Client.h"
#include "client/render/TexturePath.h"

namespace gf::client::atmosphere {

namespace ca = core::atmosphere;

LevelRenderer* levelRenderer() {
    auto* c = currentClient();
    return c ? c->getLevelRenderer() : nullptr;
}

mce::MaterialPtr const* starsMaterial() {
    auto* lr = levelRenderer();
    if (!lr) return nullptr;
    auto const& player = lr->mLevelRendererPlayer;
    if (!player) return nullptr;
    auto const& m = player->starsMaterial.get();
    return m.mRenderMaterialInfoPtr.get() ? &m : nullptr;
}

mce::MaterialPtr const* particleBlendMaterial() {
    auto* lr = levelRenderer();
    if (!lr) return nullptr;
    auto const& m = lr->getParticleEngine().blendMat.get();
    return m.mRenderMaterialInfoPtr.get() ? &m : nullptr;
}

mce::TexturePtr const* texture(std::string const& path) {
    static std::mutex                                        m;
    static std::unordered_map<std::string, mce::TexturePtr*> cache; // kept for the process
    std::lock_guard                                          lock(m);
    if (auto it = cache.find(path); it != cache.end()) return it->second;
    auto* c = currentClient();
    if (!c) return nullptr;
    auto group = c->getTextureGroup();
    if (!group) return nullptr;
    auto* t = new mce::TexturePtr(group->getTexture(render::textureLocation(path), false, std::nullopt, cg::TextureSetLayerType::Color));
    cache.emplace(path, t);
    return t;
}

mce::MaterialPtr const* materialNamed(char const* name) {
    static std::mutex                               m;
    static std::unordered_map<std::string, mce::MaterialPtr*> cache; // kept for the process
    std::lock_guard                                 lock(m);
    auto&                                           slot = cache[name];
    if (!slot) {
        slot = new mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString(name));
        if (!slot->mRenderMaterialInfoPtr.get()) slot = new mce::MaterialPtr(mce::RenderMaterialGroup::switchable(), HashedString(name));
    }
    return slot->mRenderMaterialInfoPtr.get() ? slot : nullptr;
}

float farPlane(ScreenContext& screen) {
    float f = screen.camera.mZFar;
    return std::isfinite(f) && f > 0 ? f : 0.0f;
}

bool vibrantVisuals() {
    auto* c = currentClient();
    return c && c->getOptions().getGraphicsMode() == GraphicsMode::Advanced;
}

bool fancyGraphics() {
    auto* c = currentClient();
    return !c || c->getOptions().getGraphicsMode() != GraphicsMode::Simple;
}

namespace {

/// The screen's shader colour set to white for our draws and put back after.
class WhiteShaderColour {
public:
    explicit WhiteShaderColour(ScreenContext& screen) : mColor(screen.currentShaderColor), mKeep(mColor) {
        mColor.color = mce::Color{1.f, 1.f, 1.f, 1.f};
        mColor.dirty = true;
    }
    ~WhiteShaderColour() {
        mColor       = mKeep;
        mColor.dirty = true;
    }
    WhiteShaderColour(WhiteShaderColour const&)            = delete;
    WhiteShaderColour& operator=(WhiteShaderColour const&) = delete;

private:
    ShaderColor& mColor;
    ShaderColor  mKeep;
};

} // namespace

void drawColouredQuads(ScreenContext& screen, mce::MaterialPtr const& material, std::vector<ColourVertex> const& quads) {
    if (quads.size() < 4) return;
    Tessellator&      tess = screen.tessellator;
    WhiteShaderColour white(screen);
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, static_cast<int>(quads.size()), false);
    for (auto const& v : quads) {
        tess.color(v.r, v.g, v.b, v.a);
        tess.vertex(v.x, v.y, v.z);
    }
    MeshHelpers::renderMeshImmediately(screen, tess, material, OffscreenCaptureDescription{});
}

void drawSmoothQuads(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const* tex, std::vector<ColourVertex> const& quads) {
    if (quads.size() < 4) return;
    Tessellator&      tess = screen.tessellator;
    WhiteShaderColour white(screen);
    // batches of at most 16 000 quads: an immediate mesh stays within 16-bit indices
    constexpr std::size_t kBatch = 16000 * 4;
    // Vibrant Visuals lights the material by its MERS (metalness, emissive, roughness, subsurface):
    // the sky's stars and aurora glow by themselves (emissive and rough, the bytes 1 and 2 whatever
    // the channel order), as TF draws them without light
    bool const emissive = vibrantVisuals();
    for (std::size_t from = 0; from + 3 < quads.size(); from += kBatch) {
        std::size_t to = std::min(quads.size() - quads.size() % 4, from + kBatch);
        tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, static_cast<int>(to - from), false);
        for (std::size_t i = from; i < to; ++i) {
            auto const& v = quads[i];
            tess.color(v.r, v.g, v.b, v.a);
            if (tex) tess.mNextUV[0] = glm::vec2{0.5f, 0.5f};
            if (emissive) tess.mNextMERS = 0x00FFFF00u;
            tess.vertex(v.x, v.y, v.z);
        }
        if (tex) {
            std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> variant{*tex};
            MeshHelpers::renderMeshImmediately(screen, tess, material, variant, OffscreenCaptureDescription{});
        } else {
            MeshHelpers::renderMeshImmediately(screen, tess, material, OffscreenCaptureDescription{});
        }
    }
}

void drawTintedQuads(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const& tex, std::vector<ColourVertex> const& quads) {
    if (quads.size() < 4) return;
    Tessellator&      tess = screen.tessellator;
    WhiteShaderColour white(screen);
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
    for (std::size_t i = 0; i + 3 < quads.size(); i += 4) {
        auto const* q = &quads[i];
        tess.color((q[0].r + q[1].r + q[2].r + q[3].r) / 4, (q[0].g + q[1].g + q[2].g + q[3].g) / 4, (q[0].b + q[1].b + q[2].b + q[3].b) / 4, (q[0].a + q[1].a + q[2].a + q[3].a) / 4);
        tess.simpleQuad(Vec3{q[0].x, q[0].y, q[0].z}, Vec2{0, 0}, Vec3{q[1].x, q[1].y, q[1].z}, Vec2{1, 0}, Vec3{q[2].x, q[2].y, q[2].z}, Vec2{1, 1}, Vec3{q[3].x, q[3].y, q[3].z}, Vec2{0, 1});
    }
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> variant{tex};
    MeshHelpers::renderMeshImmediately(screen, tess, material, variant, OffscreenCaptureDescription{});
}

void drawColumns(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const& tex, std::vector<ca::Column> const& columns, float brightness) {
    if (columns.empty()) return;
    Tessellator&      tess = screen.tessellator;
    WhiteShaderColour white(screen);
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
    int        quads = 0;
    bool const vv    = vibrantVisuals();
    for (auto const& c : columns) {
        float br = c.fullBright ? 1.0f : brightness;
        tess.color(std::clamp(c.color.r * br, 0.f, 1.f), std::clamp(c.color.g * br, 0.f, 1.f), std::clamp(c.color.b * br, 0.f, 1.f), std::clamp(c.alpha, 0.f, 1.f));
        // u: one repeat across the width, split where it wraps
        struct USeg {
            float s0, s1, tu0, tu1;
        };
        USeg  useg[2];
        int   nu  = 0;
        float ufr = c.u0 - std::floor(c.u0);
        if (ufr < 1e-5f || ufr > 1.f - 1e-5f) useg[nu++] = {0.f, 1.f, 0.f, 1.f};
        else {
            useg[nu++] = {0.f, 1.f - ufr, ufr, 1.f};
            useg[nu++] = {1.f - ufr, 1.f, 0.f, ufr};
        }
        // v: from v0 at the bottom to v1 at the top, split at every repeat
        float const dv = c.v1 - c.v0;
        if (std::abs(dv) < 1e-6f || !std::isfinite(c.v0) || !std::isfinite(c.v1)) continue;
        float const vlo = std::min(c.v0, c.v1), vhi = std::max(c.v0, c.v1);
        for (float k = std::floor(vlo); k < vhi; k += 1.f) {
            float a = std::max(vlo, k), b = std::min(vhi, k + 1.f);
            if (b - a < 1e-5f) continue;
            float ya = c.y0 + (c.y1 - c.y0) * (a - c.v0) / dv;
            float yb = c.y0 + (c.y1 - c.y0) * (b - c.v0) / dv;
            float ta = a - k, tb = b - k;
            for (int i = 0; i < nu; ++i) {
                auto const& s  = useg[i];
                float       x0 = c.x0 + (c.x1 - c.x0) * s.s0, z0 = c.z0 + (c.z1 - c.z0) * s.s0;
                float       x1 = c.x0 + (c.x1 - c.x0) * s.s1, z1 = c.z0 + (c.z1 - c.z0) * s.s1;
                if (vv && c.fullBright) tess.mNextMERS = 0x00FFFF00u; // Vibrant Visuals: TF's full-bright columns glow
                tess.simpleQuad(Vec3{x0, ya, z0}, Vec2{s.tu0, ta}, Vec3{x1, ya, z1}, Vec2{s.tu1, ta}, Vec3{x1, yb, z1}, Vec2{s.tu1, tb}, Vec3{x0, yb, z0}, Vec2{s.tu0, tb});
                ++quads;
            }
        }
    }
    if (quads == 0) {
        tess.clear();
        return;
    }
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture> variant{tex};
    MeshHelpers::renderMeshImmediately(screen, tess, material, variant, OffscreenCaptureDescription{});
}

std::shared_ptr<mce::Mesh> buildMesh(ScreenContext& screen, std::vector<ColourVertex> const& quads, char const* name) {
    Tessellator& tess = screen.tessellator;
    tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, static_cast<int>(std::max<std::size_t>(quads.size(), 4)), false);
    if (quads.size() < 4) {
        tess.color(0.f, 0.f, 0.f, 0.f);
        for (int i = 0; i < 4; ++i) tess.vertex(0.f, -1000.f, 0.f); // a degenerate quad: nothing to see
    } else {
        for (auto const& v : quads) {
            tess.color(v.r, v.g, v.b, v.a);
            tess.vertex(v.x, v.y, v.z);
        }
    }
    return std::make_shared<mce::Mesh>(tess.end(Tessellator::UploadMode::Buffered, name, SupplementaryFieldAutoGenerationMode::None));
}

} // namespace gf::client::atmosphere

#endif // GF_CLIENT

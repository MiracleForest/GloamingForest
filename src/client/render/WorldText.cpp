// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// Contract "client.render" (F12): world text (feature/ext/ui/Client.h, render::WorldText), the
// look of Java's Display.TextDisplay that upstream's final castle gazebo uses for its "WIP" lines
// (TFStructureComponent#setInvisibleTextEntity: VERTICAL billboard, default background):
// - the pose: the position, the billboard (FIXED: the entity's yRot / xRot; VERTICAL: turned about
//   the vertical axis to the camera; HORIZONTAL: the entity's yRot, the camera's pitch; CENTER:
//   facing the camera), the scale, then TextDisplayRenderer#renderInner's scale of 0.025 per font
//   pixel with y down;
// - the layout: the lines wrapped at lineWidth font pixels, 10 pixels a line (9 + 1), the block of
//   text centred above the position (translate(1 - width / 2, -height)), each line centred in it;
// - the background quad (-1, -1) .. (width, height) in the background colour, then the text in its
//   colour (the light at the position, or full bright), drawn with the game's font and the vanilla
//   sign renderer's text materials as F11's sign text is.
// Drawn from an onWorld callback for the texts of the local player's dimension within viewRange.
// Client build only.

#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>

#include "glm/gtc/matrix_transform.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"

#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/blockactor/SignRenderer.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"

#include "client/Client.h"
#include "client/render/Render.h"
#include "feature/ext/ui/Client.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::render {

/// The material Lines draws its bars with (vertex colours; `throughWalls`: no depth test). Render.cpp.
[[nodiscard]] mce::MaterialPtr const* fillMaterial(bool throughWalls);

namespace {

struct TextStore {
    std::mutex                           mutex;
    std::map<std::size_t, WorldText>     texts;
    std::size_t                          next = 0;
    std::atomic<std::uint64_t>           drawn{0};
    std::atomic<bool>                    any{false};
};

TextStore& store() {
    static auto* s = new TextStore(); // the render thread may outlive static destruction
    return *s;
}

mce::Color colorOf(unsigned argb) {
    return mce::Color{static_cast<float>((argb >> 16) & 0xFF) / 255.f, static_cast<float>((argb >> 8) & 0xFF) / 255.f,
                      static_cast<float>(argb & 0xFF) / 255.f, static_cast<float>((argb >> 24) & 0xFF) / 255.f};
}

/// The vanilla sign renderer's text materials (lit, and the glowing one that ignores the light).
mce::MaterialPtr const* textMaterial(BaseActorRenderContext& rc, bool fullBright) {
    BlockActorRenderDispatcher& dispatcher = rc.mBlockEntityRenderDispatcher;
    auto* sign = static_cast<SignRenderer*>(dispatcher.mRenderers->operator[](BlockActorRendererId::Sign).get());
    if (!sign) return nullptr;
    mce::MaterialPtr const& m = fullBright ? sign->mGlowSignTextMaterial.get() : sign->mSignTextMaterial.get();
    if (m.mRenderMaterialInfoPtr.get()) return &m;
    mce::MaterialPtr const& plain = sign->mSignTextMaterial.get();
    return plain.mRenderMaterialInfoPtr.get() ? &plain : nullptr;
}

/// Font#split for one line: words wrapped at `width` font pixels (a word longer than a line is cut).
std::vector<std::string> wrap(Font& font, std::string const& text, int width) {
    std::vector<std::string> out;
    std::size_t              start = 0;
    while (start <= text.size()) {
        auto nl   = text.find('\n', start);
        auto para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        std::size_t i = 0;
        while (i < para.size()) {
            auto sp   = para.find(' ', i);
            auto word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i + 1);
            auto next = line + word;
            if (!line.empty() && font.getLineLength(next, 1.0f, false) > width) {
                while (!line.empty() && line.back() == ' ') line.pop_back();
                out.push_back(line);
                line = word;
            } else {
                line = next;
            }
            i += word.size();
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

} // namespace

std::size_t addWorldText(WorldText text) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            handle = ++s.next;
    s.texts.emplace(handle, std::move(text));
    s.any = true;
    return handle;
}

bool setWorldText(std::size_t handle, WorldText text) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = s.texts.find(handle);
    if (it == s.texts.end()) return false;
    it->second = std::move(text);
    return true;
}

bool removeWorldText(std::size_t handle) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    bool            had = s.texts.erase(handle) > 0;
    s.any               = !s.texts.empty();
    return had;
}

std::uint64_t worldTextsDrawn() { return store().drawn.load(std::memory_order_relaxed); }

bool drawWorldText(RenderContext& ctx, WorldText const& text) {
    if (!ctx.actors || ctx.itemSpace || text.lines.empty()) return false;
    glm::vec3 const p{text.pos.x - ctx.camera.x, text.pos.y - ctx.camera.y, text.pos.z - ctx.camera.z};
    // the eye in the same camera-relative space (third person: behind or in front of the player)
    glm::mat4 const viewWorld = ctx.screen.camera.viewMatrixStack.get().top()._m.get() * ctx.screen.camera.worldMatrixStack.get().top()._m.get();
    glm::vec3 const eye       = glm::vec3(glm::inverse(viewWorld) * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
    glm::vec3 const toEye     = eye - p;
    if (glm::length(toEye) > text.viewRange) return false;
    auto  handle = ctx.actors->getFontHandle();
    auto& font   = handle->getFont();
    auto const* material = textMaterial(*ctx.actors, text.fullBright);
    if (!material) return false;

    // the pose (Display#renderInner's billboard), the text then faces +z with x right, y down
    using glm::vec3;
    glm::mat4 pose = glm::translate(glm::mat4{1.0f}, p);
    auto      face = [&](vec3 n) {
        // a basis with n towards the reader: right = up x n, up = n x right
        if (glm::length(n) < 1e-5f) n = vec3{0, 0, 1};
        n            = glm::normalize(n);
        vec3 up      = std::abs(n.y) > 0.999f ? vec3{0, 0, -1} : vec3{0, 1, 0};
        vec3 right   = glm::normalize(glm::cross(up, n));
        vec3 trueUp  = glm::cross(n, right);
        glm::mat4 r{1.0f};
        r[0] = glm::vec4(right, 0);
        r[1] = glm::vec4(trueUp, 0);
        r[2] = glm::vec4(n, 0);
        pose = pose * r;
    };
    switch (text.billboard) {
    case WorldText::Billboard::Fixed:
        pose = glm::rotate(pose, glm::radians(-text.yaw), vec3{0, 1, 0});
        pose = glm::rotate(pose, glm::radians(text.pitch), vec3{1, 0, 0});
        break;
    case WorldText::Billboard::Vertical: face(vec3{toEye.x, 0.0f, toEye.z}); break;
    case WorldText::Billboard::Horizontal: {
        pose            = glm::rotate(pose, glm::radians(-text.yaw), vec3{0, 1, 0});
        float horizontal = std::sqrt(toEye.x * toEye.x + toEye.z * toEye.z);
        pose            = glm::rotate(pose, -std::atan2(toEye.y, horizontal), vec3{1, 0, 0});
        break;
    }
    case WorldText::Billboard::Center: face(toEye); break;
    }
    float const s = 0.025f * text.scale;
    pose          = glm::scale(pose, vec3{s, -s, -s}); // y down; a positive determinant (the glyphs face the reader)

    // the layout
    std::vector<std::string> lines;
    for (auto const& l : text.lines)
        for (auto& w : wrap(font, l, std::max(1, text.lineWidth))) lines.push_back(std::move(w));
    int width = 0;
    std::vector<int> widths;
    for (auto const& l : lines) {
        widths.push_back(font.getLineLength(l, 1.0f, false));
        width = std::max(width, widths.back());
    }
    int const lineHeight = 9 + 1;
    int const height     = static_cast<int>(lines.size()) * lineHeight;
    pose                 = glm::translate(pose, vec3{1.0f - static_cast<float>(width) / 2.0f, static_cast<float>(-height), 0.0f});

    if (!text.fullBright) setupLight(ctx, text.pos);
    auto&         world   = ctx.screen.camera.worldMatrixStack.get();
    // the background quad
    if ((text.background >> 24) != 0) {
        if (auto const* fill = fillMaterial(text.seeThrough)) {
            auto ref          = world.push(false);
            ref.mat->_m.get() = ref.mat->_m.get() * pose;
            Tessellator& tess = ctx.screen.tessellator;
            auto const   c    = colorOf(text.background);
            tess.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::QuadList, 0, false);
            tess.color(c.r, c.g, c.b, c.a);
            // behind the glyphs (Java: POLYGON_OFFSET for the text)
            float const z = 0.01f;
            tess.vertex(-1.0f, -1.0f, z);
            tess.vertex(-1.0f, static_cast<float>(height), z);
            tess.vertex(static_cast<float>(width), static_cast<float>(height), z);
            tess.vertex(static_cast<float>(width), -1.0f, z);
            MeshHelpers::renderMeshImmediately(ctx.screen, tess, *fill, OffscreenCaptureDescription{});
        }
    }
    // the lines
    mce::Color const color = colorOf(text.color);
    mce::Color const dark{1.0f, 1.0f, 1.0f, 1.0f};
    mce::Color const none{0.f, 0.f, 0.f, 0.f};
    float            y = 0.0f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        float x           = static_cast<float>(width) / 2.0f - static_cast<float>(widths[i]) / 2.0f;
        auto  ref         = world.push(false);
        ref.mat->_m.get() = ref.mat->_m.get() * pose;
        font.drawCached(ctx.screen, lines[i], x, y, color, false, true, false, material, -1, text.shadow, 0.0f, none, dark, 0.0f, 0.0f, OffscreenCaptureDescription{}, true);
        y += static_cast<float>(lineHeight);
    }
    if (store().drawn.fetch_add(1, std::memory_order_relaxed) == 0) log().info("render: world text drawn ({} lines)", lines.size());
    return true;
}

namespace {
class WorldTextModule {
public:
    void install() {
        // world texts belong to the world they were added in (the client opens many worlds)
        if (!mExit)
            mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
                auto&           s = store();
                std::lock_guard lock(s.mutex);
                s.texts.clear();
                s.any = false;
            });
        static bool once = false; // onWorld has no removal: registered once per process
        if (std::exchange(once, true)) return;
        onWorld([](RenderContext& ctx, float) {
            auto& s = store();
            if (!s.any.load(std::memory_order_relaxed)) return;
            auto* player = currentPlayer();
            if (!player) return;
            int const dim = static_cast<int>(player->getDimensionId());
            std::vector<WorldText> texts;
            {
                std::lock_guard lock(s.mutex);
                for (auto const& [h, t] : s.texts)
                    if (t.dim == dim) texts.push_back(t);
            }
            for (auto const& t : texts) {
                RenderContext one = ctx;
                one.throughWalls  = t.seeThrough;
                try {
                    (void)drawWorldText(one, t);
                } catch (...) {}
            }
        });
    }
    void uninstall() {
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        mExit = nullptr;
    }

private:
    ll::event::ListenerPtr mExit;
};
} // namespace

} // namespace gf::client::render

GF_REGISTER_MODULE_ORDERED(gf::client::render::WorldTextModule, gf::ModuleOrder::Contracts + 1);

#endif // GF_CLIENT

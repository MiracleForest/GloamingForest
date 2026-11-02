// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/RegistrationEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11), client mod: the text of TF's signs drawn flat on the board, front and back.
// TF adds its 16 signs to BlockEntityType.SIGN and its 16 hanging signs to HANGING_SIGN
// (RegistrationEvents), so Java draws their text with vanilla's SignRenderer / HangingSignRenderer:
// - the pose: SignRenderer.translateSign (the block's centre 0.75 x the model scale 2/3 up, turned
//   by -getYRotationDegrees; a wall sign 0.3125 down and 0.4375 back), HangingSignRenderer
//   .translateSign (0.9375 up, turned, 0.3125 down); the back side turned 180 degrees more;
//   translateSignText: TEXT_OFFSET (0, 1/3, 0.046667), a hanging sign's (0, -0.32, 0.073), then
//   the scale 1/64 x 2/3 (hanging 1/64 x 0.9) with y down;
// - the lines: four, each the first line of Font.split(line, 90; hanging 60), x = -width / 2, line
//   i at y = i x 10 - 20 (hanging i x 9 - 18);
// - the colours: text that does not glow is getDarkColor (the dye's colour x 0.4) in the sign's
//   light (packedLight); glowing text is the dye's colour at full brightness, with
//   drawInBatch8xOutline in getDarkColor (black text: 0xF0EBCC) while isOutlineVisible (black text,
//   the local player looking through a spyglass in first person, or the camera entity within 16
//   blocks);
// - drawn within 64 blocks (BlockEntityRenderer.getViewDistance).
// The server sends the text ("families.sign_text", src/feature/families/Signs.cpp); the block
// renderer of every sign family block (contract client.render, F12) draws it with the game's font
// in the sign's pose, the way Bedrock's SignRenderer draws its own text (measured in game: its
// Font::drawCached calls, the shader state and the matrix it leaves): ActorShaderManager::
// setupShaderParameters at the sign first (the light at the block, which the lit text material
// multiplies by), then each line with the darken path on (the text colour times the dark colour
// given to drawCached: the only way these materials take a colour), the lit material for unlit text
// and the glow material (no light) for glowing text and its outline, in a matrix of positive
// determinant like the game's (the glyphs, and the normals the font generates for them, face the
// reader). Vibrant Visuals lights sign text, the game's own too: next to the game's oak signs the
// colours shift alike (client scenario families, pictures 20 to 23); the Classic look shows Java's
// colours (scenario families.classic).
// The server's display entities (gloamingforest:sign_text: the name tag vanilla clients read) show
// no name tag here once the client has their sign's text, which names them by unique id:
// ActorRenderDispatcher::extractRenderTextObjects (the name tag of one actor for the frame) gives
// nothing for them on the client of a companion (the only hook of name tags; F11 owns it). The
// server withdraws a sign's text ("gone") when the sign, its text or its chunk goes, so the client's
// list holds the signs of loaded chunks only.
// An outlined line (glowing text) is drawn over its 8 outline copies nearer to the camera along the
// view rays (Java's POLYGON_OFFSET: in depth only), by a lead that grows with the distance as the
// depth buffer's step does.

#ifdef GF_CLIENT

#include "client/families/SignText.h"

#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

#include "glm/gtc/matrix_transform.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ClientThreadExecutor.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/blockactor/SignRenderer.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec4.h"
#include "mc/deps/minecraft_renderer/objects/NameTagRenderObject.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActorRendererId.h"

#include "client/Client.h"
#include "client/render/Render.h"
#include "feature/ext/ui/Client.h"
#include "feature/families/Families.h"
#include "feature/families/SignFont.h"
#include "feature/families/Signs.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace gf::client::families {

namespace fam = gf::feature::families;
using Json    = nlohmann::json;

// ---- colours ------------------------------------------------------------------------------------------

unsigned dyeTextColor(std::string_view dye) {
    static std::map<std::string, unsigned, std::less<>> const colours{
        {"white", 0xFFFFFF}, {"orange", 0xFF681F}, {"magenta", 0xFF00FF}, {"light_blue", 0x9AC0CD}, {"yellow", 0xFFFF00}, {"lime", 0xBFFF00},
        {"pink", 0xFF69B4},  {"gray", 0x808080},   {"light_gray", 0xD3D3D3}, {"cyan", 0x00FFFF},    {"purple", 0xA020F0}, {"blue", 0x0000FF},
        {"brown", 0x8B4513}, {"green", 0x00FF00},  {"red", 0xFF0000},       {"black", 0x000000},
    };
    auto it = colours.find(dye);
    return it == colours.end() ? 0 : it->second;
}

unsigned signDarkColor(unsigned textColor, bool glowing) {
    if (textColor == 0 && glowing) return 0xF0EBCC; // -988212
    auto dark = [textColor](int shift) { return static_cast<unsigned>(static_cast<int>(static_cast<double>((textColor >> shift) & 0xFF) * 0.4)) << shift; };
    return dark(16) | dark(8) | dark(0);
}

bool SignSide::empty() const {
    for (auto const& l : lines)
        if (!l.empty()) return false;
    return true;
}

// ---- the text the server sent -------------------------------------------------------------------------

namespace {

using Key = std::tuple<int, int, int, int>; ///< dim, x, y, z

struct Store {
    std::mutex                                     mutex;    ///< the client thread writes, the render thread reads
    std::map<Key, std::shared_ptr<SignText const>> texts;    ///< immutable: readers keep theirs
    std::map<std::int64_t, Key>                    entities; ///< display entity -> its sign (the texts' display ids)

    /// Puts `text` (nullptr: none) at `k`, keeping the display entity index in step.
    void put(Key const& k, std::shared_ptr<SignText const> text) {
        if (auto it = texts.find(k); it != texts.end()) {
            for (auto id : it->second->display)
                if (auto e = entities.find(id); e != entities.end() && e->second == k) entities.erase(e);
            texts.erase(it);
        }
        if (!text) return;
        for (auto id : text->display) entities[id] = k;
        texts.emplace(k, std::move(text));
    }
};
Store& store() {
    static auto* s = new Store; // kept for the process (never destroyed at exit)
    return *s;
}

std::optional<Key> keyOf(Json const& p) {
    if (!p.is_array() || p.size() != 4) return std::nullopt;
    for (auto const& v : p)
        if (!v.is_number_integer()) return std::nullopt;
    return Key{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
}

SignSide sideOf(Json const& j) {
    SignSide s;
    if (!j.is_object()) return s;
    auto lines = j.value("lines", Json::array());
    for (std::size_t i = 0; i < s.lines.size() && lines.is_array() && i < lines.size(); ++i)
        if (lines[i].is_string()) s.lines[i] = lines[i].get<std::string>();
    if (auto c = j.find("color"); c != j.end() && c->is_string()) s.color = c->get<std::string>();
    if (auto g = j.find("glowing"); g != j.end() && g->is_boolean()) s.glowing = g->get<bool>();
    return s;
}

std::atomic<bool> gReceived{false};

void onText(Json const& msg) {
    if (!msg.is_object()) return;
    std::size_t n = 0;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        if (auto g = msg.find("gone"); g != msg.end() && g->is_array())
            for (auto const& p : *g)
                if (auto k = keyOf(p)) s.put(*k, nullptr);
        if (auto list = msg.find("signs"); list != msg.end() && list->is_array()) {
            for (auto const& e : *list) {
                if (!e.is_object()) continue;
                auto k = keyOf(e.value("pos", Json()));
                if (!k) continue;
                SignText text{sideOf(e.value("front", Json())), sideOf(e.value("back", Json())), {}};
                if (auto d = e.find("display"); d != e.end() && d->is_array())
                    for (auto const& id : *d)
                        if (id.is_number_integer() && id.get<std::int64_t>() != 0) text.display.push_back(id.get<std::int64_t>());
                s.put(*k, std::make_shared<SignText const>(std::move(text)));
                ++n;
            }
        }
    }
    if (n && !gReceived.exchange(true)) log().info("families: sign text received for the client mod ({} signs)", n);
}

void clearTexts() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.texts.clear();
    s.entities.clear();
}

} // namespace

namespace {
/// The text of a sign without a copy (the render thread asks for every sign every frame).
std::shared_ptr<SignText const> sharedTextAt(int dim, int x, int y, int z) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = s.texts.find(Key{dim, x, y, z});
    return it == s.texts.end() ? nullptr : it->second;
}
/// The text of the sign that names display entity `id` (nullptr: none).
std::shared_ptr<SignText const> sharedTextOfDisplay(std::int64_t id) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            e = s.entities.find(id);
    if (e == s.entities.end()) return nullptr;
    auto it = s.texts.find(e->second);
    return it == s.texts.end() ? nullptr : it->second;
}
} // namespace

std::optional<SignText> signTextAt(int dim, int x, int y, int z) {
    auto t = sharedTextAt(dim, x, y, z);
    if (!t) return std::nullopt;
    return *t;
}

std::size_t signTextCount() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    return s.texts.size();
}

// ---- drawing ----------------------------------------------------------------------------------------------

namespace {

constexpr float kViewDistance    = 64.0f; ///< BlockEntityRenderer.getViewDistance
constexpr float kOutlineDistance = 16.0f; ///< SignRenderer.OUTLINE_RENDER_DISTANCE = Mth.square(16)

mce::Color colorOf(unsigned rgb) {
    return mce::Color{static_cast<float>((rgb >> 16) & 0xFF) / 255.f, static_cast<float>((rgb >> 8) & 0xFF) / 255.f, static_cast<float>(rgb & 0xFF) / 255.f, 1.f};
}

/// The vanilla sign renderer's text materials: the lit one, and the glowing one, which ignores the
/// light (Java's full-bright 15728880).
mce::MaterialPtr const* textMaterial(BaseActorRenderContext& rc, bool glowing) {
    BlockActorRenderDispatcher& dispatcher = rc.mBlockEntityRenderDispatcher;
    auto* sign = static_cast<SignRenderer*>(dispatcher.mRenderers->operator[](BlockActorRendererId::Sign).get());
    if (!sign) return nullptr;
    mce::MaterialPtr const& m = glowing ? sign->mGlowSignTextMaterial.get() : sign->mSignTextMaterial.get();
    if (m.mRenderMaterialInfoPtr.get()) return &m;
    mce::MaterialPtr const& plain = sign->mSignTextMaterial.get();
    return plain.mRenderMaterialInfoPtr.get() ? &plain : nullptr;
}

/// One line in `rgb` times `scale` (0..1 per channel). The sign text materials take the colour only
/// through the font's darken path: the line's colour times the shader's dark colour, which drawCached
/// sets when `darken` is on (with it off the text keeps whatever dark colour the last text left,
/// which made every line the same grey); the lit material multiplies by the light that
/// SignRenderer's setupShaderParameters left in the screen context. Vanilla's SignRenderer draws its
/// text the same way (darken on, its dark colour 0.5 for unlit text).
void drawLine(Font& font, ScreenContext& sc, std::string_view text, float x, float y, unsigned rgb, float scale, mce::MaterialPtr const* material) {
    mce::Color const color = colorOf(rgb);
    mce::Color const dark{scale, scale, scale, 1.f};
    mce::Color const none{0.f, 0.f, 0.f, 0.f};
    font.drawCached(sc, text, x, y, color, true, true, false, material, -1, false, 0.0f, none, dark, 0.0f, 0.0f, OffscreenCaptureDescription{}, true);
}

/// One side of a sign: SignRenderer.renderSignText in the pose `pose` (the sign's, in camera space),
/// with the camera's eye at `eye` (in the same camera-relative space).
void drawSide(render::RenderContext& ctx, Font& font, glm::mat4 const& pose, SignSide const& side, bool front, bool hanging, bool outlineNear, glm::vec3 const& eye) {
    if (side.empty()) return;
    auto& rc = *ctx.actors;
    // translateSignText
    glm::mat4 m = pose;
    if (!front) m = glm::rotate(m, glm::radians(180.0f), glm::vec3{0, 1, 0});
    m             = glm::translate(m, hanging ? glm::vec3{0.0f, -0.32f, 0.073f} : glm::vec3{0.0f, 0.33333334f, 0.046666667f});
    float const f = 0.015625f * (hanging ? 0.9f : 0.6666667f);
    // Java's scale (f, -f, f) puts the glyphs where (f, -f, -f) does (they lie at z 0); the latter
    // keeps the handedness of the game's own sign text (a positive determinant, as vanilla's
    // SignRenderer leaves the matrix: measured), so the glyph quads face the reader and so do the
    // normals the font generates for them, which Vibrant Visuals lights the text by (mirrored, the
    // text was lit from behind: grey-blue and washed out).
    m = glm::scale(m, glm::vec3{f, -f, -f});
    int const lineHeight = hanging ? 9 : 10;  // HangingSignBlockEntity.TEXT_LINE_HEIGHT / SignBlockEntity's
    int const maxWidth   = hanging ? 60 : 90; // getMaxTextLineWidth
    int const top        = 4 * lineHeight / 2;
    // the colours (renderSignText): text that does not glow is getDarkColor (the dye's colour x 0.4,
    // drawn as the dye's colour with the dark colour 0.4) in the sign's light; glowing text is the
    // dye's colour at full brightness, with the 8x outline in getDarkColor (black: 0xF0EBCC) while
    // isOutlineVisible
    unsigned const textColor = dyeTextColor(side.color);
    unsigned const dark      = signDarkColor(textColor, side.glowing);
    bool const     outline   = side.glowing && (textColor == 0 || outlineNear); // isOutlineVisible
    float const    scale     = side.glowing ? 1.0f : 0.4f;
    auto const*    material  = textMaterial(rc, side.glowing);
    if (!material) return;
    // Java draws an outlined line over its 8 copies with POLYGON_OFFSET (glPolygonOffset(-1, -10):
    // nearer in depth only, never moved on screen). The same here: the line moves towards the
    // camera along the view rays (a scale about the eye), so its pixels stay where they are, by a
    // lead that grows with the square of the
    // distance as a perspective depth buffer's step does (about distance^2 / near plane per step):
    // 0.001 block close up, 0.017 at 40 blocks, 0.042 at 64, where the text stops being drawn. A
    // fixed lead (the 0.001 block of before) falls below the depth step past ~20 blocks, and the
    // copies behind could show through the letters.
    // The eye is not always the origin of these matrices (camera-relative to the camera's target,
    // the player): in third person it stands behind or in front of the player, so the scale is
    // about the eye itself.
    glm::mat4 frontOfOutline = m;
    {
        glm::vec3 const centre   = glm::vec3(m * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
        float const     distance = glm::length(centre - eye);
        float const     lead     = 0.001f + distance * distance * 1.0e-5f;
        if (distance > 0.0f && lead < distance)
            frontOfOutline = glm::translate(glm::mat4{1.0f}, eye) * glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f - lead / distance}) * glm::translate(glm::mat4{1.0f}, -eye) * m;
    }

    auto& world = ctx.screen.camera.worldMatrixStack.get();
    for (int i = 0; i < 4; ++i) {
        auto line = fam::signfont::firstLine(side.lines[i], maxWidth);
        if (line.empty()) continue;
        int   width = font.getLineLength(line, 1.0f, false);
        float x     = static_cast<float>((-width) / 2);
        float y     = static_cast<float>(i * lineHeight - top);
        if (outline) {
            // drawInBatch8xOutline: the line in the dark colour moved one pixel (the glyphs' shadow
            // offset) in eight directions, full bright, then the line itself in front (Java's
            // POLYGON_OFFSET). The font caches a line's mesh by its text and colour, not its place:
            // each copy moves by its matrix.
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    auto ref          = world.push(false);
                    ref.mat->_m.get() = ref.mat->_m.get() * glm::translate(m, glm::vec3{static_cast<float>(dx), static_cast<float>(dy), 0.0f});
                    drawLine(font, ctx.screen, line, x, y, dark, 1.0f, material);
                }
            }
        }
        auto ref          = world.push(false);
        ref.mat->_m.get() = ref.mat->_m.get() * (outline ? frontOfOutline : m);
        drawLine(font, ctx.screen, line, x, y, textColor, scale, material);
    }
}

/// The screen context's text state for one sign: SignRenderer's light at the sign
/// (ActorShaderManager::setupShaderParameters at the block, as SignRenderer::render sets it before
/// the board and the text: the light the lit text material multiplies by, Java's packedLight) and
/// environmental text.
/// What it writes, and what is restored afterwards:
/// - the screen context's own state: its shader colour and dark colour (ShaderColor values the next
///   draw may read without setting: the font's darken path, Tessellator colours) and its drawing
///   flags (isDrawingEnvironmentalText, set here, and the others, should the game's calls touch
///   them): saved and restored;
/// - the entity constant buffer (mce::GlobalConstantBuffers::entityConstantBuffer, ActorConstants:
///   TILE_LIGHT_COLOR / INTENSITY, BLOCK_LIGHT_COLOR, OVERLAY_COLOR, CHANGE_COLOR, GLINT_COLOR,
///   UV_ANIM / OFFSET / ROTATION / SCALE, through setEntityConstants): not restorable, as the game
///   offers no way to read it back (its ShaderConstantFloat2 / Float4 constants have no data
///   accessor). It is per-draw state: every actor and block actor renderer of the game writes all of
///   it before it draws (vanilla's SignRenderer leaves exactly this state after a vanilla sign), and
///   F12's world pass runs after LevelRendererCamera::renderBlockEntities, where the last block
///   actor drawn left it at its own position. A block renderer after this one finds the light of
///   the last sign drawn instead of the last block actor's: never a state it could rely on (one
///   that draws with an actor material sets its own, as the game's renderers do).
class SignTextState {
public:
    SignTextState(render::RenderContext& ctx, BlockPos const& pos, bool lit)
    : mScreen(ctx.screen),
      mColor(static_cast<ShaderColor&>(ctx.screen.currentShaderColor)),
      mDark(static_cast<ShaderColor&>(ctx.screen.currentShaderDarkColor)),
      mFlags(Flags::of(ctx.screen)) {
        mScreen.isDrawingEnvironmentalText = true;
        if (!lit) return;
        IClientInstance& client = ctx.actors->mClientInstance;
        auto*            region = client.getRegion();
        auto*            light  = client.getLightTexture();
        if (region && light)
            ActorShaderManager::setupShaderParameters(ctx.screen, *region, pos, ctx.partialTicks, false, *light, ctx.actors->mLightVolumeManager, Vec2{1.0f, 1.0f}, Vec4{0.0f, 0.0f, 0.0f, 0.0f});
    }
    ~SignTextState() {
        ShaderColor& color = mScreen.currentShaderColor;
        ShaderColor& dark  = mScreen.currentShaderDarkColor;
        color              = mColor;
        color.dirty        = true;
        dark               = mDark;
        dark.dirty         = true;
        mFlags.restore(mScreen);
    }
    SignTextState(SignTextState const&)            = delete;
    SignTextState& operator=(SignTextState const&) = delete;

private:
    /// The screen context's drawing flags (mce::MeshContext).
    struct Flags {
        bool ui, firstPerson, environmentalText, persistentUi, frameCapture, editorHighlight, movingBlock, dithering, alphaMaskedTint;
        static Flags of(ScreenContext const& s) {
            return {s.isDrawingUI, s.isDrawingFirstPersonObjects, s.isDrawingEnvironmentalText, s.isDrawingPersistentUIElement, s.isDoingFrameCapture,
                    s.isDrawingEditorSelectionHighlight, s.isDrawingMovingBlock, s.isDitheringEnabled, s.isAlphaMaskedTintEnabled};
        }
        void restore(ScreenContext& s) const {
            s.isDrawingUI                       = ui;
            s.isDrawingFirstPersonObjects       = firstPerson;
            s.isDrawingEnvironmentalText        = environmentalText;
            s.isDrawingPersistentUIElement      = persistentUi;
            s.isDoingFrameCapture               = frameCapture;
            s.isDrawingEditorSelectionHighlight = editorHighlight;
            s.isDrawingMovingBlock              = movingBlock;
            s.isDitheringEnabled                = dithering;
            s.isAlphaMaskedTintEnabled          = alphaMaskedTint;
        }
    };
    ScreenContext& mScreen;
    ShaderColor    mColor;
    ShaderColor    mDark;
    Flags          mFlags;
};

std::atomic<bool> gDrawn{false};

/// SignRenderer.isOutlineVisible without its black text case: the local player scoping in first
/// person, or the camera entity within 16 blocks of the sign's centre (distanceToSqr of the entity's
/// position, its feet).
bool outlineVisible(render::RenderContext& ctx, LocalPlayer& player, BlockPos const& pos) {
    IClientInstance& client = ctx.actors->mClientInstance;
    if (client.getOptions().getPlayerViewPerspective() == 0 && player.isScoping()) return true;
    Actor const* camera = client.getCameraActor();
    if (!camera) camera = &player;
    Vec3 const feet = camera->getFeetPos();
    float const dx = feet.x - (static_cast<float>(pos.x) + 0.5f), dy = feet.y - (static_cast<float>(pos.y) + 0.5f), dz = feet.z - (static_cast<float>(pos.z) + 0.5f);
    return dx * dx + dy * dy + dz * dz < kOutlineDistance * kOutlineDistance;
}

/// The block renderer of every sign family block.
void drawSign(render::RenderContext& ctx, BlockPos const& pos, Block const& block, float) {
    if (!ctx.actors) return;
    auto* player = currentPlayer();
    if (!player) return;
    auto text = sharedTextAt(static_cast<int>(player->getDimensionId()), pos.x, pos.y, pos.z);
    if (!text || (text->front.empty() && text->back.empty())) return;
    auto const* e = fam::entryOf(block);
    if (!e) return;
    float cx = static_cast<float>(pos.x) + 0.5f - ctx.camera.x, cy = static_cast<float>(pos.y) + 0.5f - ctx.camera.y, cz = static_cast<float>(pos.z) + 0.5f - ctx.camera.z;
    float d2 = cx * cx + cy * cy + cz * cz;
    if (d2 > kViewDistance * kViewDistance) return;
    bool const hanging  = e->family == fam::Family::HangingSign || e->family == fam::Family::WallHangingSign;
    bool const standing = e->family == fam::Family::Sign;
    // translateSign, in camera space
    glm::mat4 pose = glm::translate(glm::mat4{1.0f}, glm::vec3{static_cast<float>(pos.x) - ctx.camera.x, static_cast<float>(pos.y) - ctx.camera.y, static_cast<float>(pos.z) - ctx.camera.z});
    float     yRot = -fam::signYaw(block, *e); // SignBlock.getYRotationDegrees, as the server's display entities
    if (hanging) {
        pose = glm::translate(pose, glm::vec3{0.5f, 0.9375f, 0.5f});
        pose = glm::rotate(pose, glm::radians(yRot), glm::vec3{0, 1, 0});
        pose = glm::translate(pose, glm::vec3{0.0f, -0.3125f, 0.0f});
    } else {
        pose = glm::translate(pose, glm::vec3{0.5f, 0.75f * 0.6666667f, 0.5f});
        pose = glm::rotate(pose, glm::radians(yRot), glm::vec3{0, 1, 0});
        if (!standing) pose = glm::translate(pose, glm::vec3{0.0f, -0.3125f, -0.4375f});
    }
    // isOutlineVisible: the local player looking through a spyglass in first person, or the camera
    // entity (its feet) within 16 blocks of the sign's centre
    bool outlineNear = outlineVisible(ctx, *player, pos);
    auto handle      = ctx.actors->getFontHandle();
    auto& font       = handle->getFont();
    // the light at the sign even when only glowing text is drawn (the glow material ignores it; a pack
    // without that material falls back to the lit one)
    SignTextState state(ctx, pos, true);
    // the eye in the camera-relative space of the pose: the inverse of the view x world matrices
    // applied to the view space's origin (first person: about the origin; third person: the camera
    // behind or in front of the player)
    glm::mat4 const viewWorld = ctx.screen.camera.viewMatrixStack.get().top()._m.get() * ctx.screen.camera.worldMatrixStack.get().top()._m.get();
    glm::vec3 const eye       = glm::vec3(glm::inverse(viewWorld) * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
    drawSide(ctx, font, pose, text->front, true, hanging, outlineNear, eye);
    drawSide(ctx, font, pose, text->back, false, hanging, outlineNear, eye);
    if (!gDrawn.exchange(true)) log().info("families: sign text drawn on the board ({} at {} {} {})", e->id, pos.x, pos.y, pos.z);
}

// ---- the display entities' name tags --------------------------------------------------------------------

/// An actor of the client's own level (not the integrated server's).
bool clientActor(Actor const& a) {
    if (a.isClientSide()) return true;
    auto level = ll::service::getLevel();
    return !level || &a.getLevel() != level.as_ptr();
}

} // namespace

/// The client draws the sign text itself: the name tag of a display entity is not shown once the
/// client has the text of its sign. The server names a sign's display entities in its text (their
/// unique ids, which the client's copies of the entities share: AddActorPacket), so an entity is
/// matched to its own sign only: one whose sign's text has not arrived keeps its tag, whatever its
/// neighbours have.
bool hidesSignName(Actor const& actor) {
    if (!net::client::connected() || actor.getTypeName() != fam::kSignTextDisplayType || !clientActor(actor) || !actor.hasUniqueID()) return false;
    auto t = sharedTextOfDisplay(actor.getOrCreateUniqueID().rawID);
    return t && !(t->front.empty() && t->back.empty());
}

} // namespace gf::client::families

// The name tag of an actor is extracted here for the frame (one call per actor with a visible name).
LL_TYPE_INSTANCE_HOOK(
    FamiliesSignNameTagHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    &ActorRenderDispatcher::extractRenderTextObjects,
    std::vector<NameTagRenderObject>,
    Font&               font,
    Tessellator&        tessellator,
    Actor&              actor,
    std::string const&  str,
    Vec3                camTargetPos,
    float               a,
    mce::Color          color
) {
    try {
        if (gf::client::families::hidesSignName(actor)) {
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true)) gf::log().info("families: the sign text display entities show no name tag (the client mod draws the text)");
            return {};
        }
    } catch (...) {}
    return origin(font, tessellator, actor, str, camTargetPos, a, color);
}

namespace gf::client::families {

namespace {

class SignTextModule {
public:
    void install() {
        FamiliesSignNameTagHook::hook();
        net::client::on("families.sign_text", onText);
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>([](auto&) { clearTexts(); });
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { clearTexts(); });
        int n = 0;
        for (auto family : {fam::Family::Sign, fam::Family::WallSign, fam::Family::HangingSign, fam::Family::WallHangingSign}) {
            for (auto const* e : fam::entriesOf(family)) {
                render::registerBlockRenderer(e->id, drawSign);
                ++n;
            }
        }
        log().info("families: sign text drawn by the client mod on {} sign blocks", n);
    }

    void uninstall() {
        for (auto family : {fam::Family::Sign, fam::Family::WallSign, fam::Family::HangingSign, fam::Family::WallHangingSign})
            for (auto const* e : fam::entriesOf(family)) render::unregisterBlockRenderer(e->id);
        if (mJoin) ll::event::EventBus::getInstance().removeListener(mJoin);
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        mJoin = nullptr;
        mExit = nullptr;
        FamiliesSignNameTagHook::unhook();
        clearTexts();
    }

private:
    ll::event::ListenerPtr mJoin;
    ll::event::ListenerPtr mExit;
};

} // namespace

} // namespace gf::client::families

GF_REGISTER_MODULE(gf::client::families::SignTextModule);

// The client mod's copy of a sign's text (what it draws on the board) follows the server's: local
// worlds of the client build, where the local player is a companion of the integrated server.
GF_REGISTER_SELFTEST("families.sign_text_client", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> {
    namespace fam = gf::feature::families;
    using gf::client::families::signTextAt;
    if (!gf::net::client::connected()) {
        ctx.info("the client mod is not a companion of this server: skipped");
        co_return;
    }
    if (!gf::game::blockWith("gf_test:family_sign")) {
        ctx.info("no gf_test packs in this build: skipped");
        co_return;
    }
    auto box = co_await ctx.flatArea(6182, 5);
    auto p   = ctx.testPos(6186, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto below = p;
    below.y -= 1;
    ctx.require(ctx.setBlock(below, "minecraft:stone"), "ground");
    ctx.require(ctx.setBlock(p, "gf_test:family_sign", {{fam::state::Rotation, 8}}), "a sign");
    co_await ctx.ticks(1);
    ctx.check(gf::signs::setText(p, {"client", "copy"}), "the server writes the sign");
    auto text = signTextAt(p.dim, p.x, p.y, p.z);
    for (int i = 0; i < 100 && !text; ++i) {
        co_await ctx.ticks(1);
        text = signTextAt(p.dim, p.x, p.y, p.z);
    }
    ctx.check(text && text->front.lines[0] == "client" && text->front.lines[1] == "copy" && text->front.color == "black" && !text->front.glowing && text->back.empty(),
              "the client mod got the text to draw");
    // the sign's display entity, named by its unique id, which the client's copy of it shares: its
    // name tag is hidden there (asked on the client thread, which owns the client's level)
    auto*        display = fam::detail::signDisplay(p, true);
    std::int64_t id      = display ? display->getOrCreateUniqueID().rawID : 0;
    ctx.check(display && text && text->display == std::vector<std::int64_t>{id}, "the text names the sign's display entity (" + std::to_string(id) + ")");
    // each ask has its own answer slot: a late answer to an earlier ask never counts for a later one
    auto hidden = std::make_shared<std::atomic<int>>(0); // 0 asked, 1 hidden, 2 shown, 3 no such actor on the client
    for (int i = 0; i < 20 && id != 0 && hidden->load() != 1; ++i) {
        hidden = std::make_shared<std::atomic<int>>(0);
        ll::thread::ClientThreadExecutor::getDefault().execute([hidden, id] {
            auto*         player = gf::client::currentPlayer();
            ActorUniqueID uid;
            uid.rawID = id;
            Actor* a  = player ? player->getLevel().fetchEntity(uid, false) : nullptr;
            hidden->store(!a ? 3 : gf::client::families::hidesSignName(*a) ? 1 : 2);
        });
        for (int t = 0; t < 40 && hidden->load() == 0; ++t) co_await ctx.ticks(1);
        if (hidden->load() != 1) co_await ctx.ticks(5);
    }
    ctx.check(hidden->load() == 1, "the client's copy of the display entity shows no name tag (" + std::to_string(hidden->load()) + ")");
    ctx.setBlock(p, "minecraft:air");
    for (int i = 0; i < 100 && signTextAt(p.dim, p.x, p.y, p.z); ++i) co_await ctx.ticks(1);
    ctx.check(!signTextAt(p.dim, p.x, p.y, p.z), "and forgets it when the sign goes");
    (void)box;
});

#endif // GF_CLIENT

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
// Font::drawCached calls and shader state): ActorShaderManager::setupShaderParameters at the sign
// first (the light at the block, which the lit text material multiplies by), then each line with
// the darken path on (the text colour times the dark colour given to drawCached: the only way these
// materials take a colour), the lit material for unlit text and the glow material (no light) for
// glowing text and its outline.
// The server's display entities (gloamingforest:sign_text: the name tag vanilla clients read) show
// no name tag here while the server sends the text: ActorRenderDispatcher::extractRenderTextObjects
// (the name tag of one actor for the frame) gives nothing for them on the client of a companion
// (the only hook of name tags; F11 owns it).

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
    std::mutex                                         mutex; ///< the client thread writes, the render thread reads
    std::map<Key, std::shared_ptr<SignText const>> texts; ///< immutable: readers keep theirs
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
                if (auto k = keyOf(p)) s.texts.erase(*k);
        if (auto list = msg.find("signs"); list != msg.end() && list->is_array()) {
            for (auto const& e : *list) {
                if (!e.is_object()) continue;
                auto k = keyOf(e.value("pos", Json()));
                if (!k) continue;
                s.texts[*k] = std::make_shared<SignText const>(SignText{sideOf(e.value("front", Json())), sideOf(e.value("back", Json()))});
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

/// Java SignBlock.getYRotationDegrees: a standing or ceiling sign's rotation x 22.5, a wall
/// sign's facing (Direction.toYRot: south 0, west 90, north 180, east 270).
float yRotationDegrees(Block const& b, fam::Entry const& e) {
    if (e.family == fam::Family::Sign || e.family == fam::Family::HangingSign) return static_cast<float>(fam::intState(b, fam::state::Rotation).value_or(0)) * 22.5f;
    static constexpr float toYRot[6]{0, 0, 180, 0, 90, 270};
    int f = fam::facingOf(b);
    return f >= 0 && f < 6 ? toYRot[f] : 0.0f;
}

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

/// One side of a sign: SignRenderer.renderSignText in the pose `pose` (the sign's, in camera space).
void drawSide(render::RenderContext& ctx, Font& font, glm::mat4 const& pose, SignSide const& side, bool front, bool hanging, bool outlineNear) {
    if (side.empty()) return;
    auto& rc = *ctx.actors;
    // translateSignText
    glm::mat4 m = pose;
    if (!front) m = glm::rotate(m, glm::radians(180.0f), glm::vec3{0, 1, 0});
    m             = glm::translate(m, hanging ? glm::vec3{0.0f, -0.32f, 0.073f} : glm::vec3{0.0f, 0.33333334f, 0.046666667f});
    float const f = 0.015625f * (hanging ? 0.9f : 0.6666667f);
    m             = glm::scale(m, glm::vec3{f, -f, f});
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
        ref.mat->_m.get() = ref.mat->_m.get() * glm::translate(m, glm::vec3{0.0f, 0.0f, outline ? 0.1f : 0.0f});
        drawLine(font, ctx.screen, line, x, y, textColor, scale, material);
    }
}

/// The screen context's text state for one sign, restored afterwards: SignRenderer's light at the
/// sign (ActorShaderManager::setupShaderParameters at the block, as SignRenderer::render sets it
/// before the board and the text: the light the lit text material multiplies by, Java's packedLight)
/// and environmental text.
class SignTextState {
public:
    SignTextState(render::RenderContext& ctx, BlockPos const& pos, bool lit)
    : mScreen(ctx.screen),
      mColor(static_cast<ShaderColor&>(ctx.screen.currentShaderColor)),
      mDark(static_cast<ShaderColor&>(ctx.screen.currentShaderDarkColor)),
      mEnvironmental(ctx.screen.isDrawingEnvironmentalText) {
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
        mScreen.isDrawingEnvironmentalText = mEnvironmental;
    }
    SignTextState(SignTextState const&)            = delete;
    SignTextState& operator=(SignTextState const&) = delete;

private:
    ScreenContext& mScreen;
    ShaderColor    mColor;
    ShaderColor    mDark;
    bool           mEnvironmental;
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
    float     yRot = -yRotationDegrees(block, *e);
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
    drawSide(ctx, font, pose, text->front, true, hanging, outlineNear);
    drawSide(ctx, font, pose, text->back, false, hanging, outlineNear);
    if (!gDrawn.exchange(true)) log().info("families: sign text drawn on the board ({} at {} {} {})", e->id, pos.x, pos.y, pos.z);
}

// ---- the display entities' name tags --------------------------------------------------------------------

constexpr char kDisplayType[] = "gloamingforest:sign_text";

/// An actor of the client's own level (not the integrated server's).
bool clientActor(Actor const& a) {
    if (a.isClientSide()) return true;
    auto level = ll::service::getLevel();
    return !level || &a.getLevel() != level.as_ptr();
}

} // namespace

/// The client draws the sign text itself: the display entity's name tag is not shown.
bool hidesSignName(Actor const& actor) {
    if (!net::client::connected() || actor.getTypeName() != kDisplayType) return false;
    return clientActor(actor);
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
    ctx.setBlock(p, "minecraft:air");
    for (int i = 0; i < 100 && signTextAt(p.dim, p.x, p.y, p.z); ++i) co_await ctx.ticks(1);
    ctx.check(!signTextAt(p.dim, p.x, p.y, p.z), "and forgets it when the sign goes");
    (void)box;
});

#endif // GF_CLIENT

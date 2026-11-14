#pragma once

// F12 additions to the client contracts (rule R18: new files only; folded by A8 in W2b).
// Client build only.
//
// - render::RenderContext, declared opaque in client/render/Render.h, is defined here so renderers
//   can reach the engine's contexts (tessellator, matrices, camera) beyond Lines / drawGeo; the
//   threads and spaces every registry runs in; removal of registrations (fixtures, selftests); the
//   light a renderer gets (what a renderer finds at entry, lightAt, setupLight); name tags
//   (hideNameTag by type or predicate, showNameTag, nameTagHidden).
// - tint: tints built on the game's own colours at the position (GameColors, GameTintFn).
// - ui: textures in GUI pixels (upstream's GUI skins), the item stacks of a ContainerScreen's slots, the
//   hit tests of its slots and of the player inventory drawn inside an upstream skin, an item's hover text.
// - toast: literal lines (LiteralToastSpec: Java's Component.literal, e.g. F10's
//   MissingAdvancementToast showing an advancement's title); upstream's two toast styles
//   (LockedBiomeToast, MissingAdvancementToast) and their colours;
//   toasts are not drawn while the GUI is hidden (F1), as Java's ToastComponent; the frame texture;
//   lookups by key or literal title (tests).
// - hud::guiHidden: Java's options.hideGui for HUD layers (upstream's overlays check it themselves).
// - particles::spawn: runs a native particle factory; F12 connects the registry to F10's client
//   handler of fx.particle (gf::fx::particle) through gf::fx::client::setNativeSink
//   (feature/ext/combat/FxNative.h); what a factory gets from fx.particle and when the engine spawns
//   the particle (molang variables).
// - maps::setDecorations: upstream map decorations drawn over a map (MagicMapPacket / MazeMapPacket data),
//   with upstream's two IMapDecorationRenderers (the conquered red X, the player icon on top) and the
//   vanilla map icons.
// - camera: what camera::shake does (upstream's degrees) and its state (tests).
// - keys: when registerKey must run (mod load) and what happens to a key registered later; the
//   current binding of a key (the player's remapping in the controls).
// - hud: hiding effect icons of the game's HUD effect list (Java's MobEffectInstance#showIcon false:
//   upstream's hidden effects and the carriers of custom effects).
// - render: world text (Java's TextDisplay: billboarded text with a background, in the world).
// - textures: every texture name these APIs take is a resource pack path without extension, or an
//   absolute file path (with its extension), which is read from the disk (development fixtures).

#ifdef GF_CLIENT

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "client/render/Render.h"
#include "client/ui/Ui.h"
#include "client/world/Sky.h"

class Actor;
class BaseActorRenderContext;
class ScreenContext;
class MinecraftUIRenderContext;

namespace gf::client::tint {
/// The game's own colours at the position being tinted: Bedrock's tint methods, the biomes' colours
/// blended around the position as the game blends them for its own blocks (Java's
/// BiomeColors.getAverageFoliageColor / getAverageGrassColor / getAverageWaterColor). Each is
/// computed when asked. nullopt without a world (a tint asked outside a chunk mesh, e.g. selftests):
/// upstream then uses its colours without a world (FoliageColor.getDefaultColor, GrassColor.getDefaultColor).
class GameColors {
public:
    [[nodiscard]] virtual std::optional<unsigned> foliage() const = 0; ///< tint method "default_foliage"
    [[nodiscard]] virtual std::optional<unsigned> grass() const   = 0; ///< tint method "grass"
    [[nodiscard]] virtual std::optional<unsigned> water() const   = 0; ///< tint method "water"

protected:
    ~GameColors() = default;
};
/// A tint that builds on the game's colours at the position (upstream's ColorHandler: CANOPY_COLORIZER of
/// the biome foliage colour, the fiddlehead's grass colour, ...). Same threads and rules as TintFn;
/// a later registration for the block (either kind) replaces the earlier one.
using GameTintFn = std::function<unsigned(BlockPos const& pos, GameColors const& game)>;
void registerBlock(std::string const& blockId, GameTintFn fn);
} // namespace gf::client::tint

namespace gf::client::render {

// What a renderer finds at entry (block, item and entity renderers, onWorld callbacks):
// - the world matrix of its space (world space relative to the camera, or the item's space; see
//   RenderContext below), which it may push onto and must leave balanced;
// - the place of its light, RenderContext::lightAt / fullBright: a block renderer the light at its
//   block (Java's BlockEntityRenderer packedLight), an entity renderer and the items an entity holds
//   or wears the light at the entity's eyes (Java's getPackedLightCoords), an item on the ground the
//   light at the item, an item in a frame the light at the frame block (full bright in a glow
//   frame), an item in a GUI slot full bright (Java's GUI items). An onWorld callback gets no
//   lightAt (nullopt): drawGeo lights each geometry it draws at that geometry's own translation (a
//   world position), wherever in the world it is; a callback that draws with the game's lit
//   materials itself calls setupLight(ctx, place) for each place it draws at. The light itself (the entity constants of the actor materials:
//   ActorShaderManager::setupShaderParameters, the block and sky light the lit materials multiply
//   by) is applied where it is drawn with: drawGeo sets it before every draw (at lightAt, else at
//   its own position), so neither the draw before the renderer nor draws in between (text, the
//   game's own renderers) change the light of a geometry, and a renderer that draws nothing costs no
//   light lookup. A renderer that draws with the game's lit materials itself calls
//   setupLight(ctx, *ctx.lightAt, ctx.fullBright) before it draws; what the constants hold at
//   entry is unspecified (the last draw's);
// - the screen context's shader colour and dark colour as the game left them (put back after every
//   renderer, whatever the light setup or the renderer changed); materials and the tessellator are
//   whatever the game left (Lines and drawGeo set what they draw with);
// - a RenderContext of its own: what one renderer changes in it (lightAt, throughWalls, ...) never
//   reaches the next.

// Which blocks a block renderer runs for: the positions of its block are recorded when the game builds
// the render chunk (16^3 section) that holds them (RenderChunkBuilder::build, whatever tessellator then
// meshes the block), and a rebuild of the section (a block placed or broken in it) replaces them.
// Register block renderers at install (before the world's chunks are meshed): a section built before
// the registration is not scanned again until it is rebuilt.

/// Name tags: whether the game draws the name tag (the text over an entity) of `entity`. Render
/// thread, once per entity with a visible name tag per frame: keep it cheap; the entity is the
/// client's copy (its unique id is the server's).
using NameTagFn = std::function<bool(Actor const& entity)>;
/// Hides the name tag of every entity of type `entityId` (`when` empty), or of those for which
/// `when` returns true. A later call for the same type replaces the earlier one.
void hideNameTag(std::string const& entityId, NameTagFn when = {});
/// Hides the name tag of every entity for which `when` returns true, whatever its type. Returns a
/// handle for showNameTag (0: `when` was empty, nothing registered).
std::size_t hideNameTag(NameTagFn when);

/// Armour hiding (upstream's ArmorHooks#cancelArmorRendering / CancelElytraRenderingTransformer: the
/// emperor's cloth): whether the game draws `stack` worn in an armour slot (head, chest, legs, feet
/// and the body slot of horses / wolves; the elytra is a chest item). Bedrock draws worn armour as
/// attachables; the check is DataDrivenModel::shouldRenderAttachableOnActor, asked per worn stack and
/// frame on the render thread (keep it cheap; the stack is the client's copy, with the item data the
/// server sent). Held items (main hand, off hand) are never hidden. A stack is hidden when any
/// registered function returns true. Returns a handle for showArmor (0: `when` was empty).
/// The v2 actor renderer (DataDrivenRendererV2, which draws players and data-driven actors on 1.26.51)
/// does not call that check: for it a hidden worn stack is taken out of the wearer's armour container
/// (the client's copy) while the renderer takes the actor for the frame (tryAddActor) and put back
/// when the frame is generated (generateFrame), so the renderer bakes no attachable for it
/// (armorChecks().v2Hidden counts them; the hidden stacks' functions run once per actor and frame).
using ArmorFn = std::function<bool(ItemStack const& stack)>;
std::size_t hideArmor(ArmorFn when);
/// Removes a hideArmor function by its handle; false when there was none.
bool showArmor(std::size_t handle);
/// Whether a hideArmor function hides `stack` (what the attachable check asks; tests).
[[nodiscard]] bool armorHidden(ItemStack const& stack);
/// How many worn stacks the game's attachable check would draw (its own answer true: not hidden by
/// the game's own rules, e.g. spectators), and how many of them a hideArmor function hid, since the
/// process started (tests and e2e: the check is reached).
struct ArmorChecks {
    std::uint64_t asked    = 0;
    std::uint64_t hidden   = 0;
    std::uint64_t v2Hidden = 0; ///< worn stacks kept from the v2 actor renderer
};
[[nodiscard]] ArmorChecks armorChecks();

/// World text (Java's Display.TextDisplay as upstream uses it: the final castle gazebo's "WIP" lines): text
/// with an optional background, billboarded or fixed, drawn with the game's font in the world.
struct WorldText {
    enum class Billboard { Fixed, Vertical, Horizontal, Center };
    int                      dim = 0;            ///< the dimension it is drawn in (the local player's)
    Vec3                     pos{};              ///< world position (the text's bottom centre, as TextDisplay)
    std::vector<std::string> lines;              ///< lines (each wrapped at lineWidth font pixels; § formatting kept)
    Billboard                billboard  = Billboard::Fixed;
    float                    yaw        = 0;     ///< Fixed / Horizontal / Vertical: the entity's yRot (degrees)
    float                    pitch      = 0;     ///< Fixed / Vertical: the entity's xRot
    float                    scale      = 1.0f;  ///< Display#transformation scale (one font pixel = 0.025 block x scale)
    unsigned                 color      = 0xFFFFFFFF; ///< ARGB text colour (alpha: TextDisplay#textOpacity)
    unsigned                 background = 0x40000000; ///< ARGB background (TextDisplay's default; 0: none)
    bool                     shadow     = false;
    bool                     seeThrough = false; ///< drawn through blocks
    int                      lineWidth  = 200;   ///< TextDisplay#lineWidth
    float                    viewRange  = 64.0f; ///< blocks (Display#viewRange 1.0 x 64)
    bool                     fullBright = false; ///< Display#brightnessOverride 15 (else the light at pos)
};
/// Shows a world text on this client until removed or the client leaves the world; returns its handle.
std::size_t addWorldText(WorldText text);
/// Replaces a world text (false: unknown handle).
bool setWorldText(std::size_t handle, WorldText text);
/// Removes a world text (false: unknown handle).
bool removeWorldText(std::size_t handle);
/// Draws one world text now (an entity renderer drawing the text of its entity, an onWorld callback);
/// the dimension is not checked. False when nothing was drawn (no world pass, out of range).
bool drawWorldText(RenderContext& ctx, WorldText const& text);
/// How many world texts were drawn since the process started (tests and e2e).
[[nodiscard]] std::uint64_t worldTextsDrawn();

/// The per-frame render context handed to renderers (valid during the callback only).
///
/// Threads: block, item and entity renderers, onWorld callbacks and sky providers run on the
/// render thread, one at a time. Tint functions (gf::client::tint) run on the chunk builder
/// threads, several at once, while chunks are meshed: they must be thread-safe, must not read the
/// world through the level (only their position) and should be cheap (one call per tinted block per
/// mesh). The game asks for a position-dependent tint only for blocks whose resource pack geometry
/// has a tint method (`minecraft:material_instances` "tint_method", e.g. "default_foliage" or
/// "grass"); other blocks never reach a registered tint function.
///
/// Item spaces (`itemSpace` true; drawGeo's translation and Lines' points are in that space):
/// - FirstPerson / ThirdPerson: ItemInHandRenderer::renderItem's matrix after the game drew the item;
/// - Ground: the item entity's position (no rotation);
/// - Fixed (item frames): Java's ItemFrameRenderer before ItemRenderer.render with FIXED: the frame
///   block's centre turned to the frame (xRot, 180 - yRot), 7/16 towards the wall, the item rotation
///   (45 degree steps about z) and scale 0.5;
/// - Head (items on the head slot of humanoids): Java's CustomHeadLayer after translateToHead: the
///   head's pose, at the head's centre (a quarter block above the neck), y up, facing forward,
///   1 unit = 0.625 block (times the entity's scale).
class RenderContext {
public:
    explicit RenderContext(ScreenContext& screenContext) : screen(screenContext) {}

    ScreenContext&          screen;           ///< tessellator, camera matrix stacks
    /// The actor render context drawGeo draws with: every world pass (blocks, entities, items,
    /// onWorld) and the GUI item pass (ItemContext::Gui, built on the UI's screen context); null for
    /// the sky. Tell the GUI pass from the world passes by `ui` (non-null only there).
    BaseActorRenderContext* actors = nullptr;
    Vec3                    camera{};         ///< camera position, world coordinates
    float                   partialTicks = 0;
    bool                    throughWalls = false; ///< Lines / drawGeo skip the depth test
    /// Item contexts other than Gui: the world matrix is the item's own space (see above); drawGeo's
    /// translation and Lines' points are then relative to the item origin.
    bool                    itemSpace = false;
    /// Half the width of Lines' bars in units of the current space (1/64 block in the world).
    float                   lineHalfWidth = 1.0f / 64;
    /// ItemContext::Gui: the UI context and the slot's rectangle in GUI pixels (null in every other
    /// pass).
    MinecraftUIRenderContext* ui = nullptr;
    float                     guiX = 0, guiY = 0, guiSize = 16;
    /// Where the light of this renderer is taken (world coordinates; "What a renderer
    /// finds at entry"): the block of a block renderer, the eyes of the entity of an entity renderer
    /// and of the holder / wearer of an item, the item entity on the ground, the frame block of a
    /// framed item; nullopt for onWorld. drawGeo draws with the light here (nullopt: at its own
    /// translation, which is then a world position). A renderer may move it (e.g. to light a
    /// geometry by the block above).
    std::optional<Vec3> lightAt;
    /// Full brightness instead of the light at lightAt (a glow item frame: Java's 15728880).
    bool fullBright = false;
};

/// Sets the light of the actor materials (ActorShaderManager::setupShaderParameters: the entity
/// constants the moving-block / entity materials multiply by) to the block and sky light at the
/// world position `at` (full bright with `fullBright`), as the game does before it draws a block
/// actor. For renderers that draw with the game's lit materials themselves (drawGeo does it on its
/// own). False when there is no world pass (ctx.actors null) or no client world.
bool setupLight(RenderContext& ctx, Vec3 const& at, bool fullBright = false);
/// How many times the light was set (setupLight, drawGeo before each draw; tests and e2e).
[[nodiscard]] std::uint64_t lightSetups();

/// Number of block positions currently tracked for block renderers (tests).
[[nodiscard]] std::size_t trackedBlockCount();
/// Whether a block renderer is registered for `blockId` (tests).
[[nodiscard]] bool hasBlockRenderer(std::string const& blockId);
/// Whether an item renderer of `itemId` draws in `where`.
[[nodiscard]] bool hasItemRenderer(std::string const& itemId, ItemContext where);
/// The tint of `blockId` at `pos` from the registry (0xRRGGBB; nullopt: none registered), with the
/// game's colours of `game` (default: none, as without a world).
[[nodiscard]] std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z);
[[nodiscard]] std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z, tint::GameColors const& game);
/// Removals (fixtures and selftests); false when nothing was registered.
bool unregisterBlockRenderer(std::string const& blockId);
bool unregisterItemRenderer(std::string const& itemId);
bool unregisterEntityRenderers(std::string const& entityId);

/// Shows the name tags of `entityId` again (hideNameTag by type); false when they were not hidden.
bool showNameTag(std::string const& entityId);
/// Removes a hideNameTag(predicate) by its handle; false when there was none.
bool showNameTag(std::size_t handle);
/// Whether the name tag of `entity` is hidden by a hideNameTag rule (what the name tag pass asks;
/// tests).
[[nodiscard]] bool nameTagHidden(Actor const& entity);
/// The hideNameTag rule of type `entityId` (nullopt: none; an empty function: every entity of the
/// type), so a selftest can put back what another module registered.
[[nodiscard]] std::optional<NameTagFn> nameTagRule(std::string const& entityId);

} // namespace gf::client::render

namespace gf::client::tint {
/// Removes the tint of `blockId` (fixtures and selftests); false when none was registered.
bool unregisterBlock(std::string const& blockId);
} // namespace gf::client::tint

namespace gf::client::ui {

/// Draws a rectangle of a texture (resource pack path without extension, e.g.
/// "textures/gui/guigoblintinkering") in GUI pixels, like Java's GuiGraphics.blit: `dst` on the
/// canvas (canvas origin applies), source (u, v, w, h) in texture pixels of a `texW` x `texH`
/// texture. Returns false when the texture can't be loaded.
bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW = 256, float texH = 256);
/// How a blit mixes with what is under it: Normal is alpha blending; Invert is Java's
/// RenderSystem.blendFuncSeparate(ONE_MINUS_DST_COLOR, ONE_MINUS_SRC_COLOR, ONE, ZERO) of the
/// crosshair (upstream's QuestingRamIndicatorOverlay), Bedrock's own crosshair material "ui_crosshair"
/// (OneMinusDestColor / OneMinusSrcColor): white texels invert the screen, black ones leave it.
enum class Blend { Normal, Invert };
/// blit with a blend mode (Invert always draws a textured mesh, whatever blitMethod says).
bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW, float texH, Blend blend);
/// A whole texture (a GUI sprite, Java's GuiGraphics.blitSprite) over `dst` with `blend`; false
/// while it is loading or when it is missing (logged once).
bool blitSprite(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, Blend blend = Blend::Normal);
/// A nine-slice of a `texW` x `texH` texture over `dst` (the game's nineslice_size): the corners of
/// `slice` texture pixels drawn 1:1, the edges and the middle stretched. False when the texture
/// can't be loaded.
bool blitNineSlice(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float texW, float texH, float slice);
/// How blit draws: 0 the UI image batch flushed with "ui_flush", 1 flushed with
/// "ui_textured_and_glcolor", 2 a textured mesh with the "ui_textured" material (default; spike S5).
[[nodiscard]] int blitMethod();
void              setBlitMethod(int method);
/// The size of a texture in pixels (nullopt when it can't be loaded).
[[nodiscard]] std::optional<std::pair<int, int>> textureSize(std::string const& texture);

/// Sets the stacks shown in a ContainerScreen's slots: {"<slot index>": item json (items::toJson)}.
void setContainerItems(screen::ContainerScreen& screen, nlohmann::json const& items);
/// The container slot index (SlotPos::index) at panel pixels (mx, my), -1 for none.
[[nodiscard]] int containerSlotAt(screen::ContainerScreen const& screen, float mx, float my);
/// The player inventory slot (0..8 hotbar, 9..35 main) drawn inside the skin at panel pixels
/// (mx, my), -1 for none. upstream skins put the inventory at Java's (8, height - 82).
[[nodiscard]] int skinInventorySlotAt(screen::ContainerScreen const& screen, float mx, float my);
/// An item's full hover text as lines, as Java's getTooltipFromItem (name, then the game's lines:
/// enchantments, lore, the lines of tooltip::add and of gf::item::addTooltip); empty for an empty
/// stack. ContainerScreen's hover tooltips show it.
[[nodiscard]] std::vector<std::string> hoverLines(ItemStack const& stack);

/// Per-screen states of ContainerScreens (slot stacks, the title the skin draws) kept now (tests).
/// An entry lives as long as its screen: a ContainerScreen keeps an invisible widget (the first of
/// `widgets`; leave it there) whose destruction drops the state, so a screen that is covered (chat,
/// a modal) or not opened yet keeps its stacks. A state whose screen lost that widget (a subclass
/// cleared `widgets`) goes once its screen has not been the open one for 10 seconds.
[[nodiscard]] std::size_t containerStateCount();

/// Toasts waiting or shown (tests).
[[nodiscard]] std::size_t toastCount();
/// HUD layer ids in draw order (tests).
[[nodiscard]] std::vector<std::string> hudLayers();
/// Removes a HUD layer (fixtures).
void removeHudLayer(std::string const& id);
/// The tooltip lines the registry adds for `stack` (tests).
[[nodiscard]] std::vector<std::string> tooltipLines(ItemStack const& stack);
/// Removes the tooltip providers of `itemId` (fixtures and selftests); false when there were none.
bool removeTooltips(std::string const& itemId);

} // namespace gf::client::ui

namespace gf::client::hud {
/// Whether the player hid the GUI (F1; Java's options.hideGui). HUD layers are drawn either way, as
/// in Java where each upstream overlay checks hideGui itself (all but the portal overlay do).
[[nodiscard]] bool guiHidden();

/// One effect of the local player as the game's HUD effect list shows it (Bedrock's
/// HudMobEffectsRenderer: an icon per active effect).
struct EffectIcon {
    std::string id;            ///< "minecraft:<name>" (MobEffect::getNameById)
    int         amplifier = 0;
    int         duration  = 0; ///< remaining ticks (-1: infinite)
    bool        ambient   = false;
    bool        particles = true; ///< MobEffectInstance's effect-visible flag (the particles)
};
/// Hides HUD effect icons (Java's MobEffectInstance#showIcon false: U12's high jump, U11's haste,
/// the glide's slow falling): an effect whose icon any registered function hides is left out of the
/// HUD effect list for the draw only (the effect itself stays). Render thread, once per effect and
/// HUD frame: keep it cheap. Returns a handle for showEffectIcons (0: `when` was empty).
using EffectIconFn = std::function<bool(EffectIcon const& effect)>;
std::size_t hideEffectIcons(EffectIconFn when);
/// Removes a hideEffectIcons function; false when there was none.
bool showEffectIcons(std::size_t handle);
/// Whether a registered function hides `effect` (what the HUD asks; tests).
[[nodiscard]] bool effectIconHidden(EffectIcon const& effect);
/// The effect icons hidden from the HUD since the process started (tests and e2e).
[[nodiscard]] std::uint64_t effectIconsHidden();
} // namespace gf::client::hud

namespace gf::client::toast {
/// A toast with literal lines (Java's Component.literal; e.g. a name the server sent): each line is
/// the literal text when it is set, else the spec's translation key translated on this client (the
/// mod's strings, then the game's; an unknown key is shown as it is). Drawn as Java's ToastComponent
/// draws upstream's toasts (160 x 32, the icon at (6, 8), the title at (25, 7), the body at (25, 18)).
struct LiteralToastSpec {
    ToastSpec   spec;  ///< icon, keys (used for a line without literal text) and display time
    std::string title; ///< literal title, wins over spec.titleKey
    std::string body;  ///< literal body, wins over spec.bodyKey
};
/// A toast's text colours (ARGB, as Java's drawString).
struct Style {
    unsigned titleColor = 0xFFFFFF00; ///< LockedBiomeToast's title: -256
    unsigned bodyColor  = 0xFFFFFFFF;
};
/// upstream's toasts: LockedBiomeToast (title -256, body white; the contract's show() draws this style)
/// and MissingAdvancementToast (UPPER_TEXT 0xffffffff, the advancement title 0xffffff: white).
/// Both are shown for 10 s (ToastSpec::ms = 10000).
enum class Kind { LockedBiome, MissingAdvancement };
[[nodiscard]] constexpr Style styleOf(Kind kind) {
    return kind == Kind::MissingAdvancement ? Style{0xFFFFFFFF, 0xFFFFFFFF} : Style{};
}
/// Shows a toast with `style` (Java's ToastComponent: 5 slots, a queue, 600 ms slides; not drawn while
/// the GUI is hidden).
void show(ToastSpec const& spec, Style style);
/// Shows a toast with literal lines, in LockedBiomeToast's colours (show) or with `style`.
void show(LiteralToastSpec const& toast, Style style = {});
/// Removes the toasts whose titleKey or literal title is `title`, waiting or shown (selftests);
/// returns how many.
std::size_t cancel(std::string const& title);
/// The style of the first toast whose titleKey or literal title is `title`, waiting or shown (tests).
[[nodiscard]] std::optional<Style> styleOfToast(std::string const& title);
/// The two lines the first toast whose titleKey or literal title is `title` shows (the literal text,
/// else the key translated on this client), waiting or shown (tests).
[[nodiscard]] std::optional<std::pair<std::string, std::string>> linesOfToast(std::string const& title);
/// The frame every toast is drawn on, in place of Java's sprite toast/advancement (160 x 32, blitted
/// whole): the texture Bedrock draws its own toasts with (toast_screen.json's popup background
/// "textures/ui/greyBorder": the dark advancement-toast look, a black outline with cut corners, a
/// 2 px #555555 border and the #212121 body; a 16 x 16 nine-slice with 4 px corners), stretched to
/// the toast's 160 x 32 as the game's nine-slice does.
inline constexpr char kFrameTexture[] = "textures/ui/greyBorder";
inline constexpr int  kFrameSlice     = 4;  ///< nineslice_size of textures/ui/greyBorder.json
inline constexpr int  kFrameSize      = 16; ///< base_size

/// The toast slots every toast drawer shares (Java's ToastComponent.occupiedSlots: one table for every
/// toast, 5 slots of 32 GUI pixels from the top right). A unit that draws its own toasts (U14's
/// AdvancementToast) claims a slot when its toast starts sliding in and releases it once the toast slid
/// out; F12's queue takes its slots from the same table, so neither draws over the other.
inline constexpr int kSlotCount = 5;
/// The lowest free slot, now taken (0..kSlotCount - 1); -1 when every slot is taken (the caller's toast
/// waits). Any thread.
[[nodiscard]] int claimSlot();
/// Gives a slot back that this caller claimed, once (a slot that is not claimed, or out of range, is
/// ignored; F12's own slots are never freed by it). Leaving a level frees F12's own slots only: a
/// claimer releases its claims itself when it drops its toasts.
void releaseSlot(int slot);
/// The slots taken now, by F12's toasts and by claims (tests).
[[nodiscard]] int slotsTaken();
} // namespace gf::client::toast

namespace gf::client::particles {
/// Runs the native factory of `particleId`; false when none is registered (then the caller
/// spawns the data-driven particle).
bool spawn(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, nlohmann::json const& params = {});
/// What F10's fx.particle handler calls (gf::fx::client::setNativeSink, installed by F12): runs the
/// factory of `particleId` max(1, count) times (at most 256) with the message's velocity and params,
/// each particle offset by params "spread" [dx, dy, dz] as gf::fx does for vanilla clients (a
/// gaussian times the distance per axis, ServerLevel#sendParticles); false when no factory is
/// registered (then F10 spawns the resource pack effect).
bool spawnFromFx(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, int count, nlohmann::json const& params);
/// upstream's 29 particle ids (declared at load) and whether each has a factory.
[[nodiscard]] std::vector<std::pair<std::string, bool>> declared();
/// Removes the factory of `particleId` (fixtures and selftests); false when there was none.
bool unregisterNative(std::string const& particleId);
// When the client spawns a particle effect with a native factory (the server's SpawnParticleEffectPacket,
// e.g. /particle or F10's fx, and Level::addParticleEffect of the client level), the factory gets the emitter
// position, the velocity `variable.velocity` (x, y, z), else `variable.direction` (x, y, z) times
// `variable.speed` (default 1), else zero, and as params every molang variable of the effect by
// name without "variable." (numbers; structs as objects of their members: {"color": {"r", "g",
// "b", "a"}, "direction": {"x", "y", "z"}}). Senders (F10's fx for vanilla clients) use these names.
// A SpawnParticleEffectPacket is handled as the vanilla handler does: one for another dimension than
// the local player's is dropped; one bound to an actor (its actor id set) spawns at the actor's
// position plus the packet's position (an offset then), with params "actor" (the unique id), and one
// whose actor this client does not know is left to the vanilla handler.
} // namespace gf::client::particles

namespace gf::client::maps {
/// Extra (upstream) decorations drawn over map `mapId` with the icons of `setIcon`; replaces the
/// previous list (an empty list removes it). Map pixels -128..127 as MapItemSavedData. Drawn on top
/// of held maps (first person) and maps in item frames (there only the decorations that show on item
/// frames, DecorationStyle::onFrame), as Java's MapRenderer: an 8 x 8 map pixel
/// quad at (x / 2 + 64, z / 2 + 64) turned by rotation * 22.5 degrees, in the map space the game
/// uses when it looks the map up (MapRenderer::_getMapInstance inside the first-person pass, an
/// item in hand or an item frame; MapRenderer::render itself cannot be hooked, spike S5).
/// `Decoration::name` is kept (view()) but not drawn on the map: upstream's map decorations
/// (GloamingMagicMapData, GloamingMazeMapData) carry no names.
void setDecorations(std::int64_t mapId, std::vector<Decoration> decorations);
/// The upstream decorations of `mapId` (empty: none).
[[nodiscard]] std::vector<Decoration> decorationsOf(std::int64_t mapId);
/// The texture registered for a decoration type ("" for none).
[[nodiscard]] std::string iconOf(std::string const& decorationType);

/// upstream's IMapDecorationRenderers for one decoration (Java draws them only on GloamingMagicMapData; the caller
/// sets them for magic maps).
struct DecorationStyle {
    /// ConqueredMapIconRenderer (structure icons of conquered landmarks): the icon is drawn and the
    /// red X (MapDecorationTypes.RED_X: the X of the vanilla map icon atlas) over it, at the icon's
    /// place turned with it, scaled 2, translated (-1, -1, -0.005), a 2 x 2 quad at depth -0.095.
    bool conquered = false;
    /// MagicMapPlayerIconRenderer (the player icon of magic maps): the icon at depth -0.3 instead of
    /// its place in the list, so it lies on top of every other decoration and red X.
    bool topmost = false;
    /// Java's MapDecorationType#showOnItemFrame: whether the decoration is drawn on a map in an item
    /// frame (MapRenderer skips the others there; held maps draw every decoration). nullopt: the
    /// type's own value (showOnItemFrame below).
    std::optional<bool> onFrame;
};
/// Java's showOnItemFrame of a decoration type: false for the vanilla player markers
/// (MapDecorationTypes PLAYER, RED_MARKER, BLUE_MARKER, PLAYER_OFF_MAP, PLAYER_OFF_LIMITS: by Java
/// name or as "minecraft:<index>" 0, 2, 3, 6, 13), true for every other type (upstream's 15 GloamingMapDecorations
/// register true).
[[nodiscard]] bool showOnItemFrame(std::string const& decorationType);
/// Whether map `mapId` has upstream decorations (one lock, no copy: the render thread asks for every map it
/// draws).
[[nodiscard]] bool hasDecorations(std::int64_t mapId);
/// The decorations of `mapId` with their styles (one entry per decoration), as one immutable snapshot
/// (nullptr: none).
struct DecoratedMap {
    std::vector<Decoration>      decorations;
    std::vector<DecorationStyle> styles;
};
[[nodiscard]] std::shared_ptr<DecoratedMap const> decorated(std::int64_t mapId);
/// setDecorations with a style per decoration (`styles[i]` for `decorations[i]`; missing entries are
/// plain). The contract's setDecorations draws every decoration plain.
void setDecorations(std::int64_t mapId, std::vector<Decoration> decorations, std::vector<DecorationStyle> styles);
/// The styles of `mapId`'s decorations (one per decoration).
[[nodiscard]] std::vector<DecorationStyle> stylesOf(std::int64_t mapId);
/// The icon a decoration type is drawn with: a texture and its part (u0, v0, u1, v1 in 0..1).
struct Icon {
    std::string texture;
    float       u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};
/// setIcon's texture (all of it); else a vanilla map icon, a 16 x 16 cell of the vanilla atlas
/// "textures/map/map_icons" (4 x 4 cells): "minecraft:<MapDecoration::Type index 0..15>" (as view()
/// reports them) or the Java name ("minecraft:player" 0, "minecraft:frame" 1, "minecraft:red_marker"
/// 2, "minecraft:blue_marker" 3, "minecraft:red_x" / "minecraft:target_x" 4, "minecraft:target_point"
/// 5, "minecraft:player_off_map" 6, "minecraft:player_off_limits" 13, "minecraft:mansion" 14,
/// "minecraft:monument" 15); nullopt: none.
[[nodiscard]] std::optional<Icon> iconFor(std::string const& decorationType);
/// Removes the icon of a decoration type (fixtures and selftests); false when there was none.
bool removeIcon(std::string const& decorationType);
} // namespace gf::client::maps

namespace gf::client::camera {
// camera::shake(intensity, ticks) is upstream's visible screen shake (ClientGameEvents, beanstalk
// rumbling): for `ticks` client ticks the local player's yaw moves by (r - 0.5) * intensity and
// its pitch by (r * 2.5 - 1.25) * intensity degrees (r uniform 0..1, a new r each), so intensity is
// in upstream's units (the beanstalk passes 1 - distance^2 / 256). upstream's ComputeCameraAngles jitter never
// runs: its tick handler zeroes shakeIntensity right after moving the player. A call while a shake
// runs keeps it going for max(remaining, ticks) ticks at the latest intensity. Off with the client
// config's screenShakingEffect.

/// Remaining shake ticks (0: not shaking).
[[nodiscard]] int shaking();
/// The intensity of the running shake (0: none).
[[nodiscard]] float shakeIntensity();
/// The combined FOV factor of the registered modifiers for `fovFactor`.
[[nodiscard]] float applyFovModifiers(float fovFactor);
} // namespace gf::client::camera

namespace gf::client::sky {
/// The provider of `dimension` (nullptr: vanilla).
[[nodiscard]] std::shared_ptr<SkyProvider const> providerOf(int dimension);
/// Removes the provider of `dimension` (fixtures).
void clearProvider(int dimension);
// Graphics modes (spike S5): sky and stars draw in Classic and Vibrant Visuals. Fog sets the
// distance fog (Classic) and the volumetric air fog (Vibrant Visuals: density from `end`, the colour
// as scattering). Clouds set the cloud height of the level renderer (both modes) and nullopt hides
// the clouds. A weather provider that draws hides the vanilla rain / snow (no weather pass, no
// splashes, no rain sounds) and leaves the rain and lightning levels alone (the sky still darkens).
} // namespace gf::client::sky

namespace gf::client::keys {
// When to register (spike S5): LeviLamina binds a key into the game's controls when the game builds
// its input handler, which happens after mod load and before the modules' install(). Register keys
// from a module's load(): they then appear in the keyboard controls and can be remapped. A key
// registered later (install(), a world join) is still delivered, from the raw keyboard events with
// its default key, with a warning; it is missing from the controls until the next game start.

/// The registered key ids (tests).
[[nodiscard]] std::vector<std::string> registered();
/// Whether `id` was registered after the input handler was built (delivered from raw key events).
[[nodiscard]] bool lateBound(std::string const& id);

/// The keys a binding is on now: the player's remapping in the keyboard controls, else the default
/// key it was registered with (a late key, or controls not loaded yet). Java's KeyMapping#getKey /
/// getTranslatedKeyMessage.
struct Binding {
    std::vector<int> keys;            ///< Windows virtual-key codes (empty: unbound)
    std::string      name;            ///< the game's name of the first key ("" when unbound or unknown)
    bool             remapped = false; ///< read from the controls' remapping layout
};
/// The current binding of a registered key (nullopt: `id` was never registered).
[[nodiscard]] std::optional<Binding> binding(std::string const& id);
} // namespace gf::client::keys

#endif // GF_CLIENT

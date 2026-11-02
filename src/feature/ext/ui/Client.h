#pragma once

// F12 additions to the client contracts (rule R18: new files only; folded by A8 in W2b).
// Client build only.
//
// - render::RenderContext, declared opaque in client/render/Render.h, is defined here so renderers
//   can reach the engine's contexts (tessellator, matrices, camera) beyond Lines / drawGeo; the
//   threads and spaces every registry runs in; removal of registrations (fixtures, selftests).
// - ui: textures in GUI pixels (TF's GUI skins), the item stacks of a ContainerScreen's slots, the
//   hit tests of its slots and of the player inventory drawn inside a TF skin, an item's hover text.
// - toast: TF's two toast styles (LockedBiomeToast, MissingAdvancementToast) and their colours;
//   toasts are not drawn while the GUI is hidden (F1), as Java's ToastComponent.
// - hud::guiHidden: Java's options.hideGui for HUD layers (TF's overlays check it themselves).
// - particles::spawn: runs a native particle factory (the client handler of fx.particle uses it);
//   what a factory gets when the engine spawns the particle (molang variables).
// - maps::setDecorations: TF map decorations drawn over a map (MagicMapPacket / MazeMapPacket data),
//   with TF's two IMapDecorationRenderers (the conquered red X, the player icon on top) and the
//   vanilla map icons.
// - camera: what camera::shake does (TF's degrees) and its state (tests).
// - keys: when registerKey must run (mod load) and what happens to a key registered later.
// - textures: every texture name these APIs take is a resource pack path without extension, or an
//   absolute file path (with its extension), which is read from the disk (development fixtures).

#ifdef GF_CLIENT

#include <cstdint>
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

class BaseActorRenderContext;
class ScreenContext;
class MinecraftUIRenderContext;

namespace gf::client::render {

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
    BaseActorRenderContext* actors = nullptr; ///< world passes (blocks, entities, items, onWorld); null for the sky
    Vec3                    camera{};         ///< camera position, world coordinates
    float                   partialTicks = 0;
    bool                    throughWalls = false; ///< Lines / drawGeo skip the depth test
    /// Item contexts other than Gui: the world matrix is the item's own space (see above); drawGeo's
    /// translation and Lines' points are then relative to the item origin.
    bool                    itemSpace = false;
    /// Half the width of Lines' bars in units of the current space (1/64 block in the world).
    float                   lineHalfWidth = 1.0f / 64;
    /// ItemContext::Gui: the UI context and the slot's rectangle in GUI pixels.
    MinecraftUIRenderContext* ui = nullptr;
    float                     guiX = 0, guiY = 0, guiSize = 16;
};

/// Number of block positions currently tracked for block renderers (tests).
[[nodiscard]] std::size_t trackedBlockCount();
/// Whether a block renderer is registered for `blockId` (tests).
[[nodiscard]] bool hasBlockRenderer(std::string const& blockId);
/// Whether an item renderer of `itemId` draws in `where`.
[[nodiscard]] bool hasItemRenderer(std::string const& itemId, ItemContext where);
/// The tint of `blockId` at `pos` from the registry (0xRRGGBB; nullopt: none registered).
[[nodiscard]] std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z);
/// Removals (fixtures and selftests); false when nothing was registered.
bool unregisterBlockRenderer(std::string const& blockId);
bool unregisterItemRenderer(std::string const& itemId);
bool unregisterEntityRenderers(std::string const& entityId);

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
/// (mx, my), -1 for none. TF skins put the inventory at Java's (8, height - 82).
[[nodiscard]] int skinInventorySlotAt(screen::ContainerScreen const& screen, float mx, float my);
/// An item's full hover text as lines, as Java's getTooltipFromItem (name, then the game's lines:
/// enchantments, lore, the lines of tooltip::add and of gf::item::addTooltip); empty for an empty
/// stack. ContainerScreen's hover tooltips show it.
[[nodiscard]] std::vector<std::string> hoverLines(ItemStack const& stack);

/// Per-screen states of ContainerScreens (slot stacks, the title the skin draws) kept now: an entry
/// goes away the frame after its screen stopped being the open one (tests).
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
/// in Java where each TF overlay checks hideGui itself (all but the portal overlay do).
[[nodiscard]] bool guiHidden();
} // namespace gf::client::hud

namespace gf::client::toast {
/// A toast's text colours (ARGB, as Java's drawString).
struct Style {
    unsigned titleColor = 0xFFFFFF00; ///< LockedBiomeToast's title: -256
    unsigned bodyColor  = 0xFFFFFFFF;
};
/// TF's toasts: LockedBiomeToast (title -256, body white; the contract's show() draws this style)
/// and MissingAdvancementToast (UPPER_TEXT 0xffffffff, the advancement title 0xffffff: white).
/// Both are shown for 10 s (ToastSpec::ms = 10000).
enum class Kind { LockedBiome, MissingAdvancement };
[[nodiscard]] constexpr Style styleOf(Kind kind) {
    return kind == Kind::MissingAdvancement ? Style{0xFFFFFFFF, 0xFFFFFFFF} : Style{};
}
/// Shows a toast with `style` (Java's ToastComponent: 5 slots, a queue, 600 ms slides; not drawn while
/// the GUI is hidden).
void show(ToastSpec const& spec, Style style);
/// Removes the toasts with `titleKey`, waiting or shown (selftests); returns how many.
std::size_t cancel(std::string const& titleKey);
/// The style of the first toast with `titleKey`, waiting or shown (tests).
[[nodiscard]] std::optional<Style> styleOfToast(std::string const& titleKey);
} // namespace gf::client::toast

namespace gf::client::particles {
/// Runs the native factory of `particleId`; false when none is registered (then the caller
/// spawns the data-driven particle).
bool spawn(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, nlohmann::json const& params = {});
/// TF's 29 particle ids (declared at load) and whether each has a factory.
[[nodiscard]] std::vector<std::pair<std::string, bool>> declared();
/// Removes the factory of `particleId` (fixtures and selftests); false when there was none.
bool unregisterNative(std::string const& particleId);
// When the client spawns a particle effect with a native factory (the server's SpawnParticleEffectPacket,
// e.g. /particle or F10's fx, and Level::addParticleEffect of the client level), the factory gets the emitter
// position, the velocity `variable.velocity` (x, y, z), else `variable.direction` (x, y, z) times
// `variable.speed` (default 1), else zero, and as params every molang variable of the effect by
// name without "variable." (numbers; structs as objects of their members: {"color": {"r", "g",
// "b", "a"}, "direction": {"x", "y", "z"}}). Senders (F10's fx for vanilla clients) use these names.
} // namespace gf::client::particles

namespace gf::client::maps {
/// Extra (TF) decorations drawn over map `mapId` with the icons of `setIcon`; replaces the
/// previous list (an empty list removes it). Map pixels -128..127 as MapItemSavedData. Drawn on top
/// of held maps (first person) and maps in item frames, as Java's MapRenderer: an 8 x 8 map pixel
/// quad at (x / 2 + 64, z / 2 + 64) turned by rotation * 22.5 degrees, in the map space the game
/// uses when it looks the map up (MapRenderer::_getMapInstance inside the first-person pass, an
/// item in hand or an item frame; MapRenderer::render itself cannot be hooked, spike S5).
/// `Decoration::name` is kept (view()) but not drawn on the map: TF's map decorations
/// (TFMagicMapData, TFMazeMapData) carry no names.
void setDecorations(std::int64_t mapId, std::vector<Decoration> decorations);
/// The TF decorations of `mapId` (empty: none).
[[nodiscard]] std::vector<Decoration> decorationsOf(std::int64_t mapId);
/// The texture registered for a decoration type ("" for none).
[[nodiscard]] std::string iconOf(std::string const& decorationType);

/// TF's IMapDecorationRenderers for one decoration (Java draws them only on TFMagicMapData; the caller
/// sets them for magic maps).
struct DecorationStyle {
    /// ConqueredMapIconRenderer (structure icons of conquered landmarks): the icon is drawn and the
    /// red X (MapDecorationTypes.RED_X: the X of the vanilla map icon atlas) over it, at the icon's
    /// place turned with it, scaled 2, translated (-1, -1, -0.005), a 2 x 2 quad at depth -0.095.
    bool conquered = false;
    /// MagicMapPlayerIconRenderer (the player icon of magic maps): the icon at depth -0.3 instead of
    /// its place in the list, so it lies on top of every other decoration and red X.
    bool topmost = false;
};
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
// camera::shake(intensity, ticks) is TF's visible screen shake (ClientGameEvents, beanstalk
// rumbling): for `ticks` client ticks the local player's yaw moves by (r - 0.5) * intensity and
// its pitch by (r * 2.5 - 1.25) * intensity degrees (r uniform 0..1, a new r each), so intensity is
// in TF's units (the beanstalk passes 1 - distance^2 / 256). TF's ComputeCameraAngles jitter never
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
} // namespace gf::client::keys

#endif // GF_CLIENT

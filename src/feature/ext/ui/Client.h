#pragma once

// F12 additions to the client contracts (rule R18: new files only; folded by A8 in W2b).
// Client build only.
//
// - render::RenderContext, declared opaque in client/render/Render.h, is defined here so renderers
//   can reach the engine's contexts (tessellator, matrices, camera) beyond Lines / drawGeo.
// - ui: textures in GUI pixels (TF's GUI skins), the item stacks of a ContainerScreen's slots and
//   the hit tests of its slots and of the player inventory drawn inside a TF skin.
// - particles::spawn: runs a native particle factory (the client handler of fx.particle uses it).
// - maps::setDecorations: TF map decorations drawn over a map (MagicMapPacket / MazeMapPacket data).
// - camera::shaking: the remaining shake ticks (tests).

#ifdef GF_CLIENT

#include <cstdint>
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
class RenderContext {
public:
    explicit RenderContext(ScreenContext& screenContext) : screen(screenContext) {}

    ScreenContext&          screen;           ///< tessellator, camera matrix stacks
    BaseActorRenderContext* actors = nullptr; ///< world passes (blocks, entities, items, onWorld); null for the sky
    Vec3                    camera{};         ///< camera position, world coordinates
    float                   partialTicks = 0;
    bool                    throughWalls = false; ///< Lines / drawGeo skip the depth test
    /// Item contexts (FirstPerson, ThirdPerson, Ground): the world matrix is the item's own
    /// space; drawGeo's translation is then relative to the item origin.
    bool                    itemSpace = false;
    /// ItemContext::Gui: the UI context and the slot's rectangle in GUI pixels.
    MinecraftUIRenderContext* ui = nullptr;
    float                     guiX = 0, guiY = 0, guiSize = 16;
};

/// Number of block positions currently tracked for block renderers (tests).
[[nodiscard]] std::size_t trackedBlockCount();
/// Whether a block renderer is registered for `blockId` (tests).
[[nodiscard]] bool hasBlockRenderer(std::string const& blockId);
/// The tint of `blockId` at `pos` from the registry (0xRRGGBB; nullopt: none registered).
[[nodiscard]] std::optional<unsigned> tintOf(std::string const& blockId, int x, int y, int z);

} // namespace gf::client::render

namespace gf::client::ui {

/// Draws a rectangle of a texture (resource pack path without extension, e.g.
/// "textures/gui/guigoblintinkering") in GUI pixels, like Java's GuiGraphics.blit: `dst` on the
/// canvas (canvas origin applies), source (u, v, w, h) in texture pixels of a `texW` x `texH`
/// texture. Returns false when the texture can't be loaded.
bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW = 256, float texH = 256);
/// The size of a texture in pixels (nullopt when it can't be loaded).
[[nodiscard]] std::optional<std::pair<int, int>> textureSize(std::string const& texture);

/// Sets the stacks shown in a ContainerScreen's slots: {"<slot index>": item json (items::toJson)}.
void setContainerItems(screen::ContainerScreen& screen, nlohmann::json const& items);
/// The container slot index (SlotPos::index) at panel pixels (mx, my), -1 for none.
[[nodiscard]] int containerSlotAt(screen::ContainerScreen const& screen, float mx, float my);
/// The player inventory slot (0..8 hotbar, 9..35 main) drawn inside the skin at panel pixels
/// (mx, my), -1 for none. TF skins put the inventory at Java's (8, height - 82).
[[nodiscard]] int skinInventorySlotAt(screen::ContainerScreen const& screen, float mx, float my);

/// Toasts waiting or shown (tests).
[[nodiscard]] std::size_t toastCount();
/// HUD layer ids in draw order (tests).
[[nodiscard]] std::vector<std::string> hudLayers();
/// Removes a HUD layer (fixtures).
void removeHudLayer(std::string const& id);
/// The tooltip lines the registry adds for `stack` (tests).
[[nodiscard]] std::vector<std::string> tooltipLines(ItemStack const& stack);

} // namespace gf::client::ui

namespace gf::client::particles {
/// Runs the native factory of `particleId`; false when none is registered (then the caller
/// spawns the data-driven particle).
bool spawn(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, nlohmann::json const& params = {});
/// TF's 29 particle ids (declared at load) and whether each has a factory.
[[nodiscard]] std::vector<std::pair<std::string, bool>> declared();
} // namespace gf::client::particles

namespace gf::client::maps {
/// Extra (TF) decorations drawn over map `mapId` with the icons of `setIcon`; replaces the
/// previous list. Map pixels -128..127 as MapItemSavedData.
void setDecorations(std::int64_t mapId, std::vector<Decoration> decorations);
/// The texture registered for a decoration type ("" for none).
[[nodiscard]] std::string iconOf(std::string const& decorationType);
} // namespace gf::client::maps

namespace gf::client::camera {
/// Remaining shake ticks (0: not shaking).
[[nodiscard]] int shaking();
/// The combined FOV factor of the registered modifiers for `fovFactor`.
[[nodiscard]] float applyFovModifiers(float fovFactor);
} // namespace gf::client::camera

namespace gf::client::sky {
/// The provider of `dimension` (nullptr: vanilla).
[[nodiscard]] SkyProvider const* providerOf(int dimension);
/// Removes the provider of `dimension` (fixtures).
void clearProvider(int dimension);
} // namespace gf::client::sky

namespace gf::client::keys {
/// The registered key ids (tests).
[[nodiscard]] std::vector<std::string> registered();
} // namespace gf::client::keys

#endif // GF_CLIENT

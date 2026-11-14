#pragma once

// Contract extension of "client.ui" (rule R18: F4, new files only; folded into the contract by A8
// in W2b). Client build only. The HUD canvas (client/screen/base/Screen.h) fills, frames and
// writes text but draws no texture; upstream's PortalOverlay blits the portal's animated sprite over the
// whole screen with an alpha (GuiGraphics#setColor(1, 1, 1, alpha) + blit). This draws a frame of
// an animated block texture the way Bedrock lays it out (frames stacked top to bottom, square)
// stretched over a rectangle of GUI pixels, with an alpha:
//
//     gf::client::hud::ext::blitSprite(canvas, "textures/blocks/portal", {0, 0, w, h}, alpha, tick);
//
// The quad is a UI mesh (the route spike S5 found to draw UI textures in Classic and Vibrant
// Visuals); the alpha multiplies the texture through the shader colour.

#ifdef GF_CLIENT

#include <string>

#include "client/screen/base/Screen.h"

namespace gf::client::hud::ext {

/// Draws frame `frame % frames` of `texture` (a resource pack path without extension) over `dst`
/// (canvas coordinates, GUI pixels) with `alpha` (0..1). False while the texture is loading or when
/// it is missing (logged once).
bool blitSprite(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float alpha, int frame);
/// The number of frames of a vertical strip texture (height / width); 0 while unknown.
[[nodiscard]] int spriteFrames(screen::Canvas& canvas, std::string const& texture);

} // namespace gf::client::hud::ext

#endif // GF_CLIENT

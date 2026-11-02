#pragma once

// Block families (F11), client mod: a GUI texture drawn in a native screen (the sign editor's
// board). Client build only.
//
// The draw is a textured quad in GUI pixels with the UI's "ui_textured" material, the way the
// UI framework of F12 (spike S5) found GUI textures to draw on this game version (the image
// batch draws them black); it stays here until a W0 contract offers it.

#ifdef GF_CLIENT

#include <optional>
#include <string>
#include <utility>

#include "client/screen/base/Screen.h"

namespace gf::client::families {

/// Draws the region (u, v, w, h) of the texture `texture` (a resource pack path without the
/// extension, `texW` x `texH` pixels) into `dst` (panel coordinates of `canvas`). False while the
/// texture loads or when it is missing (warned once).
bool blit(screen::Canvas& canvas, std::string const& texture, screen::Rect const& dst, float u, float v, float w, float h, float texW, float texH);

/// The size of a loaded texture in pixels (nullopt while it loads or when it is missing).
[[nodiscard]] std::optional<std::pair<int, int>> textureSize(screen::Canvas& canvas, std::string const& texture);

} // namespace gf::client::families

#endif // GF_CLIENT

#pragma once

// Texture names of the client APIs (F12, feature/ext/ui/Client.h): a resource pack path without
// extension ("textures/gui/guigoblintinkering"), or an absolute file path with its extension, which
// the game reads from the disk (ResourceFileSystem::Raw; the development fixture's own textures, so
// they never ship in the resource pack). Client build only.

#ifdef GF_CLIENT

#include <filesystem>
#include <string>

#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceFileSystem.h"
#include "mc/deps/core/resource/ResourceLocation.h"

namespace gf::client::render {

[[nodiscard]] inline bool isFileTexture(std::string const& texture) { return std::filesystem::path(texture).is_absolute(); }

[[nodiscard]] inline ResourceLocation textureLocation(std::string const& texture) {
    if (isFileTexture(texture)) return ResourceLocation(Core::PathView(texture), ResourceFileSystem::Raw);
    return ResourceLocation(Core::PathView(texture));
}

} // namespace gf::client::render

#endif // GF_CLIENT

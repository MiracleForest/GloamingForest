// U15 (atmosphere), client: immediate meshes with the engine's materials (client build only).
//
// RenderDragon has no GLSL for mods: TF's sky, weather and aurora shaders become CPU-built meshes
// drawn with the game's own materials (docs/spec/atmosphere.md section 4):
// - the stars and the aurora: the level renderer's stars material (vertex colours, additive blend,
//   no fog: TF's stars draw with SRC_ALPHA, ONE and no fog);
// - the weather columns: the particle engine's blend material (texture times vertex colour, alpha
//   blend: TF draws them with the particle shader).

#pragma once

#ifdef GF_CLIENT

#include <memory>
#include <string>
#include <vector>

#include "core/atmosphere/Atmosphere.h"

class ScreenContext;
class LevelRenderer;
namespace mce {
class MaterialPtr;
class TexturePtr;
class Mesh;
} // namespace mce

namespace gf::client::atmosphere {

struct ColourVertex {
    float x = 0, y = 0, z = 0;
    float r = 1, g = 1, b = 1, a = 1;
};

/// The level renderer of the client (null outside a world).
[[nodiscard]] LevelRenderer* levelRenderer();
/// The stars material of the level renderer (null when there is none).
[[nodiscard]] mce::MaterialPtr const* starsMaterial();
/// The particle engine's blend material (null when there is none).
[[nodiscard]] mce::MaterialPtr const* particleBlendMaterial();
/// A resource pack texture (cached for the process).
[[nodiscard]] mce::TexturePtr const* texture(std::string const& path);

/// Draws quads (4 vertices each, colours per vertex) with `material` (no texture).
void drawColouredQuads(ScreenContext& screen, mce::MaterialPtr const& material, std::vector<ColourVertex> const& quads);
/// Draws quads (4 vertices each) with a textured material and `tex`: each quad in the mean colour of
/// its corners (the tessellator's quads take one colour), uv 0..1 across it.
void drawTintedQuads(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const& tex, std::vector<ColourVertex> const& quads);
/// Draws TF weather columns with `material` and `tex`, the uv wrapped by splitting each column at
/// the texture's edges (TF draws them with a repeating sampler); `brightness` scales the colour of
/// the columns lit by the world (big rain).
void drawColumns(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const& tex, std::vector<core::atmosphere::Column> const& columns, float brightness);
/// A mesh of `quads` (4 vertices each) built with the screen's tessellator, for the level
/// renderer's own sky meshes (an empty `quads` builds one invisible degenerate quad).
[[nodiscard]] std::shared_ptr<mce::Mesh> buildMesh(ScreenContext& screen, std::vector<ColourVertex> const& quads, char const* name);

} // namespace gf::client::atmosphere

#endif // GF_CLIENT

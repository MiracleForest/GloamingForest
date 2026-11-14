// U15 (atmosphere), client: immediate meshes with the engine's materials (client build only).
//
// RenderDragon has no GLSL for mods: upstream's sky, weather and aurora shaders become CPU-built meshes
// drawn with the game's own materials (docs/spec/atmosphere.md section 4, docs/units/atmosphere.md
// for the material of each part in each graphics mode): vertex-coloured quads (the stars, the void
// plate, the aurora; textured materials sample a white texture uploaded from memory) and textured
// weather columns.

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
/// A resource pack texture: cached, looked up again for every level renderer (world / server) and
/// after resetTextures(), and reloaded when the texture group no longer has it loaded.
[[nodiscard]] mce::TexturePtr const* texture(std::string const& path);
/// A 16 x 16 white texture uploaded from memory (no resource pack needed; uploaded again the same
/// way), for the vertex-coloured draws with textured materials.
[[nodiscard]] mce::TexturePtr const* whiteTexture();
/// Forgets the looked-up textures (the level was left).
void resetTextures();
/// Bumped by resetTextures() and by a new level renderer.
[[nodiscard]] unsigned textureGeneration();
/// A material by name: "@particles_blend" / "@particles_opaque" / "@particles_alpha" (the particle
/// engine's), "@stars" (the level renderer's stars material), else materialNamed(name).
[[nodiscard]] mce::MaterialPtr const* materialFor(std::string const& name);
/// A material of the engine by name (the common group, else the switchable one; null when there is
/// none), cached for the process.
[[nodiscard]] mce::MaterialPtr const* materialNamed(char const* name);
/// The far plane of the screen's camera (0 when unknown).
[[nodiscard]] float farPlane(ScreenContext& screen);
/// Graphics options of the client: Vibrant Visuals, fancy graphics (Java's fancy weather).
[[nodiscard]] bool vibrantVisuals();
[[nodiscard]] bool fancyGraphics();

/// Draws quads (4 vertices each) with `material`, the colour and alpha of every vertex interpolated
/// across the quad; with `tex` the material samples it at its centre (a white texture keeps the
/// vertex colours; the uv of every vertex is set in the tessellator's next-vertex state).
void drawSmoothQuads(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const* tex, std::vector<ColourVertex> const& quads);

/// Draws quads (4 vertices each, colours per vertex) with `material` (no texture).
void drawColouredQuads(ScreenContext& screen, mce::MaterialPtr const& material, std::vector<ColourVertex> const& quads);
/// Draws quads (4 vertices each) with a textured material and `tex`: each quad in the mean colour of
/// its corners (the tessellator's quads take one colour), uv 0..1 across it.
void drawTintedQuads(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const& tex, std::vector<ColourVertex> const& quads);
/// Draws upstream weather columns with `material` and `tex`, the uv wrapped by splitting each column at
/// the texture's edges (upstream draws them with a repeating sampler); `brightness` scales the colour of
/// the columns lit by the world (big rain).
void drawColumns(ScreenContext& screen, mce::MaterialPtr const& material, mce::TexturePtr const& tex, std::vector<core::atmosphere::Column> const& columns, float brightness);
/// A mesh of `quads` (4 vertices each) built with the screen's tessellator, for the level
/// renderer's own sky meshes (an empty `quads` builds one invisible degenerate quad).
[[nodiscard]] std::shared_ptr<mce::Mesh> buildMesh(ScreenContext& screen, std::vector<ColourVertex> const& quads, char const* name);

} // namespace gf::client::atmosphere

#endif // GF_CLIENT

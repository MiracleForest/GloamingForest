#pragma once

// Contract "client.world" (header owned by F0, implemented by F12; spike S5). Client build only.
//
// Providers for the dimension's sky (TF: no sun or moon, a star field, the aurora), fog,
// clouds and weather (locked-biome weather, acid rain), the music selection, native particles
// (TF's 29 particle ids are pre-declared by F12), map views and map icons, FOV modifiers and
// camera shake. Units never hook the sky / fog / weather functions (rule R8).
//
//     gf::client::sky::setProvider(dim, {.stars = drawStars, .fog = fogFor});
//     gf::client::camera::shake(0.5f, 20);

#ifdef GF_CLIENT

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "client/render/Render.h"

namespace gf::client::sky {

struct Rgb {
    float r = 0, g = 0, b = 0;
};
struct FogSpec {
    Rgb   color;
    float start = 0;  ///< blocks
    float end   = 0;
};
struct SkyProvider {
    /// Draws the sky; true replaces the vanilla sky.
    std::function<bool(render::RenderContext& ctx, float partialTicks)> sky;
    std::function<void(render::RenderContext& ctx, float partialTicks)> stars;
    /// The fog in a biome (nullopt: vanilla fog).
    std::function<std::optional<FogSpec>(std::string const& biomeId)>   fog;
    /// Cloud height (nullopt: no clouds).
    std::function<std::optional<float>()>                                clouds;
    /// Draws the weather; true replaces vanilla rain / snow.
    std::function<bool(render::RenderContext& ctx, float partialTicks)> weather;
};
/// The provider of dimension `dimension` (engine id; the Twilight Forest's from gf::world).
void setProvider(int dimension, SkyProvider provider);

} // namespace gf::client::sky

namespace gf::client::music {
/// The music event to play in a biome / dimension (nullopt: the game's choice).
using MusicFn = std::function<std::optional<std::string>(std::string const& biomeId, int dimension)>;
void setProvider(MusicFn fn);
} // namespace gf::client::music

namespace gf::client::particles {
/// A native particle: spawns its client-side visuals at `pos` with `velocity` and `params`.
using Factory = std::function<void(Vec3 const& pos, Vec3 const& velocity, nlohmann::json const& params)>;
void registerNative(std::string const& particleId, Factory factory);
} // namespace gf::client::particles

namespace gf::client::maps {
struct Decoration {
    std::string type;      ///< TF map decoration id ("twilightforest:lich_tower_icon") or vanilla
    int         x = 0, z = 0; ///< map pixels (-128..127)
    int         rotation = 0; ///< 0..15
    std::string name;
};
struct MapView {
    int                     size = 128;
    std::vector<unsigned>   pixels; ///< ARGB, size * size
    std::vector<Decoration> decorations;
};
/// The current view of map `mapId` (nullopt: unknown on this client).
[[nodiscard]] std::optional<MapView> view(std::int64_t mapId);
/// The texture of a map decoration type (TF's 15 map icons).
void setIcon(std::string const& decorationType, std::string const& texture);
} // namespace gf::client::maps

namespace gf::client::camera {
/// Changes the field of view each frame (GetFieldOfViewModifier); gets and returns the FOV factor.
using FovFn = std::function<float(float fovFactor)>;
void addFovModifier(FovFn fn);
/// Shakes the camera (TF's shakeCamera).
void shake(float intensity, int ticks);
} // namespace gf::client::camera

#endif // GF_CLIENT

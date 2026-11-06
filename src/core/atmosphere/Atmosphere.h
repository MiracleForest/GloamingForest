// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/TwilightForestRenderInfo.java
//   src/main/java/twilightforest/client/renderer/TFSkyRenderer.java
//   src/main/java/twilightforest/client/event/FogHandler.java
//   src/main/java/twilightforest/client/renderer/TFWeatherRenderer.java
//   src/main/java/twilightforest/util/IntervalUtils.java
//   src/main/java/twilightforest/client/event/ClientGameEvents.java
//   src/main/resources/assets/twilightforest/shaders/core/aurora/aurora.fsh
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere): the game-independent maths of the Twilight Forest's sky, fog, locked-biome
// weather and aurora. The client (src/client/atmosphere) feeds it the camera, biomes and time and
// draws what it returns; the server (src/feature/atmosphere) uses the fog rules for the vanilla
// fallback (PlayerFogPacket). Everything here is pure C++ and gtested (tests/atmosphere).

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/GlobalPos.h"

namespace gf::core::atmosphere {

struct Rgb {
    float r = 0, g = 0, b = 0;
    friend bool operator==(Rgb const&, Rgb const&) = default;
};

struct Vec3f {
    float x = 0, y = 0, z = 0;
};

// ---- biome look (TF's biome JSON: sky_color and fog_color, docs/spec/biomes.md section 6) ----------------

struct BiomeLook {
    char const* id;
    unsigned    sky; ///< 0xRRGGBB
    unsigned    fog; ///< 0xRRGGBB
};
/// The 22 Twilight Forest biomes and their sky / fog colours.
[[nodiscard]] std::vector<BiomeLook> const& biomeLooks();
/// The look of `biomeId` (nullopt: not a Twilight Forest biome).
[[nodiscard]] std::optional<BiomeLook> biomeLook(std::string_view biomeId);
[[nodiscard]] Rgb rgbOf(unsigned rgb);

// ---- dimension effects (TwilightForestRenderInfo) ---------------------------------------------------------

/// DimensionSpecialEffects cloud height of the "twilightforest:renderer" effects.
inline constexpr float kCloudHeight = 128.0f;
/// The dimension's fixed time (twilight_forest_type.json fixed_time).
inline constexpr long long kFixedTime = 13000;

/// DimensionType#timeOfDay: the celestial angle 0..1 of a day time.
[[nodiscard]] float timeOfDay(long long dayTime);
/// ClientLevel's daylight factor of the fog / sky colour: clamp(cos(timeOfDay * 2 pi) * 2 + 0.5, 0, 1).
[[nodiscard]] float daylight(float timeOfDay);
/// TwilightForestRenderInfo#getBrightnessDependentFogColor.
[[nodiscard]] Rgb brightnessDependentFogColor(Rgb biomeFog, float daylight);
/// ClientLevel#getSkyColor without the lightning flash: the (blended) biome sky colour times the
/// daylight, greyed by the rain (towards 0.6 x its luminance by rain * 0.75) and the thunder
/// (towards 0.2 x its luminance by thunder * 0.75).
[[nodiscard]] Rgb skyColor(Rgb biomeSky, float daylight, float rainLevel, float thunderLevel);
/// The frame's fog colour as FogRenderer#setupColor makes it in the Twilight Forest (FogType.NONE):
/// the (blended) biome fog colour through getBrightnessDependentFogColor, pulled towards the sky
/// colour (skyColor) by the render distance (`renderDistanceChunks`), no sunrise colour; then
/// darkened by the rain (red and green x (1 - rain * 0.5), blue x (1 - rain * 0.4)) and the thunder
/// (x (1 - thunder * 0.5)), and towards black when the camera is less than one block above the
/// dimension's floor (`aboveFloor` = camera y - min build height: x clamp(aboveFloor, 0, 1)^2).
[[nodiscard]] Rgb fogColor(Rgb biomeFog, Rgb biomeSky, float daylight, int renderDistanceChunks, float rainLevel = 0.0f, float thunderLevel = 0.0f, double aboveFloor = 1.0e9);
/// CubicSampler#gaussianSampleVec3: the colours `fetch(qx, qy, qz)` of the 6 x 6 x 6 quart cells
/// around `(x, y, z)` (quart coordinates: (camera - 2) / 4) weighted by the kernel 0 1 4 6 4 1 0,
/// as FogRenderer#setupColor blends the biome fog colours (and ClientLevel#getSkyColor the sky).
template <class Fetch>
[[nodiscard]] Rgb gaussianSample(double x, double y, double z, Fetch&& fetch) {
    constexpr double kKernel[7] = {0.0, 1.0, 4.0, 6.0, 4.0, 1.0, 0.0};
    auto const       floorOf    = [](double v) { return static_cast<int>(v < 0 && static_cast<double>(static_cast<int>(v)) != v ? static_cast<int>(v) - 1 : static_cast<int>(v)); };
    int const        i = floorOf(x), j = floorOf(y), k = floorOf(z);
    double const     dx = x - i, dy = y - j, dz = z - k;
    double           total = 0.0, r = 0.0, g = 0.0, b = 0.0;
    for (int l = 0; l < 6; ++l) {
        double const wx = kKernel[l + 1] + dx * (kKernel[l] - kKernel[l + 1]);
        for (int m = 0; m < 6; ++m) {
            double const wy = kKernel[m + 1] + dy * (kKernel[m] - kKernel[m + 1]);
            for (int n = 0; n < 6; ++n) {
                double const wz = kKernel[n + 1] + dz * (kKernel[n] - kKernel[n + 1]);
                double const w  = wx * wy * wz;
                if (w == 0.0) continue;
                Rgb const c = fetch(i - 2 + l, j - 2 + m, k - 2 + n);
                total += w;
                r += c.r * w;
                g += c.g * w;
                b += c.b * w;
            }
        }
    }
    if (total <= 0.0) return {};
    return {static_cast<float>(r / total), static_cast<float>(g / total), static_cast<float>(b / total)};
}
/// fog.glsl's linear_fog value (1.21.1): 0 up to `start`, smoothstep(start, end, distance) up to
/// `end`, 1 beyond (the share of the fog colour mixed in).
[[nodiscard]] float linearFogValue(float distance, float start, float end);
/// TwilightForestRenderInfo#isFoggyAt (Java's near fog): the spooky forest at any height, the dark
/// forest and its center at y <= 20.
[[nodiscard]] bool isFoggyAt(std::string_view biomeId, double playerY);
[[nodiscard]] bool isSpooky(std::string_view biomeId);
/// ClientLevel#getCloudColor at a celestial angle (`timeOfDay`), rain level and thunder level:
/// white darkened by the daylight (TwilightForestRenderInfo has no sunrise colour, so the clouds
/// get no sunset tint).
[[nodiscard]] Rgb cloudColor(float timeOfDay, float rainLevel, float thunderLevel);
/// The void plate of TFSkyRenderer (LevelRenderer's dark buffer: buildSkyDisc(-16) moved up by 12):
/// a triangle fan around (0, y, 0), y = -4 relative to the camera, radius 512, as quads (the last
/// corner repeated), corners at -180, -135, ..., 180 degrees.
[[nodiscard]] std::vector<std::array<Vec3f, 4>> voidPlateQuads();
/// A vertex of the fogged void plate: its position (camera relative, as voidPlateQuads) and the
/// share of the sky fog colour TF's position shader mixes into its black (linear_fog of the dark
/// buffer's own vertex, cylindrical distance: max(length(xz), |y|) at y -16).
struct FoggedVertex {
    Vec3f pos;
    float fog = 0;
};
/// The void plate of voidPlateQuads with each fan triangle cut into `rings` rings (quads), every
/// vertex with its fog value for the sky fog planes `fogStart`..`fogEnd`: drawn opaque, black at
/// the centre and the fog colour towards the horizon, as Java's dark buffer is.
[[nodiscard]] std::vector<std::array<FoggedVertex, 4>> voidPlateFogged(float fogStart, float fogEnd, int rings);

/// ResourceLocation#tryParse as TFConfig#getValidAuroraBiomes reads an id: "namespace:path" (or
/// "path" in "minecraft"), the namespace of [a-z0-9_.-], the path of [a-z0-9_.-/]; nullopt when
/// invalid.
[[nodiscard]] std::optional<std::string> parseResourceLocation(std::string_view id);
/// The same biome id as Bedrock spells it: vanilla biomes have no "minecraft:" prefix there.
[[nodiscard]] std::string bedrockBiomeName(std::string_view resourceLocation);

/// ClientGameEvents#customizeSplashes: "Happy <ordinal> birthday to the Twilight Forest!" for
/// `year` (year - 2011, ordinal as Java's RuleBasedNumberFormat ORDINAL in English: 1st, 2nd, 11th).
[[nodiscard]] std::string birthdaySplash(int year);

// ---- fog distances (FogRenderer#setupFog, FogHandler) -----------------------------------------------------

struct FogPlanes {
    float start = 0; ///< near plane, blocks
    float end   = 0; ///< far plane, blocks
    friend bool operator==(FogPlanes const&, FogPlanes const&) = default;
};
/// Vanilla FogRenderer#setupFog for FogType.NONE: the sky fog (start 0, end = render distance), or
/// the terrain fog (start = distance - clamp(distance / 10, 4, 64), end = distance); near fog
/// (`foggy`, terrain only: FOG_SKY is checked first): start = distance * 0.05, end = min(distance, 192) * 0.5.
[[nodiscard]] FogPlanes vanillaFog(bool sky, float renderDistance, bool foggy);

/// FogHandler: the spooky forest halves the far plane (sky near 0, terrain near = far * 0.75) and
/// the planes move towards their targets every frame (far by 0.003, near by 0.0015 when it grows
/// and 0.006 when it shrinks). The first frame with the player's chunk loaded sets them outright.
class FogSmoother {
public:
    /// One frame of the sky fog (FogMode.FOG_SKY); nullopt: vanilla (chunk not loaded yet).
    /// `frames` > 1 (or a fraction) applies that many frames of smoothing at once (the client
    /// steps by the time since its last step, in frames of 1/60 s); 1 is exactly TF's lerp.
    std::optional<FogPlanes> sky(FogPlanes vanilla, bool spooky, bool chunkLoaded, float frames = 1.0f);
    /// One frame of the terrain fog.
    std::optional<FogPlanes> terrain(FogPlanes vanilla, bool spooky, bool chunkLoaded, float frames = 1.0f);
    /// FogHandler#unloadFog (the level unloaded).
    void reset();
    [[nodiscard]] bool      skyLoaded() const { return mSkyLoaded; }
    [[nodiscard]] bool      terrainLoaded() const { return mTerrainLoaded; }
    [[nodiscard]] FogPlanes skyPlanes() const { return mSky; }
    [[nodiscard]] FogPlanes terrainPlanes() const { return mTerrain; }

private:
    bool      mSkyLoaded = false, mTerrainLoaded = false;
    FogPlanes mSky{}, mTerrain{};
};

// ---- sky (TFSkyRenderer) ----------------------------------------------------------------------------------

/// TFSkyRenderer#drawStars: LevelRenderer#drawStars with 3000 tries (RandomSource.create(10842)),
/// each a quad of 4 vertices at distance 100 (Java's in-place Vector3f#add makes each corner the sum
/// of the corners before it, as vanilla 1.21 draws them). Positions before the sky rotation.
[[nodiscard]] std::vector<std::array<Vec3f, 4>> starQuads();
/// The star rotation of TFSkyRenderer#renderSky: Y by -90 degrees, then X by timeOfDay * 360.
[[nodiscard]] Vec3f rotateStar(Vec3f v, float timeOfDay);
/// TFSkyRenderer: the void plate (the vanilla dark buffer at +12) is drawn when the camera eye is
/// below the dimension's minimum build height.
[[nodiscard]] inline bool drawsVoidPlate(double eyeY, int minBuildHeight) { return eyeY - minBuildHeight < 0.0; }

// ---- locked-biome weather (TFWeatherRenderer) -------------------------------------------------------------

enum class Effect { None, Blizzard, Mosquito, Ashes, DarkStream, BigRain, Sparkles };
/// The texture of an effect (resource pack path, TF's textures/environment/<name>.png).
[[nodiscard]] char const* textureOf(Effect effect);
[[nodiscard]] char const* nameOf(Effect effect);
/// The enforcement of a restriction ("twilightforest:frost", ...) to TF's RenderType; darkness
/// draws a stream in half the columns (random.nextBoolean() after the column seed, `darkRoll`).
[[nodiscard]] Effect effectFor(std::string_view enforcement, bool darkRoll);
/// The weather column seed (vanilla rain): x * x * 3121 + x * 45238971 ^ z * z * 418711 + z * 13761.
[[nodiscard]] std::int64_t columnSeed(int x, int z);
/// rainxs / rainzs of the column at offset (dx, dz) from the camera (|dx|, |dz| <= 15): the quad's
/// half-width direction (NaN at the camera's own column, as in Java: that column draws nothing).
[[nodiscard]] std::pair<float, float> rainOffset(int dx, int dz);
/// The range of the weather columns (5, 10 with fancy graphics).
[[nodiscard]] inline int weatherRange(bool fancy) { return fancy ? 10 : 5; }
/// The square around the viewer (15 blocks) sampled for a locked biome (isNearLockedBiome).
inline constexpr int kLockedBiomeScan = 15;

/// One weather column, as TFWeatherRenderer#renderEffect draws it (positions relative to the camera).
struct Column {
    Effect   effect = Effect::None;
    float    x0 = 0, z0 = 0, x1 = 0, z1 = 0; ///< bottom-left and bottom-right corners (camera relative)
    float    y0 = 0, y1 = 0;                 ///< bottom and top (camera relative)
    float    u0 = 0, u1 = 1;                 ///< u at the left and right edge
    float    v0 = 0, v1 = 0;                 ///< v at the bottom and the top
    Rgb      color{1, 1, 1};
    float    alpha     = 1;
    bool     fullBright = true;
};
/// Where a column is drawn: its block (x, z), its vertical span, the client ticks and partial tick,
/// the camera and the range.
struct ColumnInput {
    int    x = 0, z = 0;
    int    minY = 0, maxY = 0;
    int    ticks = 0;
    float  partialTicks = 0;
    double camX = 0, camY = 0, camZ = 0;
    int    range = 10;
};
/// The locked-biome column (renderLockedBiome's switch). Returns nullopt for Effect::None or the
/// camera's own column. The column random is seeded here (setSeed(columnSeed)) and `darkness` is
/// rolled with it, as Java does; `enforcement` is the biome restriction's.
[[nodiscard]] std::optional<Column> lockedColumn(ColumnInput const& in, std::string_view enforcement);
/// The vertical span of a locked-biome column: max(py - range, minBuild) .. max(py + range, minBuild);
/// nullopt when it is empty.
[[nodiscard]] std::optional<std::pair<int, int>> lockedSpan(int py, int range, int minBuild);

/// renderLockedStructure's sparkles column for one interval (in.minY..in.maxY) of the column at
/// (x, z); nullopt for the camera's own column.
[[nodiscard]] std::optional<Column> sparkleColumn(ColumnInput const& in);

/// IntervalUtils#mergeAndSortIntervals.
using Interval = std::pair<int, int>;
[[nodiscard]] std::vector<Interval> mergeAndSortIntervals(std::vector<Interval> intervals);
/// IntervalUtils#subtractIntervals (sortedB from sortedA).
[[nodiscard]] std::vector<Interval> subtractIntervals(std::vector<Interval> sortedA, std::vector<Interval> const& sortedB);
/// The camera box of renderLockedStructure: x/z +-range, y -range..+2 range.
[[nodiscard]] BlockBox cameraBox(int px, int py, int pz, int range);
/// updateRainIntervals for one column: the protected intervals minus the unprotected ones of the
/// boxes (box, protected) that intersect `pBox`, at (x, z).
[[nodiscard]] std::vector<Interval> sparkleIntervals(int x, int z, BlockBox const& pBox, std::vector<std::pair<BlockBox, bool>> const& boxes);

/// TFWeatherRenderer#tickRain's Ur-Ghast rain: rises by 0.1 a tick to 1 while the Ur-Ghast is in
/// its tantrum (`alive` set that tick), else falls by 0.02 to 0.
[[nodiscard]] float tickUrGhastRain(float rain, bool alive);
/// tickRain's rain level: max(level rain, Ur-Ghast rain), halved without fancy graphics.
[[nodiscard]] float rainLevel(float levelRain, float urGhastRain, bool fancy);
/// The splash particle count of tickRain: (int)(100 * level^2), halved with decreased particles.
[[nodiscard]] int splashCount(float rainLevel, bool decreasedParticles);
/// FluidState#getHeight of a Bedrock liquid block with `liquidDepth` (0 source, 1..7 flowing, 8 and
/// up falling): 1 under the same fluid, else amount / 9 (amount 8 - depth, 8 when falling).
[[nodiscard]] float fluidHeight(int liquidDepth, bool sameFluidAbove);
/// tickRain's random (RandomSource.create(ticks * 312987231)).
[[nodiscard]] std::int64_t rainTickSeed(int ticks);

// ---- aurora (ClientGameEvents#renderAurora, aurora.fsh) ---------------------------------------------------

/// ClientGameEvents#clientTick's aurora counter: +1 in an aurora biome, -1 elsewhere, 0..60; 0 when
/// the biome list is empty.
[[nodiscard]] int tickAurora(int aurora, bool inAuroraBiome, bool listEmpty);
inline constexpr int kAuroraMax = 60;
/// The aurora's colour modulator alpha: lerp(partial, last, current) / 60 * 0.5.
[[nodiscard]] float auroraAlpha(int lastAurora, int aurora, float partialTicks);
/// The aurora quad's half size: 2048 * (render distance in blocks / 32).
[[nodiscard]] inline float auroraScale(float renderDistanceBlocks) { return 2048.0f * (renderDistanceBlocks / 32.0f); }
/// The aurora quad's height relative to the camera: 256 - camera y.
[[nodiscard]] inline float auroraHeight(double cameraY) { return static_cast<float>(256.0 - cameraY); }

/// K.jpg's OpenSimplex2 (re-oriented 4-point BCC noise) as aurora.fsh evaluates it (GLSL floats):
/// openSimplex2_ImproveXY(X).a.
[[nodiscard]] float openSimplex2ImproveXY(float x, float y, float z);

/// The aurora shader's uniforms.
struct AuroraUniforms {
    int   seed = 0;      ///< SeedContext: |(int) biomeZoomSeed|
    float camX = 0, camY = 0, camZ = 0; ///< PositionContext
    float gameTime = 0;  ///< GameTime: (game time % 24000 + partial) / 24000
    float fogStart = 0, fogEnd = 1;
    Rgb   fogColor{};
    float fogAlpha = 1; ///< FogColor.a
};
/// SeedContext of a world seed: Mth.abs((int) BiomeManager.obfuscateSeed(seed)).
[[nodiscard]] int auroraSeed(std::int64_t worldSeed);
/// The aurora's fragment at `pixel` (the position relative to the camera, as the vertex shader
/// passes it on) before the colour modulator: rgb and alpha (the noise times the fog fade), rgb fogged.
struct AuroraFragment {
    Rgb   color{};
    float alpha = 0;
};
[[nodiscard]] AuroraFragment auroraFragment(float px, float pz, AuroraUniforms const& u);
/// The aurora's mesh (the shader runs per pixel; RenderDragon has no custom shaders, so the shader
/// runs per vertex of a fine polar mesh and the colours are interpolated): ring r of `rings` at the
/// angle atan(extent / h) * r / rings from the zenith (h = the plane's distance above or below the
/// camera, at least 8), segment s of `segments` around it; the camera-relative x / z of a vertex.
/// The cells keep about the same angular size from the camera whatever the render distance.
struct AuroraMesh {
    int   rings = 0, segments = 0;
    float height = 0; ///< plane height relative to the camera
    float extent = 0; ///< radius covered (2.75 x the fog end: the shader fades the aurora out by then)
};
[[nodiscard]] AuroraMesh auroraMesh(float planeHeight, float fogEnd, int rings, int segments);
[[nodiscard]] std::pair<float, float> auroraMeshPoint(AuroraMesh const& mesh, int ring, int segment);
/// fog.glsl's linear_fog_fade / smoothstep.
[[nodiscard]] float linearFogFade(float distance, float fogStart, float fogEnd);

} // namespace gf::core::atmosphere

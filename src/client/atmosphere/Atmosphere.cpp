// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/TwilightForestRenderInfo.java
//   src/main/java/twilightforest/client/renderer/TFSkyRenderer.java
//   src/main/java/twilightforest/client/event/FogHandler.java
//   src/main/java/twilightforest/client/renderer/TFWeatherRenderer.java
//   src/main/java/twilightforest/client/event/ClientGameEvents.java
//   src/main/java/twilightforest/config/TFConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 (atmosphere), client mod: the Twilight Forest's DimensionSpecialEffects ("twilightforest:
// renderer") through F12's sky provider (gf::client::sky::setProvider for the TF dimension):
// - sky: the vanilla sky dome in the biome's sky colour, no sun and no moon (the level renderer's
//   sun / moon meshes are swapped for an empty one while the player is in the Twilight Forest), and
//   TF's 3000-try star field (seed 10842) at full brightness instead of the vanilla stars;
// - fog: FogHandler's smoothed planes (the spooky forest halves them, the dark forests below y 20
//   use the near fog) in the fog colour of getBrightnessDependentFogColor;
// - clouds at y 128;
// - weather: TFWeatherRenderer's locked-biome columns (blizzard, mosquitoes, ashes, dark streams,
//   big rain) and the sparkles in protected structure pieces; vanilla weather still renders;
// - the aurora (ClientGameEvents#renderAurora) in the biomes of the config's auroraBiomes, the
//   shader evaluated on the CPU into a vertex-coloured grid drawn with the sky;
// - tickRain's Ur-Ghast rain: splashes and rain sounds while the Ur-Ghast is in its tantrum
//   ("atmosphere.ur_ghast").
// Client config: section "atmosphere" (feature/atmosphere/Atmosphere.h).

#ifdef GF_CLIENT

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/comprehensive/ParticleType.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "client/atmosphere/AtmosphereClient.h"
#include "client/atmosphere/Draw.h"
#include "client/world/Sky.h"
#include "core/atmosphere/Atmosphere.h"
#include "feature/atmosphere/Atmosphere.h"
#include "feature/dimension/World.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::client::atmosphere {

namespace ca  = core::atmosphere;
namespace at  = feature::atmosphere;
namespace net = feature::net;
using Json    = nlohmann::json;

namespace {

// ---- state ------------------------------------------------------------------------------------------------

/// A development preview ("atmosphere.preview").
struct Preview {
    std::optional<std::string>                                    biome;   ///< every biome lookup answers this
    bool                                                          locked = false; ///< the preview biome is locked
    std::optional<std::vector<std::pair<core::BlockBox, bool>>>   boxes;   ///< protected boxes instead of F6's
    bool                                                          urGhast = false;
    int                                                           winding = 0; ///< 0 one side, 1 the other, 2 both
    int                                                           auroraMode = 0; ///< 0 the blend material, 1 the stars material
};

/// The aurora grid the worker computed: camera-relative positions (at `cam`) and colours.
struct AuroraGrid {
    double                    camX = 0, camY = 0, camZ = 0;
    int                       n    = 0; ///< cells per side
    std::vector<ColourVertex> vertices; ///< (n + 1)^2, alpha before the colour modulator
};

struct Shared {
    std::mutex mutex;
    // client tick
    int         ticks = 0;
    int         aurora = 0, lastAurora = 0;
    float       urGhastRain = 0;
    bool        urGhastActive = false;
    std::string cameraBiome;
    bool        inTwilight = false;
    // locked-biome weather: enforcement per column of the (2 * 15 + 1)^2 square around (cx, cz)
    bool                     nearLocked = false;
    int                      cx = 0, cz = 0;
    std::vector<std::string> enforcement;
    // aurora
    std::shared_ptr<AuroraGrid const> grid;
    ca::AuroraUniforms                uniforms;
    float                             auroraScale = 0;
    bool                              uniformsValid = false;
    // fog (render thread)
    ca::FogSmoother                       fog;
    std::chrono::steady_clock::time_point fogStepped{};
    std::optional<ca::FogPlanes>          fogPlanes;
    float                                 renderDistance = 192;
    Preview                               preview;
};
Shared& shared() {
    static Shared s;
    return s;
}

std::atomic<int> gDimension{-1}; ///< the TF dimension the provider is set for

struct OnceKeys {
    std::mutex                      m;
    std::unordered_set<std::string> done;
};
OnceKeys& onceKeys() {
    static OnceKeys k;
    return k;
}
bool onceFlag(std::string const& key) {
    std::lock_guard lock(onceKeys().m);
    return onceKeys().done.insert(key).second;
}
/// A new preview reports what it shows again (the e2e scenario waits for these lines).
void forgetOnce(std::initializer_list<char const*> prefixes) {
    std::lock_guard lock(onceKeys().m);
    std::erase_if(onceKeys().done, [&](std::string const& k) { return std::any_of(prefixes.begin(), prefixes.end(), [&](char const* p) { return k.starts_with(p); }); });
}
void logOnce(std::string const& key, std::string const& line) {
    if (onceFlag(key)) log().info("{}", line);
}

// ---- config -----------------------------------------------------------------------------------------------

struct AtmosphereConfigView {
    std::vector<std::string> auroraBiomes;
    bool                     sky, fog, weather, aurora;
};

/// TFConfig#getValidAuroraBiomes: the configured ids that parse as resource locations (an invalid
/// one is warned about once and skipped).
AtmosphereConfigView config() {
    auto const&          c = gf::config::get<at::AtmosphereConfig>();
    AtmosphereConfigView v{{}, c.sky, c.fog, c.weather, c.aurora};
    for (auto const& id : c.auroraBiomes) {
        auto colon = id.find(':');
        bool ok    = !id.empty() && std::all_of(id.begin(), id.end(), [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.' || ch == '/' || ch == ':';
        }) && std::count(id.begin(), id.end(), ':') <= 1 && colon != 0;
        if (!ok) {
            if (onceFlag("aurora_invalid:" + id)) log().warn("Biome {} in Twilight Forest's validAuroraBiomes config option is not a valid biome. Skipping!", id);
            continue;
        }
        v.auroraBiomes.push_back(colon == std::string::npos ? "minecraft:" + id : id);
    }
    return v;
}

// ---- restrictions (TF's Restriction registry: <mod>/data/<ns>/twilight/restrictions/<biome>.json) ----------

std::unordered_map<std::string, std::string> const& enforcements() {
    static std::unordered_map<std::string, std::string> const map = [] {
        std::unordered_map<std::string, std::string> m;
        std::error_code                              ec;
        std::filesystem::path                        data;
        try {
            data = Mod::getInstance().getSelf().getModDir() / "data";
        } catch (...) {
            return m;
        }
        for (auto const& ns : std::filesystem::directory_iterator(data, ec)) {
            auto root = ns.path() / "twilight" / "restrictions";
            if (!std::filesystem::is_directory(root, ec)) continue;
            for (auto const& f : std::filesystem::recursive_directory_iterator(root, ec)) {
                if (f.path().extension() != ".json") continue;
                try {
                    std::ifstream in(f.path());
                    auto          j  = Json::parse(in, nullptr, true, true);
                    auto          id = ns.path().filename().string() + ":" + std::filesystem::relative(f.path(), root, ec).replace_extension().generic_string();
                    m[id]            = j.value("enforcement", std::string{});
                } catch (...) {}
            }
        }
        return m;
    }();
    return map;
}

// ---- biomes --------------------------------------------------------------------------------------------------

std::string biomeAt(BlockSource& region, BlockPos const& pos, Preview const& preview) {
    if (preview.biome) return *preview.biome;
    if (!region.hasChunksAt(pos, 0, false)) return {};
    return region.getBiome(pos).mHash->getString();
}

/// Restriction#getRestrictionForBiome(biome, player).enforcement(), "" when the biome is safe.
std::string lockedEnforcement(std::string const& biome, Preview const& preview) {
    if (biome.empty()) return {};
    bool locked = preview.biome && preview.locked ? true : progression::client::biomeLocked(biome);
    if (!locked) return {};
    auto it = enforcements().find(biome);
    return it == enforcements().end() ? std::string{} : it->second;
}

bool inTwilight(LocalPlayer const& p) { return gDimension.load() >= 0 && static_cast<int>(p.getDimensionId()) == gDimension.load(); }

// ---- client tick -----------------------------------------------------------------------------------------------

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

/// tickRain's splashes and rain sounds of the Ur-Ghast rain (the level's own rain is the game's).
void urGhastSplashes(LocalPlayer& p, float level, int& rainSoundTime) {
    if (level <= 0.0f) return;
    auto&       region = p.getDimensionBlockSource();
    auto const& pos    = p.getPosition();
    BlockPos    at{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    int         n = ca::splashCount(level, false);
    std::uniform_int_distribution<int>     d21(0, 20);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::optional<BlockPos>                splash;
    auto&                                  lvl = p.getLevel();
    int const                              minY = p.getDimension().mHeightRange->mMin;
    for (int j = 0; j < n; ++j) {
        BlockPos column{at.x + d21(rng()) - 10, 0, at.z + d21(rng()) - 10};
        if (!region.hasChunksAt(column, 0, false)) continue;
        BlockPos top = region.getHeightmapPos(column);
        if (top.y <= minY || top.y > at.y + 10 || top.y < at.y - 10) continue;
        BlockPos below{top.x, top.y - 1, top.z};
        splash          = below;
        auto const& id  = game::blockId(region.getBlock(below));
        bool        hot = id == "minecraft:lava" || id == "minecraft:flowing_lava" || id == "minecraft:magma" || id == "minecraft:campfire" || id == "minecraft:soul_campfire";
        Vec3        where{static_cast<float>(below.x + unit(rng())), static_cast<float>(below.y + 1.0), static_cast<float>(below.z + unit(rng()))};
        lvl.addParticle(hot ? ParticleType::Smoke : ParticleType::RainSplash, where, Vec3{}, 0, nullptr, false);
    }
    std::uniform_int_distribution<int> d4(0, 3);
    if (splash && d4(rng()) < rainSoundTime++) {
        rainSoundTime = 0;
        auto* c       = currentClient();
        if (!c) return;
        Vec3 where{static_cast<float>(splash->x), static_cast<float>(splash->y), static_cast<float>(splash->z)};
        bool above = splash->y > at.y + 1 && region.getHeightmapPos(at).y > at.y;
        // weather.rain.above (0.1, 0.5) / weather.rain (0.2, 1)
        c->getSoundEngine()->play("ambient.weather.rain", where, above ? 0.1f : 0.2f, above ? 0.5f : 1.0f, std::nullopt);
    }
}

void clientTick() {
    auto* p = currentPlayer();
    // the TF dimension's engine id: the local server's registry, or (a remote server) the dimension
    // the player is in when its name is TF's
    int dim = gDimension.load();
    if (p && ::gf::world::isTwilight(p->getDimension())) dim = static_cast<int>(p->getDimensionId());
    else if (dim < 0)
        if (auto tf = ::gf::world::twilight()) dim = static_cast<int>(*tf);
    if (dim >= 0 && dim != gDimension.load()) {
        gDimension = dim;
        installProvider();
    }
    auto& s = shared();
    if (!p || !inTwilight(*p)) {
        std::lock_guard lock(s.mutex);
        s.inTwilight = false;
        s.aurora = s.lastAurora = 0;
        s.nearLocked            = false;
        s.urGhastRain           = 0;
        return;
    }
    auto const cfg = config();
    Preview    preview;
    {
        std::lock_guard lock(s.mutex);
        preview = s.preview;
    }
    auto&       region = p->getDimensionBlockSource();
    auto const& pos    = p->getPosition();
    BlockPos    bp{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    std::string camera = biomeAt(region, bp, preview);

    // aurora counter (ClientGameEvents#clientTick)
    bool inAurora = cfg.aurora && std::find(cfg.auroraBiomes.begin(), cfg.auroraBiomes.end(), camera) != cfg.auroraBiomes.end();

    // locked-biome weather: the columns around the player (isNearLockedBiome at y 0)
    std::vector<std::string> enforcement;
    bool                     near = false;
    bool const gameMode = !p->isCreative() && !p->isSpectator();
    if (cfg.weather && (preview.locked || (progression::client::enforced() && gameMode))) {
        constexpr int r = ca::kLockedBiomeScan;
        enforcement.assign(static_cast<std::size_t>((2 * r + 1) * (2 * r + 1)), std::string{});
        std::unordered_map<std::string, std::string> memo;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                auto biome = biomeAt(region, BlockPos{bp.x + dx, 0, bp.z + dz}, preview);
                auto it    = memo.find(biome);
                if (it == memo.end()) it = memo.emplace(biome, lockedEnforcement(biome, preview)).first;
                if (!it->second.empty()) near = true;
                enforcement[static_cast<std::size_t>((dz + r) * (2 * r + 1) + dx + r)] = it->second;
            }
    }

    static int rainSoundTime = 0;
    float      urRain        = 0;
    float      levelRain     = 0;
    if (auto* w = p->getDimension().mWeather.get()) levelRain = w->mRainLevel;
    {
        std::lock_guard lock(s.mutex);
        s.inTwilight  = true;
        s.ticks++;
        s.lastAurora  = s.aurora;
        s.aurora      = ca::tickAurora(s.aurora, inAurora, cfg.auroraBiomes.empty() || !cfg.aurora);
        s.cameraBiome = camera;
        s.nearLocked  = near;
        s.cx          = bp.x;
        s.cz          = bp.z;
        s.enforcement = std::move(enforcement);
        s.urGhastRain = ca::tickUrGhastRain(s.urGhastRain, s.urGhastActive || preview.urGhast);
        urRain        = s.urGhastRain;
    }
    if (inAurora) logOnce("aurora_in:" + camera, "atmosphere: aurora fading in (" + camera + ")");
    if (near) logOnce("locked_near", "atmosphere: near a locked biome, its weather is drawn");
    // tickRain: the Ur-Ghast's rain where it is stronger than the level's own (that one is the game's)
    if (urRain > levelRain) {
        urGhastSplashes(*p, ca::rainLevel(levelRain, urRain, true), rainSoundTime);
        logOnce("ur_ghast_rain", "atmosphere: Ur-Ghast rain splashes");
    }
}

// ---- aurora worker -------------------------------------------------------------------------------------------

class AuroraWorker {
public:
    ~AuroraWorker() { stop(); } // never destroy a joinable thread (std::terminate at exit)
    void start() {
        if (mThread.joinable()) return;
        mStop   = false;
        mThread = std::thread([this] { run(); });
    }
    void stop() {
        {
            std::lock_guard lock(mMutex);
            mStop = true;
        }
        mWake.notify_all();
        if (mThread.joinable()) mThread.join();
    }

private:
    void run() {
        while (true) {
            {
                std::unique_lock lock(mMutex);
                mWake.wait_for(lock, std::chrono::milliseconds(50), [this] { return mStop; });
                if (mStop) return;
            }
            ca::AuroraUniforms u;
            float              scale = 0;
            {
                auto&           s = shared();
                std::lock_guard lock(s.mutex);
                if (!s.inTwilight || (s.aurora == 0 && s.lastAurora == 0) || !s.uniformsValid) {
                    if (s.aurora == 0 && s.lastAurora == 0) s.grid.reset();
                    continue;
                }
                u     = s.uniforms;
                scale = s.auroraScale;
            }
            constexpr int n      = 96; // about 11 blocks a cell at a 192-block fog end
            float const   extent = std::min(scale, std::max(u.fogEnd, 1.0f) * 2.75f);
            auto          grid   = std::make_shared<AuroraGrid>();
            grid->camX           = u.camX;
            grid->camY           = u.camY;
            grid->camZ           = u.camZ;
            grid->n              = n;
            grid->vertices.reserve(static_cast<std::size_t>((n + 1) * (n + 1)));
            for (int j = 0; j <= n; ++j)
                for (int i = 0; i <= n; ++i) {
                    float px = -extent + 2.0f * extent * static_cast<float>(i) / n;
                    float pz = -extent + 2.0f * extent * static_cast<float>(j) / n;
                    auto  f  = ca::auroraFragment(px, pz, u);
                    grid->vertices.push_back({px, 0.0f, pz, f.color.r, f.color.g, f.color.b, f.alpha});
                }
            auto&           s = shared();
            std::lock_guard lock(s.mutex);
            s.grid = std::move(grid);
        }
    }

    std::thread             mThread;
    std::mutex              mMutex;
    std::condition_variable mWake;
    bool                    mStop = false;
};
AuroraWorker& auroraWorker() {
    static AuroraWorker w;
    return w;
}

// ---- level renderer sky meshes ---------------------------------------------------------------------------------

/// The sun, moon and star meshes of the level renderer while the player is in the Twilight Forest
/// (TFSkyRenderer: no sun, no moon; TF's own stars are drawn by the stars callback).
struct SkyMeshes {
    LevelRenderer*                            owner = nullptr;
    bool                                      swapped = false;
    std::shared_ptr<mce::Mesh>                hidden;
    std::shared_ptr<mce::Mesh>                sun, stars;
    std::array<std::shared_ptr<mce::Mesh>, 8> moon;
};
SkyMeshes& skyMeshes() {
    static SkyMeshes m;
    return m;
}

void hideVanillaSky(ScreenContext& screen) {
    auto* lr = levelRenderer();
    auto& m  = skyMeshes();
    if (!lr) return;
    if (m.owner != lr) m = SkyMeshes{.owner = lr}; // a new level renderer (another world)
    if (!m.hidden) m.hidden = buildMesh(screen, {}, "gf_atmosphere_hidden");
    auto& sun   = lr->mSunMesh;
    auto& stars = lr->mStarsMesh;
    auto& moons = lr->mMoonMesh;
    if (!m.swapped) {
        m.sun   = sun;
        m.stars = stars;
        for (int i = 0; i < 8; ++i) m.moon[static_cast<std::size_t>(i)] = moons[i];
        m.swapped = true;
        log().info("atmosphere: sun, moon and vanilla stars hidden in the Twilight Forest");
    }
    // the game may rebuild them (device lost): put ours back every frame
    if (sun != m.hidden) sun = m.hidden;
    if (stars != m.hidden) stars = m.hidden;
    for (int i = 0; i < 8; ++i)
        if (moons[i] != m.hidden) moons[i] = m.hidden;
}

void restoreVanillaSky() {
    auto& m = skyMeshes();
    if (!m.swapped) return;
    auto* lr = levelRenderer();
    if (lr && lr == m.owner) {
        lr->mSunMesh   = m.sun;
        lr->mStarsMesh = m.stars;
        for (int i = 0; i < 8; ++i) lr->mMoonMesh[i] = m.moon[static_cast<std::size_t>(i)];
        log().info("atmosphere: sun, moon and stars restored");
    }
    m.swapped = false;
    m.sun.reset();
    m.stars.reset();
    for (auto& mo : m.moon) mo.reset();
}

// ---- providers (render thread) ----------------------------------------------------------------------------------

float renderDistanceBlocks() {
    auto* lr = levelRenderer();
    if (!lr || !lr->mLevelRendererPlayer.get()) return 192.0f;
    float rd = lr->mLevelRendererPlayer.get()->mRenderDistance;
    logOnce("render_distance", fmt::format("atmosphere: level renderer render distance {:.1f}", rd));
    if (rd <= 0) return 192.0f;
    return rd < 64.0f ? rd * 16.0f : rd; // chunks or blocks
}

/// FogHandler: one step of the smoothed fog planes for the camera's biome (by the time since the
/// last step, in frames of 1/60 s), returns the terrain planes to use.
std::optional<ca::FogPlanes> stepFog(std::string const& biome, double y, bool chunkLoaded) {
    auto&       s     = shared();
    float const rd    = renderDistanceBlocks();
    auto const  now   = std::chrono::steady_clock::now();
    bool const  foggy = ca::isFoggyAt(biome, y);
    bool const  spook = ca::isSpooky(biome);
    std::lock_guard lock(s.mutex);
    float frames = 1.0f;
    if (s.fogStepped.time_since_epoch().count() != 0) frames = std::chrono::duration<float>(now - s.fogStepped).count() * 60.0f;
    s.fogStepped = now;
    frames       = std::clamp(frames, 0.0f, 600.0f);
    s.fog.sky(ca::vanillaFog(true, rd, foggy), spook, chunkLoaded, frames);
    auto t           = s.fog.terrain(ca::vanillaFog(false, rd, foggy), spook, chunkLoaded, frames);
    s.renderDistance = rd;
    s.fogPlanes      = t ? t : (s.fog.terrainLoaded() ? std::optional(s.fog.terrainPlanes()) : std::nullopt);
    if (!s.fogPlanes) s.fogPlanes = ca::vanillaFog(false, rd, foggy);
    return s.fogPlanes;
}

ca::Rgb fogColourOf(std::string const& biome) {
    auto look = ca::biomeLook(biome);
    if (!look) return {};
    int chunks = static_cast<int>(renderDistanceBlocks() / 16.0f);
    return ca::fogColor(ca::rgbOf(look->fog), ca::rgbOf(look->sky), ca::daylight(ca::timeOfDay(ca::kFixedTime)), chunks);
}

std::string previewBiomeOr(std::string const& biome) {
    std::lock_guard lock(shared().mutex);
    return shared().preview.biome ? *shared().preview.biome : biome;
}

std::optional<sky::FogSpec> fogFor(std::string const& playerBiome) {
    if (!config().fog) return std::nullopt;
    auto* p = currentPlayer();
    if (!p) return std::nullopt;
    std::string biome = previewBiomeOr(playerBiome);
    if (!ca::biomeLook(biome)) return std::nullopt; // not a TF biome: the game's own fog
    auto const& pos   = p->getPosition();
    BlockPos    bp{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    auto        planes = stepFog(biome, pos.y, p->getDimensionBlockSource().hasChunksAt(bp, 0, false));
    if (!planes) return std::nullopt;
    auto c = fogColourOf(biome);
    logOnce("fog:" + biome, fmt::format("atmosphere: fog of {} {:.1f}..{:.1f}", biome, planes->start, planes->end));
    return sky::FogSpec{{c.r, c.g, c.b}, planes->start, planes->end};
}

/// The stars (TFSkyRenderer#createStars, rotated as renderSky does at the fixed time).
std::vector<ColourVertex> const& starVertices() {
    static std::vector<ColourVertex> const v = [] {
        std::vector<ColourVertex> out;
        float const               tod = ca::timeOfDay(ca::kFixedTime);
        for (auto const& q : ca::starQuads()) {
            std::array<ca::Vec3f, 4> r{};
            for (int i = 0; i < 4; ++i) r[static_cast<std::size_t>(i)] = ca::rotateStar(q[static_cast<std::size_t>(i)], tod);
            // both windings: the stars face the camera whatever the material culls
            for (int i : {0, 1, 2, 3}) out.push_back({r[static_cast<std::size_t>(i)].x, r[static_cast<std::size_t>(i)].y, r[static_cast<std::size_t>(i)].z, 1, 1, 1, 1});
            for (int i : {3, 2, 1, 0}) out.push_back({r[static_cast<std::size_t>(i)].x, r[static_cast<std::size_t>(i)].y, r[static_cast<std::size_t>(i)].z, 1, 1, 1, 1});
        }
        return out;
    }();
    return v;
}

/// Pushes one quad of four vertices with the preview's winding (0: as given, 1: reversed, 2: both).
void pushQuad(std::vector<ColourVertex>& out, ColourVertex const& a, ColourVertex const& b, ColourVertex const& c, ColourVertex const& d, int winding) {
    if (winding != 1) out.insert(out.end(), {a, b, c, d});
    if (winding != 0) out.insert(out.end(), {d, c, b, a});
}

void drawAurora(render::RenderContext& ctx, float partial) {
    std::shared_ptr<AuroraGrid const> grid;
    int                               last = 0, cur = 0, winding = 0, mode = 0;
    {
        auto&           s = shared();
        std::lock_guard lock(s.mutex);
        grid    = s.grid;
        last    = s.lastAurora;
        cur     = s.aurora;
        winding = s.preview.winding;
        mode    = s.preview.auroraMode;
    }
    if (!grid || (cur == 0 && last == 0)) return;
    // TF blends the aurora over the sky (SRC_ALPHA, 1 - SRC_ALPHA): the particle engine's blend
    // material with a white texture; the stars material (additive, no alpha) needs premultiplied
    // colours (preview auroraMode 1, the e2e calibration)
    auto const* mat = mode == 1 ? starsMaterial() : particleBlendMaterial();
    auto const* tex = texture("textures/atmosphere/white");
    if (!mat || (mode != 1 && !tex)) return;
    float const mod = ca::auroraAlpha(last, cur, partial);
    // the grid moved with the camera since the worker computed it; the plane is at y 256 and is drawn
    // scaled towards the camera (1/8: the same picture, inside the sky's depth range)
    float const k  = 0.125f;
    float const ox = static_cast<float>(grid->camX - ctx.camera.x), oz = static_cast<float>(grid->camZ - ctx.camera.z);
    float const y  = ca::auroraHeight(ctx.camera.y) * k;
    int const   n  = grid->n;
    std::vector<ColourVertex> quads;
    quads.reserve(static_cast<std::size_t>(n * n * 4));
    auto vtx = [&](int i, int j) {
        ColourVertex v = grid->vertices[static_cast<std::size_t>(j * (n + 1) + i)];
        v.x            = (v.x + ox) * k;
        v.z            = (v.z + oz) * k;
        v.y            = y;
        v.a            = std::clamp(v.a * mod, 0.0f, 1.0f);
        if (mode == 1) v = ColourVertex{v.x, v.y, v.z, v.r * v.a, v.g * v.a, v.b * v.a, v.a};
        return v;
    };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            auto a = vtx(i, j), b = vtx(i + 1, j), c = vtx(i + 1, j + 1), d = vtx(i, j + 1);
            if (a.a <= 0.002f && b.a <= 0.002f && c.a <= 0.002f && d.a <= 0.002f) continue;
            pushQuad(quads, a, b, c, d, winding);
        }
    if (mode == 1) drawColouredQuads(ctx.screen, *mat, quads);
    else drawTintedQuads(ctx.screen, *mat, *tex, quads);
    logOnce("aurora_drawn", fmt::format("atmosphere: aurora drawn ({} quads, alpha {:.2f}, {})", quads.size() / 4, mod, mode == 1 ? "stars material" : "blend material"));
}

/// Publishes the aurora shader's uniforms for the worker (render thread, once per frame).
void publishAuroraUniforms(render::RenderContext const& ctx) {
    auto* c     = currentClient();
    auto* level = c ? c->getLevel() : nullptr;
    if (!level) return;
    auto& s = shared();
    float fogEnd = 192.0f, fogStart = 0.0f;
    std::string biome;
    {
        std::lock_guard lock(s.mutex);
        if (s.fogPlanes) {
            fogStart = s.fogPlanes->start;
            fogEnd   = s.fogPlanes->end;
        } else {
            auto v   = ca::vanillaFog(false, s.renderDistance, false);
            fogStart = v.start;
            fogEnd   = v.end;
        }
        biome = s.cameraBiome;
    }
    auto               fc = fogColourOf(biome);
    ca::AuroraUniforms u;
    u.seed     = ca::auroraSeed(static_cast<std::int64_t>(level->getLevelSeed64().mValue));
    u.camX     = static_cast<float>(ctx.camera.x);
    u.camY     = static_cast<float>(ctx.camera.y);
    u.camZ     = static_cast<float>(ctx.camera.z);
    auto tick  = level->getCurrentTick().tickID;
    u.gameTime = (static_cast<float>(tick % 24000) + ctx.partialTicks) / 24000.0f;
    u.fogStart = fogStart;
    u.fogEnd   = fogEnd;
    u.fogColor = fc;
    std::lock_guard lock(s.mutex);
    s.uniforms      = u;
    s.auroraScale   = ca::auroraScale(renderDistanceBlocks());
    s.uniformsValid = true;
}

bool skyFrame(render::RenderContext& ctx, float) {
    auto cfg = config();
    if (cfg.sky) hideVanillaSky(ctx.screen);
    else restoreVanillaSky();
    publishAuroraUniforms(ctx);
    return false; // the vanilla sky dome (biome sky colour) stays
}

void starsFrame(render::RenderContext& ctx, float partial) {
    auto cfg = config();
    if (cfg.sky) {
        if (auto const* mat = starsMaterial()) {
            drawColouredQuads(ctx.screen, *mat, starVertices());
            logOnce("stars", fmt::format("atmosphere: TF stars drawn ({} quads)", starVertices().size() / 8));
        }
    }
    if (cfg.aurora) drawAurora(ctx, partial);
}

/// TFWeatherRenderer#renderSnowAndRain: the locked-biome columns and the protected structure sparkles.
bool weatherFrame(render::RenderContext& ctx, float partial) {
    auto cfg = config();
    if (!cfg.weather) return false;
    auto* p = currentPlayer();
    if (!p) return false;
    std::vector<std::string>                           enforcement;
    bool                                               near = false;
    int                                                cx = 0, cz = 0, ticks = 0, winding = 0;
    std::optional<std::vector<std::pair<core::BlockBox, bool>>> boxes;
    bool                                               previewLocked = false;
    {
        auto&           s = shared();
        std::lock_guard lock(s.mutex);
        if (!s.inTwilight) return false;
        near          = s.nearLocked;
        cx            = s.cx;
        cz            = s.cz;
        ticks         = s.ticks;
        winding       = s.preview.winding;
        boxes         = s.preview.boxes;
        previewLocked = s.preview.locked;
        if (near) enforcement = s.enforcement;
    }
    bool const gameMode = !p->isCreative() && !p->isSpectator();
    if (!previewLocked && !(progression::client::enforced() && gameMode)) return false;
    auto const* mat = particleBlendMaterial();
    if (!mat) return false;
    int const    range = ca::weatherRange(true);
    double const camX = ctx.camera.x, camY = ctx.camera.y, camZ = ctx.camera.z;
    int const    px = static_cast<int>(std::floor(camX)), py = static_cast<int>(std::floor(camY)), pz = static_cast<int>(std::floor(camZ));
    int const    minBuild = p->getDimension().mHeightRange->mMin;
    std::map<ca::Effect, std::vector<ca::Column>> batches;
    auto flipped = [](ca::Column c) {
        std::swap(c.x0, c.x1);
        std::swap(c.z0, c.z1);
        std::swap(c.u0, c.u1);
        return c;
    };
    // the preview's winding: 0 as Java gives the corners, 1 reversed, 2 both
    auto add = [&](ca::Column const& c) {
        auto& b = batches[c.effect];
        if (winding != 1) b.push_back(c);
        if (winding != 0) b.push_back(flipped(c));
    };
    if (near) {
        constexpr int r    = ca::kLockedBiomeScan;
        auto          span = ca::lockedSpan(py, range, minBuild);
        for (int z = pz - range; span && z <= pz + range; ++z)
            for (int x = px - range; x <= px + range; ++x) {
                int gx = x - cx, gz = z - cz;
                if (gx < -r || gx > r || gz < -r || gz > r) continue;
                auto const& enf = enforcement[static_cast<std::size_t>((gz + r) * (2 * r + 1) + gx + r)];
                if (enf.empty()) continue;
                ca::ColumnInput in{.x = x, .z = z, .minY = span->first, .maxY = span->second, .ticks = ticks, .partialTicks = partial, .camX = camX, .camY = camY, .camZ = camZ, .range = range};
                if (auto col = ca::lockedColumn(in, enf)) {
                    if (!col->fullBright) { // big rain: the world's light at its own column, y 0 (LevelRenderer#getLightColor)
                        auto&    region = p->getDimensionBlockSource();
                        BlockPos at{x, 0, z};
                        float    br = region.hasChunksAt(at, 0, false) ? std::clamp(region.getBrightness(at), 0.05f, 1.0f) : 1.0f;
                        col->color  = {col->color.r * br, col->color.g * br, col->color.b * br};
                        col->fullBright = true; // lit already
                    }
                    add(*col);
                }
            }
    }
    // renderLockedStructure: sparkles inside the protected parts of the boxes near the camera
    auto all = boxes ? *boxes : ::gf::landmark::ext::client::structureProtection();
    if (!all.empty()) {
        auto pBox = ca::cameraBox(px, py, pz, range);
        if (std::any_of(all.begin(), all.end(), [&](auto const& b) { return b.first.intersects(pBox); })) {
            for (int x = pBox.minX; x <= pBox.maxX; ++x)
                for (int z = pBox.minZ; z <= pBox.maxZ; ++z)
                    for (auto const& [lo, hi] : ca::sparkleIntervals(x, z, pBox, all)) {
                        if (lo >= hi) continue;
                        ca::ColumnInput in{.x = x, .z = z, .minY = lo, .maxY = hi, .ticks = ticks, .partialTicks = partial, .camX = camX, .camY = camY, .camZ = camZ, .range = range};
                        if (auto col = ca::sparkleColumn(in)) add(*col);
                    }
        }
    }
    if (batches.empty()) return false;
    for (auto const& [effect, cols] : batches) {
        auto const* tex = texture(ca::textureOf(effect));
        if (!tex) continue;
        drawColumns(ctx.screen, *mat, *tex, cols, 1.0f);
        logOnce(std::string("weather:") + ca::nameOf(effect), fmt::format("atmosphere: {} weather drawn ({} columns)", ca::nameOf(effect), cols.size()));
    }
    return false; // TF renders the normal weather anyway
}

std::optional<float> cloudsAt() { return ca::kCloudHeight; }

} // namespace

void installProvider() {
    int dim = gDimension.load();
    if (dim < 0) return;
    sky::setProvider(dim, sky::SkyProvider{.sky = skyFrame, .stars = starsFrame, .fog = fogFor, .clouds = cloudsAt, .weather = weatherFrame});
    log().info("atmosphere: sky provider set for dimension {}", dim);
}

namespace {

void applyPreview(Json const& msg) {
    Preview p;
    if (msg.contains("biome") && msg["biome"].is_string()) p.biome = msg["biome"].get<std::string>();
    p.locked  = msg.value("locked", false);
    p.urGhast = msg.value("urGhast", false);
    p.winding = msg.value("winding", 0);
    p.auroraMode = msg.value("auroraMode", 0);
    if (msg.contains("protected") && msg["protected"].is_array()) {
        std::vector<std::pair<core::BlockBox, bool>> boxes;
        for (auto const& b : msg["protected"])
            if (b.is_array() && b.size() == 7) boxes.push_back({core::BlockBox{b[0].get<int>(), b[1].get<int>(), b[2].get<int>(), b[3].get<int>(), b[4].get<int>(), b[5].get<int>()}, b[6].get<bool>()});
        p.boxes = std::move(boxes);
    }
    {
        std::lock_guard lock(shared().mutex);
        shared().preview = p;
    }
    forgetOnce({"fog:", "weather:", "aurora_drawn", "aurora_in:", "locked_near", "ur_ghast_rain"});
    log().info("atmosphere: preview {}", msg.dump());
}

class AtmosphereClientModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            try {
                clientTick();
            } catch (std::exception const& e) {
                if (onceFlag("tick_error")) log().error("atmosphere: client tick failed: {}", e.what());
            }
        });
        mExit = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            auto&           s = shared();
            std::lock_guard lock(s.mutex);
            s.fog.reset(); // FogHandler#unloadFog
            s.fogStepped    = {};
            s.fogPlanes.reset();
            s.preview       = {};
            s.aurora = s.lastAurora = 0;
            s.grid.reset();
            s.uniformsValid = false;
            s.urGhastActive = false;
            s.urGhastRain   = 0;
            s.inTwilight    = false;
            // the swapped sky meshes stay recorded: the next frame outside the Twilight Forest puts
            // them back if the level renderer survives the level (restoreVanillaSky)
        });
        net::client::on(at::kChannelUrGhast, [](Json const& msg) {
            bool active = msg.value("active", false);
            {
                std::lock_guard lock(shared().mutex);
                shared().urGhastActive = active;
            }
            log().info("atmosphere: Ur-Ghast tantrum {}", active ? "on" : "off");
        });
        net::client::on(at::kChannelPreview, applyPreview);
        // the sun and moon come back outside the Twilight Forest (render thread)
        render::onWorld([](render::RenderContext&, float) {
            auto* p = currentPlayer();
            if (!p || !inTwilight(*p)) restoreVanillaSky();
        });
        auroraWorker().start();
        if (auto tf = ::gf::world::twilight()) {
            gDimension = static_cast<int>(*tf);
            installProvider();
        }
    }
    void uninstall() {
        auroraWorker().stop();
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mExit);
        if (int dim = gDimension.load(); dim >= 0) sky::clearProvider(dim);
    }

private:
    ll::event::ListenerPtr mTick, mExit;
};

} // namespace

// ---- selftest access (client selftests) -----------------------------------------------------------------------

AtmosphereState state() {
    auto&           s = shared();
    std::lock_guard lock(s.mutex);
    return AtmosphereState{s.inTwilight, s.aurora, s.lastAurora, s.urGhastRain, s.cameraBiome, s.nearLocked, gDimension.load(), static_cast<bool>(s.grid)};
}

void setPreview(Json const& msg) { applyPreview(msg); }

std::optional<sky::FogSpec> fogForTest(std::string const& biome) { return fogFor(biome); }

} // namespace gf::client::atmosphere

GF_REGISTER_MODULE(gf::client::atmosphere::AtmosphereClientModule);

#endif // GF_CLIENT

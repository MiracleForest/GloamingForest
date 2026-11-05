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
// - sky: the vanilla sky dome in the biome's sky colour, no sun, no moon and no sunrise colour (the
//   level renderer's sun / moon meshes are swapped for an empty one while the player is in the
//   Twilight Forest; the sunrise band through the view override of ext/atmosphere/SkyView.h), TF's
//   3000-try star field (seed 10842) at full brightness instead of the vanilla stars, and the void
//   plate (the dark disc) when the camera is below the dimension's floor;
// - fog: FogHandler's smoothed planes (the spooky forest halves them, the dark forests below y 20
//   and a boss bar asking for world fog use the near fog) in the colour of
//   getBrightnessDependentFogColor;
// - clouds at y 128 in ClientLevel#getCloudColor's colour (no sunset tint);
// - weather: TFWeatherRenderer's locked-biome columns (blizzard, mosquitoes, ashes, dark streams,
//   big rain) and the sparkles in protected structure pieces, 10 columns around the camera with
//   fancy graphics and 5 without; vanilla weather still renders;
// - the aurora (ClientGameEvents#renderAurora) in the biomes of the config's auroraBiomes, in any
//   dimension: the shader evaluated per vertex of a fine polar mesh (cells of about half a degree
//   seen from the camera) on worker threads, the colours interpolated across the cells;
// - tickRain's Ur-Ghast rain: splashes and rain sounds while the Ur-Ghast is in its tantrum
//   ("atmosphere.ur_ghast").
// Graphics modes: in Classic the stars, the void plate and the aurora draw in the sky pass (the
// terrain and the clouds then draw over them, as the depth test does in Java); Vibrant Visuals
// paints its own sky after that pass, so there they draw in the world pass (depth tested, at the
// far plane), and the provider's fog is thinned so the VV sky stays visible through it.
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
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "glm/gtc/matrix_transform.hpp"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/comprehensive/ParticleType.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "client/Client.h"
#include "client/atmosphere/AtmosphereClient.h"
#include "client/atmosphere/Draw.h"
#include "client/world/Sky.h"
#include "core/atmosphere/Atmosphere.h"
#include "core/jrand/JRand.h"
#include "feature/atmosphere/Atmosphere.h"
#include "feature/dimension/World.h"
#include "feature/ext/atmosphere/SkyView.h"
#include "feature/ext/entity/EntityClientExt.h"
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
namespace vx  = feature::ext::atmosphere::client;
using Json    = nlohmann::json;

namespace {

// ---- state ------------------------------------------------------------------------------------------------

/// How the aurora / stars are drawn (Pass, material). Auto picks by graphics mode and dimension; the
/// others are the e2e calibration's previews.
enum class AuroraMode { Auto = 0, SkyUi = 1, WorldBlend = 2, SkyStars = 3, WorldWire = 4, SkyBlend = 5 };
enum class StarsMode { Auto = 0, SkyStars = 1, WorldBlend = 2, WorldStars = 3 };

/// A development preview ("atmosphere.preview").
struct Preview {
    std::optional<std::string>                                  biome;          ///< every biome lookup answers this
    bool                                                        locked = false; ///< the preview biome is locked
    std::optional<std::vector<std::pair<core::BlockBox, bool>>> boxes;          ///< protected boxes instead of F6's
    bool                                                        urGhast    = false;
    int                                                         winding    = 0; ///< 0 one side, 1 the other, 2 both
    AuroraMode                                                  auroraMode = AuroraMode::Auto;
    StarsMode                                                   starsMode  = StarsMode::Auto;
    float                                                       vvFog      = 0; ///< VV fog thinning (0: the default)
    bool                                                        voidPlate  = false; ///< draw the void plate anyway
    bool                                                        bossFog    = false; ///< a boss bar with world fog
};

/// The aurora mesh the worker computed: camera-relative positions (at `cam`) and colours.
struct AuroraGrid {
    double                    camX = 0, camY = 0, camZ = 0;
    ca::AuroraMesh            mesh;
    std::vector<ColourVertex> vertices; ///< (rings + 1) x segments, alpha before the colour modulator
};

struct Shared {
    std::mutex mutex;
    // client tick
    int         ticks  = 0;
    int         aurora = 0, lastAurora = 0;
    float       urGhastRain   = 0;
    bool        urGhastActive = false;
    std::string cameraBiome;
    bool        inTwilight = false;
    int         playerDim  = -1;
    bool        bossFog    = false;
    // locked-biome weather: enforcement per column of the (2 * 15 + 1)^2 square around (cx, cz)
    bool                     nearLocked = false;
    int                      cx = 0, cz = 0;
    std::vector<std::string> enforcement;
    int                      scanX = 0, scanZ = 0, scanTick = -1000, scanGen = -1;
    // aurora
    std::shared_ptr<AuroraGrid const> grid;
    ca::AuroraUniforms                uniforms;
    bool                              uniformsValid = false;
    // fog (render thread)
    ca::FogSmoother                       fog;
    std::chrono::steady_clock::time_point fogStepped{};
    std::optional<ca::FogPlanes>          fogPlanes;
    float                                 renderDistance = 192;
    Preview                               preview;
    int                                   previewGen = 0;
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
    std::vector<std::string> auroraBiomes; ///< parsed resource locations, as Bedrock names them
    bool                     sky = true, fog = true, weather = true, aurora = true;
};

/// The ids of auroraBiomes no biome of the current world has (warned once each, skipped).
struct UnknownBiomes {
    std::mutex                      mutex;
    std::unordered_set<std::string> unknown, checked;
};
UnknownBiomes& unknownBiomes() {
    static UnknownBiomes u;
    return u;
}

/// TFConfig#getValidAuroraBiomes: the configured ids that parse as resource locations (an invalid
/// one is warned about once and skipped; so is one that names no biome of the world, once the
/// world's biomes are known). Parsed again only when the config section changes.
std::shared_ptr<AtmosphereConfigView const> config() {
    struct Cache {
        std::mutex                                  mutex;
        bool                                        init = false;
        std::vector<std::string>                    raw;
        std::array<bool, 4>                         flags{};
        std::shared_ptr<AtmosphereConfigView const> view;
    };
    static Cache cache;
    auto const&  c = gf::config::get<at::AtmosphereConfig>();
    std::array<bool, 4> flags{c.sky, c.fog, c.weather, c.aurora};
    std::lock_guard     lock(cache.mutex);
    if (cache.init && cache.raw == c.auroraBiomes && cache.flags == flags) return cache.view;
    auto v = std::make_shared<AtmosphereConfigView>();
    v->sky = c.sky, v->fog = c.fog, v->weather = c.weather, v->aurora = c.aurora;
    for (auto const& id : c.auroraBiomes) {
        auto rl = ca::parseResourceLocation(id);
        if (!rl) {
            if (onceFlag("aurora_invalid:" + id)) log().warn("Biome {} in Twilight Forest's validAuroraBiomes config option is not a valid biome. Skipping!", id);
            continue;
        }
        v->auroraBiomes.push_back(ca::bedrockBiomeName(*rl));
    }
    cache.init  = true;
    cache.raw   = c.auroraBiomes;
    cache.flags = flags;
    cache.view  = v;
    {
        std::lock_guard l2(unknownBiomes().mutex);
        unknownBiomes().checked.clear(); // check the new list against the world's biomes again
    }
    return cache.view;
}

/// The second half of getValidAuroraBiomes: ids the world has no biome of are warned about once and
/// skipped (TF's own biomes always count, the client may not have their definitions yet).
void checkAuroraBiomes(Level& level, AtmosphereConfigView const& cfg) {
    std::lock_guard lock(unknownBiomes().mutex);
    auto const&     reg = std::as_const(level).getBiomeRegistry();
    if (reg.lookupByName("plains") == nullptr) return; // the world's biomes are not known yet
    for (auto const& name : cfg.auroraBiomes) {
        if (!unknownBiomes().checked.insert(name).second) continue;
        bool known = ca::biomeLook(name).has_value();
        if (!known) known = reg.lookupByName(name) != nullptr || reg.lookupByName("minecraft:" + name) != nullptr;
        if (known) unknownBiomes().unknown.erase(name);
        else {
            unknownBiomes().unknown.insert(name);
            log().warn("Biome {} in Twilight Forest's validAuroraBiomes config option is not a valid biome. Skipping!", name);
        }
    }
}

bool auroraBiome(AtmosphereConfigView const& cfg, std::string const& biome) {
    if (biome.empty() || std::find(cfg.auroraBiomes.begin(), cfg.auroraBiomes.end(), biome) == cfg.auroraBiomes.end()) return false;
    std::lock_guard lock(unknownBiomes().mutex);
    return !unknownBiomes().unknown.contains(biome);
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

BlockPos blockPosOf(Vec3 const& pos) { return {static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))}; }

// ---- client tick -----------------------------------------------------------------------------------------------

/// tickRain's splashes and rain sounds of the Ur-Ghast rain (the level's own rain is the game's):
/// the random of RandomSource.create(ticks * 312987231), columns of biomes with precipitation that
/// are warm enough to rain, the splash at the top of the block's collision shape or fluid.
void urGhastSplashes(LocalPlayer& p, float level, int ticks, int& rainSoundTime) {
    if (level <= 0.0f) return;
    auto&                   region = p.getDimensionBlockSource();
    BlockPos const          at     = blockPosOf(p.getPosition());
    core::jrand::LegacyRandom random(ca::rainTickSeed(ticks));
    int const               n    = ca::splashCount(level, false); // Bedrock has no "decreased particles" option
    auto&                   lvl  = p.getLevel();
    int const               minY = p.getDimension().mHeightRange->mMin;
    std::optional<BlockPos> splash;
    for (int j = 0; j < n; ++j) {
        int      k = random.nextInt(21) - 10;
        int      l = random.nextInt(21) - 10;
        BlockPos column{at.x + k, 0, at.z + l};
        if (!region.hasChunksAt(column, 0, false)) continue;
        BlockPos top = region.getHeightmapPos(column);
        if (top.y <= minY || top.y > at.y + 10 || top.y < at.y - 10) continue;
        Biome const& biome = region.getBiome(top);
        if (!biome.mRain || biome.getTemperature(region, top) < 0.15f) continue; // hasPrecipitation, warmEnoughToRain
        BlockPos below{top.x, top.y - 1, top.z};
        splash          = below;
        double      d0  = random.nextDouble();
        double      d1  = random.nextDouble();
        auto const& b   = region.getBlock(below);
        auto const  id  = game::blockId(b);
        auto const& liq = region.getLiquidBlock(below);
        auto const  lid = game::blockId(liq);
        bool const  lava = id == "minecraft:lava" || id == "minecraft:flowing_lava" || lid == "minecraft:lava" || lid == "minecraft:flowing_lava";
        bool const  water = id == "minecraft:water" || id == "minecraft:flowing_water" || lid == "minecraft:water" || lid == "minecraft:flowing_water";
        bool        campfire = id == "minecraft:campfire" || id == "minecraft:soul_campfire";
        bool const  litCampfire = campfire && game::statesOf(b)["extinguished"] != "true"; // CampfireBlock#isLitCampfire
        bool const  hot = lava || id == "minecraft:magma" || litCampfire;
        // VoxelShape#max(Y) of the collision shape, FluidState#getHeight
        double shapeTop = 0.0;
        try {
            AABB box = b.getBlockType().getCollisionShape(b, region, below, {});
            if (box.max.y > box.min.y) shapeTop = std::clamp(static_cast<double>(box.max.y) - below.y, 0.0, 1.5);
        } catch (...) {}
        double fluidTop = 0.0;
        if (lava || water) {
            auto const& fluid  = (lid == "minecraft:air" || lid.empty()) ? b : liq;
            auto const  aboveL = game::blockId(region.getLiquidBlock(top));
            auto const  aboveB = game::blockId(region.getBlock(top));
            bool        same   = lava ? (aboveL.find("lava") != std::string::npos || aboveB.find("lava") != std::string::npos)
                                      : (aboveL.find("water") != std::string::npos || aboveB.find("water") != std::string::npos);
            fluidTop           = ca::fluidHeight(game::intState(fluid, "liquid_depth").value_or(0), same);
        }
        Vec3 where{static_cast<float>(below.x + d0), static_cast<float>(below.y + std::max(shapeTop, fluidTop)), static_cast<float>(below.z + d1)};
        lvl.addParticle(hot ? ParticleType::Smoke : ParticleType::RainSplash, where, Vec3{}, 0, nullptr, false);
    }
    if (splash && random.nextInt(4) < rainSoundTime++) {
        rainSoundTime = 0;
        auto* c       = currentClient();
        if (!c) return;
        Vec3 where{static_cast<float>(splash->x), static_cast<float>(splash->y), static_cast<float>(splash->z)};
        bool above = splash->y > at.y + 1 && region.getHeightmapPos(at).y > at.y;
        // weather.rain.above (0.1, 0.5) / weather.rain (0.2, 1)
        c->getSoundEngine()->play("ambient.weather.rain", where, above ? 0.1f : 0.2f, above ? 0.5f : 1.0f, std::nullopt);
    }
}

/// BossOverlay#shouldCreateWorldFog: a boss bar the player sees asks for world fog (the companion's
/// TFBossBarPacket state; Bedrock's own BossEventPacket has no such flag).
bool bossWorldFog(LocalPlayer& p) {
    auto const& pos = p.getPosition();
    for (Actor* a : p.getLevel().getRuntimeActorList()) {
        if (!a || a->getDimensionId() != p.getDimensionId()) continue;
        auto const& ap = a->getPosition();
        float       dx = ap.x - pos.x, dz = ap.z - pos.z;
        if (dx * dx + dz * dz > 160.0f * 160.0f) continue;
        if (auto bar = entity::bossBar(a->getOrCreateUniqueID().rawID); bar && bar->fog) return true;
    }
    return false;
}

void installProviderFor(int dim);
void clearProviderFor(int dim);

void clientTick() {
    auto* p = currentPlayer();
    // the TF dimension's engine id: the local server's registry, or (a remote server) the dimension
    // the player is in when its name is TF's
    int dim = gDimension.load();
    if (p && ::gf::world::isTwilight(p->getDimension())) dim = static_cast<int>(p->getDimensionId());
    else if (dim < 0)
        if (auto tf = ::gf::world::twilight()) dim = static_cast<int>(*tf);
    if (dim >= 0 && dim != gDimension.load()) {
        int old = gDimension.exchange(dim);
        if (old >= 0) clearProviderFor(old);
        installProviderFor(dim);
    }
    auto& s = shared();
    if (!p) {
        std::lock_guard lock(s.mutex);
        s.inTwilight = false;
        s.aurora = s.lastAurora = 0;
        s.nearLocked            = false;
        s.urGhastRain           = 0;
        s.playerDim             = -1;
        return;
    }
    auto const cfg = config();
    checkAuroraBiomes(p->getLevel(), *cfg);
    Preview preview;
    int     previewGen = 0, scanX = 0, scanZ = 0, scanTick = 0, scanGen = 0, ticks = 0;
    {
        std::lock_guard lock(s.mutex);
        preview    = s.preview;
        previewGen = s.previewGen;
        scanX = s.scanX, scanZ = s.scanZ, scanTick = s.scanTick, scanGen = s.scanGen, ticks = s.ticks + 1;
    }
    bool const  twilight = inTwilight(*p);
    auto&       region   = p->getDimensionBlockSource();
    BlockPos    bp       = blockPosOf(p->getPosition());
    std::string camera   = biomeAt(region, bp, preview);

    // aurora counter (ClientGameEvents#clientTick): every dimension, by the camera's biome
    bool inAurora = cfg->aurora && auroraBiome(*cfg, camera);

    // locked-biome weather: the columns around the player (isNearLockedBiome at y 0), scanned again
    // when the player's block column or the preview changes, and every second (progression)
    std::optional<std::vector<std::string>> enforcement;
    bool                                    near     = false;
    bool const                              gameMode = !p->isCreative() && !p->isSpectator();
    bool const                              scan     = twilight && cfg->weather && (preview.locked || (progression::client::enforced() && gameMode));
    if (scan && (bp.x != scanX || bp.z != scanZ || previewGen != scanGen || ticks - scanTick >= 20)) {
        constexpr int r = ca::kLockedBiomeScan;
        enforcement.emplace(static_cast<std::size_t>((2 * r + 1) * (2 * r + 1)), std::string{});
        std::unordered_map<std::string, std::string> memo;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                auto biome = biomeAt(region, BlockPos{bp.x + dx, 0, bp.z + dz}, preview);
                auto it    = memo.find(biome);
                if (it == memo.end()) it = memo.emplace(biome, lockedEnforcement(biome, preview)).first;
                if (!it->second.empty()) near = true;
                (*enforcement)[static_cast<std::size_t>((dz + r) * (2 * r + 1) + dx + r)] = it->second;
            }
    }
    bool const bossFog = preview.bossFog || (twilight && ticks % 10 == 0 ? bossWorldFog(*p) : false);

    static int rainSoundTime = 0;
    float      urRain        = 0;
    float      levelRain     = 0;
    if (auto* w = p->getDimension().mWeather.get()) levelRain = w->mRainLevel;
    {
        std::lock_guard lock(s.mutex);
        s.ticks      = ticks;
        s.inTwilight = twilight;
        s.playerDim  = static_cast<int>(p->getDimensionId());
        s.lastAurora = s.aurora;
        s.aurora     = ca::tickAurora(s.aurora, inAurora, cfg->auroraBiomes.empty() || !cfg->aurora);
        s.cameraBiome = camera;
        if (!scan) s.nearLocked = false;
        else if (enforcement) {
            s.nearLocked  = near;
            s.cx          = bp.x;
            s.cz          = bp.z;
            s.enforcement = std::move(*enforcement);
            s.scanX = bp.x, s.scanZ = bp.z, s.scanTick = ticks, s.scanGen = previewGen;
        }
        near = s.nearLocked;
        if (!twilight) s.bossFog = false;
        else if (preview.bossFog || ticks % 10 == 0) s.bossFog = bossFog;
        s.urGhastRain = twilight ? ca::tickUrGhastRain(s.urGhastRain, s.urGhastActive || preview.urGhast) : 0.0f;
        urRain        = s.urGhastRain;
    }
    if (inAurora) logOnce("aurora_in:" + camera, "atmosphere: aurora fading in (" + camera + ")");
    if (near) logOnce("locked_near", "atmosphere: near a locked biome, its weather is drawn");
    // tickRain: the Ur-Ghast's rain where it is stronger than the level's own (that one is the game's)
    if (twilight && urRain > levelRain) {
        urGhastSplashes(*p, ca::rainLevel(levelRain, urRain, fancyGraphics()), ticks, rainSoundTime);
        logOnce("ur_ghast_rain", "atmosphere: Ur-Ghast rain splashes");
    }
}

// ---- aurora worker -------------------------------------------------------------------------------------------

constexpr int kAuroraRings    = 128; ///< about 0.55 degrees a ring seen from the camera
constexpr int kAuroraSegments = 256; ///< 1.4 degrees a segment

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
                mWake.wait_for(lock, std::chrono::milliseconds(120), [this] { return mStop; });
                if (mStop) return;
            }
            ca::AuroraUniforms u;
            {
                auto&           s = shared();
                std::lock_guard lock(s.mutex);
                if ((s.aurora == 0 && s.lastAurora == 0) || !s.uniformsValid) {
                    if (s.aurora == 0 && s.lastAurora == 0) s.grid.reset();
                    continue;
                }
                u = s.uniforms;
            }
            auto grid  = std::make_shared<AuroraGrid>();
            grid->camX = u.camX;
            grid->camY = u.camY;
            grid->camZ = u.camZ;
            grid->mesh = ca::auroraMesh(ca::auroraHeight(u.camY), u.fogEnd, kAuroraRings, kAuroraSegments);
            int const rows = grid->mesh.rings + 1, segs = grid->mesh.segments;
            grid->vertices.resize(static_cast<std::size_t>(rows * segs));
            // two threads: about 45 ms for the 33 k vertices (17 noise samples each)
            auto part = [&](int from, int to) {
                for (int r = from; r < to; ++r)
                    for (int k = 0; k < segs; ++k) {
                        auto [px, pz] = ca::auroraMeshPoint(grid->mesh, r, k);
                        auto f        = ca::auroraFragment(px, pz, u);
                        grid->vertices[static_cast<std::size_t>(r * segs + k)] = {px, 0.0f, pz, f.color.r, f.color.g, f.color.b, f.alpha};
                    }
            };
            std::thread helper(part, rows / 2, rows);
            part(0, rows / 2);
            helper.join();
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
    shared(); // constructed first, so destroyed after the worker (an exit without uninstall)
    static AuroraWorker w;
    return w;
}

// ---- level renderer sky meshes ---------------------------------------------------------------------------------

/// The sun, moon and star meshes of the level renderer while the player is in the Twilight Forest
/// (TFSkyRenderer: no sun, no moon; TF's own stars are drawn by the stars callback).
struct SkyMeshes {
    LevelRenderer*                            owner   = nullptr;
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

/// The render distance in blocks (LevelRendererCamera::mRenderDistance is in blocks: 16 x the
/// chunks of the video option; the e2e logs it next to the view area for the record).
float renderDistanceBlocks() {
    auto* lr = levelRenderer();
    if (!lr || !lr->mLevelRendererPlayer.get()) return 192.0f;
    auto& player = *lr->mLevelRendererPlayer.get();
    float rd     = player.mRenderDistance;
    logOnce("render_distance", fmt::format("atmosphere: level renderer render distance {:.1f} blocks (view area {}, far chunks {:.1f})", rd, player.mViewAreaDistance, player.mFarChunksDistance));
    return rd > 0 ? rd : 192.0f;
}

/// FogHandler: one step of the smoothed fog planes for the camera's biome (by the time since the
/// last step, in frames of 1/60 s), returns the terrain planes to use.
std::optional<ca::FogPlanes> stepFog(std::string const& biome, double y, bool chunkLoaded) {
    auto&       s   = shared();
    float const rd  = renderDistanceBlocks();
    auto const  now = std::chrono::steady_clock::now();
    bool const  spook = ca::isSpooky(biome);
    std::lock_guard lock(s.mutex);
    bool const      foggy  = ca::isFoggyAt(biome, y) || s.bossFog; // isFoggyAt || BossOverlay#shouldCreateWorldFog
    float           frames = 1.0f;
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

Preview previewNow() {
    std::lock_guard lock(shared().mutex);
    return shared().preview;
}

/// The fog thinning under Vibrant Visuals: F12 turns the provider's far plane into a uniform air fog
/// (extinction 3 / end); Vibrant Visuals fogs its sky with it too, so at TF's planes the sky is the
/// fog colour. The planes are stretched by this factor there (the fog colour stays TF's).
constexpr float kVibrantFogStretch = 8.0f;

std::optional<sky::FogSpec> fogFor(std::string const& playerBiome) {
    auto cfg = config();
    if (!cfg->fog) return std::nullopt;
    auto* p = currentPlayer();
    if (!p) return std::nullopt;
    auto const  preview = previewNow();
    std::string biome   = preview.biome ? *preview.biome : playerBiome;
    if (!ca::biomeLook(biome)) return std::nullopt; // not a TF biome: the game's own fog
    auto const& pos    = p->getPosition();
    auto        planes = stepFog(biome, pos.y, p->getDimensionBlockSource().hasChunksAt(blockPosOf(pos), 0, false));
    if (!planes) return std::nullopt;
    auto c = fogColourOf(biome);
    logOnce("fog:" + biome, fmt::format("atmosphere: fog of {} {:.1f}..{:.1f}", biome, planes->start, planes->end));
    if (vibrantVisuals()) {
        float k = preview.vvFog > 0 ? preview.vvFog : kVibrantFogStretch;
        logOnce("fog_vv", fmt::format("atmosphere: Vibrant Visuals fog stretched x{:.1f}", k));
        return sky::FogSpec{{c.r, c.g, c.b}, planes->start * k, planes->end * k};
    }
    return sky::FogSpec{{c.r, c.g, c.b}, planes->start, planes->end};
}

/// The stars (TFSkyRenderer#createStars, rotated as renderSky does at the fixed time), radius 100.
std::vector<ColourVertex> const& starVertices() {
    static std::vector<ColourVertex> const v = [] {
        std::vector<ColourVertex> out;
        float const               tod = ca::timeOfDay(ca::kFixedTime);
        for (auto const& q : ca::starQuads()) {
            std::array<ca::Vec3f, 4> r{};
            for (std::size_t i = 0; i < 4; ++i) r[i] = ca::rotateStar(q[i], tod);
            // both windings: the stars face the camera whatever the material culls
            for (std::size_t i : {0u, 1u, 2u, 3u}) out.push_back({r[i].x, r[i].y, r[i].z, 1, 1, 1, 1});
            for (std::size_t i : {3u, 2u, 1u, 0u}) out.push_back({r[i].x, r[i].y, r[i].z, 1, 1, 1, 1});
        }
        return out;
    }();
    return v;
}

/// The void plate (both windings), black, radius 512 at y -4.
std::vector<ColourVertex> const& voidPlateVertices() {
    static std::vector<ColourVertex> const v = [] {
        std::vector<ColourVertex> out;
        for (auto const& q : ca::voidPlateQuads()) {
            for (std::size_t i : {0u, 1u, 2u, 3u}) out.push_back({q[i].x, q[i].y, q[i].z, 0, 0, 0, 1});
            for (std::size_t i : {3u, 2u, 1u, 0u}) out.push_back({q[i].x, q[i].y, q[i].z, 0, 0, 0, 1});
        }
        return out;
    }();
    return v;
}

std::vector<ColourVertex> scaled(std::vector<ColourVertex> const& in, float k) {
    std::vector<ColourVertex> out(in);
    for (auto& v : out) v.x *= k, v.y *= k, v.z *= k;
    return out;
}

/// Pushes one quad of four vertices with the preview's winding (0: as given, 1: reversed, 2: both).
void pushQuad(std::vector<ColourVertex>& out, ColourVertex const& a, ColourVertex const& b, ColourVertex const& c, ColourVertex const& d, int winding) {
    if (winding != 1) out.insert(out.end(), {a, b, c, d});
    if (winding != 0) out.insert(out.end(), {d, c, b, a});
}

AuroraMode auroraModeFor(Preview const& preview, bool inTwilightSky) {
    if (preview.auroraMode != AuroraMode::Auto) return preview.auroraMode;
    // the sky pass exists only in the TF provider's dimension and only Classic shows it
    return inTwilightSky && !vibrantVisuals() ? AuroraMode::SkyUi : AuroraMode::WorldBlend;
}
StarsMode starsModeFor(Preview const& preview) {
    if (preview.starsMode != StarsMode::Auto) return preview.starsMode;
    return vibrantVisuals() ? StarsMode::WorldBlend : StarsMode::SkyStars;
}
bool skyPass(AuroraMode m) { return m == AuroraMode::SkyUi || m == AuroraMode::SkyStars || m == AuroraMode::SkyBlend; }

/// Draws the aurora in the sky pass (`sky`) or the world pass; only the mode's own pass draws.
void drawAurora(render::RenderContext& ctx, float partial, bool sky) {
    std::shared_ptr<AuroraGrid const> grid;
    int                               last = 0, cur = 0;
    Preview                           preview;
    bool                              twilight = false;
    {
        auto&           s = shared();
        std::lock_guard lock(s.mutex);
        grid     = s.grid;
        last     = s.lastAurora;
        cur      = s.aurora;
        preview  = s.preview;
        twilight = s.inTwilight;
    }
    if (!grid || (cur == 0 && last == 0)) return;
    AuroraMode const mode = auroraModeFor(preview, twilight && config()->sky);
    if (skyPass(mode) != sky) return;
    mce::MaterialPtr const* mat = nullptr;
    mce::TexturePtr const*  tex = nullptr;
    char const*             name = "";
    switch (mode) {
    case AuroraMode::SkyUi: mat = materialNamed(name = "ui_fill_gradient"); break;
    case AuroraMode::SkyStars: mat = starsMaterial(), name = "stars"; break;
    case AuroraMode::WorldWire: mat = materialNamed(name = "wireframe"); break;
    case AuroraMode::SkyBlend:
    case AuroraMode::WorldBlend:
    default:
        mat  = particleBlendMaterial();
        tex  = texture("textures/atmosphere/white");
        name = "particles blend";
        if (!tex) return;
        break;
    }
    if (!mat) {
        logOnce(std::string("aurora_material:") + name, std::string("atmosphere: no ") + name + " material for the aurora");
        return;
    }
    float const mod = ca::auroraAlpha(last, cur, partial);
    // the mesh moved with the camera since the worker computed it; the plane stays at y 256
    float const ox = static_cast<float>(grid->camX - ctx.camera.x), oz = static_cast<float>(grid->camZ - ctx.camera.z);
    float const y  = ca::auroraHeight(ctx.camera.y);
    // the sky pass draws without depth: scaled towards the camera (the same picture) to within the
    // stars' 100 blocks; the world pass draws at the real place unless it is beyond the far plane
    float const maxDist = std::sqrt(grid->mesh.extent * grid->mesh.extent + y * y) + std::hypot(ox, oz);
    float       k       = 1.0f;
    if (sky) k = 100.0f / std::max(maxDist, 1.0f);
    else if (float far = farPlane(ctx.screen); far > 0) {
        k = std::min(1.0f, 0.9f * far / std::max(maxDist, 1.0f));
        logOnce("aurora_far", fmt::format("atmosphere: world pass far plane {:.0f}, aurora scaled x{:.3f}", far, k));
    }
    bool const premultiply = mode == AuroraMode::SkyStars; // the stars material ignores vertex alpha
    // the quads are built again only when the mesh, the fade, the scale or the mode changes (about
    // every 100 ms); the camera's movement since the mesh was computed is the world matrix's
    struct Cache {
        AuroraGrid const*         grid = nullptr;
        int                       fade = -1, scale = -1, winding = -1, mode = -1;
        std::vector<ColourVertex> quads;
    };
    static Cache cache[2]; // sky pass, world pass (render thread)
    Cache&      c     = cache[sky ? 0 : 1];
    int const   fade  = static_cast<int>(std::lround(mod * 240.0f));
    int const   scale = static_cast<int>(std::lround(k * 4096.0f));
    if (c.grid != grid.get() || c.fade != fade || c.scale != scale || c.winding != preview.winding || c.mode != static_cast<int>(mode)) {
        c = Cache{grid.get(), fade, scale, preview.winding, static_cast<int>(mode), {}};
        int const rings = grid->mesh.rings, segs = grid->mesh.segments;
        auto      vtx   = [&](int r, int sIdx) {
            ColourVertex v = grid->vertices[static_cast<std::size_t>(r * segs + (sIdx % segs))];
            v.x            = v.x * k;
            v.z            = v.z * k;
            v.y            = y * k;
            v.a            = std::clamp(v.a * mod, 0.0f, 1.0f);
            if (premultiply) v = ColourVertex{v.x, v.y, v.z, v.r * v.a, v.g * v.a, v.b * v.a, 1.0f};
            return v;
        };
        auto lit = [premultiply](ColourVertex const& v) { return premultiply ? (v.r + v.g + v.b) > 0.004f : v.a > 0.002f; };
        c.quads.reserve(static_cast<std::size_t>(rings * segs * 2));
        for (int r = 0; r < rings; ++r)
            for (int sIdx = 0; sIdx < segs; ++sIdx) {
                auto a = vtx(r, sIdx), b = vtx(r, sIdx + 1), cc = vtx(r + 1, sIdx + 1), d = vtx(r + 1, sIdx);
                if (!lit(a) && !lit(b) && !lit(cc) && !lit(d)) continue;
                pushQuad(c.quads, a, b, cc, d, preview.winding);
            }
    }
    auto& world  = ctx.screen.camera.worldMatrixStack.get();
    auto  ref    = world.push(false);
    ref.mat->_m.get() = ref.mat->_m.get() * glm::translate(glm::mat4{1.f}, glm::vec3{ox * k, 0.0f, oz * k});
    drawSmoothQuads(ctx.screen, *mat, tex, c.quads);
    logOnce("aurora_drawn", fmt::format("atmosphere: aurora drawn ({} quads, alpha {:.2f}, {} pass, {})", c.quads.size() / 4, mod, sky ? "sky" : "world", name));
}

/// Publishes the aurora shader's uniforms for the worker (render thread, once per frame).
void publishAuroraUniforms(render::RenderContext const& ctx) {
    auto* p = currentPlayer();
    if (!p) return;
    auto& level = p->getLevel();
    auto& s     = shared();
    float fogEnd = 192.0f, fogStart = 0.0f;
    std::string biome;
    bool        twilight = false;
    {
        std::lock_guard lock(s.mutex);
        if (s.aurora == 0 && s.lastAurora == 0) return;
        twilight = s.inTwilight;
        if (twilight && s.fogPlanes) {
            fogStart = s.fogPlanes->start;
            fogEnd   = s.fogPlanes->end;
        } else {
            auto v   = ca::vanillaFog(false, s.renderDistance, false);
            fogStart = v.start;
            fogEnd   = v.end;
        }
        biome = s.preview.biome ? *s.preview.biome : s.cameraBiome;
    }
    ca::AuroraUniforms u;
    u.seed     = ca::auroraSeed(static_cast<std::int64_t>(level.getLevelSeed64().mValue));
    u.camX     = static_cast<float>(ctx.camera.x);
    u.camY     = static_cast<float>(ctx.camera.y);
    u.camZ     = static_cast<float>(ctx.camera.z);
    auto tick  = level.getCurrentTick().tickID;
    u.gameTime = (static_cast<float>(tick % 24000) + ctx.partialTicks) / 24000.0f;
    u.fogStart = fogStart;
    u.fogEnd   = fogEnd;
    u.fogColor = fogColourOf(biome); // outside TF's biomes: black (the aurora is faded out there anyway)
    std::lock_guard lock(s.mutex);
    s.uniforms      = u;
    s.uniformsValid = true;
}

/// Stars and the void plate in the world pass (Vibrant Visuals: its sky covers the sky pass), at 90 %
/// of the far plane.
void drawWorldSky(render::RenderContext& ctx, bool stars, bool voidPlate, StarsMode mode) {
    float far = farPlane(ctx.screen);
    float r   = far > 0 ? std::min(0.9f * far, 2000.0f) : 400.0f;
    if (stars) {
        if (mode == StarsMode::WorldStars) {
            if (auto const* mat = starsMaterial()) drawColouredQuads(ctx.screen, *mat, scaled(starVertices(), r / 100.0f));
        } else if (auto const* mat = particleBlendMaterial(); mat && texture("textures/atmosphere/white")) {
            drawSmoothQuads(ctx.screen, *mat, texture("textures/atmosphere/white"), scaled(starVertices(), r / 100.0f));
        }
        logOnce("stars_world", fmt::format("atmosphere: TF stars drawn in the world pass (radius {:.0f})", r));
    }
    if (voidPlate)
        if (auto const* mat = particleBlendMaterial(); mat && texture("textures/atmosphere/white")) {
            drawSmoothQuads(ctx.screen, *mat, texture("textures/atmosphere/white"), scaled(voidPlateVertices(), r / 512.0f));
            logOnce("void_plate_world", "atmosphere: void plate drawn in the world pass");
        }
}

bool drawsVoidPlate(Preview const& preview) {
    auto* p = currentPlayer();
    if (!p) return false;
    return preview.voidPlate || ca::drawsVoidPlate(p->getPosition().y, p->getDimension().mHeightRange->mMin); // getPosition: the eyes
}

bool skyFrame(render::RenderContext& ctx, float) {
    if (config()->sky) hideVanillaSky(ctx.screen);
    else restoreVanillaSky();
    return false; // the vanilla sky dome (biome sky colour) stays
}

void starsFrame(render::RenderContext& ctx, float partial) {
    auto       cfg     = config();
    auto const preview = previewNow();
    if (cfg->sky && !vibrantVisuals()) {
        if (starsModeFor(preview) == StarsMode::SkyStars)
            if (auto const* mat = starsMaterial()) {
                drawColouredQuads(ctx.screen, *mat, starVertices());
                logOnce("stars", fmt::format("atmosphere: TF stars drawn ({} quads)", starVertices().size() / 8));
            }
        if (drawsVoidPlate(preview))
            if (auto const* mat = starsMaterial()) {
                drawColouredQuads(ctx.screen, *mat, scaled(voidPlateVertices(), 100.0f / 512.0f));
                logOnce("void_plate", "atmosphere: void plate drawn");
            }
    } else if (cfg->sky) {
        logOnce("stars", "atmosphere: TF stars drawn (world pass under Vibrant Visuals)");
    }
    if (cfg->aurora) drawAurora(ctx, partial, true);
}

/// The world pass of every dimension: the aurora's uniforms and its world-pass drawing; in the
/// Twilight Forest under Vibrant Visuals (or a calibration preview) the stars and the void plate.
void worldFrame(render::RenderContext& ctx, float partial) {
    auto* p = currentPlayer();
    if (!p || !inTwilight(*p)) restoreVanillaSky();
    if (!p) return;
    auto cfg = config();
    if (cfg->aurora) {
        publishAuroraUniforms(ctx);
        drawAurora(ctx, partial, false);
    }
    if (inTwilight(*p) && cfg->sky) {
        auto const preview = previewNow();
        StarsMode  mode    = starsModeFor(preview);
        bool const stars   = mode == StarsMode::WorldBlend || mode == StarsMode::WorldStars;
        bool const plate   = vibrantVisuals() && drawsVoidPlate(preview);
        if (stars || plate) drawWorldSky(ctx, stars, plate, mode);
    }
}

/// TFWeatherRenderer#renderSnowAndRain: the locked-biome columns and the protected structure sparkles.
bool weatherFrame(render::RenderContext& ctx, float partial) {
    if (!config()->weather) return false;
    auto* p = currentPlayer();
    if (!p) return false;
    std::vector<std::string>                                    enforcement;
    bool                                                        near = false;
    int                                                         cx = 0, cz = 0, ticks = 0, winding = 0;
    std::optional<std::vector<std::pair<core::BlockBox, bool>>> boxes;
    bool                                                        previewLocked = false;
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
    int const    range = ca::weatherRange(fancyGraphics());
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
    if (near && !enforcement.empty()) {
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
        logOnce(std::string("weather:") + ca::nameOf(effect), fmt::format("atmosphere: {} weather drawn ({} columns, range {})", ca::nameOf(effect), cols.size(), range));
    }
    return false; // TF renders the normal weather anyway
}

std::optional<float> cloudsAt() { return ca::kCloudHeight; }

/// The frame's view override in the Twilight Forest: getCloudColor at the fixed time with the level's
/// rain and thunder, and no sunrise colour (TwilightForestRenderInfo#getSunriseColor is null).
std::optional<vx::ViewOverride> viewOverride() {
    if (!config()->sky) return std::nullopt;
    auto* p = currentPlayer();
    if (!p) return std::nullopt;
    float rain = 0, thunder = 0;
    if (auto* w = p->getDimension().mWeather.get()) rain = w->mRainLevel, thunder = w->mLightningLevel;
    auto c = ca::cloudColor(ca::timeOfDay(ca::kFixedTime), rain, thunder);
    logOnce("clouds_colour", fmt::format("atmosphere: clouds {:.2f} {:.2f} {:.2f}, no sunrise colour", c.r, c.g, c.b));
    return vx::ViewOverride{.cloud = std::array<float, 3>{c.r, c.g, c.b}, .noSunrise = true};
}

void installProviderFor(int dim) {
    if (dim < 0) return;
    sky::setProvider(dim, sky::SkyProvider{.sky = skyFrame, .stars = starsFrame, .fog = fogFor, .clouds = cloudsAt, .weather = weatherFrame});
    vx::setViewOverride(dim, viewOverride);
    log().info("atmosphere: sky provider set for dimension {}", dim);
}

void clearProviderFor(int dim) {
    if (dim < 0) return;
    sky::clearProvider(dim);
    vx::clearViewOverride(dim);
    log().info("atmosphere: sky provider of dimension {} cleared", dim);
}

} // namespace

void installProvider() { installProviderFor(gDimension.load()); }

namespace {

void applyPreview(Json const& msg) {
    Preview p;
    if (msg.contains("biome") && msg["biome"].is_string()) p.biome = msg["biome"].get<std::string>();
    p.locked     = msg.value("locked", false);
    p.urGhast    = msg.value("urGhast", false);
    p.winding    = msg.value("winding", 0);
    p.auroraMode = static_cast<AuroraMode>(std::clamp(msg.value("auroraMode", 0), 0, 5));
    p.starsMode  = static_cast<StarsMode>(std::clamp(msg.value("starsMode", 0), 0, 3));
    p.vvFog      = msg.value("vvFog", 0.0f);
    p.voidPlate  = msg.value("voidPlate", false);
    p.bossFog    = msg.value("bossFog", false);
    if (msg.contains("protected") && msg["protected"].is_array()) {
        std::vector<std::pair<core::BlockBox, bool>> boxes;
        for (auto const& b : msg["protected"])
            if (b.is_array() && b.size() == 7) boxes.push_back({core::BlockBox{b[0].get<int>(), b[1].get<int>(), b[2].get<int>(), b[3].get<int>(), b[4].get<int>(), b[5].get<int>()}, b[6].get<bool>()});
        p.boxes = std::move(boxes);
    }
    {
        std::lock_guard lock(shared().mutex);
        shared().preview = p;
        shared().previewGen++;
    }
    forgetOnce({"fog:", "fog_vv", "weather:", "aurora_drawn", "aurora_in:", "locked_near", "ur_ghast_rain", "stars", "void_plate", "aurora_far"});
    log().info("atmosphere: preview {}", msg.dump());
}

class AtmosphereClientModule {
public:
    void install() {
        shared(); // before the worker (static destruction order)
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            try {
                clientTick();
            } catch (std::exception const& e) {
                if (onceFlag("tick_error")) log().error("atmosphere: client tick failed: {}", e.what());
            }
        });
        mExit = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            {
                auto&           s = shared();
                std::lock_guard lock(s.mutex);
                s.fog.reset(); // FogHandler#unloadFog
                s.fogStepped = {};
                s.fogPlanes.reset();
                s.preview = {};
                s.previewGen++;
                s.aurora = s.lastAurora = 0;
                s.grid.reset();
                s.uniformsValid = false;
                s.urGhastActive = false;
                s.urGhastRain   = 0;
                s.inTwilight    = false;
                s.nearLocked    = false;
                s.bossFog       = false;
                s.playerDim     = -1;
            }
            // the next world may number the Twilight Forest differently (a remote server)
            clearProviderFor(gDimension.exchange(-1));
            {
                std::lock_guard lock(unknownBiomes().mutex);
                unknownBiomes().unknown.clear();
                unknownBiomes().checked.clear();
            }
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
        // the world pass: the aurora anywhere, the sun and moon back outside the Twilight Forest
        render::onWorld([](render::RenderContext& ctx, float partial) { worldFrame(ctx, partial); });
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
        clearProviderFor(gDimension.exchange(-1));
    }

private:
    ll::event::ListenerPtr mTick, mExit;
};

} // namespace

// ---- selftest access (client selftests) -----------------------------------------------------------------------

AtmosphereState state() {
    auto&           s = shared();
    std::lock_guard lock(s.mutex);
    return AtmosphereState{s.inTwilight, s.aurora, s.lastAurora, s.urGhastRain, s.cameraBiome, s.nearLocked, gDimension.load(), static_cast<bool>(s.grid), s.ticks};
}

void setPreview(Json const& msg) { applyPreview(msg); }

std::optional<sky::FogSpec> fogForTest(std::string const& biome) { return fogFor(biome); }

std::vector<std::string> validAuroraBiomes() {
    auto cfg = config();
    std::vector<std::string> out;
    for (auto const& b : cfg->auroraBiomes)
        if (auroraBiome(*cfg, b)) out.push_back(b);
    return out;
}

} // namespace gf::client::atmosphere

GF_REGISTER_MODULE(gf::client::atmosphere::AtmosphereClientModule);

#endif // GF_CLIENT

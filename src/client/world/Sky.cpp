// "client.world" (F12). Client build only.
//
// Providers and hooks for the dimension's atmosphere (rule R8: units register here):
// - sky / stars: LevelRendererCamera::renderSky (a provider may replace the vanilla sky);
// - fog: LevelRendererPlayer::setupFog (distance fog of the camera's biome: colour, start, end);
// - clouds: Dimension::getCloudHeight of the client's dimension (nullopt: no clouds);
// - weather: drawn in the world pass; when a provider draws it, the vanilla rain / snow of the
//   client's dimension is held at 0 (LevelRendererPlayer::tickRain, no splashes or rain sounds);
// - music: MusicManager::getSituationalMusic (TF's setMusicInDimension);
// - particles: native factories for TF's 29 particle ids, run instead of the data-driven particle
//   when the client level spawns that effect (Level::addParticleEffect);
// - maps: the client's MapItemSavedData as a MapView, TF's decoration icons;
// - camera: LocalPlayer::getFieldOfViewModifier modifiers, camera shake (TF's shakeCamera through
//   the game's CameraShakeComponent, config "client".screenShakingEffect).
//
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFParticleType.java (the particle ids)
//   src/main/java/twilightforest/config/TFClientConfig.java (screenShakingEffect)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#ifdef GF_CLIENT

#include "client/world/Sky.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "glm/gtc/matrix_transform.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/MapRenderer.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/sound/Music.h"
#include "mc/client/sound/MusicManager.h"
#include "mc/client/world/level/fog/FogDistanceSetting.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/minecraft_camera/CameraShakeType.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderData.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderObject.h"
#include "mc/entity/components/CameraShakeComponent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/OverworldDimension.h"
#include "mc/world/level/saveddata/maps/MapDecoration.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"

#include "client/Client.h"
#include "client/world/ClientConfig.h"
#include "feature/ext/ui/Client.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client {

GF_REGISTER_CONFIG("client", ClientConfig);

namespace {

/// The local player's dimension (-1 outside a world).
int clientDimension() {
    auto* p = currentPlayer();
    return p ? static_cast<int>(p->getDimensionId()) : -1;
}

/// The biome at the local player ("" outside a world).
std::string clientBiome() {
    auto* p = currentPlayer();
    if (!p) return {};
    auto const& pos = p->getPosition();
    BlockPos    bp{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    auto&       region = p->getDimensionBlockSource();
    if (!region.hasChunksAt(bp, 0, false)) return {};
    return region.getBiome(bp).mHash->getString();
}

/// One log line per kind of activity per level (the e2e scenarios wait for them).
struct Once {
    std::mutex                       mutex;
    std::unordered_map<std::string, bool> done;
};
Once& once() {
    static Once o;
    return o;
}
void logOnce(std::string const& key, std::string const& line) {
    {
        std::lock_guard lock(once().mutex);
        if (once().done[key]) return;
        once().done[key] = true;
    }
    log().info("{}", line);
}

} // namespace

// ---- sky providers ---------------------------------------------------------------------------------------

namespace sky {
namespace {
/// Providers are immutable once set (shared, so a frame keeps the one it started with while
/// another thread replaces or removes it).
struct Providers {
    std::mutex                                        mutex;
    std::map<int, std::shared_ptr<SkyProvider const>> byDim;
};
Providers& providers() {
    static Providers p;
    return p;
}
std::atomic<bool> gReplaceWeather{false};

/// A new provider reports its hooks again (one log line per hook and provider).
void forgetProviderLines() {
    std::lock_guard lock(once().mutex);
    for (char const* k : {"sky", "fog", "clouds", "weather"}) once().done.erase(k);
}
} // namespace

void setProvider(int dimension, SkyProvider provider) {
    {
        std::lock_guard lock(providers().mutex);
        providers().byDim[dimension] = std::make_shared<SkyProvider const>(std::move(provider));
    }
    forgetProviderLines();
}

std::shared_ptr<SkyProvider const> providerOf(int dimension) {
    std::lock_guard lock(providers().mutex);
    auto            it = providers().byDim.find(dimension);
    return it == providers().byDim.end() ? nullptr : it->second;
}

void clearProvider(int dimension) {
    {
        std::lock_guard lock(providers().mutex);
        providers().byDim.erase(dimension);
    }
    gReplaceWeather = false;
    forgetProviderLines();
}

namespace {
/// The provider of the client's dimension (nullptr: vanilla).
std::shared_ptr<SkyProvider const> current() {
    int dim = clientDimension();
    if (dim < 0) return nullptr;
    return providerOf(dim);
}
} // namespace
} // namespace sky

// ---- music ---------------------------------------------------------------------------------------------------

namespace music {
namespace {
std::mutex& musicMutex() {
    static std::mutex m;
    return m;
}
MusicFn& provider() {
    static MusicFn fn;
    return fn;
}
} // namespace
void setProvider(MusicFn fn) {
    std::lock_guard lock(musicMutex());
    provider() = std::move(fn);
}
} // namespace music

// ---- particles ---------------------------------------------------------------------------------------------

namespace particles {
namespace {
/// TF's particle types (TFParticleType), in registration order; `alwaysShow` is the
/// SimpleParticleType(overrideLimiter) flag.
struct Declared {
    char const* id;
    bool        alwaysShow;
};
constexpr Declared kDeclared[] = {
    {"twilightforest:large_flame", false},
    {"twilightforest:leaf_rune", false},
    {"twilightforest:boss_tear", false},
    {"twilightforest:ghast_trap", false},
    {"twilightforest:protection", true},
    {"twilightforest:snow", false},
    {"twilightforest:snow_warning", false},
    {"twilightforest:extended_snow_warning", false},
    {"twilightforest:snow_guardian", false},
    {"twilightforest:ice_beam", false},
    {"twilightforest:annihilate", false},
    {"twilightforest:perfect_dodge", false},
    {"twilightforest:double_jump", true},
    {"twilightforest:huge_smoke", false},
    {"twilightforest:firefly", false},
    {"twilightforest:wandering_firefly", false},
    {"twilightforest:particle_spawner_firefly", false},
    {"twilightforest:fallen_leaf", false},
    {"twilightforest:dim_flame", false},
    {"twilightforest:ominous_flame", false},
    {"twilightforest:sorting_particle", false},
    {"twilightforest:transformation_particle", false},
    {"twilightforest:log_core_particle", false},
    {"twilightforest:cloud_puff", false},
    {"twilightforest:magic_effect", false},
    {"twilightforest:angry_lich", false},
    {"twilightforest:twilight_orb", false},
    {"twilightforest:shield_break", false},
    {"twilightforest:drying_rack", false},
};
static_assert(std::size(kDeclared) == 29, "TF 4.8 registers 29 particle types");

struct Factories {
    std::mutex                               mutex;
    std::unordered_map<std::string, Factory> byId;
    std::unordered_map<std::string, bool>    warned;
};
Factories& factories() {
    static Factories f;
    return f;
}
bool isDeclared(std::string const& id) {
    return std::any_of(std::begin(kDeclared), std::end(kDeclared), [&id](Declared const& d) { return id == d.id; });
}
} // namespace

void registerNative(std::string const& particleId, Factory factory) {
    if (!factory) return;
    auto&           f = factories();
    std::lock_guard lock(f.mutex);
    if (!isDeclared(particleId) && !particleId.starts_with("gf_test:")) {
        if (!f.warned[particleId]) log().warn("particles: {} is not a TF particle id", particleId);
        f.warned[particleId] = true;
    }
    f.byId[particleId] = std::move(factory);
}

bool spawn(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, nlohmann::json const& params) {
    Factory fn;
    {
        auto&           f = factories();
        std::lock_guard lock(f.mutex);
        auto            it = f.byId.find(particleId);
        if (it == f.byId.end()) return false;
        fn = it->second;
    }
    try {
        fn(pos, velocity, params);
    } catch (std::exception const& e) {
        log().error("particles: factory of {} failed: {}", particleId, e.what());
    }
    return true;
}

std::vector<std::pair<std::string, bool>> declared() {
    std::vector<std::pair<std::string, bool>> out;
    auto&                                     f = factories();
    std::lock_guard                           lock(f.mutex);
    for (auto const& d : kDeclared) out.emplace_back(d.id, f.byId.contains(d.id));
    return out;
}

namespace {
bool hasFactory(std::string const& id) {
    auto&           f = factories();
    std::lock_guard lock(f.mutex);
    return !f.byId.empty() && f.byId.contains(id);
}
} // namespace
} // namespace particles

// ---- maps ----------------------------------------------------------------------------------------------------

namespace maps {
namespace {
struct MapState {
    std::mutex                                           mutex;
    std::unordered_map<std::string, std::string>         icons;
    std::unordered_map<std::int64_t, std::vector<Decoration>> extra;
};
MapState& mapState() {
    static MapState s;
    return s;
}
} // namespace

std::optional<MapView> view(std::int64_t mapId) {
    auto* c     = currentClient();
    auto* level = c ? c->getLevel() : nullptr;
    if (!level) return std::nullopt;
    auto* data = level->getMapSavedData(ActorUniqueID{mapId});
    if (!data) return std::nullopt;
    MapView v;
    auto const& pixels = data->mPixels.get();
    v.size             = 128;
    v.pixels.resize(static_cast<std::size_t>(v.size * v.size), 0);
    for (std::size_t i = 0; i < v.pixels.size() && i < pixels.size(); ++i) {
        unsigned abgr = pixels[i]; // stored ABGR: back to ARGB
        v.pixels[i]   = (abgr & 0xFF00FF00u) | ((abgr & 0xFFu) << 16) | ((abgr >> 16) & 0xFFu);
    }
    for (auto const& [id, dec] : data->mDecorations.get()) {
        if (!dec) continue;
        Decoration d;
        d.type     = "minecraft:" + std::to_string(static_cast<int>(dec->mImage));
        d.x        = dec->mX;
        d.z        = dec->mY;
        d.rotation = dec->mRotation;
        d.name     = dec->mLabel.get();
        v.decorations.push_back(std::move(d));
    }
    {
        std::lock_guard lock(mapState().mutex);
        if (auto it = mapState().extra.find(mapId); it != mapState().extra.end()) v.decorations.insert(v.decorations.end(), it->second.begin(), it->second.end());
    }
    return v;
}

void setIcon(std::string const& decorationType, std::string const& texture) {
    std::lock_guard lock(mapState().mutex);
    mapState().icons[decorationType] = texture;
}

void setDecorations(std::int64_t mapId, std::vector<Decoration> decorations) {
    std::lock_guard lock(mapState().mutex);
    if (decorations.empty()) mapState().extra.erase(mapId);
    else mapState().extra[mapId] = std::move(decorations);
}

std::string iconOf(std::string const& decorationType) {
    std::lock_guard lock(mapState().mutex);
    auto            it = mapState().icons.find(decorationType);
    return it == mapState().icons.end() ? std::string{} : it->second;
}
} // namespace maps

// ---- camera --------------------------------------------------------------------------------------------------

namespace camera {
namespace {
struct CameraState {
    std::mutex         mutex;
    std::vector<FovFn> fov;
    float              intensity = 0;
    int                ticks     = 0;
    bool               queued    = false;
};
CameraState& cam() {
    static CameraState s;
    return s;
}
} // namespace

void addFovModifier(FovFn fn) {
    if (!fn) return;
    std::lock_guard lock(cam().mutex);
    cam().fov.push_back(std::move(fn));
}

float applyFovModifiers(float fovFactor) {
    std::vector<FovFn> fns;
    {
        std::lock_guard lock(cam().mutex);
        if (cam().fov.empty()) return fovFactor;
        fns = cam().fov;
    }
    for (auto& fn : fns) {
        try {
            fovFactor = fn(fovFactor);
        } catch (...) {}
    }
    return fovFactor;
}

void shake(float intensity, int ticks) {
    if (!config::get<ClientConfig>().screenShakingEffect || intensity <= 0 || ticks <= 0) return;
    std::lock_guard lock(cam().mutex);
    cam().intensity = std::max(cam().intensity, intensity);
    cam().ticks     = std::max(cam().ticks, ticks);
    cam().queued    = false;
}

int shaking() {
    std::lock_guard lock(cam().mutex);
    return cam().ticks;
}

namespace {
/// Client tick: hands a pending shake to the game's camera shake (rotational, like TF's jitter
/// of yaw / pitch / roll) and counts it down.
void tickShake() {
    float intensity = 0;
    int   ticks     = 0;
    bool  start     = false;
    {
        std::lock_guard lock(cam().mutex);
        if (cam().ticks <= 0) return;
        if (!cam().queued) {
            cam().queued = true;
            start        = true;
            intensity    = cam().intensity;
            ticks        = cam().ticks;
        }
        if (--cam().ticks <= 0) {
            cam().intensity = 0;
            cam().queued    = false;
        }
    }
    if (!start) return;
    auto* p = currentPlayer();
    if (!p) return;
    if (auto c = p->getEntityContext().tryGetComponent<CameraShakeComponent>()) {
        c->queueShakeEvent(CameraShakeType::Rotational, intensity, static_cast<float>(ticks) / 20.0f);
        logOnce("camera_shake", "camera: shake started");
    } else {
        logOnce("camera_shake_missing", "camera: the local player has no camera shake component");
    }
}
} // namespace
} // namespace camera

} // namespace gf::client

// ---- hooks (one per engine event, rule R8) ----------------------------------------------------------------------------

namespace world_hooks {
namespace sky  = gf::client::sky;
namespace cl   = gf::client;

LL_TYPE_INSTANCE_HOOK(
    SkyRenderHook,
    ll::memory::HookPriority::Normal,
    LevelRendererCamera,
    &LevelRendererCamera::renderSky,
    void,
    ScreenContext&                             screenContext,
    ViewRenderObject const&                    renderObj,
    BaseSceneDirectionalLightRenderData const& sceneDirectionalLightRenderData
) {
    std::shared_ptr<sky::SkyProvider const> p;
    try {
        p = sky::current();
    } catch (...) {}
    if (!p || (!p->sky && !p->stars)) {
        origin(screenContext, renderObj, sceneDirectionalLightRenderData);
        return;
    }
    gf::client::render::RenderContext ctx(screenContext);
    glm::vec3 cam    = renderObj.mViewData->mCameraPos;
    ctx.camera       = Vec3{cam.x, cam.y, cam.z};
    auto* c          = cl::currentClient();
    ctx.partialTicks = c ? c->getFrameAlpha() : 0.0f;
    bool replaced    = false;
    try {
        if (p->sky) replaced = p->sky(ctx, ctx.partialTicks);
    } catch (...) {}
    if (!replaced) origin(screenContext, renderObj, sceneDirectionalLightRenderData);
    try {
        if (p->stars) p->stars(ctx, ctx.partialTicks);
    } catch (...) {}
    cl::logOnce("sky", std::string("sky: provider drawn (") + (replaced ? "replaced" : "over") + " the vanilla sky)");
}

LL_TYPE_INSTANCE_HOOK(
    SkyFogHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$setupFog,
    void,
    ScreenContext& screenContext,
    float const    skyIntensityOverride
) {
    origin(screenContext, skyIntensityOverride);
    try {
        auto p = sky::current();
        if (!p || !p->fog) return;
        auto spec = p->fog(cl::clientBiome());
        if (!spec) return;
        auto& fog = mCurrentDistanceFog.get();
        fog.mColor = mce::Color{spec->color.r, spec->color.g, spec->color.b, 1.0f};
        fog.mStart = spec->start;
        fog.mEnd   = spec->end;
        fog.mType  = FogDistanceSetting::DistanceType::Fixed;
        cl::logOnce("fog", "sky: provider fog applied");
    } catch (...) {}
}

// The fog colour the frame is drawn with (the view data of the frame).
LL_TYPE_INSTANCE_HOOK(
    SkyViewFogHook,
    ll::memory::HookPriority::Normal,
    LevelRendererCamera,
    &LevelRendererCamera::$render,
    void,
    BaseActorRenderContext& baseEntityRenderContext,
    ViewRenderObject const& renderObj,
    IClientInstance&        ci
) {
    try {
        auto p = sky::current();
        if (p && p->fog) {
            if (auto spec = p->fog(cl::clientBiome())) {
                auto& view     = const_cast<ViewRenderObject&>(renderObj).mViewData.get();
                view.mFogColor = mce::Color{spec->color.r, spec->color.g, spec->color.b, 1.0f};
            }
        }
    } catch (...) {}
    origin(baseEntityRenderContext, renderObj, ci);
}

/// The client's cloud height from the provider of a dimension (nullopt: none).
std::optional<short> cloudHeight(Dimension const& dim) {
    if (!dim.mLevel.isClientSide()) return std::nullopt;
    auto p = sky::providerOf(static_cast<int>(dim.getDimensionId()));
    if (!p || !p->clouds) return std::nullopt;
    auto h = p->clouds();
    cl::logOnce("clouds", "sky: provider cloud height applied");
    return h ? static_cast<short>(std::clamp(*h, -2048.f, 30000.f)) : static_cast<short>(30000); // no clouds: far above the sky
}

LL_TYPE_INSTANCE_HOOK(SkyCloudHook, ll::memory::HookPriority::Normal, Dimension, &Dimension::$getCloudHeight, short) {
    try {
        if (auto h = cloudHeight(*this)) return *h;
    } catch (...) {}
    return origin();
}

LL_TYPE_INSTANCE_HOOK(SkyOverworldCloudHook, ll::memory::HookPriority::Normal, OverworldDimension, &OverworldDimension::$getCloudHeight, short) {
    try {
        if (auto h = cloudHeight(*this)) return *h;
    } catch (...) {}
    return origin();
}

// While a provider draws the weather: no vanilla rain / snow (visuals, splashes, sounds).
LL_TYPE_INSTANCE_HOOK(SkyRainHook, ll::memory::HookPriority::Normal, LevelRendererPlayer, &LevelRendererPlayer::$tickRain, void) {
    if (!sky::gReplaceWeather) {
        origin();
        return;
    }
    try {
        if (auto* p = cl::currentPlayer()) {
            auto& dim = p->getDimension();
            if (auto& w = dim.mWeather; w) {
                w->mRainLevel      = 0;
                w->mOldRainLevel   = 0;
                w->mLightningLevel = 0;
                w->mOldLightningLevel = 0;
            }
        }
    } catch (...) {}
}

LL_TYPE_INSTANCE_HOOK(
    MusicSelectHook,
    ll::memory::HookPriority::Normal,
    MusicManager,
    &MusicManager::getSituationalMusic,
    Bedrock::NonOwnerPointer<Music>,
    LocalPlayer const* player,
    bool               isCreditsScreen,
    bool               isLoadingProgressScreen
) {
    try {
        gf::client::music::MusicFn fn;
        {
            std::lock_guard lock(gf::client::music::musicMutex());
            fn = gf::client::music::provider();
        }
        if (fn && player && !isCreditsScreen && !isLoadingProgressScreen) {
            if (auto name = fn(cl::clientBiome(), static_cast<int>(player->getDimensionId()))) {
                auto m = _getMusic(*name);
                if (m.get()) {
                    cl::logOnce("music:" + *name, "music: provider chose " + *name);
                    return m;
                }
            }
        }
    } catch (...) {}
    return origin(player, isCreditsScreen, isLoadingProgressScreen);
}

LL_TYPE_INSTANCE_HOOK(
    ParticleEffectHook,
    ll::memory::HookPriority::Normal,
    Level,
    &Level::$addParticleEffect,
    void,
    HashedString const&      effect,
    Vec3 const&              emitterPosition,
    MolangVariableMap const& molangVariables
) {
    try {
        if (isClientSide() && gf::client::particles::hasFactory(effect.getString())) {
            gf::client::particles::spawn(effect.getString(), emitterPosition, Vec3{0, 0, 0}, nlohmann::json::object());
            return;
        }
    } catch (...) {}
    origin(effect, emitterPosition, molangVariables);
}

LL_TYPE_INSTANCE_HOOK(CameraFovHook, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::getFieldOfViewModifier, float) {
    float f = origin();
    try {
        return gf::client::camera::applyFovModifiers(f);
    } catch (...) {
        return f;
    }
}

} // namespace world_hooks

namespace gf::client {
namespace {

/// The weather providers draw in the world pass; they decide whether vanilla weather is held off.
void drawWeather(render::RenderContext& ctx, float partial) {
    auto p = sky::current();
    if (!p || !p->weather) {
        sky::gReplaceWeather = false;
        return;
    }
    bool replaced = false;
    try {
        replaced = p->weather(ctx, partial);
    } catch (...) {}
    sky::gReplaceWeather = replaced;
    if (replaced) logOnce("weather", "sky: provider weather drawn");
}

class WorldModule {
public:
    void load() {
        if (!mListeners.empty()) return;
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { camera::tickShake(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            std::lock_guard lock(once().mutex);
            once().done.clear();
            sky::gReplaceWeather = false;
        }));
    }
    void install() {
        load();
        if (!mWeatherRegistered) {
            mWeatherRegistered = true;
            render::onWorld(drawWeather);
        }
        world_hooks::SkyRenderHook::hook();
        world_hooks::SkyFogHook::hook();
        world_hooks::SkyViewFogHook::hook();
        world_hooks::SkyCloudHook::hook();
        world_hooks::SkyOverworldCloudHook::hook();
        world_hooks::SkyRainHook::hook();
        world_hooks::MusicSelectHook::hook();
        world_hooks::ParticleEffectHook::hook();
        world_hooks::CameraFovHook::hook();
    }
    void uninstall() {
        world_hooks::CameraFovHook::unhook();
        world_hooks::ParticleEffectHook::unhook();
        world_hooks::MusicSelectHook::unhook();
        world_hooks::SkyRainHook::unhook();
        world_hooks::SkyOverworldCloudHook::unhook();
        world_hooks::SkyCloudHook::unhook();
        world_hooks::SkyViewFogHook::unhook();
        world_hooks::SkyFogHook::unhook();
        world_hooks::SkyRenderHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    bool                                mWeatherRegistered = false;
};

} // namespace
} // namespace gf::client

GF_REGISTER_MODULE_ORDERED(gf::client::WorldModule, gf::ModuleOrder::Contracts);

#endif // GF_CLIENT

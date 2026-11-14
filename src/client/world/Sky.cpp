// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/GloamingParticleType.java
//   src/main/java/gloamingforest/config/GloamingClientConfig.java
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// "client.world" (F12). Client build only.
//
// Providers and hooks for the dimension's atmosphere (rule R8: units register here):
// - sky / stars: LevelRendererCamera::renderSky (a provider may replace the vanilla sky);
// - fog: LevelRendererPlayer::setupFog: the distance fog of the camera's biome (Classic) and the
//   volumetric air fog (Vibrant Visuals), plus the frame's fog colour (LevelRendererCamera::render);
// - clouds: the level renderer's cloud height each frame and Dimension::getCloudHeight of the client's
//   dimension; nullopt hides the clouds (ViewRenderData::mDrawClouds);
// - weather: drawn in the world pass; when a provider draws it, the frame skips the vanilla weather
//   pass (ViewRenderData::mDrawWeather) and LevelRendererPlayer::tickRain (splashes, rain sounds);
//   the rain and lightning levels are left alone, as upstream's DimensionSpecialEffects;
// - music: MusicManager::getSituationalMusic (upstream's setMusicInDimension);
// - particles: native factories for upstream's 29 particle ids (GloamingParticleType), run instead of the
//   data-driven particle when the client spawns that effect: gf::fx::particle's companion message
//   (F10's fx.particle handler asks spawnFromFx through gf::fx::client::setNativeSink), the server's
//   SpawnParticleEffectPacket (LegacyClientNetworkHandler::handle) and effects the client level
//   adds itself (Level::addParticleEffect), with the effect's molang variables;
// - maps: the client's MapItemSavedData as a MapView, upstream's decoration icons (drawn by Render.cpp);
// - camera: LocalPlayer::getFieldOfViewModifier modifiers, the screen shake of
//   ClientGameEvents (beanstalk rumbling: the local player's yaw / pitch jitter per tick, config
//   "client".screenShakingEffect = GloamingClientConfig.screenShakingEffect).

#ifdef GF_CLIENT

#include "client/world/Sky.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "glm/gtc/matrix_transform.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/network/LegacyClientNetworkHandler.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacketPayload.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/sound/Music.h"
#include "mc/client/sound/MusicManager.h"
#include "mc/client/world/level/fog/FogDistanceSetting.h"
#include "mc/client/world/level/fog/ResolvedFogVolumetricCoefficientSetting.h"
#include "mc/client/world/level/fog/ResolvedFogVolumetricDensitySetting.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderData.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderObject.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/util/MolangMemberArray.h"
#include "mc/util/MolangMemberVariable.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangVariable.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/OverworldDimension.h"
#include "mc/world/level/saveddata/maps/MapDecoration.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"

#include "client/Client.h"
#include "client/world/ClientConfig.h"
#include "feature/ext/combat/FxNative.h"
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
    std::mutex                            mutex;
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
    for (char const* k : {"sky", "fog", "fog_volumetric", "clouds", "clouds_hidden", "weather"}) once().done.erase(k);
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
/// upstream's particle types (GloamingParticleType), in registration order; `alwaysShow` is the
/// SimpleParticleType(overrideLimiter) flag.
struct Declared {
    char const* id;
    bool        alwaysShow;
};
constexpr Declared kDeclared[] = {
    {"gloamingforest:large_flame", false},
    {"gloamingforest:leaf_rune", false},
    {"gloamingforest:boss_tear", false},
    {"gloamingforest:ghast_trap", false},
    {"gloamingforest:protection", true},
    {"gloamingforest:snow", false},
    {"gloamingforest:snow_warning", false},
    {"gloamingforest:extended_snow_warning", false},
    {"gloamingforest:snow_guardian", false},
    {"gloamingforest:ice_beam", false},
    {"gloamingforest:annihilate", false},
    {"gloamingforest:perfect_dodge", false},
    {"gloamingforest:double_jump", true},
    {"gloamingforest:huge_smoke", false},
    {"gloamingforest:firefly", false},
    {"gloamingforest:wandering_firefly", false},
    {"gloamingforest:particle_spawner_firefly", false},
    {"gloamingforest:fallen_leaf", false},
    {"gloamingforest:dim_flame", false},
    {"gloamingforest:ominous_flame", false},
    {"gloamingforest:sorting_particle", false},
    {"gloamingforest:transformation_particle", false},
    {"gloamingforest:log_core_particle", false},
    {"gloamingforest:cloud_puff", false},
    {"gloamingforest:magic_effect", false},
    {"gloamingforest:angry_lich", false},
    {"gloamingforest:gloaming_orb", false},
    {"gloamingforest:shield_break", false},
    {"gloamingforest:drying_rack", false},
};
static_assert(std::size(kDeclared) == 29, "upstream 4.8 registers 29 particle types");

/// The factories as an immutable snapshot: the particle hook (every particle the game spawns)
/// reads it without a lock.
using FactoryMap = std::unordered_map<std::string, Factory>;
struct Factories {
    std::mutex                                     write;
    std::atomic<std::shared_ptr<FactoryMap const>> map{std::make_shared<FactoryMap const>()};
    std::atomic<bool>                              any{false};
    std::unordered_map<std::string, bool>          warned; ///< under `write`
};
Factories& factories() {
    static Factories f;
    return f;
}
bool isDeclared(std::string const& id) {
    return std::any_of(std::begin(kDeclared), std::end(kDeclared), [&id](Declared const& d) { return id == d.id; });
}
template <class F>
void edit(F&& change) {
    auto&           f = factories();
    std::lock_guard lock(f.write);
    auto            next = std::make_shared<FactoryMap>(*f.map.load());
    change(*next);
    f.any = !next->empty();
    f.map.store(std::move(next));
}
} // namespace

void registerNative(std::string const& particleId, Factory factory) {
    if (!factory) return;
    if (!isDeclared(particleId) && !particleId.starts_with("gf_test:")) {
        auto&           f = factories();
        std::lock_guard lock(f.write);
        if (!f.warned[particleId]) log().warn("particles: {} is not an upstream particle id", particleId);
        f.warned[particleId] = true;
    }
    edit([&](FactoryMap& m) { m[particleId] = std::move(factory); });
}

bool unregisterNative(std::string const& particleId) {
    bool had = false;
    edit([&](FactoryMap& m) { had = m.erase(particleId) > 0; });
    return had;
}

bool spawn(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, nlohmann::json const& params) {
    if (!factories().any.load(std::memory_order_relaxed)) return false;
    auto map = factories().map.load();
    auto it  = map->find(particleId);
    if (it == map->end()) return false;
    try {
        it->second(pos, velocity, params);
    } catch (std::exception const& e) {
        log().error("particles: factory of {} failed: {}", particleId, e.what());
    }
    return true;
}

bool spawnFromFx(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, int count, nlohmann::json const& params) {
    if (!factories().any.load(std::memory_order_relaxed)) return false;
    auto map = factories().map.load();
    auto it  = map->find(particleId);
    if (it == map->end()) return false;
    // as gf::fx for vanilla clients: params "spread" offsets each particle by a gaussian times the
    // distance per axis (ServerLevel#sendParticles)
    int                  n = std::clamp(count, 1, 256);
    std::optional<Vec3>  spread;
    if (params.is_object()) {
        if (auto s = params.find("spread"); s != params.end() && s->is_array() && s->size() == 3 && (*s)[0].is_number() && (*s)[1].is_number() && (*s)[2].is_number())
            spread = Vec3{(*s)[0].get<float>(), (*s)[1].get<float>(), (*s)[2].get<float>()};
    }
    thread_local std::mt19937       rng{std::random_device{}()};
    std::normal_distribution<float> gauss(0.f, 1.f);
    for (int i = 0; i < n; ++i) {
        Vec3 at = pos;
        if (spread && n > 1) at = Vec3{pos.x + gauss(rng) * spread->x, pos.y + gauss(rng) * spread->y, pos.z + gauss(rng) * spread->z};
        try {
            it->second(at, velocity, params);
        } catch (std::exception const& e) {
            log().error("particles: factory of {} failed: {}", particleId, e.what());
            break;
        }
    }
    // one line per particle, count and velocity per level (the e2e scenarios wait for a given call)
    auto line = fmt::format("particles: native factory of {} ran (fx.particle, x{} vel {:.3f} {:.3f} {:.3f})", particleId, n, velocity.x, velocity.y, velocity.z);
    logOnce("particle_fx:" + line, line);
    return true;
}

std::vector<std::pair<std::string, bool>> declared() {
    std::vector<std::pair<std::string, bool>> out;
    auto                                      map = factories().map.load();
    for (auto const& d : kDeclared) out.emplace_back(d.id, map->contains(d.id));
    return out;
}

namespace {
/// A molang value as JSON: numbers, structs as objects of their members (names without '.').
nlohmann::json molangValue(MolangScriptArg const& arg) {
    if (arg.mType == MolangScriptArgType::Float) return arg.mPOD.mFloat;
    if (arg.mType != MolangScriptArgType::Variant) return nullptr;
    auto const* members = std::get_if<MolangMemberArray>(&arg.mData);
    if (!members) return nullptr;
    nlohmann::json out = nlohmann::json::object();
    for (auto const& m : *members->mMembers) {
        std::string name = m.mName->getString();
        if (name.starts_with('.')) name.erase(0, 1);
        out[name] = molangValue(*m.mData);
    }
    return out;
}

/// The params and velocity a native factory gets for an engine-spawned effect (see ext/ui/Client.h).
std::pair<nlohmann::json, Vec3> fromMolang(MolangVariableMap const& vars) {
    nlohmann::json params = nlohmann::json::object();
    auto const&    offsets = *vars.mMapFromVariableIndexToVariableArrayOffset;
    auto const&    values  = *vars.mVariables;
    auto const&    names   = MolangVariable::mIndexToVariableNameMap();
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        short off = offsets[i];
        if (off < 0 || static_cast<std::size_t>(off) >= values.size() || !values[static_cast<std::size_t>(off)]) continue;
        auto name = names.find(static_cast<MolangVariableIndex>(i));
        if (name == names.end()) continue;
        std::string key = name->second.getString();
        if (key.starts_with("variable.")) key.erase(0, 9);
        else if (key.starts_with("v.")) key.erase(0, 2);
        auto value = molangValue(*values[static_cast<std::size_t>(off)]->mValue);
        if (!value.is_null()) params[key] = std::move(value);
    }
    auto vec = [&params](char const* key) -> std::optional<Vec3> {
        auto it = params.find(key);
        if (it == params.end() || !it->is_object()) return std::nullopt;
        return Vec3{it->value("x", 0.f), it->value("y", 0.f), it->value("z", 0.f)};
    };
    Vec3 velocity{};
    if (auto v = vec("velocity")) velocity = *v;
    else if (auto d = vec("direction")) {
        float speed = params.contains("speed") && params["speed"].is_number() ? params["speed"].get<float>() : 1.f;
        velocity    = Vec3{d->x * speed, d->y * speed, d->z * speed};
    }
    return {std::move(params), velocity};
}
} // namespace
} // namespace particles

// ---- maps ----------------------------------------------------------------------------------------------------

namespace maps {
namespace {
struct MapState {
    std::mutex                                                          mutex;
    std::unordered_map<std::string, std::string>                        icons;
    std::unordered_map<std::int64_t, std::shared_ptr<DecoratedMap const>> maps; ///< immutable snapshots
    std::atomic<std::size_t>                                            mapCount{0}; ///< maps.size(), read without the lock
};

/// The vanilla map icon atlas (textures/map/map_icons: 4 x 4 cells of 16 px; MapDecoration::Type
/// 0..15) and the Java names of its icons.
constexpr char kVanillaIcons[] = "textures/map/map_icons";
int vanillaCell(std::string const& type) {
    static std::unordered_map<std::string, int> const names{
        {"minecraft:player", 0},          {"minecraft:frame", 1},      {"minecraft:red_marker", 2},
        {"minecraft:blue_marker", 3},     {"minecraft:red_x", 4},      {"minecraft:target_x", 4},
        {"minecraft:target_point", 5},    {"minecraft:player_off_map", 6}, {"minecraft:player_off_limits", 13},
        {"minecraft:mansion", 14},        {"minecraft:monument", 15},
    };
    if (auto it = names.find(type); it != names.end()) return it->second;
    constexpr std::string_view prefix = "minecraft:";
    if (!type.starts_with(prefix)) return -1;
    auto rest = std::string_view(type).substr(prefix.size());
    if (rest.empty() || rest.size() > 2 || rest.find_first_not_of("0123456789") != std::string_view::npos) return -1;
    int n = std::stoi(std::string(rest));
    return n <= 15 ? n : -1;
}
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
    MapView     v;
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
    auto extra = decorationsOf(mapId);
    v.decorations.insert(v.decorations.end(), extra.begin(), extra.end());
    return v;
}

void setIcon(std::string const& decorationType, std::string const& texture) {
    std::lock_guard lock(mapState().mutex);
    mapState().icons[decorationType] = texture;
}

bool removeIcon(std::string const& decorationType) {
    std::lock_guard lock(mapState().mutex);
    return mapState().icons.erase(decorationType) > 0;
}

void setDecorations(std::int64_t mapId, std::vector<Decoration> decorations) { setDecorations(mapId, std::move(decorations), {}); }

void setDecorations(std::int64_t mapId, std::vector<Decoration> decorations, std::vector<DecorationStyle> styles) {
    std::shared_ptr<DecoratedMap const> next;
    if (!decorations.empty()) {
        styles.resize(decorations.size());
        next = std::make_shared<DecoratedMap const>(DecoratedMap{std::move(decorations), std::move(styles)});
    }
    std::shared_ptr<DecoratedMap const> old; // released outside the lock
    std::lock_guard                     lock(mapState().mutex);
    auto&                               m = mapState().maps;
    if (auto it = m.find(mapId); it != m.end()) {
        old = std::move(it->second);
        if (next) it->second = std::move(next);
        else m.erase(it);
    } else if (next) m.emplace(mapId, std::move(next));
    mapState().mapCount.store(m.size(), std::memory_order_relaxed);
}

std::shared_ptr<DecoratedMap const> decorated(std::int64_t mapId) {
    if (mapState().mapCount.load(std::memory_order_relaxed) == 0) return nullptr;
    std::lock_guard lock(mapState().mutex);
    auto            it = mapState().maps.find(mapId);
    return it == mapState().maps.end() ? nullptr : it->second;
}

bool hasDecorations(std::int64_t mapId) {
    if (mapState().mapCount.load(std::memory_order_relaxed) == 0) return false;
    std::lock_guard lock(mapState().mutex);
    return mapState().maps.contains(mapId);
}

bool showOnItemFrame(std::string const& decorationType) {
    // Java's MapDecorationTypes registered with showOnItemFrame false (the player markers)
    static std::unordered_map<std::string, bool> const hidden{
        {"minecraft:player", true},          {"minecraft:red_marker", true},        {"minecraft:blue_marker", true},
        {"minecraft:player_off_map", true},  {"minecraft:player_off_limits", true}, {"minecraft:0", true},
        {"minecraft:2", true},               {"minecraft:3", true},                 {"minecraft:6", true},
        {"minecraft:13", true},
    };
    return !hidden.contains(decorationType);
}

std::vector<DecorationStyle> stylesOf(std::int64_t mapId) {
    auto d = decorated(mapId);
    return d ? d->styles : std::vector<DecorationStyle>{};
}

std::optional<Icon> iconFor(std::string const& decorationType) {
    {
        std::lock_guard lock(mapState().mutex);
        if (auto it = mapState().icons.find(decorationType); it != mapState().icons.end()) return Icon{.texture = it->second};
    }
    int cell = vanillaCell(decorationType);
    if (cell < 0) return std::nullopt;
    float u = static_cast<float>(cell % 4) / 4.f, v = static_cast<float>(cell / 4) / 4.f;
    return Icon{.texture = kVanillaIcons, .u0 = u, .v0 = v, .u1 = u + .25f, .v1 = v + .25f};
}

std::vector<Decoration> decorationsOf(std::int64_t mapId) {
    auto d = decorated(mapId);
    return d ? d->decorations : std::vector<Decoration>{};
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
    cam().intensity = intensity; // the latest call decides the strength
    cam().ticks     = std::max(cam().ticks, ticks);
}

int shaking() {
    std::lock_guard lock(cam().mutex);
    return cam().ticks;
}

float shakeIntensity() {
    std::lock_guard lock(cam().mutex);
    return cam().ticks > 0 ? cam().intensity : 0.f;
}

namespace {
/// Client tick: ClientGameEvents' rumble on the local player's rotation (Entity.moveTo: the old
/// rotation is set too, so the view jumps instead of easing).
void tickShake() {
    float intensity = 0;
    {
        std::lock_guard lock(cam().mutex);
        if (cam().ticks <= 0) return;
        intensity = cam().intensity;
        if (--cam().ticks <= 0) cam().intensity = 0;
    }
    if (!config::get<ClientConfig>().screenShakingEffect) return;
    auto* p = currentPlayer();
    if (!p) return;
    thread_local std::mt19937                    rng{std::random_device{}()};
    std::uniform_real_distribution<float>        r(0.f, 1.f);
    Vec2 const                                   rot   = p->getRotation(); // x pitch, y yaw
    float const                                  yaw   = rot.y + (r(rng) - 0.5f) * intensity;
    float const                                  pitch = std::clamp(rot.x + (r(rng) * 2.5f - 1.25f) * intensity, -90.f, 90.f);
    p->setRotationWrapped(Vec2{pitch, yaw});
    auto& rc    = *p->mBuiltInComponents->mActorRotationComponent;
    rc.mRotPrev = rc.mRot;
    logOnce("camera_shake", fmt::format("camera: shake applied (intensity {:.2f})", intensity));
}
} // namespace
} // namespace camera

} // namespace gf::client

// ---- hooks (one per engine event, rule R8) ----------------------------------------------------------------------------

namespace world_hooks {
namespace sky = gf::client::sky;
namespace cl  = gf::client;

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
    glm::vec3                         cam = renderObj.mViewData->mCameraPos;
    ctx.camera                            = Vec3{cam.x, cam.y, cam.z};
    auto* c                               = cl::currentClient();
    ctx.partialTicks                      = c ? c->getFrameAlpha() : 0.0f;
    bool replaced                         = false;
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
        // Classic: the distance fog
        auto& fog  = mCurrentDistanceFog.get();
        fog.mColor = mce::Color{spec->color.r, spec->color.g, spec->color.b, 1.0f};
        fog.mStart = spec->start;
        fog.mEnd   = spec->end;
        fog.mType  = FogDistanceSetting::DistanceType::Fixed;
        cl::logOnce("fog", "sky: provider fog applied");
        // Vibrant Visuals: a uniform air fog that lets about 5 % of the light through at `end`
        // (extinction 3 / end per block), scattering the fog colour and absorbing the rest
        float const extinction = 3.0f / std::max(spec->end, 1.0f);
        auto&       density    = mCurrentFogDensity.get();
        density.mMaxDensity        = 1.0f;
        density.mMaxDensityHeight  = 1000.0f;
        density.mZeroDensityHeight = 1001.0f;
        auto& air                  = mAirFogCoefficient.get();
        auto  channel              = [extinction](float c) { return std::clamp(c, 0.0f, 1.0f) * extinction; };
        air.mScattering->r         = channel(spec->color.r);
        air.mScattering->g         = channel(spec->color.g);
        air.mScattering->b         = channel(spec->color.b);
        air.mAbsorption->r         = extinction - channel(spec->color.r);
        air.mAbsorption->g         = extinction - channel(spec->color.g);
        air.mAbsorption->b         = extinction - channel(spec->color.b);
        cl::logOnce("fog_volumetric", fmt::format("sky: provider volumetric fog applied (extinction {:.3f})", extinction));
    } catch (...) {}
}

/// Cloud heights of the level renderers while a provider overrides them (cleared with the level).
struct SavedClouds {
    std::mutex                                           mutex;
    std::unordered_map<LevelRendererCamera const*, short> heights;
};
SavedClouds& savedClouds() {
    static SavedClouds s;
    return s;
}

// The frame's fog colour, the clouds and the weather pass (the view data of the frame).
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
        auto& view = const_cast<ViewRenderObject&>(renderObj).mViewData.get();
        if (sky::gReplaceWeather) view.mDrawWeather = false;
        // the renderer's own cloud height while a provider sets it, put back after
        std::lock_guard lock(savedClouds().mutex);
        auto&           savedCloud = savedClouds().heights;
        bool            ownClouds  = false;
        if (auto p = sky::current()) {
            if (p->fog) {
                if (auto spec = p->fog(cl::clientBiome())) view.mFogColor = mce::Color{spec->color.r, spec->color.g, spec->color.b, 1.0f};
            }
            if (p->clouds) {
                ownClouds = true;
                savedCloud.try_emplace(this, mCloudHeight);
                if (auto h = p->clouds()) {
                    mCloudHeight = static_cast<short>(std::clamp(*h, -2048.f, 30000.f));
                    cl::logOnce("clouds", "sky: provider cloud height applied");
                } else {
                    view.mDrawClouds = false;
                    cl::logOnce("clouds_hidden", "sky: provider clouds hidden");
                }
            }
        }
        if (!ownClouds) {
            if (auto it = savedCloud.find(this); it != savedCloud.end()) {
                mCloudHeight = it->second;
                savedCloud.erase(it);
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

// While a provider draws the weather: no vanilla rain ticking (splashes, rain sounds). The rain and
// lightning levels stay: the sky still darkens and lightning still flashes.
LL_TYPE_INSTANCE_HOOK(SkyRainHook, ll::memory::HookPriority::Normal, LevelRendererPlayer, &LevelRendererPlayer::$tickRain, void) {
    if (sky::gReplaceWeather) return;
    origin();
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
    // every particle the game spawns comes through here: one relaxed load while no factory exists
    if (gf::client::particles::factories().any.load(std::memory_order_relaxed) && isClientSide()) {
        try {
            auto map = gf::client::particles::factories().map.load();
            if (map->contains(effect.getString())) {
                auto [params, velocity] = gf::client::particles::fromMolang(molangVariables);
                gf::client::particles::spawn(effect.getString(), emitterPosition, velocity, params);
                cl::logOnce("particle:" + effect.getString(), "particles: native factory of " + effect.getString() + " ran (engine)");
                return;
            }
        } catch (...) {}
    }
    origin(effect, emitterPosition, molangVariables);
}

// The server's particle packets (/particle, F10's fx for vanilla clients) reach the client level
// without Level::addParticleEffect (spike S5, round 3): the client handler of
// SpawnParticleEffectPacket runs the native factory instead of the data-driven effect, as the
// vanilla handler would spawn it: only in the local player's dimension, and for an effect bound to
// an actor at the actor's position plus the packet's (an offset then).
LL_TYPE_INSTANCE_HOOK(
    ParticlePacketHook,
    ll::memory::HookPriority::Normal,
    LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle,
    void,
    NetworkIdentifier const&         source,
    SpawnParticleEffectPacket const& packet
) {
    if (gf::client::particles::factories().any.load(std::memory_order_relaxed)) {
        try {
            std::string const& id  = packet.mEffectName;
            auto               map = gf::client::particles::factories().map.load();
            auto*              player = cl::currentPlayer();
            if (map->contains(id) && player) {
                int dim = static_cast<int>(packet.mVanillaDimensionId);
                if (dim != static_cast<int>(player->getDimensionId())) {
                    cl::logOnce("particle_packet_dim:" + id, "particles: " + id + " for dimension " + std::to_string(dim) + " skipped (not the player's)");
                    return; // the vanilla handler drops it too
                }
                ActorUniqueID const actorId = packet.mActorId;
                Vec3                pos     = packet.mPos;
                bool const          bound   = actorId != ActorUniqueID::INVALID_ID() && actorId.rawID != 0;
                Actor*              actor   = bound ? player->getLevel().fetchEntity(actorId, false) : nullptr;
                if (!bound || actor) {
                    std::optional<MolangVariableMap> const& vars = packet.mMolangVariables;
                    auto [params, velocity] = vars ? gf::client::particles::fromMolang(*vars) : std::pair<nlohmann::json, Vec3>{nlohmann::json::object(), Vec3{}};
                    if (actor) {
                        pos = actor->getPosition() + packet.mPos;
                        params["actor"] = actorId.rawID;
                    }
                    gf::client::particles::spawn(id, pos, velocity, params);
                    cl::logOnce(
                        std::string("particle_packet") + (actor ? "_actor:" : ":") + id,
                        "particles: native factory of " + id + " ran (engine, SpawnParticleEffectPacket" + (actor ? ", bound to an actor" : "") + ")"
                    );
                    return;
                }
                // an actor this client does not know: what the vanilla handler does with it
            }
        } catch (...) {}
    }
    origin(source, packet);
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
            {
                std::lock_guard lock(once().mutex);
                once().done.clear();
            }
            sky::gReplaceWeather = false;
            {
                std::lock_guard lock(world_hooks::savedClouds().mutex);
                world_hooks::savedClouds().heights.clear();
            }
            std::lock_guard lock(camera::cam().mutex);
            camera::cam().ticks     = 0;
            camera::cam().intensity = 0;
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
        world_hooks::ParticlePacketHook::hook();
        world_hooks::CameraFovHook::hook();
        // F10's fx.particle handler (gf::fx::particle for companions) asks the native factories first
        gf::fx::client::setNativeSink(&particles::spawnFromFx);
    }
    void uninstall() {
        gf::fx::client::setNativeSink({});
        world_hooks::CameraFovHook::unhook();
        world_hooks::ParticlePacketHook::unhook();
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

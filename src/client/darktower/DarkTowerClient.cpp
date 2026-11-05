// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/UrGhast.java
//   src/main/java/twilightforest/client/particle/GhastTearParticle.java
//   src/main/java/twilightforest/client/renderer/TFWeatherRenderer.java
//   src/main/java/twilightforest/block/entity/ReactorDebrisBlockEntity.java
//   src/main/java/twilightforest/client/renderer/block/ReactorDebrisRenderer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U26 (darktower), client mod only: what TF draws on the client.
// - The Ur-Ghast's client side of UrGhast#aiStep / #tick, from the "darktower.urghast" heartbeat
//   (uid, tantrum; every 10 ticks for each Ur-Ghast within 160 blocks): redstone dust half the time,
//   and while it is in a tantrum and not dying one boss tear a tick and TFWeatherRenderer's
//   urGhastAlive (the rain fades in by 0.1 a tick up to 1 and out by 0.02: the client dimension's rain
//   level is raised to at least that, which draws the game's own rain).
// - GhastTearParticle: the tear is the resource pack effect twilightforest:boss_tear (its look and
//   the shatter burst); its physics are followed here as Particle#tick does (gravity 0.6, friction
//   0.98, lifetime 60 + rand(40)) so that on landing it plays entity.twilightforest.tear.break half
//   the time (vol 0.5, pitch 1.65, one block above), which a particle effect cannot.
// - ReactorDebrisRenderer: a reactor debris block (blank in the pack) is drawn as the block entity's
//   box: a near-full netherrack cube until its 5th tick, then a random sub-box with a random texture
//   per face (netherrack, bedrock, nether portal, obsidian), as ReactorDebrisBlockEntity#tick rolls
//   it on the client (willDisappear, no rerolls). Each face is a one-face block geometry scaled to the
//   box (Bedrock geometry UVs are fixed: the texture is scaled over the face, not cropped to it).

#ifdef GF_CLIENT

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/comprehensive/ParticleType.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "client/Client.h"
#include "client/render/Render.h"
#include "feature/dimension/World.h"
#include "feature/ext/entity/EntityClientExt.h"
#include "feature/ext/ui/Client.h"
#include "feature/fx/FxInternal.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::darktower {

namespace {

using Json = nlohmann::json;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }
bool   nextBoolean() { return (rng()() & 1u) != 0; }
int    nextInt(int bound) { return bound <= 0 ? 0 : static_cast<int>(rng()() % static_cast<unsigned>(bound)); }

// ---- the Ur-Ghast ---------------------------------------------------------------------------------------

struct Ghast {
    std::uint64_t lastSeen = 0;
    bool          tantrum  = false;
};

/// GhastTearParticle's physics (TextureSheetParticle with hasPhysics, gravity 0.6).
struct Tear {
    Vec3   pos;
    double yd       = 0.0;
    int    age      = 0;
    int    lifetime = 60;
    bool   onGround = false;
};

struct State {
    std::unordered_map<std::int64_t, Ghast> ghasts;
    std::vector<Tear>                       tears;
    std::uint64_t                           tick = 0;
    float                                   urGhastRain = 0.0f; ///< TFWeatherRenderer.urGhastRain
    bool                                    controlling = false;
    float                                   vanillaRain = 0.0f; ///< the dimension's own level, followed while raised
    Weather const*                          raised      = nullptr; ///< the weather whose level is raised
    bool                                    loggedRain  = false;
    bool                                    loggedTears = false;
    ll::event::ListenerPtr                  listener;
};
State& st() {
    static State s;
    return s;
}

/// The empty molang map the tear effect is added with (never destroyed: the game's destructor must
/// not run at process exit).
MolangVariableMap const& emptyMolang() {
    static auto* m = new gf::fx::MolangMap();
    return m->get();
}

bool collides(BlockSource& region, BlockPos const& pos, float& top) {
    Block const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    std::vector<AABB> boxes;
    b.getBlockType().addCollisionShapes(b, region, pos, nullptr, boxes, {});
    if (boxes.empty()) return false;
    top = boxes.front().max.y;
    for (auto const& box : boxes) top = std::max(top, box.max.y);
    return true;
}

void playTearBreak(Vec3 const& at) {
    auto* client = currentClient();
    if (!client) return;
    client->getSoundEngine()->play("entity.twilightforest.tear.break", at, 0.5f, 1.65f, std::nullopt);
}

/// Particle#tick of the tears (GhastTearParticle#tick first: landed -> 50 % the break sound, gone).
void tickTears(BlockSource& region) {
    auto& tears = st().tears;
    for (auto it = tears.begin(); it != tears.end();) {
        auto& t = *it;
        if (t.onGround) {
            if (nextBoolean()) playTearBreak(Vec3{t.pos.x, t.pos.y + 1.0f, t.pos.z});
            it = tears.erase(it);
            continue;
        }
        if (t.age++ >= t.lifetime) {
            it = tears.erase(it);
            continue;
        }
        t.yd -= 0.04 * 0.6;
        float    ny = static_cast<float>(t.pos.y + t.yd);
        BlockPos below{static_cast<int>(std::floor(t.pos.x)), static_cast<int>(std::floor(ny)), static_cast<int>(std::floor(t.pos.z))};
        float    top = 0.0f;
        if (t.yd < 0.0 && collides(region, below, top) && ny < below.y + top) {
            t.pos.y    = below.y + top;
            t.yd       = 0.0;
            t.onGround = true;
        } else {
            t.pos.y = ny;
        }
        t.yd *= 0.98;
        ++it;
    }
}

/// The dimension's weather (null: none). Dimension::mWeather is a std::unique_ptr, kept in a TypedStorage.
Weather* weatherOf(Dimension& d) {
    return d.mWeather.get();
}

/// TFWeatherRenderer#tickRain's urGhastRain, applied to the client dimension's rain level.
void tickRain(Dimension& dim, bool alive) {
    auto& s = st();
    s.urGhastRain = alive ? std::min(1.0f, s.urGhastRain + 0.1f) : std::max(0.0f, s.urGhastRain - 0.02f);
    Weather* w = weatherOf(dim);
    if (s.controlling && s.raised != w) s.controlling = false; // another dimension now: nothing to restore here
    if (!w) return;
    float& level  = w->mRainLevel;
    float& old    = w->mOldRainLevel;
    float  target = w->mTargetRainLevel;
    if (s.urGhastRain > 0.0f && gf::world::isTwilight(dim)) {
        if (!s.controlling) {
            s.controlling = true;
            s.raised      = w;
            s.vanillaRain = level;
        } else {
            // Weather::tick moves the level towards its target by 0.01 a tick
            s.vanillaRain = s.vanillaRain < target ? std::min(target, s.vanillaRain + 0.01f) : std::max(target, s.vanillaRain - 0.01f);
        }
        float v = std::max(s.vanillaRain, s.urGhastRain);
        old     = level;
        level   = v;
        if (!s.loggedRain) {
            s.loggedRain = true;
            log().info("darktower: client Ur-Ghast tantrum rain");
        }
    } else if (s.controlling) {
        s.controlling = false;
        old = level = s.vanillaRain;
    }
}

void onTick() {
    auto& s = st();
    ++s.tick;
    auto* player = currentPlayer();
    if (!player) {
        s.tears.clear();
        s.ghasts.clear();
        s.urGhastRain = 0.0f;
        s.controlling = false;
        return;
    }
    auto& level  = player->getLevel();
    auto& region = player->getDimensionBlockSource();
    bool  alive  = false; // TFWeatherRenderer.urGhastAlive
    for (auto it = s.ghasts.begin(); it != s.ghasts.end();) {
        if (s.tick - it->second.lastSeen > 40) {
            it = s.ghasts.erase(it);
            continue;
        }
        ActorUniqueID id;
        id.rawID    = it->first;
        Actor* ghast = level.fetchEntity(id, false);
        if (!ghast) {
            ++it;
            continue;
        }
        auto  p = ghast->getFeetPos();
        auto  b = ghast->getAABB();
        float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
        // UrGhast#aiStep (client): redstone dust half the time
        if (nextBoolean())
            level.addParticle(
                ParticleType::RedDust,
                Vec3{float(p.x + (nextDouble() - 0.5) * w), float(p.y + nextDouble() * h - 0.25), float(p.z + (nextDouble() - 0.5) * w)},
                Vec3{0.0f, 0.0f, 0.0f},
                0,
                nullptr,
                false
            );
        bool dying = !ghast->isAlive() || gf::client::entity::deathTime(it->first) > 0;
        if (it->second.tantrum && !dying) {
            alive = true;
            Vec3 at{float(p.x + (nextDouble() - 0.5) * w * 0.75), float(p.y + nextDouble() * h * 0.5), float(p.z + (nextDouble() - 0.5) * w * 0.75)};
            level.addParticleEffect(HashedString("twilightforest:boss_tear"), at, emptyMolang());
            s.tears.push_back(Tear{at, 0.0, 0, 60 + nextInt(40), false});
            if (!s.loggedTears) {
                s.loggedTears = true;
                log().info("darktower: client boss tears");
            }
        }
        ++it;
    }
    tickTears(region);
    tickRain(player->getDimension(), alive);
}

void onUrGhast(Json const& msg) {
    if (!msg.is_object() || !msg.contains("uid") || !msg["uid"].is_number_integer()) return;
    auto& g    = st().ghasts[msg["uid"].get<std::int64_t>()];
    g.lastSeen = st().tick;
    g.tantrum  = msg.value("on", false);
}

// ---- reactor debris -------------------------------------------------------------------------------------

constexpr char const* kTextures[4] = {"textures/blocks/netherrack", "textures/blocks/bedrock", "textures/blocks/portal", "textures/blocks/obsidian"};
constexpr char const* kFaces[6]    = {"west", "east", "down", "up", "north", "south"};
constexpr float       kMin         = 0.008f;
constexpr float       kMax         = 1.0f - 0.008f;

struct Debris {
    std::chrono::steady_clock::time_point born;
    std::chrono::steady_clock::time_point drawn;
    bool                                  rolled = false;
    std::array<int, 6>                    textures{0, 0, 0, 0, 0, 0}; // DEFAULT_TEXTURE until rolled
    std::array<float, 3>                  min{kMin, kMin, kMin};
    std::array<float, 3>                  max{kMax, kMax, kMax};
};

float clampCube(float v) { return std::min(std::max(v, kMin), kMax); }

/// ReactorDebrisBlockEntity#calculateVoxelShape (+ #randomizeTextures).
void roll(Debris& d) {
    for (;;) {
        float mn[3], len[3];
        for (int k = 0; k < 3; ++k) mn[k] = nextInt(16) / 16.0f;
        for (int k = 0; k < 3; ++k) {
            int bound = static_cast<int>(17 - mn[k] * 16); // nextInt(1, bound): 1 .. bound - 1
            len[k]    = (1 + nextInt(bound - 1)) / 16.0f;
        }
        if (len[0] * len[1] * len[2] < 1.0f / 8.0f) continue;
        for (int k = 0; k < 3; ++k) {
            d.min[static_cast<std::size_t>(k)] = clampCube(mn[k]);
            d.max[static_cast<std::size_t>(k)] = clampCube(mn[k] + len[k]);
        }
        break;
    }
    for (auto& t : d.textures) t = nextInt(4);
    d.rolled = true;
}

std::mutex& debrisMutex() {
    static std::mutex m;
    return m;
}
std::map<std::tuple<int, int, int>, Debris>& debris() {
    static std::map<std::tuple<int, int, int>, Debris> m;
    return m;
}

/// The portal texture is the flipbook strip (one frame per 16 px of height): its faces then use the
/// strip geometries, which map the first frame.
bool portalIsStrip() {
    static int strip = -1;
    if (strip < 0) {
        auto size = gf::client::ui::textureSize(kTextures[2]);
        strip     = size && size->second >= size->first * 2 ? 1 : 0;
    }
    return strip == 1;
}

void drawDebris(render::RenderContext& ctx, BlockPos const& pos, Block const&, float) {
    using clock = std::chrono::steady_clock;
    auto   now  = clock::now();
    Debris d;
    {
        std::lock_guard lock(debrisMutex());
        auto&           all = debris();
        auto [it, fresh]    = all.try_emplace({pos.x, pos.y, pos.z});
        if (fresh) it->second.born = now;
        // a debris block that was not drawn for 2 s is another one (the old one was destroyed)
        if (!fresh && now - it->second.drawn > std::chrono::seconds(2)) it->second = Debris{now, now};
        it->second.drawn = now;
        // timeAlive == 5: randomizeDimensions + randomizeTextures (once: will_disappear, no rerolls)
        if (!it->second.rolled && now - it->second.born >= std::chrono::milliseconds(250)) roll(it->second);
        d = it->second;
        if (all.size() > 4096)
            for (auto e = all.begin(); e != all.end();) e = now - e->second.drawn > std::chrono::seconds(2) ? all.erase(e) : std::next(e);
    }
    render::Transform t;
    t.translation = Vec3{pos.x + (d.min[0] + d.max[0]) / 2.0f, pos.y + d.min[1], pos.z + (d.min[2] + d.max[2]) / 2.0f};
    t.scale       = Vec3{d.max[0] - d.min[0], d.max[1] - d.min[1], d.max[2] - d.min[2]};
    for (int f = 0; f < 6; ++f) {
        int         tex   = d.textures[static_cast<std::size_t>(f)];
        bool        strip = tex == 2 && portalIsStrip();
        std::string geo   = std::string("geometry.twilightforest.darktower.debris_") + kFaces[f] + (strip ? "_strip" : "");
        render::drawGeo(ctx, geo, kTextures[tex], t);
    }
}

class DarkTowerClientModule {
public:
    void install() {
        auto& s    = st();
        s.listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            try {
                onTick();
            } catch (std::exception const& e) {
                log().error("darktower: client tick failed: {}", e.what());
            }
        });
        static bool registered = false;
        if (registered) return;
        registered = true;
        gf::feature::net::client::on("darktower.urghast", onUrGhast);
        render::registerBlockRenderer("twilightforest:reactor_debris", drawDebris);
    }
    void uninstall() {
        auto& s = st();
        if (s.listener) ll::event::EventBus::getInstance().removeListener(s.listener);
        s.listener = {};
    }
};

} // namespace

} // namespace gf::client::darktower

GF_REGISTER_MODULE(gf::client::darktower::DarkTowerClientModule);

#endif // GF_CLIENT

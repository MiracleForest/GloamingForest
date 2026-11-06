// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/entity/FireflyBlockEntity.java
//   src/main/java/twilightforest/block/entity/CicadaBlockEntity.java
//   src/main/java/twilightforest/block/entity/MoonwormBlockEntity.java
//   src/main/java/twilightforest/block/FireflyJarBlock.java (animateTick)
//   src/main/java/twilightforest/block/CicadaJarBlock.java (animateTick)
//   src/main/java/twilightforest/block/AbstractParticleSpawnerBlock.java (animateTick)
//   src/main/java/twilightforest/block/FireflySpawnerBlock.java (animateTick)
//   src/main/java/twilightforest/block/MossPatchBlock.java (animateTick)
//   src/main/java/twilightforest/block/FallenLeavesBlock.java (animateTick, entityInside)
//   src/main/java/twilightforest/client/MovingCicadaSoundInstance.java
//   src/main/java/twilightforest/network/SpawnFallenLeafFromPacket.java
//   src/main/java/twilightforest/network/SetMasonJarItemPacket.java
//   src/main/java/twilightforest/network/CreateMovingCicadaSoundPacket.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U6 flora on the client (client mod): what TF's critters and jars do on the client.
// - The critter block entities' client ticks: fireflies sparkle (1/20 a tick with a player within
//   16 blocks), cicadas sing (a 100 tick song every 100..200 ticks: the cicada sound, unless
//   silentCicadas, and note particles). The critters the client knows are the ones its renderer
//   pass draws (client::render records the block positions of the registered blocks).
// - ClientLevel#animateTick around the local player (667 samples at radius 16 and 667 at 32): the
//   firefly jar's fireflies, the cicada jar's notes and song, the firefly particle spawner, the moss
//   patch's mycelium and the leaves falling onto fallen leaves.
// - Fallen leaves slow the local player above 2 layers (makeStuckInBlock) and the server's
//   flora.fallen_leaf messages kick up leaves (SpawnFallenLeafFromPacket).
// - flora.moving_cicada_sound: the looping cicada of a player wearing one (MovingCicadaSoundInstance).
// - flora.mason_jar_item / flora.jar_wobble: the jars' contents and wobbles (SetMasonJarItemPacket,
//   the block event of a wobble), kept for the jars the client draws.
#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <tuple>
#include <unordered_map>

#include "nlohmann/json.hpp"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Render.h"
#include "feature/flora/Flora.h"
#include "feature/fx/FxInternal.h"
#include "game/Items.h"
#include "feature/net/Net.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"


namespace gf::client::flora {

namespace {

constexpr char kFirefly[]  = "twilightforest:firefly";
constexpr char kCicada[]   = "twilightforest:cicada";
constexpr char kMoonworm[] = "twilightforest:moonworm";

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int    nextInt(int bound) { return bound <= 0 ? 0 : static_cast<int>(rng()() % static_cast<unsigned>(bound)); }
float  nextFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }
bool   nextBoolean() { return (rng()() & 1u) != 0; }

using Key = std::tuple<int, int, int>;

/// A critter's client block entity (FireflyBlockEntity / CicadaBlockEntity / MoonwormBlockEntity).
struct Critter {
    int          kind     = 0; ///< 0 firefly, 1 cicada, 2 moonworm
    std::int64_t lastSeen = 0;
    int          yawDelay = 0, currentYaw = 0, desiredYaw = 0;
    float        glow = 0.f;
    bool         glowing = false;
    int          glowDelay = 0;
    int          singDuration = 0, singDelay = 0;
    bool         singing = false;
};

struct MasonJar {
    nlohmann::json item;
    int            rotation = 0;
    std::string    lid;
};

struct State {
    std::mutex                              mutex; ///< `seen` is written by the render pass
    std::map<Key, std::pair<int, std::int64_t>> seen; ///< critter positions the renderer drew -> (kind, frame tick)
    std::map<Key, Critter>                  critters;
    std::map<Key, MasonJar>                 jars;
    std::map<Key, std::int64_t>             wobbles;
    std::unordered_map<std::int64_t, int>   cicadaSounds; ///< wearer unique id -> ticks to the next loop
    std::int64_t                            tick = 0;
    bool                                    loggedFireflies = false;
};
State& st() {
    static State s;
    return s;
}

/// The empty molang map effects are added with (never destroyed: the game's destructor must not
/// run at process exit).
MolangVariableMap const& emptyMolang() {
    static auto* m = new gf::fx::MolangMap();
    return m->get();
}

void effect(LocalPlayer& player, char const* name, Vec3 const& at) {
    static std::unordered_map<std::string, HashedString> names;
    auto it = names.find(name);
    if (it == names.end()) it = names.emplace(name, HashedString(name)).first;
    player.getLevel().addParticleEffect(it->second, at, emptyMolang());
}

void playSound(char const* name, Vec3 const& at, float volume, float pitch) {
    auto* c = currentClient();
    if (!c) return;
    c->getSoundEngine()->play(name, at, volume, pitch, std::nullopt);
}

bool silent() { return feature::flora::config().silentCicadas; }

// ---- block entity ticks ------------------------------------------------------------------------------

/// The yaw wiggle of fireflies and cicadas.
void wiggle(Critter& c) {
    if (c.yawDelay > 0) {
        --c.yawDelay;
        return;
    }
    if (c.currentYaw == 0 && c.desiredYaw == 0) {
        c.yawDelay   = 200 + nextInt(200);
        c.desiredYaw = nextInt(15) - nextInt(15);
    }
    if (c.currentYaw < c.desiredYaw) ++c.currentYaw;
    if (c.currentYaw > c.desiredYaw) --c.currentYaw;
    if (c.currentYaw == c.desiredYaw) c.desiredYaw = 0;
}

void tickFirefly(LocalPlayer& player, BlockPos const& p, Critter& c, double distSq) {
    if (distSq <= 16.0 * 16.0 && nextInt(20) == 0) {
        effect(player, "twilightforest:firefly", Vec3{p.x + nextFloat(), p.y + nextFloat(), p.z + nextFloat()});
        if (!st().loggedFireflies) {
            st().loggedFireflies = true;
            gf::log().info("flora: the client draws the fireflies' particles");
        }
    }
    wiggle(c);
    if (c.glowDelay > 0) {
        --c.glowDelay;
        return;
    }
    if (c.glowing && c.glow >= 1.0f) c.glowing = false;
    if (c.glowing && c.glow < 1.0f) c.glow += 0.05f;
    if (!c.glowing && c.glow > 0) c.glow -= 0.05f;
    if (!c.glowing && c.glow <= 0) {
        c.glowing   = true;
        c.glow      = 0.0f;
        c.glowDelay = nextInt(50);
    }
}

void tickCicada(LocalPlayer& player, BlockPos const& p, Critter& c) {
    wiggle(c);
    if (c.singDelay > 0) {
        --c.singDelay;
        return;
    }
    if (c.singing && c.singDuration == 0 && !silent()) playSound("block.twilightforest.cicada", Vec3{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)}, 1.0f, (nextFloat() - nextFloat()) * 0.2f + 1.0f);
    if (c.singing && c.singDuration >= 100) {
        c.singing      = false;
        c.singDuration = 0;
    }
    if (c.singing) {
        ++c.singDuration;
        if (nextInt(5) == 0) effect(player, "minecraft:note_particle", Vec3{p.x + nextFloat(), p.y + nextFloat(), p.z + nextFloat()});
    }
    if (!c.singing && c.singDuration <= 0) {
        c.singing   = true;
        c.singDelay = 100 + nextInt(100);
    }
}

void tickMoonworm(Critter& c) {
    if (c.currentYaw == -1) c.currentYaw = nextInt(4) * 90;
    if (c.yawDelay > 0) {
        --c.yawDelay;
        return;
    }
    if (c.desiredYaw == 0) {
        c.yawDelay   = 200 + nextInt(200);
        c.desiredYaw = nextInt(4) * 90;
    }
    if (++c.currentYaw > 360) c.currentYaw = 0;
    if (c.currentYaw == c.desiredYaw) c.desiredYaw = 0;
}

void tickCritters(LocalPlayer& player, BlockSource const& region) {
    auto& s = st();
    {
        std::lock_guard lock(s.mutex);
        for (auto const& [k, v] : s.seen) {
            auto [it, fresh] = s.critters.try_emplace(k);
            it->second.kind     = v.first;
            it->second.lastSeen = s.tick;
            if (fresh && v.first == 2) it->second.currentYaw = -1;
        }
        s.seen.clear();
    }
    Vec3 at = player.getPosition();
    for (auto it = s.critters.begin(); it != s.critters.end();) {
        auto [x, y, z] = it->first;
        BlockPos p{x, y, z};
        auto const& id = region.getBlock(p).getTypeName();
        bool        ok = (it->second.kind == 0 && id == kFirefly) || (it->second.kind == 1 && id == kCicada) || (it->second.kind == 2 && id == kMoonworm);
        if (!ok || s.tick - it->second.lastSeen > 200) {
            it = s.critters.erase(it);
            continue;
        }
        double dx = x + .5 - at.x, dy = y + .5 - at.y, dz = z + .5 - at.z;
        switch (it->second.kind) {
        case 0: tickFirefly(player, p, it->second, dx * dx + dy * dy + dz * dz); break;
        case 1: tickCicada(player, p, it->second); break;
        default: tickMoonworm(it->second); break;
        }
        ++it;
    }
}

// ---- animateTick -------------------------------------------------------------------------------------

/// Java's default foliage colour (FoliageColor.getDefaultColor) varied by +-0x11 per channel.
gf::fx::Rgba leafColour() {
    int base[3]{0x48, 0xB5, 0x18};
    gf::fx::Rgba c;
    float* out[3]{&c.r, &c.g, &c.b};
    for (int i = 0; i < 3; ++i) *out[i] = static_cast<float>(std::clamp(base[i] + nextInt(0x22) - 0x11, 0, 255)) / 255.0f;
    return c;
}

void spawnLeaf(LocalPlayer& player, Vec3 const& at, Vec3 const& velocity) {
    static auto* map = new gf::fx::MolangMap();
    map->setColor("variable.color", leafColour(), false);
    map->setVelocity(velocity);
    static HashedString const name("twilightforest:fallen_leaf");
    player.getLevel().addParticleEffect(name, at, map->get());
}

void animateTick(LocalPlayer& player, BlockSource const& region, BlockPos const& p, Block const& b) {
    auto const& id = b.getTypeName();
    if (id == "twilightforest:firefly_jar") {
        for (int i = 0; i < 2; ++i)
            effect(player, "twilightforest:firefly",
                   Vec3{p.x + ((nextFloat() - nextFloat()) * 0.2f + 0.5f), p.y + 0.4f + ((nextFloat() - nextFloat()) * 0.3f), p.z + ((nextFloat() - nextFloat()) * 0.2f + 0.5f)});
    } else if (id == "twilightforest:cicada_jar") {
        effect(player, "minecraft:note_particle",
               Vec3{p.x + ((nextFloat() - nextFloat()) * 0.2f + 0.5f), p.y + 0.4f + ((nextFloat() - nextFloat()) * 0.2f), p.z + ((nextFloat() - nextFloat()) * 0.2f + 0.5f)});
        if (nextInt(75) == 0 && !silent()) playSound("block.twilightforest.cicada", Vec3{p.x + .5f, p.y + .5f, p.z + .5f}, 1.0f, 1.0f);
    } else if (id == "twilightforest:firefly_particle_spawner") {
        int radius = 1;
        // the particle_radius state
        if (auto const& nbt = *b.mSerializationId; nbt.contains("states")) {
            auto const& states = nbt["states"].get<CompoundTag>();
            if (states.contains("twilightforest:particle_radius")) radius = std::max(1, static_cast<int>(states["twilightforest:particle_radius"].get<IntTag>().data));
        }
        int count = (radius + 1) / 2;
        for (int i = 0; i < count; ++i) {
            BlockPos m{p.x + nextInt(2 * radius + 1) - radius, p.y + nextInt(2 * radius + 1) - radius, p.z + nextInt(2 * radius + 1) - radius};
            if (m.y < region.getMinHeight() || m.y >= region.getMaxHeight()) continue;
            Block const& off = region.getBlock(m);
            if (!off.isAir() && off.getMaterial().mSolid) continue; // isCollisionShapeFullBlock
            effect(player, "twilightforest:particle_spawner_firefly", Vec3{static_cast<float>(m.x + nextDouble()), static_cast<float>(m.y + nextDouble()), static_cast<float>(m.z + nextDouble())});
        }
        if (nextInt(5) == 0)
            effect(player, "twilightforest:firefly",
                   Vec3{p.x + ((nextFloat() - nextFloat()) * 0.2f + 0.5f), p.y + 0.4f + ((nextFloat() - nextFloat()) * 0.3f), p.z + ((nextFloat() - nextFloat()) * 0.2f + 0.5f)});
    } else if (id == "twilightforest:moss_patch") {
        if (nextInt(10) == 0) effect(player, "minecraft:mycelium_dust_particle", Vec3{p.x + nextFloat(), p.y + 0.1f, p.z + nextFloat()});
    } else if (id == "twilightforest:fallen_leaves") {
        if (nextInt(50) != 0) return;
        float dist = 10.0f;
        if (!const_cast<BlockSource&>(region).canSeeSkyFromBelowWater(p)) {
            for (int y = 0; y <= 10; ++y) {
                BlockPos u{p.x, p.y + y, p.z};
                if (u.y >= region.getMaxHeight()) break;
                if (region.getBlock(u).getTypeName().find("leaves") != std::string::npos && region.getBlock(u).getTypeName() != "twilightforest:fallen_leaves") {
                    dist = static_cast<float>(y);
                    break;
                }
            }
            if (dist > 10.0f) return;
        }
        spawnLeaf(player, Vec3{p.x + nextFloat(), p.y + dist - 0.25f, p.z + nextFloat()}, Vec3{});
    }
}

void animateTicks(LocalPlayer& player, BlockSource const& region) {
    auto     pos = player.getPosition();
    BlockPos at{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    short    minY = region.getMinHeight(), maxY = region.getMaxHeight();
    for (int j = 0; j < 667; ++j) {
        for (int range : {16, 32}) {
            BlockPos p{at.x + nextInt(range) - nextInt(range), at.y + nextInt(range) - nextInt(range), at.z + nextInt(range) - nextInt(range)};
            if (p.y < minY || p.y >= maxY) continue;
            Block const& b = region.getBlock(p);
            if (b.isAir()) continue;
            auto const& id = b.getTypeName();
            if (id.size() < 15 || id.compare(0, 15, "twilightforest:") != 0) continue;
            animateTick(player, region, p, b);
        }
    }
}

// ---- fallen leaves under the local player ----------------------------------------------------------------

void fallenLeavesSlowdown(LocalPlayer& player, BlockSource const& region, movement::MoveState& move) {
    auto box = player.getAABB();
    int  worst = 0;
    for (int x = static_cast<int>(std::floor(box.min.x)); x <= static_cast<int>(std::floor(box.max.x - 1e-7)); ++x)
        for (int y = static_cast<int>(std::floor(box.min.y)); y <= static_cast<int>(std::floor(box.max.y - 1e-7)); ++y)
            for (int z = static_cast<int>(std::floor(box.min.z)); z <= static_cast<int>(std::floor(box.max.z - 1e-7)); ++z) {
                BlockPos     p{x, y, z};
                Block const& b = region.getBlock(p);
                if (b.getTypeName() != "twilightforest:fallen_leaves") continue;
                auto const& nbt = *b.mSerializationId;
                if (!nbt.contains("states")) continue;
                auto const& states = nbt["states"].get<CompoundTag>();
                if (states.contains("twilightforest:layers")) worst = std::max(worst, static_cast<int>(states["twilightforest:layers"].get<IntTag>().data));
            }
    if (worst <= 2) return;
    // Entity#makeStuckInBlock: the motion of this tick times (1 - 0.05 * (layers - 2), 1, same)
    float f = 1.0f - 0.05f * static_cast<float>(worst - 2);
    move.velocity.x *= f;
    move.velocity.z *= f;
}

// ---- moving cicada sounds -------------------------------------------------------------------------------

void tickCicadaSounds(LocalPlayer& player) {
    auto& sounds = st().cicadaSounds;
    if (sounds.empty()) return;
    bool off = silent() || feature::flora::config().silentCicadasOnHead;
    for (auto it = sounds.begin(); it != sounds.end();) {
        Actor* wearer = player.getLevel().fetchEntity(ActorUniqueID{it->first}, false);
        if (!wearer || !wearer->isAlive() || game::items::idOf(wearer->getArmor(SharedTypes::Legacy::ArmorSlot::Head)) != kCicada) {
            it = sounds.erase(it); // MovingCicadaSoundInstance#tick: stop
            continue;
        }
        if (--it->second <= 0) {
            // the looping sound, restarted at the wearer (the sound follows it)
            if (!off) playSound("block.twilightforest.cicada", wearer->getPosition(), 1.0f, 1.0f);
            it->second = 60;
        }
        ++it;
    }
}

class FloraClientModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        // the critters the render pass draws (their client block entities)
        int kind = 0;
        for (char const* id : {kFirefly, kCicada, kMoonworm}) {
            render::registerBlockRenderer(id, [kind](render::RenderContext&, BlockPos const& pos, Block const&, float) {
                auto& s = st();
                std::lock_guard lock(s.mutex);
                s.seen[{pos.x, pos.y, pos.z}] = {kind, s.tick};
            });
            ++kind;
        }
        movement::onLocalTick(0, [](movement::MoveState& move) {
            auto* player = currentPlayer();
            if (!player) return;
            auto const& region = player->getDimensionBlockSourceConst();
            ++st().tick;
            tickCritters(*player, region);
            animateTicks(*player, region);
            fallenLeavesSlowdown(*player, region, move);
            tickCicadaSounds(*player);
        });
        net::client::on("flora.fallen_leaf", [](nlohmann::json const& msg) {
            auto* player = currentPlayer();
            if (!player) return;
            auto const& p = msg.value("pos", nlohmann::json::array());
            auto const& m = msg.value("motion", nlohmann::json::array());
            if (!p.is_array() || p.size() != 3 || !m.is_array() || m.size() != 3) return;
            BlockPos     pos{p[0].get<int>(), p[1].get<int>(), p[2].get<int>()};
            auto const&  region = player->getDimensionBlockSourceConst();
            Block const& b      = region.getBlock(pos);
            int          layers = 1;
            if (auto const& nbt = *b.mSerializationId; nbt.contains("states")) {
                auto const& states = nbt["states"].get<CompoundTag>();
                if (states.contains("twilightforest:layers")) layers = std::max(1, static_cast<int>(states["twilightforest:layers"].get<IntTag>().data));
            }
            float mx = m[0].get<float>(), mz = m[2].get<float>();
            // SpawnFallenLeafFromPacket: FallenLeavesBlock#entityInside's client particle
            Vec3 at{pos.x + nextFloat(), pos.y + (2.0f / 16.0f) * static_cast<float>(layers - 1), pos.z + nextFloat()};
            Vec3 v{(nextFloat() * -0.5f) * mx, nextFloat() * 0.5f + 0.25f, (nextFloat() * -0.5f) * mz};
            spawnLeaf(*player, at, Vec3{v.x * 0.4f, v.y * 0.4f, v.z * 0.4f});
        });
        net::client::on("flora.moving_cicada_sound", [](nlohmann::json const& msg) {
            // CreateMovingCicadaSoundPacket: the sound instance starts after 100..200 ticks
            st().cicadaSounds[msg.value("entity", std::int64_t{0})] = 100 + nextInt(100);
        });
        net::client::on("flora.mason_jar_item", [](nlohmann::json const& msg) {
            auto const& p = msg.value("pos", nlohmann::json::array());
            if (!p.is_array() || p.size() != 3) return;
            auto& jar    = st().jars[{p[0].get<int>(), p[1].get<int>(), p[2].get<int>()}];
            jar.item     = msg.value("item", nlohmann::json{});
            jar.rotation = msg.value("rotation", 0);
            jar.lid      = msg.value("lid", std::string{});
        });
        net::client::on("flora.jar_wobble", [](nlohmann::json const& msg) {
            auto const& p = msg.value("pos", nlohmann::json::array());
            if (!p.is_array() || p.size() != 3) return;
            st().wobbles[{p[0].get<int>(), p[1].get<int>(), p[2].get<int>()}] = st().tick;
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::flora

GF_REGISTER_MODULE(gf::client::flora::FloraClientModule);

#endif

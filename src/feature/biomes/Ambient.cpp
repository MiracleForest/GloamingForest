// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/registration/biomes/BiomeHelper.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The biomes' ambient particles (BiomeSpecialEffects#ambientParticle: TF's wandering fireflies in
// the forests, white ash in the fire swamp), as Java's ClientLevel#animateTick draws them around
// the local player: 667 samples at radius 16 and 667 at radius 32 per tick (each axis
// rand(r) - rand(r) around the player's block), a sample whose block has no full collision shape
// spawning the biome's particle with its probability at a random point of the block.
//
// Bedrock clients have no data-driven ambient particle per biome, so the server stands in for the
// client, per player in the Twilight Forest (as F4 does for the portal's display ticks), and emits
// through gf::fx (TF's particles by id, owned by U6; white_ash as F10's falling-dust stand-in).
// A sample first rolls the largest probability of any biome, then the rest of its own: the same
// distribution with far fewer block and biome lookups.
// gf::fx shows a particle to every player near it, where Java shows each client only its own
// samples. So that N players close together do not see N times Java's density, a sample around a
// player spawns only when that player is the nearest Twilight Forest player to it: every point
// then gets the particles of one player's animateTick (the nearest one's), which is what each of
// them sees in Java up to the shape of the falloff between two players.
//
// The mood sound (TF's AmbientMoodSettings.LEGACY_CAVE_SETTINGS in every biome: ambient.cave,
// tick delay 6000, search extent 8, offset 2.0) is played by the server per player as
// BiomeAmbientSoundsHandler#tick does on Java's client (core/biomes/Mood): Bedrock's client biome
// mood has none of these settings, so the client biome files carry no mood.

#include <array>
#include <cmath>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/biomes/Colors.h"
#include "core/biomes/Mood.h"
#include "feature/biomes/Biomes.h"
#include "feature/dimension/World.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::biomes {

namespace {

namespace mood = core::biomes::mood;

struct AmbientParticle {
    char const* biome;
    char const* particle;
    float       probability;
};

// TF's biome JSON "effects.particle" (docs/spec/biomes.md section 6)
constexpr std::array<AmbientParticle, 7> kParticles{{
    {"twilightforest:dense_forest", "twilightforest:wandering_firefly", 0.00025f},
    {"twilightforest:dense_mushroom_forest", "twilightforest:wandering_firefly", 0.00025f},
    {"twilightforest:enchanted_forest", "twilightforest:wandering_firefly", 0.00025f},
    {"twilightforest:firefly_forest", "twilightforest:wandering_firefly", 0.001f},
    {"twilightforest:forest", "twilightforest:wandering_firefly", 0.00025f},
    {"twilightforest:mushroom_forest", "twilightforest:wandering_firefly", 0.00025f},
    {"twilightforest:fire_swamp", "minecraft:white_ash", 0.05f},
}};
constexpr float kMaxProbability = 0.05f;

AmbientParticle const* particleOf(std::string const& biome) {
    for (auto const& p : kParticles)
        if (biome == p.biome) return &p;
    return nullptr;
}

/// Java's BlockState#isCollisionShapeFullBlock: the collision shape is exactly the unit cube (a
/// slab, stair, fence or wall cell is not; neither is a block without collision).
bool fullCubeCollision(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (block.isAir()) return false;
    thread_local std::vector<AABB> boxes;
    boxes.clear();
    block.getBlockType().addCollisionShapes(block, region, pos, nullptr, boxes, {});
    if (boxes.size() != 1) return false;
    AABB        b  = boxes.front();
    auto        eq = [](float a, float c) { return a > c - 1e-4f && a < c + 1e-4f; };
    // the engine answers in world space; a local (0..1) answer is moved to the block (as F4's
    // teleporter guards against)
    if (b.min.x >= -1e-4f && b.max.x <= 1.0001f && b.min.z >= -1e-4f && b.max.z <= 1.0001f && (pos.x < -1 || pos.x > 1 || pos.z < -1 || pos.z > 1)) {
        Vec3 o(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
        b = AABB{b.min + o, b.max + o};
    }
    return eq(b.min.x, float(pos.x)) && eq(b.min.y, float(pos.y)) && eq(b.min.z, float(pos.z)) && eq(b.max.x, float(pos.x + 1))
        && eq(b.max.y, float(pos.y + 1)) && eq(b.max.z, float(pos.z + 1));
}

std::string biomeIdOf(Biome const& b) {
    auto const& name = b.mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

} // namespace

struct BiomeAmbientModule {
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) { tick(); });
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mTick); }

private:
    void tick() {
        auto twilight = world::twilight();
        auto level    = ll::service::getLevel();
        if (!twilight || !level) return;
        auto* region = game::regionOf(static_cast<int>(*twilight));
        if (!region) return;
        mPlayers.clear();
        level->forEachPlayer([&](Player& player) {
            if (player.getDimensionId() != *twilight) return true;
            mPlayers.push_back(game::blockPosOf(player.getFeetPos()));
            moodTick(*region, player);
            return true;
        });
        for (std::size_t i = 0; i < mPlayers.size(); ++i) animateTick(*region, i);
    }

    /// BiomeAmbientSoundsHandler#tick's mood part for `player` (TF's mood settings in every TF
    /// biome). Bedrock's own mood timing does not apply: the client biome files have no mood.
    void moodTick(BlockSource& region, Player& player) {
        if (player.isSimulatedPlayer()) return;
        Vec3 const feet = player.getFeetPos();
        // Entity#getEyeY: the player's eye height (1.62 standing, 1.27 crouching)
        double const eyeY = feet.y + (player.isSneaking() ? 1.27 : 1.62);
        if (!core::biomes::colors::water(biomeIdOf(region.getBiome(game::blockPosOf(feet)))).has_value()) return; // a TF biome
        auto const&                        s = mood::kTwilight;
        std::uniform_int_distribution<int> axis(0, s.blockSearch * 2);
        // Java draws x, y, z in that order
        int      dx = axis(mRandom) - s.blockSearch, dy = axis(mRandom) - s.blockSearch, dz = axis(mRandom) - s.blockSearch;
        BlockPos pos{static_cast<int>(std::floor(feet.x + dx)), static_cast<int>(std::floor(eyeY + dy)), static_cast<int>(std::floor(feet.z + dz))};
        int      sky = 0, block = 0;
        if (pos.y >= region.getMaxHeight()) sky = 15;
        else if (pos.y >= region.getMinHeight()) {
            if (!region.hasChunksAt(pos, 0, false)) return;
            auto light = region.getBrightnessPair(pos);
            sky        = light.sky->mValue;
            block      = light.block->mValue;
        }
        if (mMoodiness.size() > 1024) mMoodiness.clear(); // players long gone (a moodiness restarts at 0)
        float& moodiness = mMoodiness[player.getUuid().asString()];
        if (!mood::step(moodiness, sky, block, s)) return;
        auto at  = mood::soundPosition(mood::Vec{feet.x, eyeY, feet.z}, pos.x, pos.y, pos.z, s);
        auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
        if (!pkt) return;
        auto& sound   = static_cast<PlaySoundPacket&>(*pkt);
        sound.mName   = "ambient.cave";
        sound.mPos    = BlockPos{static_cast<int>(std::floor(at.x * 8)), static_cast<int>(std::floor(at.y * 8)), static_cast<int>(std::floor(at.z * 8))};
        sound.mVolume = mood::kVolume;
        sound.mPitch  = mood::kPitch;
        sound.sendTo(player);
        if (!mMoodLogged) {
            mMoodLogged = true;
            gf::log().info("biomes: mood sound ambient.cave played to {} at {:.1f} {:.1f} {:.1f}", player.getRealName(), at.x, at.y, at.z);
        }
    }

    /// Whether player `self` is the nearest of mPlayers to `pos` (ties: the first).
    [[nodiscard]] bool nearest(std::size_t self, BlockPos const& pos) const {
        auto dist = [&](BlockPos const& p) {
            long long dx = p.x - pos.x, dy = p.y - pos.y, dz = p.z - pos.z;
            return dx * dx + dy * dy + dz * dz;
        };
        long long mine = dist(mPlayers[self]);
        for (std::size_t i = 0; i < mPlayers.size(); ++i) {
            if (i == self) continue;
            long long d = dist(mPlayers[i]);
            if (d < mine || (d == mine && i < self)) return false;
        }
        return true;
    }

    /// ClientLevel#animateTick's ambient particle part around player `self`.
    void animateTick(BlockSource& region, std::size_t self) {
        BlockPos const                        at = mPlayers[self];
        std::uniform_real_distribution<float> unit(0.f, 1.f);
        for (int j = 0; j < 667; ++j) {
            for (int range : {16, 32}) {
                if (unit(mRandom) > kMaxProbability) continue;
                std::uniform_int_distribution<int> axis(0, range - 1);
                BlockPos pos{at.x + axis(mRandom) - axis(mRandom), at.y + axis(mRandom) - axis(mRandom), at.z + axis(mRandom) - axis(mRandom)};
                if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) continue;
                if (!region.hasChunksAt(pos, 0, false)) continue;
                Block const& block = region.getBlock(pos);
                if (fullCubeCollision(region, pos, block)) continue; // !isCollisionShapeFullBlock
                auto const* p = particleOf(biomeIdOf(region.getBiome(pos)));
                if (!p || unit(mRandom) * kMaxProbability > p->probability) continue;
                if (mPlayers.size() > 1 && !nearest(self, pos)) continue;
                Vec3 spawn{pos.x + unit(mRandom), pos.y + unit(mRandom), pos.z + unit(mRandom)};
                fx::particle(region.getDimension(), p->particle, spawn);
            }
        }
    }

    std::vector<BlockPos>                  mPlayers; ///< feet blocks of the Twilight Forest's players, this tick
    std::unordered_map<std::string, float> mMoodiness; ///< BiomeAmbientSoundsHandler#moodiness per player (UUID)
    bool                                   mMoodLogged = false;
    ll::event::ListenerPtr                 mTick;
    std::mt19937           mRandom{std::random_device{}()};
};

} // namespace gf::feature::biomes

GF_REGISTER_MODULE(gf::feature::biomes::BiomeAmbientModule);

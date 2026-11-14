// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/registration/biomes/BiomeHelper.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The biomes' ambient particles (BiomeSpecialEffects#ambientParticle: upstream's wandering fireflies in
// the forests, white ash in the fire swamp), as Java's ClientLevel#animateTick draws them around
// the local player: 667 samples at radius 16 and 667 at radius 32 per tick (each axis
// rand(r) - rand(r) around the player's block), a sample whose block has no full collision shape
// spawning the biome's particle with its probability at a random point of the block.
//
// Bedrock:
// - white ash: Bedrock clients draw it themselves for a biome whose client biome file asks for it
//   (minecraft:precipitation "white_ash", what the basalt deltas use: Java's 0.118093334 there is
//   Bedrock's 2.0), so the fire swamp's client file carries upstream's 0.05 as 0.05 / 0.118093334 * 2.0;
//   every client, vanilla included, draws Java's small white specks.
// - fireflies: Bedrock has no data-driven ambient particle per biome. The client mod runs this
//   animateTick itself (src/client/biomes/Ambient.cpp: only its own samples, drawn locally, as on
//   Java); for players without it the server stands in, per player in the Gloaming Forest, and
//   sends each of them its own samples only (a SpawnParticleEffectPacket of the resource pack's
//   effect to that player).
// A sample first rolls the largest probability of any biome, then the rest of its own: the same
// distribution with far fewer block and biome lookups.
//
// The mood sound (upstream's AmbientMoodSettings.LEGACY_CAVE_SETTINGS in every biome: ambient.cave,
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
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacketPayload.h"
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
#include "feature/biomes/Ambient.h"
#include "feature/biomes/Biomes.h"
#include "feature/dimension/World.h"
#include "feature/net/Net.h"
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

// upstream's biome JSON "effects.particle" (docs/spec/biomes.md section 6); the fire swamp's white ash is
// drawn by the clients from its client biome file (see above)
constexpr std::array<AmbientParticle, 6> kParticles{{
    {"gloamingforest:dense_forest", "gloamingforest:wandering_firefly", 0.00025f},
    {"gloamingforest:dense_mushroom_forest", "gloamingforest:wandering_firefly", 0.00025f},
    {"gloamingforest:enchanted_forest", "gloamingforest:wandering_firefly", 0.00025f},
    {"gloamingforest:firefly_forest", "gloamingforest:wandering_firefly", 0.001f},
    {"gloamingforest:forest", "gloamingforest:wandering_firefly", 0.00025f},
    {"gloamingforest:mushroom_forest", "gloamingforest:wandering_firefly", 0.00025f},
}};
constexpr float kMaxProbability = 0.001f;

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

} // namespace

char const* ambientEffectOf(std::string const& particleId) {
    // U6's resource pack effect of the wandering firefly has the particle's id
    for (auto const& p : kParticles)
        if (particleId == p.particle) return p.particle;
    return nullptr;
}

void ambientParticles(BlockSource& region, BlockPos const& at, std::mt19937& random, std::function<void(std::string const&, Vec3 const&)> const& spawn) {
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    for (int j = 0; j < 667; ++j) {
        for (int range : {16, 32}) {
            if (unit(random) > kMaxProbability) continue;
            std::uniform_int_distribution<int> axis(0, range - 1);
            BlockPos pos{at.x + axis(random) - axis(random), at.y + axis(random) - axis(random), at.z + axis(random) - axis(random)};
            if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) continue;
            if (!region.hasChunksAt(pos, 0, false)) continue;
            Block const& block = region.getBlock(pos);
            if (fullCubeCollision(region, pos, block)) continue; // !isCollisionShapeFullBlock
            auto const* p = particleOf(biomeIdOf(region.getBiome(pos)));
            if (!p || unit(random) * kMaxProbability > p->probability) continue;
            spawn(p->particle, Vec3{pos.x + unit(random), pos.y + unit(random), pos.z + unit(random)});
        }
    }
}

struct BiomeAmbientModule {
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) { tick(); });
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mTick); }

private:
    void tick() {
        auto gloaming = world::gloaming();
        auto level    = ll::service::getLevel();
        if (!gloaming || !level) return;
        auto* region = game::regionOf(static_cast<int>(*gloaming));
        if (!region) return;
        level->forEachPlayer([&](Player& player) {
            if (player.getDimensionId() != *gloaming) return true;
            moodTick(*region, player);
            // the client mod draws its own player's particles (src/client/biomes/Ambient.cpp)
            if (!player.isSimulatedPlayer() && !net::isCompanion(player)) animateTick(*region, player);
            return true;
        });
    }

    /// BiomeAmbientSoundsHandler#tick's mood part for `player` (upstream's mood settings in every upstream
    /// biome). Bedrock's own mood timing does not apply: the client biome files have no mood.
    void moodTick(BlockSource& region, Player& player) {
        if (player.isSimulatedPlayer()) return;
        Vec3 const feet = player.getFeetPos();
        // Entity#getEyeY: the player's eye height (1.62 standing, 1.27 crouching)
        double const eyeY = feet.y + (player.isSneaking() ? 1.27 : 1.62);
        if (!isGloamingBiome(biomeIdOf(region.getBiome(game::blockPosOf(feet))))) return;
        auto const&                        s = mood::kGloaming;
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

    /// ClientLevel#animateTick's ambient particle part for a player without the client mod: its own
    /// samples, sent to it only.
    void animateTick(BlockSource& region, Player& player) {
        auto dimId = static_cast<uchar>(static_cast<int>(player.getDimensionId()));
        ambientParticles(region, game::blockPosOf(player.getFeetPos()), mRandom, [&](std::string const& particle, Vec3 const& pos) {
            char const* effect = ambientEffectOf(particle);
            if (!effect) return;
            SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(pos, effect, dimId, std::nullopt));
            pkt.sendTo(player);
            if (!mParticleLogged) {
                mParticleLogged = true;
                gf::log().info("biomes: ambient particle {} sent to {} at {:.1f} {:.1f} {:.1f}", particle, player.getRealName(), pos.x, pos.y, pos.z);
            }
        });
    }

    std::unordered_map<std::string, float> mMoodiness; ///< BiomeAmbientSoundsHandler#moodiness per player (UUID)
    bool                                   mMoodLogged     = false;
    bool                                   mParticleLogged = false;
    ll::event::ListenerPtr                 mTick;
    std::mt19937                           mRandom{std::random_device{}()};
};

} // namespace gf::feature::biomes

GF_REGISTER_MODULE(gf::feature::biomes::BiomeAmbientModule);
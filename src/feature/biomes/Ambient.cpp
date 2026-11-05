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
// distribution with far fewer block and biome lookups. Particles are visible to every player near,
// where Java shows each client only its own.

#include <array>
#include <random>
#include <string>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/biomes/Biomes.h"
#include "feature/dimension/World.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "mod/Module.h"

namespace gf::feature::biomes {

namespace {

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
        level->forEachPlayer([&](Player& player) {
            if (player.getDimensionId() != *twilight) return true;
            animateTick(*region, game::blockPosOf(player.getFeetPos()));
            return true;
        });
    }

    /// ClientLevel#animateTick's ambient particle part around `at`.
    void animateTick(BlockSource& region, BlockPos const& at) {
        std::uniform_real_distribution<float> unit(0.f, 1.f);
        for (int j = 0; j < 667; ++j) {
            for (int range : {16, 32}) {
                if (unit(mRandom) > kMaxProbability) continue;
                std::uniform_int_distribution<int> axis(0, range - 1);
                BlockPos pos{at.x + axis(mRandom) - axis(mRandom), at.y + axis(mRandom) - axis(mRandom), at.z + axis(mRandom) - axis(mRandom)};
                if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) continue;
                if (!region.hasChunksAt(pos, 0, false)) continue;
                Block const& block = region.getBlock(pos);
                if (!block.isAir() && features::kit::blocksMotion(block)) continue; // isCollisionShapeFullBlock
                auto const* p = particleOf(biomeIdOf(region.getBiome(pos)));
                if (!p || unit(mRandom) * kMaxProbability > p->probability) continue;
                Vec3 spawn{pos.x + unit(mRandom), pos.y + unit(mRandom), pos.z + unit(mRandom)};
                fx::particle(region.getDimension(), p->particle, spawn);
            }
        }
    }

    ll::event::ListenerPtr mTick;
    std::mt19937           mRandom{std::random_device{}()};
};

} // namespace gf::feature::biomes

GF_REGISTER_MODULE(gf::feature::biomes::BiomeAmbientModule);

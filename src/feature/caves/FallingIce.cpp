// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/projectile/FallingIce.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The falling ice the Alpha Yeti shakes loose: a falling block that hangs for `hangTime` ticks,
// then falls (-0.04 a tick, x 0.98 drag), hurts what it lands on by its fall distance and the
// difficulty (never an alpha yeti), shatters, and becomes its block again where it can. Its motion
// is native (the BP gives it no physics); the RP draws the block (property twilightforest:block)
// and the warning trail.
#include "feature/caves/Caves.h"

#include <cmath>
#include <string>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "core/caves/Caves.h"
#include "feature/caves/Goals.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::caves {

namespace {

constexpr char kBlockKey[] = "caves:block"; ///< FallingIce BlockState
constexpr char kTimeKey[]  = "caves:time";  ///< FallingIce Time

/// The ice blocks the RP draws (#minecraft:ice), as twilightforest:block values.
constexpr char const* kIceBlocks[] = {"minecraft:packed_ice", "minecraft:ice", "minecraft:blue_ice", "minecraft:frosted_ice"};

int iceIndex(std::string const& id) {
    for (int i = 0; i < 4; i++)
        if (id == kIceBlocks[i]) return i;
    return 0;
}

struct IceState {
    int   hangTime     = 100; ///< not saved in TF (a reloaded ice hangs 100 ticks)
    float vy           = 0.0F;
    float fallDistance = 0.0F;
};
std::unordered_map<std::int64_t, IceState>& ices() {
    static std::unordered_map<std::int64_t, IceState> s;
    return s;
}

std::string blockOf(Actor& ice) {
    auto const& b = gf::entity::data(ice, kBlockKey);
    return b.is_string() ? b.get<std::string>() : std::string("minecraft:packed_ice");
}

int difficultyId() {
    auto level = ll::service::getLevel();
    return level ? static_cast<int>(level->getDifficulty()) : 2;
}

/// FallingIce#causeFallDamage.
void causeFallDamage(Actor& ice, float dist) {
    float dmg = core::caves::fallingIceDamage(dist, difficultyId());
    auto  box = ice.getAABB();
    if (dmg >= 0.0F) {
        AABB area{Vec3{box.min.x - 1.0f, box.min.y, box.min.z - 1.0f}, Vec3{box.max.x + 1.0f, box.max.y, box.max.z + 1.0f}};
        for (auto* e : fe::actorsIn(ice.getDimensionBlockSource(), static_cast<int>(ice.getDimensionId()), area)) {
            if (e == &ice || e->getTypeName() == kAlphaYeti) continue;
            if (e->isPlayer() && static_cast<Player*>(e)->isSpectator()) continue;
            gf::combat::hurt(*e, dmg, "twilightforest:falling_ice");
        }
    }
    // shattering particles and sound
    auto p = ice.getFeetPos();
    gf::fx::particle(ice.getDimension(), "minecraft:block", p, Vec3{}, 200, nlohmann::json{{"block", blockOf(ice)}});
    gf::fx::sound(ice.getDimension(), "minecraft:block.glass.break", p, 3.0F, 0.5F);
}

/// Whether a falling block passes through the block (FallingBlock#isFree: air, fire, liquid, replaceable).
bool isFree(Block const& b) { return b.isAir() || structure::blocks::canBeReplaced(b) || structure::blocks::isLiquid(b); }

/// FallingIce#tick (server).
void tickIce(Actor& ice) {
    auto  uid   = fe::uidOf(ice);
    auto& s     = ices()[uid];
    auto  block = blockOf(ice);
    auto& time  = gf::entity::data(ice, kTimeKey);
    int   t     = time.is_number_integer() ? time.get<int>() : 0;
    time        = ++t;
    bool noGravity = t < s.hangTime;
    if (!noGravity) s.vy -= 0.04F;
    auto& region = ice.getDimensionBlockSource();
    Vec3  pos    = ice.getPosition();
    bool  onGround = false;
    if (s.vy < 0.0F) {
        // move down until the first colliding block
        float target = pos.y + s.vy;
        int   fromY  = static_cast<int>(std::floor(pos.y)) - 1;
        int   toY    = static_cast<int>(std::floor(target));
        for (int y = fromY; y >= toY; --y) {
            BlockPos b{static_cast<int>(std::floor(pos.x)), y, static_cast<int>(std::floor(pos.z))};
            if (!isFree(region.getBlock(b)) && structure::blocks::blocksMotion(region.getBlock(b))) {
                target   = static_cast<float>(y + 1);
                onGround = true;
                break;
            }
        }
        s.fallDistance += pos.y - target;
        pos.y = target;
        ice.moveTo(pos, ice.getRotation());
    }
    BlockPos blockpos{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    if (!onGround) {
        if ((t > 100 && (blockpos.y <= region.getMinHeight() || blockpos.y > region.getMaxHeight())) || t > 1000) {
            fe::removeLater(uid);
            ices().erase(uid);
        }
    } else {
        causeFallDamage(ice, s.fallDistance);
        Block const& there = region.getBlock(blockpos);
        bool         flag2 = structure::blocks::canBeReplaced(there);
        bool         flag3 = isFree(region.getBlock(BlockPos{blockpos.x, blockpos.y - 1, blockpos.z}));
        if (flag2 && !flag3)
            if (auto const* b = game::blockWith(block)) game::setBlock(region, blockpos, *b);
        fe::removeLater(uid);
        ices().erase(uid);
        gf::log().info("caves: falling ice landed after {:.1f} blocks", s.fallDistance);
        return;
    }
    s.vy *= 0.98F;
}

} // namespace

Actor* spawnFallingIce(BlockSource& region, BlockPos const& pos, std::string const& blockId, int hangTime) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Vec3 at{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f};
    auto owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(kFallingIce), nullptr, at, Vec2{0, 0});
    if (!owned) return nullptr;
    auto* ice = level->addEntity(region, std::move(owned));
    if (!ice) return nullptr;
    gf::entity::data(*ice, kBlockKey) = blockId.empty() ? std::string("minecraft:packed_ice") : blockId;
    gf::entity::data(*ice, kTimeKey)  = 0;
    gf::entity::setProperty(*ice, "twilightforest:block", iceIndex(blockId));
    ices()[fe::uidOf(*ice)] = IceState{hangTime, 0.0F, 0.0F};
    return ice;
}

int fallingIceTime(Actor& ice) {
    auto const& t = gf::entity::data(ice, kTimeKey);
    return t.is_number_integer() ? t.get<int>() : 0;
}

void installFallingIce() {
    gf::entity::ext::setCanUsePortal(kFallingIce, true);
    gf::entity::Behaviour b;
    b.onSpawn = [](Actor& self) { gf::entity::setProperty(self, "twilightforest:block", iceIndex(blockOf(self))); };
    b.tick    = [](Actor& self) {
        if (!self.isAlive()) return;
        if (blockOf(self).empty()) {
            fe::removeLater(fe::uidOf(self));
            return;
        }
        tickIce(self);
    };
    b.hurt = [](Actor&, float&, std::string const&, Actor*) { return false; }; // isAttackable: false
    gf::entity::registerType(kFallingIce, b);
}

} // namespace gf::feature::caves

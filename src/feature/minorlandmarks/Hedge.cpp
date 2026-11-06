// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/HedgeBlock.java
//   src/main/java/twilightforest/util/EntityUtil.java (rayTrace)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hedge (twilightforest:hedge): 3 cactus damage to whatever is inside it or walks on it (its
// collision box is 15/16 high, assets/pack/bp/blocks/minorlandmarks/hedge.json), spiders and items
// spared; punching it hurts, and keeps hurting every 10 ticks while the player keeps swinging at
// it; breaking it hurts once more.
#include "feature/minorlandmarks/Internal.h"

#include <cmath>
#include <mutex>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/GameType.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::minorlandmarks {

namespace {

constexpr float kDamage = 3.0F;
constexpr char  kCactus[] = "minecraft:cactus";

/// HedgeBlock#shouldDamage.
bool shouldDamage(Actor& entity, int depth = 0) {
    auto const& type = entity.getTypeName();
    if (isSpiderType(type) || type == "minecraft:item") return false;
    if (depth < 8)
        if (auto* vehicle = entity.getVehicle(); vehicle && !shouldDamage(*vehicle, depth + 1)) return false;
    return true;
}

void hurt(Actor& entity) { gf::combat::hurt(entity, kDamage, kCactus); }

/// A scheduled HedgeBlock#tick (level.scheduleTick(pos, this, 10)).
struct Pending {
    int      dim = 0;
    BlockPos pos;
    int      ticks = 10;
};
struct State {
    std::mutex                       mutex;
    std::vector<Pending>             pending;
    ll::event::ListenerPtr           tick;
    bool                             installed = false;
};
State& state() {
    static State s;
    return s;
}

void schedule(int dim, BlockPos const& pos) {
    std::lock_guard lock(state().mutex);
    for (auto& p : state().pending)
        if (p.dim == dim && p.pos == pos) return; // one scheduled tick per block (LevelTicks)
    state().pending.push_back(Pending{dim, pos, 10});
}

/// EntityUtil#rayTrace: the block the player looks at within its reach (5 blocks), stepping the eye
/// ray in small steps.
bool looksAt(Player& player, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    Vec3  eye    = player.getEyePos();
    Vec3  dir    = player.getViewVector(1.0F);
    for (float t = 0.0F; t <= 5.0F; t += 0.05F) {
        BlockPos at{static_cast<int>(std::floor(eye.x + dir.x * t)), static_cast<int>(std::floor(eye.y + dir.y * t)), static_cast<int>(std::floor(eye.z + dir.z * t))};
        if (region.getBlock(at).isAir() || st::blocks::isLiquid(region.getBlock(at))) continue; // Fluid.NONE
        return at == pos;
    }
    return false;
}

/// HedgeBlock#tick: players within 8 that swing at the hedge take 3 more and the tick comes back.
void tickHedge(Pending const& p) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto* region = game::regionOf(p.dim);
    if (!region || game::blockIdAt(*region, p.pos) != kHedge) return;
    bool again = false;
    level->forEachPlayer([&](Player& player) {
        if (player.isClientSide() || static_cast<int>(player.getDimensionId()) != p.dim) return true;
        auto const& at = player.getPosition();
        // new AABB(pos).inflate(8.0)
        if (at.x < p.pos.x - 8 || at.x > p.pos.x + 9 || at.y < p.pos.y - 8 || at.y > p.pos.y + 9 || at.z < p.pos.z - 8 || at.z > p.pos.z + 9) return true;
        // player.swinging: Bedrock's swing flag, or the player still mining the hedge
        bool swinging = static_cast<Mob&>(player).mSwinging;
        if (!swinging && player.mGameMode && player.mGameMode->getDestroyBlockPos() == p.pos && player.mGameMode->mDestroyProgress > 0.0F) swinging = true;
        if (swinging && looksAt(player, p.pos)) {
            hurt(player);
            again = true;
        }
        return true;
    });
    if (again) schedule(p.dim, p.pos);
}

void onTick() {
    std::vector<Pending> due;
    {
        std::lock_guard lock(state().mutex);
        auto&           list = state().pending;
        for (auto it = list.begin(); it != list.end();) {
            if (--it->ticks <= 0) {
                due.push_back(*it);
                it = list.erase(it);
            } else ++it;
        }
    }
    for (auto const& p : due) tickHedge(p);
}

} // namespace

void installHedge() {
    auto& s = state();
    if (!s.installed) {
        s.installed = true;
        // entityInside / stepOn
        blockevents::onEntityInside(kHedge, [](BlockSource&, BlockPos const&, Actor& entity) {
            if (shouldDamage(entity)) hurt(entity);
        });
        blockevents::onSteppedOn(kHedge, [](BlockSource&, BlockPos const&, Actor& entity) {
            if (shouldDamage(entity)) hurt(entity);
        });
        // attack: hurt the player and schedule a tick in 10
        interactions::onStartDestroy({.block = kHedge}, [](interactions::DestroyContext const& c) {
            hurt(c.player);
            schedule(c.pos.dim, BlockPos{c.pos.x, c.pos.y, c.pos.z});
            return interactions::Result::Pass;
        });
        // playerDestroy: one more prick
        interactions::onDestroyed({.block = kHedge}, [](interactions::DestroyContext const& c, bool destroyed) {
            // ServerPlayerGameMode runs playerDestroy only when not in creative
            if (destroyed && c.player.getPlayerGameType() != GameType::Creative) hurt(c.player);
        });
    }
    s.tick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) { onTick(); });
}

void uninstallHedge() {
    auto& s = state();
    if (s.tick) {
        ll::event::EventBus::getInstance().removeListener(s.tick);
        s.tick.reset();
    }
    std::lock_guard lock(s.mutex);
    s.pending.clear();
}

} // namespace gf::feature::minorlandmarks

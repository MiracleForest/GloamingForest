// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/ToolEvents.java (damageNonMazebreakerToolsMore)
//   src/main/java/twilightforest/block/MazeSlimeBlock.java
//   net/minecraft/world/level/block/SlimeBlock.java (fallOn, updateEntityAfterFallOn, stepOn: the
//   vanilla behaviour MazeSlimeBlock inherits)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U23's block rules:
//   - the mazebreaker rule: breaking a block of #twilightforest:mazebreaker_accelerated_mining (the
//     mazestone and castle blocks; U23 owns the tag) with any other damageable tool costs it 16
//     extra durability (a tag matcher, rule R6);
//   - the maze slime block: SlimeBlock's fall (no fall damage unless sneaking), bounce (the fall
//     speed turned up again) and the slowdown of walking on it. Bedrock custom blocks cannot be
//     sticky for pistons (MazeSlimeBlock#isStickyBlock / canStickTo): not ported, see
//     docs/units/labyrinth.md.
#include "feature/labyrinth/Blocks.h"

#include <cmath>
#include <functional>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "ll/api/service/Bedrock.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/ext/labyrinth/W0Internals.h"
#include "feature/families/BlockEvents.h"
#include "feature/movement/Movement.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::labyrinth::blocks {

namespace ia = gf::feature::common::interactions;
namespace fe = gf::feature::entity;

namespace {

/// The last vertical movement of each player (position change of the last tick): the speed it
/// landed with (Bedrock players move on the client; their delta is not the server's).
struct Fall {
    double lastX = 0.0, lastY = 0.0, lastZ = 0.0;
    double dx = 0.0, dy = 0.0, dz = 0.0;
    bool   known = false;
};
std::unordered_map<std::int64_t, Fall>& falls() {
    static std::unordered_map<std::int64_t, Fall> m;
    return m;
}

BlockPos blockBelow(Actor const& a) {
    auto p = a.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y - 0.2F)), static_cast<int>(std::floor(p.z))};
}

bool onMazeSlime(Actor& a) { return gf::game::blockIdAt(a.getDimensionBlockSource(), blockBelow(a)) == kMazeSlimeBlock; }

/// Entity#isSuppressingBounce / isSteppingCarefully: a sneaking player.
bool suppressingBounce(Actor const& a) { return a.isPlayer() && a.isSneaking(); }

} // namespace

bool damagesTool(std::string const& itemId, ItemStack const& stack) {
    if (itemId.empty() || itemId == kMazebreaker) return false;
    // ItemStack#isDamageableItem: a max damage and not unbreakable
    return !stack.isNull() && stack.isDamageableItem();
}

void applyToolPenalty(Player& player, ItemStack const& held) {
    // ItemStack#hurtAndBreak(16, player, MAINHAND): unbreaking per point, nothing in creative
    if (player.isCreative()) return;
    auto& item = ia::mutableItem(held);
    if (item.isNull()) return;
    item.hurtAndBreak(kMazestonePenalty, &player);
    player.sendInventory(false);
}

double bounceSpeed(double fallDy, bool living) { return fallDy < 0.0 ? -fallDy * (living ? 1.0 : 0.8) : 0.0; }

void install() {
    // ToolEvents#damageNonMazebreakerToolsMore (BlockEvent.BreakEvent)
    ia::onDestroy(ia::Match::tag(kAcceleratedTag), [](ia::DestroyContext const& c) {
        if (damagesTool(c.itemId, c.item)) applyToolPenalty(c.player, c.item);
        return ia::Result::Pass;
    });

    // SlimeBlock#fallOn: causeFallDamage(fallDistance, 0) unless the entity suppresses the bounce
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        if (d.type != "minecraft:fall") return gf::combat::Verdict::Pass;
        if (suppressingBounce(d.victim) || !onMazeSlime(d.victim)) return gf::combat::Verdict::Pass;
        return gf::combat::Verdict::Cancel;
    });

    // the players' vertical speed, every tick
    fe::onLevelTick([] {
        auto level = ll::service::getLevel();
        if (!level) return;
        level->forEachPlayer(std::function<bool(Player&)>([](Player& p) {
            auto& f = falls()[p.getOrCreateUniqueID().rawID];
            auto pos = p.getFeetPos();
            if (f.known) {
                f.dx = pos.x - f.lastX;
                f.dy = pos.y - f.lastY;
                f.dz = pos.z - f.lastZ;
            }
            f.lastX = pos.x;
            f.lastY = pos.y;
            f.lastZ = pos.z;
            f.known = true;
            return true;
        }));
        if (falls().size() > 512) falls().clear();
    });

    // SlimeBlock#updateEntityAfterFallOn (bounceUp) and #stepOn
    gf::blockevents::onSteppedOn(kMazeSlimeBlock, [](BlockSource&, BlockPos const&, Actor& entity) {
        if (suppressingBounce(entity)) return;
        if (entity.isPlayer()) {
            auto it = falls().find(entity.getOrCreateUniqueID().rawID);
            if (it == falls().end() || it->second.dy > -0.1) return;
            auto& player = static_cast<Player&>(entity);
            // the horizontal speed the player had (client-side movement: the server's delta is stale)
            auto const& f = it->second;
            gf::movement::applyMotion(player, Vec3{static_cast<float>(f.dx), static_cast<float>(bounceSpeed(f.dy, true)), static_cast<float>(f.dz)});
            it->second.dy = 0.0;
            return;
        }
        auto   v  = entity.getPosDelta();
        double d0 = std::fabs(v.y);
        if (d0 < 0.1) {
            double d1 = 0.4 + d0 * 0.2;
            fe::setMotion(entity, Vec3{static_cast<float>(v.x * d1), v.y, static_cast<float>(v.z * d1)});
        }
    });

    gf::game::db::onWorldClosed([] { falls().clear(); });
}

} // namespace gf::feature::labyrinth::blocks

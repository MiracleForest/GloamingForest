// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/RopeBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): the rope's movement (dx 585, rule R12): the tightrope (RopeBlock#getCollisionShape:
// a horizontal strand holds an entity above it that is not descending; a sneaking player drops
// through; from below it is no obstacle) and scaffolding climbing (RopeBlock#isScaffolding: a
// sneaking player in a vertical rope slides down instead of holding on as on a ladder).

#include <cmath>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/gametest/ScriptNavigationResult.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/GetCollisionShapeInterface.h"
#include "mc/world/phys/AABB.h"

#include "feature/building/Building.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace b = gf::feature::building;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

SimulatedPlayer& sim(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

/// An entity for a collision query: its box and whether it descends (a sneaking player).
class Collider : public GetCollisionShapeInterface {
public:
    Collider(AABB box, bool descending) : mBox(box), mDescending(descending) {}
    float          getFeetAttachPosY() const override { return mBox.min.y; }
    bool           canDescendThroughBlock() const override { return mDescending; }
    float          getFallDistance() const override { return 0; }
    ActorType      getEntityTypeId() const override { return ActorType::Player; }
    bool           isWearingLeatherBoots() const override { return false; }
    bool           canStandOnPowderSnow() const override { return false; }
    AABB const&    getAABB() const override { return mBox; }
    bool           isWorldBuilder() const override { return false; }
    bool           isRiding() const override { return false; }
    bool           hasPassenger() const override { return false; }

private:
    AABB mBox;
    bool mDescending;
};

GF_REGISTER_SELFTEST("building.rope_movement", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(585, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // a tightrope at dy 3 from x 1 to 3, tied to stone at x 0 and 4
    auto west = at(p, 0, 3, 0), east = at(p, 4, 3, 0);
    ctx.require(ctx.setBlock(west, "minecraft:stone") && ctx.setBlock(east, "minecraft:stone"), "two posts");
    for (int x = 1; x <= 3; ++x)
        ctx.require(ctx.setBlock(at(p, x, 3, 0), "twilightforest:rope", {{"twilightforest:x", true}, {"twilightforest:y", false}, {"twilightforest:z", false}}), "a tightrope");
    // a vertical rope from x 6 dy 7 down to dy 2, hanging from stone at dy 8
    auto top = at(p, 6, 8, 0);
    ctx.require(ctx.setBlock(top, "minecraft:stone"), "a beam");
    for (int y = 2; y <= 7; ++y) ctx.require(ctx.setBlock(at(p, 6, y, 0), "twilightforest:rope"), "a hanging rope");
    co_await ctx.ticks(3);
    ctx.require(ctx.blockAt(at(p, 2, 3, 0)) == "twilightforest:rope" && ctx.blockAt(at(p, 6, 2, 0)) == "twilightforest:rope", "the ropes are supported");

    // the collision the game asks for an entity's movement
    {
        auto* region = ctx.region();
        ctx.require(region != nullptr, "a region");
        BlockPos     mid = gf::game::blockPos(at(p, 2, 3, 0));
        Block const& rope = region->getBlock(mid);
        auto         query = [&](float bottom, bool descending) {
            AABB     box{Vec3{mid.x + 0.2f, bottom, mid.z + 0.2f}, Vec3{mid.x + 0.8f, bottom + 1.8f, mid.z + 0.8f}};
            Collider c(box, descending);
            std::vector<AABB> boxes;
            rope.getBlockType().addCollisionShapes(rope, *region, mid, nullptr, boxes, c);
            return boxes;
        };
        float topY  = mid.y + 9.5f / 16.f;
        auto  above = query(topY, false);
        ctx.check(above.size() == 1 && std::abs(above[0].max.y - topY) < 1e-4f && std::abs(above[0].min.y - (mid.y + 6.5f / 16.f)) < 1e-4f,
                  "a horizontal strand holds an entity on top of it (" + std::to_string(above.size()) + " box)");
        ctx.check(query(topY, true).empty(), "a descending (sneaking) entity on top drops through");
        ctx.check(query(mid.y - 1.0f, false).empty(), "an entity below the strand is not blocked by it");
        ctx.check(query(mid.y + 0.45f, false).empty(), "nor an entity level with it");
        std::vector<AABB> none;
        rope.getBlockType().addCollisionShapes(rope, *region, mid, nullptr, none, {});
        ctx.check(none.empty(), "an empty collision context collides with nothing (Java CollisionContext.empty())");
        ctx.check(b::ropeCollision(rope, mid, true, topY + 0.01f, false).size() == 1 && b::ropeCollision(rope, mid, true, topY - 0.1f, false).empty(),
                  "ropeCollision: above it collides, below the top it does not");
    }

    // a player walking the tightrope stands on it; sneaking drops through
    {
        auto& sp = sim(ctx);
        sp.setSneaking(false);
        sp.teleport(Vec3{p.x + 2.5f, p.y + 4.2f, p.z + 0.5f}, sp.getDimensionId());
    }
    co_await ctx.ticks(20);
    float strandTop = p.y + 3 + 9.5f / 16.f;
    {
        float y = sim(ctx).getAABB().min.y;
        ctx.check(std::abs(y - strandTop) < 0.05f, "the player stands on the tightrope (feet " + std::to_string(y) + ", strand top " + std::to_string(strandTop) + ")");
        sim(ctx).setSneaking(true);
    }
    co_await ctx.ticks(20);
    {
        float y = sim(ctx).getAABB().min.y;
        ctx.check(y < strandTop - 1.0f, "sneaking, the player drops through the tightrope (feet " + std::to_string(y) + ")");
        sim(ctx).setSneaking(false);
    }

    // climbing: inside the hanging rope, a sneaking player slides down (scaffolding), where a ladder
    // would hold them
    {
        auto& sp = sim(ctx);
        sp.teleport(Vec3{p.x + 6.5f, p.y + 6.0f, p.z + 0.5f}, sp.getDimensionId());
        sp.setSneaking(true);
    }
    co_await ctx.ticks(2);
    float startY = sim(ctx).getAABB().min.y;
    co_await ctx.ticks(10);
    {
        float y = sim(ctx).getAABB().min.y;
        ctx.check(y < startY - 0.5f, "a sneaking player in the rope slides down (" + std::to_string(startY) + " -> " + std::to_string(y) + ")");
        sim(ctx).setSneaking(false);
        sim(ctx).teleport(Vec3{p.x + 6.5f, p.y + 6.0f, p.z + 0.5f}, sim(ctx).getDimensionId());
    }
    co_await ctx.ticks(2);
    startY = sim(ctx).getAABB().min.y;
    co_await ctx.ticks(10);
    // not sneaking, Java slides down too (at most 0.15 a tick, as on a ladder)
    ctx.info("not sneaking in the rope: " + std::to_string(startY) + " -> " + std::to_string(sim(ctx).getAABB().min.y));

    sim(ctx).teleport(Vec3{p.x + 0.5f, float(p.y + 1), p.z + 3.5f}, sim(ctx).getDimensionId());
    for (int y = 2; y <= 7; ++y) ctx.setBlock(at(p, 6, y, 0), "minecraft:air");
    for (int x = 1; x <= 3; ++x) ctx.setBlock(at(p, x, 3, 0), "minecraft:air");
    ctx.setBlock(top, "minecraft:air");
    ctx.setBlock(west, "minecraft:air");
    ctx.setBlock(east, "minecraft:air");
});

// KnightmetalBlock#getBlockPathType: DAMAGE_OTHER, mobs path around it (dx 592). The simulated
// player's navigation walks a straight 7-block lane whose middle ground block is the block under
// test, with free ground beside it; magma (a vanilla damaging block) is the control: when the
// navigation steps around magma it must step around knightmetal too.
GF_REGISTER_SELFTEST("building.knightmetal_path", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(592, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto walk = [&](std::string const& id) -> ll::coro::CoroTask<int> {
        auto mid = at(p, 3, 0, 2);
        // a clear stone lane (dz 1..3) one block up (the test's blocks go back to air afterwards)
        for (int x = 0; x <= 6; ++x)
            for (int z = 1; z <= 3; ++z) {
                ctx.setBlock(at(p, x, 0, z), "minecraft:stone");
                ctx.setBlock(at(p, x, 1, z), "minecraft:air");
                ctx.setBlock(at(p, x, 2, z), "minecraft:air");
            }
        ctx.setBlock(mid, id);
        auto& sp = sim(ctx);
        sp.teleport(Vec3{p.x + 0.5f, float(p.y + 1), p.z + 2.5f}, sp.getDimensionId());
        co_await ctx.ticks(5);
        sim(ctx).simulateNavigateToLocation(Vec3{p.x + 6.5f, float(p.y + 1), p.z + 2.5f}, 1.0f);
        int on = 0;
        for (int t = 0; t < 120; ++t) {
            co_await ctx.ticks(1);
            auto feet = sim(ctx).getFeetBlockPos();
            if (feet.x == mid.x && feet.z == mid.z) ++on;
        }
        sim(ctx).simulateStopMoving();
        auto end = sim(ctx).getPosition();
        ctx.info(id + ": " + std::to_string(on) + " tick(s) over it, ended at x+" + std::to_string(end.x - p.x));
        ctx.setBlock(mid, "minecraft:stone");
        co_return on;
    };
    int magma       = co_await walk("minecraft:magma");
    int knightmetal = co_await walk("twilightforest:knightmetal_block");
    if (magma == 0) ctx.check(knightmetal == 0, "the navigation steps around knightmetal as around magma (" + std::to_string(knightmetal) + " tick(s) over it)");
    else ctx.info("the simulated player's navigation walks over magma too: the knightmetal path type is shown only by its CausesDamage property");
    sim(ctx).teleport(Vec3{p.x + 0.5f, float(p.y), p.z + 4.5f}, sim(ctx).getDimensionId());
});

} // namespace

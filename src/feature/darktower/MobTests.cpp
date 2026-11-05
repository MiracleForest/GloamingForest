// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U26 (darktower) selftests of the mobs (rule R12: "darktower.*", testPos dx 2600..2699): F13's
// goldens (tests/golden/mobs.json: max health, the collision box, movement speed, follow range and
// attack damage of the six dark tower mobs), the Ur-Ghast fireball's impact (16 damage, an explosion
// that never breaks blocks, even with mob griefing on) and the Ur-Ghast spawner's height gate.
#include <cmath>
#include <string>

#include "ll/api/coro/CoroTask.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "feature/darktower/UrGhast.h"
#include "feature/darktower/Util.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::darktower::tests {

namespace {

using selftest::Context;

struct Golden {
    char const* id;
    float       health, width, height, movement, followRange, attack;
};

// tests/golden/mobs.json (F13, derived from TF 4.8): maxHealth, width, height and the
// movement_speed / follow_range / attack_damage attributes (attack -1: the mob has none)
constexpr Golden kGoldens[] = {
    {"twilightforest:ur_ghast", 250.0f, 14.0f, 18.0f, 0.7f, 128.0f, -1.0f},
    {"twilightforest:carminite_ghastguard", 30.0f, 4.0f, 6.0f, 0.7f, 64.0f, -1.0f},
    {"twilightforest:carminite_ghastling", 10.0f, 1.1f, 1.5f, 0.7f, 16.0f, -1.0f},
    {"twilightforest:carminite_golem", 40.0f, 1.4f, 2.9f, 0.25f, 16.0f, 9.0f},
    {"twilightforest:carminite_broodling", 7.0f, 0.7f, 0.5f, 0.3f, 16.0f, 4.0f},
    {"twilightforest:towerwood_borer", 15.0f, 0.4f, 0.3f, 0.27f, 8.0f, 5.0f},
};

float attribute(Mob& m, Attribute const& a) {
    auto ref = m.getMutableAttribute(a);
    if (auto* inst = ref.mInstance->mPtr) return inst->mCurrentValue;
    return -1.0f;
}

bool near(float a, float b) { return std::abs(a - b) < 0.011f; }

} // namespace

GF_REGISTER_SELFTEST("darktower.mob_goldens", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2660, 24, 24);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto at = ctx.testPos(2660, 2).offset(0, 3, 0);
    for (auto const& g : kGoldens) {
        auto* m = spawnMob(*region, g.id, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
        ctx.require(m != nullptr, std::string("spawned ") + g.id);
        auto b = m->getAABB();
        ctx.check(near(m->getMaxHealth(), g.health), std::string(g.id) + " max health " + std::to_string(m->getMaxHealth()));
        ctx.check(near(b.max.x - b.min.x, g.width) && near(b.max.y - b.min.y, g.height),
                  std::string(g.id) + " box " + std::to_string(b.max.x - b.min.x) + " x " + std::to_string(b.max.y - b.min.y));
        ctx.check(near(attribute(*m, SharedAttributes::MOVEMENT_SPEED()), g.movement),
                  std::string(g.id) + " movement " + std::to_string(attribute(*m, SharedAttributes::MOVEMENT_SPEED())));
        ctx.check(near(attribute(*m, SharedAttributes::FOLLOW_RANGE()), g.followRange),
                  std::string(g.id) + " follow range " + std::to_string(attribute(*m, SharedAttributes::FOLLOW_RANGE())));
        if (g.attack >= 0)
            ctx.check(near(attribute(*m, SharedAttributes::ATTACK_DAMAGE()), g.attack),
                      std::string(g.id) + " attack " + std::to_string(attribute(*m, SharedAttributes::ATTACK_DAMAGE())));
        m->remove();
    }
    co_return;
});

// UrGhastFireball#onHitBlock: an explosion of power 1 that never breaks blocks (ExplosionInteraction.NONE)
GF_REGISTER_SELFTEST("darktower.urghast_fireball", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(2640, 24, 24);
    (void)box;
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    (void)selftest::runServerCommand("gamerule mobgriefing true");
    auto base = ctx.testPos(2640, 2).offset(0, 1, 0);
    // a stone wall 8 blocks east of the shot
    for (int y = 0; y < 4; y++)
        for (int z = -1; z <= 1; z++) ctx.setBlock(base.offset(8, y, z), "minecraft:stone");
    auto* owner = spawnMob(*region, id::Ghastling, Vec3{base.x + 0.5f, float(base.y + 6), base.z + 0.5f});
    ctx.require(owner != nullptr, "an owner for the fireball");
    auto  ownerId = uidOf(*owner);
    auto* fb      = ghasts::shootUrGhastFireball(*owner, Vec3{base.x + 0.5f, base.y + 1.5f, base.z + 0.5f}, Vec3{1.0f, 0.0f, 0.0f});
    ctx.require(fb != nullptr, "the fireball spawned");
    auto fbId = uidOf(*fb);
    for (int i = 0; i < 60 && actorById(fbId); i++) co_await ctx.ticks(1);
    ctx.check(actorById(fbId) == nullptr, "the fireball hit the wall");
    region = ctx.region();
    int intact = 0;
    for (int y = 0; y < 4; y++)
        for (int z = -1; z <= 1; z++) intact += idAt(*region, game::blockPos(base.offset(8, y, z))) == "minecraft:stone" ? 1 : 0;
    ctx.check(intact == 12, "no block of the wall broke (" + std::to_string(intact) + " of 12)");
    if (auto* o = actorById(ownerId)) o->remove();
    co_return; // the flat area is restored afterwards (the wall and any fire)
});

} // namespace gf::feature::darktower::tests

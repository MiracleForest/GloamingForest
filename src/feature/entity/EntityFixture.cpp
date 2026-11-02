// The fixture boss of F8's selftests (see EntityFixture.h).

#include "feature/entity/EntityFixture.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"

#include "feature/boss/Boss.h"
#include "feature/boss/BossInternal.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"

namespace gf::feature::entity::tests {

namespace ge = gf::entity;

Probe& probe() {
    static Probe p;
    return p;
}

namespace {
int gScopes = 0;
} // namespace

FixtureScope::FixtureScope() {
    if (gScopes++ > 0) return;
    ge::registerType(
        "gf_test:boss",
        {
            .onSpawn =
                [](Actor& a) {
                    probe().spawns++;
                    // a boss named "gf_parts": three linked parts (client e2e screenshots)
                    if (a.getNameTag() == "gf_parts")
                        if (auto* m = asMob(a)) {
                            ge::addPart(*m, "left", {0.8f, 0.8f}, [](Mob&) { return Vec3{2.0f, 0.5f, 0.0f}; });
                            ge::addPart(*m, "right", {0.8f, 0.8f}, [](Mob&) { return Vec3{-2.0f, 0.5f, 0.0f}; });
                            ge::addPart(*m, "head", {1.0f, 1.0f}, [](Mob&) { return Vec3{0.0f, 2.5f, 0.0f}; });
                        }
                },
            .tick    = [](Actor&) { probe().ticks++; },
            .aiStep  = [](Actor&) { probe().aiSteps++; },
            .die =
                [](Actor&, std::string const& type, Actor*) {
                    probe().dies++;
                    probe().lastDieType = type;
                },
            .save = [](Actor& a, ge::Json& out) {
                // nothing to save once the probe says so (a stale save must not come back)
                if (ge::data(a, "probe_skip_save").is_boolean()) return;
                out["probe"] = "saved";
            },
            .load = [](Actor&, ge::Json const& in) {
                if (in.value("probe", "") == "saved") probe().loads++;
            },
            .interact =
                [](Actor&, Player&) {
                    probe().interacts++;
                    return true;
                },
        }
    );
    gf::boss::registerBoss({
        .entityId        = "gf_test:boss",
        .homeStructure   = "gf_test:fixture",
        .lootTable       = "gf_test:entities/boss",
        .deathChestBlock = "minecraft:chest",
        .bar             = {.rgb = kFixtureRgb, .darken = true, .fog = false},
        .deathTicks      = 20,
    });
    gf::feature::boss::markInclusiveForTest("gf_test:boss");
}

FixtureScope::~FixtureScope() {
    if (--gScopes > 0) return;
    gf::feature::boss::unmarkInclusiveForTest("gf_test:boss");
    gf::feature::boss::unregisterBossForTest("gf_test:boss");
    unregisterTypeForTest("gf_test:boss");
}

} // namespace gf::feature::entity::tests

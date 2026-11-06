// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/RegistrationEvents.java (registerSpawnPlacements)
//   src/main/java/twilightforest/entity/monster/HostileWolf.java (checkWolfSpawnRules)
//   src/main/java/twilightforest/entity/monster/WinterWolf.java (canSpawnHere)
//   src/main/java/twilightforest/entity/monster/HedgeSpider.java (canSpawn)
//   src/main/java/twilightforest/entity/monster/SwarmSpider.java (getCanSpawnHere)
//   src/main/java/twilightforest/entity/monster/SkeletonDruid.java (checkDruidSpawnRules)
//   src/main/java/twilightforest/entity/monster/Wraith.java (checkMonsterSpawnRules)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U17 (hostiles): the unit's module: the wolves, spiders, skeleton druid, redcaps, wraith and
// beetles, their spawn placements and the armour of the redcap sapper and the pinch beetle.

#include <algorithm>
#include <string>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/BrightnessPair.h"

#include "feature/combat/Combat.h"
#include "feature/ext/worldgen/Spawning.h"
#include "feature/hostiles/Common.h"
#include "feature/hostiles/Hostiles.h"
#include "feature/spawn/Spawn.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "mod/Module.h"

namespace gf::feature::hostiles {

namespace {

using gf::spawn::PlacementRule;
using gf::spawn::SpawnReason;
using Rnd = gf::core::jrand::RandomSource;

/// HostileWolf / HedgeSpider / SwarmSpider#isValidLightLevel: bright light only in hedge mazes.
bool hedgeOrDark(BlockSource& region, BlockPos const& pos, Rnd& random) { return hedgeMazeChunk(region, pos) || darkEnough(region, pos, random); }

/// SkeletonDruid#isValidLightLevel ([VanillaCopy] of Monster's with rand(12) for rand(8)).
bool druidLight(BlockSource& region, BlockPos const& pos, Rnd& random) {
    auto pair = region.getBrightnessPair(pos);
    if (static_cast<int>(pair.sky->mValue) > random.nextInt(32)) return false;
    return maxLocalRawBrightness(region, pos) <= random.nextInt(12); // TF - rand(8) -> rand(12)
}

bool notPeaceful() { return difficulty() != 0; }

void registerMonster(char const* type, double w, double h, gf::spawn::PredicateFn predicate, int cluster = 4,
                     gf::spawn::ext::WalkTarget walk = gf::spawn::ext::WalkTarget::Monster) {
    gf::spawn::registerPlacement(type, PlacementRule::OnGround, std::move(predicate));
    gf::spawn::ext::registerSpawnBehaviour(type, {.width = w, .height = h, .maxClusterSize = cluster, .walkTarget = walk});
}

} // namespace

void installSpawns() {
    auto monster = [](BlockSource& r, std::string const&, BlockPos const& p, SpawnReason reason, Rnd& random) {
        return checkMonsterSpawnRules(r, p, reason, random);
    };
    // checkMobSpawnRules (a valid spawn block below, or a spawner) is the ON_GROUND placement's own check
    registerMonster(id::HostileWolf, 0.6, 0.85, [](BlockSource& r, std::string const&, BlockPos const& p, SpawnReason, Rnd& random) {
        return notPeaceful() && hedgeOrDark(r, p, random);
    });
    registerMonster(id::MistWolf, 1.4, 1.9, monster);
    // WinterWolf#canSpawnHere, Java's precedence kept: (not peaceful && snowy forest) || dark enough
    registerMonster(id::WinterWolf, 1.4, 1.9, [](BlockSource& r, std::string const&, BlockPos const& p, SpawnReason, Rnd& random) {
        return (notPeaceful() && gf::worldgen::biomeAt(p) == "twilightforest:snowy_forest") || darkEnough(r, p, random);
    });
    registerMonster(id::KingSpider, 1.6, 1.6, monster);
    // HedgeSpider#canSpawn: no checkMobSpawnRules
    registerMonster(id::HedgeSpider, 1.4, 0.9, [](BlockSource& r, std::string const&, BlockPos const& p, SpawnReason, Rnd& random) {
        return notPeaceful() && hedgeOrDark(r, p, random);
    });
    registerMonster(
        id::SwarmSpider, 0.8, 0.4,
        [](BlockSource& r, std::string const&, BlockPos const& p, SpawnReason, Rnd& random) { return notPeaceful() && hedgeOrDark(r, p, random); },
        6 // SwarmSpider#getMaxSpawnClusterSize
    );
    registerMonster(id::Druid, 0.6, 1.99, [](BlockSource& r, std::string const&, BlockPos const& p, SpawnReason, Rnd& random) {
        return notPeaceful() && druidLight(r, p, random);
    });
    registerMonster(id::Redcap, 0.9, 1.4, monster);
    registerMonster(id::RedcapSapper, 0.9, 1.4, monster);
    // Wraith#checkMonsterSpawnRules (a FlyingMob: walk target 0)
    registerMonster(
        id::Wraith, 0.6, 2.1, [](BlockSource& r, std::string const&, BlockPos const& p, SpawnReason, Rnd& random) { return notPeaceful() && darkEnough(r, p, random); }, 4,
        gf::spawn::ext::WalkTarget::Zero
    );
    registerMonster(id::FireBeetle, 1.1, 0.5, monster);
    registerMonster(id::SlimeBeetle, 0.9, 0.5, monster);
    registerMonster(id::PinchBeetle, 1.2, 0.5, monster);
}

class HostilesModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        // the dispatches of entity / combat / loot have no removal: register once per process (the
        // client enables the mod once)
        registered = true;
        wolves::install();
        spiders::install();
        druid::install();
        redcaps::install();
        wraith::install();
        beetles::install();
        installSpawns();
        // Attributes.ARMOR 2 of the redcap sapper and the pinch beetle (CombatRules#getDamageAfterAbsorb,
        // no toughness): Bedrock mobs have no armour attribute without worn armour
        gf::combat::onIncoming(gf::combat::priority::Low, [](gf::combat::DamageInfo& d) {
            auto type = typeOf(d.victim);
            if (type != id::RedcapSapper && type != id::PinchBeetle) return gf::combat::Verdict::Pass;
            if (gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_armor", d.type)) return gf::combat::Verdict::Pass;
            float armor = 2.0F;
            float f1    = std::clamp(armor - d.amount / 2.0F, armor * 0.2F, 20.0F);
            d.amount *= 1.0F - f1 / 25.0F;
            return gf::combat::Verdict::Pass;
        });
    }
    void uninstall() {}
};

} // namespace gf::feature::hostiles

GF_REGISTER_MODULE(gf::feature::hostiles::HostilesModule);

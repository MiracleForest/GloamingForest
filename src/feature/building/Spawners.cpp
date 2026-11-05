// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/entity/spawner/FinalBossSpawnerBlockEntity.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): the final boss spawner. Its boss (the plateau boss) never spawns
// ("no spawning for you": spawnMyBoss returns false); while a player is in range it only shows the
// annihilate particle (getSpawnerParticle). The eight other boss spawners are registered by their
// boss units (rule R20). U30's spec registers the same spec for the plateau boss; the boss kit
// keeps one spec per block id, and the two are identical.

#include "feature/boss/Boss.h"
#include "mod/Module.h"

namespace gf::feature::building {

namespace {

struct SpawnersModule {
    void install() {
        boss::registerSpawner(boss::SpawnerSpec{
            .blockId  = "twilightforest:final_boss_boss_spawner",
            .entityId = "twilightforest:plateau_boss",
            .range    = 9,
            .particle = "twilightforest:annihilate",
            // no spawnFn: the kit's SpawnerKind::Never (the final boss spawner) never spawns
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::building

GF_REGISTER_MODULE(gf::feature::building::SpawnersModule);

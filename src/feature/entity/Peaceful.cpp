// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/HostileWolf.java
//   src/main/java/twilightforest/entity/monster/LoyalZombie.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "entity" (F8): Mob#checkDespawn on Peaceful for TF's monsters. Java removes every
// Monster (and the types that answer shouldDespawnInPeaceful true: HostileWolf, the Ghast
// subclasses) on Peaceful; Bedrock keeps custom monsters. The bosses go back to their spawners
// through the boss kit (BaseTFBoss#checkDespawn), LoyalZombie (a TamableAnimal) stays.

#include <string>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/level/Level.h"

#include "feature/ext/entity/World.h"
#include "feature/entity/EntityInternal.h"

namespace gf::feature::entity {

namespace {

/// The non-boss TF types whose shouldDespawnInPeaceful is true (F13's goldens, as the U17 spawn
/// sweep table src/core/hostiles/MobSweep.h lists them).
std::unordered_set<std::string>& peacefulTypes() {
    static std::unordered_set<std::string> set{
        "twilightforest:adherent", "twilightforest:armored_giant", "twilightforest:blockchain_goblin",
        "twilightforest:carminite_broodling", "twilightforest:carminite_ghastguard", "twilightforest:carminite_ghastling",
        "twilightforest:carminite_golem", "twilightforest:death_tome", "twilightforest:fire_beetle",
        "twilightforest:giant_miner", "twilightforest:harbinger_cube", "twilightforest:hedge_spider",
        "twilightforest:helmet_crab", "twilightforest:hostile_wolf", "twilightforest:ice_crystal",
        "twilightforest:king_spider", "twilightforest:kobold", "twilightforest:lich_minion",
        "twilightforest:lower_goblin_knight", "twilightforest:maze_slime", "twilightforest:minotaur",
        "twilightforest:mist_wolf", "twilightforest:mosquito_swarm", "twilightforest:pinch_beetle",
        "twilightforest:redcap", "twilightforest:redcap_sapper", "twilightforest:rising_zombie",
        "twilightforest:roving_cube", "twilightforest:skeleton_druid", "twilightforest:slime_beetle",
        "twilightforest:snow_guardian", "twilightforest:stable_ice_core", "twilightforest:swarm_spider",
        "twilightforest:towerwood_borer", "twilightforest:troll", "twilightforest:unstable_ice_core",
        "twilightforest:upper_goblin_knight", "twilightforest:winter_wolf", "twilightforest:wraith",
        "twilightforest:yeti",
    };
    return set;
}

bool peacefulNow() {
    auto level = ll::service::getLevel();
    return level && level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful;
}

} // namespace

bool peacefulDespawnType(std::string const& type) { return peacefulTypes().contains(type); }

bool despawnIfPeaceful(Actor& actor, std::string const& type) {
    if (!peacefulTypes().contains(type) || !actor.isAlive() || !peacefulNow()) return false;
    removeLater(uidOf(actor));
    return true;
}

} // namespace gf::feature::entity

namespace gf::entity::ext {

void setDespawnInPeaceful(std::string const& entityId, bool despawn) {
    auto& set     = gf::feature::entity::peacefulTypes();
    bool  changed = despawn ? set.insert(entityId).second : set.erase(entityId) > 0;
    if (changed) gf::feature::entity::invalidateInterest();
}

bool despawnsInPeaceful(std::string const& entityId) { return gf::feature::entity::peacefulDespawnType(entityId); }

} // namespace gf::entity::ext

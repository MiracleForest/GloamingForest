// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/monster/HostileWolf.java
//   src/main/java/gloamingforest/entity/monster/LoyalZombie.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "entity" (F8): Mob#checkDespawn on Peaceful for upstream's monsters. Java removes every
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

/// The non-boss upstream types whose shouldDespawnInPeaceful is true (F13's goldens, as the U17 spawn
/// sweep table src/core/hostiles/MobSweep.h lists them).
std::unordered_set<std::string>& peacefulTypes() {
    static std::unordered_set<std::string> set{
        "gloamingforest:adherent", "gloamingforest:armored_giant", "gloamingforest:blockchain_goblin",
        "gloamingforest:carminite_broodling", "gloamingforest:carminite_ghastguard", "gloamingforest:carminite_ghastling",
        "gloamingforest:carminite_golem", "gloamingforest:death_tome", "gloamingforest:fire_beetle",
        "gloamingforest:giant_miner", "gloamingforest:harbinger_cube", "gloamingforest:hedge_spider",
        "gloamingforest:helmet_crab", "gloamingforest:hostile_wolf", "gloamingforest:ice_crystal",
        "gloamingforest:king_spider", "gloamingforest:kobold", "gloamingforest:lich_minion",
        "gloamingforest:lower_goblin_knight", "gloamingforest:maze_slime", "gloamingforest:minotaur",
        "gloamingforest:mist_wolf", "gloamingforest:mosquito_swarm", "gloamingforest:pinch_beetle",
        "gloamingforest:redcap", "gloamingforest:redcap_sapper", "gloamingforest:rising_zombie",
        "gloamingforest:roving_cube", "gloamingforest:skeleton_druid", "gloamingforest:slime_beetle",
        "gloamingforest:snow_guardian", "gloamingforest:stable_ice_core", "gloamingforest:swarm_spider",
        "gloamingforest:towerwood_borer", "gloamingforest:troll", "gloamingforest:unstable_ice_core",
        "gloamingforest:upper_goblin_knight", "gloamingforest:winter_wolf", "gloamingforest:wraith",
        "gloamingforest:yeti",
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

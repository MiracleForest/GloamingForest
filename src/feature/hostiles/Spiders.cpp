// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/KingSpider.java
//   src/main/java/twilightforest/entity/monster/HedgeSpider.java
//   src/main/java/twilightforest/entity/monster/SwarmSpider.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Also translated: vanilla Minecraft 1.21.1's Spider#finalizeSpawn (the 1 in 100 skeleton jockey,
// SpiderEffectsGroupData on Hard) and SpiderAttackGoal / SpiderTargetGoal's light rule.
//
// The spiders' vanilla goals are the BP's behaviours. The king spider keeps vanilla's light rule:
// it takes no new target while the light level dependent magic value is 0.5 or more (the BP group
// gf:hostile, switched here) and drops its target 1 time in 100 per tick there; the hedge and swarm
// spiders ignore light. Native: the jockeys (a druid always rides the king spider's stack, a baby
// druid rides 1 swarm spider in 200), the Hard effects and the swarm spider's 1-in-4 bite.

#include <unordered_map>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockSource.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/hostiles/Common.h"
#include "feature/hostiles/Hostiles.h"
#include "mod/Log.h"

namespace gf::feature::hostiles::spiders {

namespace fe = gf::feature::entity;

namespace {

Actor* topRider(Actor& vehicle) {
    Actor* last = &vehicle;
    for (int i = 0; i < 16; ++i) {
        Actor* next = last->getFirstPassenger();
        if (!next) break;
        last = next;
    }
    return last;
}

/// Spider#finalizeSpawn, then the TF subclass's additions.
void finalizeSpawn(Mob& spider) {
    auto& region = spider.getDimensionBlockSource();
    auto  pos    = spider.getFeetPos();
    float yaw    = spider.getRotation().y;
    auto& r      = levelRandom();
    // vanilla: a skeleton jockey 1 time in 100
    if (r.nextInt(100) == 0) {
        if (auto* skeleton = spawnMob(region, "minecraft:skeleton", pos, gf::spawn::SpawnReason::Jockey, yaw)) skeleton->startRiding(spider, true);
    }
    // vanilla: SpiderEffectsGroupData#setRandomEffect on Hard
    if (difficulty() == 3 && r.nextFloat() < 0.1F * specialMultiplier()) {
        int      i      = r.nextInt(5);
        unsigned effect = i <= 1 ? Effect::Speed : i <= 2 ? Effect::Strength : i <= 3 ? Effect::Regeneration : Effect::Invisibility;
        addEffect(spider, effect, -1, 0);
    }
    auto type = typeOf(spider);
    if (type == id::KingSpider) {
        // KingSpider#finalizeSpawn: a druid always rides the spider, or whatever rides it
        if (auto* druid = spawnMob(region, id::Druid, pos, gf::spawn::SpawnReason::Jockey, yaw)) {
            Actor* top = topRider(spider);
            if (!druid->startRiding(*top, true) && top != &spider) druid->startRiding(spider, true);
            log().info("hostiles: king spider spawned with a skeleton druid rider");
        }
    } else if (type == id::SwarmSpider) {
        // SwarmSpider#summonJockey: with a rider already, or 1 time in 200, a baby druid
        if (spider.getFirstPassenger() != nullptr || r.nextInt(200) == 0) {
            if (auto* druid = spawnMob(region, id::Druid, pos, gf::spawn::SpawnReason::Jockey, yaw)) {
                druid::markBaby(*druid);
                if (spider.getFirstPassenger() != nullptr) spider.removeAllPassengers(false, true); // ejectPassengers
                druid->startRiding(spider, true);
                log().info("hostiles: swarm spider spawned with a baby skeleton druid rider");
            }
        }
    }
}

void spiderTick(Actor& self) {
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    if (firstSpawn(self)) finalizeSpawn(*m);
    if (typeOf(self) != id::KingSpider) return;
    // SpiderTargetGoal: no new target in the light; SpiderAttackGoal#canContinueToUse: in the light
    // the spider forgets its target 1 time in 100
    bool bright = lightMagicValue(self) >= 0.5F;
    setGroup(self, "hostile", !bright);
    if (bright && targetOf(*m) && randomOf(self).nextInt(100) == 0) m->setTarget(nullptr);
}

} // namespace

void install() {
    for (char const* type : {id::KingSpider, id::HedgeSpider, id::SwarmSpider})
        ge::registerType(type, {.tick = spiderTick, .die = forgetActor});
    // SwarmSpider#doHurtTarget: only one melee attack in four lands
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        if (!isType(d.attacker, id::SwarmSpider) || (d.direct && d.direct != d.attacker) || d.type != "minecraft:mob_attack") return gf::combat::Verdict::Pass;
        return randomOf(*d.attacker).nextInt(4) == 0 ? gf::combat::Verdict::Pass : gf::combat::Verdict::Cancel;
    });
}

} // namespace gf::feature::hostiles::spiders

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/CarminiteGolem.java
//   src/main/java/twilightforest/entity/monster/TowerBroodling.java
//   src/main/java/twilightforest/entity/monster/TowerwoodBorer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Bedrock: the vanilla parts of their AI (melee attack, wander, look, hurt-by / player targets,
// the silverfish-like borer's float and stroll) are the BP's vanilla behaviour components; what
// TF adds is native here.

#include "feature/darktower/Mobs.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/darktower/Util.h"
#include "feature/entity/Entity.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"

namespace gf::feature::darktower::mobs {

namespace ge = gf::entity;

namespace {

// ---- carminite golem ---------------------------------------------------------------------------

std::unordered_map<std::int64_t, int>& golemTimers() {
    static std::unordered_map<std::int64_t, int> m;
    return m;
}

void golemTick(Actor& self) {
    auto& timer = golemTimers()[uidOf(self)];
    if (timer > 0) {
        --timer;
        ge::setProperty(self, "twilightforest:attack_timer", timer);
    }
    if (random().nextBoolean()) {
        auto  b = self.getAABB();
        auto  p = self.getFeetPos();
        float w = b.max.x - b.min.x, h = b.max.y - b.min.y;
        auto& r = random();
        redstoneDust(self.getDimensionBlockSource(), Vec3{float(p.x + (r.nextDouble() - 0.5) * w), float(p.y + r.nextDouble() * h - 0.25), float(p.z + (r.nextDouble() - 0.5) * w)});
    }
}

// ---- towerwood borer -----------------------------------------------------------------------------

struct Borer {
    int lookForFriends = 0; ///< SummonBorersGoal
};
std::unordered_map<std::int64_t, Borer>& borers() {
    static std::unordered_map<std::int64_t, Borer> m;
    return m;
}

/// TowerwoodBorer.SummonBorersGoal#tick (silverfish's wake-up copy over infested towerwood).
void summonTick(Actor& self, Borer& b) {
    if (b.lookForFriends <= 0) return;
    if (--b.lookForFriends > 0) return;
    auto& region = self.getDimensionBlockSource();
    auto  fp     = self.getFeetPos();
    BlockPos base{int(std::floor(fp.x)), int(std::floor(fp.y)), int(std::floor(fp.z))};
    auto& r = random();
    for (int i = 0; i <= 5 && i >= -5; i = (i <= 0 ? 1 : 0) - i) {
        for (int j = 0; j <= 10 && j >= -10; j = (j <= 0 ? 1 : 0) - j) {
            for (int k = 0; k <= 10 && k >= -10; k = (k <= 0 ? 1 : 0) - k) {
                BlockPos p{base.x + j, base.y + i, base.z + k};
                if (idAt(region, p) != id::InfestedTowerwood) continue;
                if (mobGriefing()) {
                    destroy(region, p, true);
                    // InfestedTowerwoodBlock#spawnAfterBreak: a borer pops out
                    spawnMob(region, id::Borer, Vec3{p.x + 0.5f, float(p.y), p.z + 0.5f});
                } else {
                    set(region, p, id::Towerwood);
                }
                if (r.nextBoolean()) return;
            }
        }
    }
}

/// TowerwoodBorer.HideInTowerwoodGoal (silverfish merge copy).
void hideTick(Mob& self) {
    if (actorById(self.mTargetId->rawID)) return;
    // Silverfish.SilverfishMergeWithStoneGoal#canUse: getNavigation().isDone() (no path in progress)
    if (auto nav = self.getEntityContext().tryGetComponent<NavigationComponent>(); nav && nav->mPath->get() != nullptr) return;
    if (!mobGriefing()) return;
    auto& r = random();
    if (r.nextInt(10) != 0) return; // reducedTickDelay(10)
    static constexpr core::darktower::Facing kDirs[] = {core::darktower::Facing::Down, core::darktower::Facing::Up, core::darktower::Facing::North,
                                                         core::darktower::Facing::South, core::darktower::Facing::West, core::darktower::Facing::East};
    auto  dir    = kDirs[r.nextInt(6)];
    auto  fp     = self.getFeetPos();
    BlockPos p   = offset(BlockPos{int(std::floor(fp.x)), int(std::floor(fp.y + 0.5)), int(std::floor(fp.z))}, dir);
    auto& region = self.getDimensionBlockSource();
    if (idAt(region, p) != id::Towerwood) return;
    if (r.nextInt(5) != 0) return; // doMerge chance
    set(region, p, id::InfestedTowerwood);
    particle(region, "minecraft:poof", self.getFeetPos(), {}, 20); // spawnAnim
    borers().erase(uidOf(self));
    self.remove();
}

void borerTick(Actor& self) {
    auto* m = asMob(self);
    if (!m) return;
    auto& b = borers()[uidOf(self)];
    summonTick(self, b);
    hideTick(*m);
    // the body follows the head every tick
    m->setYBodyRotation(self.getRotation().y);
}

} // namespace

void install() {
    ge::registerType(id::Golem, {.tick = golemTick, .die = [](Actor& a, std::string const&, Actor*) { golemTimers().erase(uidOf(a)); }});
    // CarminiteGolem#doHurtTarget: attack timer 10, entity event 4 (the swing sound), push up 0.4
    combat::onDealt(combat::priority::Normal, [](combat::DamageInfo const& d) {
        if (!d.attacker || typeOf(*d.attacker) != id::Golem || d.direct != d.attacker && d.direct) return;
        golemTimers()[uidOf(*d.attacker)] = 10;
        ge::setProperty(*d.attacker, "twilightforest:attack_timer", 10);
        soundAt(d.attacker->getDimensionBlockSource(), d.attacker->getFeetPos(), "entity.twilightforest.carminite_golem.attack", 1.0f, 1.0f);
        push(d.victim, Vec3{0.0f, 0.4f, 0.0f});
    });
    ge::registerType(id::Borer, {.tick = borerTick, .die = [](Actor& a, std::string const&, Actor*) { borers().erase(uidOf(a)); }});
    // SummonBorersGoal#notifyHurt: by an entity or #minecraft:always_triggers_silverfish
    combat::onDealt(combat::priority::Normal, [](combat::DamageInfo const& d) {
        if (typeOf(d.victim) != id::Borer) return;
        if (d.attacker || d.type == "minecraft:magic") {
            auto& b = borers()[uidOf(d.victim)];
            if (b.lookForFriends <= 0) b.lookForFriends = 20;
        }
    });
    ge::registerType(id::Broodling, {});
    combat::onIncoming(combat::priority::Normal, [](combat::DamageInfo& d) {
        // SwarmSpider#doHurtTarget: only one melee attack in four lands
        if (d.attacker && d.direct == d.attacker && typeOf(*d.attacker) == id::Broodling && random().nextInt(4) != 0) return combat::Verdict::Cancel;
        // CarminiteGolem: Attributes.ARMOR 2 (CombatRules#getDamageAfterAbsorb, no toughness)
        if (typeOf(d.victim) == id::Golem && !tags::has(tags::Registry::DamageType, "minecraft:bypasses_armor", d.type)) {
            float armor = 2.0f;
            float f1    = std::clamp(armor - d.amount / 2.0f, armor * 0.2f, 20.0f);
            d.amount *= 1.0f - f1 / 25.0f;
        }
        return combat::Verdict::Pass;
    });
    game::db::onWorldClosed([] { golemTimers().clear(); });
}

} // namespace gf::feature::darktower::mobs

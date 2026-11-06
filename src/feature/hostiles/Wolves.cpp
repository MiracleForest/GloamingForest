// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/HostileWolf.java
//   src/main/java/twilightforest/entity/monster/MistWolf.java
//   src/main/java/twilightforest/entity/monster/WinterWolf.java
//   src/main/java/twilightforest/client/renderer/entity/MistWolfRenderer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hostile, mist and winter wolves. Their vanilla goals (float, leap, melee, stroll, look, the
// hurt-by and nearest-attackable targets) are the BP's behaviours (assets/pack/bp/entities/hostiles);
// what TF adds runs here: the angry face and tail (entity properties), the target sound, the mist
// wolf's blinding bite and light-dependent translucency, the winter wolf's frost breath (F8's
// BreathAttackGoal) with its snow and sound.

#include "feature/hostiles/Hostiles.h"

#include <cmath>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/fx/Fx.h"
#include "feature/hostiles/Common.h"
#include "game/WorldDb.h"

namespace gf::feature::hostiles::wolves {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

namespace {

std::unordered_map<std::int64_t, std::int64_t>& lastTargets() {
    static std::unordered_map<std::int64_t, std::int64_t> m;
    return m;
}

std::string soundOf(std::string const& type, char const* event) {
    auto name = type.substr(type.find(':') + 1);
    return "entity.twilightforest." + name + "." + event;
}

/// WinterWolf#aiStep while breathing: 10 snow particles half a block ahead at y + 1.25 (client side
/// in TF; sent through gf::fx here) and the shoot sound.
void winterBreath(Mob& m) {
    auto&  r    = randomOf(m);
    Vec3   look = m.getViewVector(1.0f);
    auto   p    = m.getFeetPos();
    double dist = 0.5;
    Vec3   at{float(p.x + look.x * dist), float(p.y + 1.25 + look.y * dist), float(p.z + look.z * dist)};
    for (int i = 0; i < 10; i++) {
        double dx       = look.x;
        double dy       = look.y;
        double dz       = look.z;
        double spread   = 5.0 + r.nextDouble() * 2.5;
        double velocity = 3.0 + r.nextDouble() * 0.15;
        // spread flame
        dx += r.nextGaussian() * 0.0075 * spread;
        dy += r.nextGaussian() * 0.0075 * spread;
        dz += r.nextGaussian() * 0.0075 * spread;
        dx *= velocity;
        dy *= velocity;
        dz *= velocity;
        gf::fx::particle(m.getDimension(), "twilightforest:snow", at, Vec3{float(dx), float(dy), float(dz)});
    }
    playSound(m, "entity.twilightforest.winter_wolf.shoot", r.nextFloat() * 0.5F, r.nextFloat() * 0.5F);
}

void wolfTick(Actor& self) {
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    auto type = typeOf(self);
    if (type == id::WinterWolf && !nativeGoalsPresent(self)) {
        // WinterWolf#registerGoals: 2 BreathAttackGoal(5, 30, 0.1); its breath is Mob#doHurtTarget's
        // damage source: 2 mob_attack damage (F8's default breath attack)
        ge::addGoal(*m, 2, ge::goals::breath(*m, {.range = 5.0f, .durationTicks = 30, .chance = 0.1f, .damageType = "minecraft:mob_attack"}));
    }
    // the angry face (HostileWolf#getTexture: isAggressive) and the tail (getTailAngle: a target)
    Actor* target = targetOf(*m);
    setPropertyCached(self, "twilightforest:has_target", target != nullptr);
    setPropertyCached(self, "twilightforest:aggressive", target != nullptr);
    // HostileWolf#setTarget: a new target plays the target sound at volume 4
    auto& last = lastTargets()[uidOf(self)];
    auto  now  = target ? uidOf(*target) : 0;
    if (now != 0 && now != last) playSound(self, soundOf(type, "target"), 4.0F, voicePitch(self, type == id::HostileWolf ? 1.0F : 0.6F));
    last = now;
    if (type == id::MistWolf && self.mTickCount % 5 == 0) {
        // MistWolfRenderer's colour comes from the light at the wolf's block
        setPropertyCached(self, "twilightforest:brightness", maxLocalRawBrightness(self.getDimensionBlockSource(), blockPosOf(self)));
    }
    if (type == id::WinterWolf) {
        bool breathing = ge::ext::isBreathing(self);
        setPropertyCached(self, "twilightforest:breathing", breathing);
        // the breath goal holds MOVE / LOOK: melee and stroll (priorities 3, 6) wait
        setGroup(self, "vanilla_move", !breathing);
        if (breathing) winterBreath(*m);
    }
}

} // namespace

void install() {
    for (char const* type : {id::HostileWolf, id::MistWolf, id::WinterWolf}) {
        ge::registerType(type, {.tick = wolfTick, .die = [](Actor& a, std::string const&, Actor*) {
                                        forgetActor(a);
                                    }});
    }
    // MistWolf#doHurtTarget: a hit in the dark (raw brightness 0 at the wolf, its block not solid)
    // blinds for 7 s (Hard 15 s, Easy none)
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (!isType(d.attacker, id::MistWolf) || (d.direct && d.direct != d.attacker) || d.type != "minecraft:mob_attack") return;
        if (!isLiving(d.victim)) return;
        auto& wolf   = *d.attacker;
        auto& region = wolf.getDimensionBlockSource();
        auto  pos    = blockPosOf(wolf);
        if (static_cast<float>(maxLocalRawBrightness(region, pos)) >= 0.10F) return;
        int seconds = difficulty() == 1 ? 0 : difficulty() == 3 ? 15 : 7;
        if (seconds > 0 && !mobs::legacySolid(region, pos)) addEffect(d.victim, Effect::Blindness, seconds * 20, 0);
    });
    gf::game::db::onWorldClosed([] { lastTargets().clear(); });
    onForget([](std::int64_t uid) { lastTargets().erase(uid); });
}

} // namespace gf::feature::hostiles::wolves

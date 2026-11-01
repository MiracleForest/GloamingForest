// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/ITFCharger.java
//   src/main/java/twilightforest/entity/IBreathAttacker.java
//   src/main/java/twilightforest/entity/ai/control/NoClipMoveControl.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// F8's contract extension (rule R18): the entity part. The part, mount, projectile and boss
// entry points live next to their implementations (Multipart.cpp, Mount.cpp, Projectile.cpp,
// Boss.cpp).

#include "feature/ext/entity/EntityExt.h"

#include <unordered_map>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"

#include "feature/combat/Combat.h"
#include "feature/entity/EntityInternal.h"

namespace gf::entity::ext {

namespace fe = gf::feature::entity;

namespace {
std::unordered_map<std::string, BreathAttackFn>& breathAttacks() {
    static std::unordered_map<std::string, BreathAttackFn> m;
    return m;
}

bool flag(Actor const& actor, char const* key) {
    auto* s = fe::findState(fe::uidOf(actor));
    if (!s || !s->data.is_object() || !s->data.contains(key)) return false;
    auto const& v = s->data[key];
    return v.is_boolean() && v.get<bool>();
}
} // namespace

void setWantedPosition(Mob& mob, Vec3 const& pos, double speed) {
    auto& s       = fe::stateOf(mob);
    s.hasWanted   = true;
    s.wanted      = pos;
    s.wantedSpeed = speed;
}

std::optional<Vec3> wantedPosition(Mob const& mob) {
    auto* s = fe::findState(fe::uidOf(mob));
    if (!s || !s->hasWanted) return std::nullopt;
    return s->wanted;
}

bool isCharging(Actor const& mob) { return flag(mob, "entity:charging"); }
bool isBreathing(Actor const& mob) { return flag(mob, "entity:breathing"); }

void onBreathAttack(std::string const& entityId, BreathAttackFn fn) { breathAttacks()[entityId] = std::move(fn); }

void doBreathAttack(Mob& breather, Actor& target, std::string const& damageType) {
    if (auto it = breathAttacks().find(breather.getTypeName()); it != breathAttacks().end() && it->second) {
        it->second(breather, target);
        return;
    }
    gf::combat::hurt(target, 2.0f, damageType, &breather);
}

} // namespace gf::entity::ext

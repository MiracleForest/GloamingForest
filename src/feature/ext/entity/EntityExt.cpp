// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/ITFCharger.java
//   src/main/java/gloamingforest/entity/IBreathAttacker.java
//   src/main/java/gloamingforest/entity/ai/control/NoClipMoveControl.java
//   src/main/java/gloamingforest/init/TFDataSerializers.java
// Copyright (c) the upstream authors (see NOTICE).
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

namespace {
std::unordered_map<std::string, double>& flyingSpeeds() {
    static std::unordered_map<std::string, double> m;
    return m;
}
} // namespace

void setFlyingSpeed(std::string const& entityId, double speed) {
    if (speed > 0.0) flyingSpeeds()[entityId] = speed;
    else flyingSpeeds().erase(entityId);
}

// Attributes.FLYING_SPEED: the type's (Java's attribute supplier), else the attribute's default
// 0.4. Bedrock's minecraft:flying_speed is not this value: it is the air acceleration per tick
// (LivingEntity#getFlyingSpeed, 0.02 by default).
double flyingSpeed(Mob& mob) {
    if (auto it = flyingSpeeds().find(mob.getTypeName()); it != flyingSpeeds().end()) return it->second;
    return 0.4;
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

// TFDataSerializers.STRING_LIST: EntityDataSerializer.forValueType(STRING_UTF8.apply(list())).
void setStringList(Actor& actor, std::string const& key, std::vector<std::string> const& values) {
    auto& v = gf::entity::data(actor, key);
    nlohmann::json list(values);
    if (v == list) return; // SynchedEntityData sends only changed values
    v = std::move(list);
    gf::entity::sync(actor, key);
}

std::vector<std::string> stringList(Actor& actor, std::string const& key) {
    std::vector<std::string> out;
    auto const&              v = gf::entity::data(actor, key);
    if (v.is_array())
        for (auto const& e : v)
            if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

} // namespace gf::entity::ext

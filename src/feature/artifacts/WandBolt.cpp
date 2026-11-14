// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/projectile/GloamingWandBolt.java
//   src/main/java/gloamingforest/entity/projectile/TFThrowable.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The gloaming scepter's bolt (gloamingforest:wand_bolt) as an F8 projectile: gravity 0.003, a
// teal gloamingforest:magic_effect trail, 6 gloamingforest:gloaming_scepter damage to a living
// target, 8 gloaming_orb particles where it ends. Damage with a causing entity re-shoots it along
// that entity's look (GloamingWandBolt#hurt). U21's Lich deflects it by id.
#include <cmath>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/projectile/Projectile.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace ia = gf::feature::common::interactions;
namespace pe = gf::projectile::ext;

namespace {

/// Projectile#setOwner after the shot: bolt uid -> owner uid (0: none).
std::unordered_map<std::int64_t, std::int64_t>& owners() {
    static std::unordered_map<std::int64_t, std::int64_t> m;
    return m;
}

/// EntityEvent.DEATH / 3: 8 gloaming orb particles.
void orbBurst(Actor& bolt) {
    auto& r   = randomOf(bolt);
    auto  pos = bolt.getPosition();
    for (int i = 0; i < 8; ++i)
        particle(bolt, "gloamingforest:gloaming_orb", pos,
                 Vec3{static_cast<float>(r.nextGaussian() * 0.05), static_cast<float>(r.nextDouble() * 0.2), static_cast<float>(r.nextGaussian() * 0.05)});
}

bool hitEntity(Actor& bolt, Actor& target) {
    // GloamingWandBolt#canHitEntity: never a lich bolt or bomb
    auto const& id = target.getTypeName();
    if (id == "gloamingforest:lich_bolt" || id == "gloamingforest:lich_bomb") return false;
    Actor* owner = boltOwner(bolt);
    if (isLiving(target)) gf::combat::hurt(target, 6.0F, "gloamingforest:gloaming_scepter", owner, &bolt);
    playSoundAt(bolt, target.getPosition(), "item.gloamingforest.gloaming_scepter.pearl_hit", 1.0F, 1.0F);
    orbBurst(bolt);
    owners().erase(uidOf(bolt));
    return true;
}

void trail(Actor& bolt) {
    auto& r   = randomOf(bolt);
    auto  pos = bolt.getPosition();
    for (int i = 0; i < 5; ++i) {
        double dx = pos.x + 0.25 * (r.nextDouble() - r.nextDouble());
        double dy = pos.y + 0.25 * (r.nextDouble() - r.nextDouble());
        double dz = pos.z + 0.25 * (r.nextDouble() - r.nextDouble());
        (void)r.nextFloat(), (void)r.nextFloat(), (void)r.nextFloat(); // the colour draws (the effect rolls its own)
        // the colour (0.17, 0.80, 0.69) * (0.5..1): the effect reads an upward velocity as teal
        particle(bolt, "gloamingforest:magic_effect", Vec3{static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)}, Vec3{0.0F, 1e-4F, 0.0F});
    }
}

} // namespace

Actor* boltOwner(Actor const& bolt) {
    if (auto it = owners().find(uidOf(bolt)); it != owners().end()) return actorById(it->second);
    return pe::owner(bolt);
}

void setBoltOwner(Actor const& bolt, Actor* owner) { owners()[uidOf(bolt)] = owner ? uidOf(*owner) : 0; }

void reflectWandBolt(Actor& bolt, Actor& by, Actor* direct) {
    // shoot(look, 1.5, 0.1): "reflect faster and more accurately"
    Vec3   look = viewVector(by);
    auto&  r    = randomOf(bolt);
    double len  = std::sqrt(double(look.x) * look.x + double(look.y) * look.y + double(look.z) * look.z);
    if (len < 1e-9) return;
    double spread = 0.0172275 * 0.1;
    pe::setVelocity(bolt, Vec3{static_cast<float>((look.x / len + r.triangle(0.0, spread)) * 1.5), static_cast<float>((look.y / len + r.triangle(0.0, spread)) * 1.5),
                               static_cast<float>((look.z / len + r.triangle(0.0, spread)) * 1.5)});
    if (direct && isLiving(*direct)) setBoltOwner(bolt, direct);
    log().info("artifacts: wand bolt reflected by {}", by.getTypeName());
}

void registerWandBolt() {
    game::db::onWorldClosed([] { owners().clear(); });
    gf::projectile::registerType(kWandBolt, {
        .gravity     = 0.003F,
        .drag        = 0.99F,
        .onHitEntity = hitEntity,
        .onHitBlock  = [](Actor& p, BlockPos const&, int) {
            orbBurst(p);
            owners().erase(uidOf(p));
            return true;
        },
        .tick = trail,
    });
    // GloamingWandBolt#hurt: a hit with a causing entity re-shoots it
    ia::onAttack({.block = kWandBolt}, [](ia::EntityContext const& c) {
        reflectWandBolt(c.entity, c.player, &c.player);
        return ia::Result::Consume;
    });
    gf::combat::onIncoming(gf::combat::priority::Highest, [](gf::combat::DamageInfo& d) {
        if (!isType(d.victim, kWandBolt)) return gf::combat::Verdict::Pass;
        if (d.attacker && d.attacker != &d.victim) reflectWandBolt(d.victim, *d.attacker, d.direct);
        return gf::combat::Verdict::Cancel; // a projectile has no health
    });
}

} // namespace gf::feature::artifacts


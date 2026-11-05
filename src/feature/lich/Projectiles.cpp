// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/projectile/LichBolt.java
//   src/main/java/twilightforest/entity/projectile/LichBomb.java
//   src/main/java/twilightforest/entity/projectile/TomeBolt.java
//   src/main/java/twilightforest/entity/projectile/TFThrowable.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Lich's bolt (reflectable with a hit, the way to break its shield) and bomb, and the Death
// Tome's paper bolt, as F8 projectiles. A hit on a bolt or bomb comes through gf::interactions'
// attack (the projectiles carry a hit test larger than their box, Java's pick radius 1.0).

#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/lich/Common.h"
#include "feature/lich/Lich.h"
#include "feature/projectile/Projectile.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::lich {

namespace gp = gf::projectile;
namespace pe = gf::projectile::ext;
namespace ia = gf::feature::common::interactions;

namespace {

/// Projectile#setOwner after the shot (reflections, deflections): projectile uid -> owner uid.
std::unordered_map<std::int64_t, std::int64_t>& owners() {
    static std::unordered_map<std::int64_t, std::int64_t> m;
    return m;
}

bool isLiving(Actor const& a) { return a.isPlayer() || a.hasCategory(ActorCategory::Mob); }

/// An ItemParticleOption burst (EntityEvent.DEATH / 3 of the projectiles).
void itemBurst(Actor& p, char const* item) {
    auto& r   = randomOf(p);
    auto  pos = p.getPosition();
    Json  params{{"item", item}};
    for (int i = 0; i < 8; ++i)
        particle(p, "minecraft:item", pos, Vec3{float(r.nextGaussian() * 0.05), float(r.nextDouble() * 0.2), float(r.nextGaussian() * 0.05)}, params);
}

// ---- LichBolt ----------------------------------------------------------------------------------------

/// LichBolt#canHitEntity.
bool boltCanHit(Actor& bolt, Actor& target) {
    if (isLich(target)) {
        auto* lich  = asMob(target);
        auto* s     = lichState(target);
        Actor* owner = projectileOwner(bolt);
        if (s->tpInvisibility > 0 || (!(owner && owner->isPlayer()) && phaseOf(*lich) == 1)) return false;
    }
    auto const& id = target.getTypeName();
    return id != kLichBomb && id != kLichBolt && id != kWandBolt;
}

bool boltHitEntity(Actor& bolt, Actor& target) {
    if (!boltCanHit(bolt, target)) return false;
    // Lich#deflection (phases 2 and 3)
    if (isLich(target))
        if (auto* lich = asMob(target); lich && deflect(*lich, bolt, projectileOwner(bolt))) return false;
    if (isLiving(target)) {
        Actor* owner = projectileOwner(bolt);
        gf::combat::hurt(target, 6.0F, "twilightforest:lich_bolt", owner ? owner : &bolt, &bolt);
    }
    itemBurst(bolt, "minecraft:ender_pearl");
    return true;
}

void boltTick(Actor& bolt) {
    auto& r  = randomOf(bolt);
    float s1 = ((r.nextFloat() * 0.5F) + 0.5F) * 0.17F;
    float s2 = ((r.nextFloat() * 0.5F) + 0.5F) * 0.80F;
    float s3 = ((r.nextFloat() * 0.5F) + 0.5F) * 0.69F;
    pe::makeTrail(bolt, "minecraft:entity_effect", 5, Vec3{s1, s2, s3});
}

// ---- LichBomb ----------------------------------------------------------------------------------------

bool bombCanHit(Actor& bomb, Actor& target) {
    if (isLich(target)) return false; // never a Lich (invisible or not, whatever the owner)
    (void)bomb;
    auto const& id = target.getTypeName();
    return id != kLichBomb && id != kLichBolt && id != kWandBolt;
}

void bombTick(Actor& bomb) {
    auto& r = randomOf(bomb);
    auto  v = pe::velocity(bomb);
    auto  p = bomb.getPosition();
    double sx = 0.5 * (r.nextDouble() - r.nextDouble()) + v.x;
    double sy = 0.5 * (r.nextDouble() - r.nextDouble()) + v.y;
    double sz = 0.5 * (r.nextDouble() - r.nextDouble()) + v.z;
    particle(bomb, "minecraft:flame", Vec3{float(p.x - sx), float(p.y - sy), float(p.z - sz)}, Vec3{float(sx * -0.25), float(sy * -0.25), float(sz * -0.25)});
}

// ---- TomeBolt ----------------------------------------------------------------------------------------

bool tomeBoltHitEntity(Actor& bolt, Actor& target) {
    if (isLiving(target)) {
        auto&  r     = randomOf(bolt);
        Actor* owner = projectileOwner(bolt);
        char const* type = r.nextBoolean() ? "twilightforest:lost_words" : "twilightforest:schooled";
        if (gf::combat::hurt(target, 3.0F, type, owner ? owner : &bolt, &bolt)) {
            auto level = ll::service::getLevel();
            auto d     = level ? level->getDifficulty() : SharedTypes::Legacy::Difficulty::Normal;
            int  secs  = d == SharedTypes::Legacy::Difficulty::Easy ? 2 : d == SharedTypes::Legacy::Difficulty::Normal ? 6 : 8;
            MobEffectInstance e(2, EffectDuration{secs * 20}); // MOVEMENT_SLOWDOWN II
            e.mAmplifier = 1;
            target.addEffect(e);
        }
    }
    itemBurst(bolt, "minecraft:paper");
    return true;
}

void tomeBoltTick(Actor& bolt) {
    pe::makeTrail(bolt, "minecraft:crit", 5);
    // TomeBolt#onHit ignores the lectern its owner stands on: fly through it this tick
    Actor* owner = projectileOwner(bolt);
    if (!owner) return;
    auto  v      = pe::velocity(bolt);
    auto  p      = bolt.getPosition();
    Vec3  next{p.x + v.x, p.y + v.y, p.z + v.z};
    auto& region = bolt.getDimensionBlockSource();
    HitResult hr = region.clip(p, next, false, ShapeType::Collision, 64, false, false, nullptr, [](BlockSource const&, Block const&, bool) { return true; }, false);
    if (hr.mType != HitResultType::Tile) return;
    auto ob = game::blockPosOf(owner->getFeetPos());
    if (hr.mBlock == ob && game::blockIdAt(region, hr.mBlock) == "minecraft:lectern") bolt.moveTo(next, bolt.getRotation());
}

} // namespace

Actor* projectileOwner(Actor const& projectile) {
    if (auto it = owners().find(uidOf(projectile)); it != owners().end()) return actorById(it->second);
    return pe::owner(projectile);
}

void setProjectileOwner(Actor const& projectile, Actor* owner) {
    if (owner) owners()[uidOf(projectile)] = uidOf(*owner);
    else owners()[uidOf(projectile)] = 0;
}

void pruneProjectiles() {
    std::erase_if(owners(), [](auto const& kv) { return actorById(kv.first) == nullptr; });
}

void reflectBolt(Actor& bolt, Actor& by, Actor* direct) {
    // shoot(look, 1.5, 0.1): normalized look plus a tiny triangle spread
    Vec3   look = viewVector(by);
    auto&  r    = randomOf(bolt);
    double len  = std::sqrt(double(look.x) * look.x + double(look.y) * look.y + double(look.z) * look.z);
    if (len < 1e-9) return;
    double spread = 0.0172275 * 0.1;
    Vec3   v{float((look.x / len + r.triangle(0.0, spread)) * 1.5), float((look.y / len + r.triangle(0.0, spread)) * 1.5),
           float((look.z / len + r.triangle(0.0, spread)) * 1.5)};
    pe::setVelocity(bolt, v);
    // the direct entity becomes the owner when it is a living one (a player's melee hit)
    if (direct && isLiving(*direct)) setProjectileOwner(bolt, direct);
    log().info("lich: bolt reflected by {}", by.getTypeName());
}

void explodeBomb(Actor& bomb) {
    auto level = ll::service::getLevel();
    if (!level) return;
    // once: the explosion's own damage reaches the bomb again (and is ignored there)
    static std::unordered_set<std::int64_t> exploding;
    auto uid = uidOf(bomb);
    if (!exploding.insert(uid).second) return;
    auto pos = bomb.getPosition();
    level->explode(bomb.getDimensionBlockSource(), &bomb, pos, 2.0F, false, false, FLT_MAX, false);
    exploding.erase(uid);
    owners().erase(uid);
    discard(bomb);
    log().info("lich: a lich bomb exploded");
}

void registerProjectiles() {
    gp::registerType(kLichBolt, {
        .gravity     = 0.001F,
        .drag        = 0.99F,
        .onHitEntity = boltHitEntity,
        .onHitBlock  = [](Actor& p, BlockPos const&, int) {
            itemBurst(p, "minecraft:ender_pearl");
            return true;
        },
        .tick = boltTick,
    });
    gp::registerType(kLichBomb, {
        .gravity     = 0.001F,
        .drag        = 0.99F,
        .onHitEntity = [](Actor& p, Actor& target) {
            if (!bombCanHit(p, target)) return false;
            explodeBomb(p);
            return true;
        },
        .onHitBlock = [](Actor& p, BlockPos const&, int) {
            explodeBomb(p);
            return true;
        },
        .tick = bombTick,
    });
    gp::registerType(kTomeBolt, {
        .gravity     = 0.003F,
        .drag        = 0.99F,
        .onHitEntity = tomeBoltHitEntity,
        .onHitBlock  = [](Actor& p, BlockPos const&, int) {
            itemBurst(p, "minecraft:paper");
            return true;
        },
        .tick = tomeBoltTick,
    });
    // LichBolt#hurt / LichBomb#hurt: a hit by an entity reflects the bolt, explodes the bomb. A
    // player's melee hit comes through the attack interaction (the BP's custom_hit_test makes them
    // pickable); any other damage with an entity source (an arrow, a mob's projectile, a thorns
    // hit) through the damage pipeline below.
    ia::onAttack({.block = kLichBolt}, [](ia::EntityContext const& c) {
        reflectBolt(c.entity, c.player, &c.player);
        return ia::Result::Consume;
    });
    ia::onAttack({.block = kLichBomb}, [](ia::EntityContext const& c) {
        explodeBomb(c.entity);
        return ia::Result::Consume;
    });
    gf::combat::onIncoming(gf::combat::priority::Highest, [](gf::combat::DamageInfo& d) {
        bool bolt = isType(d.victim, kLichBolt), bomb = isType(d.victim, kLichBomb);
        if (!bolt && !bomb) return gf::combat::Verdict::Pass;
        // the bomb's own explosion and the bolts' and bombs' blasts leave them be (ignoreExplosion)
        Actor* blast = d.direct && isType(*d.direct, kLichBomb) ? d.direct : d.attacker && isType(*d.attacker, kLichBomb) ? d.attacker : nullptr;
        if (blast) return gf::combat::Verdict::Cancel;
        if (bomb && d.direct && d.direct != &d.victim) explodeBomb(d.victim);                   // source.getDirectEntity() != null
        else if (bolt && d.attacker && d.attacker != &d.victim) reflectBolt(d.victim, *d.attacker, d.direct); // source.getEntity() != null
        return gf::combat::Verdict::Cancel; // a projectile has no health
    });
    // EntityEvents#lichBombsDontBlowUpItems: a lich bomb's explosion leaves item entities alone
    gf::combat::onIncoming(gf::combat::priority::Highest, [](gf::combat::DamageInfo& d) {
        if (d.victim.getTypeName() != "minecraft:item") return gf::combat::Verdict::Pass;
        bool fromBomb = (d.direct && isType(*d.direct, kLichBomb)) || (d.attacker && isType(*d.attacker, kLichBomb));
        return fromBomb ? gf::combat::Verdict::Cancel : gf::combat::Verdict::Pass;
    });
    // the bomb's explosion is TFDamageTypes.LICH_BOMB, attributed to the bomb's owner
    gf::combat::onIncoming(gf::combat::priority::Highest, [](gf::combat::DamageInfo& d) {
        Actor* bomb = d.direct && isType(*d.direct, kLichBomb) ? d.direct : d.attacker && isType(*d.attacker, kLichBomb) ? d.attacker : nullptr;
        if (!bomb) return gf::combat::Verdict::Pass;
        d.type     = "twilightforest:lich_bomb";
        d.direct   = bomb;
        d.attacker = projectileOwner(*bomb);
        if (!d.attacker) d.attacker = bomb;
        return gf::combat::Verdict::Pass;
    });
}

} // namespace gf::feature::lich

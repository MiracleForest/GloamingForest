// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/LichMinion.java
//   src/main/java/twilightforest/entity/monster/DeathTome.java
//   src/main/java/twilightforest/entity/monster/RisingZombie.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Lich's minions (zombies bound to a master), the Death Tome (a flying book that mimics a
// lectern book) and the rising zombie of the tower yard. Their walking, flying and targeting are
// Bedrock behaviour components in their BP files; the TF-specific parts run here.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/lich/Common.h"
#include "feature/lich/Lich.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::lich {

namespace ge = gf::entity;

namespace {

// ---- LichMinion ------------------------------------------------------------------------------------

std::unordered_map<std::int64_t, std::int64_t>& masters() {
    static std::unordered_map<std::int64_t, std::int64_t> m;
    return m;
}

void addEffect(Actor& a, unsigned id, int ticks, int amplifier) {
    MobEffectInstance e(id, EffectDuration{ticks});
    e.mAmplifier = amplifier;
    a.addEffect(e);
}

void setHealthTo(Actor& a, float value) {
    auto* map = const_cast<BaseAttributeMap*>(static_cast<BaseAttributeMap const*>(a.getAttributes()));
    (void)map->setCurrentValue(SharedAttributes::HEALTH(), value);
}

/// LichMinion#findNewMaster.
void findNewMaster(Mob& minion) {
    auto p = minion.getFeetPos();
    AABB box{Vec3{p.x - 32.0f, p.y - 16.0f, p.z - 32.0f}, Vec3{p.x + 33.0f, p.y + 17.0f, p.z + 33.0f}};
    for (auto* a : actorsIn(minion.getDimensionBlockSource(), dimOf(minion), box)) {
        if (!isLich(*a)) continue;
        auto* lich = asMob(*a);
        if (!lich || isShadowClone(*lich) || !wantsNewMinion(*lich)) continue;
        setMinionMaster(minion, lich);
        forgetMinionCount(*lich);
        makeMagicTrail(*lich, eyeOf(minion), eyeOf(*lich), 0.0F, 0.0F, 0.0F);
        setTarget(minion, targetOf(*lich));
        break;
    }
}

void minionAiStep(Actor& a) {
    auto* m = asMob(a);
    if (!m || !m->isAlive()) return;
    if (!minionMaster(*m)) findNewMaster(*m);
    Mob* master = minionMaster(*m);
    if (!master || !master->isAlive()) m->kill();
}

void minionRemoved(Actor& a, std::string const&, Actor*) { masters().erase(uidOf(a)); }

// ---- DeathTome -------------------------------------------------------------------------------------

std::unordered_map<std::int64_t, bool>& lecterns() {
    static std::unordered_map<std::int64_t, bool> m;
    return m;
}

/// DeathTome#performRangedAttack: a TomeBolt at the target's eyes - 1.1, aimed a little high.
void tomeShoot(Mob& tome, Actor& target) {
    auto   tp  = target.getFeetPos();
    auto   p   = tome.getFeetPos();
    float  py  = eyeOf(tome).y - 0.1f; // ThrowableProjectile(type, owner): at the eyes - 0.1
    double tx  = tp.x - p.x;
    double ty  = tp.y + (eyeOf(target).y - tp.y) - 1.1 - py;
    double tz  = tp.z - p.z;
    float  off = std::sqrt(static_cast<float>(tx * tx + tz * tz)) * 0.2F;
    gf::projectile::shoot(tome, kTomeBolt, Vec3{float(tx), float(ty + off), float(tz)}, 0.6F, 6.0F);
}

/// RangedAttackGoal(mob, 1.0, 100, 5).
struct RangedAttackGoal final : ge::Goal {
    Mob& m;
    int  attackTime = -1;
    int  seeTime    = 0;
    explicit RangedAttackGoal(Mob& mob) : m(mob) { flags = Move | Look; }
    bool canUse() override {
        if (tomeOnLectern(m)) return false;
        Actor* t = targetOf(m);
        return t && t->isAlive();
    }
    bool canContinueToUse() override { return canUse() || !navigationDone(m); }
    void stop() override {
        seeTime    = 0;
        attackTime = -1;
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        Actor* t = targetOf(m);
        if (!t) return;
        double d     = distSqr(m.getFeetPos(), t->getFeetPos());
        bool   sight = hasLineOfSight(m, *t);
        if (sight) ++seeTime;
        else seeTime = 0;
        if (d <= 25.0 && seeTime >= 5) stopNavigation(m);
        else moveTo(m, t->getFeetPos(), 1.0);
        lookAt(m, *t, 30.0F, 30.0F);
        if (--attackTime == 0) {
            if (!sight) return;
            tomeShoot(m, *t);
            attackTime = 100;
        } else if (attackTime < 0) attackTime = 100;
    }
};

bool isLecternAt(BlockSource& region, BlockPos const& pos) { return game::blockIdAt(region, pos) == "minecraft:lectern"; }

/// DeathTome's lectern target selector: the nearest player within 3 that looks at the tome.
Player* lecternTarget(Mob& tome) {
    auto    p    = tome.getFeetPos();
    Player* best = nullptr;
    double  bd   = 1e300;
    float   eye  = eyeOf(tome).y;
    for (auto* pl : playersNear(dimOf(tome), p, 3.0)) {
        if (pl->isSneaking() || pl->isCreative() || pl->isSpectator()) continue;
        if (!isLookedAtBy(tome, *pl, 0.25, {eye, p.y + 0.5, (eye + p.y) / 2.0})) continue;
        double d = distSqr(pl->getFeetPos(), p);
        if (d < bd) bd = d, best = pl;
    }
    return best;
}

void tomeOnSpawn(Actor& a) {
    auto* m = asMob(a);
    if (!m) return;
    auto& lect = ge::data(a, "on_lectern");
    if (lect.is_boolean()) setTomeOnLectern(a, lect.get<bool>());
    ge::addGoal(*m, 4, std::make_shared<RangedAttackGoal>(*m));
}

void tomeAiStep(Actor& a) {
    auto* m = asMob(a);
    if (!m || !m->isAlive()) return;
    if (tomeOnLectern(a)) {
        auto& region = m->getDimensionBlockSource();
        auto  at     = game::blockPosOf(m->getFeetPos());
        // the tome stands in the lectern's block, or on its top (0.875 high) when the engine lifted it;
        // the lectern it was put on is remembered (the engine's collision may push the tome aside)
        auto& seat = ge::data(a, "lectern_pos");
        if (seat.is_array() && seat.size() == 3) at = BlockPos{seat[0].get<int>(), seat[1].get<int>(), seat[2].get<int>()};
        else if (!isLecternAt(region, at)) at.y -= 1;
        if (isLecternAt(region, at)) {
            if (!seat.is_array()) seat = Json::array({at.x, at.y, at.z});
            // it rests there, still (Java: the tome only turns to the lectern's facing)
            Vec3 rest{at.x + 0.5f, float(at.y), at.z + 0.5f};
            setMotion(a, Vec3{0.0f, 0.0f, 0.0f});
            if (distSqr(m->getFeetPos(), rest) > 0.04) teleportTo(a, rest);
            ge::data(a, "lectern_missed") = 0;
            // face the lectern's FACING (Bedrock: direction 0 south .. 3 east -> yaw)
            int   dir = game::intState(region.getBlock(at), "direction").value_or(0);
            float yaw = static_cast<float>(dir) * 90.0F;
            m->setYBodyRotation(yaw);
            m->setYHeadRot(yaw);
            const_cast<Vec2&>(m->getRotation()) = Vec2{0.0f, yaw};
            // only the lectern's own target selector picks a target here (the BP's flying behaviours
            // may have picked one before the lectern group took over)
            setTarget(*m, lecternTarget(*m));
            if (Actor* t = targetOf(*m)) {
                setTomeOnLectern(a, false);
                setMotion(a, Vec3{0.0f, 0.25f, 0.0f});
                tomeShoot(*m, *t);
                log().info("lich: a death tome left its lectern");
            }
            return;
        }
        // a tome placed on a lectern this tick may still see the spawner's adjusted position: the
        // lectern counts as gone once the tome has missed it for 5 ticks in a row
        auto& missed = ge::data(a, "lectern_missed");
        int   n      = missed.is_number_integer() ? missed.get<int>() + 1 : 1;
        missed       = n;
        if (n < 5) return;
        log().info("lich: a death tome is off its lectern ({} below it)", game::blockIdAt(region, at));
        setTomeOnLectern(a, false);
    }
    auto v = m->getPosDelta();
    if (!m->isOnGround() && v.y < 0.0f) setMotion(a, Vec3{v.x, v.y * 0.6f, v.z});
    auto& r = randomOf(a);
    auto  p = m->getFeetPos();
    float w = bbWidth(a), h = bbHeight(a);
    particle(a, "minecraft:enchant",
             Vec3{float(p.x + (r.nextDouble() - 0.5) * w), float(p.y + r.nextDouble() * (h - 0.75) + 0.5), float(p.z + (r.nextDouble() - 0.5) * w)},
             Vec3{0.0f, 0.5f, 0.0f});
}

void tomeSave(Actor& a, Json& out) { out["on_lectern"] = tomeOnLectern(a); }
void tomeLoad(Actor& a, Json const& in) { setTomeOnLectern(a, in.value("on_lectern", false)); }

/// DeathTome#hurt after the damage applied: off the lectern, the hurt loot.
void tomeDealt(Mob& tome, gf::combat::DamageInfo const& d) {
    if (d.amount <= 0) return;
    if (tomeOnLectern(tome)) {
        log().info("lich: a death tome was hurt off its lectern ({} {})", d.type, d.amount);
        setMotion(tome, Vec3{0.0f, 0.25f, 0.0f});
        setTomeOnLectern(tome, false);
    }
    gf::loot::LootContext ctx;
    ctx.region     = &tome.getDimensionBlockSource();
    ctx.origin     = tome.getFeetPos();
    ctx.thisEntity = &tome;
    ctx.attacker   = d.attacker;
    ctx.direct     = d.direct;
    ctx.damageType = d.type;
    if (d.attacker && d.attacker->isPlayer()) ctx.player = static_cast<Player*>(d.attacker);
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto& stack : gf::loot::roll("twilightforest:entities/death_tome_hurt", ctx))
        level->getSpawner().spawnItem(tome.getDimensionBlockSource(), stack, nullptr, tome.getFeetPos(), 0);
}

// ---- RisingZombie ----------------------------------------------------------------------------------

std::unordered_map<std::int64_t, int>& rising() {
    static std::unordered_map<std::int64_t, int> m;
    return m;
}

/// RisingZombie#isLookingInMyDirection(player, 0.5, false, true, ...).
bool risingLookedAt(Mob& z, Player& player) {
    auto   p    = z.getFeetPos();
    double eye  = eyeOf(z).y;
    Vec3   view = viewVector(player);
    auto   pp   = player.getFeetPos();
    double pe   = eyeOf(player).y;
    for (double y : {eye, p.y + 0.5, (eye + p.y) / 2.0}) {
        double dx = p.x - pp.x, dy = y - pe, dz = p.z - pp.z;
        double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-9) continue;
        if ((view.x * dx + view.y * dy + view.z * dz) / len <= 0.5) continue;
        // RisingZombie#hasLineOfSight(player, VISUAL, NONE, yOffs): zombie eyes -> (player x, yOffs, player z)
        Vec3 from{p.x, float(eye), p.z};
        Vec3 to{pp.x, float(y), pp.z};
        if (distSqr(from, to) > 128.0 * 128.0) continue;
        if (clearPath(z.getDimensionBlockSource(), from, to)) return true;
    }
    return false;
}

void applyRisingSize(Actor& a, int ticks) {
    float h = 1.95F * static_cast<float>(ticks) / 130.0F;
    a.synchronousSetSize(0.6F, std::max(h, 0.01F));
    a.setInvisible(ticks == 0);
}

void risingOnSpawn(Actor& a) {
    auto& d = ge::data(a, "rising_ticks");
    setRisingTicks(a, d.is_number_integer() ? d.get<int>() : 0);
}

void risingAiStep(Actor& a) {
    auto* z = asMob(a);
    if (!z) return;
    int  rt     = risingTicks(a);
    auto& r     = randomOf(a);
    auto& region = z->getDimensionBlockSource();
    if (rt > 0 && z->isAlive()) {
        BlockPos     below = game::blockPosOf(z->getFeetPos());
        below.y -= 1;
        Block const& state = region.getBlock(below);
        setRisingTicks(a, rt + 1);
        rt = rt + 1;
        // the break sound of the block below (Bedrock picks it by the block; no pitch of our own)
        if (rt % 10 == 0 && rt < 130 && !state.isAir())
            z->playSound(region, SharedTypes::Legacy::LevelSoundEvent::BreakBlock, z->getFeetPos(), static_cast<int>(state.mNetworkId));
        if (!state.isAir()) {
            // the client's block particles (Java: client side)
            auto   p  = z->getFeetPos();
            double px = p.x + r.nextDouble() - 0.5, py = p.y + r.nextDouble() * 0.2 + 0.075, pz = p.z + r.nextDouble() - 0.5;
            Json   params{{"block", game::blockId(state)}};
            for (int i = 0, n = r.nextInt(10) + 5; i < n; ++i) {
                double ox = r.nextDouble() * 0.1 - 0.05, oz = r.nextDouble() * 0.1 - 0.05;
                double mx = r.nextDouble() * 0.2 - 0.1, my = r.nextDouble() * 0.25 + 0.1, mz = r.nextDouble() * 0.2 - 0.1;
                particle(a, "minecraft:block", Vec3{float(px + ox), float(py), float(pz + oz)}, Vec3{float(mx), float(my), float(mz)}, params);
            }
        }
    } else {
        auto& t = ge::data(a, "lich:age");
        int   age = t.is_number_integer() ? t.get<int>() + 1 : 1;
        t        = age;
        if (age % 10 == 0) {
            if (Player* player = nearestPlayer(dimOf(a), z->getFeetPos(), 35.0 / 2.0); player && risingLookedAt(*z, *player)) {
                setRisingTicks(a, 1);
                log().info("lich: a rising zombie starts to rise");
            }
        }
    }
    if (risingTicks(a) >= 130 && z->isAlive()) {
        // Mob#convertTo(ZOMBIE, true) keeping health and yaw
        float hp  = health(*z);
        float yaw = z->getRotation().y;
        Mob*  zombie = spawnMob(region, "minecraft:zombie", z->getFeetPos(), yaw);
        if (zombie) {
            setHealthTo(*zombie, hp);
            zombie->setPersistent();
        }
        rising().erase(uidOf(a));
        discardLater(a);
        log().info("lich: a rising zombie became a zombie");
    }
}

} // namespace

// ---- the shared operations ------------------------------------------------------------------------------

Mob* minionMaster(Actor const& minion) {
    auto it = masters().find(uidOf(minion));
    if (it == masters().end()) return nullptr;
    auto* m = mobById(it->second);
    return m && isLich(*m) ? m : nullptr;
}

void setMinionMaster(Actor const& minion, Mob* master) {
    if (master) masters()[uidOf(minion)] = uidOf(*master);
    else masters().erase(uidOf(minion));
}

Mob* spawnMinion(Mob& lich, Vec3 const& pos, Actor* target) {
    auto* s     = lichState(lich);
    bool  baby  = false;
    auto  level = ll::service::getLevel();
    // LichMinion#finalizeSpawn: on HARD a quarter of the minions may be babies (21 %)
    if (level && level->getDifficulty() == SharedTypes::Legacy::Difficulty::Hard && s) {
        if (s->babies < s->minionCount / 4) baby = randomOf(lich).nextInt(100) <= 20;
        if (baby) s->babies++;
    }
    std::string id    = baby ? std::string(kMinion) + "<gf:as_baby>" : std::string(kMinion);
    Mob*        minion = spawnMob(lich.getDimensionBlockSource(), id, pos, lich.getRotation().y);
    if (!minion) return nullptr;
    setMinionMaster(*minion, &lich);
    forgetMinionCount(lich);
    if (target) setTarget(*minion, target);
    // Mob#spawnAnim: 20 poof particles
    auto& r = randomOf(lich);
    auto  p = minion->getFeetPos();
    for (int i = 0; i < 20; ++i) {
        double vx = r.nextGaussian() * 0.02, vy = r.nextGaussian() * 0.02, vz = r.nextGaussian() * 0.02;
        particle(*minion, "minecraft:poof",
                 Vec3{float(p.x + (r.nextDouble() * 2 - 1) * 0.6 - vx * 10), float(p.y + r.nextDouble() * 1.95 - vy * 10), float(p.z + (r.nextDouble() * 2 - 1) * 0.6 - vz * 10)},
                 Vec3{float(vx), float(vy), float(vz)});
    }
    log().info("lich: minion summoned ({} left)", s ? s->minionsLeft - 1 : 0);
    return minion;
}

void pruneMobs() {
    auto gone = [](std::int64_t uid) { return actorById(uid) == nullptr; };
    std::erase_if(masters(), [&](auto const& kv) { return gone(kv.first); });
    std::erase_if(lecterns(), [&](auto const& kv) { return gone(kv.first); });
    std::erase_if(rising(), [&](auto const& kv) { return gone(kv.first); });
}

bool tomeOnLectern(Actor const& tome) {
    auto it = lecterns().find(uidOf(tome));
    return it != lecterns().end() && it->second;
}

void setTomeOnLectern(Actor& tome, bool on) {
    if (!on) ge::data(tome, "lectern_pos") = nullptr;
    lecterns()[uidOf(tome)]     = on;
    ge::data(tome, "on_lectern") = on;
    ge::setProperty(tome, "gf:on_lectern", on);
}

int risingTicks(Actor const& zombie) {
    auto it = rising().find(uidOf(zombie));
    return it != rising().end() ? it->second : 0;
}

void setRisingTicks(Actor& zombie, int ticks) {
    int old                    = risingTicks(zombie);
    rising()[uidOf(zombie)]    = ticks;
    ge::data(zombie, "rising_ticks") = ticks;
    ge::setProperty(zombie, "twilightforest:rising_ticks", std::min(ticks, 130));
    if (old != ticks || ticks == 0) applyRisingSize(zombie, ticks);
}

void registerMobs() {
    ge::registerType(kMinion, {.aiStep = minionAiStep, .die = minionRemoved});
    ge::registerType(kTome, {.onSpawn = tomeOnSpawn, .aiStep = tomeAiStep, .save = tomeSave, .load = tomeLoad});
    ge::registerType(kRising, {.onSpawn = risingOnSpawn, .aiStep = risingAiStep});
    gf::entity::ext::setFlyingSpeed(kTome, 0.6);
    gf::entity::ext::setCanUsePortal(kRising, false);
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        // DeathTome#hurt: fire damage doubled
        if (isType(d.victim, kTome) && damageTypeIn("minecraft:is_fire", d.type)) d.amount *= 2;
        // a lectern is no full block in Java: a tome resting in one does not suffocate (Bedrock says it does)
        if (isType(d.victim, kTome) && d.type == "minecraft:in_wall" && tomeOnLectern(d.victim)) return gf::combat::Verdict::Cancel;
        // RisingZombie#isInvulnerableTo: only in_wall
        if (isType(d.victim, kRising) && d.type == "minecraft:in_wall") return gf::combat::Verdict::Cancel;
        return gf::combat::Verdict::Pass;
    });
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (isType(d.victim, kTome)) {
            if (auto* m = asMob(d.victim); m && isServerActor(*m)) tomeDealt(*m, d);
        } else if (isType(d.victim, kMinion) && d.attacker && isLich(*d.attacker)) {
            // LichMinion#hurt by its Lich: back to the previous target, sped up and stronger
            auto* m = asMob(d.victim);
            if (!m) return;
            if (Actor* prev = targetOf(*m)) setLastHurtBy(*m, prev);
            addEffect(*m, 1, 200, 2); // MOVEMENT_SPEED III
            addEffect(*m, 5, 200, 1); // DAMAGE_BOOST II
        }
    });
}

} // namespace gf::feature::lich

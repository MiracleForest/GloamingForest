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
#include <map>
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

#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/LecternBlockActor.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/fx/Fx.h"
#include "game/Items.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/lich/Common.h"
#include "feature/lich/Lich.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::lich {

namespace ge = gf::entity;
namespace ia = gf::feature::common::interactions;

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

/// Zombie#tick's under-water conversion (a LichMinion is a Zombie): 600 ticks with the eyes in
/// water start it, 300 more turn the minion into a drowned (Zombie#doUnderWaterConversion).
void minionWaterConversion(Mob& m) {
    auto& d          = ge::data(m, "lich:water");
    int   inWater    = d.is_object() ? d.value("in", 0) : 0;
    int   conversion = d.is_object() ? d.value("conversion", -1) : -1;
    if (conversion >= 0) {
        if (--conversion < 0) {
            float hp  = health(m);
            float yaw = m.getRotation().y;
            Mob*  drowned = spawnMob(m.getDimensionBlockSource(), "minecraft:drowned", m.getFeetPos(), yaw);
            if (drowned) {
                setHealthTo(*drowned, hp);
                if (!m.getNameTag().empty()) {
                    drowned->setNameTag(m.getNameTag());
                    drowned->setPersistent();
                }
            }
            playSound(m, "mob.zombie.converted_to_drowned", 1.0F, 1.0F); // levelEvent 1040
            masters().erase(uidOf(m));
            discardLater(m);
            log().info("lich: a lich minion became a drowned");
            return;
        }
    } else {
        Vec3  eye   = eyeOf(m);
        auto  block = game::blockIdAt(m.getDimensionBlockSource(), game::blockPosOf(eye));
        bool  eyeInWater = block == "minecraft:water" || block == "minecraft:flowing_water";
        if (eyeInWater) {
            if (++inWater >= 600) conversion = 300; // startUnderWaterConversion(300)
        } else inWater = -1;
    }
    if (!d.is_object() || d.value("in", 0) != inWater || d.value("conversion", -1) != conversion) d = Json{{"in", inWater}, {"conversion", conversion}};
}

/// LichMinionModel#renderToBuffer tints the minion by hasEffect(DAMAGE_BOOST) (r, b x0.5; x0.25
/// with Strength): the client reads it from the twilightforest:strong property.
void syncMinionStrength(Mob& m) {
    bool  strong = m.getEffect(5) != nullptr; // DAMAGE_BOOST
    auto& d      = ge::data(m, "lich:strong");
    if (d.is_boolean() && d.get<bool>() == strong) return;
    d = strong;
    ge::setProperty(m, "twilightforest:strong", strong);
}

void minionAiStep(Actor& a) {
    auto* m = asMob(a);
    if (!m || !m->isAlive()) return;
    if (!minionMaster(*m)) findNewMaster(*m);
    Mob* master = minionMaster(*m);
    if (!master || !master->isAlive()) {
        m->kill();
        return;
    }
    minionWaterConversion(*m);
    syncMinionStrength(*m);
}

/// Zombie#handleAttributes: the reinforcement chance a zombie spawns with (nextDouble() * 0.1;
/// a leader, 5 % x the special multiplier, adds 0.5..0.75). Bedrock has no attribute for it: the
/// minion's data keeps it.
double reinforcementChance(Mob& m) {
    auto& d = ge::data(m, "lich:reinforcements");
    if (d.is_number()) return d.get<double>();
    auto&  r      = randomOf(m);
    double chance = r.nextDouble() * 0.1F;
    auto   level  = ll::service::getLevel();
    float  special = level && level->getDifficulty() == SharedTypes::Legacy::Difficulty::Hard ? 0.5F : 0.0F; // DifficultyInstance#getSpecialMultiplier
    if (r.nextFloat() < special * 0.05F) chance += r.nextDouble() * 0.25 + 0.5;
    d = chance;
    return chance;
}

/// Zombie#hurt's reinforcements on Hard: up to 50 tries for a spot 7..40 blocks away on each axis,
/// dark enough and clear, with no player within 7; the new minion takes the target, and both
/// zombies lose 0.05 of their chance.
void callReinforcements(Mob& m, Actor* attacker) {
    auto level = ll::service::getLevel();
    if (!level || level->getDifficulty() != SharedTypes::Legacy::Difficulty::Hard) return;
    Actor* target = targetOf(m);
    if (!target && attacker && (attacker->isPlayer() || attacker->hasCategory(ActorCategory::Mob))) target = attacker;
    if (!target) return;
    auto& r = randomOf(m);
    if (r.nextFloat() >= reinforcementChance(m)) return;
    auto& region = m.getDimensionBlockSource();
    auto  p      = m.getFeetPos();
    int   x = int(std::floor(p.x)), y = int(std::floor(p.y)), z = int(std::floor(p.z));
    auto  nextInt = [&](int lo, int hi) { return lo + r.nextInt(hi - lo + 1); };
    for (int l = 0; l < 50; ++l) {
        int i1 = x + nextInt(7, 40) * nextInt(-1, 1);
        int j1 = y + nextInt(7, 40) * nextInt(-1, 1);
        int k1 = z + nextInt(7, 40) * nextInt(-1, 1);
        BlockPos pos{i1, j1, k1};
        if (!region.hasChunksAt(pos, 1, false)) continue;
        // SpawnPlacements ON_GROUND + Monster#checkMonsterSpawnRules (no light check without a
        // brightness query here: the block below must be solid, the column clear and dry)
        Block const& below = region.getBlock(BlockPos{i1, j1 - 1, k1});
        if (below.isAir() || game::blockId(below).find("water") != std::string::npos || game::blockId(below).find("lava") != std::string::npos) continue;
        if (!region.getBlock(pos).isAir() || !region.getBlock(BlockPos{i1, j1 + 1, k1}).isAir()) continue;
        Vec3 at{i1 + 0.0f, float(j1), k1 + 0.0f};
        if (!playersNear(dimOf(m), at, 7.0).empty()) continue;
        Mob* reinforcement = spawnMob(region, kMinion, at, 0.0f);
        if (!reinforcement) continue;
        setTarget(*reinforcement, target);
        ge::data(m, "lich:reinforcements")              = reinforcementChance(m) - 0.05;
        ge::data(*reinforcement, "lich:reinforcements") = reinforcementChance(*reinforcement) - 0.05;
        log().info("lich: a lich minion called a reinforcement");
        break;
    }
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

/// The lectern's facing as 0 south, 1 west, 2 north, 3 east (Bedrock's minecraft:cardinal_direction,
/// or the older integer direction state).
int lecternDirection(Block const& block) {
    auto states = game::statesOf(block);
    if (auto it = states.find("minecraft:cardinal_direction"); it != states.end()) {
        static std::map<std::string, int> const k{{"south", 0}, {"west", 1}, {"north", 2}, {"east", 3}};
        if (auto d = k.find(it->second); d != k.end()) return d->second;
    }
    return game::intState(block, "direction").value_or(0);
}

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

/// The lectern a tome was put on (DeathTome#blockPosition when it was set on it).
std::optional<BlockPos> lecternSeat(Actor& tome) {
    auto const& seat = ge::data(tome, "lectern_pos");
    if (seat.is_array() && seat.size() == 3) return BlockPos{seat[0].get<int>(), seat[1].get<int>(), seat[2].get<int>()};
    return std::nullopt;
}

void tomeAiStep(Actor& a) {
    auto* m = asMob(a);
    if (!m || !m->isAlive()) return;
    if (tomeOnLectern(a)) {
        auto& region = m->getDimensionBlockSource();
        // DeathTome#aiStep looks at the block the tome stands in. A tome is put on a lectern with
        // its seat (the lectern's position); Bedrock's collision may move it out of the lectern's
        // block, so the seat, not the current position, is the block to check. A tome set on a
        // lectern without a seat (an older save) takes the lectern it stands in, or the one the
        // engine lifted it onto.
        auto seat = lecternSeat(a);
        if (!seat) {
            auto at = game::blockPosOf(m->getFeetPos());
            if (isLecternAt(region, at)) seat = at;
            else if (isLecternAt(region, BlockPos{at.x, at.y - 1, at.z})) seat = BlockPos{at.x, at.y - 1, at.z};
            if (seat) ge::data(a, "lectern_pos") = Json::array({seat->x, seat->y, seat->z});
        }
        if (seat && isLecternAt(region, *seat)) {
            auto at = *seat;
            // it rests there, still (Java: the tome only turns to the lectern's facing)
            Vec3 rest{at.x + 0.5f, float(at.y) + kLecternTop, at.z + 0.5f};
            setMotion(a, Vec3{0.0f, 0.0f, 0.0f});
            if (distSqr(m->getFeetPos(), rest) > 0.0025) teleportTo(a, rest);
            // face the lectern's FACING (Bedrock: direction 0 south .. 3 east -> yaw)
            int   dir = lecternDirection(region.getBlock(at));
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
        // the lectern is gone (broken, replaced): the tome wakes up
        log().info("lich: a death tome is off its lectern ({} at its seat)", seat ? game::blockIdAt(region, *seat) : std::string("no lectern"));
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
            // Mob#convertTo keeps the custom name and the persistence the source required (Bedrock
            // has no query for it: a named rising zombie, a name tag making a mob persistent, or one
            // the unit marked persistent)
            auto const& name = z->getNameTag();
            auto const& keep = ge::data(a, "lich:persistent");
            if (!name.empty()) zombie->setNameTag(name);
            if (!name.empty() || (keep.is_boolean() && keep.get<bool>())) zombie->setPersistent();
        }
        rising().erase(uidOf(a));
        discardLater(a);
        log().info("lich: a rising zombie became a zombie");
    }
}

} // namespace

// ---- the shared operations ------------------------------------------------------------------------------

// The per-actor fields live in the actor's saved document (gf::entity::data), so they survive a
// chunk unload and a restart; the maps are a cache rebuilt from it.

Mob* minionMaster(Actor const& minion) {
    auto uid = uidOf(minion);
    auto it  = masters().find(uid);
    if (it == masters().end()) {
        auto const& saved = ge::data(const_cast<Actor&>(minion), "lich:master");
        if (!saved.is_number_integer()) return nullptr;
        it = masters().emplace(uid, saved.get<std::int64_t>()).first;
    }
    auto* m = mobById(it->second);
    return m && isLich(*m) ? m : nullptr;
}

void setMinionMaster(Actor const& minion, Mob* master) {
    auto& saved = ge::data(const_cast<Actor&>(minion), "lich:master");
    if (master) {
        masters()[uidOf(minion)] = uidOf(*master);
        saved                    = uidOf(*master);
    } else {
        masters().erase(uidOf(minion));
        saved = nullptr;
    }
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
    auto uid = uidOf(tome);
    auto it  = lecterns().find(uid);
    if (it == lecterns().end()) {
        if (!isType(tome, kTome)) return false;
        auto const& saved = ge::data(const_cast<Actor&>(tome), "on_lectern");
        it                = lecterns().emplace(uid, saved.is_boolean() && saved.get<bool>()).first;
    }
    return it->second;
}

void setTomeOnLectern(Actor& tome, bool on, BlockPos const* seat) {
    if (!on) ge::data(tome, "lectern_pos") = nullptr;
    else if (seat) {
        ge::data(tome, "lectern_pos") = Json::array({seat->x, seat->y, seat->z});
        teleportTo(tome, Vec3{seat->x + 0.5f, float(seat->y) + kLecternTop, seat->z + 0.5f});
        setMotion(tome, Vec3{0.0f, 0.0f, 0.0f});
    }
    lecterns()[uidOf(tome)]      = on;
    ge::data(tome, "on_lectern") = on;
    // the BP's gf:lectern group: no AI while seated; its gf:persistent group (added with it, never
    // removed): LichTowerWingRoom's tomeMimic.setPersistenceRequired(), for good
    ge::setProperty(tome, "gf:on_lectern", on);
    if (on) tome.setPersistent();
}

int risingTicks(Actor const& zombie) {
    auto uid = uidOf(zombie);
    auto it  = rising().find(uid);
    if (it == rising().end()) {
        if (!isType(zombie, kRising)) return 0;
        auto const& saved = ge::data(const_cast<Actor&>(zombie), "rising_ticks");
        it                = rising().emplace(uid, saved.is_number_integer() ? saved.get<int>() : 0).first;
    }
    return it->second;
}

void setRisingTicks(Actor& zombie, int ticks) {
    int old                    = risingTicks(zombie);
    rising()[uidOf(zombie)]    = ticks;
    ge::data(zombie, "rising_ticks") = ticks;
    ge::setProperty(zombie, "twilightforest:rising_ticks", std::min(ticks, 130));
    if (old != ticks || ticks == 0) applyRisingSize(zombie, ticks);
}

Mob* spawnLecternTome(BlockSource& region, BlockPos const& pos) {
    if (!isLecternAt(region, pos)) return nullptr;
    // facing the lectern (HorizontalDirectionalBlock.FACING#toYRot; Bedrock: 0 south .. 3 east)
    int  dir  = lecternDirection(region.getBlock(pos));
    Mob* tome = spawnMob(region, kTome, Vec3{pos.x + 0.5f, float(pos.y) + kLecternTop, pos.z + 0.5f}, static_cast<float>(dir) * 90.0F);
    if (!tome) return nullptr;
    setTomeOnLectern(*tome, true, &pos);
    return tome;
}

void registerMobs() {
    game::db::onWorldClosed([] {
        masters().clear();
        lecterns().clear();
        rising().clear();
    });
    // MiscEvents#addTomesToLecterns: a Death Tome spawn egg used on a lectern without a book puts a
    // tome mimic on it
    ia::onUseOn({.block = "minecraft:lectern", .item = "twilightforest:death_tome_spawn_egg"}, [](ia::UseOnContext const& c) {
        auto& region = c.player.getDimensionBlockSource();
        BlockPos pos{c.pos.x, c.pos.y, c.pos.z};
        auto*    actor = region.getBlockEntity(pos);
        if (actor && actor->getType() == BlockActorType::Lectern && !static_cast<LecternBlockActor*>(actor)->mBook->isNull()) return ia::Result::Pass;
        if (!c.firstEvent) return ia::Result::Consume;
        if (auto* dim = &c.player.getDimension()) gf::fx::sound(*dim, "item.book.put", Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 1.0F, 1.0F);
        bool  creative = c.player.isCreative();
        auto  uid      = uidOf(c.player);
        ia::nextTick([pos, dim = c.pos.dim, uid, creative] {
            auto* player = static_cast<Player*>(actorById(uid));
            if (!player || dimOf(*player) != dim) return;
            auto& r = player->getDimensionBlockSource();
            if (!spawnLecternTome(r, pos)) return;
            log().info("lich: a death tome was put on a lectern");
            if (creative) return;
            auto& held = const_cast<ItemStack&>(player->getSelectedItem());
            if (game::items::idOf(held) == "twilightforest:death_tome_spawn_egg") {
                held.remove(1);
                player->sendInventory(false);
            }
        });
        return ia::Result::Consume;
    });
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
        // the Zombie's natural armor 2 (tests/golden/mobs.json; Bedrock mobs have no armor
        // attribute): CombatRules#getDamageAfterAbsorb with toughness 0
        if ((isType(d.victim, kMinion) || isType(d.victim, kRising)) && d.amount > 0 && !damageTypeIn("minecraft:bypasses_armor", d.type)) {
            constexpr float kArmor = 2.0F;
            float           f      = std::clamp(kArmor - d.amount / 2.0F, kArmor * 0.2F, 20.0F);
            d.amount *= 1.0F - f / 25.0F;
        }
        return gf::combat::Verdict::Pass;
    });
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (isType(d.victim, kTome)) {
            if (auto* m = asMob(d.victim); m && isServerActor(*m)) tomeDealt(*m, d);
        } else if (isType(d.victim, kMinion)) {
            auto* m = asMob(d.victim);
            if (!m || !isServerActor(*m)) return;
            if (d.attacker && isLich(*d.attacker)) {
                // LichMinion#hurt by its Lich: back to the previous target, sped up and stronger
                if (Actor* prev = targetOf(*m)) setLastHurtBy(*m, prev);
                addEffect(*m, 1, 200, 2); // MOVEMENT_SPEED III
                addEffect(*m, 5, 200, 1); // DAMAGE_BOOST II
            }
            // Zombie#hurt (super.hurt of LichMinion#hurt): reinforcements on Hard
            if (m->isAlive()) callReinforcements(*m, d.attacker);
        }
    });
}

} // namespace gf::feature::lich

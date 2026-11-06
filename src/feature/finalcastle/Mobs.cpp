// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/Adherent.java
//   src/main/java/twilightforest/entity/monster/HarbingerCube.java
//   src/main/java/twilightforest/entity/RovingCube.java
//   src/main/java/twilightforest/entity/ai/goal/CubeMoveToRedstoneSymbolsGoal.java
//   src/main/java/twilightforest/entity/ai/goal/CubeCenterOnSymbolGoal.java
//   src/main/java/twilightforest/entity/boss/PlateauBoss.java
//   src/main/java/twilightforest/block/entity/spawner/FinalBossSpawnerBlockEntity.java
//   src/main/java/twilightforest/events/RegistrationEvents.java (registerSpawnPlacements)
//   net/minecraft/world/entity/ai/goal/RangedAttackGoal.java (vanilla, the adherent's goal)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30's castle mobs. Their vanilla goals (float, restrict / flee sun, stroll, look, the hurt-by and
// nearest-player targets) are the BP's behaviours (assets/pack/bp/entities/finalcastle); what TF
// adds is native here: the adherent's RangedAttackGoal with a nature bolt (U17's projectile, shot
// by id: a missing type is skipped at runtime), the roving cube's two symbol goals and its
// annihilation particles, the plateau boss through F8's boss kit and the final boss spawner that
// never spawns. None of them spawns naturally in 4.8 (no castle spawn lists, no eggs); the spawn
// placements are TF's (ON_GROUND, MOTION_BLOCKING_NO_LEAVES, Monster#checkMonsterSpawnRules).
//
// Bedrock: RovingCube#aiStep's particles are client-side in Java (3 a tick at the cube's previous
// position); here the server emits them through gf::fx at the current position, so vanilla clients
// see them too.
#include "feature/finalcastle/Mobs.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "core/finalcastle/Mobs.h"
#include "feature/boss/Boss.h"
#include "feature/entity/Entity.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/finalcastle/W0Internals.h"
#include "feature/ext/worldgen/Spawning.h"
#include "feature/fx/Fx.h"
#include "feature/projectile/Projectile.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"

namespace gf::feature::finalcastle::mobs {

namespace fe = gf::feature::entity;
namespace fm = gf::feature::entity::mobs;
namespace cf = gf::core::finalcastle;
namespace ge = gf::entity;

namespace {

std::atomic<int> gAdherentShots{0};

// ---- shared ---------------------------------------------------------------------------------------

Mob* mobOf(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

BlockPos blockPosOf(Actor const& a) {
    auto p = a.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
}

/// Monster#checkMonsterSpawnRules: not on peaceful, and dark enough unless a spawner places it.
bool checkMonsterSpawnRules(BlockSource& region, std::string const&, BlockPos const& pos, gf::spawn::SpawnReason reason, gf::core::jrand::RandomSource& random) {
    auto level = ll::service::getLevel();
    if (!level || level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful) return false;
    if (reason == gf::spawn::SpawnReason::Spawner || reason == gf::spawn::SpawnReason::TrialSpawner) return true;
    return gf::world::ext::isDarkEnoughToSpawn(region, pos, random);
}

// ---- adherent ---------------------------------------------------------------------------------------

/// Adherent#performRangedAttack: a nature bolt at the target's eyes - 1.1, raised by a fifth of the
/// horizontal distance; velocity 0.6, inaccuracy 10 - difficulty * 4; the ghast's shoot sound.
void performRangedAttack(Mob& self, Actor& target) {
    auto& random = fe::randomOf(self);
    auto  feet   = self.getFeetPos();
    // playSound(GHAST_SHOOT, 1.0F, 1.0F / (random.nextFloat() * 0.4F + 0.8F)) comes first
    gf::fx::sound(self.getDimension(), "minecraft:entity.ghast.shoot", feet, 1.0F, 1.0F / (random.nextFloat() * 0.4F + 0.8F));
    int  difficulty = 2;
    if (auto level = ll::service::getLevel()) difficulty = static_cast<int>(level->getDifficulty());
    auto tf   = target.getFeetPos();
    // new NatureBolt(level, this): TFThrowable at the owner's eyes - 0.1 (gf::projectile::shoot's start)
    double boltY = fe::eyeOf(self).y - 0.1;
    auto   shot  = cf::adherentShot(cf::DVec{feet.x, feet.y, feet.z}, boltY, cf::DVec{tf.x, tf.y, tf.z}, fe::eyeOf(target).y - tf.y, difficulty);
    gf::projectile::shoot(self, id::NatureBolt, Vec3{static_cast<float>(shot.direction.x), static_cast<float>(shot.direction.y), static_cast<float>(shot.direction.z)},
                          shot.velocity, shot.inaccuracy);
    ++gAdherentShots;
}

/// RangedAttackGoal(adherent, 1.0, 60, 10.0F) (vanilla).
class RangedAttackGoal final : public ge::Goal {
public:
    RangedAttackGoal(Mob& mob, double speed, int intervalMin, int intervalMax, float radius)
    : mUid(fe::uidOf(mob)), mSpeed(speed), mMin(intervalMin), mMax(intervalMax), mRadius(radius), mRadiusSqr(radius * radius) {
        flags = Move | Look;
    }
    bool canUse() override {
        auto* mob = mobOf(mUid);
        if (!mob) return false;
        auto* t = fm::target(*mob);
        if (t && t->isAlive()) {
            mTarget = fe::uidOf(*t);
            return true;
        }
        return false;
    }
    bool canContinueToUse() override {
        if (canUse()) return true;
        auto* mob = mobOf(mUid);
        auto* t   = mTarget ? fe::actorById(mTarget) : nullptr;
        return mob && t && t->isAlive() && !fm::navigationDone(*mob);
    }
    void stop() override {
        mTarget     = 0;
        mSeeTime    = 0;
        mAttackTime = -1;
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* mob = mobOf(mUid);
        auto* t   = mTarget ? fe::actorById(mTarget) : nullptr;
        if (!mob || !t) return;
        double d0   = fm::distanceSqr(*mob, t->getFeetPos());
        bool   sees = fm::hasLineOfSight(*mob, *t);
        if (sees) ++mSeeTime;
        else mSeeTime = 0;
        if (!(d0 > mRadiusSqr) && mSeeTime >= 5) fm::stopNavigation(*mob);
        else fm::moveTo(*mob, t->getFeetPos(), mSpeed);
        fm::lookAt(*mob, *t, 30.0F, 30.0F);
        if (--mAttackTime == 0) {
            if (!sees) return;
            performRangedAttack(*mob, *t);
            mAttackTime = cf::rangedIntervalAfterShot(d0, mRadius, mMin, mMax);
        } else if (mAttackTime < 0) {
            mAttackTime = cf::rangedIntervalOnReset(d0, mRadius, mMin, mMax);
        }
    }

private:
    std::int64_t mUid;
    std::int64_t mTarget = 0;
    double       mSpeed;
    int          mMin, mMax;
    float        mRadius, mRadiusSqr;
    int          mSeeTime    = 0;
    int          mAttackTime = -1;
};

void adherentSpawn(Actor& self) {
    auto* mob = fe::asMob(self);
    if (!mob) return;
    ge::addGoal(*mob, 4, std::make_shared<RangedAttackGoal>(*mob, 1.0, 60, 60, 10.0F));
}

// ---- roving cube ------------------------------------------------------------------------------------

std::unordered_map<std::int64_t, Symbol>& symbols() {
    static std::unordered_map<std::int64_t, Symbol> m;
    return m;
}

/// CubeMoveToRedstoneSymbolsGoal (priority 0, MOVE).
class MoveToSymbolsGoal final : public ge::Goal {
public:
    MoveToSymbolsGoal(Mob& mob, double speed) : mUid(fe::uidOf(mob)), mSpeed(speed) { flags = Move; }
    bool canUse() override {
        auto* mob = mobOf(mUid);
        if (!mob) return false;
        if (fe::randomOf(*mob).nextInt(20) != 0) return false;
        auto& region = mob->getDimensionBlockSource();
        auto  cube   = blockPosOf(*mob);
        auto  found  = cf::searchForRedstoneSymbol(cf::IPos{cube.x, cube.y, cube.z}, [&](cf::IPos p) { return isRedstoneSymbol(region, BlockPos{p.x, p.y, p.z}); });
        if (!found) return false;
        symbols()[mUid] = Symbol{true, *found};
        mTargetPos      = *found;
        return true;
    }
    bool canContinueToUse() override {
        auto* mob = mobOf(mUid);
        return mob && !fm::navigationDone(*mob);
    }
    void start() override {
        if (auto* mob = mobOf(mUid)) fm::moveTo(*mob, Vec3{float(mTargetPos.x), float(mTargetPos.y), float(mTargetPos.z)}, mSpeed);
    }

private:
    std::int64_t mUid;
    double       mSpeed;
    cf::IPos     mTargetPos;
};

/// CubeCenterOnSymbolGoal (priority 1, MOVE): near the symbol with the path done, inch to its centre.
class CenterOnSymbolGoal final : public ge::Goal {
public:
    CenterOnSymbolGoal(Mob& mob, double speed) : mUid(fe::uidOf(mob)), mSpeed(speed) { flags = Move; }
    bool canUse() override {
        auto* mob = mobOf(mUid);
        if (!mob) return false;
        mSymbol = symbolPos();
        if (!fm::navigationDone(*mob)) return false;
        return cf::isCloseToSymbol(distance(*mob));
    }
    bool canContinueToUse() override {
        auto* mob = mobOf(mUid);
        if (!mob) return false;
        // inch towards it
        auto t = cf::centerTarget(mSymbol);
        ge::ext::setWantedPosition(*mob, Vec3{float(t.x), float(t.y), float(t.z)}, mSpeed);
        return cf::keepsCentering(distance(*mob));
    }

private:
    /// symbolX / Y / Z: 0, 0, 0 until a symbol was found (Java's int fields)
    cf::IPos symbolPos() const {
        auto it = symbols().find(mUid);
        return it != symbols().end() ? it->second.pos : cf::IPos{};
    }
    double distance(Mob& mob) const {
        auto p = mob.getFeetPos();
        return cf::distanceFromSymbol(mSymbol, p.x, p.y, p.z);
    }
    std::int64_t mUid;
    double       mSpeed;
    cf::IPos     mSymbol;
};

void rovingCubeSpawn(Actor& self) {
    auto* mob = fe::asMob(self);
    if (!mob) return;
    // MoveControl for CubeCenterOnSymbolGoal's setWantedPosition (the path follower is the engine's)
    ge::setMoveControl(*mob, ge::MoveKind::Ground);
    ge::addGoal(*mob, 0, std::make_shared<MoveToSymbolsGoal>(*mob, 1.0));
    ge::addGoal(*mob, 1, std::make_shared<CenterOnSymbolGoal>(*mob, 1.0));
}

/// RovingCube#aiStep: 3 annihilate particles within +-2 around the eyes - 0.25.
void rovingCubeAiStep(Actor& self) {
    auto& r    = fe::randomOf(self);
    auto  p    = self.getFeetPos();
    float eye  = fe::eyeOf(self).y - p.y;
    auto& dim  = self.getDimension();
    for (int i = 0; i < 3; i++) {
        float px = (r.nextFloat() - r.nextFloat()) * 2.0F;
        float py = eye - 0.25F + (r.nextFloat() - r.nextFloat()) * 2.0F;
        float pz = (r.nextFloat() - r.nextFloat()) * 2.0F;
        gf::fx::particle(dim, id::Annihilate, Vec3{p.x + px, p.y + py, p.z + pz});
    }
}

} // namespace

bool isRedstoneSymbol(BlockSource& region, BlockPos const& pos) {
    auto at = [](cf::IPos p) { return BlockPos{p.x, p.y, p.z}; };
    return cf::isRedstoneSymbol(
        cf::IPos{pos.x, pos.y, pos.z},
        [&](cf::IPos p) { return region.hasChunksAt(at(p), 0, false); },
        [&](cf::IPos p) { return region.getBlock(at(p)).isAir(); },
        [&](cf::IPos p) { return game::blockIdAt(region, at(p)) == "minecraft:redstone_wire"; }
    );
}

std::optional<Symbol> symbolOf(std::int64_t uid) {
    auto it = symbols().find(uid);
    if (it == symbols().end()) return std::nullopt;
    return it->second;
}

void setSymbolForTest(std::int64_t uid, cf::IPos pos) { symbols()[uid] = Symbol{true, pos}; }

int adherentShotsForTest() { return gAdherentShots.load(); }

void installMobs() {
    auto forget = [](Actor& a, std::string const&, Actor*) { symbols().erase(fe::uidOf(a)); };
    ge::registerType(id::Adherent, {.onSpawn = adherentSpawn});
    ge::registerType(id::RovingCube, {.onSpawn = rovingCubeSpawn, .aiStep = rovingCubeAiStep, .die = forget});
    // RegistrationEvents#registerSpawnPlacements
    for (char const* m : {id::HarbingerCube, id::Adherent, id::RovingCube}) {
        gf::spawn::registerPlacement(m, gf::spawn::PlacementRule::OnGround, checkMonsterSpawnRules);
        gf::spawn::ext::setPlacementHeightmap(m, worldgen::Heightmap::MotionBlockingNoLeaves);
    }
    // PlateauBoss (BaseTFBoss): home final_castle (radius 30), canopy chest, a white bar
    gf::boss::registerBoss({
        .entityId        = id::PlateauBoss,
        .homeStructure   = id::FinalCastle,
        .lootTable       = "twilightforest:entities/plateau_boss",
        .deathChestBlock = id::CanopyChest,
        .bar             = {.rgb = 0xFFFFFF},
    });
    gf::boss::ext::setHomeRadius(id::PlateauBoss, 30);
    // FinalBossSpawnerBlockEntity: SHORT_RANGE (9), the annihilate particle, spawnMyBoss false
    gf::boss::registerSpawner({
        .blockId  = id::BossSpawner,
        .entityId = id::PlateauBoss,
        .range    = 9,
        .particle = id::Annihilate,
        .spawnFn  = [](BlockSource&, BlockPos const&) -> Mob* { return nullptr; }, // no spawning for you
    });
    game::db::onWorldClosed([] { symbols().clear(); });
}

void install() {
    installMobs();
    installWip();
}

} // namespace gf::feature::finalcastle::mobs

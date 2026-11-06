// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/Troll.java
//   src/main/java/twilightforest/entity/projectile/ThrownBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The troll and its thrown block. Vanilla goals (float, restrict / flee sun, stroll, look, hurt-by
// target) are the BP's behaviours; the combat goal (melee without a rock, ranged with one), the
// player target with the rock's follow range boost, the rock pick-up and the berry ripening on death
// are native. The rock the troll holds is drawn on its own model (the RP's rock render controller,
// property twilightforest:rock) instead of a riding ThrownBlock entity: the same picture on every
// client without an actor the projectile kit would fly.
#include "feature/caves/Caves.h"

#include <cmath>
#include <string>
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
#include "mc/world/phys/AABB.h"

#include "feature/caves/Goals.h"
#include "feature/combat/Combat.h"
#include "feature/death/Death.h"
#include "feature/entity/Entity.h"
#include "feature/fx/Fx.h"
#include "feature/projectile/Projectile.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::caves {

namespace {

constexpr char kRockKey[]     = "caves:rock";          ///< Troll RockState (a Bedrock block id; null: no rock)
constexpr char kCooldownKey[] = "caves:rock_cooldown"; ///< Troll RockCooldown
constexpr char kBlockKey[]    = "caves:block";         ///< ThrownBlock BlockState

/// The rock blocks the RP draws (#minecraft:base_stone_overworld), as twilightforest:rock /
/// twilightforest:block values (0: none / stone for a block outside the list).
constexpr char const* kRockBlocks[] = {"minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate"};

int rockIndex(std::string const& id) {
    for (int i = 0; i < 6; i++)
        if (id == kRockBlocks[i]) return i;
    return 0;
}

int difficultyId() {
    auto level = ll::service::getLevel();
    return level ? static_cast<int>(level->getDifficulty()) : 2;
}

std::string rockOf(Actor& troll) {
    auto const& r = gf::entity::data(troll, kRockKey);
    return r.is_string() ? r.get<std::string>() : std::string();
}

int cooldown(Actor& troll) {
    auto& c = gf::entity::data(troll, kCooldownKey);
    if (!c.is_number_integer()) c = 300 + fe::randomOf(troll).nextInt(100);
    return c.get<int>();
}

void setCooldown(Actor& troll, int v) { gf::entity::data(troll, kCooldownKey) = v; }

/// Troll#setHasRock (the flag is the RP's rock; the follow range boost is the target goal's).
void setRock(Actor& troll, std::string const& blockId) {
    if (blockId.empty()) gf::entity::data(troll, kRockKey) = nullptr;
    else gf::entity::data(troll, kRockKey) = blockId;
    gf::entity::setProperty(troll, "twilightforest:rock", blockId.empty() ? 0 : rockIndex(blockId) + 1);
}

/// Level#clip(OUTLINE) from `from` to `to`: the first block with a shape on the way.
BlockPos clipBlock(BlockSource& region, Vec3 const& from, Vec3 const& to, BlockPos const& fallback) {
    double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    double len   = std::sqrt(dx * dx + dy * dy + dz * dz);
    int    steps = std::max(1, static_cast<int>(len / 0.05));
    for (int i = 0; i <= steps; i++) {
        double   t = static_cast<double>(i) / steps;
        BlockPos p{static_cast<int>(std::floor(from.x + dx * t)), static_cast<int>(std::floor(from.y + dy * t)), static_cast<int>(std::floor(from.z + dz * t))};
        if (!region.getBlock(p).isAir()) return p;
    }
    return fallback;
}

/// Troll#tick, server side: the take-block step (EnderMan.EndermanTakeBlockGoal#tick copy).
void tryTakeRock(Mob& troll) {
    if (!rockOf(troll).empty() || !mobs::target(troll)) return;
    int cd = cooldown(troll);
    if (cd > 0) {
        setCooldown(troll, cd - 1);
        return;
    }
    auto&        random = fe::randomOf(troll);
    auto&        region = troll.getDimensionBlockSource();
    auto         p      = troll.getFeetPos();
    int          i      = static_cast<int>(std::floor(p.x - 2.0 + random.nextDouble() * 4.0));
    int          j      = static_cast<int>(std::floor(p.y + random.nextDouble() * 3.0));
    int          k      = static_cast<int>(std::floor(p.z - 2.0 + random.nextDouble() * 4.0));
    BlockPos     blockpos{i, j, k};
    Block const& blockstate = region.getBlock(blockpos);
    Vec3         vec3{static_cast<float>(std::floor(p.x) + 0.5), static_cast<float>(j + 0.5), static_cast<float>(std::floor(p.z) + 0.5)};
    Vec3         vec31{static_cast<float>(i + 0.5), static_cast<float>(j + 0.5), static_cast<float>(k + 0.5)};
    BlockPos     hit  = clipBlock(region, vec3, vec31, blockpos);
    bool         flag = hit == blockpos;
    std::string  rock;
    if (structure::blocks::inTag(blockstate, "minecraft:base_stone_overworld") && flag) {
        rock = blockstate.getTypeName();
        game::clearBlock(region, blockpos);
    }
    if (!rock.empty()) {
        setRock(troll, rock);
        gf::fx::sound(troll.getDimension(), "entity.twilightforest.troll.grab_rock", troll.getFeetPos());
        gf::log().info("caves: troll grabbed a rock ({})", rock);
    }
}

/// Troll#performRangedAttack.
void throwRock(Mob& troll, Actor& target) {
    std::string rock = rockOf(troll);
    if (rock.empty()) return;
    auto   from = troll.getFeetPos();
    auto   to   = target.getFeetPos();
    auto   box  = target.getAABB();
    double d0   = to.x - from.x;
    double d1   = box.min.y + (box.max.y - box.min.y) / 3.0F - (troll.getEyePos().y - 0.1F); // the ThrownBlock spawns at the eyes - 0.1
    double d2   = to.z - from.z;
    double d3   = std::sqrt(static_cast<float>(d0 * d0 + d2 * d2));
    Vec3   dir{static_cast<float>(d0), static_cast<float>(d1 + d3 * 0.2), static_cast<float>(d2)};
    if (auto* blocc = gf::projectile::shoot(troll, kThrownBlock, dir, 1.6F, static_cast<float>(4 - difficultyId()), nlohmann::json{{"block", rock}})) {
        gf::entity::data(*blocc, kBlockKey) = rock;
        gf::entity::setProperty(*blocc, "twilightforest:block", rockIndex(rock));
    }
    auto& random = fe::randomOf(troll);
    gf::fx::sound(troll.getDimension(), "entity.twilightforest.troll.throw_rock", from, 1.0F, 1.0F / (random.nextFloat() * 0.4F + 0.8F));
    setRock(troll, "");
    setCooldown(troll, 300 + random.nextInt(100));
    gf::log().info("caves: troll threw a rock");
}

/// Troll#setCombatTask: the ranged goal while it holds a rock, else the melee goal (priority 4).
class TrollCombatGoal final : public gf::entity::Goal {
public:
    explicit TrollCombatGoal(Mob& troll)
    : mUid(fe::uidOf(troll)),
      mRanged(troll, 1.0, 20, 60, 15.0F, [](Mob& self, Actor& target, float) { throwRock(self, target); }),
      mMelee(troll, 1.2, false) {
        flags = Move | Look;
    }
    bool canUse() override { return current().canUse(); }
    bool canContinueToUse() override { return withRock() == mRunningRanged && current().canContinueToUse(); }
    bool requiresUpdateEveryTick() override { return true; }
    void start() override {
        mRunningRanged = withRock();
        current().start();
    }
    void stop() override { (mRunningRanged ? static_cast<gf::entity::Goal&>(mRanged) : mMelee).stop(); }
    void tick() override { current().tick(); }

private:
    [[nodiscard]] bool withRock() const {
        auto* m = mobOf(mUid);
        return m && !rockOf(*m).empty();
    }
    gf::entity::Goal& current() { return withRock() ? static_cast<gf::entity::Goal&>(mRanged) : mMelee; }
    std::int64_t      mUid;
    RangedAttackGoal  mRanged;
    MeleeAttackGoal   mMelee;
    bool              mRunningRanged = false;
};

/// Troll#ripenTrollBerNearby over the death animation (deathTime 5, 10, 15, 20).
struct Ripening {
    int          dim = 0;
    BlockPos     pos{0, 0, 0};
    std::int64_t seed = 0;
    int          deathTime = 0;
};
std::vector<Ripening>& ripenings() {
    static std::vector<Ripening> r;
    return r;
}

void ripenTick() {
    auto& list = ripenings();
    for (auto it = list.begin(); it != list.end();) {
        auto& r = *it;
        r.deathTime++;
        if (r.deathTime % 5 == 0) {
            if (auto* region = game::regionOf(r.dim)) {
                core::jrand::LegacyRandom random(r.seed + r.deathTime);
                int                       offset = r.deathTime / 5;
                constexpr int             range  = 12;
                for (int x = r.pos.x - range; x <= r.pos.x + range; x++)
                    for (int y = r.pos.y - range; y <= r.pos.y + range; y++)
                        for (int z = r.pos.z - range; z <= r.pos.z + range; z++) {
                            BlockPos p{x, y, z};
                            if (game::blockIdAt(*region, p) != kUnripeTrollber) continue;
                            if (!random.nextBoolean() || std::abs(x + y + z) % 5 != offset) continue;
                            if (auto const* ber = game::blockWith(kTrollber)) game::setBlock(*region, p, *ber);
                            // LevelEvent.PARTICLES_MOBBLOCK_SPAWN
                            for (int i = 0; i < 20; i++) {
                                Vec3 at{x + random.nextFloat(), y + random.nextFloat(), z + random.nextFloat()};
                                gf::fx::particle(region->getDimension(), "minecraft:smoke", at);
                                gf::fx::particle(region->getDimension(), "minecraft:flame", at);
                            }
                        }
            }
        }
        if (r.deathTime >= 20) it = list.erase(it);
        else ++it;
    }
}

} // namespace

bool trollHasRock(Actor& troll) { return !rockOf(troll).empty(); }
void setTrollRockCooldownForTest(Actor& troll, int cd) { setCooldown(troll, cd); }
void giveTrollRockForTest(Mob& troll, std::string const& blockId) { setRock(troll, blockId); }
void trollThrowForTest(Mob& troll, Actor& target) { throwRock(troll, target); }

std::string thrownBlockState(Actor& thrownBlock) {
    auto const& b = gf::entity::data(thrownBlock, kBlockKey);
    if (b.is_string()) return b.get<std::string>();
    auto const& init = gf::entity::data(thrownBlock, "init");
    return init.is_object() ? init.value("block", std::string("minecraft:stone")) : std::string("minecraft:stone");
}

void installTroll() {
    gf::entity::Behaviour b;
    b.onSpawn = [](Actor& self) {
        auto* troll = fe::asMob(self);
        if (!troll) return;
        (void)cooldown(self); // this.rockCooldown = 300 + nextInt(100)
        auto rock = rockOf(self);
        gf::entity::setProperty(self, "twilightforest:rock", rock.empty() ? 0 : rockIndex(rock) + 1);
        gf::entity::addGoal(*troll, 4, std::make_shared<TrollCombatGoal>(*troll));
        // NearestAttackableTargetGoal(Player, true): follow range 16, + 8 while holding a rock
        gf::entity::addTargetGoal(*troll, 2, std::make_shared<NearestPlayerTargetGoal>(*troll, [](Mob& m) { return rockOf(m).empty() ? 16.0 : 24.0; }));
    };
    b.tick = [](Actor& self) {
        if (auto* troll = fe::asMob(self); troll && troll->isAlive()) tryTakeRock(*troll);
    };
    b.die = [](Actor& self, std::string const&, Actor*) {
        auto p = self.getFeetPos();
        ripenings().push_back({static_cast<int>(self.getDimensionId()),
                               BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))},
                               fe::randomOf(self).nextLong(), 0});
    };
    gf::entity::registerType(kTroll, b);
    fe::onLevelTick(ripenTick);
    gf::spawn::registerPlacement(kTroll, gf::spawn::PlacementRule::OnGround);

    // ThrownBlock (TFThrowable: gravity 0.03)
    gf::projectile::Spec spec;
    spec.gravity     = 0.03f;
    spec.drag        = 0.99f;
    spec.onHitEntity = [](Actor& p, Actor& target) {
        // a living non-troll target: 6 thrown_block damage (no attacker), else the block flies on
        if (!fe::asMob(target) || target.getTypeName() == kTroll) return false;
        gf::combat::hurt(target, 6.0f, "twilightforest:thrown_block");
        gf::fx::particle(p.getDimension(), "minecraft:block", p.getFeetPos(), Vec3{}, 20, nlohmann::json{{"block", thrownBlockState(p)}});
        gf::log().info("caves: a thrown block hit {}", target.getTypeName());
        return true;
    };
    spec.onHitBlock = [](Actor& p, BlockPos const&, int) {
        gf::fx::particle(p.getDimension(), "minecraft:block", p.getFeetPos(), Vec3{}, 20, nlohmann::json{{"block", thrownBlockState(p)}});
        return true;
    };
    gf::projectile::registerType(kThrownBlock, spec);
}

} // namespace gf::feature::caves

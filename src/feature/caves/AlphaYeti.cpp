// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/AlphaYeti.java
//   src/main/java/twilightforest/entity/ai/goal/YetiRampageGoal.java
//   src/main/java/twilightforest/entity/ai/goal/YetiTiredGoal.java
//   src/main/java/twilightforest/block/entity/spawner/AlphaYetiSpawnerBlockEntity.java
//   src/main/java/twilightforest/util/EntityUtil.java (canDestroyBlock)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Alpha Yeti on F8's boss kit: ice bombs from afar while it cannot rampage (projectiles do it
// no harm then), a rampage once anything hurt it (jumps, falling ice, ice bombs in all directions,
// the ground pound when it lands from a jump or drop, blocks broken around it), then 100 tired ticks in which it is
// vulnerable; grabs and throws its target like the yeti. Float, stroll, look and the hurt-by
// target are BP behaviours; the rampage's snow ring and the tired splash are the RP's particle
// emitters driven by the twilightforest:rampaging / twilightforest:tired properties.
#include "feature/caves/Caves.h"

#include <cmath>
#include <string>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/vanilla_components/HorizontalCollisionFlagComponent.h"
#include "mc/deps/vanilla_components/VerticalCollisionFlagComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/caves/Goals.h"
#include "feature/caves/Structures.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/projectile/Projectile.h"
#include "feature/spawn/Spawn.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::caves {

namespace {

/// The per-yeti server state (RAMPAGE_FLAG / TIRED_FLAG mirror the properties; canRampage and the
/// collision counter are plain fields in TF, not saved).
struct AlphaState {
    bool         canRampage = false;
    bool         rampaging  = false;
    bool         tired      = false;
    int          collisionCounter = 0;
    bool         wasOnGround = true;
    float        lastY       = 0.0F;
    bool         fell        = false; // Java's fallDistance > 0: it moved down since it left the ground
    std::int64_t lastTarget = 0;
};
std::unordered_map<std::int64_t, AlphaState>& states() {
    static std::unordered_map<std::int64_t, AlphaState> s;
    return s;
}
AlphaState& stateOf(Actor& yeti) { return states()[fe::uidOf(yeti)]; }

int difficultyId() {
    auto level = ll::service::getLevel();
    return level ? static_cast<int>(level->getDifficulty()) : 2;
}

bool mobGriefing() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    GameRuleId griefing;
    griefing.mValue = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
    return level->getGameRules().getBool(griefing, true);
}

void setRampaging(Actor& yeti, bool rampaging) {
    stateOf(yeti).rampaging = rampaging;
    gf::entity::setProperty(yeti, "twilightforest:rampaging", rampaging);
}

/// AlphaYeti#setTired: also clears canRampage.
void setTired(Actor& yeti, bool tired) {
    auto& s      = stateOf(yeti);
    s.tired      = tired;
    s.canRampage = false;
    gf::entity::setProperty(yeti, "twilightforest:tired", tired);
}

/// #minecraft:is_projectile (vanilla 1.21.1 members, plus what gf::tags adds: TF's own types).
bool isProjectileDamage(std::string const& type) {
    static char const* const kVanilla[] = {"minecraft:arrow",   "minecraft:trident",       "minecraft:mob_projectile", "minecraft:unattributed_fireball",
                                           "minecraft:fireball", "minecraft:wither_skull", "minecraft:thrown",          "minecraft:wind_charge"};
    for (auto const* v : kVanilla)
        if (type == v) return true;
    return gf::tags::has(gf::tags::Registry::DamageType, "minecraft:is_projectile", type);
}

/// The ice sound (vol 1, pitch 1 / (r x 0.4 + 0.8)).
void iceSound(Mob& yeti) {
    auto& r = fe::randomOf(yeti);
    gf::fx::sound(yeti.getDimension(), "entity.twilightforest.alpha_yeti.ice", yeti.getFeetPos(), 1.0F, 1.0F / (r.nextFloat() * 0.4F + 0.8F));
}

/// `be instanceof Container` of Java's block entities (BaseContainerBlockEntity, the
/// ContainerSingleItem jukebox / decorated pot, the chiseled bookshelf). Bedrock's item frames are
/// block actors where Java's are entities that destroyBlock never touches: they are spared too.
bool isContainer(BlockActor const& be) {
    switch (be.getType()) {
    case BlockActorType::Furnace:
    case BlockActorType::BlastFurnace:
    case BlockActorType::Smoker:
    case BlockActorType::Chest:
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::Hopper:
    case BlockActorType::ShulkerBox:
    case BlockActorType::BarrelBlock:
    case BlockActorType::BrewingStand:
    case BlockActorType::Crafter:
    case BlockActorType::Jukebox:
    case BlockActorType::ChiseledBookshelf:
    case BlockActorType::DecoratedPot:
    case BlockActorType::ItemFrame:
    case BlockActorType::GlowItemFrame: return true;
    default: return false;
    }
}

/// EntityUtil#canDestroyBlock (hardness 0..50, not air, no Container block entity: signs, banners,
/// beds, skulls and spawners are destroyed like in TF; TF's custom containers have no block actor
/// here, gf::inventory knows them).
bool canDestroyBlock(BlockSource& region, BlockPos const& pos) {
    Block const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    float hardness = b.getBlockType().getDestroySpeed();
    if (!(hardness >= 0.0F && hardness < 50.0F)) return false;
    if (auto* be = region.getBlockEntity(pos); be && isContainer(*be)) return false;
    if (b.getBlockType().mNameInfo->mNamespaceName.get() != "minecraft" && gf::inventory::find(region, pos)) return false;
    return true;
}

/// AlphaYeti#destroyBlocksInAABB.
void destroyBlocksInAABB(Mob& yeti, AABB const& box) {
    if (!mobGriefing()) return;
    auto  level  = ll::service::getLevel();
    auto& region = yeti.getDimensionBlockSource();
    if (!level) return;
    // WorldUtil#getAllInBB: the box corners cast to int (toward zero)
    for (int x = static_cast<int>(box.min.x); x <= static_cast<int>(box.max.x); x++)
        for (int y = static_cast<int>(box.min.y); y <= static_cast<int>(box.max.y); y++)
            for (int z = static_cast<int>(box.min.z); z <= static_cast<int>(box.max.z); z++) {
                BlockPos p{x, y, z};
                if (!canDestroyBlock(region, p)) continue;
                BlockChangeContext ctx{};
                level->destroyBlock(region, p, false, ctx);
            }
}

/// AlphaYeti#makeBlockFall.
void makeBlockFall(Mob& yeti, BlockPos const& pos, int hangTime) {
    auto& region = yeti.getDimensionBlockSource();
    auto  id     = game::blockIdAt(region, pos);
    spawnFallingIce(region, pos, id, hangTime);
    game::clearBlock(region, pos);
}

/// AlphaYeti#makeRandomBlockFall.
void makeRandomBlockFall(Mob& yeti, int range, int hangTime) {
    if (!mobGriefing()) return;
    auto& r  = fe::randomOf(yeti);
    auto  p  = yeti.getFeetPos();
    int   a  = r.nextInt(range);
    int   b  = r.nextInt(range);
    int   bx = static_cast<int>(std::floor(p.x)) + a - b;
    a        = r.nextInt(range);
    b        = r.nextInt(range);
    int bz   = static_cast<int>(std::floor(p.z)) + a - b;
    int by   = static_cast<int>(std::floor(yeti.getEyePos().y));
    alphaMakeBlockFallAbove(yeti, BlockPos{bx, by, bz}, hangTime);
}

/// The ranged attack (an ice bomb) while it cannot rampage.
void performRangedAttack(Mob& yeti, Actor& target) {
    if (stateOf(yeti).canRampage) return;
    auto   from = yeti.getFeetPos();
    auto   to   = target.getFeetPos();
    auto   box  = target.getAABB();
    double d0   = to.x - from.x;
    double d1   = box.min.y + (box.max.y - box.min.y) / 3.0F - (yeti.getEyePos().y - 0.1F);
    double d2   = to.z - from.z;
    double d3   = std::sqrt(static_cast<float>(d0 * d0 + d2 * d2));
    gf::projectile::shoot(yeti, kThrownIce, Vec3{static_cast<float>(d0), static_cast<float>(d1 + d3 * 0.2), static_cast<float>(d2)}, 1.6F,
                          static_cast<float>(14 - difficultyId() * 4));
    iceSound(yeti);
}

/// AlphaYeti#hitNearbyEntities: the ground pound.
void hitNearbyEntities(Mob& yeti) {
    auto box = yeti.getAABB();
    AABB area{Vec3{box.min.x - 5.0f, box.min.y, box.min.z - 5.0f}, Vec3{box.max.x + 5.0f, box.max.y, box.max.z + 5.0f}};
    for (auto* e : fe::actorsIn(yeti.getDimensionBlockSource(), static_cast<int>(yeti.getDimensionId()), area)) {
        if (e == &yeti || !fe::asMob(*e)) continue;
        if (gf::combat::hurt(*e, 5.0F, "minecraft:mob_attack", &yeti, &yeti)) fe::push(*e, Vec3{0.0f, 0.4f, 0.0f});
    }
}

/// YetiTiredGoal(alpha, 100).
class YetiTiredGoal final : public gf::entity::Goal {
public:
    YetiTiredGoal(Mob& yeti, int duration) : mUid(fe::uidOf(yeti)), mDuration(duration) { flags = Move | Jump; }
    bool canUse() override {
        auto* m = mobOf(mUid);
        return m && stateOf(*m).tired;
    }
    bool canContinueToUse() override { return mTimer < mDuration; }
    bool isInterruptable() override { return false; }
    void start() override { mTimer = 0; }
    void stop() override {
        mTimer = 0;
        if (auto* m = mobOf(mUid)) setTired(*m, false);
    }
    void tick() override {
        auto* m = mobOf(mUid);
        if (!m) return;
        mobs::stopNavigation(*m); // MOVE: the BP stroll stays off it
        if (++mTimer % 10 == 0)
            gf::fx::sound(m->getDimension(), "entity.twilightforest.alpha_yeti.pant", m->getFeetPos(), 4.0F, 0.5F + fe::randomOf(*m).nextFloat() * 0.5F);
    }

private:
    std::int64_t mUid;
    int          mDuration;
    int          mTimer = 0;
};

/// YetiRampageGoal(alpha, 10, 180).
class YetiRampageGoal final : public gf::entity::Goal {
public:
    YetiRampageGoal(Mob& yeti, int timeout, int duration) : mUid(fe::uidOf(yeti)), mTimeOut(timeout), mMaxTimeOut(timeout), mDuration(duration) {
        flags = Move | Jump;
    }
    bool canUse() override {
        auto* m = mobOf(mUid);
        if (!m) return false;
        if (mobs::target(*m) && stateOf(*m).canRampage) mTimeOut--;
        return mTimeOut <= 0;
    }
    bool canContinueToUse() override { return mCurrent > 0; }
    void start() override {
        mCurrent = mDuration;
        auto* m  = mobOf(mUid);
        if (!m) return;
        setRampaging(*m, true);
        gf::fx::sound(m->getDimension(), "entity.twilightforest.alpha_yeti.roar", m->getFeetPos(), 4.0F, 0.5F + fe::randomOf(*m).nextFloat() * 0.5F);
        gf::log().info("caves: alpha yeti rampages");
    }
    void tick() override {
        mCurrent--;
        auto* m = mobOf(mUid);
        if (!m) return;
        auto& r = fe::randomOf(*m);
        if (auto* t = mobs::target(*m)) mobs::lookAt(*m, *t, 10.0F, 40.0F);
        if (m->isOnGround()) {
            fe::setMotion(*m, Vec3{0.0f, 0.4f, 0.0f});
            mobs::stopNavigation(*m);
        }
        auto box = m->getAABB();
        destroyBlocksInAABB(*m, AABB{Vec3{box.min.x - 1.0f, box.min.y - 2.0f + 2.0f, box.min.z - 1.0f}, Vec3{box.max.x + 1.0f, box.max.y + 2.0f + 2.0f, box.max.z + 1.0f}});
        // regular falling blocks, twice a second
        if (mCurrent % 10 == 0) makeRandomBlockFall(*m, 30, 80);
        // blocks target players, one every second
        if (mCurrent % 20 == 0)
            if (auto* t = mobs::target(*m)) {
                auto p = t->getFeetPos();
                alphaMakeBlockFallAbove(*m, BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))}, 40);
            }
        // blocks that fall close to the yeti, twice a second near the end of the rampage
        if (mCurrent < 40 && mCurrent % 10 == 0) makeRandomBlockFall(*m, 15, 40);
        if (mCurrent % 20 == 0) {
            float  x     = 0.5F + r.nextFloat() * 0.5F;
            float  y     = 0.5F + r.nextFloat() * 0.3F;
            float  angle = r.nextFloat() * 360.0F; // Vec3#yRot takes radians: TF's degrees used as radians
            float  c = std::cos(angle), s = std::sin(angle);
            Vec3   vec{x * c, y, -x * s};
            float  speed = 0.4F + r.nextFloat() * 0.3F;
            gf::projectile::shoot(*m, kThrownIce, vec, speed, 0.0F);
            iceSound(*m);
        }
    }
    void stop() override {
        mTimeOut = mMaxTimeOut;
        auto* m  = mobOf(mUid);
        if (!m) return;
        setRampaging(*m, false);
        setTired(*m, true);
        gf::log().info("caves: alpha yeti tired");
    }

private:
    std::int64_t mUid;
    int          mTimeOut, mMaxTimeOut, mDuration;
    int          mCurrent = 0;
};

} // namespace

bool alphaCanRampage(Actor& yeti) { return stateOf(yeti).canRampage; }
bool alphaRampaging(Actor& yeti) { return stateOf(yeti).rampaging; }
bool alphaTired(Actor& yeti) { return stateOf(yeti).tired; }
void setAlphaCanRampageForTest(Actor& yeti, bool canRampage) { stateOf(yeti).canRampage = canRampage; }

bool alphaMakeBlockFallAbove(Mob& yeti, BlockPos const& pos, int hangTime) {
    auto& region = yeti.getDimensionBlockSource();
    for (int i = 1; i < 25; i++) {
        BlockPos up{pos.x, pos.y + i, pos.z};
        if (structure::blocks::inTag(region.getBlock(up), "minecraft:ice") && region.getBlock(BlockPos{up.x, up.y - 1, up.z}).isAir()) {
            makeBlockFall(yeti, up, hangTime);
            return true;
        }
    }
    return false;
}

void installAlphaYeti() {
    gf::entity::ext::markHostileMount(kAlphaYeti);
    gf::entity::Behaviour b;
    b.onSpawn = [](Actor& self) {
        auto* yeti = fe::asMob(self);
        if (!yeti) return;
        auto& s = stateOf(self);
        gf::entity::setProperty(self, "twilightforest:rampaging", s.rampaging);
        gf::entity::setProperty(self, "twilightforest:tired", s.tired);
        gf::entity::addGoal(*yeti, 1, std::make_shared<YetiTiredGoal>(*yeti, 100));
        gf::entity::addGoal(*yeti, 3, std::make_shared<YetiRampageGoal>(*yeti, 10, 180));
        struct AlphaRanged final : RangedAttackGoal {
            using RangedAttackGoal::RangedAttackGoal;
            bool canUse() override {
                // give us a chance to move to the next AI
                auto* m = mobOf(mUid);
                if (!m || fe::randomOf(*m).nextInt(50) <= 0) return false;
                auto* t = mobs::target(*m);
                return t && mobs::distanceSqr(*m, *t) >= 16.0 && RangedAttackGoal::canUse();
            }
        };
        gf::entity::addGoal(*yeti, 4, std::make_shared<AlphaRanged>(*yeti, 1.0, 40, 40, 40.0F, [](Mob& m, Actor& t, float) { performRangedAttack(m, t); }));
        gf::entity::addGoal(*yeti, 4, makeThrowRiderGoal(*yeti, true));
        gf::entity::addGoal(*yeti, 5, gf::entity::goals::goHome(*yeti, 1.25));
        gf::entity::addTargetGoal(*yeti, 2, std::make_shared<NearestPlayerTargetGoal>(*yeti, [](Mob&) { return 40.0; }));
    };
    b.aiStep = [](Actor& self) {
        auto* yeti = fe::asMob(self);
        if (!yeti) return;
        if (auto* rider = yeti->getFirstPassenger()) mobs::lookAt(*yeti, *rider, 100.0F, 100.0F);
    };
    b.tick = [](Actor& self) {
        auto* yeti = fe::asMob(self);
        if (!yeti || !yeti->isAlive()) return;
        auto& s = stateOf(self);
        // AlphaYeti#setTarget: the alert on a new target
        auto* t   = mobs::target(*yeti);
        auto  tid = t ? fe::uidOf(*t) : 0;
        if (tid != 0 && tid != s.lastTarget)
            gf::fx::sound(yeti->getDimension(), "entity.twilightforest.alpha_yeti.alert", yeti->getFeetPos(), 4.0F, 0.5F + fe::randomOf(*yeti).nextFloat() * 0.5F);
        s.lastTarget = tid;
        // customServerAiStep: collisions while rampaging
        auto& ctx = yeti->getEntityContext();
        if (s.rampaging && (ctx.hasComponent<HorizontalCollisionFlagComponent>() || ctx.hasComponent<VerticalCollisionFlagComponent>())) s.collisionCounter++;
        if (s.collisionCounter >= 15) {
            destroyBlocksInAABB(*yeti, yeti->getAABB());
            s.collisionCounter = 0;
        }
        // causeFallDamage while rampaging: Entity#checkFallDamage calls it on a landing with
        // fallDistance > 0 (any height; Bedrock fires no fall event below 3 blocks), i.e. after the
        // rampage's jumps or any drop, never on a step up or while it stays on the ground
        bool  onGround = yeti->isOnGround();
        float y        = yeti->getFeetPos().y;
        if ((!onGround || !s.wasOnGround) && y < s.lastY) s.fell = true;
        if (yeti->isInWater()) s.fell = false; // Entity#updateInWaterStateAndDoFluidPushing resets fallDistance
        if (onGround) {
            if (!s.wasOnGround && s.fell && s.rampaging) {
                iceSound(*yeti);
                hitNearbyEntities(*yeti);
            }
            s.fell = false;
        }
        s.lastY = y;
        s.wasOnGround = onGround;
    };
    b.hurt = [](Actor& self, float&, std::string const& type, Actor*) {
        auto& s = stateOf(self);
        // no arrow damage when in ranged mode
        if (!s.canRampage && !s.tired && isProjectileDamage(type)) return false;
        return true;
    };
    // AlphaYeti#hurt: canRampage once a hit actually landed (super.hurt returned true)
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (d.victim.getTypeName() != kAlphaYeti || d.amount <= 0.0F) return;
        auto& s = stateOf(d.victim);
        if (!s.canRampage) gf::log().info("caves: alpha yeti can rampage");
        s.canRampage = true;
    });
    b.die = [](Actor& self, std::string const&, Actor*) { states().erase(fe::uidOf(self)); };
    gf::game::db::onWorldClosed([] { states().clear(); });
    gf::entity::registerType(kAlphaYeti, b);

    gf::boss::registerBoss({
        .entityId        = kAlphaYeti,
        .homeStructure   = kYetiCave,
        .lootTable       = "twilightforest:entities/alpha_yeti",
        .deathChestBlock = "twilightforest:canopy_chest",
        .bar             = {.rgb = 0xB4F0F0},
    });
    gf::boss::ext::setHomeRadius(kAlphaYeti, 30);
    gf::boss::registerSpawner({.blockId = "twilightforest:alpha_yeti_boss_spawner", .entityId = kAlphaYeti, .range = 9, .particle = "minecraft:falling_water"});
    gf::spawn::registerPlacement(kAlphaYeti, gf::spawn::PlacementRule::OnGround);
}

} // namespace gf::feature::caves

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/ai/goal/ChargeAttackGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/BreathAttackGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/FlockToSameKindGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/PanicOnFlockDeathGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/AvoidAnyEntityGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/AttemptToGoHomeGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/AlwaysWatchTargetGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/RandomLookAroundIfBoredGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/SimplifiedAttackGoal.java
//   src/main/java/gloamingforest/entity/ai/control/NoClipMoveControl.java
//   src/main/java/gloamingforest/entity/ITFCharger.java
//   src/main/java/gloamingforest/entity/IBreathAttacker.java
//   src/main/java/gloamingforest/entity/EnforcedHomePoint.java
//   src/main/java/gloamingforest/util/entities/EntityUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The goal adapter of contract "entity": a Java GoalSelector per mob (goal and target selectors,
// flags, priorities, interruption, every-other-tick updates) run from F8's tick hook, and upstream's
// shared goals. Goals drive the engine's own NavigationComponent, LookControlComponent and the
// mob's motion; vanilla's RandomPos / GoalUtils helpers they need are translated here too.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/entity/components/LookControlComponent.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/pathfinder/Path.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/entity/World.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::entity {

namespace ge = gf::entity;

// ---- GoalSelector (net.minecraft.world.entity.ai.goal.GoalSelector) ---------------------------

namespace {
int flagIndex(unsigned flag) {
    switch (flag) {
    case ge::Goal::Move: return 0;
    case ge::Goal::Look: return 1;
    case ge::Goal::Jump: return 2;
    default: return 3;
    }
}
constexpr unsigned kFlags[] = {ge::Goal::Move, ge::Goal::Look, ge::Goal::Jump, ge::Goal::Target};
} // namespace

void GoalSelector::add(int priority, ge::GoalPtr goal) { mGoals.push_back({priority, std::move(goal), false}); }

bool GoalSelector::running(ge::Goal const* goal) const {
    for (auto const& w : mGoals)
        if (w.goal.get() == goal) return w.isRunning;
    return false;
}

void GoalSelector::tick(unsigned disabled) {
    // goalCleanup: stop goals with a disabled flag (a controlling mob passenger) or that can't continue
    for (std::size_t i = 0; i < mGoals.size(); ++i) {
        if (mGoals[i].isRunning && ((mGoals[i].goal->flags & disabled) || !mGoals[i].goal->canContinueToUse())) {
            mGoals[i].isRunning = false;
            mGoals[i].goal->stop();
        }
    }
    for (auto& locked : mLocked)
        if (locked >= 0 && (locked >= static_cast<int>(mGoals.size()) || !mGoals[locked].isRunning)) locked = -1;
    // goalUpdate (availableGoals keeps insertion order); indices, so a goal may add goals
    for (std::size_t i = 0; i < mGoals.size(); ++i) {
        if (mGoals[i].isRunning || (mGoals[i].goal->flags & disabled)) continue;
        bool replaceable = true;
        for (unsigned f : kFlags) {
            if (!(mGoals[i].goal->flags & f)) continue;
            int holder = mLocked[flagIndex(f)];
            // WrappedGoal#canBeReplacedBy: interruptable and the newcomer has a lower priority value
            if (holder >= 0 && !(mGoals[holder].goal->isInterruptable() && mGoals[i].priority < mGoals[holder].priority)) {
                replaceable = false;
                break;
            }
        }
        if (!replaceable || !mGoals[i].goal->canUse()) continue;
        for (unsigned f : kFlags) {
            if (!(mGoals[i].goal->flags & f)) continue;
            int& holder = mLocked[flagIndex(f)];
            if (holder >= 0 && mGoals[holder].isRunning) {
                mGoals[holder].isRunning = false;
                mGoals[holder].goal->stop();
            }
            holder = static_cast<int>(i);
        }
        mGoals[i].isRunning = true;
        mGoals[i].goal->start();
    }
    tickRunning(true);
}

void GoalSelector::tickRunning(bool force) {
    for (std::size_t i = 0; i < mGoals.size(); ++i)
        if (mGoals[i].isRunning && (force || mGoals[i].goal->requiresUpdateEveryTick())) mGoals[i].goal->tick();
}

void GoalSelector::stopAll() {
    for (auto& w : mGoals) {
        if (!w.isRunning) continue;
        w.isRunning = false;
        w.goal->stop();
    }
    for (auto& locked : mLocked) locked = -1;
}

// ---- mob helpers ----------------------------------------------------------------------------------

namespace mobs {

Actor* target(Mob& mob) {
    auto* t = actorById(mob.mTargetId->rawID);
    return t && t->isAlive() ? t : nullptr;
}

Actor* lastHurtByMob(Mob& mob) {
    auto* t = actorById(mob.mLastHurtByMobId->rawID);
    return t && t->isAlive() ? t : nullptr;
}

bool hasLineOfSight(Mob& mob, Actor& target) {
    if (&target.getDimension() != &mob.getDimension()) return false;
    auto   p  = target.getFeetPos();
    auto   m  = mob.getFeetPos();
    double dx = p.x - m.x, dy = p.y - m.y, dz = p.z - m.z;
    if (dx * dx + dy * dy + dz * dz > 128.0 * 128.0) return false; // LivingEntity#hasLineOfSight
    return mob.canSee(target.getEyePos(), ShapeType::Collision);
}

double distanceSqr(Actor const& a, Vec3 const& p) {
    auto q = a.getFeetPos();
    double      dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
    return dx * dx + dy * dy + dz * dz;
}

double distanceSqr(Actor const& a, Actor const& b) { return distanceSqr(a, b.getFeetPos()); }

namespace {
NavigationComponent* navOf(Mob& mob) {
    auto nav = mob.getEntityContext().tryGetComponent<NavigationComponent>();
    return nav ? nav.as_ptr() : nullptr;
}
} // namespace

/// Whether F8 steers the mob itself (MoveControl#setWantedPosition): a NoClip mob always (its
/// NoClipMoveControl ignores the world, the engine's navigation would path around it), any mob
/// without an engine navigation.
bool steeredByWanted(Mob& mob) {
    auto* s = findState(uidOf(mob));
    if (s && s->moveKind && *s->moveKind == ge::MoveKind::NoClip) return true;
    auto* nav = navOf(mob);
    return !nav || !nav->mNavigation.get();
}

bool moveTo(Mob& mob, Vec3 const& pos, double speed) {
    if (steeredByWanted(mob)) {
        if (auto* nav = navOf(mob); nav && nav->mNavigation.get()) nav->mNavigation.get()->stop(*nav, mob);
        ge::ext::setWantedPosition(mob, pos, speed);
        return true;
    }
    auto* nav = navOf(mob);
    return nav->mNavigation.get()->moveTo(*nav, mob, pos, static_cast<float>(speed));
}

// PathNavigation#createPath returns null below the world or when !canUpdatePath() (not on the
// ground, in a liquid or riding: GroundPathNavigation); otherwise the pathfinder's best partial
// path. Bedrock's Path has no callable destructor, so the path itself is not built here.
bool canCreatePath(Mob& mob, Vec3 const&) {
    auto* nav = navOf(mob);
    if (!nav || !nav->mNavigation.get() || steeredByWanted(mob)) return true;
    if (mob.getFeetPos().y < mob.getDimensionBlockSource().getMinHeight()) return false;
    return nav->mNavigation.get()->canUpdatePath(mob);
}

bool navigationDone(Mob& mob) {
    auto* nav = navOf(mob);
    if (!nav || steeredByWanted(mob)) {
        auto* s = findState(uidOf(mob));
        return !s || !s->hasWanted;
    }
    auto const& path = nav->mPath.get();
    return !path || path->mIndex >= path->mNodes->size();
}

void stopNavigation(Mob& mob) {
    if (auto* nav = navOf(mob); nav && nav->mNavigation.get()) nav->mNavigation.get()->stop(*nav, mob);
    if (auto* s = findState(uidOf(mob))) s->hasWanted = false;
}

void setSpeedModifier(Mob& mob, double speed) {
    if (auto* nav = navOf(mob)) nav->mSpeed = static_cast<float>(speed);
}

void lookAt(Mob& mob, Vec3 const& pos, float yMax, float xMax) {
    auto look = mob.getEntityContext().tryGetComponent<LookControlComponent>();
    if (!look) return;
    look->mWantedPosition    = pos;
    look->mHasWantedPosition = true;
    look->mYMax              = yMax;
    look->mXMax              = xMax;
}

void lookAt(Mob& mob, Actor& target, float yMax, float xMax) {
    auto look = mob.getEntityContext().tryGetComponent<LookControlComponent>();
    if (!look) return;
    look->setLookAtPosition(&target, yMax, xMax);
}

namespace {
struct Box {
    double x0, y0, z0, x1, y1, z1;
    [[nodiscard]] bool intersects(Box const& o) const {
        return x0 < o.x1 && x1 > o.x0 && y0 < o.y1 && y1 > o.y0 && z0 < o.z1 && z1 > o.z0;
    }
};
Box boxOf(Actor const& a) {
    auto const& b = a.getAABB();
    return {b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z};
}
} // namespace

bool isWithinMeleeAttackRange(Mob& mob, Actor& target) {
    static double const reach = std::sqrt(2.04F) - 0.6F; // Mob#DEFAULT_ATTACK_REACH
    Box a = boxOf(mob);
    a.x0 -= reach, a.z0 -= reach, a.x1 += reach, a.z1 += reach;
    return a.intersects(boxOf(target));
}

bool doHurtTarget(Mob& mob, Actor& target) {
    return static_cast<bool>(mob.doHurtTarget(&target, SharedTypes::Legacy::ActorDamageCause::EntityAttack));
}

bool attackable(Actor& actor) {
    if (!actor.isAlive()) return false;
    if (actor.isPlayer()) {
        auto& p = static_cast<Player&>(actor);
        if (p.isCreative() || p.isSpectator()) return false;
    }
    return true;
}

namespace {
BlockPos containing(double x, double y, double z) {
    return BlockPos{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
}

/// The block's collision box at `pos` (world coordinates; no volume: no collision).
AABB collisionOf(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    return block.getBlockType().getCollisionShape(block, region, pos, nullptr);
}

/// BlockComponentDirectData#mIsOpaqueFullBlock (BlockState#isSolidRender), checked once against
/// stone / glass / air; the engine's _isSolid() if that layout does not hold.
bool opaqueFullBlock(Block const& block) {
    static int layout = -1;
    auto       read   = [](Block const& b) -> bool { return b.mDirectData->mIsOpaqueFullBlock; };
    if (layout < 0) {
        auto const* stone = game::blockWith("minecraft:stone");
        auto const* glass = game::blockWith("minecraft:glass");
        auto const* air   = game::blockWith("minecraft:air");
        layout            = stone && glass && air && read(*stone) && !read(*glass) && !read(*air) ? 1 : 0;
        if (!layout) log().warn("entity: the opaque-full-block flag of the block data does not hold; isSolidRender uses _isSolid");
    }
    return layout ? read(block) : block._isSolid();
}

bool idIs(std::string const& id, std::initializer_list<char const*> ids) {
    for (auto const* i : ids)
        if (id == i) return true;
    return false;
}

/// WalkNodeEvaluator#isBurningBlock: fire, magma, a lit campfire, a lava cauldron.
bool burning(Block const& block, std::string const& id) {
    if (idIs(id, {"minecraft:fire", "minecraft:soul_fire", "minecraft:magma"})) return true;
    auto states = game::statesOf(block);
    if (idIs(id, {"minecraft:campfire", "minecraft:soul_campfire"})) {
        auto it = states.find("extinguished");
        return it == states.end() || (it->second != "true" && it->second != "1");
    }
    if (id == "minecraft:cauldron" || id == "minecraft:lava_cauldron") {
        auto it = states.find("cauldron_liquid");
        return id == "minecraft:lava_cauldron" || (it != states.end() && it->second == "lava");
    }
    return false;
}

bool stateTrue(Block const& block, char const* name) {
    auto states = game::statesOf(block);
    auto it     = states.find(name);
    return it != states.end() && (it->second == "true" || it->second == "1");
}
} // namespace

bool emptyCollision(BlockSource& region, BlockPos const& pos) {
    AABB b = collisionOf(region, pos);
    return b.max.x - b.min.x <= 1e-6f || b.max.y - b.min.y <= 1e-6f || b.max.z - b.min.z <= 1e-6f;
}

bool legacySolid(BlockSource& region, BlockPos const& pos) {
    if (emptyCollision(region, pos)) return false;
    AABB   b    = collisionOf(region, pos);
    double size = ((b.max.x - b.min.x) + (b.max.y - b.min.y) + (b.max.z - b.min.z)) / 3.0;
    return size >= 0.7291666666666666 || (b.max.y - b.min.y) >= 1.0;
}

bool stableDestination(BlockSource& region, BlockPos const& pos) { return opaqueFullBlock(region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z})); }

PathType pathTypeFromState(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (block.isAir()) return PathType::Open;
    std::string id   = game::blockId(block);
    auto const& type = block.getBlockType();
    if (id.ends_with("trapdoor") || idIs(id, {"minecraft:waterlily", "minecraft:big_dripleaf"})) return PathType::Trapdoor;
    if (id == "minecraft:powder_snow") return PathType::PowderSnow;
    if (idIs(id, {"minecraft:cactus", "minecraft:sweet_berry_bush"})) return PathType::DamageOther;
    if (id == "minecraft:honey_block") return PathType::StickyHoney;
    if (id == "minecraft:cocoa") return PathType::Cocoa;
    if (idIs(id, {"minecraft:wither_rose", "minecraft:pointed_dripstone"})) return PathType::DamageCautious;
    if (idIs(id, {"minecraft:lava", "minecraft:flowing_lava"})) return PathType::Lava;
    if (burning(block, id)) return PathType::DamageFire;
    if (id.ends_with("_door") || id == "minecraft:wooden_door") {
        if (stateTrue(block, "open_bit")) return PathType::DoorOpen;
        return id == "minecraft:iron_door" ? PathType::DoorIronClosed : PathType::DoorWoodClosed;
    }
    if (type.isRailBlock()) return PathType::Rail;
    if (id.find("leaves") != std::string::npos) return PathType::Leaves;
    if (id.ends_with("fence_gate")) {
        // FenceGateBlock: closed is FENCE, open falls through to the pathfindable check
        if (!stateTrue(block, "open_bit")) return PathType::Fence;
    } else if (type.isFenceBlock() || type.isWallBlock()) {
        return PathType::Fence;
    }
    // BlockBehaviour#isPathfindable(LAND): no full collision cube (stairs and slabs refuse it)
    bool pathfindable = emptyCollision(region, pos);
    if (!pathfindable) {
        AABB b = collisionOf(region, pos);
        bool full = b.max.x - b.min.x >= 1.0f && b.max.y - b.min.y >= 1.0f && b.max.z - b.min.z >= 1.0f;
        pathfindable = !full && !type.isSlabBlock() && id.find("stairs") == std::string::npos;
        if (id == "minecraft:snow_layer") {
            auto st = game::statesOf(block);
            auto it = st.find("height");
            pathfindable = it != st.end() && std::atoi(it->second.c_str()) + 1 < 5; // LAYERS < 5
        }
    }
    if (!pathfindable) return PathType::Blocked;
    bool water = idIs(id, {"minecraft:water", "minecraft:flowing_water"}) || idIs(game::blockId(region.getLiquidBlock(pos)), {"minecraft:water", "minecraft:flowing_water"});
    return water ? PathType::Water : PathType::Open;
}

PathType pathTypeStatic(BlockSource& region, BlockPos const& pos) {
    PathType type = pathTypeFromState(region, pos);
    if (type != PathType::Open || pos.y < region.getMinHeight() + 1) return type;
    switch (pathTypeFromState(region, BlockPos{pos.x, pos.y - 1, pos.z})) {
    case PathType::Open:
    case PathType::Water:
    case PathType::Lava:
    case PathType::Walkable: return PathType::Open;
    case PathType::DamageFire: return PathType::DamageFire;
    case PathType::DamageOther: return PathType::DamageOther;
    case PathType::StickyHoney: return PathType::StickyHoney;
    case PathType::PowderSnow: return PathType::DangerPowderSnow;
    case PathType::DamageCautious: return PathType::DamageCautious;
    case PathType::Trapdoor: return PathType::DangerTrapdoor;
    default: break;
    }
    // WalkNodeEvaluator#checkNeighbourBlocks
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            for (int k = -1; k <= 1; ++k) {
                if (i == 0 && k == 0) continue;
                PathType n = pathTypeFromState(region, BlockPos{pos.x + i, pos.y + j, pos.z + k});
                if (n == PathType::DamageOther) return PathType::DangerOther;
                if (n == PathType::DamageFire || n == PathType::Lava) return PathType::DangerFire;
                if (n == PathType::Water) return PathType::WaterBorder;
                if (n == PathType::DamageCautious) return PathType::DamageCautious;
            }
    return PathType::Walkable;
}

float pathMalus(PathType type) {
    switch (type) {
    case PathType::Blocked:
    case PathType::PowderSnow:
    case PathType::Fence:
    case PathType::Lava:
    case PathType::DamageOther:
    case PathType::DoorWoodClosed:
    case PathType::DoorIronClosed:
    case PathType::Leaves: return -1.0F;
    case PathType::Water:
    case PathType::WaterBorder:
    case PathType::DangerFire:
    case PathType::DangerOther:
    case PathType::StickyHoney: return 8.0F;
    case PathType::DamageFire: return 16.0F;
    default: return 0.0F;
    }
}

bool hasMalus(BlockSource& region, BlockPos const& pos) { return pathMalus(pathTypeStatic(region, pos)) != 0.0F; }

bool isMonster(Actor const& actor) {
    static HashedString const monster("monster");
    return actor.hasCategory(ActorCategory::Monster) || actor.hasFamily(monster);
}

bool isAnimal(Actor const& actor) {
    static HashedString const animal("animal");
    return actor.hasCategory(ActorCategory::Animal) || actor.hasFamily(animal);
}

// LevelReader#getPathfindingCostFromLightLevels = brightness - 0.5, where Java's brightness rises
// with the light level (DimensionType#brightness); the engine's brightness at the position (night
// included) rises with it too, and only the order of the ten candidates matters.
float walkTargetValue(Mob& mob, BlockPos const& pos) {
    auto& region = mob.getDimensionBlockSource();
    if (isMonster(mob)) return 0.5F - region.getBrightness(pos); // Monster#getWalkTargetValue
    if (isAnimal(mob)) {
        auto below = game::blockIdAt(region, BlockPos{pos.x, pos.y - 1, pos.z});
        if (below == "minecraft:grass_block" || below == "minecraft:grass") return 10.0F; // Animal#getWalkTargetValue
        return region.getBrightness(pos) - 0.5F;
    }
    return 0.0F;
}

namespace {

/// DefaultRandomPos#generateRandomPosTowardDirection (no restriction: upstream's homes are goals).
std::optional<BlockPos> towardDirection(Mob& mob, BlockPos const& offset) {
    auto     m      = mob.getFeetPos();
    BlockPos pos    = containing(offset.x + m.x, offset.y + m.y, offset.z + m.z);
    auto&    region = mob.getDimensionBlockSource();
    if (pos.y < region.getMinHeight() || pos.y > region.getMaxHeight()) return std::nullopt; // GoalUtils#isOutsideLimits
    if (!region.hasChunksAt(pos, 1, false)) return std::nullopt;
    if (!stableDestination(region, pos) || hasMalus(region, pos)) return std::nullopt;
    return pos;
}

/// RandomPos#generateRandomPos: ten tries, the one with the best walk target value.
template <class Supplier>
std::optional<Vec3> generate(Mob& mob, Supplier&& supplier) {
    std::optional<BlockPos> best;
    double                  bestValue = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < 10; ++i) {
        auto p = supplier();
        if (!p) continue;
        double v = walkTargetValue(mob, *p);
        if (v > bestValue) bestValue = v, best = p;
    }
    if (!best) return std::nullopt;
    return Vec3{best->x + 0.5f, static_cast<float>(best->y), best->z + 0.5f};
}

/// RandomPos#generateRandomDirectionWithinRadians.
std::optional<BlockPos> directionWithinRadians(JavaRandom& r, int h, int v, double x, double z, double maxAngle) {
    double d0 = std::atan2(z, x) - static_cast<double>(static_cast<float>(std::numbers::pi / 2));
    double d1 = d0 + static_cast<double>(2.0F * r.nextFloat() - 1.0F) * maxAngle;
    double d2 = std::sqrt(r.nextDouble()) * static_cast<double>(static_cast<float>(std::numbers::sqrt2)) * static_cast<double>(h);
    double d3 = -d2 * std::sin(d1);
    double d4 = d2 * std::cos(d1);
    if (std::abs(d3) > h || std::abs(d4) > h) return std::nullopt;
    int i = r.nextInt(2 * v + 1) - v;
    return containing(d3, i, d4);
}
} // namespace

std::optional<Vec3> randomPos(Mob& mob, int radius, int vertical) {
    auto& r = randomOf(mob);
    return generate(mob, [&]() -> std::optional<BlockPos> {
        // RandomPos#generateRandomDirection
        int x = r.nextInt(2 * radius + 1) - radius;
        int y = r.nextInt(2 * vertical + 1) - vertical;
        int z = r.nextInt(2 * radius + 1) - radius;
        return towardDirection(mob, BlockPos{x, y, z});
    });
}

std::optional<Vec3> randomPosAway(Mob& mob, int radius, int vertical, Vec3 const& away) {
    auto&  r  = randomOf(mob);
    auto   m  = mob.getFeetPos();
    double vx = m.x - away.x, vz = m.z - away.z;
    return generate(mob, [&]() -> std::optional<BlockPos> {
        auto dir = directionWithinRadians(r, radius, vertical, vx, vz, static_cast<float>(std::numbers::pi / 2));
        return dir ? towardDirection(mob, *dir) : std::nullopt;
    });
}

std::optional<Vec3> randomPosTowards(Mob& mob, int radius, int vertical, Vec3 const& towards, double maxAngle) {
    auto&  r  = randomOf(mob);
    auto   m  = mob.getFeetPos();
    double vx = towards.x - m.x, vz = towards.z - m.z;
    return generate(mob, [&]() -> std::optional<BlockPos> {
        auto dir = directionWithinRadians(r, radius, vertical, vx, vz, maxAngle);
        return dir ? towardDirection(mob, *dir) : std::nullopt;
    });
}

} // namespace mobs

// ---- the shared goals -----------------------------------------------------------------------------

namespace {

/// Base of the translated goals: the mob is looked up again on every call (rule R16).
class MobGoal : public ge::Goal {
public:
    explicit MobGoal(Mob& mob) : mId(uidOf(mob)) {}

protected:
    [[nodiscard]] Mob* mob() const {
        auto* a = actorById(mId);
        return a ? asMob(*a) : nullptr;
    }
    std::int64_t mId;
};

bool typeIs(Actor const& a, char const* id) { return a.getTypeName() == id; }

/// ITFCharger#setCharging / IBreathAttacker#setBreathing: a flag in the mob's data, synced to
/// companion renderers (ext: isCharging / isBreathing).
void setFlag(Mob& mob, char const* key, bool value) {
    auto& v = ge::data(mob, key);
    if (v.is_boolean() && v.get<bool>() == value) return;
    v = value;
    ge::sync(mob, key);
}

// ChargeAttackGoal
class ChargeGoal final : public MobGoal {
public:
    ChargeGoal(Mob& mob, ge::goals::ChargeSpec spec) : MobGoal(mob), mSpeed(static_cast<float>(spec.speed)), mCanBreak(spec.canBreak) {
        flags = Move | Look;
    }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto* t = mobs::target(*m);
        mTarget = t ? uidOf(*t) : 0;
        if (!t) return false;
        double distance = mobs::distanceSqr(*m, *t);
        // the minoshroom charges when the target is further away than the normal minotaur
        double bonus = typeIs(*m, "gloamingforest:minoshroom") ? 9.0 : 0.0;
        if (distance < kMinRangeSq + bonus || distance > kMaxRangeSq + bonus) return false;
        if (!m->isOnGround()) return false;
        Vec3 chargePos = findChargePoint(*m, *t);
        if (!mobs::hasLineOfSight(*m, *t)) return false;
        mChargePos = chargePos;
        return randomOf(*m).nextInt(kFreq) == 0;
    }
    void start() override {
        auto* m = mob();
        if (!m) return;
        mWindup = 15 + randomOf(*m).nextInt(30);
        m->setSprinting(true);
    }
    bool canContinueToUse() override {
        auto* m = mob();
        return m && (mWindup > 0 || !mobs::navigationDone(*m));
    }
    void tick() override {
        auto* m = mob();
        auto* t = actorById(mTarget);
        if (!m) return;
        // look where we're going
        mobs::lookAt(*m, Vec3{mChargePos.x, mChargePos.y - 1, mChargePos.z}, 10.0F, 40.0F);
        if (mWindup > 0) {
            if (--mWindup == 0) {
                // actually charge!
                mobs::moveTo(*m, mChargePos, mSpeed);
            } else {
                setFlag(*m, "entity:charging", true);
            }
        } else if (mCanBreak) {
            breakBlocks(*m);
        }
        if (!t) return;
        // attack the target when we get in range
        double w       = m->getAABB().max.x - m->getAABB().min.x;
        double tw      = t->getAABB().max.x - t->getAABB().min.x;
        double rangeSq = typeIs(*m, "gloamingforest:minoshroom") ? 5.0 : static_cast<float>(w * 2.0F * w * 2.0F) + tw;
        auto   tp      = t->getFeetPos();
        if (mobs::distanceSqr(*m, Vec3{tp.x, t->getAABB().min.y, tp.z}) <= rangeSq && !mHasAttacked) {
            mHasAttacked = true;
            mobs::doHurtTarget(*m, *t);
        }
    }
    void stop() override {
        mWindup      = 0;
        mTarget      = 0;
        mHasAttacked = false;
        if (auto* m = mob()) {
            m->setSprinting(false);
            setFlag(*m, "entity:charging", false);
        }
    }

private:
    static constexpr double kMinRangeSq = 16.0;
    static constexpr double kMaxRangeSq = 64.0;
    static constexpr int    kFreq       = 10;

    /// Finds a point that is overshoot blocks "beyond" the target from our position.
    static Vec3 findChargePoint(Actor const& attacker, Actor const& target) {
        auto   a        = attacker.getFeetPos();
        auto   b        = target.getFeetPos();
        double vecx     = b.x - a.x;
        double vecz     = b.z - a.z;
        auto   rangle   = static_cast<float>(std::atan2(vecz, vecx));
        double distance = std::sqrt(static_cast<float>(vecx * vecx + vecz * vecz));
        double overshoot = 2.1;
        double dx        = std::cos(rangle) * (distance + overshoot);
        double dz        = std::sin(rangle) * (distance + overshoot);
        return Vec3{static_cast<float>(a.x + dx), b.y, static_cast<float>(a.z + dz)};
    }

    /// EntityUtil#canDestroyBlock over the charger's box (the minoshroom's charge).
    static void breakBlocks(Mob& m) {
        auto level = ll::service::getLevel();
        if (!level) return;
        GameRuleId griefing;
        griefing.mValue = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
        if (!level->getGameRules().getBool(griefing, true)) return;
        auto const& bb     = m.getAABB();
        auto&       region = m.getDimensionBlockSource();
        BlockPos    min{static_cast<int>(std::floor(bb.min.x - 0.75)), static_cast<int>(std::floor(bb.min.y)), static_cast<int>(std::floor(bb.min.z - 0.75))};
        BlockPos    max{static_cast<int>(std::floor(bb.max.x + 0.75)), static_cast<int>(std::floor(bb.max.y + 0.15)), static_cast<int>(std::floor(bb.max.z + 0.75))};
        // EntityUtil#canDestroyBlock && getBlockEntity(pos) == null, destroyBlock(pos, true)
        (void)ge::ext::destroyBlocks(region, min, max, ge::ext::BreakRule::SpareBlockEntities, true);
    }

    float        mSpeed;
    bool         mCanBreak;
    std::int64_t mTarget = 0;
    Vec3         mChargePos{};
    int          mWindup      = 0;
    bool         mHasAttacked = false;
};

// BreathAttackGoal
class BreathGoal final : public MobGoal {
public:
    BreathGoal(Mob& mob, ge::goals::BreathSpec spec) : MobGoal(mob), mSpec(std::move(spec)) { flags = Move | Look | Jump; }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto* t = mobs::lastHurtByMob(*m);
        mTarget = t ? uidOf(*t) : 0;
        if (!t || m->distanceTo(*t) > mSpec.range || !mobs::hasLineOfSight(*m, *t) || !mobs::attackable(*t)) return false;
        mBreathPos = eyeOf(*t);
        return randomOf(*m).nextFloat() < mSpec.chance;
    }
    void start() override {
        mDurationLeft = mSpec.durationTicks;
        if (auto* m = mob()) setFlag(*m, "entity:breathing", true);
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* t = actorById(mTarget);
        return mDurationLeft > 0 && m && m->isAlive() && t && t->isAlive() && m->distanceTo(*t) <= mSpec.range
            && mobs::hasLineOfSight(*m, *t) && mobs::attackable(*t);
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        mDurationLeft--;
        // why do we need both of these?
        mobs::lookAt(*m, mBreathPos, 10.0F, 40.0F);
        faceVec(*m, mBreathPos, 100.0F, 100.0F);
        if ((mSpec.durationTicks - mDurationLeft) > 5) {
            // anyhoo, deal damage
            if (auto* target = headLookTarget(*m)) ge::ext::doBreathAttack(*m, *target, mSpec.damageType);
        }
    }
    void stop() override {
        mDurationLeft = 0;
        mTarget       = 0;
        if (auto* m = mob()) setFlag(*m, "entity:breathing", false);
    }

private:
    /// What, if anything, is the head currently looking at?
    static Actor* headLookTarget(Mob& m) {
        constexpr double range  = 30.0;
        constexpr double offset = 3.0;
        auto             p      = m.getFeetPos();
        Vec3             src{p.x, p.y + 0.25f, p.z};
        Vec3             look = m.getViewVector(1.0f);
        Vec3             dest{src.x + look.x * float(range), src.y + look.y * float(range), src.z + look.z * float(range)};
        auto             bb = m.getAABB();
        constexpr float  inf = 0.5f;
        AABB search{
            Vec3{bb.min.x + look.x * float(offset) - inf, bb.min.y + look.y * float(offset) - inf, bb.min.z + look.z * float(offset) - inf},
            Vec3{bb.max.x + look.x * float(offset) + inf, bb.max.y + look.y * float(offset) + inf, bb.max.z + look.z * float(offset) + inf}
        };
        Actor* pointed = nullptr;
        double hitDist = 0;
        for (auto* e : actorsIn(m.getDimensionBlockSource(), static_cast<int>(m.getDimensionId()), search)) {
            if (!e || e == &m || !e->isAlive() || !mobs::attackable(*e)) continue;
            // EntitySelector.LIVING_ENTITY_STILL_ALIVE: living entities only, so no GloamingPart (a
            // multipart mob is breathed on through its main box), ours or another mob's
            if (parentOfPart(*e) != 0) continue;
            if (!e->hasCategory(ActorCategory::Mob) && !e->isPlayer()) continue;
            auto const& b = e->getAABB();
            if (b.contains(src)) {
                if (0.0 < hitDist || hitDist == 0.0) {
                    pointed = e;
                    hitDist = 0.0;
                }
                continue;
            }
            if (auto t = clip(src, dest, b)) {
                double d = *t * range;
                if (d < hitDist || hitDist == 0.0) {
                    pointed = e;
                    hitDist = d;
                }
            }
        }
        return pointed;
    }
    /// AABB#clip: the segment parameter of the first hit (0..1).
    static std::optional<double> clip(Vec3 const& a, Vec3 const& b, AABB const& box) {
        double t0 = 0, t1 = 1;
        double av[3] = {a.x, a.y, a.z}, bv[3] = {b.x, b.y, b.z};
        double lo[3] = {box.min.x, box.min.y, box.min.z}, hi[3] = {box.max.x, box.max.y, box.max.z};
        for (int i = 0; i < 3; ++i) {
            double d = bv[i] - av[i];
            if (std::abs(d) < 1e-9) {
                if (av[i] < lo[i] || av[i] > hi[i]) return std::nullopt;
                continue;
            }
            double u = (lo[i] - av[i]) / d, v = (hi[i] - av[i]) / d;
            if (u > v) std::swap(u, v);
            t0 = std::max(t0, u);
            t1 = std::min(t1, v);
            if (t0 > t1) return std::nullopt;
        }
        return t0;
    }
    /// Face the head towards a specific vector.
    static void faceVec(Mob& m, Vec3 const& pos, float yawConstraint, float pitchConstraint) {
        auto   p        = m.getFeetPos();
        double xOffset  = pos.x - p.x;
        double zOffset  = pos.z - p.z;
        double yOffset  = (p.y + 0.25) - pos.y;
        double distance = std::sqrt(static_cast<float>(xOffset * xOffset + zOffset * zOffset));
        auto   xyAngle  = static_cast<float>((std::atan2(zOffset, xOffset) * 180.0) / std::numbers::pi) - 90.0F;
        auto   zdAngle  = static_cast<float>(-((std::atan2(yOffset, distance) * 180.0) / std::numbers::pi));
        auto   rot      = m.getRotation();
        float  pitch    = -updateRotation(rot.x, zdAngle, pitchConstraint);
        float  yaw      = updateRotation(rot.y, xyAngle, yawConstraint);
        const_cast<Vec2&>(m.getRotation()) = Vec2{pitch, yaw};
    }
    static float updateRotation(float current, float target, float maxDelta) {
        float delta = std::remainder(target - current, 360.0f);
        delta       = std::clamp(delta, -maxDelta, maxDelta);
        return current + delta;
    }

    ge::goals::BreathSpec mSpec;
    std::int64_t          mTarget = 0;
    Vec3                  mBreathPos{};
    int                   mDurationLeft = 0;
};

/// Same-type entities in `box` around the mob (getEntitiesOfClass(this.getClass(), ...)).
std::vector<Mob*> sameKind(Mob& m, double h, double v) {
    auto const&       bb = m.getAABB();
    AABB              box{Vec3{float(bb.min.x - h), float(bb.min.y - v), float(bb.min.z - h)}, Vec3{float(bb.max.x + h), float(bb.max.y + v), float(bb.max.z + h)}};
    std::vector<Mob*> out;
    for (auto* e : m.getDimensionBlockSource().getEntities(box, 0.0f))
        if (e && e->getTypeName() == m.getTypeName())
            if (auto* mm = asMob(*e)) out.push_back(mm);
    return out;
}

// FlockToSameKindGoal
class FlockGoal final : public MobGoal {
public:
    FlockGoal(Mob& mob, double speed) : MobGoal(mob), mSpeed(speed) {}
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        if (typeIs(*m, "gloamingforest:kobold")) {
            auto const& held = m->getCarriedItem();
            if (!game::items::isEmpty(held) && tags::has(tags::Registry::Item, "gloamingforest:kobold_pacification_breads", game::items::idOf(held)))
                return false;
        }
        if (randomOf(*m).nextInt(40) != 0) return false;
        auto flock = sameKind(*m, 16.0, 4.0);
        if (flock.size() > 5) return false;
        int    n = 0;
        double fx = 0, fy = 0, fz = 0;
        for (auto* f : flock) {
            ++n;
            auto p = f->getFeetPos();
            fx += p.x, fy += p.y, fz += p.z;
        }
        if (n == 0) return false;
        fx /= n, fy /= n, fz /= n;
        Vec3 at{float(fx), float(fy), float(fz)};
        if (mobs::distanceSqr(*m, at) < kMinDist) return false;
        mFlock = at;
        mHas   = true;
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        if (!m || !mHas) return false;
        double d = mobs::distanceSqr(*m, mFlock);
        return d >= kMinDist && d <= kMaxDist;
    }
    void start() override { mMoveTimer = 0; }
    void stop() override { mHas = false; }
    void tick() override {
        auto* m = mob();
        if (!m || !mHas) return;
        if (--mMoveTimer <= 0) {
            mMoveTimer = 10;
            mobs::moveTo(*m, mFlock, mSpeed);
        }
    }

private:
    static constexpr double kMaxDist = 256.0;
    static constexpr double kMinDist = 25.0;
    double                  mSpeed;
    Vec3                    mFlock{};
    bool                    mHas       = false;
    int                     mMoveTimer = 0;
};

// PanicOnFlockDeathGoal
class PanicGoal final : public MobGoal {
public:
    PanicGoal(Mob& mob, double speed) : MobGoal(mob), mSpeed(static_cast<float>(speed)) { flags = Move; }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        bool yikes = mFleeTimer > 0;
        // check if any of us is dead within 4 squares
        for (auto* f : sameKind(*m, 4.0, 2.0)) {
            if (f->getDeathTime() > 0) {
                yikes = true;
                break;
            }
        }
        if (!yikes) return false;
        auto target = mobs::randomPos(*m, 5, 4);
        if (!target) return false;
        mFlee = *target;
        return true;
    }
    void start() override {
        mFleeTimer = 40;
        auto* m    = mob();
        if (!m) return;
        mobs::moveTo(*m, mFlee, mSpeed);
        // panic flag for kobold animations
        if (typeIs(*m, "gloamingforest:kobold")) setFlag(*m, "entity:panicked", true);
    }
    bool canContinueToUse() override {
        auto* m = mob();
        return m && mFleeTimer > 0 && !mobs::navigationDone(*m);
    }
    void tick() override { mFleeTimer--; }
    void stop() override {
        mFleeTimer -= 20;
        auto* m = mob();
        if (m && typeIs(*m, "gloamingforest:kobold")) setFlag(*m, "entity:panicked", false);
    }

private:
    float mSpeed;
    Vec3  mFlee{};
    int   mFleeTimer = 0;
};

// AvoidAnyEntityGoal (vanilla AvoidEntityGoal with the 1.12 any-entity selector)
class AvoidGoal final : public MobGoal {
public:
    AvoidGoal(Mob& mob, std::function<bool(Actor&)> avoid, float distance, double walkSpeed, double sprintSpeed)
    : MobGoal(mob),
      mAvoid(std::move(avoid)),
      mDistance(distance),
      mFarSpeed(walkSpeed),
      mNearSpeed(sprintSpeed) {
        flags = Move;
    }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto const& bb = m->getAABB();
        AABB        box{Vec3{bb.min.x - mDistance, bb.min.y - 3.0f, bb.min.z - mDistance}, Vec3{bb.max.x + mDistance, bb.max.y + 3.0f, bb.max.z + mDistance}};
        Actor*      first = nullptr;
        for (auto* e : actorsIn(m->getDimensionBlockSource(), static_cast<int>(m->getDimensionId()), box)) {
            if (!e || e == m || !e->isAlive()) continue;
            if (!mobs::attackable(*e) && e->isPlayer()) continue; // NO_CREATIVE_OR_SPECTATOR
            if (!mobs::hasLineOfSight(*m, *e)) continue;
            if (auto* other = asMob(*e); other && m->isAlliedTo(other)) continue; // !isAlliedTo
            if (mAvoid && !mAvoid(*e)) continue;
            first = e;
            break;
        }
        if (!first) return false;
        mAvoidTarget = uidOf(*first);
        // upstream's AvoidAnyEntityGoal: away from the mob's own position (sic)
        auto pos = mobs::randomPosAway(*m, 16, 7, m->getFeetPos());
        if (!pos) return false;
        if (mobs::distanceSqr(*first, *pos) < mobs::distanceSqr(*first, *m)) return false;
        mPos = *pos;
        // this.path = navigation.createPath(...); return this.path != null
        return mobs::canCreatePath(*m, mPos);
    }
    bool canContinueToUse() override {
        auto* m = mob();
        return m && !mobs::navigationDone(*m);
    }
    void start() override {
        if (auto* m = mob()) mobs::moveTo(*m, mPos, mFarSpeed);
    }
    void stop() override { mAvoidTarget = 0; }
    void tick() override {
        auto* m = mob();
        auto* t = actorById(mAvoidTarget);
        if (!m || !t) return;
        mobs::setSpeedModifier(*m, mobs::distanceSqr(*m, *t) < 49.0 ? mNearSpeed : mFarSpeed);
    }

private:
    std::function<bool(Actor&)> mAvoid;
    float                       mDistance;
    double                      mFarSpeed, mNearSpeed;
    std::int64_t                mAvoidTarget = 0;
    Vec3                        mPos{};
};

// AttemptToGoHomeGoal
class GoHomeGoal final : public MobGoal {
public:
    GoHomeGoal(Mob& mob, double speed) : MobGoal(mob), mSpeed(speed) { flags = Move; }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto h = ge::home(*m);
        if (!h || withinHomeArea(*m, *h)) return false;
        Vec3 bottomCenter{h->pos.x + 0.5f, static_cast<float>(h->pos.y), h->pos.z + 0.5f};
        auto pos = mobs::randomPosTowards(*m, 16, 7, bottomCenter, static_cast<float>(std::numbers::pi / 2));
        if (!pos) return false;
        mWanted = *pos;
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        return m && !mobs::navigationDone(*m);
    }
    void start() override {
        auto* m = mob();
        if (!m) return;
        mobs::stopNavigation(*m);
        mobs::moveTo(*m, mWanted, mSpeed);
    }

private:
    /// EnforcedHomePoint#isMobWithinHomeArea.
    static bool withinHomeArea(Mob& m, ge::Home const& h) {
        if (h.pos.dim != static_cast<int>(m.getDimensionId())) return true; // !isRestrictionPointValid
        auto   bp = m.getFeetBlockPos();
        double dx = h.pos.x - bp.x, dy = h.pos.y - bp.y, dz = h.pos.z - bp.z;
        return dx * dx + dy * dy + dz * dz < static_cast<double>(h.radius * h.radius);
    }
    double mSpeed;
    Vec3   mWanted{};
};

// AlwaysWatchTargetGoal
class WatchTargetGoal final : public MobGoal {
public:
    using MobGoal::MobGoal;
    bool requiresUpdateEveryTick() override { return true; }
    bool canUse() override {
        auto* m = mob();
        return m && mobs::target(*m);
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        if (auto* t = mobs::target(*m)) mobs::lookAt(*m, *t, 100.0F, 100.0F);
    }
};

// RandomLookAroundIfBoredGoal
class LookIfBoredGoal final : public MobGoal {
public:
    explicit LookIfBoredGoal(Mob& mob) : MobGoal(mob) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mob();
        return m && !mobs::target(*m) && randomOf(*m).nextFloat() < 0.02F;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        return m && !mobs::target(*m) && mLookTime >= 0;
    }
    void start() override {
        auto* m = mob();
        if (!m) return;
        double d0  = 6.283185307179586 * randomOf(*m).nextDouble();
        mRelX      = std::cos(d0);
        mRelZ      = std::sin(d0);
        mLookTime  = 20 + randomOf(*m).nextInt(20);
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        --mLookTime;
        auto p = m->getFeetPos();
        mobs::lookAt(*m, Vec3{float(p.x + mRelX), m->getEyePos().y, float(p.z + mRelZ)}, 10.0F, 40.0F);
    }

private:
    double mRelX = 0, mRelZ = 0;
    int    mLookTime = 0;
};

// SimplifiedAttackGoal
class SimplifiedAttackGoal final : public MobGoal {
public:
    using MobGoal::MobGoal;
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto* t = mobs::target(*m);
        return t && mobs::isWithinMeleeAttackRange(*m, *t);
    }
    bool requiresUpdateEveryTick() override { return true; }
    void start() override { mAttackTick = 0; }
    void stop() override { mAttackTick = 0; }
    void tick() override {
        if (mAttackTick > 0) {
            mAttackTick--;
            return;
        }
        auto* m = mob();
        if (!m) return;
        auto* t = mobs::target(*m);
        if (!t) {
            stop();
            return;
        }
        if (mAttackTick <= 0 && mobs::isWithinMeleeAttackRange(*m, *t) && mobs::hasLineOfSight(*m, *t)) {
            mAttackTick = 20; // adjustedTickDelay(20) of an every-tick goal
            m->swing(ActorSwingSource::Attack, HandSlot::Mainhand);
            mobs::doHurtTarget(*m, *t);
        }
    }

private:
    int mAttackTick = 0;
};

/// Mth#wrapDegrees.
float wrapDegrees(float v) {
    float f = std::fmod(v, 360.0F);
    if (f >= 180.0F) f -= 360.0F;
    if (f < -180.0F) f += 360.0F;
    return f;
}

/// MoveControl#rotlerp.
float rotlerp(float from, float to, float max) {
    float f = std::clamp(wrapDegrees(to - from), -max, max);
    float g = from + f;
    if (g < 0.0F) g += 360.0F;
    else if (g > 360.0F) g -= 360.0F;
    return g;
}

/// Entity#setNoGravity(true) while the engine keeps its gravity flag: the engine's gravity (0.08
/// a tick for mobs, LivingEntity#getDefaultGravity) is given back, so only the 0.98 drag stays.
void counterGravity(Mob& m) {
    if (m.isOnGround() || !m.getStatusFlag(ActorFlags::HasGravity)) return;
    push(m, Vec3{0.0f, 0.08f, 0.0f});
}

/// LivingEntity#travel's moveRelative for the controller's (xxa = 0, yya, zza): onGround the
/// friction-influenced speed (the zza's speed itself on normal blocks: 0.216 / 0.6^3 = 1), in the
/// air 0.02 (LivingEntity#getFlyingSpeed); the engine adds friction and gravity.
void moveRelative(Mob& m, float yya, float zza, float amount) {
    double len = std::sqrt(double(yya) * yya + double(zza) * zza);
    if (len < 1.0E-7) return;
    double scale = (len > 1.0 ? 1.0 / len : 1.0) * amount;
    double yaw   = m.getRotation().y * std::numbers::pi / 180.0;
    double fz    = zza * scale;
    push(m, Vec3{float(-std::sin(yaw) * fz), float(yya * scale), float(std::cos(yaw) * fz)});
}

/// Turns the body towards the wanted position (MoveControl: setYRot(rotlerp(yRot, f, 90))).
void turnTowards(Mob& m, double dx, double dz, float maxTurn) {
    float target = static_cast<float>(std::atan2(dz, dx) * 180.0 / std::numbers::pi) - 90.0F;
    auto  rot    = m.getRotation();
    float yaw    = rotlerp(rot.y, target, maxTurn);
    const_cast<Vec2&>(m.getRotation()) = Vec2{rot.x, yaw};
    m.setYBodyRotation(yaw);
}

float movementSpeed(Mob& m) {
    auto attr = m.getAttribute(SharedAttributes::MOVEMENT_SPEED());
    return attr.mPtr ? attr.mPtr->mCurrentValue : 0.25F;
}

/// The MOVE_TO step of vanilla's controllers for a mob F8 steers (no engine navigation, or the
/// unit's goal set the wanted position): MoveControl (Ground: turn, walk, jump up a step),
/// FlyingMoveControl(maxTurn 10, hoversInPlace) (Flying / Hovering: no gravity while moving,
/// Hovering also while idle; climb or sink with yya) and SmoothSwimmingMoveControl(85, 10, 0.02,
/// 0.1, true) (Swimming: in water 0.02 of the speed along the pitch plus 0.005 buoyancy, on land
/// 0.1 of it). The wanted position stays until reached or stopNavigation (the path follower that
/// sets it every tick in Java has no counterpart on a mob without navigation).
void wantedMove(Mob& m, ActorState& s, ge::MoveKind kind) {
    bool flying = kind == ge::MoveKind::Flying || kind == ge::MoveKind::Hovering;
    if (kind == ge::MoveKind::Swimming && m.isInWater()) push(m, Vec3{0.0f, 0.005f, 0.0f});
    if (!s.hasWanted) {
        if (kind == ge::MoveKind::Hovering) counterGravity(m); // hoversInPlace: setNoGravity stays
        return;
    }
    auto   p  = m.getFeetPos();
    double dx = s.wanted.x - p.x, dy = s.wanted.y - p.y, dz = s.wanted.z - p.z;
    double horizontal = std::sqrt(dx * dx + dz * dz);
    if (dx * dx + dy * dy + dz * dz < 2.5000003E-7 || (horizontal < 0.5 && std::abs(dy) < (flying ? 0.5 : 1.0))) {
        s.hasWanted = false; // reached
        return;
    }
    float speed = static_cast<float>(s.wantedSpeed) * movementSpeed(m);
    if (flying) {
        // FlyingMoveControl: speedModifier * MOVEMENT_SPEED on the ground, * FLYING_SPEED in the air
        if (!m.isOnGround()) speed = static_cast<float>(s.wantedSpeed * ge::ext::flyingSpeed(m));
        counterGravity(m);
        turnTowards(m, dx, dz, 90.0F);
        if (std::abs(dy) > 1.0E-5 || horizontal > 1.0E-5) {
            float pitch = static_cast<float>(-(std::atan2(dy, horizontal) * 180.0 / std::numbers::pi));
            auto  rot   = m.getRotation();
            const_cast<Vec2&>(m.getRotation()) = Vec2{rotlerp(rot.x, pitch, 10.0F), rot.y};
            moveRelative(m, dy > 0.0 ? speed : -speed, speed, m.isOnGround() ? speed : 0.02F);
        }
        return;
    }
    if (kind == ge::MoveKind::Swimming) {
        turnTowards(m, dx, dz, 10.0F);
        if (m.isInWater()) {
            float pitch = std::clamp(wrapDegrees(static_cast<float>(-(std::atan2(dy, horizontal) * 180.0 / std::numbers::pi))), -85.0F, 85.0F);
            auto  rot   = m.getRotation();
            float xRot  = rotlerp(rot.x, pitch, 5.0F);
            const_cast<Vec2&>(m.getRotation()) = Vec2{xRot, rot.y};
            float f1 = speed * 0.02F;
            float rx = xRot * static_cast<float>(std::numbers::pi) / 180.0F;
            moveRelative(m, -std::sin(rx) * f1, std::cos(rx) * f1, 0.02F);
        } else {
            moveRelative(m, 0.0F, speed * 0.1F, m.isOnGround() ? speed * 0.1F : 0.02F);
        }
        return;
    }
    // MoveControl
    turnTowards(m, dx, dz, 90.0F);
    moveRelative(m, 0.0F, speed, m.isOnGround() ? speed : 0.02F);
    auto const& bb    = m.getAABB();
    float       width = bb.max.x - bb.min.x;
    if (m.isOnGround() && dy > 0.6 && dx * dx + dz * dz < std::max(1.0F, width)) m.jumpFromGround(); // maxUpStep 0.6
}

/// NoClipMoveControl#tick.
void noClipMove(Mob& m, ActorState& s) {
    if (!s.hasWanted) return;
    auto   p    = m.getFeetPos();
    double dx   = s.wanted.x - p.x;
    double dy   = s.wanted.y - p.y;
    double dz   = s.wanted.z - p.z;
    double dist = dx * dx + dy * dy + dz * dz;
    if (s.courseChangeCooldown-- <= 0) {
        s.courseChangeCooldown += randomOf(m).nextInt(5) + 2;
        dist = std::sqrt(static_cast<float>(dist));
        if (dist < 1e-9) return;
        double sp = s.wantedSpeed;
        push(m, Vec3{float(dx / dist * 0.1 * sp), float(dy / dist * 0.1 * sp), float(dz / dist * 0.1 * sp)});
    }
}

} // namespace

unsigned controlFlagsDisabled(Mob& mob) {
    // !(getControllingPassenger() instanceof Mob): the first passenger steers; a player is no Mob
    // on Java (Bedrock's Player is: players are excluded)
    Actor* controller = mob.getFirstPassenger();
    bool   mobDriven  = controller && !controller->isPlayer() && asMob(*controller);
    auto*  vehicle    = mob.getVehicle();
    bool   inBoat     = vehicle && (vehicle->getTypeName() == "minecraft:boat" || vehicle->getTypeName() == "minecraft:chest_boat");
    unsigned disabled = 0;
    if (mobDriven) disabled |= ge::Goal::Move | ge::Goal::Look | ge::Goal::Jump;
    if (inBoat) disabled |= ge::Goal::Jump;
    return disabled;
}

void tickGoals(Mob& mob, ActorState& s) {
    // Mob#tick: updateControlFlags every 5 ticks (the goal selector's MOVE, JUMP, LOOK flags only)
    if (mob.mTickCount % 5 == 0) s.disabledFlags = controlFlagsDisabled(mob);
    // Mob#serverAiStep: full selector ticks every other tick, the running goals in between; the
    // parity is the server's tick count + Entity#getId (the network id: Bedrock's runtime id)
    int  tickCount = mob.mTickCount;
    auto id        = static_cast<std::int64_t>(mob.getRuntimeID().rawID);
    auto parity    = static_cast<std::int64_t>(currentTick()) + id;
    bool full      = !(parity % 2 != 0 && tickCount > 1);
    if (s.targets) full ? s.targets->tick(0) : s.targets->tickRunning(false);
    if (s.goals) full ? s.goals->tick(s.disabledFlags) : s.goals->tickRunning(false);
    // the movement controller: a custom one, the NoClipMoveControl, or vanilla's controller of the
    // kind for a mob F8 steers (an engine navigation drives a mob of the other kinds with the
    // movement component of its BP: movement.basic / fly / hover / amphibious, Bedrock's own
    // MoveControl / FlyingMoveControl / SmoothSwimmingMoveControl)
    if (s.moveFn) s.moveFn(mob);
    else if (s.moveKind && *s.moveKind == ge::MoveKind::NoClip) noClipMove(mob, s);
    else if (s.moveKind) {
        // idle, a hovering / swimming mob F8 steers keeps its no-gravity / buoyancy; one the engine
        // steers has them from its own controller
        bool idleForces = (*s.moveKind == ge::MoveKind::Hovering || *s.moveKind == ge::MoveKind::Swimming) && mobs::steeredByWanted(mob);
        if (s.hasWanted || idleForces) wantedMove(mob, s, *s.moveKind);
    }
}

} // namespace gf::feature::entity

namespace gf::entity::goals {

namespace fe = gf::feature::entity;

GoalPtr charge(Mob& mob, ChargeSpec spec) { return std::make_shared<fe::ChargeGoal>(mob, spec); }
GoalPtr breath(Mob& mob, BreathSpec spec) { return std::make_shared<fe::BreathGoal>(mob, std::move(spec)); }
GoalPtr flockToSameKind(Mob& mob, double speed) { return std::make_shared<fe::FlockGoal>(mob, speed); }
GoalPtr panicOnFlockDeath(Mob& mob, double speed) { return std::make_shared<fe::PanicGoal>(mob, speed); }
GoalPtr avoidAny(Mob& mob, std::function<bool(Actor&)> avoid, float distance, double walkSpeed, double sprintSpeed) {
    return std::make_shared<fe::AvoidGoal>(mob, std::move(avoid), distance, walkSpeed, sprintSpeed);
}
GoalPtr goHome(Mob& mob, double speed) { return std::make_shared<fe::GoHomeGoal>(mob, speed); }
GoalPtr alwaysWatchTarget(Mob& mob) { return std::make_shared<fe::WatchTargetGoal>(mob); }
GoalPtr randomLookIfBored(Mob& mob) { return std::make_shared<fe::LookIfBoredGoal>(mob); }
GoalPtr simplifiedAttack(Mob& mob) { return std::make_shared<fe::SimplifiedAttackGoal>(mob); }

} // namespace gf::entity::goals

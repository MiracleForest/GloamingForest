// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/boss/Naga.java
//   src/main/java/gloamingforest/entity/boss/NagaSegment.java
//   src/main/java/gloamingforest/entity/ai/goal/NagaMovementPattern.java
//   src/main/java/gloamingforest/entity/ai/goal/NagaSmashGoal.java
//   src/main/java/gloamingforest/entity/ai/control/NagaMoveControl.java
//   src/main/java/gloamingforest/entity/ai/goal/AttemptToGoHomeGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/SimplifiedAttackGoal.java
//   src/main/java/gloamingforest/block/entity/spawner/NagaSpawnerBlockEntity.java
//   src/main/java/gloamingforest/util/entities/EntityUtil.java (canDestroyBlock, bossChestLocation)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Naga (U20) on F8's entity and boss kits: the 12 NagaSegments are F8 part actors of type
// gloamingforest:naga_part (2 x 2 when active, 0 x 0 and hidden through the part's
// gloamingforest:active property otherwise), moved by moveSegments every tick. The goals run in
// F8's goal selector; the engine's walk navigation finds the paths (Java's PathNavigation) and the
// Naga's move control adds the slither (xxa). Synced data (DATA_DAZE, DATA_CHARGE, DATA_STUNLESS)
// are BP entity properties, which the resource pack's render controller and animations read on
// every client. Java's client-only particles (daze sparkles, stunless anger, the death trail) are
// emitted from the server through gf::fx so vanilla clients see them too.

#include "feature/naga/NagaInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <numbers>
#include <random>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/ActorCollision.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/attribute/AttributeModifierOperation.h"
#include "mc/world/attribute/AttributeOperands.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/entity/components/PushableByEntityComponent.h"
#include "mc/entity/components/PushablePreset.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/Item.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/pathfinder/Path.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/item/Item.h"
#include "feature/movement/Movement.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::naga {

namespace ge = gf::entity;

namespace {

constexpr double kPi = std::numbers::pi;

// ---- actors ----------------------------------------------------------------------------------------

std::int64_t uidOf(Actor const& a) { return a.getOrCreateUniqueID().rawID; }

Actor* byId(std::int64_t uid) {
    if (!uid) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

Mob* asMob(Actor& a) { return a.hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(&a) : nullptr; }

Mob* mobById(std::int64_t uid) {
    auto* a = byId(uid);
    return a ? asMob(*a) : nullptr;
}

std::uint64_t serverTick() {
    auto level = ll::service::getLevel();
    return level ? level->getCurrentServerTick().tickID : 0;
}

Actor* targetOf(Mob& m) {
    auto* t = byId(m.mTargetId->rawID);
    return t && t->isAlive() ? t : nullptr;
}

bool attackable(Actor& a) {
    if (!a.isAlive()) return false;
    if (a.isPlayer()) {
        auto& p = static_cast<Player&>(a);
        if (p.isCreative() || p.isSpectator()) return false;
    }
    return true;
}

/// LivingEntity#getVisibilityPercent(naga): 0.8 while crouching; invisible: 0.7 * the armor cover
/// (worn armor pieces / 4, at least 0.1). No mob-head factor: the Naga is not a skeleton, zombie,
/// piglin or creeper.
double visibilityPercent(Actor const& a) {
    double v = 1.0;
    if (a.isSneaking()) v *= 0.8;
    if (a.isInvisible()) {
        int worn = 0;
        for (auto slot : {SharedTypes::Legacy::ArmorSlot::Head, SharedTypes::Legacy::ArmorSlot::Torso, SharedTypes::Legacy::ArmorSlot::Legs,
                          SharedTypes::Legacy::ArmorSlot::Feet})
            if (!game::items::isEmpty(a.getArmor(slot))) ++worn;
        v *= 0.7 * std::max(static_cast<double>(worn) / 4.0, 0.1);
    }
    return v;
}

/// LivingEntity#isDeadOrDying.
bool isDeadOrDying(Actor const& a) { return !a.isAlive() || a.getHealth() <= 0; }

/// Entity#isPushable: Bedrock's minecraft:pushable (is_pushable) puts the PushableByEntityComponent
/// on what entities may push; LivingEntity#isPushable adds alive and not a spectator.
/// Players are pushable in Java whatever Bedrock's player entity declares.
bool isPushable(Actor& a) {
    if (a.isPlayer()) return !isDeadOrDying(a) && !static_cast<Player&>(a).isSpectator();
    if (!a.getEntityContext().hasComponent<PushableByEntityComponent>()) return false;
    if (a.hasCategory(ActorCategory::Mob) && isDeadOrDying(a)) return false;
    return true;
}

/// Entity#blockPosition.
BlockPos blockPosOf(Actor const& a) {
    auto p = a.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
}

double distSqr(Vec3 const& a, Vec3 const& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

int gPlayerMotions = 0;

/// Entity#setDeltaMovement (players get a SetActorMotionPacket through gf::movement).
void setMotion(Actor& a, Vec3 const& v) {
    if (a.isPlayer()) {
        ++gPlayerMotions;
        gf::movement::applyMotion(static_cast<Player&>(a), v);
        return;
    }
    const_cast<Vec3&>(a.getPosDelta()) = v;
}

/// Entity#push(x, y, z).
void push(Actor& a, Vec3 const& d) {
    auto v = a.getPosDelta();
    setMotion(a, Vec3{v.x + d.x, v.y + d.y, v.z + d.z});
}

bool mobGriefing() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    GameRuleId griefing;
    griefing.mValue = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
    return level->getGameRules().getBool(griefing, true);
}

/// The block's collision box at `pos` has volume.
bool hasCollision(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    AABB         b     = block.getBlockType().getCollisionShape(block, region, pos, nullptr);
    return b.max.x - b.min.x > 1e-6f && b.max.y - b.min.y > 1e-6f && b.max.z - b.min.z > 1e-6f;
}

/// The blocks whose Block#canEntityDestroy always refuses (BossSpawnerBlock, ForceFieldBlock,
/// StrongholdShieldBlock, SkullChestBlock).
std::unordered_set<std::string> const kRefusesEntityDestroy{
    "gloamingforest:naga_boss_spawner",       "gloamingforest:lich_boss_spawner",          "gloamingforest:hydra_boss_spawner",
    "gloamingforest:ur_ghast_boss_spawner",   "gloamingforest:knight_phantom_boss_spawner", "gloamingforest:snow_queen_boss_spawner",
    "gloamingforest:minoshroom_boss_spawner", "gloamingforest:alpha_yeti_boss_spawner",     "gloamingforest:final_boss_boss_spawner",
    "gloamingforest:violet_force_field",      "gloamingforest:pink_force_field",           "gloamingforest:orange_force_field",
    "gloamingforest:green_force_field",       "gloamingforest:blue_force_field",           "gloamingforest:stronghold_shield",
    "gloamingforest:skull_chest",
};
/// The VanishingBlock family (`instanceof VanishingBlock`).
std::unordered_set<std::string> const kVanishingBlocks{
    "gloamingforest:vanishing_block",
    "gloamingforest:unbreakable_vanishing_block",
    "gloamingforest:locked_vanishing_block",
    "gloamingforest:reappearing_block",
};

bool isLeaves(Block const& block) {
    // state.is(BlockTags.LEAVES) only (R6: upstream leaves join the tag through their owners' fragments)
    return gf::tags::has(gf::tags::Registry::Block, "minecraft:leaves", game::blockId(block));
}

/// A boolean block state ("true" / "false" in the block's serialized states; false when absent).
bool boolState(Block const& block, std::string const& name) {
    auto states = game::statesOf(block);
    auto it     = states.find(name);
    return it != states.end() && (it->second == "true" || it->second == "1");
}

/// BlockState#getDestroySpeed with Java's values where Bedrock's differ across the 50 limit of
/// canDestroyBlock: obsidian and crying obsidian are 50 in Java (Bedrock: 35).
float javaHardness(Block const& block) {
    auto id = game::blockId(block);
    if (id == "minecraft:obsidian" || id == "minecraft:crying_obsidian") return 50.0F;
    return block.getBlockType().getDestroySpeed();
}

/// `world.getBlockEntity(pos) instanceof Container`: an upstream container (gf::inventory, custom blocks
/// keep no block entity) or a vanilla block entity whose Java counterpart implements Container
/// (chests, furnaces, hoppers, dispensers, droppers, barrels, shulker boxes, brewing stands,
/// crafters, jukeboxes, decorated pots, chiseled bookshelves; Bedrock's shelf). Signs, banners,
/// beds, skulls, flower pots, cauldrons, note blocks, lecterns, enchanting tables, spawners and the
/// other block entities are not containers: an entity may destroy them.
bool isContainer(BlockSource& region, BlockPos const& pos) {
    if (gf::inventory::find(region, pos)) return true;
    auto* be = region.getBlockEntity(pos);
    if (!be) return false;
    switch (be->getType()) {
    case BlockActorType::Furnace:
    case BlockActorType::BlastFurnace:
    case BlockActorType::Smoker:
    case BlockActorType::Chest:
    case BlockActorType::BrewingStand:
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::Hopper:
    case BlockActorType::ShulkerBox:
    case BlockActorType::BarrelBlock:
    case BlockActorType::Crafter:
    case BlockActorType::Jukebox:
    case BlockActorType::DecoratedPot:
    case BlockActorType::ChiseledBookshelf:
    case BlockActorType::Shelf:
        return true;
    default:
        return false;
    }
}

/// VanishingBlock#areBlocksLocked: a locked locked_vanishing_block among the vanishing blocks
/// connected to `start` (at most 512 positions looked at).
bool vanishingBlocksLocked(BlockSource& region, BlockPos const& start) {
    std::deque<BlockPos> queue{start};
    std::set<std::tuple<int, int, int>> checked;
    for (int iter = 0; !queue.empty() && iter < 512; iter++) {
        BlockPos cur = queue.front(); // ArrayDeque#pop takes the head
        queue.pop_front();
        Block const& block = region.getBlock(cur);
        auto         id    = game::blockId(block);
        if (id == "gloamingforest:locked_vanishing_block" && boolState(block, "gloamingforest:locked")) return true;
        checked.insert({cur.x, cur.y, cur.z});
        if (kVanishingBlocks.contains(id))
            for (auto [dx, dy, dz] : {std::tuple{0, -1, 0}, std::tuple{0, 1, 0}, std::tuple{0, 0, -1}, std::tuple{0, 0, 1}, std::tuple{-1, 0, 0}, std::tuple{1, 0, 0}}) {
                BlockPos n{cur.x + dx, cur.y + dy, cur.z + dz};
                if (!checked.contains({n.x, n.y, n.z})) queue.push_back(n); // Deque#offer adds at the tail
            }
    }
    return false;
}

/// Block#canEntityDestroy of upstream's blocks that override it (by id, R2; their states read as upstream
/// reads them): BossSpawnerBlock, ForceFieldBlock, StrongholdShieldBlock and SkullChestBlock refuse;
/// VanishingBlock (vanishing, unbreakable vanishing, reappearing) refuses an inactive block whose
/// connected vanishing blocks are locked; LockedVanishingBlock refuses while locked, else as a
/// VanishingBlock. Every other block lets entities destroy it.
bool canEntityDestroy(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto id = game::blockId(block);
    if (kRefusesEntityDestroy.contains(id)) return false;
    if (id == "gloamingforest:locked_vanishing_block" && boolState(block, "gloamingforest:locked")) return false;
    if (kVanishingBlocks.contains(id) && !boolState(block, "gloamingforest:active")) return !vanishingBlocksLocked(region, pos);
    return true;
}

/// EntityUtil#canDestroyBlock: hardness in 0..50, not air, not a Container, canEntityDestroy, and
/// EventHooks#onEntityDestroyBlock (the mobGriefing rule).
bool canDestroyBlock(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (block.isAir()) return false;
    float hardness = javaHardness(block);
    if (hardness < 0.0f || hardness >= 50.0f) return false;
    if (isContainer(region, pos)) return false;
    if (!canEntityDestroy(region, pos, block)) return false;
    return mobGriefing();
}

void destroyBlock(BlockSource& region, BlockPos const& pos, bool drop) {
    auto level = ll::service::getLevel();
    if (!level) return;
    BlockChangeContext ctx{};
    level->destroyBlock(region, pos, drop, ctx);
}

float voicePitch(core::jrand::RandomSource& r) { return (r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F; }

void particle(Actor& a, char const* id, Vec3 const& pos, Vec3 const& vel = {}) { gf::fx::particle(a.getDimension(), id, pos, vel); }

// ---- per-Naga state ------------------------------------------------------------------------------

struct Registry {
    std::unordered_map<std::int64_t, std::unique_ptr<NagaState>> states;
    std::unordered_map<std::int64_t, std::int64_t>               seeds; ///< selftests: seeds for the next state
};
Registry& R() {
    static Registry r;
    return r;
}

std::int64_t freshSeed() {
    static std::random_device rd;
    return (static_cast<std::int64_t>(rd()) << 32) ^ rd();
}

NagaState& stateFor(Actor& naga) {
    auto uid       = uidOf(naga);
    auto [it, add] = R().states.try_emplace(uid);
    if (add) {
        it->second      = std::make_unique<NagaState>();
        it->second->uid = uid;
        it->second->random.setSeed(freshSeed());
    }
    it->second->lastSeen = serverTick();
    return *it->second;
}

// ---- synced data (entity properties) -------------------------------------------------------------

void setDazed(Actor& naga, NagaState& s, bool v) {
    if (s.dazed == v) return;
    s.dazed = v;
    ge::setProperty(naga, "gloamingforest:dazed", v);
}
void setStunless(Actor& naga, NagaState& s, bool v) {
    if (s.stunless == v) return;
    s.stunless = v;
    ge::setProperty(naga, "gloamingforest:stunless", v);
}
void setCharging(Actor& naga, NagaState& s, bool v) {
    if (s.charging != v) {
        s.charging = v;
        ge::setProperty(naga, "gloamingforest:charging", v);
    }
    if (!v) setStunless(naga, s, false);
}

// ---- home (EnforcedHomePoint) --------------------------------------------------------------------

std::optional<ge::Home> validHome(Mob& naga) {
    auto h = ge::home(naga);
    if (!h || h->pos.dim != static_cast<int>(naga.getDimensionId())) return std::nullopt;
    return h;
}

bool homeContains(Mob& naga, Actor& entity) {
    auto h = validHome(naga);
    if (!h) return true;
    auto p = blockPosOf(entity);
    return std::abs(h->pos.x - p.x) <= kXzHomeBounds && std::abs(h->pos.y - p.y) <= kYHomeBounds && std::abs(h->pos.z - p.z) <= kXzHomeBounds;
}

bool areSelfAndTargetInHome(Mob& naga, Actor* entity) { return homeContains(naga, naga) && (!entity || homeContains(naga, *entity)); }

bool shouldDestroyAllBlocks(Mob& naga, NagaState& s) { return s.charging || !homeContains(naga, naga); }

// ---- navigation (PathNavigation on the engine's walk navigation) ---------------------------------

NavigationComponent* navOf(Mob& m) {
    auto nav = m.getEntityContext().tryGetComponent<NavigationComponent>();
    return nav ? nav.as_ptr() : nullptr;
}

void navStop(Mob& m, NagaState& s) {
    s.navActive = false;
    if (auto* nav = navOf(m); nav && nav->mNavigation.get()) nav->mNavigation.get()->stop(*nav, m);
}

/// PathNavigation#moveTo(x, y, z, speed): false (done at once) when no path is found.
bool navMoveTo(Mob& m, NagaState& s, Vec3 const& dest, float speed) {
    auto* nav = navOf(m);
    if (!nav || !nav->mNavigation.get()) return false;
    bool ok = nav->mNavigation.get()->moveTo(*nav, m, dest, speed);
    s.navActive     = ok;
    s.navDest       = dest;
    s.navSpeed      = speed;
    s.navTicks      = 0;
    s.stuck         = false;
    s.stuckCheckPos = m.getFeetPos();
    return ok;
}

bool navDone(Mob& m, NagaState& s) {
    if (!s.navActive) return true;
    auto* nav = navOf(m);
    if (!nav || !nav->mPath.get()) {
        s.navActive = false;
        return true;
    }
    auto const& path = nav->mPath.get();
    if (path->mIndex >= path->mNodes->size()) {
        s.navActive = false;
        return true;
    }
    return false;
}

/// Naga#customServerAiStep's path skipping: while the next node is closer than bbWidth * 4
/// horizontally, advance the path (done when the last node is reached), plus PathNavigation's
/// stuck detection (no progress for 100 ticks: isStuck).
void navTick(Mob& m, NagaState& s) {
    if (navDone(m, s)) return;
    auto*       nav  = navOf(m);
    auto const& path = nav->mPath.get();
    auto        bb   = m.getAABB();
    double      d    = (bb.max.x - bb.min.x) * 4.0;
    auto        p    = m.getFeetPos();
    while (path->mIndex < path->mNodes->size()) {
        auto const& node = (*path->mNodes)[path->mIndex].pos.get();
        // Path#getNextEntityPos -> getEntityPosAtNode: node + (int)(bbWidth + 1) * 0.5 = 1.5 for the
        // 2-wide Naga
        double      nx = node.x + 1.5, nz = node.z + 1.5;
        double      dx = nx - p.x, dz = nz - p.z;
        if (dx * dx + dz * dz >= d * d) break;
        path->mIndex = path->mIndex + 1;
    }
    if (path->mIndex >= path->mNodes->size()) {
        navStop(m, s);
        return;
    }
    if (++s.navTicks % 100 == 0) {
        auto q = m.getFeetPos();
        s.stuck = distSqr(q, s.stuckCheckPos) < 2.25;
        s.stuckCheckPos = q;
    }
}

// ---- segments (NagaSegment) ----------------------------------------------------------------------

void makePoofAt(Actor& naga, NagaState& s, Vec3 const& pos) {
    float width = 2.0F, height = 3.0F;
    auto& r     = s.random;
    for (int k = 0; k < 20; k++) {
        Vec3 at{pos.x + r.nextFloat() * width * 2.0F - width, pos.y + r.nextFloat() * height, pos.z + r.nextFloat() * width * 2.0F - width};
        Vec3 vel{float(r.nextGaussian() * 0.02), float(r.nextGaussian() * 0.02), float(r.nextGaussian() * 0.02)};
        particle(naga, "minecraft:explosion", at, vel);
    }
}

void setSegmentActive(Actor& naga, Segment& seg, bool active) {
    seg.active = active;
    ge::ext::setPartSize(seg.part, active ? ge::PartSize{2.0f, 2.0f} : ge::PartSize{0.0f, 0.0f});
    if (auto* a = ge::partActor(seg.part)) ge::setProperty(*a, "gloamingforest:active", active);
    (void)naga;
}

void addSegments(Mob& naga, NagaState& s) {
    auto feet = naga.getFeetPos();
    for (int i = 0; i < kMaxSegments; i++) {
        auto& seg = s.segments[i];
        if (seg.pos.x == 0 && seg.pos.y == 0 && seg.pos.z == 0) seg.pos = feet;
        std::int64_t uid = s.uid;
        seg.part         = ge::addPart(naga, "segment_" + std::to_string(i), {0.0f, 0.0f}, [uid, i](Mob& parent) {
            auto it = R().states.find(uid);
            auto p  = parent.getFeetPos();
            if (it == R().states.end()) return Vec3{0, 0, 0};
            auto const& q = it->second->segments[i].pos;
            return Vec3{q.x - p.x, q.y - p.y, q.z - p.z};
        });
        setSegmentActive(naga, seg, seg.active);
    }
}

/// Naga#activateBodySegments.
void activateBodySegments(Mob& naga, NagaState& s) {
    auto  feet = naga.getFeetPos();
    auto& r    = s.random;
    for (int i = 0; i < s.currentSegmentCount; i++) {
        auto& seg = s.segments[i];
        setSegmentActive(naga, seg, true);
        seg.pos  = Vec3{float(feet.x + 0.1 * i), float(feet.y + 0.5), float(feet.z + 0.1 * i)};
        seg.yRot = r.nextFloat() * 360.0F;
        seg.xRot = 0.0F;
        for (int j = 0; j < 20; j++) {
            double d0 = r.nextGaussian() * 0.02, d1 = r.nextGaussian() * 0.02, d2 = r.nextGaussian() * 0.02;
            float  w = 2.0F, h = 2.0F;
            Vec3   at{float(seg.pos.x + r.nextFloat() * w * 2.0F - w - d0 * 10.0), float(seg.pos.y + r.nextFloat() * h - d1 * 10.0),
                     float(seg.pos.z + r.nextFloat() * w * 2.0F - w - d2 * 10.0)};
            particle(naga, "minecraft:explosion", at, Vec3{float(d0), float(d1), float(d2)});
        }
    }
}

void applySpeedBoost(Mob& naga, int segments) {
    auto  attr = naga.getMutableAttribute(SharedAttributes::MOVEMENT_SPEED());
    auto* inst = attr.mInstance->mPtr;
    if (!inst) return;
    auto id = mce::UUID::fromStringHash("gloamingforest:segment_speed_boost");
    inst->removeModifier(id, attr.mContext);
    if (segments <= 0) return;
    double speedMod = static_cast<float>(kMaxSegments) / segments * 0.02F;
    AttributeModifier mod(id, "gloamingforest:segment_speed_boost", static_cast<float>(speedMod), AttributeModifierOperation::OperationAddition,
                          AttributeOperands::OperandCurrent, false);
    inst->addModifier(mod, attr.mContext);
}

} // namespace

float blockFriction(Block const& block) {
    float f = block.getBlockType().mFriction;
    return f > 0.0F && f <= 1.5F ? f : 0.6F; // Block's default friction 0.6 (ice 0.98, slime 0.8)
}

int segmentsFor(float health, float healthPerSegment) {
    return std::clamp(static_cast<int>((health / healthPerSegment) + (health > 0 ? 2 : 0)), 0, kMaxSegments);
}

namespace {

/// Naga#setSegmentsPerHealth.
void setSegmentsPerHealth(Mob& naga, NagaState& s) {
    int oldSegments       = s.currentSegmentCount;
    int newSegments       = segmentsFor(static_cast<float>(naga.getHealth()), s.healthPerSegment);
    s.currentSegmentCount = newSegments;
    if (newSegments < oldSegments) {
        for (int i = newSegments; i < oldSegments; i++) s.segments[i].deathCounter = (oldSegments - i) * 12;
    } else if (newSegments > oldSegments) {
        activateBodySegments(naga, s);
    }
    if (oldSegments != newSegments) applySpeedBoost(naga, newSegments);
}

/// Entity#isInWall of a segment (2 x 2 when active, 0 x 0 when not): any block touched by the box
/// 0.8 * width wide and 1e-6 high around the eye (0.85 * height up) that is suffocating
/// (BlockBehaviour#isSuffocating: blocks motion with a full-block collision shape) and whose
/// collision shape meets the box.
bool segmentInWall(BlockSource& region, Vec3 const& pos, bool active) {
    double size = active ? 2.0 : 0.0;
    double eyeY = pos.y + size * 0.85;
    double half = size * 0.8 / 2.0;
    double minX = pos.x - half, maxX = pos.x + half, minY = eyeY - 5.0E-7, maxY = eyeY + 5.0E-7, minZ = pos.z - half, maxZ = pos.z + half;
    for (int x = int(std::floor(minX)); x <= int(std::floor(maxX)); ++x)
        for (int y = int(std::floor(minY)); y <= int(std::floor(maxY)); ++y)
            for (int z = int(std::floor(minZ)); z <= int(std::floor(maxZ)); ++z) {
                BlockPos p{x, y, z};
                if (!hasCollision(region, p)) continue;
                Block const& block = region.getBlock(p);
                AABB         b     = block.getBlockType().getCollisionShape(block, region, p, nullptr);
                bool full = b.max.x - b.min.x >= 0.999f && b.max.y - b.min.y >= 0.999f && b.max.z - b.min.z >= 0.999f;
                if (!full) continue;
                // Shapes.joinIsNotEmpty(shape.move(pos), box, AND): the full cube overlaps the box
                if (b.min.x < maxX && b.max.x > minX && b.min.y < maxY && b.max.y > minY && b.min.z < maxZ && b.max.z > minZ) return true;
            }
    return false;
}

/// NagaSegment#tick: the self-destruct countdown and the contact with other entities.
void tickSegment(Mob& naga, NagaState& s, int i) {
    auto& seg = s.segments[i];
    if (seg.active) {
        if (auto* part = ge::partActor(seg.part)) {
            auto& region = naga.getDimensionBlockSource();
            AABB  box{Vec3{seg.pos.x - 1.0f, seg.pos.y, seg.pos.z - 1.0f}, Vec3{seg.pos.x + 1.0f, seg.pos.y + 2.0f, seg.pos.z + 1.0f}};
            std::vector<Actor*> others;
            for (auto* a : region.getEntities(box, 0.0f))
                if (a && a != part && a->isAlive()) others.push_back(a);
            if (auto level = ll::service::getLevel())
                level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
                    if (static_cast<int>(p.getDimensionId()) != static_cast<int>(naga.getDimensionId()) || !p.isAlive()) return true;
                    auto const& pb = p.getAABB();
                    if (pb.min.x < box.max.x && pb.max.x > box.min.x && pb.min.y < box.max.y && pb.max.y > box.min.y && pb.min.z < box.max.z && pb.max.z > box.min.z)
                        others.push_back(&p);
                    return true;
                }));
            for (auto* e : others) {
                if (e == &naga) continue; // NagaSegment#is: the Naga and its own parts
                if (ge::ext::parentOf(*e)) continue; // parts (TFPart, PartEntity) are not pushable
                // Entity#isPushable: living entities (alive, not spectators), boats and minecarts;
                // Bedrock's minecraft:pushable is_pushable (absent on items, XP orbs, arrows, falling
                // blocks and armor stands, false on the Naga and the parts)
                if (!isPushable(*e)) continue;
                // Entity#push(Entity): apart along the larger horizontal axis. On a ServerPlayer it
                // only changes the server's copy of deltaMovement (Entity#push(double...) does not set
                // hurtMarked), so nothing reaches the player's client and the player walks through
                // the body, knocked back only by the mob_attack hits below: no motion for players.
                auto   ep = e->getFeetPos();
                double dx = ep.x - seg.pos.x, dz = ep.z - seg.pos.z;
                double m  = std::max(std::abs(dx), std::abs(dz));
                if (!e->isPlayer() && m >= 0.01) {
                    m = std::sqrt(m);
                    dx /= m, dz /= m;
                    double inv = std::min(1.0, 1.0 / m);
                    dx *= inv * 0.05, dz *= inv * 0.05;
                    push(*e, Vec3{float(dx), 0.0f, float(dz)});
                }
                // attack anything that's not us
                if (e->hasCategory(ActorCategory::Mob) && e->getTypeName() != kNagaId && !s.dazed && !isDeadOrDying(naga)) {
                    int attackStrength = 2;
                    if (e->hasCategory(ActorCategory::Animal)) attackStrength *= 3; // get rid of nearby deer & look impressive
                    gf::combat::hurt(*e, static_cast<float>(attackStrength), "minecraft:mob_attack", &naga, &naga);
                }
            }
        }
    }
    if (seg.deathCounter > 0) {
        seg.deathCounter--;
        if (seg.deathCounter <= 0) {
            makePoofAt(naga, s, seg.pos);
            float pitch = (voicePitch(s.random) * 0.75F) + (0.5F * s.random.nextFloat());
            gf::fx::sound(naga.getDimension(), "entity.gloamingforest.naga.hurt", naga.getFeetPos(), 0.25F, pitch);
            // naga.deathTime = 0: while segments still pop, the death animation (the trail, the
            // chest) waits for the last one (no effect on a live Naga)
            gf::boss::ext::setDeathTime(naga, 0);
            setSegmentActive(naga, seg, false);
        }
    }
}

/// Naga#moveSegments.
void moveSegments(Mob& naga, NagaState& s) {
    auto& region = naga.getDimensionBlockSource();
    bool  dying  = !naga.isAlive() || naga.getHealth() <= 0;
    for (int i = 0; i < kMaxSegments; i++) {
        tickSegment(naga, s, i);
        Vec3  follow;
        float leaderYRot;
        if (i == 0) {
            follow     = naga.getFeetPos();
            leaderYRot = naga.getRotation().y;
        } else {
            follow     = s.segments[i - 1].pos;
            leaderYRot = s.segments[i - 1].yRot;
        }
        auto&  seg   = s.segments[i];
        float  angle = (((leaderYRot + 180) * static_cast<float>(kPi)) / 180.0F);
        double straightenForce = 0.05 + (1.0 / (i + 1)) * 0.5;
        if (dying) straightenForce = 0.0; // Dead snakes don't move

        double idealX  = -std::sin(angle) * straightenForce;
        double idealZ  = std::cos(angle) * straightenForce;
        double groundY = segmentInWall(region, seg.pos, seg.active) ? follow.y + 2.0F : follow.y;
        double idealY  = (groundY - follow.y) * straightenForce;

        double dx = seg.pos.x - follow.x, dy = seg.pos.y - follow.y, dz = seg.pos.z - follow.z;
        double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1.0E-4) dx = dy = dz = 0;
        else dx /= len, dy /= len, dz /= len;
        dx += idealX, dy += idealY, dz += idealZ;
        len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1.0E-4) dx = dy = dz = 0;
        else dx /= len, dy /= len, dz /= len;

        double f = 2.0;
        seg.pos  = Vec3{float(follow.x + f * dx), float(follow.y + f * dy), float(follow.z + f * dz)};

        double distance = std::sqrt(static_cast<float>(dx * dx + dz * dz));
        seg.yRot        = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi) + 90.0F;
        seg.xRot        = -static_cast<float>(std::atan2(dy, distance) * 180.0 / kPi);
        ge::ext::setPartRotation(seg.part, seg.yRot, seg.xRot);
    }
}

// ---- movement pattern (NagaMovementPattern) ------------------------------------------------------

void setState(NagaState& s, MovementState st) { s.state = st; }

/// NagaMovementPattern#findCirclePoint.
Vec3 findCirclePoint(Mob& naga, Actor& toCircle, bool clockwise, double radius, double rotation) {
    auto  n      = naga.getFeetPos();
    auto  t      = toCircle.getFeetPos();
    double vecx  = n.x - t.x;
    double vecz  = n.z - t.z;
    float  rangle = static_cast<float>(std::atan2(vecz, vecx));
    rangle += static_cast<float>(clockwise ? rotation : -rotation);
    double dx = std::cos(rangle) * radius;
    double dz = std::sin(rangle) * radius;
    double dy = std::min(static_cast<double>(naga.getAABB().min.y), static_cast<double>(t.y));
    // BlockPos.containing, then PathNavigation#moveTo(x, y, z) at the block's corner
    return Vec3{float(std::floor(t.x + dx)), float(std::floor(dy)), float(std::floor(t.z + dz))};
}

void doDazeImpl(Mob& naga, NagaState& s) {
    setState(s, MovementState::Daze);
    navStop(naga, s);
    s.stateCounter = 60 + s.random.nextInt(40);
}

void doCircleImpl(NagaState& s) {
    setState(s, MovementState::Circle);
    s.stateCounter += 10 + s.random.nextInt(10);
    s.stunCalculated = false;
}

void forceCircle(NagaState& s) {
    setState(s, MovementState::Circle);
    s.stateCounter   = 10 + s.random.nextInt(10);
    s.stunCalculated = false;
}

void doCrumblePlayer(Mob& naga, NagaState& s) {
    setState(s, MovementState::Crumble);
    navStop(naga, s);
    s.stateCounter = 20 + s.random.nextInt(20);
}

void doCharge(NagaState& s, bool stunless) {
    setState(s, stunless ? MovementState::StunlessCharge : MovementState::Charge);
    s.stateCounter = 2;
}

void doIntimidate(Mob& naga, NagaState& s) {
    setState(s, MovementState::Intimidate);
    gf::fx::sound(naga.getDimension(), "entity.gloamingforest.naga.rattle", naga.getFeetPos(), 4.0F, voicePitch(s.random));
    s.stateCounter += 15 + s.random.nextInt(10);
    log().info("naga: {} intimidates (counter {})", s.uid, s.stateCounter);
}

void transitionState(Mob& naga, NagaState& s) {
    switch (s.state) {
    case MovementState::Intimidate: {
        s.clockwise = !s.clockwise;
        auto* t     = targetOf(naga);
        if (t && t->getAABB().min.y > naga.getAABB().max.y) doCrumblePlayer(naga, s);
        else doCharge(s, s.stunless);
        break;
    }
    case MovementState::Crumble:
        doCharge(s, s.stunless);
        break;
    case MovementState::Charge:
    case MovementState::StunlessCharge:
    case MovementState::Daze:
        doCircleImpl(s);
        break;
    case MovementState::Circle:
        doIntimidate(naga, s);
        break;
    }
}

/// NagaMovementPattern#crumbleBelowTarget.
void crumbleBelowTarget(Mob& naga, NagaState& s, int range) {
    auto* target = targetOf(naga);
    if (!mobGriefing() || !target) return;
    int floor   = static_cast<int>(naga.getAABB().min.y);
    int targetY = static_cast<int>(target->getAABB().min.y);
    if (targetY <= floor) return;
    auto& r  = s.random;
    auto  tp = target->getFeetPos();
    int   dx = static_cast<int>(tp.x) + r.nextInt(range) - r.nextInt(range);
    int   dz = static_cast<int>(tp.z) + r.nextInt(range) - r.nextInt(range);
    int   dy = targetY - r.nextInt(range) + r.nextInt(range > 1 ? range - 1 : range);
    if (dy <= floor) dy = targetY;
    BlockPos pos{dx, dy, dz};
    auto&    region = naga.getDimensionBlockSource();
    if (!canDestroyBlock(region, pos)) return;
    destroyBlock(region, pos, true);
    auto np = naga.getFeetPos();
    for (int k = 0; k < 20; k++) {
        double d = r.nextGaussian() * 0.02, d1 = r.nextGaussian() * 0.02, d2 = r.nextGaussian() * 0.02;
        Vec3   at{np.x + r.nextFloat() * 2.0F * 2.0F - 2.0F, np.y + r.nextFloat() * 3.0F, np.z + r.nextFloat() * 2.0F * 2.0F - 2.0F};
        particle(naga, "minecraft:crit", at, Vec3{float(d), float(d1), float(d2)});
    }
}

/// Mob#lookAt(entity, maxYRot, maxXRot) on the body yaw.
void turnTowards(Mob& naga, Actor& target, float maxYRot) {
    auto   n  = naga.getFeetPos();
    auto   t  = target.getFeetPos();
    double dx = t.x - n.x, dz = t.z - n.z;
    float  want = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi) - 90.0F;
    auto   rot  = naga.getRotation();
    float  diff = std::fmod(want - rot.y, 360.0F);
    if (diff >= 180.0F) diff -= 360.0F;
    if (diff < -180.0F) diff += 360.0F;
    float yaw = rot.y + std::clamp(diff, -maxYRot, maxYRot);
    const_cast<Vec2&>(naga.getRotation()) = Vec2{rot.x, yaw};
    naga.setYBodyRotation(yaw);
}

void tickPattern(Mob& naga, NagaState& s) {
    if (!navDone(naga, s)) {
        if (s.stuck) navStop(naga, s); // if we get stuck, give up
        return;
    }
    if (s.stateCounter-- <= 0) {
        transitionState(naga, s);
        return;
    }
    auto* target = targetOf(naga);
    switch (s.state) {
    case MovementState::Intimidate:
        navStop(naga, s);
        if (target) turnTowards(naga, *target, 30.0F);
        // upstream sets naga.zza = 0.1 here, but in the same Mob#serverAiStep the MoveControl ticks after
        // the goals with its operation at WAIT (the path is done: nothing set a wanted position)
        // and MoveControl#tick's WAIT branch calls mob.setZza(0): travel() never sees the 0.1, the
        // Naga does not creep forward in upstream either. Nothing to port.
        if (!s.stunCalculated) {
            // the stunless charge has a higher chance to happen the lower the naga's health gets;
            // difficulty is also factored in
            float healthRatio = 1.0F - (static_cast<float>(naga.getHealth()) / static_cast<float>(naga.getMaxHealth())) - 0.25F;
            int   difficulty  = 2;
            if (auto level = ll::service::getLevel()) difficulty = static_cast<int>(level->getDifficulty());
            float chance     = std::clamp(healthRatio + (difficulty * 0.05F), 0.0F, 0.5F);
            float randChance = s.random.nextFloat() * 0.75F;
            setStunless(naga, s, randChance < chance);
            s.stunCalculated = true;
        }
        break;
    case MovementState::Crumble:
        navStop(naga, s);
        crumbleBelowTarget(naga, s, 2);
        crumbleBelowTarget(naga, s, 3);
        break;
    case MovementState::Charge:
    case MovementState::StunlessCharge:
        if (target) navMoveTo(naga, s, findCirclePoint(naga, *target, s.clockwise, 5, kPi), 1.5F);
        setCharging(naga, s, true);
        break;
    case MovementState::Circle: {
        setCharging(naga, s, false);
        setDazed(naga, s, false);
        double radius   = s.stateCounter % 2 == 0 ? 12.0 : 14.0;
        double rotation = 1.0;
        if (s.stateCounter == 2) radius = 16;   // hook out slightly before circling
        if (s.stateCounter == 1) rotation = 0.1; // head almost straight at the player at the end
        if (target) navMoveTo(naga, s, findCirclePoint(naga, *target, s.clockwise, radius, rotation), 1.0F);
        break;
    }
    case MovementState::Daze:
        navStop(naga, s);
        setDazed(naga, s, true);
        setCharging(naga, s, false);
        break;
    }
}

// ---- goals -------------------------------------------------------------------------------------------

class NagaGoal : public ge::Goal {
public:
    explicit NagaGoal(std::int64_t uid) : mUid(uid) {}

protected:
    Mob* mob() const {
        auto* m = mobById(mUid);
        return m && m->isAlive() ? m : nullptr;
    }
    NagaState* state() const {
        auto it = R().states.find(mUid);
        return it != R().states.end() ? it->second.get() : nullptr;
    }
    std::int64_t mUid;
};

/// Entity#getFluidHeight(FluidTags.WATER): how far the water surface is above the bottom of the
/// mob's box, over the blocks its box touches. A water block's surface is y + amount / 9 (amount 8
/// for a source or falling water, 8 - liquid_depth for flowing water), y + 1 under more water.
double waterHeight(Mob& m) {
    auto& region = m.getDimensionBlockSource();
    auto  bb     = m.getAABB();
    auto  water  = [&](BlockPos const& p) -> Block const* {
        for (Block const* b : {&region.getBlock(p), &region.getLiquidBlock(p)}) {
            auto id = game::blockId(*b);
            if (id == "minecraft:water" || id == "minecraft:flowing_water") return b;
        }
        return nullptr;
    };
    double best = 0.0;
    int    x0 = static_cast<int>(std::floor(bb.min.x + 0.001)), x1 = static_cast<int>(std::ceil(bb.max.x - 0.001));
    int    y0 = static_cast<int>(std::floor(bb.min.y + 0.001)), y1 = static_cast<int>(std::ceil(bb.max.y - 0.001));
    int    z0 = static_cast<int>(std::floor(bb.min.z + 0.001)), z1 = static_cast<int>(std::ceil(bb.max.z - 0.001));
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z) {
                Block const* b = water(BlockPos{x, y, z});
                if (!b) continue;
                double surface;
                if (water(BlockPos{x, y + 1, z})) surface = y + 1.0;
                else {
                    auto states = game::statesOf(*b);
                    auto it     = states.find("liquid_depth");
                    int  depth  = it != states.end() ? std::atoi(it->second.c_str()) : 0;
                    int  amount = depth == 0 || depth >= 8 ? 8 : 8 - depth;
                    surface     = y + amount / 9.0;
                }
                if (surface >= bb.min.y) best = std::max(best, surface - bb.min.y);
            }
    return best;
}

/// FloatGoal.
class FloatGoal final : public NagaGoal {
public:
    explicit FloatGoal(std::int64_t uid) : NagaGoal(uid) { flags = Jump; }
    bool canUse() override {
        auto* m = mob();
        // isInWater() && getFluidHeight(WATER) > getFluidJumpThreshold() (0.4: eye height 2.85 of a
        // 3-high mob); Naga#isInLava is always false
        return m && m->isInWater() && waterHeight(*m) > 0.4;
    }
    bool requiresUpdateEveryTick() override { return true; }
    void tick() override {
        auto* m = mob();
        auto* s = state();
        if (m && s && s->random.nextFloat() < 0.8F) push(*m, Vec3{0.0f, 0.04f, 0.0f}); // jumpInLiquid
    }
};

/// SimplifiedAttackGoal with Naga#doHurtTarget.
class AttackGoal final : public NagaGoal {
public:
    using NagaGoal::NagaGoal;
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto* t = targetOf(*m);
        return t && inRange(*m, *t);
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
        auto* t = targetOf(*m);
        if (!t) return;
        if (inRange(*m, *t) && m->canSee(t->getEyePos(), ShapeType::Collision)) {
            mAttackTick = 20;
            m->swing(ActorSwingSource::Attack, HandSlot::Mainhand);
            nagaHurtTarget(*m, *t);
        }
    }

private:
    /// Mob#isWithinMeleeAttackRange.
    static bool inRange(Mob& m, Actor& t) {
        static double const reach = std::sqrt(2.04F) - 0.6F;
        auto const&         a     = m.getAABB();
        auto const&         b     = t.getAABB();
        return a.min.x - reach < b.max.x && a.max.x + reach > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z - reach < b.max.z &&
               a.max.z + reach > b.min.z;
    }
    int mAttackTick = 0;
};

/// NagaSmashGoal.
class SmashGoal final : public NagaGoal {
public:
    using NagaGoal::NagaGoal;
    bool canUse() override {
        auto* m = mob();
        return m && ActorCollision::hasHorizontalCollision(m->getEntityContext()) && mobGriefing();
    }
    void start() override {
        auto* m = mob();
        auto* s = state();
        if (!m || !s) return;
        auto const& bb = m->getAABB();
        BlockPos    min{int(std::floor(bb.min.x - 0.75)), int(std::floor(bb.min.y + 1.01)), int(std::floor(bb.min.z - 0.75))};
        BlockPos    max{int(std::floor(bb.max.x + 0.75)), int(std::floor(bb.max.y + 0.0)), int(std::floor(bb.max.z + 0.75))};
        clearBox(*m, *s, min, max);
    }
    static void clearBox(Mob& m, NagaState& s, BlockPos const& min, BlockPos const& max) {
        auto& region = m.getDimensionBlockSource();
        if (!region.hasChunksAt(min, 0, false) || !region.hasChunksAt(max, 0, false)) return;
        bool all = shouldDestroyAllBlocks(m, s);
        for (int x = min.x; x <= max.x; ++x)
            for (int y = min.y; y <= max.y; ++y)
                for (int z = min.z; z <= max.z; ++z) {
                    BlockPos     pos{x, y, z};
                    Block const& block  = region.getBlock(pos);
                    bool         leaves = isLeaves(block);
                    if (leaves || (all && canDestroyBlock(region, pos))) destroyBlock(region, pos, !leaves);
                }
    }
};

/// NagaMovementPattern (MOVE, LOOK).
class PatternGoal final : public NagaGoal {
public:
    explicit PatternGoal(std::int64_t uid) : NagaGoal(uid) { flags = Move | Look; }
    bool canUse() override {
        auto* m = mob();
        if (!m) return false;
        auto* t = targetOf(*m);
        return t && areSelfAndTargetInHome(*m, t);
    }
    bool requiresUpdateEveryTick() override { return true; }
    void stop() override {
        if (auto* s = state()) {
            setState(*s, MovementState::Circle);
            s->stateCounter = 15;
            s->clockwise    = false;
        }
    }
    void tick() override {
        auto* m = mob();
        auto* s = state();
        if (m && s) tickPattern(*m, *s);
    }
};

/// RandomPos#generateRandomDirectionWithinRadians.
std::optional<BlockPos> directionWithinRadians(core::jrand::RandomSource& r, int h, int v, int yOffset, double x, double z, double maxAngle) {
    double d0 = std::atan2(z, x) - static_cast<double>(static_cast<float>(kPi) / 2.0F);
    double d1 = d0 + static_cast<double>(2.0F * r.nextFloat() - 1.0F) * maxAngle;
    double d2 = std::sqrt(r.nextDouble()) * static_cast<double>(std::sqrt(2.0F)) * h;
    double d3 = -d2 * std::sin(d1);
    double d4 = d2 * std::cos(d1);
    if (std::abs(d3) > h || std::abs(d4) > h) return std::nullopt;
    int i = r.nextInt(2 * v + 1) - v + yOffset;
    return BlockPos{int(std::floor(d3)), i, int(std::floor(d4))};
}

/// WalkNodeEvaluator#getPathTypeFromState, reduced to the classes whose default malus matters here.
enum class PathClass { Open, Blocked, Water, Lava, DamageFire, DamageOther, PowderSnow, Honey };
PathClass pathClassAt(BlockSource& region, BlockPos const& pos) {
    for (Block const* b : {&region.getBlock(pos), &region.getLiquidBlock(pos)}) {
        auto const& id = b->getTypeName();
        if (id == "minecraft:water" || id == "minecraft:flowing_water") return PathClass::Water;
        if (id == "minecraft:lava" || id == "minecraft:flowing_lava") return PathClass::Lava;
    }
    auto const& id = region.getBlock(pos).getTypeName();
    // isBurningBlock: fire, soul fire, magma, lit campfires (Bedrock: extinguished false)
    if (id == "minecraft:fire" || id == "minecraft:soul_fire" || id == "minecraft:magma") return PathClass::DamageFire;
    if (id == "minecraft:campfire" || id == "minecraft:soul_campfire") {
        auto states = game::statesOf(region.getBlock(pos));
        auto it     = states.find("extinguished");
        if (it == states.end() || (it->second != "true" && it->second != "1")) return PathClass::DamageFire;
    }
    if (id == "minecraft:cactus" || id == "minecraft:sweet_berry_bush") return PathClass::DamageOther;
    if (id == "minecraft:powder_snow") return PathClass::PowderSnow;
    if (id == "minecraft:honey_block") return PathClass::Honey;
    return hasCollision(region, pos) ? PathClass::Blocked : PathClass::Open;
}

/// GoalUtils#hasMalus for the Naga (no malus overrides): the default malus of
/// WalkNodeEvaluator#getPathTypeStatic at `pos` is not 0. Blocked (-1), water (8), lava (-1),
/// powder snow (-1), damaging blocks; over magma / campfire / cactus / berry bush (DAMAGE_*) or
/// honey (STICKY_HONEY 8); and, for a walkable node, checkNeighbourBlocks: fire or lava beside it
/// (DANGER_FIRE 8), cactus / berry bush (DANGER_OTHER 8) or water (WATER_BORDER 8).
bool hasMalus(BlockSource& region, BlockPos const& pos) {
    auto here = pathClassAt(region, pos);
    if (here != PathClass::Open) return true;
    if (pos.y >= region.getMinHeight() + 1) {
        auto below = pathClassAt(region, BlockPos{pos.x, pos.y - 1, pos.z});
        if (below == PathClass::Open || below == PathClass::Water || below == PathClass::Lava) return false; // OPEN: no neighbour check
        if (below == PathClass::PowderSnow) return false; // DANGER_POWDER_SNOW: malus 0
        if (below != PathClass::Blocked) return true;     // DAMAGE_FIRE, DAMAGE_OTHER, STICKY_HONEY
    }
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                auto c = pathClassAt(region, BlockPos{pos.x + dx, pos.y + dy, pos.z + dz});
                if (c == PathClass::DamageFire || c == PathClass::Lava || c == PathClass::DamageOther || c == PathClass::Water) return true;
            }
    return false;
}

/// RandomPos#generateRandomPos over DefaultRandomPos's checks: the candidate with the best
/// walk target value of 10 (Naga#getWalkTargetValue: 0 at home, Float.MIN_VALUE outside).
template <class Dir>
std::optional<Vec3> randomPos(Mob& naga, NagaState& s, Dir&& direction) {
    auto&                   region = naga.getDimensionBlockSource();
    std::optional<BlockPos> best;
    float                   bestValue = -std::numeric_limits<float>::infinity();
    BlockPos                origin    = blockPosOf(naga);
    for (int k = 0; k < 10; ++k) {
        auto dir = direction(s.random);
        if (!dir) continue;
        BlockPos pos{origin.x + dir->x, origin.y + dir->y, origin.z + dir->z};
        if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) continue; // isOutsideLimits
        // DefaultRandomPos#generateRandomPosTowardDirection: GoalUtils#isNotStable (nothing solid
        // below) and GoalUtils#hasMalus reject the candidate (no RandomPos#moveUpOutOfSolid here,
        // that is LandRandomPos)
        if (!hasCollision(region, BlockPos{pos.x, pos.y - 1, pos.z})) continue;
        if (hasMalus(region, pos)) continue;
        auto h = validHome(naga);
        bool in = !h || (std::abs(h->pos.x - pos.x) <= kXzHomeBounds && std::abs(h->pos.y - pos.y) <= kYHomeBounds && std::abs(h->pos.z - pos.z) <= kXzHomeBounds);
        float value = in ? 0.0F : std::numeric_limits<float>::min();
        if (value > bestValue) {
            bestValue = value;
            best      = pos;
        }
    }
    if (!best) return std::nullopt;
    return Vec3{best->x + 0.5f, float(best->y), best->z + 0.5f};
}

/// AttemptToGoHomeGoal (Naga's: clears the target when it starts).
class GoHomeGoal final : public NagaGoal {
public:
    explicit GoHomeGoal(std::int64_t uid) : NagaGoal(uid) { flags = Move; }
    bool canUse() override {
        auto* m = mob();
        auto* s = state();
        if (!m || !s) return false;
        auto h = validHome(*m);
        if (!ge::home(*m) || homeContains(*m, *m)) return false;
        if (!h) return false;
        auto   p    = m->getFeetPos();
        double tx   = h->pos.x + 0.5 - p.x, tz = h->pos.z + 0.5 - p.z;
        auto   want = randomPos(*m, *s, [&](core::jrand::RandomSource& r) { return directionWithinRadians(r, 16, 7, 0, tx, tz, kPi / 2.0F); });
        if (!want) return false;
        mWanted = *want;
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* s = state();
        return m && s && !navDone(*m, *s);
    }
    void start() override {
        auto* m = mob();
        auto* s = state();
        if (!m || !s) return;
        m->setTarget(nullptr);
        navStop(*m, *s);
        navMoveTo(*m, *s, mWanted, 1.0F);
    }

private:
    Vec3 mWanted{};
};

/// RandomStrollGoal(1, 1) inside the home area without a target, DefaultRandomPos.getPos(30, 7).
class StrollGoal final : public NagaGoal {
public:
    explicit StrollGoal(std::int64_t uid) : NagaGoal(uid) { flags = Move; }
    bool canUse() override {
        auto* m = mob();
        auto* s = state();
        if (!m || !s || !homeContains(*m, *m) || targetOf(*m)) return false;
        // RandomStrollGoal#canUse: nextInt(reducedTickDelay(1)) == 0 always
        (void)s->random.nextInt(1);
        auto want = randomPos(*m, *s, [](core::jrand::RandomSource& r) {
            int x = r.nextInt(2 * 30 + 1) - 30, y = r.nextInt(2 * 7 + 1) - 7, z = r.nextInt(2 * 30 + 1) - 30;
            return std::optional<BlockPos>(BlockPos{x, y, z});
        });
        if (!want) return false;
        mWanted = *want;
        return true;
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* s = state();
        return m && s && !navDone(*m, *s);
    }
    void start() override {
        auto* m = mob();
        auto* s = state();
        if (m && s) navMoveTo(*m, *s, mWanted, 1.0F);
    }
    void stop() override {
        auto* m = mob();
        auto* s = state();
        if (m && s) navStop(*m, *s);
    }

private:
    Vec3 mWanted{};
};

/// TargetGoal#canContinueToUse (follow range 80, alive, attackable).
bool targetStillValid(Mob& m, Actor& t) { return attackable(t) && distSqr(m.getFeetPos(), t.getFeetPos()) <= 80.0 * 80.0; }

/// HurtByTargetGoal.
class HurtByTargetGoal final : public NagaGoal {
public:
    explicit HurtByTargetGoal(std::int64_t uid) : NagaGoal(uid) { flags = Target; }
    bool canUse() override {
        auto* m = mob();
        auto* s = state();
        if (!m || !s || s->lastHurtByTimestamp == mTimestamp) return false;
        auto* a = byId(s->lastHurtBy);
        return a && a != m && attackable(*a);
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* t = m ? targetOf(*m) : nullptr;
        return t && targetStillValid(*m, *t);
    }
    void start() override {
        auto* m = mob();
        auto* s = state();
        if (!m || !s) return;
        if (auto* a = byId(s->lastHurtBy)) m->setTarget(a);
        mTimestamp = s->lastHurtByTimestamp;
    }
    void stop() override {
        if (auto* m = mob()) m->setTarget(nullptr);
    }

private:
    int mTimestamp = 0;
};

/// NearestAttackableTargetGoal(Player, mustSee false) with Naga's home condition.
class NearestPlayerGoal final : public NagaGoal {
public:
    explicit NearestPlayerGoal(std::int64_t uid) : NagaGoal(uid) { flags = Target; }
    bool canUse() override {
        auto* m = mob();
        auto* s = state();
        if (!m || !s) return false;
        if (s->random.nextInt(10) != 0) return false; // reducedTickDelay(10)
        // NearestAttackableTargetGoal#findTarget for Player.class: level.getNearestPlayer(targetConditions,
        // mob, x, eyeY, z). No search box (getTargetSearchArea is for other target types):
        // TargetingConditions#test keeps players within max(80 * visibility, 2) of the mob's feet,
        // and the nearest to (x, eyeY, z) wins.
        Vec3    feet  = m->getFeetPos();
        Vec3    eye{feet.x, m->getEyePos().y, feet.z};
        Player* best  = nullptr;
        double  bestD = 1e300;
        auto    level = ll::service::getLevel();
        if (!level) return false;
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (static_cast<int>(p.getDimensionId()) != static_cast<int>(m->getDimensionId()) || !attackable(p)) return true;
            double range = std::max(80.0 * visibilityPercent(p), 2.0);
            if (distSqr(feet, p.getFeetPos()) > range * range) return true;
            double d = distSqr(eye, p.getFeetPos());
            if (d < bestD) bestD = d, best = &p;
            return true;
        }));
        if (!best) return false;
        mFound = uidOf(*best);
        // Naga's override checks the mob's current target (upstream), not the one just found
        return areSelfAndTargetInHome(*m, targetOf(*m));
    }
    bool canContinueToUse() override {
        auto* m = mob();
        auto* t = m ? targetOf(*m) : nullptr;
        return t && targetStillValid(*m, *t);
    }
    void start() override {
        auto* m = mob();
        if (!m) return;
        if (auto* p = byId(mFound)) m->setTarget(p);
    }
    void stop() override {
        if (auto* m = mob()) m->setTarget(nullptr);
    }

private:
    std::int64_t mFound = 0;
};

// ---- the Naga's server step ------------------------------------------------------------------------

/// Naga#customServerAiStep (between the goals and the move control, as Mob#serverAiStep).
void customServerAiStep(Mob& naga, NagaState& s) {
    if (auto* t = targetOf(naga); t && (distSqr(naga.getFeetPos(), t->getFeetPos()) > 80.0 * 80.0 || !areSelfAndTargetInHome(naga, t)))
        naga.setTarget(nullptr);

    if (mobGriefing()) {
        auto const& bb  = naga.getAABB();
        bool        all = shouldDestroyAllBlocks(naga, s);
        BlockPos    min{int(std::floor(bb.min.x - 0.75)), int(std::floor(bb.min.y + (all ? 1.01F : 0.5F))), int(std::floor(bb.min.z - 0.75))};
        BlockPos    max{int(std::floor(bb.max.x + 0.75)), int(std::floor(bb.max.y + 1.0)), int(std::floor(bb.max.z + 0.75))};
        SmashGoal::clearBox(naga, s, min, max);
    }

    // if we get stuck in a hole/cave send us home, dont even deal with trying to get out
    if (s.tickCount % 20 == 0)
        if (auto h = validHome(naga); h && naga.getFeetPos().y < h->pos.y - 5) {
            naga.teleportTo(Vec3{float(h->pos.x), float(h->pos.y), float(h->pos.z)}, true, 0, 0, false);
            navStop(naga, s);
        }

    // if we are very close to the path point, go to the next point, unless the path is finished
    navTick(naga, s);

    // update health
    s.ticksSinceDamaged++;
    if (s.ticksSinceDamaged > kTicksBeforeHealing && s.ticksSinceDamaged % 20 == 0) naga.heal(1);

    if (s.damageDuringCurrentStun > 15) {
        forceCircle(s);
        s.damageDuringCurrentStun = 0;
    }
}

/// LivingEntity#getFrictionInfluencedSpeed(blockFriction) of the Naga: on the ground getSpeed()
/// (Mob#setSpeed by the MoveControl: the path's speed modifier times the movement speed
/// attribute; 0 before the first path) * 0.216 / friction^3 (1 on blocks of friction 0.6), in
/// the air Mob#getFlyingSpeed 0.02. The friction is the block's 0.500001 under the feet
/// (Entity#getBlockPosBelowThatAffectsMyMovement).
float slitherSpeed(Mob& naga, NagaState& s) {
    if (!naga.isOnGround()) return 0.02F;
    // Mob#getSpeed = the path's speed modifier * the MOVEMENT_SPEED attribute's value (the segment
    // boost, Speed / Slowness and any other modifier included)
    auto  attr  = naga.getAttribute(SharedAttributes::MOVEMENT_SPEED());
    float value = attr.mPtr ? attr.mPtr->mCurrentValue : static_cast<float>(kDefaultSpeed);
    float speed = s.navSpeed * value;
    auto   p     = naga.getFeetPos();
    BlockPos below{int(std::floor(p.x)), int(std::floor(p.y - 0.500001)), int(std::floor(p.z))};
    float  f = blockFriction(naga.getDimensionBlockSource().getBlock(below));
    return speed * (0.21600002F / (f * f * f));
}

/// NagaMoveControl#tick (after customServerAiStep): the slither on top of the engine's move
/// control, which walks the path.
void moveControl(Mob& naga) {
    auto* sp = stateOf(naga);
    if (!sp) return;
    auto& s = *sp;
    customServerAiStep(naga, s);
    if (s.state == MovementState::Daze) s.xxa = 0.0F;
    else if (s.state != MovementState::Charge && s.state != MovementState::Intimidate) s.xxa = std::cos(s.tickCount * 0.3F) * 0.6F;
    else s.xxa *= 0.8F;
    // LivingEntity#aiStep: xxa *= 0.98 (kept in the field) before travel
    s.xxa *= 0.98F;
    if (std::abs(s.xxa) * std::abs(s.xxa) < 1.0E-7F) return; // moveRelative: no input
    float amount = slitherSpeed(naga, s);
    float yaw    = naga.getRotation().y * static_cast<float>(kPi) / 180.0F;
    float x      = s.xxa * amount;
    push(naga, Vec3{x * std::cos(yaw), 0.0f, x * std::sin(yaw)});
}

// ---- the Naga's client effects (from the server) -------------------------------------------------

void tickEffects(Mob& naga, NagaState& s) {
    auto p     = naga.getFeetPos();
    int  death = gf::boss::ext::deathTime(naga);
    if (s.dazed && death < 10) {
        for (int i = 0; i < 5; i++) {
            double a = static_cast<double>(s.random.nextInt(360)) * kPi / 180.0;
            // Vec3(1.5, 0, 0).yRot(a)
            particle(naga, "minecraft:crit", Vec3{float(p.x + 1.5 * std::cos(a)), p.y + 2.15f, float(p.z - 1.5 * std::sin(a))});
        }
    }
    if (s.stunless && death <= 0) {
        BlockPos b = blockPosOf(naga);
        particle(naga, "minecraft:angry_villager",
                 Vec3{float(p.x + 2.0 * (2.0 * s.random.nextDouble() - 1.0) * 0.85), b.y + 2.25f, float(p.z + 2.0 * (2.0 * s.random.nextDouble() - 1.0) * 0.85)});
    }
}

// ---- damage ----------------------------------------------------------------------------------------

/// DamageTypeTags.IS_EXPLOSION: the imported tag holds only upstream's additions (lich_bomb), so the
/// vanilla 1.21.1 members (data/minecraft/tags/damage_type/is_explosion.json: fireworks, explosion,
/// player_explosion, bad_respawn_point) are listed here; fireball and wither_skull are not members.
bool isExplosion(std::string const& type) {
    if (gf::tags::has(gf::tags::Registry::DamageType, "minecraft:is_explosion", type)) return true;
    return type == "minecraft:explosion" || type == "minecraft:player_explosion" || type == "minecraft:fireworks" ||
           type == "minecraft:bad_respawn_point";
}

/// Naga#isInvulnerableTo + NagaSegment#hurt (2/3 of the damage to the head, nothing through a
/// hidden segment).
bool hurt(Actor& self, float& amount, std::string const& type, Actor* attacker) {
    auto* naga = asMob(self);
    if (!naga) return true;
    if (attacker && !homeContains(*naga, *attacker)) return false; // reject damage from outside of our home radius
    if (isExplosion(type)) return false;
    auto const& part = ge::ext::hurtPart();
    if (!part.empty()) {
        auto* s = stateOf(self);
        if (s && part.rfind("segment_", 0) == 0) {
            int i = std::atoi(part.c_str() + 8);
            if (i >= 0 && i < kMaxSegments && !s->segments[i].active) return false;
        }
        amount = amount * 2.0F / 3.0F;
    }
    return true;
}

// ---- the death (BaseTFBoss steps) ----------------------------------------------------------------

/// EntityUtil#bossChestLocation's center.
Vec3 chestCenter(Mob& naga) {
    if (auto h = validHome(naga)) return Vec3{h->pos.x + 0.5f, h->pos.y - 1 + 0.5f, h->pos.z + 0.5f};
    auto b = blockPosOf(naga);
    return Vec3{b.x + 0.5f, b.y + 0.5f, b.z + 0.5f};
}

/// The ground under a point (Level#clip from +2 to -3, colliders and water).
float groundAt(BlockSource& region, Vec3 const& p) {
    int x = int(std::floor(p.x)), z = int(std::floor(p.z));
    for (int y = int(std::floor(p.y + 2.0f)); y >= int(std::floor(p.y - 3.0f)); --y) {
        BlockPos     bp{x, y, z};
        Block const& block = region.getBlock(bp);
        auto         id    = game::blockId(block);
        if (id == "minecraft:water" || id == "minecraft:flowing_water") return std::min(p.y + 2.0f, float(y) + 1.0f);
        if (!hasCollision(region, bp)) continue;
        AABB b = block.getBlockType().getCollisionShape(block, region, bp, nullptr);
        return std::min(p.y + 2.0f, b.max.y);
    }
    return p.y - 3.0f;
}

/// Naga#tickDeathAnimation: the slithering trail of composter particles to the chest.
void tickDeathAnimation(Mob& naga, int deathTime) {
    if (deathTime < kDeathAnimationDuration) return;
    auto   feet  = naga.getFeetPos();
    Vec3   start{feet.x, feet.y + 1.5f, feet.z};
    Vec3   end   = chestCenter(naga);
    Vec3   diff{end.x - start.x, end.y - start.y, end.z - start.z};
    double angle = std::atan2(end.z - start.z, end.x - start.x) * 180.0 / kPi + 180.0;
    double xMul  = std::fmod(angle, 180.0);
    xMul         = std::min(xMul, 180.0 - xMul);
    xMul         = std::pow(xMul / 90.0, 1.5) * 2.0;
    double zMul  = std::fmod(angle + 90.0, 180.0);
    zMul         = std::min(zMul, 180.0 - zMul);
    zMul         = std::pow(zMul / 90.0, 1.5) * 2.0;
    auto& region = naga.getDimensionBlockSource();
    for (int p = 1; p <= 4; p++) {
        int trailTime = (deathTime - kDeathAnimationDuration) - p;
        if (trailTime < 0) continue;
        for (double d = 0.0; d < 1.0; d += 0.25) {
            double t = trailTime - d;
            if (t < 0.0) continue;
            double factor = t / static_cast<double>(kDeathParticlesDuration);
            Vec3   at{float(start.x + diff.x * factor + std::sin(t * kPi * 0.075) * xMul), float(start.y + diff.y * factor + std::sin(t * kPi * 0.025) * 0.1),
                      float(start.z + diff.z * factor + std::cos(t * kPi * 0.0625) * zMul)};
            at.y = groundAt(region, at) + 0.15f;
            particle(naga, "minecraft:composter", at);
        }
    }
}

// ---- the behaviour ---------------------------------------------------------------------------------

void addGoals(Mob& naga, NagaState& s) {
    auto uid = s.uid;
    ge::addGoal(naga, 1, std::make_shared<FloatGoal>(uid));
    ge::addGoal(naga, 2, std::make_shared<AttackGoal>(uid));
    ge::addGoal(naga, 3, std::make_shared<SmashGoal>(uid));
    ge::addGoal(naga, 4, std::make_shared<PatternGoal>(uid));
    ge::addGoal(naga, 5, std::make_shared<GoHomeGoal>(uid));
    ge::addGoal(naga, 8, std::make_shared<StrollGoal>(uid));
    ge::addTargetGoal(naga, 1, std::make_shared<HurtByTargetGoal>(uid));
    ge::addTargetGoal(naga, 2, std::make_shared<NearestPlayerGoal>(uid));
}

/// Naga#finalizeSpawn: the difficulty health boost (once, healed to full) and on every load the
/// modifier back on the attribute.
void applyHealthBoost(Mob& naga) {
    auto& saved = ge::data(naga, "naga:health_boost");
    auto  attr  = naga.getMutableAttribute(SharedAttributes::HEALTH());
    auto* inst  = attr.mInstance->mPtr;
    if (!inst) return;
    auto id = mce::UUID::fromStringHash("gloamingforest:difficulty_health_boost");
    bool fresh = !saved.is_number();
    float boost = 0.0F;
    if (fresh) {
        auto level = ll::service::getLevel();
        auto diff  = level ? level->getDifficulty() : SharedTypes::Legacy::Difficulty::Normal;
        if (diff != SharedTypes::Legacy::Difficulty::Easy) boost = diff == SharedTypes::Legacy::Difficulty::Hard ? 130.0F : 80.0F;
        saved = boost;
    } else boost = saved.get<float>();
    if (boost <= 0.0F) return;
    for (auto const& m : inst->getModifiers())
        if (m.mId.get() == id) return; // already there (Bedrock saved it)
    AttributeModifier mod(id, "gloamingforest:difficulty_health_boost", boost, AttributeModifierOperation::OperationAddition, AttributeOperands::OperandMax, true);
    inst->addModifier(mod, attr.mContext);
    if (fresh) naga.heal(naga.getMaxHealth());
}

void onSpawn(Actor& self) {
    auto* naga = asMob(self);
    if (!naga) return;
    auto& s = stateFor(self);
    if (auto it = R().seeds.find(s.uid); it != R().seeds.end()) {
        s.random.setSeed(it->second);
        R().seeds.erase(it);
    }
    // healthPerSegment = getMaxHealth() / 10 in the constructor (before finalizeSpawn's boost)
    s.healthPerSegment = 120.0F / 10.0F;
    applyHealthBoost(*naga);
    addSegments(*naga, s);
    addGoals(*naga, s);
    ge::setMoveControl(*naga, [](Mob& m) { moveControl(m); });
    // Naga#getBossBarOverlay NOTCHED_10, #getBossBarColor 0x5E9916
    gf::boss::ext::setBarStyle(*naga, 0x5E9916, gf::boss::ext::Overlay::Notched10, false);
    ge::setProperty(self, "gloamingforest:dazed", s.dazed);
    ge::setProperty(self, "gloamingforest:charging", s.charging);
    ge::setProperty(self, "gloamingforest:stunless", s.stunless);
    s.initialized = true;
    log().info("naga: {} spawned with {} health ({} segments)", s.uid, naga->getHealth(), segmentsFor(float(naga->getHealth()), s.healthPerSegment));
}

/// A Naga whose U20 state was dropped while F8 kept its own (its chunk was unloaded for a while,
/// F8 did not run onSpawn again): upstream saves neither the segments nor the movement pattern nor the
/// synced flags (Naga#addAdditionalSaveData has none of them), so it comes back as a fresh one:
/// DATA_DAZE / DATA_CHARGE / DATA_STUNLESS off (the entity properties Bedrock saved are reset), the
/// segments re-attached to the part actors F8 kept and inactive until setSegmentsPerHealth activates
/// them, CIRCLE with a counter of 15. The goals and the move control F8 kept find this state by id.
void reattach(Mob& naga, NagaState& s) {
    s.initialized      = true;
    s.healthPerSegment = 120.0F / 10.0F;
    auto parts         = ge::ext::parts(naga);
    if (parts.size() >= static_cast<std::size_t>(kMaxSegments)) {
        auto feet = naga.getFeetPos();
        for (int i = 0; i < kMaxSegments; i++) {
            auto& seg = s.segments[i];
            seg.part  = parts[i];
            seg.pos   = feet;
            setSegmentActive(naga, seg, false);
        }
    } else addSegments(naga, s);
    ge::setProperty(naga, "gloamingforest:dazed", false);
    ge::setProperty(naga, "gloamingforest:charging", false);
    ge::setProperty(naga, "gloamingforest:stunless", false);
    log().info("naga: {} came back without its state: reset (health {})", s.uid, naga.getHealth());
}

/// Drops the states of Nagas that were not seen for 200 ticks (unloaded or removed).
void sweepStates() {
    auto now = serverTick();
    std::erase_if(R().states, [&](auto const& kv) { return now > kv.second->lastSeen + 200; });
}

void onTick(Actor& self) {
    auto* naga = asMob(self);
    if (!naga) return;
    auto& s = stateFor(self);
    if (!s.initialized) reattach(*naga, s);
    s.tickCount++;
    if (s.tickCount % 200 == 0) sweepStates();
    tickEffects(*naga, s);
    setSegmentsPerHealth(*naga, s);
}

void onAiStep(Actor& self) {
    auto* naga = asMob(self);
    if (!naga) return;
    moveSegments(*naga, stateFor(self));
}

struct Handlers {
    bool installed = false;
};
Handlers& H() {
    static Handlers h;
    return h;
}

} // namespace

// ---- internal API ----------------------------------------------------------------------------------

char const* stateName(MovementState state) {
    switch (state) {
    case MovementState::Intimidate:
        return "INTIMIDATE";
    case MovementState::Crumble:
        return "CRUMBLE";
    case MovementState::Charge:
        return "CHARGE";
    case MovementState::StunlessCharge:
        return "STUNLESS_CHARGE";
    case MovementState::Circle:
        return "CIRCLE";
    case MovementState::Daze:
        return "DAZE";
    }
    return "?";
}

NagaState* stateOf(Actor& naga) {
    if (naga.getTypeName() != kNagaId) return nullptr;
    return &stateFor(naga);
}

void dropStateForTest(Actor& naga) { R().states.erase(uidOf(naga)); }

void seedNagaForTest(Actor& naga, std::int64_t seed) {
    auto it = R().states.find(uidOf(naga));
    if (it != R().states.end()) it->second->random.setSeed(seed);
    else R().seeds[uidOf(naga)] = seed;
}

void doDaze(Mob& naga) {
    if (auto* s = stateOf(naga)) doDazeImpl(naga, *s);
}
void doCircle(Mob& naga) {
    if (auto* s = stateOf(naga)) doCircleImpl(*s);
}

bool withinHome(Mob& naga, Actor& entity) { return homeContains(naga, entity); }

bool nagaCanDestroyBlock(BlockSource& region, BlockPos const& pos) { return canDestroyBlock(region, pos); }

std::string shieldCooldownCategory(ItemStack const& stack) {
    if (!stack.isNull() && stack.mItem) {
        auto const& category = stack.mItem->getCooldownCategory();
        if (!category.getString().empty()) return category.getString();
    }
    auto id = game::items::idOf(stack);
    auto colon = id.find(':');
    return colon == std::string::npos ? id : id.substr(colon + 1);
}

/// player.getUsedItemHand() of the raised shield: Player::getCurrentActiveShield is the stack the
/// player blocks with (Bedrock blocks with a shield in either hand); it is the offhand slot itself
/// when the offhand blocks. Without an active shield, the offhand when it holds minecraft:shield.
bool shieldInOffhand(Player const& player) {
    auto const& active  = player.getCurrentActiveShield();
    auto const& offhand = player.getOffhandSlot();
    if (&active == &offhand) return true;
    if (&active == &player.getCarriedItem()) return false;
    // a copy: Bedrock looks at the offhand shield first
    if (!game::items::isEmpty(active)) return game::items::idOf(active) == game::items::idOf(offhand);
    return game::items::idOf(offhand) == "minecraft:shield";
}

int playerMotionCountForTest() { return gPlayerMotions; }

bool isExplosionDamage(std::string const& type) { return isExplosion(type); }

/// Naga#doHurtTarget.
bool nagaHurtTarget(Mob& naga, Actor& toAttack) {
    auto* sp = stateOf(naga);
    if (!sp) return false;
    auto& s = *sp;
    if (toAttack.hasCategory(ActorCategory::Mob) && toAttack.isBlocking()) {
        if (s.state == MovementState::Charge) {
            Vec3 motion = naga.getPosDelta();
            if (toAttack.isPlayer()) {
                auto& player = static_cast<Player&>(toAttack);
                // the used item (the raised shield) takes 5 durability
                bool      offhand = shieldInOffhand(player);
                ItemStack used    = offhand ? player.getOffhandSlot() : player.getCarriedItem();
                if (!game::items::isEmpty(used)) {
                    used.hurtAndBreak(5, &player);
                    if (offhand) player.setOffhandSlot(used);
                    else player.setCarriedItem(used);
                }
                // toAttack.push on a ServerPlayer only changes the server's copy of its motion (no
                // hurtMarked: nothing is sent to its own client); what moves the player is the
                // MovePlayerPacket, whose handler pushes the client by (x * 3, y + 0.75, z * 3).
                // Bedrock has no additive motion packet (SetActorMotionPacket sets the velocity), so
                // the push is added to the server's delta, which under server-authoritative movement
                // is the client's own delta from its last PlayerAuthInputPacket (one tick old at most).
                auto v = player.getPosDelta();
                gf::movement::applyMotion(player, Vec3{v.x + motion.x * 3.0f, v.y + motion.y + 0.75f, v.z + motion.z * 3.0f});
            } else push(toAttack, Vec3{motion.x * 1.5f, 0.5f, motion.z * 1.5f});
            push(naga, Vec3{motion.x * -1.25f, 0.5f, motion.z * -1.25f});
            gf::combat::hurt(naga, 2.0F, "minecraft:generic");
            gf::fx::sound(naga.getDimension(), "minecraft:item.shield.block", toAttack.getFeetPos(), 1.0F, 0.8F + s.random.nextFloat() * 0.4F);
            doDazeImpl(naga, s);
            log().info("naga: {} hit a shield while charging: dazed for {} ticks", s.uid, s.stateCounter);
            return false;
        }
        if (s.state == MovementState::StunlessCharge) {
            if (toAttack.isPlayer()) {
                auto& player  = static_cast<Player&>(toAttack);
                bool      offhand = shieldInOffhand(player);
                ItemStack used    = offhand ? player.getOffhandSlot() : player.getCarriedItem();
                if (!game::items::isEmpty(used)) {
                    // ItemCooldowns#addCooldown(item, 200): the item's own cooldown category
                    // (Bedrock's shield: the one an axe hit also disables it through)
                    std::string category = shieldCooldownCategory(used);
                    used.hurtAndBreak(10, &player);
                    if (offhand) player.setOffhandSlot(used);
                    else player.setCarriedItem(used);
                    gf::item::cooldown(player, category, 200);
                }
                player.stopUsingItem();
                // entity event 30: the shield break sound
                gf::fx::sound(player.getDimension(), "minecraft:item.shield.break", player.getFeetPos(), 0.8F, 0.8F + s.random.nextFloat() * 0.4F);
            }
            gf::combat::hurt(toAttack, 4.0F, "minecraft:mob_attack", &naga, &naga);
            gf::fx::sound(naga.getDimension(), "minecraft:entity.fox.bite", naga.getFeetPos(), 2.0F, 0.5F);
            doCircleImpl(s);
            log().info("naga: {} broke through a shield", s.uid);
            return false;
        }
    }
    if (!s.dazed) {
        bool result = static_cast<bool>(naga.doHurtTarget(&toAttack, SharedTypes::Legacy::ActorDamageCause::EntityAttack));
        if (result) {
            // charging, apply extra pushback
            float yaw = naga.getRotation().y * static_cast<float>(kPi) / 180.0F;
            push(toAttack, Vec3{-std::sin(yaw) * 2.0F, 0.4F, std::cos(yaw) * 2.0F});
        }
        return result;
    }
    return false;
}

void installNaga() {
    ge::registerType(kNagaId, ge::Behaviour{
                                  .onSpawn = onSpawn,
                                  .tick    = onTick,
                                  .aiStep  = onAiStep,
                                  .hurt    = hurt,
                              });
    gf::boss::registerBoss({
        .entityId        = kNagaId,
        .homeStructure   = kCourtyard,
        .lootTable       = "gloamingforest:entities/naga",
        .deathChestBlock = "gloamingforest:gloaming_oak_chest",
        .bar             = {.rgb = 0x5E9916, .darken = false, .fog = false},
        .deathTicks      = kDeathAnimationDuration + kDeathParticlesDuration,
    });
    gf::boss::ext::setHomeRadius(kNagaId, kHomeRadius);
    gf::boss::ext::setLifecycle(kNagaId, gf::boss::ext::Lifecycle{
                                             .tickDeath = [](Mob& boss, int deathTime) { tickDeathAnimation(boss, deathTime); },
                                             .deathAnimationFinished = [](Mob&, int deathTime) { return deathTime >= kDeathAnimationDuration + kDeathParticlesDuration; },
                                             .deathContainer =
                                                 [](Mob&, core::jrand::RandomSource& random) {
                                                     return std::string(random.nextBoolean() ? "gloamingforest:gloaming_oak_chest" : "gloamingforest:canopy_chest");
                                                 },
                                             .onPostRemoval =
                                                 [](Mob& boss) {
                                                     // Naga#makePoofParticles: the death sound, then 20 explosions
                                                     auto* s = stateOf(boss);
                                                     if (!s) return;
                                                     gf::fx::sound(boss.getDimension(), "entity.gloamingforest.naga.hurt", boss.getFeetPos(), 1.25F, voicePitch(s->random) * 0.25F);
                                                     makePoofAt(boss, *s, boss.getFeetPos());
                                                 },
                                         });
    gf::boss::registerSpawner({
        .blockId  = kSpawnerId,
        .entityId = kNagaId,
        .range    = 50, // BossSpawnerBlockEntity.LONG_RANGE
        .particle = "minecraft:crit",
    });
    if (!H().installed) {
        H().installed = true;
        // Naga#isInvulnerableTo's direct entity: a projectile (or any direct entity) outside the
        // 46 / 7 home box deals nothing, whoever shot it (the causing entity is checked in hurt);
        // a hit on a segment is the Naga's (NagaSegment#hurt -> parent.hurt with the same source)
        gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
            if (!d.direct) return gf::combat::Verdict::Pass;
            Actor* target = &d.victim;
            if (target->getTypeName() != kNagaId) {
                auto* parent = ge::ext::parentOf(*target);
                if (!parent || parent->getTypeName() != kNagaId) return gf::combat::Verdict::Pass;
                target = parent;
            }
            auto* naga = asMob(*target);
            if (naga && !homeContains(*naga, *d.direct)) return gf::combat::Verdict::Cancel;
            return gf::combat::Verdict::Pass;
        });
        // Naga#hurt: a hit that landed resets the healing timer, adds to the stun damage and is
        // remembered for HurtByTargetGoal (LivingEntity#lastHurtByMob)
        gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
            if (d.victim.getTypeName() != kNagaId) return;
            auto* s = stateOf(d.victim);
            if (!s) return;
            s->ticksSinceDamaged = 0;
            if (s->dazed) s->damageDuringCurrentStun += static_cast<int>(d.amount);
            if (d.attacker && d.attacker != &d.victim && d.attacker->hasCategory(ActorCategory::Mob)) {
                s->lastHurtBy = uidOf(*d.attacker);
                s->lastHurtByTimestamp++;
            }
        });
        // forget the states of Nagas that are gone (R16)
        gf::game::db::onWorldClosed([] {
            R().states.clear();
            R().seeds.clear();
        });
    }
    log().info("naga: registered the Naga, its spawner and boss lifecycle");
}

void uninstallNaga() { R().states.clear(); }

} // namespace gf::feature::naga

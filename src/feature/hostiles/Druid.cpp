// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/monster/SkeletonDruid.java
//   src/main/java/gloamingforest/entity/projectile/NatureBolt.java
//   src/main/java/gloamingforest/entity/projectile/TFThrowable.java
//   src/main/java/gloamingforest/util/entities/EntityUtil.java (canDestroyBlock)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Also translated: vanilla Minecraft 1.21.1's AbstractSkeleton#finalizeSpawn (the equipment, its
// enchantments and the Halloween pumpkin head).
//
// The skeleton druid: AbstractSkeleton's goals are the BP's behaviours (sun, wolves, stroll, look,
// targets); the weapon goal is reassessWeaponGoal's: holding a hoe the native RangedAttackGoal
// (1.25, 60, 5) casts nature bolts, anything else the BP's MeleeAttack 1.2 (group gf:melee). A
// baby druid (the swarm spider's jockey) holds a stick: no bolts.

#include <cmath>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/FertilizerType.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "core/Holiday.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/fx/Fx.h"
#include "feature/hostiles/Common.h"
#include "feature/hostiles/Hostiles.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::hostiles::druid {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

namespace {

using Slot = SharedTypes::Legacy::EquipmentSlot;

/// `getItem() instanceof HoeItem`.
bool holdsHoe(Mob& m) {
    auto held = mainHandId(m);
    return held.size() > 4 && held.ends_with("_hoe");
}

/// AbstractSkeleton#finalizeSpawn: SkeletonDruid#populateDefaultEquipmentSlots (a golden hoe, a
/// stick for a baby), their enchantments, the Halloween pumpkin.
void finalizeSpawn(Mob& m) {
    if (ge::data(m, "hostiles:baby").is_boolean()) fireEvent(m, "gf:become_baby");
    bool baby = ge::data(m, "hostiles:baby").is_boolean();
    if (auto s = gf::game::items::make(baby ? "minecraft:stick" : "minecraft:golden_hoe")) setMainHand(m, *s);
    enchantSpawnEquipment(m);
    auto& r = levelRandom();
    if (gf::game::items::isEmpty(m.getItemSlot(Slot::Head)) && gf::core::holiday::isHalloweenWeek() && r.nextFloat() < 0.25F) {
        if (auto s = gf::game::items::make(r.nextFloat() < 0.1F ? "minecraft:lit_pumpkin" : "minecraft:carved_pumpkin")) {
            m.setItemSlot(Slot::Head, *s);
            ge::data(m, "hostiles:pumpkin") = true; // armorDropChances[HEAD] = 0
        }
    }
}

void druidTick(Actor& self) {
    if (despawnInPeaceful(self)) return;
    auto* m = asMob(self);
    if (!m || !self.isAlive()) return;
    if (firstSpawn(self)) finalizeSpawn(*m);
    if (!nativeGoalsPresent(self)) {
        // SkeletonDruid#registerGoals: RangedAttackGoal(this, 1.25, 60, 5) at 4, kept only while it
        // holds a hoe (reassessWeaponGoal)
        ge::addGoal(*m, 4, std::make_shared<RangedAttackGoal>(*m, 1.25, 60, 60, 5.0F, [](Mob& d, Actor& t, float) { performRangedAttack(d, t); }, holdsHoe));
    }
    // reassessWeaponGoal: anything but a hoe takes AbstractSkeleton's melee goal
    setGroup(self, "melee", !holdsHoe(*m));
    syncVanillaMove(*m); // the stroll (5) waits while the ranged goal (4) runs
}

// ---- NatureBolt ------------------------------------------------------------------------------------------

/// NatureBolt#handleEntityEvent(DEATH): 8 oak leaf block particles.
void boltBurst(Actor& bolt) {
    auto& r = levelRandom();
    for (int i = 0; i < 8; ++i)
        gf::fx::particle(bolt.getDimension(), "minecraft:block", bolt.getFeetPos(),
                         Vec3{float(r.nextGaussian() * 0.05), float(r.nextDouble() * 0.2), float(r.nextGaussian() * 0.05)}, 1, {{"block", "minecraft:oak_leaves"}});
}

/// EntityUtil#canDestroyBlock (+ isSolidRender, no block entity, the replaceable tag).
bool canReplaceBlock(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (block.isAir() || region.getBlockEntity(pos)) return false;
    float hardness = block.getBlockType().getDestroySpeed();
    if (hardness < 0.0F || hardness >= 50.0F) return false;
    if (!mobs::stableDestination(region, BlockPos{pos.x, pos.y + 1, pos.z})) return false; // isSolidRender
    return gf::tags::has(gf::tags::Registry::Block, "gloamingforest:druid_projectile_replaceable", gf::game::blockId(block));
}

bool boltHitBlock(Actor& bolt, BlockPos const& pos, int) {
    auto& region = bolt.getDimensionBlockSource();
    if (mobGriefing()) {
        Block const& state = region.getBlock(pos);
        Block const& above = region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z});
        if (state.getBlockType().canBeFertilized(region, pos, above)) {
            // BonemealableBlock#performBonemeal (server)
            state.getBlockType().onFertilized(region, pos, gf::projectile::ext::owner(bolt), FertilizerType::Bonemeal);
        } else if (mobs::legacySolid(region, pos) && canReplaceBlock(region, pos)) {
            if (auto const* leaves = gf::game::blockWithStates("minecraft:birch_leaves", {{"persistent_bit", true}})) {
                gf::game::setBlock(region, pos, *leaves);
                log().info("hostiles: a nature bolt turned {} {} {} into birch leaves", pos.x, pos.y, pos.z);
            }
        }
    }
    boltBurst(bolt);
    return true;
}

bool boltHitEntity(Actor& bolt, Actor& target) {
    Actor* owner = gf::projectile::ext::owner(bolt);
    if (isLiving(target) && (!owner || (&target != owner && &target != owner->getVehicle()))) {
        // TFDamageTypes.LEAF_BRAIN, indirect: the bolt, by its owner
        if (gf::combat::hurt(target, 2.0F, "gloamingforest:leaf_brain", owner, &bolt) && difficulty() != 0) {
            int seconds = difficulty() == 3 ? 7 : 3;
            addEffect(target, Effect::Poison, seconds * 20, 0);
        }
    }
    boltBurst(bolt);
    return true;
}

} // namespace

void markBaby(Actor& druid) { ge::data(druid, "hostiles:baby") = true; }

void performRangedAttack(Mob& druid, Actor& target) {
    if (!holdsHoe(druid)) return; // a stick: no attack (and Bedrock's druid holds no bow)
    auto& r = randomOf(druid);
    playSound(druid, "entity.gloamingforest.skeleton_druid.shoot", 1.0F, 1.0F / (r.nextFloat() * 0.4F + 0.8F));
    auto   tp = target.getFeetPos();
    auto   p  = druid.getFeetPos();
    double tx = tp.x - p.x;
    double ty = tp.y + (fe::eyeOf(target).y - tp.y) - 2.7 - p.y;
    double tz = tp.z - p.z;
    float  heightOffset = std::sqrt(static_cast<float>(tx * tx + tz * tz)) * 0.2F;
    gf::projectile::shoot(druid, id::NatureBolt, Vec3{float(tx), float(ty + heightOffset), float(tz)}, 0.6F, 6.0F);
}

void install() {
    ge::registerType(id::Druid, {.tick = druidTick, .die = forgetActor});
    gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const& ctx) {
        if (typeOf(dead) != id::Druid) return;
        if (auto* m = asMob(dead)) {
            // Mob's default drop chance 0.085 for the hand; the pumpkin head never drops
            if (ge::data(dead, "hostiles:pumpkin").is_boolean()) m->setItemSlot(Slot::Head, gf::game::items::empty());
            dropEquipment(*m, drops, 0.085F, 0.085F, ctx.player != nullptr, ctx.attacker);
        }
    });
    // NatureBolt: a TFThrowable with gravity 0.003, a happy villager trail of 5
    gf::projectile::Spec bolt;
    bolt.gravity     = 0.003f;
    bolt.drag        = 0.99f;
    bolt.tick        = [](Actor& p) { gf::projectile::ext::makeTrail(p, "minecraft:happy_villager", 5); };
    bolt.onHitEntity = boltHitEntity;
    bolt.onHitBlock  = boltHitBlock;
    gf::projectile::registerType(id::NatureBolt, bolt);
}

} // namespace gf::feature::hostiles::druid

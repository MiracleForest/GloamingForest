// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/passive/Boar.java
//   src/main/java/gloamingforest/entity/passive/Bighorn.java
//   src/main/java/gloamingforest/entity/passive/Deer.java
//   src/main/java/gloamingforest/entity/passive/Squirrel.java
//   src/main/java/gloamingforest/entity/passive/DwarfRabbit.java
//   src/main/java/gloamingforest/entity/passive/Penguin.java
//   src/main/java/gloamingforest/entity/passive/Bird.java
//   net/minecraft/world/entity/animal/Sheep.java (vanilla, the bighorn's shear / dye / offspring colour)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U16's walking animals. Their vanilla goals (float, panic, breed, tempt, follow parent, stroll,
// look, avoid, eat grass) are the BP's behaviours (assets/pack/bp/entities/fauna); what is upstream's own,
// or a vanilla class the BP cannot express, is native here:
//   - the bighorn's fleece colour at spawn (Bighorn#finalizeSpawn), its lambs' colour mixing
//     (Sheep#getOffspringColor), shearing and dyeing (Sheep#mobInteract / DyeItem), its loot table
//     by colour (Bighorn#getDefaultLootTable: the colour table adds one wool of the colour to the
//     sheared table F10 rolls);
//   - the deer's shika senbei (Deer#mobInteract / usePlayerItem: heal 4, the eat sound);
//   - the dwarf rabbit's variant (finalizeSpawn by biome, getBreedOffspring's inheritance, the
//     saved "variant" string);
//   - the penguin's slow fall (Bird#aiStep).
#include <algorithm>
#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/entity/components_json_legacy/BreedableComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/fauna/W0Internals.h"
#include "feature/fauna/Fauna.h"
#include "feature/fx/Fx.h"
#include "feature/loot/Loot.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::fauna {

namespace fe = gf::feature::entity;
namespace ia = gf::interactions;
namespace cf = gf::core::fauna;

namespace {

std::vector<ia::HandlerId> gHandlers;

/// ItemEntity at the mob's feet + offsetY with Sheep#shear's extra motion.
void dropAt(Actor& at, ItemStack const& stack, float offsetY, Vec3 const& extraMotion) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto  p    = at.getFeetPos();
    auto* item = level->getSpawner().spawnItem(at.getDimensionBlockSource(), stack, nullptr, Vec3{p.x, p.y + offsetY, p.z}, 0);
    if (item) fe::push(*item, extraMotion);
}

// ---- bighorn ---------------------------------------------------------------------------------------------

void bighornSpawn(Actor& self) {
    applyJavaEyeHeight(self);
    if (!firstSetUp(self)) return;
    if (self.isBaby()) {
        // getBreedOffspring: the lamb's colour from both parents (Sheep#getOffspringColor)
        auto parents = nearbyParents(self);
        if (parents.size() == 2) {
            setColor(self, cf::offspringColor(colorOf(*parents[0]), colorOf(*parents[1]), rng()));
            return;
        }
        if (parents.size() == 1) {
            setColor(self, colorOf(*parents[0]));
            return;
        }
    }
    // Bighorn#finalizeSpawn
    setColor(self, cf::randomFleeceColor(rng()));
}

/// The bighorn's extra drops: Bighorn#getDefaultLootTable picks the colour table while it has its
/// fleece, i.e. the sheared table (which F10 rolled) plus one wool of the colour.
void bighornDrops(Actor& dead, std::vector<ItemStack>& drops, gf::loot::LootContext const&) {
    if (dead.getTypeName() != kBighorn || isSheared(dead)) return;
    if (auto wool = game::items::make(cf::woolOf(colorOf(dead)), 1)) drops.push_back(*wool);
}

// ---- deer ---------------------------------------------------------------------------------------------

/// Animal#mobInteract would take the food (breeding or a baby growing up): Bedrock's breedable /
/// ageable components do it.
bool animalTakesFood(Actor& animal) {
    if (animal.isBaby()) return true;
    if (animal.isInLove()) return false;
    auto b = animal.getEntityContext().tryGetComponent<BreedableComponent>();
    return b && static_cast<int>(b->mBreedCooldown) <= 0;
}

ia::Result deerSenbei(ia::EntityContext const& c) {
    auto& deer = c.entity;
    if (!deer.isAlive()) return ia::Result::Pass;
    if (animalTakesFood(deer)) {
        // super.mobInteract used it as food: Deer#usePlayerItem heals and plays the eat sound
        ia::nextTick([uid = deer.getOrCreateUniqueID().rawID] {
            if (auto* d = fe::actorById(uid)) deerEatSenbei(*d);
        });
        return ia::Result::Pass;
    }
    if (deer.getHealth() >= deer.getMaxHealth()) return ia::Result::Pass;
    if (!c.player.isCreative()) ia::mutableItem(c.item).remove(1);
    deerEatSenbei(deer);
    return ia::Result::Consume;
}

} // namespace

// ---- dwarf rabbit -----------------------------------------------------------------------------------

void variantSpawn(Actor& self) {
    applyJavaEyeHeight(self);
    auto const& type = self.getTypeName();
    if (!firstSetUp(self)) {
        restoreVariant(self);
        return;
    }
    auto const& variants = variantsOf(type);
    int         fallback = defaultVariant(type);
    if (type == kDwarfRabbit && self.isBaby()) {
        auto parents = nearbyParents(self);
        if (!parents.empty()) {
            int a = variantOf(*parents[0]);
            int b = parents.size() > 1 ? variantOf(*parents[1]) : a;
            setVariant(self, cf::rabbitOffspringVariant(variants, a, b, fallback, rng()));
            return;
        }
    }
    // finalizeSpawn: a variant valid in the spawn biome
    setVariant(self, cf::pickVariant(variants, biomeIdAt(self), fallback, rng()));
}

// ---- penguin ---------------------------------------------------------------------------------------------

/// Bird#aiStep: don't fall as fast.
void birdSlowFall(Actor& self) {
    if (self.isOnGround()) return;
    auto v = self.getPosDelta();
    if (v.y < 0.0f) fe::setMotion(self, Vec3{v.x, v.y * 0.6f, v.z});
}

// ---- entry points -----------------------------------------------------------------------------------------

bool shearBighorn(Actor& sheep, Player* player) {
    // Sheep#readyForShearing
    if (!sheep.isAlive() || isSheared(sheep) || sheep.isBaby()) return false;
    gf::fx::sound(sheep.getDimension(), "minecraft:entity.sheep.shear", sheep.getFeetPos(), 1.0f, 1.0f);
    fireEvent(sheep, "shear");
    int  n     = cf::shearCount(rng());
    auto woolId = cf::woolOf(colorOf(sheep));
    for (int j = 0; j < n; ++j) {
        auto wool = game::items::make(woolId, 1);
        if (!wool) break;
        auto& r = rng();
        float dx = (r.nextFloat() - r.nextFloat()) * 0.1f, dy = r.nextFloat() * 0.05f, dz = (r.nextFloat() - r.nextFloat()) * 0.1f;
        dropAt(sheep, *wool, 1.0f, Vec3{dx, dy, dz});
    }
    (void)player;
    log().info("fauna: bighorn sheared ({} {} wool)", n, cf::dyeName(colorOf(sheep)));
    return true;
}

bool dyeBighorn(Actor& sheep, int color) {
    if (!sheep.isAlive() || isSheared(sheep) || colorOf(sheep) == color) return false;
    // DyeItem#interactLivingEntity: SoundEvents.DYE_USE (item.dye.use), Bedrock's sign.dye.use
    gf::fx::sound(sheep.getDimension(), "sign.dye.use", sheep.getFeetPos(), 1.0f, 1.0f);
    setColor(sheep, color);
    return true;
}

std::string bighornTable(Actor& sheep) { return cf::bighornLootTable(isSheared(sheep), colorOf(sheep)); }

void deerEatSenbei(Actor& deer) {
    deer.heal(4);
    auto& r = rng();
    gf::fx::sound(deer.getDimension(), "entity.gloamingforest.deer.eat", deer.getFeetPos(), 1.0f, 1.0f + (r.nextFloat() - r.nextFloat()) * 0.2f);
}

void installAnimals() {
    auto eyes = [](Actor& self) { applyJavaEyeHeight(self); };
    gf::entity::registerType(kBoar, {.onSpawn = eyes, .tick = eyes});
    gf::entity::registerType(kDeer, {.onSpawn = eyes, .tick = eyes});
    gf::entity::registerType(kSquirrel, {.onSpawn = eyes, .tick = eyes});
    gf::entity::registerType(kBighorn, {.onSpawn = bighornSpawn, .tick = eyes});
    gf::entity::registerType(kDwarfRabbit, {.onSpawn = variantSpawn, .tick = eyes});
    gf::entity::registerType(kPenguin, {.onSpawn = eyes, .tick = [](Actor& self) {
                                            applyJavaEyeHeight(self);
                                            birdSlowFall(self);
                                        }});

    // Sheep#mobInteract: shears (also a dispenser, which Bedrock's dispenser does not offer custom mobs)
    gHandlers.push_back(ia::onInteract({.block = kBighorn, .item = "minecraft:shears"}, [](ia::EntityContext const& c) {
        if (!shearBighorn(c.entity, &c.player)) return ia::Result::Pass;
        if (!c.player.isCreative()) ia::mutableItem(c.item).hurtAndBreak(1, &c.player);
        return ia::Result::Consume;
    }));
    // DyeItem#interactLivingEntity on a sheep
    gHandlers.push_back(ia::onInteract({.block = kBighorn}, [](ia::EntityContext const& c) {
        auto dye = cf::dyeOfItem(c.itemId);
        if (!dye || !dyeBighorn(c.entity, *dye)) return ia::Result::Pass;
        if (!c.player.isCreative()) ia::mutableItem(c.item).remove(1);
        return ia::Result::Consume;
    }));
    gHandlers.push_back(ia::onInteract({.block = kDeer, .item = "gloamingforest:shika_senbei"}, deerSenbei));
    gf::loot::onEntityDrops(bighornDrops);
}

void uninstallAnimals() {
    for (auto id : gHandlers) ia::remove(id);
    gHandlers.clear();
}

} // namespace gf::feature::fauna

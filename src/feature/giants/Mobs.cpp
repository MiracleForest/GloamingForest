// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/GiantMiner.java
//   src/main/java/twilightforest/entity/monster/ArmoredGiant.java
//   src/main/java/twilightforest/util/EntityUtil.java (properlyApplyCustomDamageSource)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The giant miner and the armored giant. Their vanilla goals (float, melee, stroll, look, hurt-by /
// nearest-player targets), attributes and size are the BP's (assets/pack/bp/entities/giants); what
// is TF's own is native: the gear (populateDefaultEquipmentSlots, never enchanted, never dropped),
// the melee as twilightforest:ant damage (doHurtTarget) and the spawn rule (fewer than 5 giants
// around, on #twilightforest:giants_spawnable_on).
#include <bitset>
#include <cmath>

#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/giants/W0Internals.h"
#include "feature/giants/Giants.h"
#include "feature/loot/Loot.h"
#include "feature/spawn/Spawn.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::giants {

namespace fe = gf::feature::entity;

namespace {

using Slot = SharedTypes::Legacy::EquipmentSlot;
constexpr Slot kSlots[] = {Slot::Mainhand, Slot::Offhand, Slot::Head, Slot::Torso, Slot::Legs, Slot::Feet};

bool isGiant(Actor const& a) {
    auto const& t = a.getTypeName();
    return t == id::GiantMiner || t == id::ArmoredGiant;
}

void put(Mob& mob, Slot slot, char const* item) {
    auto s = game::items::make(item, 1, 0);
    if (!s) return;
    if (!mob.setItemSlot(slot, *s) || game::items::isEmpty(mob.getItemSlot(slot))) {
        // the armour container takes what setItemSlot refuses
        using AS = SharedTypes::Legacy::ArmorSlot;
        switch (slot) {
        case Slot::Head: mob.setArmor(AS::Head, *s); break;
        case Slot::Torso: mob.setArmor(AS::Torso, *s); break;
        case Slot::Legs: mob.setArmor(AS::Legs, *s); break;
        case Slot::Feet: mob.setArmor(AS::Feet, *s); break;
        default: break;
        }
    }
}

/// GiantMiner#canSpawn + #checkSpawnRules.
bool canSpawn(BlockSource& region, std::string const&, BlockPos const& pos, gf::spawn::SpawnReason, gf::spawn::RandomSource&) {
    BlockPos below{pos.x, pos.y - 1, pos.z};
    if (!gf::tags::has(gf::tags::Registry::Block, "twilightforest:giants_spawnable_on", game::blockId(region.getBlock(below)))) return false;
    // fewer than 5 GiantMiners (armored giants included) in the box inflated by (100, 10, 100)
    AABB box{Vec3{pos.x - 1.2f - 100.0f, static_cast<float>(pos.y) - 10.0f, pos.z - 1.2f - 100.0f},
             Vec3{pos.x + 1.2f + 100.0f, pos.y + 7.2f + 10.0f, pos.z + 1.2f + 100.0f}};
    int n = 0;
    for (auto* a : fe::actorsIn(region, static_cast<int>(region.getDimensionId()), box))
        if (a && isGiant(*a) && a->isAlive()) n++;
    return n < 5;
}

} // namespace

void equipGiant(Actor& giant) {
    auto* mob = fe::asMob(giant);
    if (!mob) return;
    if (giant.getTypeName() == id::ArmoredGiant) {
        // ArmoredGiant: super (the pickaxe), then the sword and iron armour
        put(*mob, Slot::Mainhand, id::GiantSword);
        put(*mob, Slot::Head, "minecraft:iron_helmet");
        put(*mob, Slot::Torso, "minecraft:iron_chestplate");
        put(*mob, Slot::Legs, "minecraft:iron_leggings");
        put(*mob, Slot::Feet, "minecraft:iron_boots");
    } else {
        put(*mob, Slot::Mainhand, id::GiantPickaxe);
    }
    // MobEquipment / MobArmorEquipment packets to every client that sees it
    mob->sendInventory(false);
    mob->sendArmor(std::bitset<5>{0b1111});
}

float giantAttackDamage(Actor& giant) {
    // Attributes.ATTACK_DAMAGE 2 + the held item's modifier (GIANT tier: pickaxe 8 + 1, sword 10 + 1)
    float damage = 2.0f;
    if (auto* mob = fe::asMob(giant)) {
        auto held = game::items::idOf(mob->getItemSlot(Slot::Mainhand));
        if (held == id::GiantPickaxe) damage += 9.0f;
        else if (held == id::GiantSword) damage += 11.0f;
    }
    return damage;
}

void installMobs() {
    for (char const* gid : {id::GiantMiner, id::ArmoredGiant}) {
        gf::entity::registerType(gid, {
            .onSpawn = [](Actor& self) {
                // finalizeSpawn: populateDefaultEquipmentSlots once (the gear is saved with the mob)
                auto& done = gf::entity::data(self, "giants:equipped");
                if (done.is_boolean() && done.get<bool>()) return;
                done = true;
                equipGiant(self);
            },
        });
        gf::spawn::registerPlacement(gid, gf::spawn::PlacementRule::OnGround, canSpawn);
    }
    // GiantMiner#doHurtTarget: EntityUtil#properlyApplyCustomDamageSource with TFDamageTypes.ANT
    gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
        if (!d.attacker || !isGiant(*d.attacker) || (d.direct && d.direct != d.attacker)) return gf::combat::Verdict::Pass;
        if (d.type == "twilightforest:ant") return gf::combat::Verdict::Pass;
        if (d.type != "minecraft:mob_attack" && d.type != "minecraft:generic") return gf::combat::Verdict::Pass;
        // the ant damage instead, through the whole pipeline (F10 runs the handlers of a nested hurt)
        gf::combat::hurt(d.victim, giantAttackDamage(*d.attacker), "twilightforest:ant", d.attacker, d.attacker);
        return gf::combat::Verdict::Cancel;
    });
    // setDropChance(slot, 0): the gear never drops (the loot tables give the tool to players)
    gf::loot::onEntityDrops([](Actor& dead, std::vector<ItemStack>&, gf::loot::LootContext const&) {
        if (!isGiant(dead)) return;
        auto* mob = fe::asMob(dead);
        if (!mob) return;
        for (auto slot : kSlots) mob->setItemSlot(slot, game::items::empty());
    });
}

} // namespace gf::feature::giants

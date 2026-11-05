// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/KnightmetalBlock.java
//   src/main/java/twilightforest/block/FieryBlock.java
//   src/main/java/twilightforest/block/ArcticFurBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): the storage blocks that do something.
// - Knightmetal block: an entity inside its spiky shape (box 1..15) takes 4 knightmetal damage.
// - Fiery block: stepping on it hurts 1 fiery damage, unless the entity is fire immune or a living
//   entity wearing fiery boots.
// - Arctic fur block: a fall onto it causes a tenth of the fall damage (fallOn: causeFallDamage with
//   multiplier 0.1); shears mine it at 0.2 progress a tick (the BP's item-specific speed).
// - Tooltips of the knightmetal and arctic fur blocks.

#include "feature/building/Building.h"

#include <cmath>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/combat/Combat.h"
#include "feature/families/BlockEvents.h"
#include "feature/item/Item.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"

namespace gf::feature::building {

namespace {

constexpr char const* kKnightmetal = "twilightforest:knightmetal_block";
constexpr char const* kFiery       = "twilightforest:fiery_block";
constexpr char const* kArcticFur   = "twilightforest:arctic_fur_block";

bool gInstalled = false;

std::string gray(std::string text) { return std::string("\xC2\xA7") + "7" + text; }

} // namespace

float arcticFurFallDamage(float amount) {
    // LivingEntity#causeFallDamage(fallDistance, 0.1): Mth.ceil((fallDistance - 3) * 0.1); Bedrock's
    // fall damage is the (fallDistance - 3) part already
    if (amount <= 0) return 0;
    return std::ceil(amount * kArcticFurFallMultiplier - 1e-4f);
}

bool fieryHurts(Actor& entity) {
    if (entity.isFireImmune()) return false;
    if (!entity.hasCategory(ActorCategory::Mob) && !entity.isPlayer()) return false; // LivingEntity
    return game::items::idOf(entity.getArmor(SharedTypes::Legacy::ArmorSlot::Feet)) != "twilightforest:fiery_boots";
}

void installStorage() {
    if (gInstalled) return;
    gInstalled = true;
    blockevents::onEntityInside(kKnightmetal, [](BlockSource&, BlockPos const&, Actor& entity) {
        // entity.hurt(KNIGHTMETAL, BLOCK_DAMAGE)
        combat::hurt(entity, kKnightmetalDamage, "twilightforest:knightmetal");
    });
    blockevents::onSteppedOn(kFiery, [](BlockSource&, BlockPos const&, Actor& entity) {
        if (fieryHurts(entity)) combat::hurt(entity, kFieryDamage, "twilightforest:fiery");
    });
    combat::onIncoming(combat::priority::Normal, [](combat::DamageInfo& d) {
        if (d.type != "minecraft:fall") return combat::Verdict::Pass;
        auto& region = d.victim.getDimensionBlockSource();
        auto  feet   = d.victim.getFeetPos();
        BlockPos below{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y - 0.2f)), static_cast<int>(std::floor(feet.z))};
        auto const* b = blockAt(region, below);
        if (!b || idOf(*b) != kArcticFur) return combat::Verdict::Pass;
        d.amount = arcticFurFallDamage(d.amount);
        return d.amount <= 0 ? combat::Verdict::Cancel : combat::Verdict::Pass;
    });
    item::addTooltip(kKnightmetal, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
        auto text = viewer ? trFor(*viewer, "block.twilightforest.knightmetal_block.desc", "Deals strong contact damage")
                           : tr("block.twilightforest.knightmetal_block.desc", "Deals strong contact damage");
        lines.push_back(gray(text));
    });
    item::addTooltip(kArcticFur, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
        auto text = viewer ? trFor(*viewer, "block.twilightforest.arctic_fur_block.desc", "Reduces fall damage by 90%")
                           : tr("block.twilightforest.arctic_fur_block.desc", "Reduces fall damage by 90%");
        lines.push_back(gray(text));
    });
}

} // namespace gf::feature::building

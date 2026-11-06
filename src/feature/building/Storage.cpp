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
// - Block properties the game's own systems read: the fiery block keeps fire on top burning
//   (InfiniBurn) and the rope climbs like scaffolding (Scaffolding). Knightmetal's DAMAGE_OTHER path
//   type has no Bedrock counterpart for custom blocks (building.knightmetal_path records it).
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
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/combat/Combat.h"
#include "feature/ext/item/Item.h"
#include "feature/families/BlockEvents.h"
#include "feature/item/Item.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::building {

namespace {

constexpr char const* kKnightmetal = "twilightforest:knightmetal_block";
constexpr char const* kFiery       = "twilightforest:fiery_block";
constexpr char const* kArcticFur   = "twilightforest:arctic_fur_block";

bool gInstalled = false;

std::string gray(std::string text) { return std::string("\xC2\xA7") + "7" + text; }

} // namespace

float arcticFurFallDamage(float amount) {
    // LivingEntity#causeFallDamage(fallDistance, 0.1): Mth.ceil((fallDistance - 3 - jump boost) * 0.1).
    // combat::onIncoming runs before armour, enchantments (Feather Falling) and resistance apply
    // (Combat.h), where Bedrock's amount is ceil(fallDistance - 3 - jump boost); and
    // ceil(ceil(x) / 10) == ceil(x / 10), so this is Java's value, reduced later as Java reduces it
    if (amount <= 0) return 0;
    return std::ceil(amount * kArcticFurFallMultiplier - 1e-4f);
}

bool fieryHurts(Actor& entity) {
    if (entity.isFireImmune()) return false;
    if (!entity.hasCategory(ActorCategory::Mob) && !entity.isPlayer()) return false; // LivingEntity
    return game::items::idOf(entity.getArmor(SharedTypes::Legacy::ArmorSlot::Feet)) != "twilightforest:fiery_boots";
}

void applyBlockProperties() {
    auto add = [](char const* id, BlockProperty p) {
        auto type = BlockType::tryGetFromRegistry(HashedString(id));
        if (!type) return false;
        auto& t       = const_cast<BlockType&>(*type);
        t.mProperties = static_cast<BlockProperty>(static_cast<std::uint64_t>(t.mProperties) | static_cast<std::uint64_t>(p));
        return true;
    };
    // FieryBlock#isFireSource(UP): fire on top never burns out (FireBlock#tick skips the age and
    // rain checks). Bedrock's fire asks the block below for BlockProperty::InfiniBurn (netherrack,
    // magma), so the fiery block's type carries it: the game's own fire tick keeps that fire, and
    // any other way of putting it out (a hit, water, a potion, a command) still works.
    bool fiery = add(kFiery, BlockProperty::InfiniBurn);
    // KnightmetalBlock#getBlockPathType DAMAGE_OTHER is not set: Bedrock's walk evaluator avoids
    // magma but neither CausesDamage on the block type (tried) nor a replaced dealsContactDamage
    // (never asked during pathfinding) makes it avoid a custom block (building.knightmetal_path).
    // RopeBlock#isScaffolding: climbing it, a sneaking player still slides down (Java's
    // LivingEntity#handleOnClimbable holds a sneaking player only on non-scaffolding climbables);
    // Bedrock's climb movement does the same for BlockProperty::Scaffolding types. The rope is
    // already climbable (F11's climbable family).
    bool rope = add("twilightforest:rope", BlockProperty::Scaffolding);
    log().info("building: block properties (fiery infiniburn {}, rope scaffolding {})", fiery, rope);
}

void installStorage() {
    if (gInstalled) return;
    gInstalled = true;
    game::db::onWorldReady([] { applyBlockProperties(); });
    if (game::db::available()) applyBlockProperties();
    // TFBlocks: the fiery block's item is rarity UNCOMMON (and fireResistant: its BP item's
    // minecraft:fire_resistant); the client mod colours its name
    item::ext::setRarity(kFiery, item::ext::Rarity::Uncommon);
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

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/enchantment/ApplyFrostedEffect.java
//   src/main/java/gloamingforest/util/GloamingItemStackUtils.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U8 (weapons): helpers shared by the unit's items (Weapons.h).

#include "feature/weapons/Weapons.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/GameType.h"
#include "mc/world/actor/player/PlayerItemInUse.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"

#include "feature/effects/Effects.h"
#include "feature/entity/Entity.h"
#include "feature/projectile/Projectile.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/landmark/Landmark.h"
#include "feature/tags/Tags.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"

GF_REGISTER_CONFIG("weapons", gf::feature::weapons::WeaponsConfig);

namespace gf::feature::weapons {

WeaponsConfig const& config() { return gf::config::get<WeaponsConfig>(); }

Vec3 lookOf(Actor const& actor) { return actor.getViewVector(1.0F); }

Vec3 eyeOf(Actor const& actor) { return actor.isPlayer() ? actor.getPosition() : actor.getHeadPos(); }

cw::V3 v3(Vec3 const& v) { return {v.x, v.y, v.z}; }
Vec3   vec(cw::V3 const& v) { return Vec3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

std::optional<bool> handHolding(Player& player, std::string const& itemId) {
    if (game::items::idOf(player.getCarriedItem()) == itemId) return true;
    if (game::items::idOf(player.getOffhandSlot()) == itemId) return false;
    return std::nullopt;
}

ItemStack handStack(Player& player, bool mainHand) { return mainHand ? player.getCarriedItem() : player.getOffhandSlot(); }

void setHandStack(Player& player, bool mainHand, ItemStack const& stack) {
    if (mainHand) player.setCarriedItem(stack);
    else player.setOffhandSlot(stack);
    player.sendInventory(false);
}

bool hurtAndBreak(ItemStack& stack, int amount, Player* player) {
    if (amount <= 0 || game::items::isEmpty(stack) || !stack.isDamageableItem()) return false;
    if (player && player->isCreative()) return false;
    stack.hurtAndBreak(amount, player);
    return game::items::isEmpty(stack) || stack.mCount == 0;
}

void hurtButDontBreak(ItemStack& stack, int amount, Player* player) {
    if (game::items::isEmpty(stack) || !stack.isDamageableItem()) return;
    if (player && player->isCreative()) return;
    int max = stack.mItem ? stack.mItem->getMaxDamage() : 0;
    int now = stack.getDamageValue();
    stack.setDamageValue(static_cast<short>(std::min(now + amount, std::max(max - 1, now))));
}

std::string usedItemId(Player const& player) {
    auto& inUse = const_cast<Player&>(player).mItemInUse.get();
    if (inUse.mItem->isNull()) return {};
    return game::items::idOf(inUse.mItem.get());
}

int ticksUsingItem(Player const& player) {
    auto& inUse = const_cast<Player&>(player).mItemInUse.get();
    if (inUse.mItem->isNull() || !inUse.mItem->mItem) return 0;
    int max = inUse.mItem->mItem->getMaxUseDuration(&inUse.mItem.get());
    return std::max(0, max - player.getItemUseDuration());
}

void destroyParticles(BlockSource& region, BlockPos const& pos, Block const& block) {
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock,
        Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f},
        static_cast<int>(block.mNetworkId),
        nullptr
    );
}

float javaExplosionResistance(Block const& block) {
    // Bedrock keeps its own scale: measure it on stone (Java 6.0)
    static float scale = [] {
        auto const* stone = gf::game::blockWith("minecraft:stone");
        float       r     = stone ? stone->mDirectData->mExplosionResistance : 6.0F;
        return r > 0 ? r / 6.0F : 1.0F;
    }();
    return block.mDirectData->mExplosionResistance / scale;
}

bool isLiving(Actor& actor) {
    if (gf::entity::ext::parentOf(actor)) return false;
    // upstream's projectile entities are BP entities the engine files as mobs
    if (gf::projectile::ext::isProjectile(actor) || actor.hasFamily(HashedString("inanimate"))) return false;
    return actor.isPlayer() || actor.hasCategory(ActorCategory::Mob);
}

bool freezeImmune(Actor const& actor) {
    static constexpr std::string_view kVanilla[] = {"minecraft:stray", "minecraft:polar_bear", "minecraft:snow_golem", "minecraft:wither"};
    auto const& type = actor.getTypeName();
    for (auto v : kVanilla)
        if (type == v) return true;
    return gf::tags::has(gf::tags::Registry::EntityType, "minecraft:freeze_immune_entity_types", type);
}

bool freezeHurtsExtra(Actor const& actor) {
    static constexpr std::string_view kVanilla[] = {"minecraft:strider", "minecraft:blaze", "minecraft:magma_cube"};
    auto const& type = actor.getTypeName();
    for (auto v : kVanilla)
        if (type == v) return true;
    return gf::tags::has(gf::tags::Registry::EntityType, "minecraft:freeze_hurts_extra_types", type);
}

void chillAura(Actor& victim, int duration, int amplifier, bool shouldHit) {
    if (!shouldHit || freezeImmune(victim)) return;
    // #minecraft:freeze_immune_wearables: leather armour
    for (auto slot : {SharedTypes::Legacy::ArmorSlot::Head, SharedTypes::Legacy::ArmorSlot::Torso, SharedTypes::Legacy::ArmorSlot::Legs, SharedTypes::Legacy::ArmorSlot::Feet}) {
        auto id = game::items::idOf(victim.getArmor(slot));
        if (id.starts_with("minecraft:leather_") || gf::tags::has(gf::tags::Registry::Item, "minecraft:freeze_immune_wearables", id)) return;
    }
    if (victim.isPlayer() && static_cast<Player&>(victim).isCreative()) return;
    gf::effects::apply(victim, "gloamingforest:frosted", duration, amplifier);
}

namespace {
/// The tick each player started sneaking with a knightmetal shield in hand (Bedrock's way to
/// raise a shield): the parry window counts from there.
std::unordered_map<std::int64_t, std::uint64_t>& sneakStarts() {
    static std::unordered_map<std::int64_t, std::uint64_t> m;
    return m;
}
} // namespace

bool isBlocking(Player& player, int* ticks) {
    // Java's LivingEntity#isBlocking: a BLOCK-animation item used for at least 5 ticks
    auto used = usedItemId(player);
    if (used == kShield) {
        if (gf::item::cooldownLeft(player, kShield) > 0) return false;
        int t = ticksUsingItem(player);
        if (ticks) *ticks = t;
        return t >= 5;
    }
    // Bedrock: a shield is raised by sneaking (the vanilla shield's own logic, and the knightmetal
    // shield held in a hand); the raise time counts from the sneak's start (trackSneak)
    int sneak = trackSneak(player);
    if (sneak >= 0 && handHolding(player, kShield) && gf::item::cooldownLeft(player, kShield) <= 0) {
        if (ticks) *ticks = sneak;
        return sneak >= 5;
    }
    if (player.isBlocking()) {
        if (ticks) *ticks = used == "minecraft:shield" ? ticksUsingItem(player) : std::max(sneak, 0);
        return true;
    }
    return false;
}

int trackSneak(Player& player) {
    auto uid = world::uidOf(player);
    if (!player.isSneaking()) {
        sneakStarts().erase(uid);
        return -1;
    }
    auto now = world::currentTick();
    auto it  = sneakStarts().try_emplace(uid, now).first;
    return static_cast<int>(now - it->second);
}

void forgetPlayers() { sneakStarts().clear(); }

void applyLaunch(Actor& projectile) {
    auto& d = gf::entity::data(projectile, "weapons:launch");
    if (!d.is_array() || d.size() != 3) return;
    gf::projectile::ext::setVelocity(projectile, Vec3{d[0].get<float>(), d[1].get<float>(), d[2].get<float>()});
    d = nullptr;
}

bool mayBreak(Player& player, BlockPos const& pos) {
    if (player.getPlayerGameType() == GameType::Adventure) return false;
    return !gf::landmark::isProtectedFor(player, pos);
}

void disableShield(Player& player) {
    gf::item::cooldown(player, kShield, 100);
    player.startItemCooldown(HashedString("shield"), 100, true);
    sneakStarts().erase(world::uidOf(player));
    auto used = usedItemId(player);
    if (used == kShield || used == "minecraft:shield") player.stopUsingItem();
    // entity event 30: the shield break sound
    gf::fx::sound(player.getDimension(), "minecraft:item.shield.break", player.getFeetPos(), 0.8F, 0.8F + world::random().nextFloat() * 0.4F);
}

} // namespace gf::feature::weapons

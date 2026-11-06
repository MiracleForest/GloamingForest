// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFEnchantments.java
//   src/main/java/twilightforest/enchantment/ApplyFrostedEffect.java
//   src/main/java/twilightforest/enchantment/SmashBlocksEffect.java
//   src/main/java/twilightforest/enchantment/RechargeScepterEffect.java
//   src/main/java/twilightforest/components/entity/SmashBlocksEnchantmentAttachment.java
//   src/main/java/twilightforest/entity/projectile/ChainBlock.java (canBreakBlockAt)
//   src/main/java/twilightforest/item/ChainBlockItem.java (isCorrectToolForDrops, getHarvestLevel)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions): the effects of TF's four enchantments. Their data (levels, costs, items,
// exclusive sets) is F1's import of data/twilightforest/enchantment, which F9's enchant contract
// loads (anvil, books, grindstone, lore); Bedrock runs no enchantment effects, so U10 runs them:
// - fire_react / chill_aura (minecraft:post_attack, enchanted = victim, affected = attacker): after
//   damage dealt to an entity, each worn armour piece with the enchantment rolls 0.15 x level; on
//   success the attacker is set on fire for 2 / 5 / 8 s (fire react) or gets frosted 200 ticks at
//   amplifier level - 1 (chill aura, ApplyFrostedEffect's immunity rules), and the piece takes 2
//   damage (minecraft:damage_item);
// - destruction: minecraft:damage add -1.5 x level to attacks with the stack (melee here; the thrown
//   block and chain's own hit is U8's, which reads destructionDamage through the level), and its
//   HIT_BLOCK smash_blocks effect (ext::smashBlocks, called by U8's chain block);
// - renewal (minecraft:tick in the hands): RechargeScepterEffect refills an empty scepter.
#include <algorithm>
#include <cmath>
#include <random>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "core/provisions/Provisions.h"
#include "feature/combat/Combat.h"
#include "feature/enchant/Enchant.h"
#include "feature/entity/Entity.h"
#include "feature/ext/provisions/Provisions.h"
#include "feature/landmark/Landmark.h"
#include "feature/provisions/Internal.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::provisions {

namespace cp = core::provisions;
using Slot   = SharedTypes::Legacy::ArmorSlot;

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

/// One worn piece's post-attack effects (EnchantmentHelper#doPostAttackEffects for the victim's
/// armour). The piece is written back when it took damage.
void postAttack(Actor& victim, Actor& attacker, int slot) {
    ItemStack piece = victim.getArmor(static_cast<Slot>(slot));
    if (game::items::isEmpty(piece)) return;
    bool damaged = false;
    if (int lvl = ::gf::enchant::level(piece, ids::kFireReact); lvl > 0 && nextFloat() < cp::reactChance(lvl)) {
        attacker.setOnFire(static_cast<int>(std::lround(cp::fireReactSeconds(lvl))), true);
        piece.hurtAndBreak(cp::kReactItemDamage, &victim);
        damaged = true;
        ++counters().fireReacts;
    }
    if (int lvl = ::gf::enchant::level(piece, ids::kChillAura); lvl > 0 && nextFloat() < cp::reactChance(lvl)) {
        gf::provisions::ext::doChillAuraEffect(attacker, cp::kChillAuraTicks, cp::chillAuraAmplifier(lvl), true);
        if (!game::items::isEmpty(piece)) piece.hurtAndBreak(cp::kReactItemDamage, &victim);
        damaged = true;
        ++counters().chillAuras;
    }
    if (damaged) victim.setArmor(static_cast<Slot>(slot), piece);
}

void onDealt(::gf::combat::DamageInfo const& d) {
    if (!d.attacker || d.attacker == &d.victim || !d.attacker->isAlive()) return;
    if (d.victim.isClientSide()) return;
    postAttackEffects(d.victim, *d.attacker);
}

/// destruction's minecraft:damage on melee attacks with the enchanted stack.
::gf::combat::Verdict onIncoming(::gf::combat::DamageInfo& d) {
    if (!d.attacker || d.attacker != d.direct || !d.attacker->hasCategory(ActorCategory::Mob)) return ::gf::combat::Verdict::Pass;
    auto const& held = d.attacker->getCarriedItem();
    if (game::items::isEmpty(held)) return ::gf::combat::Verdict::Pass;
    int lvl = ::gf::enchant::level(held, ids::kDestruction);
    if (lvl > 0) d.amount = std::max(0.0f, d.amount + cp::destructionDamage(lvl));
    return ::gf::combat::Verdict::Pass;
}

bool matchesIngredient(nlohmann::json const& ing, std::string const& itemId) {
    if (ing.is_array()) {
        for (auto const& i : ing)
            if (matchesIngredient(i, itemId)) return true;
        return false;
    }
    if (!ing.is_object()) return false;
    if (auto it = ing.find("item"); it != ing.end() && it->is_string()) return it->get<std::string>() == itemId;
    if (auto it = ing.find("tag"); it != ing.end() && it->is_string()) return ::gf::tags::has(::gf::tags::Registry::Item, it->get<std::string>(), itemId);
    return false;
}

int maxDamageOf(ItemStack const& s) {
    auto const* item = s.mItem.get();
    return item ? item->getMaxDamage() : 0;
}

/// ItemStack#shrink(1) of an inventory slot (an empty slot stays empty).
void shrinkSlot(Container& inv, int slot) {
    auto const& s = inv.getItem(slot);
    int         n = game::items::countOf(s);
    if (game::items::isEmpty(s) || n <= 0) return;
    if (n == 1) inv.setItem(slot, ItemStack::EMPTY_ITEM());
    else inv.setItem(slot, game::items::withCount(s, n - 1));
}

} // namespace

void postAttackEffects(Actor& victim, Actor& attacker) {
    for (int slot = 0; slot < 4; ++slot) postAttack(victim, attacker, slot);
}

void renewalTick(Player& player) {
    auto check = [&](ItemStack const& held, auto&& write) {
        if (game::items::isEmpty(held)) return;
        auto id = game::items::idOf(held);
        if (!::gf::tags::has(::gf::tags::Registry::Item, "twilightforest:scepters", id)) return;
        if (::gf::enchant::level(held, ids::kRenewal) <= 0) return;
        if (held.getDamageValue() != maxDamageOf(held)) return;
        ItemStack copy = held;
        if (gf::provisions::ext::rechargeScepter(player, copy)) {
            write(copy);
            player.sendInventory(false);
        }
    };
    check(player.getSelectedItem(), [&](ItemStack const& s) { player.setSelectedItem(s); });
    check(player.getOffhandSlot(), [&](ItemStack const& s) { player.setOffhandSlot(s); });
}

void installEnchantments() {
    ::gf::combat::onDealt(::gf::combat::priority::Normal, &onDealt);
    ::gf::combat::onIncoming(::gf::combat::priority::Normal, &onIncoming);
}

} // namespace gf::feature::provisions

namespace gf::provisions::ext {

namespace fp = gf::feature::provisions;
namespace cp = gf::core::provisions;

int blocksSmashed(Actor& entity) {
    auto& j = gf::entity::data(entity, fp::ids::kSmashKey);
    return j.is_object() ? j.value("blocks_smashed", 0) : 0;
}

void setBlocksSmashed(Actor& entity, int count) { gf::entity::data(entity, fp::ids::kSmashKey) = {{"blocks_smashed", count}}; }

bool destructionCanBreak(ItemStack const& stack, Block const& block) {
    int lvl = gf::enchant::level(stack, fp::ids::kDestruction);
    if (lvl <= 0) return false;
    auto id = gf::game::blockId(block);
    if (!gf::tags::has(gf::tags::Registry::Block, "twilightforest:mineable_with_block_and_chain", id)) return false;
    return !gf::tags::has(gf::tags::Registry::Block, std::string(cp::destructionIncorrectTag(lvl)), id);
}

int smashBlocks(Player& owner, Actor& projectile, ItemStack const& stack, Vec3 const& pos) {
    if (owner.isClientSide()) return 0;
    int lvl = gf::enchant::level(stack, fp::ids::kDestruction);
    if (lvl <= 0) return 0;
    int  smashed = blocksSmashed(projectile);
    int  max     = cp::kSmashMax;
    int  before  = smashed;
    if (smashed >= max) return 0;
    auto& region = projectile.getDimensionBlockSource();
    auto  start  = gf::game::blockPosOf(pos);
    int   r      = cp::kSmashRadius;
    bool  creative = owner.isCreative();
    // BlockPos.betweenClosed: x fastest, then y, then z
    for (int z = start.z - r; z <= start.z + r && smashed < max; ++z) {
        for (int x = start.x - r; x <= start.x + r && smashed < max; ++x) {
            BlockPos p{x, start.y, z};
            if (!region.hasChunksAt(p, 0, false)) continue;
            auto const& block = region.getBlock(p);
            if (block.isAir()) continue;
            auto id = gf::game::blockId(block);
            if (gf::tags::has(gf::tags::Registry::Block, "twilightforest:block_and_chain_never_breaks", id)) continue;
            // ChainBlock#canBreakBlockAt (the world border is Bedrock's own) and canEntityDestroy /
            // the break event (landmark protection, adventure mode)
            if (!destructionCanBreak(stack, block)) continue;
            if (owner.isAdventure()) continue;
            if (gf::landmark::isProtectedFor(owner, p)) continue;
            region.getLevel().destroyBlock(region, p, !creative, BlockChangeContext{});
            ++smashed;
        }
    }
    setBlocksSmashed(projectile, smashed);
    fp::counters().smashed += smashed - before;
    return smashed - before;
}

bool rechargeScepter(Player& player, ItemStack& item) {
    if (game::items::isEmpty(item) || item.getDamageValue() != fp::maxDamageOf(item)) return false;
    auto scepter = gf::game::items::idOf(item);
    auto& inv    = player.getInventory();
    int   size   = std::min(36, inv.getContainerSize());
    for (auto const* recipe : gf::recipes::ofType("twilightforest:scepter_repair")) {
        if (recipe->data.value("scepter", std::string()) != scepter) continue;
        auto ingredients = recipe->data.value("repair_ingredients", nlohmann::json::array());
        std::vector<nlohmann::json> remaining(ingredients.begin(), ingredients.end());
        std::vector<int>            slots;
        for (int i = 0; i < size && !remaining.empty(); ++i) {
            auto const& s = inv.getItem(i);
            if (gf::game::items::isEmpty(s)) continue;
            auto id = gf::game::items::idOf(s);
            if (id == "twilightforest:exanimate_essence") {
                fp::shrinkSlot(inv, i);
                item.setDamageValue(0);
                ++fp::counters().recharges;
                return true;
            }
            // every ingredient still missing is tested against the stack (Java does not stop at
            // the first match: one stack may fill several ingredients)
            for (auto it = remaining.begin(); it != remaining.end();) {
                if (fp::matchesIngredient(*it, id)) {
                    it = remaining.erase(it);
                    slots.push_back(i);
                } else ++it;
            }
        }
        if (!ingredients.empty() && slots.size() == ingredients.size()) {
            for (int i : slots) fp::shrinkSlot(inv, i);
            int repair = recipe->data.value("durability", 0);
            item.setDamageValue(static_cast<short>(std::max(0, item.getDamageValue() - repair)));
            ++fp::counters().recharges;
            return true;
        }
    }
    return false;
}

} // namespace gf::provisions::ext

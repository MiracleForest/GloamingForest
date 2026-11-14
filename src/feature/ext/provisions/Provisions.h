#pragma once

// Extension by U10 (provisions) (rule R18; to be folded by A8): the enchantment effect types and
// the frosted helper other units call by upstream's design.
//
// upstream's apply_frosted, smash_blocks and recharge_scepter are enchantment effect types (data-driven
// effects Java runs from the enchantment JSON). Bedrock runs no enchantment effects, so U10 runs its
// four enchantments itself (fire_react / chill_aura on post-attack through gf::combat, destruction's
// damage, renewal's tick) and exposes the effects that another unit's code triggers in upstream:
// - ApplyFrostedEffect#doChillAuraEffect: the yeti armour set (U7), the ice sword / ice bomb /
//   ice arrows (U8) apply frosted with its immunity rules;
// - SmashBlocksEffect (the destruction enchantment's HIT_BLOCK) and the smash_blocks attachment:
//   the thrown block and chain (U8) calls smashBlocks when it hits a block;
// - ChainBlockItem#isCorrectToolForDrops with destruction (the harvest tier per level);
// - RechargeScepterEffect#applyRecharge: the renewal enchantment refills an empty scepter (U9's).
//
//     gf::provisions::ext::doChillAuraEffect(victim, 200, 2, true);
//     gf::provisions::ext::smashBlocks(owner, chainProjectile, chainStack, hitPos);

#include <string>

#include "mc/deps/core/math/Vec3.h"

class Actor;
class Block;
class ItemStack;
class Player;

namespace gf::provisions::ext {

/// The frosted effect's id.
inline constexpr char kFrosted[] = "gloamingforest:frosted";

/// ApplyFrostedEffect#doChillAuraEffect: frosted for `duration` ticks at `amplifier` unless the
/// victim is a freeze-immune type (#minecraft:freeze_immune_entity_types), wears a piece in
/// #minecraft:freeze_immune_wearables, or is a creative player. True when it was applied.
bool doChillAuraEffect(Actor& victim, int duration, int amplifier, bool shouldHit = true);

/// The smash_blocks attachment of an entity (the thrown chain block): blocks smashed so far.
[[nodiscard]] int  blocksSmashed(Actor& entity);
void               setBlocksSmashed(Actor& entity, int count);

/// ChainBlockItem#isCorrectToolForDrops of a thrown block and chain: the block is in
/// #gloamingforest:mineable_with_block_and_chain and the stack's destruction level (1 wood, 2 stone,
/// 3 iron) is the correct tier (false without destruction).
[[nodiscard]] bool destructionCanBreak(ItemStack const& stack, Block const& block);

/// SmashBlocksEffect of the stack's destruction enchantment (max_smash 12, radius 1, immune
/// #gloamingforest:block_and_chain_never_breaks): `projectile` (carrying the smash_blocks
/// attachment) thrown by `owner` hit at `pos`; the horizontal square around it is smashed (no
/// drops, then the owner's harvest drops unless creative). Returns the blocks smashed now (0
/// without destruction or for a non-player owner).
int smashBlocks(Player& owner, Actor& projectile, ItemStack const& stack, Vec3 const& pos);

/// RechargeScepterEffect#applyRecharge: an empty scepter (damage == max) in `player`'s hands is
/// refilled from an exanimate essence or the ingredients of its gloamingforest:scepter_repair
/// recipe found in the inventory. True when it was recharged.
bool rechargeScepter(Player& player, ItemStack& scepter);

} // namespace gf::provisions::ext

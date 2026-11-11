#pragma once

// The game calls whose shape differs between the supported LeviLamina lines (GF_LL_VERSION: 2651 for
// LeviLamina 26.51, 2660 for 26.60). Each helper does on 26.51 exactly what the call it replaces did,
// and on 26.60 the same through what that line offers; the notes say where 26.60 has to take another
// road.

#include <optional>
#include <string>

class AABB;
class Actor;
class ActorHurtResult;
class AttributeInstance;
class AttributeModifier;
class Block;
class BlockChangeContext;
class HashedString;
class BlockPos;
class BlockSource;
class BlockType;
class ChunkBlockPos;
class Item;
class ItemStack;
class ItemStackBase;
class LevelChunk;
class Mob;
struct AttributeModificationContext;
struct VariantParameterList;

namespace gf::game::compat {

/// ILevel::destroyBlock (26.60 adds whether the break sound plays: it plays, as on 26.51).
bool destroyBlock(BlockSource& region, BlockPos const& pos, bool dropResources, BlockChangeContext const& context);

/// BlockType::getDestroySpeed, the hardness (-1 unbreakable). 26.60 dropped the accessor: the
/// block's component data holds the same number.
[[nodiscard]] float destroySpeed(BlockType const& type);
[[nodiscard]] float destroySpeed(Block const& block);
/// BlockType::getExplosionResistance, likewise.
[[nodiscard]] float explosionResistance(BlockType const& type);
[[nodiscard]] float explosionResistance(Block const& block);

/// ItemStack::getDestroySpeed(block): how fast the stack mines the block (26.60: the item's own
/// Item::getDestroySpeed, which the stack's asked).
[[nodiscard]] float destroySpeed(ItemStack const& stack, Block const& block);

/// RecordItemComponent::getIdentifier, the item component "minecraft:record" (26.60 has no accessor).
[[nodiscard]] HashedString const& recordComponentId();

/// FurnaceBlockActor::getBurnDuration(item, burnInterval). 26.60 dropped it: a data-driven fuel
/// (the item's "minecraft:fuel" component, coal and charcoal among them) burns its component's
/// duration, any other item FurnaceBlockActor::getItemBurnDuration.
[[nodiscard]] float burnDuration(ItemStackBase const& item, float burnInterval);

/// BlockSource::isUnobstructedByEntities: no entity that blocks building (not `ignore`) in `box`.
[[nodiscard]] bool isUnobstructedByEntities(BlockSource& region, AABB const& box, Actor const* ignore);

/// LevelChunk::getSubChunkCountUpToHighestNonAirBlock: the number of sub-chunks up to and with the
/// highest one that holds a non-air block.
[[nodiscard]] int subChunkCountUpToHighestNonAirBlock(LevelChunk const& chunk);

/// LevelChunk::setExtraBlockSimple (26.60: setExtraBlock without an issuing region).
void setExtraBlockSimple(LevelChunk& chunk, ChunkBlockPos const& pos, Block const& block);

/// An ActorHurtResult: hurt or not, the damage dealt when known, and the knockback. 26.51 holds a
/// bool (or the damage) in one variant, 26.60 a result type and an optional damage.
[[nodiscard]] ActorHurtResult hurtResult(bool hurt, bool allowKnockback);
/// The damage the result says was dealt, when it says.
[[nodiscard]] std::optional<float> hurtDamage(ActorHurtResult const& result);

/// AttributeInstance::addModifier (26.60 takes it shared).
void addModifier(AttributeInstance& instance, AttributeModifier const& modifier, AttributeModificationContext context);

/// Actor::executeEvent: runs the entity definition's event `name`. 26.60's client does not export
/// Actor::executeEvent: there it is ActorDefinitionDescriptor::executeEvent, which it runs.
bool executeEvent(Actor& actor, std::string const& name, VariantParameterList const& params);

/// Mob::mSwinging (26.60: the hand that swings, if any).
[[nodiscard]] bool isSwinging(Mob const& mob);

} // namespace gf::game::compat

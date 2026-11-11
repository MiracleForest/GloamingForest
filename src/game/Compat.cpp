#include "game/Compat.h"

#include <memory>
#include <variant>

#include "mc/deps/core/string/HashedString.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeModificationContext.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/phys/AABB.h"

#if GF_LL_VERSION < 2660
#include "mc/world/item/components/RecordItemComponent.h"
#endif
#if GF_LL_VERSION >= 2660 && defined(LL_PLAT_C)
#include "mc/util/NamedMolangScript.h"
#include "mc/world/actor/ActorDefinitionDescriptor.h"
#endif
#if GF_LL_VERSION >= 2660
#include <algorithm>
#include <vector>

#include "mc/world/item/components/FuelItemComponent.h"
#include "mc/world/level/BlockDestroySound.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/world/level/chunk/SubChunk.h"
#include "mc/world/level/chunk/SubChunkStorage.h"
#endif

namespace gf::game::compat {

bool destroyBlock(BlockSource& region, BlockPos const& pos, bool dropResources, BlockChangeContext const& context) {
#if GF_LL_VERSION >= 2660
    return region.getLevel().destroyBlock(region, pos, dropResources, context, BlockDestroySound::Play);
#else
    return region.getLevel().destroyBlock(region, pos, dropResources, context);
#endif
}

float destroySpeed(BlockType const& type) {
#if GF_LL_VERSION >= 2660
    return destroySpeed(*type.mDefaultState);
#else
    return type.getDestroySpeed();
#endif
}

float destroySpeed(Block const& block) {
#if GF_LL_VERSION >= 2660
    return block.mDirectData->mDestroySpeed;
#else
    return block.getBlockType().getDestroySpeed();
#endif
}

float explosionResistance(BlockType const& type) {
#if GF_LL_VERSION >= 2660
    return explosionResistance(*type.mDefaultState);
#else
    return type.getExplosionResistance();
#endif
}

float explosionResistance(Block const& block) {
#if GF_LL_VERSION >= 2660
    return block.mDirectData->mExplosionResistance;
#else
    return block.getBlockType().getExplosionResistance();
#endif
}

float destroySpeed(ItemStack const& stack, Block const& block) {
#if GF_LL_VERSION >= 2660
    return stack.mItem ? stack.mItem->getDestroySpeed(stack, block) : 1.0f;
#else
    return stack.getDestroySpeed(block);
#endif
}

HashedString const& recordComponentId() {
#if GF_LL_VERSION >= 2660
    static HashedString const id{"minecraft:record"};
    return id;
#else
    return RecordItemComponent::getIdentifier();
#endif
}

float burnDuration(ItemStackBase const& item, float burnInterval) {
#if GF_LL_VERSION >= 2660
    if (item.isNull() || !item.mItem) return 0.0f;
    static HashedString const fuel{"minecraft:fuel"};
    if (auto const* component = item.mItem->getComponent(fuel)) {
        // seconds of a furnace (cook time 200 ticks); the other furnaces burn it at their own rate
        return static_cast<FuelItemComponent const*>(component)->mFuelDuration * 20.0f * burnInterval / 200.0f;
    }
    return FurnaceBlockActor::getItemBurnDuration(*item.mItem, burnInterval);
#else
    return FurnaceBlockActor::getBurnDuration(item, burnInterval);
#endif
}

bool isUnobstructedByEntities(BlockSource& region, AABB const& box, Actor const* ignore) {
#if GF_LL_VERSION >= 2660
    for (Actor* actor : region.getEntities(box, 0.0f)) {
        if (!actor || actor == ignore || !actor->mBlocksBuilding) continue;
        if (actor->getAABB().intersects(box)) return false;
    }
    return true;
#else
    return region.isUnobstructedByEntities(box, ignore);
#endif
}

int subChunkCountUpToHighestNonAirBlock(LevelChunk const& chunk) {
#if GF_LL_VERSION >= 2660
    auto const& subChunks = *chunk.mSubChunks;
    for (int i = static_cast<int>(subChunks.size()) - 1; i >= 0; --i) {
        auto const* storage = subChunks[i].mBlocksReadPtr[0];
        if (storage && storage->hasAnyElementMatchingFilterInPalette([](Block const& b) { return !b.isAir(); }))
            return i + 1;
    }
    return 0;
#else
    return chunk.getSubChunkCountUpToHighestNonAirBlock();
#endif
}

void setExtraBlockSimple(LevelChunk& chunk, ChunkBlockPos const& pos, Block const& block) {
#if GF_LL_VERSION >= 2660
    chunk.setExtraBlock(pos, block, nullptr);
#else
    chunk.setExtraBlockSimple(pos, block);
#endif
}

ActorHurtResult hurtResult(bool hurt, bool allowKnockback) {
    ActorHurtResult result{};
#if GF_LL_VERSION >= 2660
    result.mType         = hurt ? ActorHurtResult::Type::DamageProcessed : ActorHurtResult::Type::NotHurt;
    result.mDamageAmount = std::nullopt;
#else
    result.mHurtOrDamage = std::variant<bool, float>(hurt);
#endif
    result.mShouldAllowKnockback = allowKnockback;
    return result;
}

std::optional<float> hurtDamage(ActorHurtResult const& result) {
#if GF_LL_VERSION >= 2660
    return *result.mDamageAmount;
#else
    if (auto const* f = std::get_if<float>(&*result.mHurtOrDamage)) return *f;
    return std::nullopt;
#endif
}

void addModifier(AttributeInstance& instance, AttributeModifier const& modifier, AttributeModificationContext context) {
#if GF_LL_VERSION >= 2660
    instance.addModifier(std::make_shared<AttributeModifier>(modifier), context);
#else
    instance.addModifier(modifier, context);
#endif
}

bool executeEvent(Actor& actor, std::string const& name, VariantParameterList const& params) {
#if GF_LL_VERSION >= 2660 && defined(LL_PLAT_C)
    return ActorDefinitionDescriptor::executeEvent(actor, name, params);
#else
    return actor.executeEvent(name, params);
#endif
}

bool isSwinging(Mob const& mob) {
#if GF_LL_VERSION >= 2660
    return mob.mSwingingHand->has_value();
#else
    return mob.mSwinging;
#endif
}

} // namespace gf::game::compat

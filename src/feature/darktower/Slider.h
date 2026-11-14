#pragma once

// U26 (darktower): the sliding trap block (SliderBlock) and its moving entity (SlideBlock,
// gloamingforest:slider).

class Actor;
class BlockPos;
class BlockSource;

namespace gf::feature::darktower::slider {

/// SliderBlock#scheduleBlockUpdate (also starts the cycle of a generated slider).
void scheduleUpdate(BlockSource& region, BlockPos const& pos);
/// LivingEntity#knockback(strength, x, z) (Java's formula, the target's knockback resistance aside).
void knockback(Actor& target, double strength, double x, double z);

void install();

} // namespace gf::feature::darktower::slider

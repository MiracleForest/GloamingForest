#pragma once

// U21 (lich): the appliers of the lich tower's queued edits (Pending.h) and the leash the unit
// keeps for the zombies tied to iron posts.

class AABB;
class BlockSource;

namespace gf::feature::lich::edits {

/// Registers the pending edit kinds (lectern, brewing_stand, jar, shelf, sinister,
/// leashed_zombie, magic_painting).
void registerEdits();
/// Picks up the zombies tied to a post within `around` (also the ones loaded from a save).
void tickTethers(BlockSource& region, int dim, AABB const& around);
/// Mob#tickLeash for the tied zombies (every tick): pulled back beyond 6 blocks, loose beyond 10.
void tickLeashes();

} // namespace gf::feature::lich::edits

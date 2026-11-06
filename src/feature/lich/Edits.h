#pragma once

// U21 (lich): the appliers of the lich tower's queued edits (Pending.h) and the leash the unit
// keeps for the zombies tied to iron posts.

#include <cstddef>

class BlockPos;
class BlockSource;
class Mob;
class Vec3;

namespace gf::feature::lich::edits {

/// Registers the pending edit kinds (lectern, brewing_stand, jar, shelf, sinister,
/// leashed_zombie, magic_painting) and the tethers' world lifecycle.
void registerEdits();
/// Mob#tickLeash for the tied zombies (every tick): pulled back beyond 6 blocks, loose beyond 10
/// (the knot goes with the leash). The tied zombies are kept by id in the world DB.
void tickLeashes();
/// A persistent zombie at `at` tied to a leash knot on the iron post at `post` (the zombie trap and
/// the perimeter fence's bound zombie; selftests, the client showcase).
Mob* tieZombie(BlockSource& region, BlockPos const& post, Vec3 const& at);
/// The tied zombies the unit keeps (loaded or not).
[[nodiscard]] std::size_t tetheredCount();

} // namespace gf::feature::lich::edits
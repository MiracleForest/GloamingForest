#pragma once

// Contract extension of F8 (rule R18: new files only, folded into feature/entity/Entity.h by A8).
// What the entity contract lacks for the world rules every upstream mob shares:
//
//   - Mob#checkDespawn on Peaceful (Monster#shouldDespawnInPeaceful): F8 removes the marked types
//     generically, so units need no per-mob check.
//   - EntityUtil#canDestroyBlock and ChargeAttackGoal's stricter rule: one block-breaking helper for
//     charges and rampages (minoshroom, hydra, alpha yeti, ...).
//
//     if (mobGriefing) gf::entity::ext::destroyBlocks(region, lo, hi, gf::entity::ext::BreakRule::SpareContainers, false);

#include <string>

class BlockPos;
class BlockSource;

namespace gf::entity::ext {

/// Mob#checkDespawn on Peaceful: an actor of a marked type is removed by F8 at its first tick on
/// Peaceful, before its behaviours run (no death, no drops). Every non-boss upstream monster of Java is
/// marked by default (40 types: the Monsters, HostileWolf and the Ghast subclasses); the bosses go
/// back to their spawners through gf::boss, LoyalZombie (a TamableAnimal) stays. A unit may unmark
/// or mark more types.
void setDespawnInPeaceful(std::string const& entityId, bool despawn);
[[nodiscard]] bool despawnsInPeaceful(std::string const& entityId);

/// Which blocks a mob's block breaking spares.
enum class BreakRule {
    /// EntityUtil#canDestroyBlock: hardness 0..50, not air, no Container block entity (vanilla
    /// containers, item frames, gf::inventory blocks); signs, banners, beds, skulls, spawners and
    /// upstream's jars, trophies, candles break. Hydra, alpha yeti rampage.
    SpareContainers,
    /// canDestroyBlock && getBlockEntity(pos) == null (ChargeAttackGoal: the minoshroom's charge):
    /// also spares every block with a Java block entity (the vanilla block actors Java has too, not
    /// flower pots, note blocks, cauldrons or pistons; upstream's chests, signs, trophies, spawners,
    /// candles, jars, drying racks, ... of TFBlockEntities).
    SpareBlockEntities,
};
/// Whether a mob may break the block at `pos` under `rule` (no mobGriefing check: the callers ask
/// the gamerule, as Java's callers do).
[[nodiscard]] bool canDestroyBlock(BlockSource& region, BlockPos const& pos, BreakRule rule = BreakRule::SpareContainers);
/// Level#destroyBlock(pos, drop) for every block of the closed box min..max that canDestroyBlock
/// allows; nothing when a chunk of the box is not loaded. Returns the number of blocks broken.
int destroyBlocks(BlockSource& region, BlockPos const& min, BlockPos const& max, BreakRule rule, bool drop);

} // namespace gf::entity::ext

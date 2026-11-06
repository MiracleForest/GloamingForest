// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U27 (caves): troll caves and the yeti cave. The unit's ids and the entry points its files share
// (blocks, features, mobs; the structures are in Structures.h).

#include <cstdint>
#include <optional>
#include <string>

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class Mob;
class Player;

namespace gf::feature::caves {

inline constexpr char kTrollsteinn[]    = "twilightforest:trollsteinn";
inline constexpr char kTrollvidr[]      = "twilightforest:trollvidr";
inline constexpr char kTrollber[]       = "twilightforest:trollber";
inline constexpr char kUnripeTrollber[] = "twilightforest:unripe_trollber";
inline constexpr char kHugeMushgloom[]  = "twilightforest:huge_mushgloom";
inline constexpr char kMushgloomStem[]  = "twilightforest:huge_mushgloom_stem";

inline constexpr char kTroll[]       = "twilightforest:troll";
inline constexpr char kThrownBlock[] = "twilightforest:thrown_block";
inline constexpr char kYeti[]        = "twilightforest:yeti";
inline constexpr char kAlphaYeti[]   = "twilightforest:alpha_yeti";
inline constexpr char kFallingIce[]  = "twilightforest:falling_ice";
inline constexpr char kThrownIce[]   = "twilightforest:thrown_ice"; ///< U8's ice bomb

// ---- blocks (Blocks.cpp) ----------------------------------------------------------------------------
/// Level#getMaxLocalRawBrightness(pos): max(block light, sky light - the sky darkening).
[[nodiscard]] int maxLocalRawBrightness(BlockSource& region, BlockPos const& pos);
/// The trollsteinn with the face states its neighbours' light gives at `pos`.
[[nodiscard]] Block const* trollsteinnFor(BlockSource& region, BlockPos const& pos);
/// TrollRootBlock.canPlaceRootBelow: the block at `above` holds a troll root.
[[nodiscard]] bool canPlaceRootBelow(BlockSource& region, BlockPos const& above);
/// TrollRootBlock#useWithoutItem on a trollber (the block turns next tick); false: not a trollber.
bool harvestTrollber(Player& player, core::GlobalPos const& pos);
/// TrollsteinnBlock#randomTick / UnripeTorchClusterBlock#randomTick (true: the block changed).
bool trollsteinnRandomTick(BlockSource& region, BlockPos const& pos);
bool unripeTrollberRandomTick(BlockSource& region, BlockPos const& pos);
/// TrollsteinnBlock#getAnalogOutputSignal.
[[nodiscard]] std::optional<int> trollsteinnComparator(BlockSource& region, BlockPos const& pos);
void installBlocks();

// ---- features (Features.cpp) --------------------------------------------------------------------------
void installFeatures();

// ---- mobs (Troll.cpp, Yeti.cpp, AlphaYeti.cpp, FallingIce.cpp) ---------------------------------------
void installTroll();
void installYeti();
void installAlphaYeti();
void installFallingIce();

/// Troll#hasRock / rock cooldown (selftests).
[[nodiscard]] bool trollHasRock(Actor& troll);
void               setTrollRockCooldownForTest(Actor& troll, int cooldown);
/// Gives the troll a rock now (the take-block step with a fixed block), as its tick would.
void giveTrollRockForTest(Mob& troll, std::string const& blockId);
/// Troll#performRangedAttack at `target` (selftests).
void trollThrowForTest(Mob& troll, Actor& target);
/// The thrown block's carried block id.
[[nodiscard]] std::string thrownBlockState(Actor& thrownBlock);

/// Yeti#isAngry.
[[nodiscard]] bool yetiAngry(Actor& yeti);

/// AlphaYeti state (selftests).
[[nodiscard]] bool alphaCanRampage(Actor& yeti);
[[nodiscard]] bool alphaRampaging(Actor& yeti);
[[nodiscard]] bool alphaTired(Actor& yeti);
void               setAlphaCanRampageForTest(Actor& yeti, bool canRampage);
/// AlphaYeti#makeBlockFallAbove(pos, hangTime): the first ice block above with air below becomes a
/// falling ice entity; true when one did.
bool alphaMakeBlockFallAbove(Mob& yeti, BlockPos const& pos, int hangTime);

/// Spawns a falling ice of `blockId` at the centre of block `pos` (FallingIce(level, x + 0.5, y,
/// z + 0.5, state, hangTime)); nullptr on failure.
Actor* spawnFallingIce(BlockSource& region, BlockPos const& pos, std::string const& blockId, int hangTime);
/// FallingIce state (selftests).
[[nodiscard]] int fallingIceTime(Actor& ice);

} // namespace gf::feature::caves

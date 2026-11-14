#pragma once

// U22 (lichdecor): declarations shared by the lightable blocks (candelabra, skull candles, ominous
// candles), the ominous fire and the brazier. Private to the unit.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

#include "feature/common/Interactions.h"
#include "feature/lichdecor/Lichdecor.h"

namespace gf::feature::lichdecor::light {

namespace ia = gf::interactions;

inline constexpr char kCandelabra[]  = "gloamingforest:candelabra";
inline constexpr char kBrazier[]     = "gloamingforest:brazier";
inline constexpr char kOminousFire[] = "gloamingforest:ominous_fire";
inline constexpr char kEssence[]     = "gloamingforest:exanimate_essence";
inline constexpr char kOminousFireDamage[] = "gloamingforest:ominous_fire";

// ---- upstream's CandleColors (AbstractSkullCandleBlock.CandleColors: the saved int) ----------------------

/// The vanilla candle id of upstream's CandleColors value 0..16 (0 plain, 1 white, 2 light gray, 3 gray,
/// 4 black, 5 red, 6 orange, 7 yellow, 8 green, 9 lime, 10 blue, 11 cyan, 12 light blue, 13 purple,
/// 14 magenta, 15 pink, 16 brown).
[[nodiscard]] std::string candleOfTfColor(int value);
/// upstream's CandleColors value of a vanilla candle id (-1: not a vanilla candle).
[[nodiscard]] int tfColorOfCandle(std::string_view vanillaCandleId);

// ---- shared item / player helpers -------------------------------------------------------------------

/// ItemAbilities.FIRESTARTER_LIGHT: flint and steel, fire charge.
[[nodiscard]] bool isFirestarter(std::string const& itemId);
/// Uses the firestarter: flint and steel loses durability, a fire charge is consumed (creative: nothing).
void useFirestarter(Player& player, ItemStack const& held, std::string const& itemId);
/// Player#getAbilities().mayBuild: not in adventure / spectator mode.
[[nodiscard]] bool mayBuild(Player const& player);
/// The creative rule of upstream's candle removal: in creative the item is added only if the inventory
/// does not already hold one; otherwise added or dropped.
void giveCandleBack(Player& player, std::string const& candleId);
/// The silk touch level of a tool stack (0: none).
[[nodiscard]] int silkTouch(ItemStack const* tool);

// ---- block-local data -------------------------------------------------------------------------------

/// The unit's BlockData object at `pos` (an empty object when none).
[[nodiscard]] Json dataAt(BlockSource& region, BlockPos const& pos);
/// Writes the unit's BlockData object at `pos` (`null` / empty erases it).
void setDataAt(BlockSource& region, BlockPos const& pos, Json const& value);
/// The data of a block that is being broken (kept from interactions::onDestroy until the loot
/// modifiers ran this tick), else the live data.
[[nodiscard]] Json dataForDrops(BlockSource& region, BlockPos const& pos);
/// Remembers the data of `pos` for the drops of a break (called before vanilla breaks it).
void stashForDrops(BlockSource& region, BlockPos const& pos);

// ---- particles and sounds ---------------------------------------------------------------------------

/// LightableBlock#extinguish's smoke: SMOKE (0, 0.025, 0) at each offset, plus the candle
/// extinguish sound.
void extinguishFx(BlockSource& region, BlockPos const& pos, std::vector<Vec3> const& offsets);
/// eruptFlameParticles: 5 particles per offset with velocity ((r - 0.5) * 0.05, 0.015, (r - 0.5) * 0.05).
void eruptFlames(BlockSource& region, BlockPos const& pos, std::string const& particle, std::vector<Vec3> const& offsets);
[[nodiscard]] Vec3 centre(BlockPos const& pos);

// ---- placement --------------------------------------------------------------------------------------

/// BlockPlaceContext#getClickedPos: the clicked block when it is replaceable, else the block on the
/// clicked face when that is replaceable (nullopt: nothing to place into).
[[nodiscard]] std::optional<BlockPos> placementPos(BlockSource& region, BlockPos const& clicked, int face);

// ---- candelabra (Candelabra.cpp) --------------------------------------------------------------------

/// CandelabraBlock.getCandleCount.
[[nodiscard]] int candelabraCandles(Block const& block);
/// Re-applies the DIM candelabra's strong power into its support (SupportPower ext).
void syncCandelabraPower(BlockSource& region, BlockPos const& pos);

// ---- skull candles (SkullCandles.cpp) ---------------------------------------------------------------

/// The skull kind of a skull candle id ("zombie", "skeleton", "wither_skeleton", "creeper", "player",
/// "piglin"; "" when not one) and whether it is the wall form.
struct SkullCandleKind {
    std::string type;
    bool        wall = false;
};
[[nodiscard]] std::optional<SkullCandleKind> skullCandleKind(std::string_view blockId);

// ---- animateTick (the client mod's flames, src/client/lichdecor) --------------------------------------

/// One flame of a block's animateTick: its position inside the block and the flame particle
/// (minecraft:small_flame, gloamingforest:dim_flame, gloamingforest:ominous_flame).
struct AnimateFlame {
    Vec3        offset;
    char const* particle = "";
    bool        smoke    = false; ///< LightableBlock: the 30 % smoke only for NORMAL lighting
};
/// CandelabraBlock / SkullCandleBlock / WallSkullCandleBlock / OminousCandleBlock#animateTick's
/// flames of `block` (`gameTime`: OminousCandleBlock.getCurrentY's bob); empty when unlit or not one.
[[nodiscard]] std::vector<AnimateFlame> animateFlames(Block const& block, BlockPos const& pos, std::int64_t gameTime);
/// The flame offsets of a candelabra's filled slots (Candelabra.cpp).
[[nodiscard]] std::vector<Vec3> candelabraFlameOffsets(Block const& block);
/// A skull candle's flame offsets, the wall form moved a quarter block off the wall (SkullCandles.cpp).
[[nodiscard]] std::vector<Vec3> skullCandleFlameOffsets(Block const& block, bool wall);

// ---- registration -----------------------------------------------------------------------------------

void registerLightable();
void registerCandelabra();
void registerSkullCandles();
void registerOminousCandles();
void registerOminousFire();
void registerBrazier();

} // namespace gf::feature::lichdecor::light

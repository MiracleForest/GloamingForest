#pragma once

// Unit U22 (lichdecor): lich tower interiors and decor. Internal declarations shared by the unit's
// files (no other unit includes this header, rule R2).
//
// Blocks: candelabra, skull candles (6 floor + 6 wall), ominous fire, 17 ominous candles, brazier,
// sinister spawner (+ /gloamingforest sinister_spawner), chiseled canopy bookshelf (Death Tome spawner), skull
// chest, wrought iron fence, terrorcotta arcs / curves / lines, coronation carpet; items wrought
// iron bar, magic painting, 6 skull candle items; the magic painting entity. Room templates are in
// tools/structures/lichdecor (assets/structures/lich/rooms).
//
// Custom blocks have no block entities: per-block data lives in BlockData under the key "lichdecor"
// (an object; each block kind uses its own fields, see the files).

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class Dimension;
class ItemStack;
class Player;

namespace gf::feature::lichdecor {

using Json = nlohmann::json;
using core::GlobalPos;

/// The unit's config section "lichdecor" (docs/spec/config.md).
struct LichdecorConfig {
    int version = 1;
    /// common.disableSkullCandleCreation (disable_skull_candles): using a candle on a skull does
    /// not make a skull candle.
    bool disableSkullCandleCreation = false;
};

/// The BlockData key of the unit.
inline constexpr char kDataKey[] = "lichdecor";

// ---- lighting (LightableBlock) --------------------------------------------------------------------

enum class Lighting { None, Normal, Dim, Ominous };
[[nodiscard]] char const*           lightingName(Lighting l);
[[nodiscard]] std::optional<Lighting> lightingOf(Block const& block); ///< nullopt: no lighting state

// ---- block state helpers --------------------------------------------------------------------------

/// Value text of state `name` (without the "gloamingforest:" prefix, which is added) of `block`
/// ("" when absent). Vanilla states ("minecraft:..." or plain names) are looked up as given when
/// the prefixed one is missing.
[[nodiscard]] std::string state(Block const& block, std::string_view name);
[[nodiscard]] int         intStateOr(Block const& block, std::string_view name, int fallback);
[[nodiscard]] bool        boolState(Block const& block, std::string_view name);
/// `block` with `changes` ({"lighting": "dim", "candles": 2}; names without the "gloamingforest:"
/// prefix get it) applied to its current states (nullptr: unknown state or value).
[[nodiscard]] Block const* withStates(Block const& block, Json const& changes);
/// Sets `block` with `changes` at `pos` (false: unknown state).
bool setStates(BlockSource& region, BlockPos const& pos, Block const& block, Json const& changes);

// ---- directions -----------------------------------------------------------------------------------

/// FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
[[nodiscard]] char const*         faceName(int face);
[[nodiscard]] int                 faceOf(std::string_view name); ///< -1 unknown
[[nodiscard]] std::array<int, 3>  faceStep(int face);
[[nodiscard]] int                 oppositeFace(int face);
[[nodiscard]] BlockPos            relative(BlockPos const& pos, int face, int n = 1);
/// The horizontal FacingID the actor looks along (Java's getHorizontalDirection).
[[nodiscard]] int lookFace(Actor const& actor);
/// Java's Direction.orderedByNearest for the actor's look (6 FacingIDs, nearest first).
[[nodiscard]] std::array<int, 6> nearestLookingDirections(Actor const& actor);

// ---- players and items ----------------------------------------------------------------------------

[[nodiscard]] bool creative(Player const& player);
/// The doTileDrops game rule (Java's doBlockDrops); true without a level.
[[nodiscard]] bool doBlockDrops();
/// Inventory.add, else drop at the player (Java's Player#addItem / drop).
void giveOrDrop(Player& player, ItemStack const& stack);
/// Shrinks the hand stack by one unless the player is in creative.
void consumeOne(Player& player, ItemStack const& handSlot);
/// Spawns an item entity at the centre of `pos` (Block.popResource).
void popResource(BlockSource& region, BlockPos const& pos, ItemStack const& stack);
/// A float in [lo, hi) from the level's random.
[[nodiscard]] float randomFloat(float lo = 0.0F, float hi = 1.0F);
[[nodiscard]] int   randomInt(int bound);

// ---- candles ---------------------------------------------------------------------------------------

/// The 17 vanilla candle colours: "" (plain) and the 16 dye names in CandleColors order
/// (white, orange, magenta, light_blue, yellow, lime, pink, gray, light_gray, cyan, purple, blue,
/// brown, green, red, black).
[[nodiscard]] std::array<std::string_view, 17> const& candleColors();
/// The vanilla candle block / item id of colour index 0..16 ("minecraft:candle", "minecraft:red_candle").
[[nodiscard]] std::string vanillaCandle(int color);
/// The colour index of a vanilla candle id (-1: not a vanilla candle).
[[nodiscard]] int candleColorOf(std::string_view vanillaCandleId);
/// The ominous twin of colour index 0..16 ("gloamingforest:ominous_candle", "gloamingforest:ominous_red_candle").
[[nodiscard]] std::string ominousCandle(int color);
/// The colour index of an ominous candle id (-1: not one).
[[nodiscard]] int ominousColorOf(std::string_view ominousCandleId);

// ---- sounds and particles --------------------------------------------------------------------------

void playSound(BlockSource& region, Vec3 const& at, std::string const& event, float volume = 1.0F, float pitch = 1.0F);
void particle(BlockSource& region, std::string const& id, Vec3 const& at, Vec3 const& velocity = {}, int count = 1);

} // namespace gf::feature::lichdecor

#pragma once

// Unit U4 (woodwork): wood sets and wooden furniture. Internal declarations shared by the unit's
// files (no other unit includes this header, rule R2).
//
// The eight TF wood sets (logs, planks, stairs, doors, signs, chests, ...) are F11 family data
// (assets/gen/import/woodwork/data/gf/families/woodwork.json); this unit adds the blocks Java gives
// their own classes: BanisterBlock, DryingRackBlock (+ DryingRackBlockEntity, DryingRecipe), the
// hollow logs (HorizontalHollowLogBlock, VerticalHollowLogBlock, ClimbableHollowLogBlock,
// HollowLogItem) and the TF boat types.

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"
#include "feature/recipes/Recipes.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;
class Vec3;

namespace gf::feature::woodwork {

using Json = nlohmann::json;
using core::GlobalPos;

inline constexpr char kNs[] = "twilightforest:";

/// TF's eight woods (TFWoodTypes), in TFBlocks order.
inline constexpr std::array<std::string_view, 8> kTfWoods{"twilight_oak", "canopy", "mangrove", "dark", "time", "transformation", "mining", "sorting"};
/// The vanilla woods of the banisters and drying racks (TFBlocks: "vangrove" is vanilla mangrove).
inline constexpr std::array<std::string_view, 11> kVanillaFurnitureWoods{"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "crimson", "warped", "vangrove", "bamboo", "cherry"};
/// The vanilla woods of the hollow logs (no bamboo); crimson / warped are stems.
inline constexpr std::array<std::string_view, 10> kVanillaHollowWoods{"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "crimson", "warped", "vangrove", "cherry"};

/// The 19 banister ids, the 19 drying rack ids.
[[nodiscard]] std::vector<std::string> const& banisterIds();
[[nodiscard]] std::vector<std::string> const& dryingRackIds();

/// One wood's hollow log blocks and their item (HollowLogItem).
struct HollowLog {
    std::string wood;       ///< "oak", "crimson", ...
    std::string item;       ///< twilightforest:hollow_oak_log / hollow_crimson_stem
    std::string horizontal; ///< twilightforest:hollow_oak_log_horizontal
    std::string vertical;   ///< twilightforest:hollow_oak_log_vertical
    std::string climbable;  ///< twilightforest:hollow_oak_log_climbable
};
[[nodiscard]] std::vector<HollowLog> const& hollowLogs();
/// The hollow log set a block or item id belongs to (nullptr: none).
[[nodiscard]] HollowLog const* hollowLogOf(std::string const& id);

// ---- block helpers -------------------------------------------------------------------------------------

/// FacingID (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) as Java's direction names.
[[nodiscard]] char const* faceName(int face);
/// The FacingID of a direction name (-1: unknown).
[[nodiscard]] int faceOf(std::string_view name);
/// The step of a FacingID.
[[nodiscard]] std::array<int, 3> faceStep(int face);
/// The opposite FacingID.
[[nodiscard]] int oppositeFace(int face);
/// The horizontal FacingID a player looks along (Java's getHorizontalDirection / getDirection).
[[nodiscard]] int lookFace(Actor const& actor);

/// The value text of block state `name` ("twilightforest:shape") of `block` ("" when absent).
[[nodiscard]] std::string stateOf(Block const& block, std::string const& name);
/// `block` with `changes` ({"twilightforest:shape": "short", "twilightforest:extended": true})
/// applied to its current states (nullptr: unknown state or value).
[[nodiscard]] Block const* withStates(Block const& block, Json const& changes);
/// The id of `block` ("minecraft:air", "twilightforest:oak_banister").
[[nodiscard]] std::string idOf(Block const& block);

/// Whether the liquid layer at `pos` holds water (Java getFluidState().getType() == WATER: a source).
[[nodiscard]] bool waterSourceAt(BlockSource& region, BlockPos const& pos);
/// Whether there is any water at `pos` (block or liquid layer, source or flowing).
[[nodiscard]] bool waterAt(BlockSource& region, BlockPos const& pos);

/// Plays a sound by Java event or Bedrock name at the centre of `pos` (gf::fx).
void playSound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume, float pitch);
/// A random float 0..1 (Java's level.random.nextFloat for sound pitches).
[[nodiscard]] float randomFloat();

/// Spawns `stack` as an item entity at (x, y, z) (Java's ItemEntity / Containers.dropItemStack).
void spawnItem(BlockSource& region, Vec3 const& at, ItemStack const& stack);
/// Java's Containers.dropItemStack at the block corner (x, y, z) of `pos`: a random spot in the
/// block's inner 0.75 box, a small random motion.
void dropItemStack(BlockSource& region, BlockPos const& pos, ItemStack const& stack);
/// Gives `stack` to the player (Inventory#add), dropping what does not fit at the player.
void giveOrDrop(Player& player, ItemStack const& stack);
/// One item of the held stack is used up (ItemStack#consume: not in creative).
void consumeOne(Player& player, ItemStack const& held);
/// The held tool loses `n` durability (hurtAndBreak; not in creative).
void hurtTool(Player& player, ItemStack const& held, int n = 1);

/// Java ItemAbilities: an axe (AXE_WAX_OFF), a shovel (SHOVEL_DIG), shears (SHEARS_HARVEST).
[[nodiscard]] bool isAxe(std::string const& itemId);
[[nodiscard]] bool isShovel(std::string const& itemId);
[[nodiscard]] bool isShears(std::string const& itemId);

/// Bedrock repeats a use-on while the button is held: true for a repeat of the same player's
/// click on the same block within the hold (the handler then consumes without acting).
[[nodiscard]] bool repeatedUse(Player const& player, GlobalPos const& pos, bool firstEvent);

// ---- parts (each file's install / uninstall) -------------------------------------------------------------

void installBanisters();
void uninstallBanisters();
void installDryingRacks();
void uninstallDryingRacks();
void installHollowLogs();
void uninstallHollowLogs();
void installBoats();
void uninstallBoats();
void installBookshelves();
void installFuel();
/// The dried slime drops' food effects and tannin's craft remainder (Materials.cpp; process-wide).
void installMaterials();
/// Tannin's craft remainder per grid slot (a glass bottle for each tannin).
[[nodiscard]] std::vector<ItemStack> craftRemainders(recipes::CraftingGrid const& grid);
void uninstallFuel();
/// Java's fuel value (ticks) of a wood set item from the vanilla fuel tags (nullopt: none).
[[nodiscard]] std::optional<int> javaFuelTicks(std::string const& itemId);
void uninstallBookshelves();

/// The TF bookshelves (#minecraft:enchantment_power_provider) an enchanting table at `table` counts.
[[nodiscard]] std::vector<BlockPos> tfBookshelvesAround(BlockSource& region, BlockPos const& table);

} // namespace gf::feature::woodwork

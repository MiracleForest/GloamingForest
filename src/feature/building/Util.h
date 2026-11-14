#pragma once

// U5 (building): small block helpers shared by the unit's files (directions as Bedrock FacingIDs:
// 0 down, 1 up, 2 north, 3 south, 4 west, 5 east; Java's Direction.values() order is D U N S W E).

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class Player;

namespace gf::feature::building {

using Json = nlohmann::json;

inline constexpr std::string_view kNs = "gloamingforest:";

enum Dir : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
inline constexpr std::array<int, 6> kDx{0, 0, 0, 0, -1, 1};
inline constexpr std::array<int, 6> kDy{-1, 1, 0, 0, 0, 0};
inline constexpr std::array<int, 6> kDz{0, 0, -1, 1, 0, 0};
inline constexpr std::array<int, 4> kHorizontal{North, South, West, East};

[[nodiscard]] constexpr int opposite(int d) { return d ^ 1; }
[[nodiscard]] constexpr bool horizontal(int d) { return d >= 2; }
/// 0 x, 1 y, 2 z
[[nodiscard]] constexpr int axisOf(int d) { return d <= 1 ? 1 : (d <= 3 ? 2 : 0); }
/// Java Direction#getClockWise / getCounterClockWise (horizontal).
[[nodiscard]] constexpr int clockWise(int d) { return d == North ? East : d == East ? South : d == South ? West : North; }
[[nodiscard]] constexpr int counterClockWise(int d) { return d == North ? West : d == West ? South : d == South ? East : North; }
[[nodiscard]] char const* dirName(int d);
[[nodiscard]] int         dirFromName(std::string_view name); ///< -1: none
[[nodiscard]] char const* axisName(int axis);

[[nodiscard]] BlockPos relative(BlockPos const& p, int d, int n = 1);

/// "gloamingforest:x" of the block (its type name).
[[nodiscard]] std::string idOf(Block const& block);
[[nodiscard]] std::string idAt(BlockSource& region, BlockPos const& pos);
[[nodiscard]] Block const* blockAt(BlockSource& region, BlockPos const& pos);
/// The block's state `name` ("gloamingforest:x") as a string ("true"/"false" for booleans, digits
/// for integers); "" when the block has none.
[[nodiscard]] std::string stateOf(Block const& block, std::string const& name);
[[nodiscard]] bool        boolState(Block const& block, std::string const& name);
[[nodiscard]] int         intState(Block const& block, std::string const& name, int fallback = -1);
/// The block with some states changed (nullptr: unknown state / value).
[[nodiscard]] Block const* withStates(Block const& block, Json const& states);
/// Sets states of the block at `pos` (false: nothing changed).
bool setStates(BlockSource& region, BlockPos const& pos, Json const& states);
bool setBlock(BlockSource& region, BlockPos const& pos, std::string const& id, Json const& states = Json::object());

/// Java Direction.orderedByNearest(entity): the six directions by how much the look follows them.
[[nodiscard]] std::array<int, 6> nearestLooking(Actor const& actor);
/// Java Entity#getDirection (horizontal facing of the yaw).
[[nodiscard]] int horizontalFacing(Actor const& actor);
/// Java Block.isFaceSturdy(FULL) / (CENTER) of the face `face` of the block at `pos`.
[[nodiscard]] bool sturdy(BlockSource& region, BlockPos const& pos, int face);
[[nodiscard]] bool sturdyCenter(BlockSource& region, BlockPos const& pos, int face);
/// Java BlockState#canBeReplaced (air, liquids, plants, snow layers ...).
[[nodiscard]] bool replaceable(BlockSource& region, BlockPos const& pos);
/// Java Level#hasNeighborSignal.
[[nodiscard]] bool hasNeighborSignal(BlockSource& region, BlockPos const& pos);

/// Spawns `stack` as an item entity at the block centre (Block.popResource).
void popResource(BlockSource& region, BlockPos const& pos, ItemStack const& stack);
/// Hands `stack` to the player (dropped at the feet when the inventory is full).
void give(Player& player, ItemStack const& stack);

} // namespace gf::feature::building

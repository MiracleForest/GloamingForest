// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/entity/GrowingBeanstalkBlockEntity.java
//   src/main/java/twilightforest/block/GiantBlock.java (getVolume)
//   src/main/java/twilightforest/client/model/block/giantblock/GiantBlockModel.java
//   src/main/java/twilightforest/block/CloudBlock.java (randomTick scan)
//   src/main/java/twilightforest/asmhooks/BlockHooks.java (isRainingAt)
//   src/main/java/twilightforest/client/event/CloudEvents.java (precipitation stop height)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// U29 (giants), pure logic (gf-core, gtested in tests/giants): the beanstalk grower's growth, the
// 4 x 4 x 4 giant block cells and their texture tiling, and the cloud precipitation scans.

#include <array>
#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/jrand/JRand.h"

namespace gf::core::giants {

/// Mth.sin / Mth.cos (the 65536-entry table of Minecraft 1.21.1).
[[nodiscard]] float mthSin(float value);
[[nodiscard]] float mthCos(float value);

// ---- beanstalk -----------------------------------------------------------------------------------

/// What GrowingBeanstalkBlockEntity asks of a block.
enum class Cell {
    Air,         ///< BlockState#isAir
    Replaceable, ///< canBeReplaced (not the grower, not a wispy cloud)
    WispyCloud,  ///< twilightforest:wispy_cloud (replaceable)
    FluffyCloud, ///< twilightforest:fluffy_cloud
    Leaves,      ///< #minecraft:leaves
    Grower,      ///< twilightforest:beanstalk_grower
    Stalk,       ///< twilightforest:huge_stalk
    Solid,       ///< anything else
};

/// The world the grower edits.
class BeanstalkWorld {
public:
    virtual ~BeanstalkWorld()                         = default;
    [[nodiscard]] virtual Cell at(int x, int y, int z) = 0;
    virtual void               setStalk(int x, int y, int z) = 0;
    virtual void               setAir(int x, int y, int z)   = 0;
    /// A beanstalk leaf with `distance` (UPDATE_CLIENTS: no neighbour updates).
    virtual void setLeaves(int x, int y, int z, int distance) = 0;
};

/// GrowingBeanstalkBlockEntity's saved fields.
struct BeanstalkState {
    int   ticker            = 0;
    int   layer             = 0;
    bool  isAreaClearEnough = true;
    int   nextLeafY         = 0;
    int   yOffset           = 0; ///< saved as "beardifierGroundDelta"
    float cScale            = 0;
    float rScale            = 0;
    int   maxY              = 0;
    int   blocksSkipped     = 0;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static BeanstalkState fromJson(nlohmann::json const& j);
};

/// The outcome of one server tick of the grower.
enum class Step {
    Waiting,  ///< nothing placed this tick (rumbling, or an odd tick)
    Grew,     ///< one layer placed
    Finished, ///< blocked or at maxY: the grower becomes a huge stalk now
};

/// `max(y + 100, 175)`: the beans' and the grower's top.
[[nodiscard]] int beanstalkMaxY(int y);
/// GrowingBeanstalkBlockEntity#tick (server side) at grower position (px, py, pz): increments the
/// ticker, initialises on tick 1 (drawing from `random` as the level's random), grows one layer
/// on even ticks after 100. On Finished the caller replaces the grower with a huge stalk.
Step tickBeanstalk(BeanstalkState& state, BeanstalkWorld& world, jrand::RandomSource& random, int px, int py, int pz);
/// GrowingBeanstalkBlockEntity#isBeanstalkRumbling (ticker < 110).
[[nodiscard]] inline bool isRumbling(BeanstalkState const& state) { return state.ticker < 110; }

// ---- giant blocks --------------------------------------------------------------------------------

/// The aligned 4 x 4 x 4 cell of a position (GiantBlock#getVolume): its minimum corner.
[[nodiscard]] inline std::array<int, 3> cellOrigin(int x, int y, int z) { return {x & ~3, y & ~3, z & ~3}; }
/// The value of the block state `twilightforest:giant_cell` for a position: 1 + (x & 3) + 4 (y & 3)
/// + 16 (z & 3) (1..64; 0 = an untiled block that shows the whole texture: items, unfixed blocks).
[[nodiscard]] inline int cellState(int x, int y, int z) { return 1 + (x & 3) + 4 * (y & 3) + 16 * (z & 3); }
/// The cell-local position (0..3 each) of a `giant_cell` value 1..64.
[[nodiscard]] std::array<int, 3> cellLocal(int state);

/// Java's Direction in ordinal order (DOWN, UP, NORTH, SOUTH, WEST, EAST).
enum class Face { Down = 0, Up, North, South, West, East };
/// GiantBlockModel#calculateOffset(side, pos + magicOffsetFromDir(side)): the top-left corner
/// (u, v), in texture pixels 0..12 step 4, of the 4 x 4 sub-square a face of the block at `pos`
/// shows.
[[nodiscard]] std::array<int, 2> giantFaceUv(Face side, int x, int y, int z);
/// Whether the face of the block at cell-local (lx, ly, lz) is drawn: its neighbour lies outside
/// the cell (GiantBlockModel#getQuads).
[[nodiscard]] bool giantFaceVisible(Face side, int lx, int ly, int lz);

// ---- clouds --------------------------------------------------------------------------------------

/// Whether the block at height y blocks precipitation (Heightmap.Types.MOTION_BLOCKING's
/// isOpaque: blocksMotion or a fluid).
using OpaqueAt = std::function<bool(int y)>;
/// CloudBlock#randomTick: the highest block that receives the cloud's rain or snow below a cloud
/// at `cloudY` (`distance` = common.cloudBlockPrecipitationDistance > 0): the first opaque block
/// below the cloud, or `cloudY - distance` when none is found in the scanned range.
[[nodiscard]] int highestRainyBlock(int cloudY, int distance, OpaqueAt const& opaque);
/// CloudEvents: the lowest y the client's rain / snow sheet reaches under a cloud at `cloudY`
/// (one above highestRainyBlock, the same scan).
[[nodiscard]] int precipitationStopY(int cloudY, int distance, OpaqueAt const& opaque);
/// What BlockHooks#isRainingAt sees at one height.
enum class RainScan { Raining, Opaque, Other };
/// BlockHooks#isRainingAt's scan above `y` (y .. y + distance - 1): true when a raining cloud is
/// met before an opaque block.
[[nodiscard]] bool rainingAbove(int y, int distance, std::function<RainScan(int y)> const& at);
/// The client distance: -1 = the common value, 0 = off (TFConfig#getClientCloudBlockPrecipitationDistance).
[[nodiscard]] inline int clientPrecipitationDistance(int client, int common) { return client < 0 ? common : client; }

// ---- giant pickaxe ---------------------------------------------------------------------------------

/// GiantPickaxeMiningAttachment.
struct GiantPickMining {
    long long mining     = -1;
    bool      breaking   = false;
    int       conversion = 0;
};
/// GiantToolGroupingModifier#doApply for a drop list whose first item is a convertible block: the
/// giant item when this is the first of the 64 conversions (counter 64), else nothing. Returns
/// true when the giant block item replaces the drops, false when the drops vanish.
[[nodiscard]] bool takeGiantConversion(GiantPickMining& state);
/// GiantToolGroupingModifier.CONVERSIONS: the giant block of a dropped block item ("" if none).
[[nodiscard]] std::string giantConversionOf(std::string const& itemId);

} // namespace gf::core::giants

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/TFTeleporter.java
//   src/main/java/twilightforest/world/NoReturnTeleporter.java
//   src/main/java/twilightforest/world/TeleporterCache.java
//   src/main/java/twilightforest/block/TFPortalBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Internal to unit F4. TF's teleporter on Bedrock: Java computes a DimensionTransition with the
// destination loaded synchronously; Bedrock loads chunks around players, so a trip is split in a
// departure step (source-side data, the cache and the destination's already loaded chunks, as
// Java's getChunkNow sees them) and an arrival step that runs once the travelling player has
// loaded the destination area (the portal search, safety checks and portal building).

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

class Block;
class BlockSource;
class Player;

namespace gf::feature::dimension {

/// Ordered BlockPos key (Java's HashMap<BlockPos, Boolean> iteration order is not relied upon).
struct PosLess {
    bool operator()(BlockPos const& a, BlockPos const& b) const { return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z); }
};
using PortalScan = std::map<BlockPos, bool, PosLess>;

/// TFPortalBlock.recursivelyValidatePortal: flood fill of the pool from `pos` (`checked`: pool
/// blocks true, rim blocks false). False when the pool leaks, is larger than maxPortalSize or the
/// rim lacks dirt with nature on top.
bool recursivelyValidatePortal(BlockSource& region, BlockPos const& pos, PortalScan& checked, int& size, Block const& poolBlock);
/// The upward face of the block at `pos` is sturdy (Java isFaceSturdy(..., UP)).
[[nodiscard]] bool sturdyTop(BlockSource& region, BlockPos const& pos);
/// TFPortalBlock#canFormPortal: the pool fluid, or a one-way portal (re-lighting it).
[[nodiscard]] bool canFormPortal(Block const& block);

namespace teleporter {

/// What the teleporter needs from the travelling entity, taken at departure (the entity's level is
/// the source dimension then).
struct Traveller {
    std::int64_t           id = 0;
    std::string            name;
    int                    srcDim = 0;
    BlockPos               srcPos;      ///< entity.blockPosition() in the source
    Vec3                   srcExact;    ///< entity.position() in the source
    float                  yaw = 0, pitch = 0;
    float                  width = 0.6f, height = 1.8f;
    std::vector<BlockPos>  srcPortalBlocks; ///< the source portal's blocks (TFPortalBlock.recursivelyValidatePortal)
    std::optional<BlockPos> srcExit;        ///< getPortalPosition(source, srcPos)
};

/// Captures a player at departure (source dimension loaded).
[[nodiscard]] Traveller capture(Player& player);

// ---- TeleporterCache (world DB "gf:dimension:teleporter_cache") ----
struct PortalPosition {
    BlockPos  pos;
    long long lastUpdateTime = 0;
};
[[nodiscard]] std::optional<PortalPosition> cacheGet(std::string const& dimension, int x, int z);
void cacheAdd(std::string const& dimension, int x, int z, PortalPosition const& position);
void cacheRemove(std::string const& dimension, int x, int z);
void cacheLoad();
void cacheSave();
void cacheClear();

// ---- TFTeleporter ----
/// The nearest portal block (lowest of its column run) within 200 blocks of `pos`, in loaded
/// chunks only (getPortalPosition).
[[nodiscard]] std::optional<BlockPos> getPortalPosition(BlockSource& region, BlockPos const& pos);
/// placeInExistingPortal: the cached portal for the traveller's source column (validated) or the
/// nearest loaded portal; caches the source portal's columns; returns the arrival position (a
/// random walkable block next to the portal). nullopt when there is no portal.
[[nodiscard]] std::optional<Vec3> placeInExistingPortal(BlockSource& dest, Traveller const& t, BlockPos const& pos);
/// The chunk-free part of moveToSafeCoords: `pos`, or the first safe spot of the biome scan.
[[nodiscard]] BlockPos safeColumn(BlockSource& dest, Traveller const& t, BlockPos const& pos);
/// safePosInColumn: `pos` when the traveller fits there, else the top of the column.
[[nodiscard]] Vec3 safePosInColumn(BlockSource& region, Traveller const& t, Vec3 const& pos);
/// makePortal: an existing portal within 16 blocks, else a new one at the best spot (ideal, okay,
/// fallback, anywhere); caches the way back. Returns the portal position.
BlockPos makePortal(BlockSource& dest, Traveller const& t, Vec3 const& pos, bool locked);
/// makePortalAt: the 2x2 portal with its grass rim, dirt floor, cleared air and nature decorations.
BlockPos makePortalAt(BlockSource& region, BlockPos const& pos, bool locked);
/// NoReturnTeleporter#placePosition: a spot next to an existing / ideal / okay portal site, or the
/// scaled position, without building a portal.
[[nodiscard]] Vec3 noReturnPlacePosition(BlockSource& dest, Traveller const& t, Vec3 const& pos);
/// isSafeAround: `pos` and the four points 16 blocks away are not in a locked biome / landmark
/// chunk / outside the border (only checked in the Twilight Forest).
[[nodiscard]] bool isSafeAround(BlockSource& region, BlockPos const& pos, Player* entity, bool checkProgression);
/// The y the Java heightmap MOTION_BLOCKING_NO_LEAVES reports at (x, z).
[[nodiscard]] int heightmap(BlockSource& region, int x, int z);
/// Whether the area within `radius` blocks of `pos` is loaded.
[[nodiscard]] bool areaLoaded(BlockSource& region, BlockPos const& pos, int radius);
/// The horizontal teleportation scale of travelling into `dest` (getHorizontalScale).
[[nodiscard]] double horizontalScale(int dest);
/// Every portal block within `radius` blocks (each axis) of `center`, in loaded chunks.
[[nodiscard]] std::vector<BlockPos> portalBlocksNear(BlockSource& region, BlockPos const& center, int radius);

} // namespace teleporter

} // namespace gf::feature::dimension

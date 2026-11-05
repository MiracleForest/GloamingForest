#pragma once

// U21 (lich): the lich tower landmark (LichTowerStructure and the lichtowerrevamp pieces).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "core/GlobalPos.h"

namespace gf::structure {
class Piece;
}
namespace gf::worldgen {
class Region;
}

namespace gf::feature::lich::tower {

/// Registers the landmark generator of twilightforest:lich_tower and the piece types.
void registerTower();

/// One piece of a test layout.
struct PieceBox {
    std::string               typeId;
    core::BlockBox            box;
    gf::structure::Piece const* piece = nullptr;
};
/// The lich tower's layout at `origin` for a fixed seed on flat terrain at origin.y (selftests: the
/// golden layout hash does not depend on the world). The pieces stay until the next call or
/// forgetTestPieces.
[[nodiscard]] std::vector<PieceBox> layoutForTest(BlockPos const& origin, std::int64_t seed);
/// Places the last test layout's pieces inside `clip`, chunk by chunk through the region
/// `regionAt` gives for a chunk centre (no queued edits).
void placeForTest(std::function<std::unique_ptr<gf::worldgen::Region>(BlockPos const&)> const& regionAt, core::BlockBox const& clip, std::int64_t seed);
void forgetTestPieces();

} // namespace gf::feature::lich::tower

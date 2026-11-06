#pragma once

// U22 (lichdecor): selftest entry points of the unit's files (SelfTestsMisc.cpp; private header).

#include <string>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"

class Actor;
class BlockPos;
class BlockSource;

namespace gf::feature::lichdecor {

/// Bookshelf.cpp: one BookshelfSpawner#serverTick of the shelf at `gp`.
void tickShelfForTest(core::GlobalPos const& gp);
/// SkullChest.cpp: the owner {"name", "uuid"} of the skull chest at `pos` (null clears it).
void setSkullChestOwner(BlockSource& region, BlockPos const& pos, nlohmann::json const& owner);
/// WroughtIron.cpp: recomputes the fence shape at `pos`.
void updateFenceForTest(BlockSource& region, BlockPos const& pos);
/// MagicPainting.cpp: hangs `variantId` at `pos` facing `face` (nullptr: it would not survive).
Actor* spawnMagicPaintingForTest(BlockSource& region, BlockPos const& pos, int face, std::string const& variantId);

} // namespace gf::feature::lichdecor

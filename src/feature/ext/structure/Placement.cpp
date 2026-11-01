// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// The contract extension forwards to the structure kit (feature/structure/Liquids.cpp,
// regionRandom in Structure.cpp).
#include "feature/ext/structure/Placement.h"

#include <algorithm>
#include <string_view>

#include "feature/structure/Internal.h"

namespace gf::structure::ext {

static_assert(std::string_view(kIgnoreWaterlogging) == std::string_view(liquids::kIgnoreWaterlogging), "one liquid-settings marker");

void setIgnoreWaterlogging(std::vector<std::string>& processors, bool ignore) {
    std::erase(processors, std::string(kIgnoreWaterlogging));
    if (ignore) processors.emplace_back(kIgnoreWaterlogging);
}

bool ignoresWaterlogging(std::vector<std::string> const& processors) { return liquids::ignoresWaterlogging(processors); }

void setWaterlogged(Region& region, BlockPos const& pos, bool waterlogged) { liquids::setWaterlogged(region, pos, waterlogged); }

bool isWaterSource(Region& region, BlockPos const& pos) { return liquids::waterSourceAt(region, pos); }

RandomSource& worldRandom(Region& region) { return regionRandom(region); }

} // namespace gf::structure::ext

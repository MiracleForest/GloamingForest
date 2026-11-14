// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// The contract extension forwards to the structure kit's template placement
// (feature/structure/Templates.cpp).
#include "feature/ext/structure/SharedRandom.h"

#include "feature/structure/Internal.h"

namespace gf::structure::ext {

bool placeTemplate(std::string const& id, Region& region, BlockPos const& offset, BlockPos const& pos, TemplateSettings const& settings,
                   RandomSource& random, int flags) {
    auto const* t = templates::parsed(id);
    if (!t) return false;
    templates::Placement pl;
    pl.offset       = offset;
    pl.pivotPos     = pos;
    pl.settings     = settings;
    pl.flags        = flags;
    pl.sharedRandom = &random;
    return templates::placeInWorld(*t, region, pl, random);
}

std::vector<std::pair<BlockPos, std::string>> filterDataMarkers(std::string const& id, BlockPos const& offset, TemplateSettings const& settings,
                                                                RandomSource& random) {
    auto const* t = templates::parsed(id);
    if (!t) return {};
    templates::Placement pl;
    pl.offset       = offset;
    pl.pivotPos     = offset;
    pl.settings     = settings;
    pl.sharedRandom = &random;
    return templates::filterDataMarkers(*t, pl);
}

} // namespace gf::structure::ext

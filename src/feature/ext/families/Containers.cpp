// Contract extension of "inventory" for the families kit (F11): the per-position size of the
// family chests (Containers.h).

#include "feature/ext/families/Containers.h"

#include "mc/world/level/block/Block.h"

#include "feature/families/Families.h"

namespace gf::feature::families::detail {
int chestSize(core::GlobalPos const& pos);
} // namespace gf::feature::families::detail

namespace gf::families_ext {

std::optional<int> containerSize(core::GlobalPos const& pos) {
    namespace fam = gf::feature::families;
    auto const* b = fam::blockAt(pos);
    auto const* e = b ? fam::entryOf(*b) : nullptr;
    if (!e || (e->family != fam::Family::Chest && e->family != fam::Family::TrappedChest)) return std::nullopt;
    return fam::detail::chestSize(pos);
}

} // namespace gf::families_ext

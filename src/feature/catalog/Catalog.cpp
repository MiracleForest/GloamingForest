// Neutral stub of the catalog part of "data" (F0). F9 replaces this file (rule R18).
#include "feature/catalog/Catalog.h"

#include "core/Stub.h"

namespace gf::catalog {

std::vector<Group> const& groups() {
    GF_STUB("catalog.groups");
    static std::vector<Group> const none;
    return none;
}

std::string groupOf(std::string const&) {
    GF_STUB("catalog.groupOf");
    return {};
}

} // namespace gf::catalog

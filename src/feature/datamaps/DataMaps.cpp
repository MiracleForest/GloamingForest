// Neutral stub of the data map part of "data" (F0). F9 replaces this file (rule R18).
#include "feature/datamaps/DataMaps.h"

#include "core/Stub.h"

namespace gf::datamaps {

nlohmann::json const* get(std::string const&, std::string const&) {
    GF_STUB("datamaps.get");
    return nullptr;
}

std::vector<std::string> keys(std::string const&) {
    GF_STUB("datamaps.keys");
    return {};
}

} // namespace gf::datamaps

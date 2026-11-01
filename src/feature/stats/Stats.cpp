// Neutral stub of the statistics part of "data" (F0). F9 replaces this file (rule R18).
#include "feature/stats/Stats.h"

#include "core/Stub.h"

namespace gf::stats {

void      award(Player&, std::string const&, int) { GF_STUB("stats.award"); }
long long value(Player const&, std::string const&) {
    GF_STUB("stats.value");
    return 0;
}

} // namespace gf::stats

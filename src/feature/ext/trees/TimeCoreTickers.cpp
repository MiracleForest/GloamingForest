// Contract extension for U3 (rule R18), see TimeCoreTickers.h.
#include "feature/ext/trees/TimeCoreTickers.h"

#include <unordered_map>

namespace gf::trees_ext {

namespace {
std::unordered_map<std::string, TimeCoreTicker>& tickers() {
    static std::unordered_map<std::string, TimeCoreTicker> m;
    return m;
}
} // namespace

void registerTimeCoreTicker(std::string const& blockId, TimeCoreTicker ticker) {
    if (ticker) tickers()[blockId] = std::move(ticker);
    else tickers().erase(blockId);
}

void unregisterTimeCoreTicker(std::string const& blockId) { tickers().erase(blockId); }

TimeCoreTicker const* timeCoreTicker(std::string const& blockId) {
    auto it = tickers().find(blockId);
    return it == tickers().end() ? nullptr : &it->second;
}

} // namespace gf::trees_ext

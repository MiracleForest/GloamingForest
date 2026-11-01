// Adapted from LiteLDev/Levitate v0.1.0 (src/core/common/Transfer.cpp, key helpers), LGPL-3.0-or-later.
#include "core/Keys.h"

#include <algorithm>
#include <charconv>

namespace gf::core {

std::string worldKey(std::string_view unit, std::string_view rest) {
    std::string k(kWorldDbPrefix);
    k += unit;
    k += ':';
    k += rest;
    return k;
}

bool validWorldKey(std::string_view key) {
    if (!key.starts_with(kWorldDbPrefix)) return false;
    key.remove_prefix(3);
    auto colon = key.find(':');
    if (colon == 0 || colon == std::string_view::npos || colon + 1 >= key.size()) return false;
    for (char c : key.substr(0, colon)) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

std::string_view worldKeyUnit(std::string_view key) {
    if (!validWorldKey(key)) return {};
    key.remove_prefix(3);
    return key.substr(0, key.find(':'));
}

std::string blockDataChunkKey(int dim, int cx, int cz) {
    return worldKey("common", "bd:" + std::to_string(dim) + ":" + std::to_string(cx) + ":" + std::to_string(cz));
}

namespace {
bool parseInts(std::string_view s, char sep, int* out, int n) {
    for (int i = 0; i < n; ++i) {
        auto end = i + 1 < n ? s.find(sep) : s.size();
        if (end == std::string_view::npos) return false;
        auto part = s.substr(0, end);
        if (part.empty()) return false;
        auto [p, ec] = std::from_chars(part.data(), part.data() + part.size(), out[i]);
        if (ec != std::errc{} || p != part.data() + part.size()) return false;
        s.remove_prefix(std::min(s.size(), end + 1));
    }
    return true;
}
} // namespace

bool parseChunkSuffix(std::string_view suffix, int& dim, int& cx, int& cz) {
    int v[3];
    if (!parseInts(suffix, ':', v, 3)) return false;
    dim = v[0], cx = v[1], cz = v[2];
    return true;
}

std::string posKey(int x, int y, int z) { return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z); }

bool parsePosKey(std::string_view key, int& x, int& y, int& z) {
    int v[3];
    if (!parseInts(key, ',', v, 3)) return false;
    x = v[0], y = v[1], z = v[2];
    return true;
}

} // namespace gf::core

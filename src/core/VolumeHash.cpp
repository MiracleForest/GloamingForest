#include "core/VolumeHash.h"

#include <cstdio>

namespace gf::core {

namespace {
constexpr std::uint64_t kPrime = 0x100000001b3ull;
}

std::uint64_t fnv1a64(std::string_view bytes) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= kPrime;
    }
    return h;
}

void VolumeHasher::feed(std::string_view bytes) {
    for (unsigned char c : bytes) {
        mHash ^= c;
        mHash *= kPrime;
    }
}

void VolumeHasher::add(int dx, int dy, int dz, std::string_view name, std::map<std::string, std::string> const& states) {
    std::string line = std::to_string(dx) + "," + std::to_string(dy) + "," + std::to_string(dz) + "=";
    line += name;
    if (!states.empty()) {
        line += '[';
        bool first = true;
        for (auto const& [k, v] : states) { // std::map: sorted by state name
            if (!first) line += ',';
            first = false;
            line += k + "=" + v;
        }
        line += ']';
    }
    line += '\n';
    feed(line);
    ++mCount;
}

void VolumeHasher::addText(std::string_view text) {
    feed(text);
    feed("\n");
}

std::string VolumeHasher::hex() const {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(mHash));
    return buf;
}

} // namespace gf::core

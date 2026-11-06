#include "core/trees/JavaHashSet.h"

namespace gf::core::trees {

namespace {
std::uint32_t spreadOf(int h) { return static_cast<std::uint32_t>(h) ^ (static_cast<std::uint32_t>(h) >> 16); }
} // namespace

int JavaHashSet::hashOf(Pos const& p) {
    // Vec3i#hashCode: (y + z * 31) * 31 + x with int overflow
    auto u = static_cast<std::uint32_t>(p.y) + static_cast<std::uint32_t>(p.z) * 31u;
    return static_cast<int>(u * 31u + static_cast<std::uint32_t>(p.x));
}

JavaHashSet::JavaHashSet() : mTable(16) {}

bool JavaHashSet::add(Pos const& p) {
    auto  s = spreadOf(hashOf(p));
    auto& b = mTable[bucket(s)];
    for (auto const& e : b)
        if (e.pos == p) return false;
    b.push_back({p, s});
    if (++mSize > mTable.size() * 3 / 4) resize();
    return true;
}

bool JavaHashSet::contains(Pos const& p) const {
    auto        s = spreadOf(hashOf(p));
    auto const& b = mTable[bucket(s)];
    for (auto const& e : b)
        if (e.pos == p) return true;
    return false;
}

bool JavaHashSet::remove(Pos const& p) {
    auto  s = spreadOf(hashOf(p));
    auto& b = mTable[bucket(s)];
    for (auto it = b.begin(); it != b.end(); ++it)
        if (it->pos == p) {
            b.erase(it);
            --mSize;
            return true;
        }
    return false;
}

std::optional<Pos> JavaHashSet::pollFirst() {
    for (auto& b : mTable)
        if (!b.empty()) {
            Pos p = b.front().pos;
            b.erase(b.begin());
            --mSize;
            return p;
        }
    return std::nullopt;
}

std::vector<Pos> JavaHashSet::elements() const {
    std::vector<Pos> out;
    out.reserve(mSize);
    for (auto const& b : mTable)
        for (auto const& e : b) out.push_back(e.pos);
    return out;
}

void JavaHashSet::resize() {
    std::size_t                     oldCap = mTable.size();
    std::vector<std::vector<Entry>> next(oldCap * 2);
    // HashMap#resize: each bucket splits into lo (same index) and hi (index + oldCap), in order
    for (std::size_t j = 0; j < oldCap; ++j)
        for (auto const& e : mTable[j]) next[(e.spread & oldCap) ? j + oldCap : j].push_back(e);
    mTable = std::move(next);
}

} // namespace gf::core::trees

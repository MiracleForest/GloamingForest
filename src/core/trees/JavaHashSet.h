#pragma once

// U3 trees (pure C++, gf-core): a java.util.HashSet<BlockPos> with Java's iteration order while it
// changes (insertions, removal of the first element, resizes), for TreeFeature#updateLeaves, whose
// seven work sets are HashSets: which position it takes next (`iterator().next()`) decides the
// order the leaf distances are written in.
//
// Java's HashMap: a table of 16 buckets (load factor 0.75, doubling), bucket index
// (h ^ h >>> 16) & (capacity - 1) of Vec3i#hashCode, a bucket's entries in insertion order (a
// resize splits each bucket keeping that order). Treeified buckets (8+ entries in a table of 64+)
// are not modelled: block position hashes never pile up like that in a tree's box.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gf::core::trees {

struct Pos {
    int  x = 0, y = 0, z = 0;
    bool operator==(Pos const&) const = default;
};

class JavaHashSet {
public:
    JavaHashSet();
    /// HashSet#add: false when `p` is there already.
    bool add(Pos const& p);
    [[nodiscard]] bool contains(Pos const& p) const;
    /// HashSet#remove.
    bool remove(Pos const& p);
    [[nodiscard]] bool        empty() const { return mSize == 0; }
    [[nodiscard]] std::size_t size() const { return mSize; }
    /// iterator().next() + iterator.remove(): the first element in iteration order, removed.
    [[nodiscard]] std::optional<Pos> pollFirst();
    /// The elements in iteration order.
    [[nodiscard]] std::vector<Pos> elements() const;

    /// Vec3i#hashCode.
    [[nodiscard]] static int hashOf(Pos const& p);

private:
    struct Entry {
        Pos           pos;
        std::uint32_t spread;
    };
    std::vector<std::vector<Entry>> mTable;
    std::size_t                     mSize = 0;
    void                            resize();
    [[nodiscard]] std::size_t       bucket(std::uint32_t spread) const { return spread & (mTable.size() - 1); }
};

} // namespace gf::core::trees

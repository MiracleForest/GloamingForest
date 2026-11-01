#pragma once

// Golden volume hashes (rule R19): a structure or tree placed in a test area is reduced to one
// 64-bit FNV-1a hash of its blocks, compared against a reviewed golden value in a selftest.
//
// Blocks are fed in a fixed order (y, then z, then x, ascending, relative to the box's minimum
// corner) as "<dx>,<dy>,<dz>=<block name>[<state>=<value>,...]" with the states sorted by name, so
// the hash depends only on the content of the volume, never on where it was placed.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace gf::core {

class VolumeHasher {
public:
    /// Adds one block at offset (dx, dy, dz) from the box's minimum corner.
    void add(int dx, int dy, int dz, std::string_view name, std::map<std::string, std::string> const& states = {});
    /// Adds raw text (entities, block data) to the hash.
    void addText(std::string_view text);
    [[nodiscard]] std::uint64_t value() const { return mHash; }
    /// 16 lowercase hex digits.
    [[nodiscard]] std::string hex() const;
    [[nodiscard]] long long   count() const { return mCount; }

private:
    void          feed(std::string_view bytes);
    std::uint64_t mHash  = 0xcbf29ce484222325ull;
    long long     mCount = 0;
};

/// FNV-1a 64 of a byte string.
[[nodiscard]] std::uint64_t fnv1a64(std::string_view bytes);

} // namespace gf::core

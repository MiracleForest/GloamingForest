// Adapted from LiteLDev/Levitate v0.1.0 (src/core/GlobalPos.h), LGPL-3.0-or-later.
#pragma once

#include <compare>
#include <string>

namespace gf::core {

/// Block position plus dimension id (0 overworld, 1 nether, 2 end, others as the game reports;
/// the Twilight Forest's id comes from gf::world, see feature/dimension/World.h).
struct GlobalPos {
    int dim = 0;
    int x = 0, y = 0, z = 0;

    friend bool operator==(GlobalPos const&, GlobalPos const&)  = default;
    friend auto operator<=>(GlobalPos const&, GlobalPos const&) = default;
    [[nodiscard]] std::string key() const {
        return std::to_string(dim) + ":" + std::to_string(x) + ":" + std::to_string(y) + ":" + std::to_string(z);
    }
    [[nodiscard]] GlobalPos offset(int dx, int dy, int dz) const { return {dim, x + dx, y + dy, z + dz}; }
};

/// An axis-aligned block box, inclusive on both corners (Java's BoundingBox).
struct BlockBox {
    int minX = 0, minY = 0, minZ = 0;
    int maxX = 0, maxY = 0, maxZ = 0;

    friend bool operator==(BlockBox const&, BlockBox const&) = default;
    [[nodiscard]] long long volume() const {
        return static_cast<long long>(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
    }
    [[nodiscard]] bool contains(int x, int y, int z) const {
        return x >= minX && x <= maxX && y >= minY && y <= maxY && z >= minZ && z <= maxZ;
    }
    [[nodiscard]] bool intersects(BlockBox const& o) const {
        return maxX >= o.minX && minX <= o.maxX && maxY >= o.minY && minY <= o.maxY && maxZ >= o.minZ && minZ <= o.maxZ;
    }
    /// The box spanned by two corners in any order.
    [[nodiscard]] static BlockBox of(int x0, int y0, int z0, int x1, int y1, int z1) {
        return {x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, z0 < z1 ? z0 : z1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0, z0 < z1 ? z1 : z0};
    }
};

} // namespace gf::core

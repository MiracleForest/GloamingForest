#pragma once

// U21 (lich): org.joml.SimplexNoise#noise(float, float, float), the static 3D simplex noise TF's
// LichYardBox shapes the yard paths, dirt and fence with. The algorithm is Stefan Gustavson's
// public-domain "simplex noise demystified" code (Ken Perlin's permutation, radius 0.6, scale 32)
// in float arithmetic as JOML runs it; written here from the algorithm. gtested in tests/lich.

namespace gf::core::lich {

/// Simplex noise at (x, y, z), about -1..1.
[[nodiscard]] float simplex3(float x, float y, float z);

} // namespace gf::core::lich

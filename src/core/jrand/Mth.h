#pragma once

// Java-parity integer and floating point helpers (net.minecraft.util.Mth, java.lang.Math and the
// BlockPos / ChunkPos / SectionPos / QuartPos packing of Minecraft 1.21.1). Pure C++ (gf-core).
//
// Java's int and long arithmetic wraps on overflow; C++ signed overflow is undefined, so every
// helper here computes in unsigned types and converts back (two's complement, C++20).

#include <cmath>
#include <cstdint>

namespace gf::core::mth {

// ---- wrapping Java arithmetic -------------------------------------------------------------------
[[nodiscard]] constexpr std::int32_t imul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr std::int32_t iadd(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr std::int64_t lmul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
[[nodiscard]] constexpr std::int64_t ladd(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
/// Java's `>>>` on a long.
[[nodiscard]] constexpr std::int64_t lushr(std::int64_t a, int n) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) >> n);
}
/// Java's `>>>` on an int.
[[nodiscard]] constexpr std::int32_t iushr(std::int32_t a, int n) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) >> n);
}
/// Java's integer `/` (truncates toward zero; INT_MIN / -1 wraps).
[[nodiscard]] constexpr std::int32_t idiv(std::int32_t a, std::int32_t b) {
    if (b == -1) return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(a));
    return a / b;
}

// ---- Mth ----------------------------------------------------------------------------------------
/// Mth.floor(double): `(int)` cast then one down for negatives with a fraction.
[[nodiscard]] inline int floor(double value) {
    // Java's (int) cast saturates; values outside the int range do not occur in generation.
    int i = static_cast<int>(value);
    return value < static_cast<double>(i) ? i - 1 : i;
}
[[nodiscard]] inline std::int64_t lfloor(double value) {
    auto i = static_cast<std::int64_t>(value);
    return value < static_cast<double>(i) ? i - 1 : i;
}
[[nodiscard]] inline int ceil(double value) {
    int i = static_cast<int>(value);
    return value > static_cast<double>(i) ? i + 1 : i;
}
[[nodiscard]] inline int floorf(float value) {
    int i = static_cast<int>(value);
    return value < static_cast<float>(i) ? i - 1 : i;
}
/// Math.floorDiv / floorMod for ints.
[[nodiscard]] constexpr int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
[[nodiscard]] constexpr int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }
[[nodiscard]] constexpr int positiveModulo(int a, int b) { return floorMod(a, b); }

[[nodiscard]] constexpr double clamp(double value, double min, double max) {
    return value < min ? min : (value > max ? max : value); // Mth.clamp(double): value < min ? min : Math.min(value, max)
}
[[nodiscard]] constexpr float clampf(float value, float min, float max) {
    return value < min ? min : (value > max ? max : value);
}
[[nodiscard]] constexpr int clampi(int value, int min, int max) { return value < min ? min : (value > max ? max : value); }

[[nodiscard]] constexpr double lerp(double delta, double start, double end) { return start + delta * (end - start); }
[[nodiscard]] constexpr float  lerpf(float delta, float start, float end) { return start + delta * (end - start); }
[[nodiscard]] constexpr double lerp2(double dx, double dy, double x0y0, double x1y0, double x0y1, double x1y1) {
    return lerp(dy, lerp(dx, x0y0, x1y0), lerp(dx, x0y1, x1y1));
}
[[nodiscard]] constexpr double lerp3(
    double dx,
    double dy,
    double dz,
    double x0y0z0,
    double x1y0z0,
    double x0y1z0,
    double x1y1z0,
    double x0y0z1,
    double x1y0z1,
    double x0y1z1,
    double x1y1z1
) {
    return lerp(dz, lerp2(dx, dy, x0y0z0, x1y0z0, x0y1z0, x1y1z0), lerp2(dx, dy, x0y0z1, x1y0z1, x0y1z1, x1y1z1));
}
[[nodiscard]] constexpr double smoothstep(double x) { return x * x * x * (x * (x * 6.0 - 15.0) + 10.0); }
[[nodiscard]] constexpr double inverseLerp(double delta, double start, double end) { return (delta - start) / (end - start); }
[[nodiscard]] constexpr double clampedLerp(double start, double end, double delta) {
    if (delta < 0.0) return start;
    return delta > 1.0 ? end : lerp(delta, start, end);
}
[[nodiscard]] constexpr double clampedMap(double input, double inMin, double inMax, double outMin, double outMax) {
    return clampedLerp(outMin, outMax, inverseLerp(input, inMin, inMax));
}
[[nodiscard]] constexpr double map(double input, double inMin, double inMax, double outMin, double outMax) {
    return lerp(inverseLerp(input, inMin, inMax), outMin, outMax);
}
[[nodiscard]] constexpr double square(double x) { return x * x; }

/// Mth.binarySearch(min, max, isTargetBeforeOrAt).
template <class Pred>
[[nodiscard]] constexpr int binarySearch(int min, int max, Pred&& isTargetBeforeOrAt) {
    int i = max - min;
    while (i > 0) {
        int j = i / 2;
        int k = min + j;
        if (isTargetBeforeOrAt(k)) {
            i = j;
        } else {
            min = k + 1;
            i -= j + 1;
        }
    }
    return min;
}

/// Mth.randomBetweenInclusive helpers are on RandomSource (nextIntBetweenInclusive).

// ---- positions ----------------------------------------------------------------------------------
/// BlockPos.asLong (26 bits x, 12 bits y, 26 bits z).
[[nodiscard]] constexpr std::int64_t blockPosAsLong(int x, int y, int z) {
    constexpr int           kXZBits = 26, kYBits = 12;
    constexpr std::uint64_t kXZMask = (1ull << kXZBits) - 1, kYMask = (1ull << kYBits) - 1;
    std::uint64_t           l = 0;
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(x)) & kXZMask) << (kYBits + kXZBits);
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(y)) & kYMask);
    l |= (static_cast<std::uint64_t>(static_cast<std::int64_t>(z)) & kXZMask) << kYBits;
    return static_cast<std::int64_t>(l);
}
[[nodiscard]] constexpr int blockPosX(std::int64_t packed) { return static_cast<int>((packed << (64 - 38 - 26)) >> (64 - 26)); }
[[nodiscard]] constexpr int blockPosY(std::int64_t packed) { return static_cast<int>((packed << (64 - 12)) >> (64 - 12)); }
[[nodiscard]] constexpr int blockPosZ(std::int64_t packed) { return static_cast<int>((packed << (64 - 12 - 26)) >> (64 - 26)); }
/// ChunkPos.asLong.
[[nodiscard]] constexpr std::int64_t chunkPosAsLong(int x, int z) {
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) << 32)
    );
}
[[nodiscard]] constexpr int chunkPosX(std::int64_t packed) { return static_cast<int>(static_cast<std::uint32_t>(packed)); }
[[nodiscard]] constexpr int chunkPosZ(std::int64_t packed) {
    return static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(packed) >> 32));
}
/// SectionPos.blockToSectionCoord / sectionToBlockCoord, QuartPos.
[[nodiscard]] constexpr int blockToSection(int block) { return block >> 4; }
[[nodiscard]] constexpr int sectionToBlock(int section) { return section << 4; }
[[nodiscard]] constexpr int sectionToBlock(int section, int offset) { return (section << 4) + offset; }
[[nodiscard]] constexpr int fromBlock(int block) { return block >> 2; }    ///< QuartPos.fromBlock
[[nodiscard]] constexpr int toBlock(int quart) { return quart << 2; }      ///< QuartPos.toBlock
[[nodiscard]] constexpr int quartFromSection(int section) { return section << 2; }
/// ChunkPos.getMiddleBlockX / getMinBlockX / getMaxBlockX.
[[nodiscard]] constexpr int chunkMinBlock(int chunk) { return chunk << 4; }
[[nodiscard]] constexpr int chunkMiddleBlock(int chunk) { return (chunk << 4) + 7; }
[[nodiscard]] constexpr int chunkMaxBlock(int chunk) { return (chunk << 4) + 15; }
/// The distance used by ChunkPos.getChessboardDistance.
[[nodiscard]] constexpr int chessboardDistance(int ax, int az, int bx, int bz) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dz = az > bz ? az - bz : bz - az;
    return dx > dz ? dx : dz;
}
/// BlockPos.distManhattan.
[[nodiscard]] constexpr int distManhattan(int ax, int ay, int az, int bx, int by, int bz) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    int dz = az > bz ? az - bz : bz - az;
    return dx + dy + dz;
}
/// Vec3i.distSqr (as Java: doubles).
[[nodiscard]] constexpr double distSqr(int ax, int ay, int az, int bx, int by, int bz) {
    double dx = static_cast<double>(ax) - bx, dy = static_cast<double>(ay) - by, dz = static_cast<double>(az) - bz;
    return dx * dx + dy * dy + dz * dz;
}

/// java.lang.StrictMath.log (fdlibm __ieee754_log, bit-exact).
[[nodiscard]] double strictLog(double x);

} // namespace gf::core::mth

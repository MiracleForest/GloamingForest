// Java-parity BiomeManager (Minecraft 1.21.1): seed obfuscation, the fiddled distance and the zoom
// from a block position to the quart whose noise biome it has. Pure C++ (gf-core).
#include "core/jrand/BiomeManager.h"

#include <limits>

#include "core/jrand/Mth.h"

#pragma clang fp contract(off)

namespace gf::core::biome {

namespace {

constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(std::uint32_t (&h)[8], std::uint8_t const* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t{p[4 * i]} << 24) | (std::uint32_t{p[4 * i + 1]} << 16) | (std::uint32_t{p[4 * i + 2]} << 8) | std::uint32_t{p[4 * i + 3]};
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i]             = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t s1  = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t ch  = (e & f) ^ (~e & g);
        std::uint32_t t1  = k + s1 + ch + kK[i] + w[i];
        std::uint32_t s0  = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t t2  = s0 + maj;
        k                 = g;
        g                 = f;
        f                 = e;
        e                 = d + t1;
        d                 = c;
        c                 = b;
        b                 = a;
        a                 = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::string_view data) {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto const*   p    = reinterpret_cast<std::uint8_t const*>(data.data());
    std::size_t   n    = data.size();
    std::size_t   i    = 0;
    for (; i + 64 <= n; i += 64) block(h, p + i);
    std::uint8_t tail[128] = {};
    std::size_t  rest      = n - i;
    for (std::size_t j = 0; j < rest; ++j) tail[j] = p[i + j];
    tail[rest]           = 0x80;
    std::size_t   blocks = rest + 1 + 8 <= 64 ? 1 : 2;
    std::uint64_t bits   = static_cast<std::uint64_t>(n) * 8;
    for (int j = 0; j < 8; ++j) tail[blocks * 64 - 1 - j] = static_cast<std::uint8_t>(bits >> (8 * j));
    for (std::size_t j = 0; j < blocks; ++j) block(h, tail + 64 * j);
    std::array<std::uint8_t, 32> out{};
    for (int j = 0; j < 8; ++j) {
        out[4 * j]     = static_cast<std::uint8_t>(h[j] >> 24);
        out[4 * j + 1] = static_cast<std::uint8_t>(h[j] >> 16);
        out[4 * j + 2] = static_cast<std::uint8_t>(h[j] >> 8);
        out[4 * j + 3] = static_cast<std::uint8_t>(h[j]);
    }
    return out;
}

std::int64_t obfuscateSeed(std::int64_t seed) {
    // Guava: putLong writes the 8 bytes little-endian; HashCode#asLong reads the first 8 bytes of
    // the digest little-endian
    char bytes[8];
    auto u = static_cast<std::uint64_t>(seed);
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<char>(static_cast<std::uint8_t>(u >> (8 * i)));
    auto          digest = sha256(std::string_view(bytes, 8));
    std::uint64_t out    = 0;
    for (int i = 7; i >= 0; --i) out = (out << 8) | digest[static_cast<std::size_t>(i)];
    return static_cast<std::int64_t>(out);
}

std::int64_t lcgNext(std::int64_t left, std::int64_t right) {
    // left *= left * 6364136223846793005L + 1442695040888963407L; return left + right;
    left = mth::lmul(left, mth::ladd(mth::lmul(left, 6364136223846793005LL), 1442695040888963407LL));
    return mth::ladd(left, right);
}

double fiddle(std::int64_t seed) {
    // Math.floorMod(seed >> 24, 1024) / 1024.0
    std::int64_t m = (seed >> 24) % 1024;
    if (m < 0) m += 1024;
    double d0 = static_cast<double>(m) / 1024.0;
    return (d0 - 0.5) * 0.9;
}

double fiddledDistance(std::int64_t seed, int x, int y, int z, double xNoise, double yNoise, double zNoise) {
    std::int64_t l = lcgNext(seed, x);
    l              = lcgNext(l, y);
    l              = lcgNext(l, z);
    l              = lcgNext(l, x);
    l              = lcgNext(l, y);
    l              = lcgNext(l, z);
    double d0      = fiddle(l);
    l              = lcgNext(l, seed);
    double d1      = fiddle(l);
    l              = lcgNext(l, seed);
    double d2      = fiddle(l);
    double a = zNoise + d2, b = yNoise + d1, c = xNoise + d0;
    return a * a + b * b + c * c; // Mth.square(z) + Mth.square(y) + Mth.square(x)
}

Quart zoom(std::int64_t zoomSeed, int x, int y, int z) {
    int    i  = mth::iadd(x, -2);
    int    j  = mth::iadd(y, -2);
    int    k  = mth::iadd(z, -2);
    int    l  = i >> 2;
    int    i1 = j >> 2;
    int    j1 = k >> 2;
    double d0 = static_cast<double>(i & 3) / 4.0;
    double d1 = static_cast<double>(j & 3) / 4.0;
    double d2 = static_cast<double>(k & 3) / 4.0;
    int    k1 = 0;
    double d3 = std::numeric_limits<double>::infinity();
    for (int l1 = 0; l1 < 8; ++l1) {
        bool   fx = (l1 & 4) == 0;
        bool   fy = (l1 & 2) == 0;
        bool   fz = (l1 & 1) == 0;
        int    i2 = fx ? l : l + 1;
        int    j2 = fy ? i1 : i1 + 1;
        int    k2 = fz ? j1 : j1 + 1;
        double d4 = fx ? d0 : d0 - 1.0;
        double d5 = fy ? d1 : d1 - 1.0;
        double d6 = fz ? d2 : d2 - 1.0;
        double d7 = fiddledDistance(zoomSeed, i2, j2, k2, d4, d5, d6);
        if (d3 > d7) {
            k1 = l1;
            d3 = d7;
        }
    }
    return {(k1 & 4) == 0 ? l : l + 1, (k1 & 2) == 0 ? i1 : i1 + 1, (k1 & 1) == 0 ? j1 : j1 + 1};
}

} // namespace gf::core::biome

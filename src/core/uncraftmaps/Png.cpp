// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#include "core/uncraftmaps/Png.h"

#include <algorithm>
#include <array>

namespace gf::core::uncraftmaps {

namespace {
std::array<std::uint32_t, 256> const& crcTable() {
    static auto const table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, char const* type, std::vector<std::uint8_t> const& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start));
}
} // namespace

std::uint32_t crc32(std::uint8_t const* data, std::size_t size, std::uint32_t crc) {
    auto const& t = crcTable();
    crc           = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = t[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

std::uint32_t adler32(std::uint8_t const* data, std::size_t size) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

std::vector<std::uint8_t> encodePng(int width, int height, std::vector<std::uint32_t> const& argb) {
    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8 bit, RGBA, deflate, adaptive filter, no interlace
    chunk(out, "IHDR", ihdr);
    // the raw scanlines (filter 0)
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (static_cast<std::size_t>(width) * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            std::uint32_t c = argb[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            raw.push_back(static_cast<std::uint8_t>(c >> 16));
            raw.push_back(static_cast<std::uint8_t>(c >> 8));
            raw.push_back(static_cast<std::uint8_t>(c));
            raw.push_back(static_cast<std::uint8_t>(c >> 24));
        }
    }
    // zlib: header, stored blocks of up to 65535 bytes, adler32
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::size_t               pos = 0;
    do {
        std::size_t   n    = std::min<std::size_t>(65535, raw.size() - pos);
        bool          last = pos + n >= raw.size();
        auto          len  = static_cast<std::uint16_t>(n);
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(~len));
        z.push_back(static_cast<std::uint8_t>(~len >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    put32(z, adler32(raw.data(), raw.size()));
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

} // namespace gf::core::uncraftmaps

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of the biomepng PNG writer (U13).
#include <gtest/gtest.h>

#include <cstring>

#include "core/uncraftmaps/Png.h"

using namespace gf::core::uncraftmaps;

TEST(UncraftMapsPng, Checksums) {
    char const* s = "123456789";
    EXPECT_EQ(crc32(reinterpret_cast<std::uint8_t const*>(s), 9), 0xCBF43926u);
    EXPECT_EQ(adler32(reinterpret_cast<std::uint8_t const*>("Wikipedia"), 9), 0x11E60398u);
}

TEST(UncraftMapsPng, Structure) {
    auto png = encodePng(2, 1, {0xFF0000FFu, 0x80FF0000u});
    ASSERT_GT(png.size(), 33u + 12u);
    EXPECT_EQ(std::memcmp(png.data(), "\x89PNG\r\n\x1A\n", 8), 0);
    EXPECT_EQ(std::memcmp(png.data() + 12, "IHDR", 4), 0);
    // width 2, height 1, depth 8, colour type 6
    EXPECT_EQ(png[19], 2);
    EXPECT_EQ(png[23], 1);
    EXPECT_EQ(png[24], 8);
    EXPECT_EQ(png[25], 6);
    // the file ends with IEND and its fixed CRC
    auto n = png.size();
    EXPECT_EQ(std::memcmp(png.data() + n - 8, "IEND", 4), 0);
    EXPECT_EQ(png[n - 4], 0xAE);
    EXPECT_EQ(png[n - 3], 0x42);
    EXPECT_EQ(png[n - 2], 0x60);
    EXPECT_EQ(png[n - 1], 0x82);
    // the stored block holds the scanline: filter 0, then RGBA of each pixel
    auto idat = std::string(reinterpret_cast<char const*>(png.data()), png.size()).find("IDAT");
    ASSERT_NE(idat, std::string::npos);
    std::size_t data = idat + 4 + 2 + 5; // zlib header, stored block header
    EXPECT_EQ(png[data], 0);
    EXPECT_EQ(png[data + 1], 0x00); // R of 0xFF0000FF
    EXPECT_EQ(png[data + 3], 0xFF); // B
    EXPECT_EQ(png[data + 4], 0xFF); // A
    EXPECT_EQ(png[data + 5], 0xFF); // R of 0x80FF0000
    EXPECT_EQ(png[data + 8], 0x80); // A
}

TEST(UncraftMapsPng, LargeImagesSplitIntoBlocks) {
    std::vector<std::uint32_t> px(200 * 200, 0xFF00FF00u);
    auto png = encodePng(200, 200, px);
    // 200 rows of 801 bytes = 160200 bytes in 3 stored blocks (+5 bytes each), zlib 2 + 4
    EXPECT_GT(png.size(), 160200u + 15u + 6u);
}

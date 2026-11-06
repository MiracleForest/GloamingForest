// U3 trees: the position tints of TF's magic and rainbow oak leaves (F11's leafcolors, which the
// client mod draws), checked against ColorHandler as Java computes it (a separate translation of
// TF 4.8's src/main/java/twilightforest/client/event/ColorHandler.java, LGPL-2.1-or-later; part of
// GloamingForest under LGPL-3.0-or-later).
#include <gtest/gtest.h>

#include <cstdint>

#include "feature/families/LeafColors.h"

namespace {

int javaFade(int fade) {
    if ((fade & 256) != 0) fade = 255 - (fade & 255);
    return fade & 255;
}

// int arithmetic with Java's wrapping
int mul(int a, int b) { return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
int add(int a, int b) { return static_cast<int>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)); }

unsigned javaSeasonal(int x, int y, int z, int a, int b, int c, int r0, int g0, int b0, int r1, int g1, int b1) {
    int   fade   = javaFade(add(add(mul(x, a), mul(y, b)), mul(z, c)));
    float spring = static_cast<float>(255 - fade) / 255.0F;
    float fall   = static_cast<float>(fade) / 255.0F;
    int   red    = static_cast<int>(spring * static_cast<float>(r0) + fall * static_cast<float>(r1));
    int   green  = static_cast<int>(spring * static_cast<float>(g0) + fall * static_cast<float>(g1));
    int   blue   = static_cast<int>(spring * static_cast<float>(b0) + fall * static_cast<float>(b1));
    return static_cast<unsigned>(red << 16 | green << 8 | blue) & 0xFFFFFFu;
}

unsigned javaRainbow(int x, int y, int z) {
    int red = add(mul(x, 32), mul(y, 16));
    if ((red & 256) != 0) red = 255 - (red & 255);
    red &= 255;
    int green = add(mul(y, 32), mul(z, 16));
    if ((green & 256) != 0) green = 255 - (green & 255);
    green ^= 255;
    int blue = add(mul(x, 16), mul(z, 32));
    if ((blue & 256) != 0) blue = 255 - (blue & 255);
    blue &= 255;
    return static_cast<unsigned>(red << 16 | green << 8 | blue) & 0xFFFFFFu;
}

} // namespace

TEST(TreesLeafTints, MagicLeavesFadeAsColorHandler) {
    namespace lc = gf::feature::families::leafcolors;
    for (int x = -40; x <= 40; x += 3)
        for (int y = -64; y <= 320; y += 17)
            for (int z = -40; z <= 40; z += 5) {
                ASSERT_EQ(lc::time(x, y, z), javaSeasonal(x, y, z, 16, 16, 16, 106, 156, 23, 251, 108, 27)) << x << " " << y << " " << z;
                ASSERT_EQ(lc::transformation(x, y, z), javaSeasonal(x, y, z, 27, 63, 39, 108, 204, 234, 96, 107, 121)) << x << " " << y << " " << z;
                ASSERT_EQ(lc::mining(x, y, z), javaSeasonal(x, y, z, 31, 33, 32, 252, 241, 68, 237, 172, 9)) << x << " " << y << " " << z;
                ASSERT_EQ(lc::sorting(x, y, z), javaSeasonal(x, y, z, 63, 63, 63, 54, 76, 3, 168, 199, 43)) << x << " " << y << " " << z;
                ASSERT_EQ(lc::rainbow(x, y, z), javaRainbow(x, y, z)) << x << " " << y << " " << z;
            }
    // far coordinates wrap like Java's int
    EXPECT_EQ(lc::rainbow(29999984, 70, -29999984), javaRainbow(29999984, 70, -29999984));
    EXPECT_EQ(lc::time(29999984, 300, 29999984), javaSeasonal(29999984, 300, 29999984, 16, 16, 16, 106, 156, 23, 251, 108, 27));
}

TEST(TreesLeafTints, LeafIdsHaveTheirTints) {
    namespace lc = gf::feature::families::leafcolors;
    for (auto const* id : {"twilightforest:time_leaves", "twilightforest:transformation_leaves", "twilightforest:mining_leaves", "twilightforest:sorting_leaves",
                           "twilightforest:rainbow_oak_leaves"})
        EXPECT_TRUE(lc::of(id).has_value()) << id;
    EXPECT_TRUE(lc::blendOf("twilightforest:canopy_leaves").has_value());
    EXPECT_TRUE(lc::blendOf("twilightforest:mangrove_leaves").has_value());
    EXPECT_FALSE(lc::of("twilightforest:dark_leaves").has_value());
    EXPECT_FALSE(lc::of("twilightforest:hardened_dark_leaves").has_value());
    // CANOPY_COLORIZER / MANGROVE_COLORIZER of the plains foliage 0x77AB2F
    EXPECT_EQ(lc::canopy(0x77AB2F), ((0x77AB2Fu & 0xFEFEFEu) + 0x469A66u) / 2u);
    EXPECT_EQ(lc::mangrove(0x77AB2F), ((0x77AB2Fu & 0xFEFEFEu) + 0xC0E694u) / 2u);
}

// U5 (building): the towerwood colour against TF 4.8's own SimplexNoiseHelper / ColorUtil, run on
// a JDK 21 (the goldens were printed by TF's SimplexNoiseHelper.java with stub BlockPos / Mth).

#include <gtest/gtest.h>

#include "core/building/TowerwoodColor.h"

namespace {

using namespace gf::core::building;

struct Golden {
    int           x, y, z;
    float         fractal, ripple;
    std::uint32_t color;
};

constexpr Golden kGolden[]{
    {0, 0, 0, 1.0f, 0.5999999f, 0xDDBA85},
    {1, 2, 3, 0.9409116f, 0.48182312f, 0xD3A766},
    {-17, 64, 250, 1.2480807f, 0.90383863f, 0xF7EDDF},
    {100, -40, -3, 0.8867426f, 0.4265149f, 0xCE9F58},
    {12345, 70, -6789, 0.8869263f, 0.4261475f, 0xCE9F58},
    {-1000, 200, 1000, 0.9035502f, 0.40710035f, 0xCD9C53},
    {31, 31, 31, 1.1270872f, 0.8541744f, 0xF3E4CF},
    {-1, -1, -1, 1.0f, 0.5999999f, 0xDDBA85},
    {500, 64, 500, 0.6522484f, 0.8955033f, 0xF6ECDC},
    {7, 0, -7, 1.2674143f, 0.86517143f, 0xF4E6D3},
};

TEST(BuildingTowerwood, FractalNoiseMatchesJava) {
    for (auto const& g : kGolden) {
        EXPECT_FLOAT_EQ(fractalNoise(2, 32.0f, g.x, g.y, g.z), g.fractal) << g.x << " " << g.y << " " << g.z;
        EXPECT_FLOAT_EQ(rippleFractalNoise(2, 32.0f, g.x, g.y, g.z, 0.4f, 1.0f, 2.0f), g.ripple) << g.x << " " << g.y << " " << g.z;
    }
}

TEST(BuildingTowerwood, ColorMatchesJava) {
    for (auto const& g : kGolden) EXPECT_EQ(towerwoodColor(g.x, g.y, g.z), g.color) << g.x << " " << g.y << " " << g.z;
}

TEST(BuildingTowerwood, HsvToRgb) {
    EXPECT_EQ(hsvToRgb(0.0f, 1.0f, 1.0f), 0xFF0000u);
    EXPECT_EQ(hsvToRgb(1.0f / 3.0f, 1.0f, 1.0f), 0x00FF00u);
    EXPECT_EQ(hsvToRgb(0.1f, 0.0f, 1.0f), 0xFFFFFFu);
}

TEST(BuildingTowerwood, RippleStaysInRange) {
    for (int x = -40; x < 40; x += 3)
        for (int z = -40; z < 40; z += 5) {
            float f = rippleFractalNoise(2, 32.0f, x, 64, z, 0.4f, 1.0f, 2.0f);
            EXPECT_GE(f, 0.4f);
            EXPECT_LE(f, 1.0f + 1e-6f);
        }
}

} // namespace

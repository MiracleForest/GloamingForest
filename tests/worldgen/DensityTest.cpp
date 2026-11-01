// Density functions, NoiseChunk interpolation and Java position maths (src/core/noise, F5).
#include <gtest/gtest.h>

#include <cmath>

#include "core/jrand/Mth.h"
#include "core/noise/Noise.h"
#include "core/noise/NoiseChunk.h"

using namespace gf::core;
using noise::Density;
using noise::DensityPoint;
using Json = nlohmann::json;

namespace {

Density::Resolver resolver() {
    Density::Resolver r;
    r.noise = [](std::string const& id) -> std::optional<noise::NoiseParameters> {
        if (id == "minecraft:gf_test") return noise::NoiseParameters{-5, {1.0, 0.5, 0.25}};
        return std::nullopt;
    };
    r.density = [](std::string const& id) -> std::optional<Json> {
        if (id == "gf_test:half") return Json(0.5);
        if (id == "gf_test:loop") return Json("gf_test:loop");
        return std::nullopt;
    };
    return r;
}

double eval(char const* json, DensityPoint p = {}) {
    auto d = Density::fromJson(Json::parse(json), 1234, resolver());
    EXPECT_TRUE(d.has_value()) << json;
    return d ? d->compute(p) : NAN;
}

} // namespace

TEST(WorldgenDensity, ArithmeticAndMapping) {
    EXPECT_EQ(eval("2.5"), 2.5);
    EXPECT_EQ(eval(R"({"type":"minecraft:add","argument1":1.5,"argument2":"gf_test:half"})"), 2.0);
    EXPECT_EQ(eval(R"({"type":"mul","argument1":-3,"argument2":2})"), -6.0);
    EXPECT_EQ(eval(R"({"type":"minecraft:min","argument1":-3,"argument2":2})"), -3.0);
    EXPECT_EQ(eval(R"({"type":"minecraft:max","argument1":-3,"argument2":2})"), 2.0);
    EXPECT_EQ(eval(R"({"type":"minecraft:abs","argument":-0.75})"), 0.75);
    EXPECT_EQ(eval(R"({"type":"minecraft:square","argument":-3})"), 9.0);
    EXPECT_EQ(eval(R"({"type":"minecraft:cube","argument":-2})"), -8.0);
    EXPECT_EQ(eval(R"({"type":"minecraft:half_negative","argument":-2})"), -1.0);
    EXPECT_EQ(eval(R"({"type":"minecraft:quarter_negative","argument":-2})"), -0.5);
    EXPECT_EQ(eval(R"({"type":"minecraft:half_negative","argument":2})"), 2.0);
    // squeeze: clamp to [-1, 1], then x/2 - x^3/24
    EXPECT_DOUBLE_EQ(eval(R"({"type":"minecraft:squeeze","argument":5})"), 0.5 - 1.0 / 24.0);
    EXPECT_EQ(eval(R"({"type":"minecraft:clamp","input":5,"min":-1,"max":0.5})"), 0.5);
}

TEST(WorldgenDensity, GradientRangeChoiceAndSpline) {
    char const* grad = R"({"type":"minecraft:y_clamped_gradient","from_y":0,"to_y":100,"from_value":1,"to_value":-1})";
    EXPECT_EQ(eval(grad, {0, -50, 0}), 1.0);
    EXPECT_EQ(eval(grad, {0, 50, 0}), 0.0);
    EXPECT_EQ(eval(grad, {0, 150, 0}), -1.0);
    char const* rc = R"({"type":"minecraft:range_choice","input":{"type":"minecraft:y_clamped_gradient","from_y":0,"to_y":10,"from_value":0,"to_value":10},
                         "min_inclusive":2,"max_exclusive":5,"when_in_range":1,"when_out_of_range":-1})";
    EXPECT_EQ(eval(rc, {0, 2, 0}), 1.0);
    EXPECT_EQ(eval(rc, {0, 5, 0}), -1.0);
    // a cubic spline over y: linear extension outside, hermite inside (float maths)
    char const* spline = R"({"type":"minecraft:spline","spline":{"coordinate":{"type":"minecraft:y_clamped_gradient","from_y":-10,"to_y":10,"from_value":-1,"to_value":1},
                            "points":[{"location":-0.5,"value":0,"derivative":0},{"location":0.5,"value":1,"derivative":0}]}})";
    EXPECT_EQ(eval(spline, {0, -10, 0}), 0.0);
    EXPECT_EQ(eval(spline, {0, 10, 0}), 1.0);
    EXPECT_EQ(eval(spline, {0, 0, 0}), 0.5);
}

TEST(WorldgenDensity, UnknownAndRecursiveReferencesFail) {
    EXPECT_FALSE(Density::fromJson(Json::parse(R"({"type":"minecraft:nope"})"), 0, resolver()).has_value());
    EXPECT_FALSE(Density::fromJson(Json("gf_test:missing"), 0, resolver()).has_value());
    EXPECT_FALSE(Density::fromJson(Json("gf_test:loop"), 0, resolver()).has_value());
}

TEST(WorldgenDensity, CustomTypes) {
    Density::registerType("gf_test:x_plus", [](Json const& j, std::int64_t, Density::Resolver const&) -> Density::Evaluator {
        double add = j.value("add", 0.0);
        return [add](DensityPoint const& p) { return p.blockX + add; };
    });
    EXPECT_EQ(eval(R"({"type":"gf_test:x_plus","add":0.5})", {3, 0, 0}), 3.5);
    EXPECT_EQ(eval(R"({"type":"minecraft:mul","argument1":{"type":"gf_test:x_plus"},"argument2":2})", {3, 0, 0}), 6.0);
}

TEST(WorldgenDensity, MarkersPassThroughForSinglePoints) {
    char const* n     = R"({"type":"minecraft:noise","noise":"gf_test","xz_scale":0.3,"y_scale":0.7})";
    std::string inner = n;
    for (char const* m : {"interpolated", "flat_cache", "cache_2d", "cache_once", "cache_all_in_cell"}) {
        std::string wrapped = std::string(R"({"type":"minecraft:)") + m + R"(","argument":)" + inner + "}";
        EXPECT_EQ(eval(wrapped.c_str(), {5, 17, -9}), eval(n, {5, 17, -9})) << m;
    }
}

TEST(WorldgenNoiseChunk, InterpolationMatchesCornersAndFlatCacheSamplesYZero) {
    auto res = resolver();
    Json n   = Json::parse(R"({"type":"minecraft:noise","noise":"gf_test","xz_scale":0.3,"y_scale":0.7})");
    auto direct = Density::fromJson(n, 99, res);
    ASSERT_TRUE(direct.has_value());
    noise::NoiseShape shape{.minY = -32, .height = 64, .sizeHorizontal = 2, .sizeVertical = 2};

    auto interp = noise::NoiseChunk::fromJson(Json{{"type", "minecraft:interpolated"}, {"argument", n}}, 99, res, shape);
    ASSERT_TRUE(interp);
    auto v = interp->fill(-3, 2);
    ASSERT_EQ(v.size(), 64u * 256u);
    auto at = [&](int x, int y, int z) { return v[static_cast<std::size_t>(((y + 32) * 16 + z) * 16 + x)]; };
    // cell corners (multiples of 8) equal the direct value; inside, a trilinear blend
    for (int y : {-32, -24, 0, 24})
        for (int x : {0, 8})
            for (int z : {0, 8}) EXPECT_EQ(at(x, y, z), direct->compute({-48 + x, y, 32 + z}));
    double c0 = direct->compute({-48, 0, 32}), c1 = direct->compute({-40, 0, 32});
    EXPECT_DOUBLE_EQ(at(4, 0, 0), mth::lerp(0.5, c0, c1));

    auto flat = noise::NoiseChunk::fromJson(Json{{"type", "minecraft:flat_cache"}, {"argument", n}}, 99, res, shape);
    auto f    = flat->fill(-3, 2);
    auto fat  = [&](int x, int y, int z) { return f[static_cast<std::size_t>(((y + 32) * 16 + z) * 16 + x)]; };
    EXPECT_EQ(fat(5, 20, 9), direct->compute({-48 + 4, 0, 32 + 8}));
    EXPECT_EQ(fat(5, -30, 9), fat(6, 7, 10));
}

TEST(WorldgenMth, JavaPositionPacking) {
    EXPECT_EQ(mth::blockPosAsLong(0, 0, 0), 0);
    for (auto p : {std::array{1, 2, 3}, std::array{-1, -64, -1}, std::array{30000000, 2047, -30000000}, std::array{-123, 300, 456}}) {
        auto l = mth::blockPosAsLong(p[0], p[1], p[2]);
        EXPECT_EQ(mth::blockPosX(l), p[0]);
        EXPECT_EQ(mth::blockPosY(l), p[1]);
        EXPECT_EQ(mth::blockPosZ(l), p[2]);
    }
    // BlockPos.asLong(1, 2, 3) in Java
    EXPECT_EQ(mth::blockPosAsLong(1, 2, 3), 274877919234LL);
    EXPECT_EQ(mth::chunkPosAsLong(-1, 2), (2LL << 32) | 0xFFFFFFFFLL);
    EXPECT_EQ(mth::chunkPosX(mth::chunkPosAsLong(-7, 9)), -7);
    EXPECT_EQ(mth::chunkPosZ(mth::chunkPosAsLong(-7, 9)), 9);
    EXPECT_EQ(mth::floor(-0.5), -1);
    EXPECT_EQ(mth::floor(2.0), 2);
    EXPECT_EQ(mth::floorDiv(-1, 16), -1);
    EXPECT_EQ(mth::floorMod(-1, 16), 15);
    EXPECT_EQ(mth::imul(0x7FFFFFFF, 2), -2);
    EXPECT_EQ(mth::fromBlock(-1), -1);
    EXPECT_EQ(mth::chunkMiddleBlock(-1), -9);
    EXPECT_EQ(mth::binarySearch(0, 4, [](int k) { return k >= 2; }), 2);
}

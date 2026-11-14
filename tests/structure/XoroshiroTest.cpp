// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// The structure kit's Xoroshiro128++ source (WorldGenRegion#getRandom) against Minecraft 1.21.1:
// XoroshiroGolden.inc is the output of oracle/XoroshiroOracle.java, run against the 1.21.1 server
// jar (net.minecraft.world.level.levelgen.XoroshiroRandomSource and its positional factory).

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "feature/structure/pure/Xoroshiro.h"

using gf::structure::pure::Xoroshiro;

namespace {

enum class Kind { Plain, Reseeded, Region, Fork };

struct Row {
    Kind         kind;
    std::int64_t seed;
    int          chunkX, chunkZ;
    struct Values {
        int          nextInt;
        int          nextInt10;
        int          nextInt1000;
        std::int64_t nextLong;
        int          nextBoolean;
        std::int32_t nextFloatBits;
        std::int64_t nextDoubleBits;
        std::int64_t nextGaussianBits;
        int          nextInt3;
        int          nextIntMax;
    } values;
};

Row const kRows[] = {
#include "XoroshiroGolden.inc"
};

std::string hex(std::array<std::uint8_t, 16> const& d) {
    static char const* digits = "0123456789abcdef";
    std::string        s;
    for (auto b : d) {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

void expectRow(gf::core::jrand::RandomSource& r, Row const& row) {
    auto const& v = row.values;
    std::string at = "seed " + std::to_string(row.seed) + " chunk " + std::to_string(row.chunkX) + "," + std::to_string(row.chunkZ);
    EXPECT_EQ(r.nextInt(), v.nextInt) << at;
    EXPECT_EQ(r.nextInt(10), v.nextInt10) << at;
    EXPECT_EQ(r.nextInt(1000), v.nextInt1000) << at;
    EXPECT_EQ(r.nextLong(), v.nextLong) << at;
    EXPECT_EQ(r.nextBoolean() ? 1 : 0, v.nextBoolean) << at;
    EXPECT_EQ(std::bit_cast<std::int32_t>(r.nextFloat()), v.nextFloatBits) << at;
    EXPECT_EQ(std::bit_cast<std::int64_t>(r.nextDouble()), v.nextDoubleBits) << at;
    // Math.log / Math.sqrt may differ from the C library in the last bit
    EXPECT_LE(std::llabs(std::bit_cast<std::int64_t>(r.nextGaussian()) - v.nextGaussianBits), 2) << at;
    EXPECT_EQ(r.nextInt(3), v.nextInt3) << at;
    EXPECT_EQ(r.nextInt(0x7FFFFFFF), v.nextIntMax) << at;
}

} // namespace

TEST(StructureXoroshiro, Md5MatchesRfc1321) {
    namespace md5 = gf::structure::pure::md5;
    EXPECT_EQ(hex(md5::digest("")), "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(hex(md5::digest("abc")), "900150983cd24fb0d6963f7d28e17f72");
    EXPECT_EQ(hex(md5::digest("The quick brown fox jumps over the lazy dog")), "9e107d9d372bb6826bd81d3542a419d6");
    // more than one 64-byte block
    EXPECT_EQ(hex(md5::digest("12345678901234567890123456789012345678901234567890123456789012345678901234567890")),
              "57edf4a22be3c955ac49da2e2107b67a");
}

TEST(StructureXoroshiro, MatchesMinecraft) {
    int checked = 0;
    for (auto const& row : kRows) {
        switch (row.kind) {
        case Kind::Plain: {
            Xoroshiro r(row.seed);
            expectRow(r, row);
            break;
        }
        case Kind::Reseeded: {
            Xoroshiro r(row.seed);
            r.setSeed(row.seed + 1);
            expectRow(r, row);
            break;
        }
        case Kind::Region: {
            auto r = gf::structure::pure::worldgenRegionRandom(row.seed, row.chunkX, row.chunkZ);
            expectRow(*r, row);
            break;
        }
        case Kind::Fork: {
            Xoroshiro r(row.seed);
            auto      f = r.fork();
            expectRow(*f, row);
            break;
        }
        }
        checked++;
    }
    EXPECT_EQ(checked, 30);
}

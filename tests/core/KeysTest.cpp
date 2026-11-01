#include <gtest/gtest.h>

#include "core/Keys.h"

using namespace gf::core;

TEST(Keys, WorldKeys) {
    EXPECT_EQ(worldKey("landmark", "starts"), "gf:landmark:starts");
    EXPECT_TRUE(validWorldKey("gf:common:bd:0:1:2"));
    EXPECT_TRUE(validWorldKey("gf:spec-world:x"));
    EXPECT_FALSE(validWorldKey("lv:common:x"));
    EXPECT_FALSE(validWorldKey("gf::x"));
    EXPECT_FALSE(validWorldKey("gf:Unit:x"));
    EXPECT_FALSE(validWorldKey("gf:unit:"));
    EXPECT_EQ(worldKeyUnit("gf:trees:cores"), "trees");
}

TEST(Keys, Chunks) {
    EXPECT_EQ(chunkOf(0), 0);
    EXPECT_EQ(chunkOf(15), 0);
    EXPECT_EQ(chunkOf(16), 1);
    EXPECT_EQ(chunkOf(-1), -1);
    EXPECT_EQ(chunkOf(-16), -1);
    EXPECT_EQ(chunkOf(-17), -2);
    EXPECT_EQ(blockDataChunkKey(0, -1, 2), "gf:common:bd:0:-1:2");
    int d, x, z;
    EXPECT_TRUE(parseChunkSuffix("3:-4:5", d, x, z));
    EXPECT_EQ(d, 3);
    EXPECT_EQ(x, -4);
    EXPECT_EQ(z, 5);
    EXPECT_FALSE(parseChunkSuffix("3:x:5", d, x, z));
    int px, py, pz;
    EXPECT_TRUE(parsePosKey(posKey(1, -2, 3), px, py, pz));
    EXPECT_EQ(py, -2);
    EXPECT_FALSE(parsePosKey("1,2", px, py, pz));
}
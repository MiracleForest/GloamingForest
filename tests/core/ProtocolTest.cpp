#include <gtest/gtest.h>

#include "core/Protocol.h"

using namespace gf::core;

TEST(Protocol, Channels) {
    EXPECT_TRUE(validChannel("net.hello"));
    EXPECT_TRUE(validChannel("uncraftmaps.recipe_page"));
    EXPECT_TRUE(validChannel("spec-world.x"));
    EXPECT_FALSE(validChannel("nodot"));
    EXPECT_FALSE(validChannel(".start"));
    EXPECT_FALSE(validChannel("end."));
    EXPECT_FALSE(validChannel("Upper.case"));
    EXPECT_FALSE(validChannel(std::string(65, 'a') + ".b"));
    EXPECT_EQ(channelUnit("tgear.zoom"), "tgear");
    EXPECT_EQ(channelUnit("none"), "");
}

TEST(Protocol, Payloads) {
    EXPECT_EQ(encodePayload(nullptr), "{}");
    auto j = decodePayload(R"({"a":1})");
    ASSERT_TRUE(j.has_value());
    EXPECT_EQ((*j)["a"], 1);
    EXPECT_FALSE(decodePayload("{broken").has_value());
    EXPECT_TRUE(protocolAccepted(kProtocolVersion));
    EXPECT_FALSE(protocolAccepted(kProtocolVersion + 1));
}
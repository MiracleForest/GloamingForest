// Adapted from LiteLDev/Levitate v0.1.0 (tests/core/FormatTest.cpp), LGPL-3.0-or-later.
#include <limits>
#include <gtest/gtest.h>

#include "core/Format.h"

using gf::core::compactCount;
using gf::core::formatMessageArgs;

TEST(Format, SequentialAndPositional) {
    EXPECT_EQ(formatMessageArgs("Page {} of {}", 2, 5), "Page 2 of 5");
    EXPECT_EQ(formatMessageArgs("{1} before {0}", "a", "b"), "b before a");
    EXPECT_EQ(formatMessageArgs("{{literal}} {}", 1), "{literal} 1");
    EXPECT_EQ(formatMessageArgs("missing {} {}", 1), "missing 1 {}");
    EXPECT_EQ(formatMessageArgs("unknown {x}", 1), "unknown {x}");
}

TEST(Format, JavaPlaceholders) {
    // upstream's lang files use Java's String.format placeholders
    EXPECT_EQ(formatMessageArgs("/%s <info | conquer>", "gloamingforest"), "/gloamingforest <info | conquer>");
    EXPECT_EQ(formatMessageArgs("%2$s then %1$s", "a", "b"), "b then a");
    EXPECT_EQ(formatMessageArgs("%d%% done", 50), "50% done");
    EXPECT_EQ(formatMessageArgs("%s and %s", "x"), "x and %s");
    EXPECT_EQ(formatMessageArgs("%3$s", "a"), "%3$s");
}

TEST(Format, CompactCount) {
    EXPECT_EQ(compactCount(999), "999");
    EXPECT_EQ(compactCount(1234), "1.2k");
    EXPECT_EQ(compactCount(1500000), "1.5M");
    EXPECT_EQ(compactCount(-2000), "-2k");
    EXPECT_EQ(compactCount(1'999'999'999), "1.9G");
}

TEST(Format, CompactCountExtremes) {
    // no intermediate value may overflow (signed overflow is undefined behaviour)
    EXPECT_EQ(compactCount(std::numeric_limits<long long>::max()), "9223372036G");
    EXPECT_EQ(compactCount(std::numeric_limits<long long>::min()), "-9223372036G");
    EXPECT_EQ(compactCount(999'999'999'999'999'999), "999999999G");
    EXPECT_EQ(compactCount(922'337'203'685'477'581), "922337203G"); // just above LLONG_MAX / 10
}
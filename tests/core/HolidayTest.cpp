#include <gtest/gtest.h>

#include "core/Holiday.h"

using namespace gf::core::holiday;

TEST(Holiday, HalloweenWeekBounds) {
    // upstream: strictly after October 19 and strictly before November 4
    EXPECT_FALSE(isHalloweenWeek({10, 19}));
    EXPECT_TRUE(isHalloweenWeek({10, 20}));
    EXPECT_TRUE(isHalloweenWeek({10, 31}));
    EXPECT_TRUE(isHalloweenWeek({11, 3}));
    EXPECT_FALSE(isHalloweenWeek({11, 4}));
    EXPECT_FALSE(isHalloweenWeek({6, 1}));
}

TEST(Holiday, Birthday) {
    EXPECT_TRUE(isGloamingBirthday({8, 19}));
    EXPECT_FALSE(isGloamingBirthday({8, 20}));
    EXPECT_EQ(gloamingAge(2026), 15);
    EXPECT_TRUE(isAprilFools({4, 1}));
    auto t = today();
    EXPECT_GE(t.month, 1);
    EXPECT_LE(t.month, 12);
}
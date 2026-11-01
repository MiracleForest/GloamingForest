// Adapted from LiteLDev/Levitate v0.1.0 (tests/core/GlobTest.cpp), LGPL-3.0-or-later.
#include <gtest/gtest.h>

#include "core/Glob.h"

using gf::core::globMatch;

TEST(Glob, Basics) {
    EXPECT_TRUE(globMatch("*", "anything"));
    EXPECT_TRUE(globMatch("core.*", "core.world_db"));
    EXPECT_FALSE(globMatch("core.*", "trees.canopy"));
    EXPECT_TRUE(globMatch("?ore.*", "core.x"));
    EXPECT_TRUE(globMatch("*.lich_*", "lich.lich_phase_2"));
    EXPECT_FALSE(globMatch("a", "ab"));
    EXPECT_TRUE(globMatch("", ""));
}
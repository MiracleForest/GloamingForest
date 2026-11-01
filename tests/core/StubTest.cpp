#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/Stub.h"

TEST(Stub, ReportsOnce) {
    std::vector<std::string> lines;
    gf::core::setStubSink([&lines](std::string const& line) { lines.push_back(line); });
    GF_STUB("test.stub_a");
    GF_STUB("test.stub_a");
    GF_STUB("test.stub_b");
    gf::core::setStubSink({});
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "test.stub_a: not implemented (contract stub)");
    auto calls = gf::core::stubCalls();
    EXPECT_EQ(std::count(calls.begin(), calls.end(), "test.stub_a"), 1);
}
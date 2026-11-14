// GloamingForest - U22 (lichdecor): gtests of src/core/lichdecor/Arguments.

#include <gtest/gtest.h>

#include "core/lichdecor/Arguments.h"

using namespace gf::core::lichdecor;

namespace {
bool knownTable(std::string const& id) { return id == "gloamingforest:blocks/ominous_spawner_drops"; }
} // namespace

TEST(LichdecorArguments, Registry) {
    EXPECT_EQ(particleRegistry().size(), 109u + 29u);
    EXPECT_TRUE(isRegisteredParticle("minecraft:smoke"));
    EXPECT_TRUE(isRegisteredParticle("gloamingforest:ominous_flame"));
    EXPECT_FALSE(isRegisteredParticle("gloamingforest:no_such_particle"));
}

TEST(LichdecorArguments, Particle) {
    ArgError e;
    auto     p = parseParticle("smoke", &e);
    ASSERT_TRUE(p);
    EXPECT_EQ(p->id, "minecraft:smoke");
    EXPECT_TRUE(p->valueEqual);

    EXPECT_FALSE(parseParticle("gloamingforest:no_such_particle", &e));
    EXPECT_EQ(e.key, "particle.notFound");
    EXPECT_EQ(e.text, "Unknown particle: gloamingforest:no_such_particle");

    EXPECT_FALSE(parseParticle("Bad:Id", &e));
    EXPECT_EQ(e.key, "argument.id.invalid");

    EXPECT_FALSE(parseParticle("minecraft:dust", &e));
    EXPECT_EQ(e.key, "particle.invalidOptions");

    auto dust = parseParticle("minecraft:dust{color:[1.0f,0.0f,0.0f],scale:1.0f}", &e);
    ASSERT_TRUE(dust);
    EXPECT_FALSE(dust->valueEqual);

    auto charge = parseParticle("minecraft:sculk_charge{roll:0.5f}", &e);
    ASSERT_TRUE(charge);
    EXPECT_TRUE(charge->valueEqual);

    EXPECT_FALSE(parseParticle("minecraft:dust{color:", &e));
    EXPECT_EQ(e.key.rfind("argument.nbt", 0) == 0 || e.key == "parsing.expected", true);
}

TEST(LichdecorArguments, Loot) {
    ArgError e;
    auto     l = parseLoot("gloamingforest:blocks/ominous_spawner_drops", knownTable, &e);
    ASSERT_TRUE(l);
    EXPECT_FALSE(l->inlineTable);
    EXPECT_FALSE(parseLoot("gloamingforest:no_such_table", knownTable, &e));
    EXPECT_EQ(e.text, "Failed to parse structure: Failed to get element ResourceKey[minecraft:loot_table / gloamingforest:no_such_table]");
    auto inl = parseLoot("{}", knownTable, &e);
    ASSERT_TRUE(inl);
    EXPECT_TRUE(inl->inlineTable);
    EXPECT_FALSE(parseLoot("NOT:valid", knownTable, &e));
    EXPECT_EQ(e.key, "argument.id.invalid");
}

TEST(LichdecorArguments, Snbt) {
    ArgError e;
    auto     k = snbtKeys("{a:1,b:{c:[1,2]},\"d e\":'x'}", &e);
    ASSERT_TRUE(k);
    EXPECT_EQ(k->size(), 3u);
    EXPECT_FALSE(snbtKeys("{a}", &e));
    EXPECT_FALSE(snbtKeys("{a:}", &e));
    EXPECT_EQ(e.key, "argument.nbt.expected.value");
}

TEST(LichdecorFence, Rules) {
    using S = FenceSide;
    EXPECT_TRUE(shouldBePost(S::None, S::None, S::None, S::None));        // lone
    EXPECT_FALSE(shouldBePost(S::Full, S::None, S::Full, S::None));       // straight N-S
    EXPECT_TRUE(shouldBePost(S::Full, S::None, S::None, S::None));        // end
    EXPECT_TRUE(shouldBePost(S::Full, S::Full, S::None, S::None));        // corner
    EXPECT_FALSE(shouldBePost(S::None, S::Full, S::None, S::Full));       // straight E-W
    EXPECT_TRUE(shouldBePost(S::Full, S::Full, S::Full, S::Full));        // cross
    EXPECT_EQ(fenceSide(true, true), S::Middle);
    EXPECT_EQ(fenceSide(false, true), S::Top);
    EXPECT_EQ(fenceSide(true, false), S::Bottom);
    EXPECT_EQ(fenceSide(false, false), S::Full);
    EXPECT_EQ(fenceSideOf("top"), S::Top);
    EXPECT_EQ(postStateOf("capped"), PostState::Capped);
}

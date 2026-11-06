// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain) gtests: the beardifier as TF 4.8 runs it on NeoForge (core/terrain/Beard.h):
// Beardifier#compute's Rigid arithmetic with the ground level delta, and the per-piece
// PieceBeardifierModifier answers of TF's piece types.
#include <gtest/gtest.h>

#include <cmath>

#include "core/landmark/Clearance.h"
#include "core/terrain/Beard.h"

namespace gf::tests::terrain {
namespace {

namespace beard = core::terrain::beard;
using beard::TerrainAdjustment;
using core::BlockBox;
using nlohmann::json;

// Beardifier#compute written out for one Rigid, independently of rigidDensity.
double javaRigid(BlockBox const& b, TerrainAdjustment a, int delta, int x, int y, int z) {
    int i1 = std::max(0, std::max(b.minX - x, x - b.maxX));
    int j1 = std::max(0, std::max(b.minZ - z, z - b.maxZ));
    int k1 = b.minY + delta;
    int l1 = y - k1;
    int i2 = 0;
    if (a == TerrainAdjustment::Bury || a == TerrainAdjustment::BeardThin) i2 = l1;
    else if (a == TerrainAdjustment::BeardBox) i2 = std::max(0, std::max(k1 - y, y - b.maxY));
    else if (a == TerrainAdjustment::Encapsulate) i2 = std::max(0, std::max(b.minY - y, y - b.maxY));
    switch (a) {
    case TerrainAdjustment::Bury:
        return core::landmark::buryContribution(i1, i2 / 2.0, j1);
    case TerrainAdjustment::BeardThin:
    case TerrainAdjustment::BeardBox:
        return core::landmark::beardContribution(i1, i2, j1, l1) * 0.8;
    case TerrainAdjustment::Encapsulate:
        return core::landmark::buryContribution(i1 / 2.0, i2 / 2.0, j1 / 2.0) * 0.8;
    default:
        return 0.0;
    }
}

TEST(TerrainBeard, RigidMatchesBeardifierArithmetic) {
    BlockBox box{0, 10, 0, 8, 20, 6};
    for (auto a : {TerrainAdjustment::Bury, TerrainAdjustment::BeardThin, TerrainAdjustment::BeardBox, TerrainAdjustment::Encapsulate})
        for (int delta : {0, 1, 3})
            for (int x = -14; x <= 22; x += 3)
                for (int y = -4; y <= 34; y += 2)
                    for (int z = -14; z <= 20; z += 4) {
                        double got = beard::rigidDensity({box, a, delta}, x, y, z), exp = javaRigid(box, a, delta, x, y, z);
                        ASSERT_EQ(got, exp) << static_cast<int>(a) << " delta " << delta << " at " << x << " " << y << " " << z;
                    }
    EXPECT_EQ(beard::rigidDensity({box, TerrainAdjustment::None, 3}, 4, 9, 3), 0.0);
    // the ground level delta moves the beard's ground up: under a BEARD_BOX piece with delta 3 the
    // ground at minY + 2 is still filled, with delta 0 it is the piece's own floor
    EXPECT_GT(beard::rigidDensity({box, TerrainAdjustment::BeardBox, 3}, 4, 12, 3), 0.0);
    EXPECT_EQ(beard::rigidDensity({box, TerrainAdjustment::BeardBox, 0}, 4, 12, 3), javaRigid(box, TerrainAdjustment::BeardBox, 0, 4, 12, 3));
    // the sum keeps no clamp (Beardifier has none, unlike TF's BoxDensityFunction)
    std::vector<beard::Rigid> many(8, beard::Rigid{box, TerrainAdjustment::Bury, 0});
    EXPECT_DOUBLE_EQ(beard::beardifierDensity(many, 4, 10, 3), 8.0);
}

TEST(TerrainBeard, IsCloseToChunk) {
    EXPECT_TRUE(beard::isCloseToChunk({-12, 0, 0, -12, 5, 0}, 0, 0));
    EXPECT_FALSE(beard::isCloseToChunk({-13, 0, 0, -13, 5, 0}, 0, 0));
    EXPECT_TRUE(beard::isCloseToChunk({27, 0, 27, 40, 5, 40}, 0, 0));
    EXPECT_FALSE(beard::isCloseToChunk({28, 0, 0, 40, 5, 0}, 0, 0));
}

TEST(TerrainBeard, TfPieceTypes) {
    BlockBox box{0, 0, 0, 10, 10, 10};
    auto     pb = [&](char const* id, json tag = json::object()) { return beard::tfPieceBeard(id, box, tag); };
    // TFStructureComponentOld: NONE (the labyrinth's maze, the dark tower's wings, hollow hills)
    for (char const* id : {"twilightforest:TFMMaze", "twilightforest:TFMMR", "twilightforest:TFDTWin", "twilightforest:TFDTMai", "twilightforest:TFHill", "twilightforest:TFITMai",
                           "twilightforest:TFFCMain", "twilightforest:TFSAC", "twilightforest:TFSUCo"}) {
        auto p = pb(id);
        ASSERT_TRUE(p) << id;
        EXPECT_TRUE(p->modifier) << id;
        EXPECT_EQ(p->adjustment, TerrainAdjustment::None) << id;
    }
    auto check = [&](char const* id, TerrainAdjustment a, int delta, json tag = json::object()) {
        auto p = pb(id, tag);
        ASSERT_TRUE(p) << id;
        EXPECT_TRUE(p->modifier) << id;
        EXPECT_EQ(p->adjustment, a) << id;
        EXPECT_EQ(p->groundLevelDelta, delta) << id;
        EXPECT_EQ(p->box, box) << id;
    };
    check("twilightforest:TFHedge", TerrainAdjustment::BeardBox, 0);
    check("twilightforest:TFDTEnt", TerrainAdjustment::BeardBox, 1); // a TowerWingComponent: delta 1
    check("twilightforest:TFLTMai", TerrainAdjustment::BeardBox, 1);
    check("twilightforest:TFNCTe", TerrainAdjustment::BeardBox, 3);
    check("twilightforest:TFNCHe", TerrainAdjustment::BeardBox, 3);
    check("twilightforest:TFSSH", TerrainAdjustment::Bury, 0);
    check("twilightforest:TFSShield", TerrainAdjustment::Bury, 0);
    check("twilightforest:TFTCMai", TerrainAdjustment::Bury, 0);
    check("twilightforest:TFTCGard", TerrainAdjustment::Bury, 0);
    check("twilightforest:TFFCDunR31", TerrainAdjustment::Bury, 0);
    check("twilightforest:TFFCDunEx", TerrainAdjustment::Bury, 0);
    check("twilightforest:TFLTCTBase", TerrainAdjustment::BeardBox, 1);
    check("twilightforest:TFLTFence", TerrainAdjustment::BeardBox, 2);
    check("twilightforest:TFLTCTSeg", TerrainAdjustment::None, 0);
    check("twilightforest:TFLTTBeard", TerrainAdjustment::BeardBox, 4, {{"gen_ground", 1}});
    check("twilightforest:TFLTTBeard", TerrainAdjustment::None, 4, {{"gen_ground", false}});
    check("twilightforest:TFLTPath", TerrainAdjustment::BeardBox, 0, {{"dirt_mix", true}});
    check("twilightforest:TFLTPath", TerrainAdjustment::None, 0);
    check("twilightforest:TFJigsawTemplate", TerrainAdjustment::BeardBox, 1, {{"terrain_adaptation", "BEARD_BOX"}, {"ground_offset", 1}});
    check("twilightforest:tfjigsawtemplate", TerrainAdjustment::None, 0);
    // plain pieces take the structure's adaptation
    for (char const* id : {"twilightforest:TFQuest1", "twilightforest:TFFallenTrunk", "twilightforest:TFHTTr", "twilightforest:TFNCMn", "twilightforest:TFNCWl", "twilightforest:TFCloud"}) {
        auto p = pb(id);
        ASSERT_TRUE(p) << id;
        EXPECT_FALSE(p->modifier) << id;
    }
    EXPECT_FALSE(pb("gf_test:anything"));
    EXPECT_FALSE(pb("twilightforest:not_a_piece"));
    EXPECT_FALSE(pb(""));
}

TEST(TerrainBeard, RigidOfFollowsForStructuresInChunk) {
    BlockBox box{0, 0, 0, 10, 10, 10}, beardBox{1, 2, 3, 4, 5, 6};
    // a modifier piece: its own box, adjustment and delta; nothing when NONE
    auto r = beard::rigidOf({true, TerrainAdjustment::BeardBox, beardBox, 3}, box, TerrainAdjustment::Bury);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->box, beardBox);
    EXPECT_EQ(r->adjustment, TerrainAdjustment::BeardBox);
    EXPECT_EQ(r->groundLevelDelta, 3);
    EXPECT_FALSE(beard::rigidOf({true, TerrainAdjustment::None, beardBox, 3}, box, TerrainAdjustment::Bury));
    // a plain piece: its bounding box with the structure's adaptation, delta 0
    r = beard::rigidOf({false, TerrainAdjustment::None, beardBox, 7}, box, TerrainAdjustment::BeardThin);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->box, box);
    EXPECT_EQ(r->adjustment, TerrainAdjustment::BeardThin);
    EXPECT_EQ(r->groundLevelDelta, 0);
    EXPECT_FALSE(beard::rigidOf({false, TerrainAdjustment::None, beardBox, 0}, box, TerrainAdjustment::None));
    EXPECT_EQ(beard::adjustmentOf("BEARD_THIN"), TerrainAdjustment::BeardThin);
    EXPECT_EQ(beard::adjustmentOf("encapsulate"), TerrainAdjustment::Encapsulate);
    EXPECT_EQ(beard::adjustmentOf("whatever"), TerrainAdjustment::None);
}

} // namespace
} // namespace gf::tests::terrain

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Tests of upstream's structure set placements (F6): LandmarkGridPlacement / AvoidLandmarkGridPlacement
// (upstream commit 9e40a757, LGPL-2.1-or-later) and the vanilla random spread they
// extend, against goldens of golden/grid_reference.js.
#include <gtest/gtest.h>

#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "core/landmark/Placement.h"

namespace {
#include "golden/GridGolden.inc"
} // namespace

using namespace gf::core::landmark;
using namespace gf::core::landmark::placement;

namespace {
Placement parse(char const* json) { return Placement::fromJson(nlohmann::json::parse(json)); }
} // namespace

TEST(LandmarkPlacement, ParsesTfStructureSets) {
    auto lich = parse(R"({"type":"gloamingforest:landmark_grid","structure_grid_lock":"gloamingforest:lich_tower"})");
    EXPECT_EQ(lich.kind, Kind::LandmarkGrid);
    EXPECT_EQ(lich.gridLock, "gloamingforest:lich_tower");
    auto camp = parse(R"({"type":"gloamingforest:avoid_landmark_grid","avoid_additional_structures":{"gloamingforest:fallen_trunk":2,"gloamingforest:hollow_tree":1},"salt":701432212,"separation":14,"spacing":18,"spread_type":"triangular"})");
    EXPECT_EQ(camp.kind, Kind::AvoidLandmarkGrid);
    EXPECT_EQ(camp.spacing, 18);
    EXPECT_EQ(camp.separation, 14);
    EXPECT_EQ(camp.salt, 701432212);
    EXPECT_EQ(camp.spread, Spread::Triangular);
    ASSERT_EQ(camp.avoidAdditional.size(), 2u);
    auto trunk = parse(R"({"type":"gloamingforest:avoid_landmark_grid","exclusion_zone":{"chunk_count":1,"other_set":"gloamingforest:hollow_tree"},"frequency":0.8,"salt":1275623845,"separation":5,"spacing":7,"spread_type":"triangular"})");
    ASSERT_TRUE(trunk.exclusionZone.has_value());
    EXPECT_EQ(trunk.exclusionZone->otherSet, "gloamingforest:hollow_tree");
    EXPECT_EQ(trunk.exclusionZone->chunkCount, 1);
    EXPECT_FLOAT_EQ(trunk.frequency, 0.8f);
    EXPECT_EQ(parse(R"({"type":"minecraft:concentric_rings"})").kind, Kind::Unknown);
}

TEST(LandmarkPlacement, PotentialChunkMatchesGoldens) {
    for (auto const& r : kSpreadRows) {
        Placement p;
        p.kind       = Kind::RandomSpread;
        p.spacing    = r.spacing;
        p.separation = r.separation;
        p.spread     = r.triangular ? Spread::Triangular : Spread::Linear;
        p.salt       = r.salt;
        auto c       = potentialStructureChunk(p, r.seed, r.cx, r.cz);
        EXPECT_EQ(c.x, r.px) << "salt " << r.salt << " seed " << r.seed << " chunk " << r.cx << "," << r.cz;
        EXPECT_EQ(c.z, r.pz) << "salt " << r.salt << " seed " << r.seed << " chunk " << r.cx << "," << r.cz;
        // the frequency reducer (default method) at that chunk
        p.frequency = 0.8f;
        EXPECT_EQ(applyAdditionalChunkRestrictions(p, r.seed, r.px, r.pz), r.freq80);
        p.frequency = 0.5f;
        EXPECT_EQ(applyAdditionalChunkRestrictions(p, r.seed, r.px, r.pz), r.freq50);
    }
}

TEST(LandmarkPlacement, LandmarkGridOnlyAtCentersAndByLock) {
    auto any  = parse(R"({"type":"gloamingforest:landmark_grid"})");
    auto lich = parse(R"({"type":"gloamingforest:landmark_grid","structure_grid_lock":"gloamingforest:lich_tower"})");
    long long seed = 20260521LL;
    int       centers = 0, liches = 0;
    for (int cx = -64; cx <= 64; ++cx) {
        for (int cz = -64; cz <= 64; ++cz) {
            bool c = chunkHasCenter(cx, cz);
            EXPECT_EQ(isPlacementChunk(any, seed, cx, cz, {}), c);
            centers += c;
            bool l = isPlacementChunk(lich, seed, cx, cz, {});
            if (l) {
                EXPECT_TRUE(c);
                EXPECT_EQ(pickVariety(seed, cx, cz), "gloamingforest:lich_tower");
                ++liches;
            }
        }
    }
    EXPECT_GT(centers, 0);
    EXPECT_LE(liches, centers);
    // the variety callback replaces the seed pick
    auto alwaysLich = [](int, int) { return std::string("gloamingforest:lich_tower"); };
    EXPECT_TRUE(isPlacementChunk(lich, seed, 0, 0, alwaysLich));
}

TEST(LandmarkPlacement, AvoidLandmarkGridKeeps80BlocksFromCenters) {
    auto hollow = parse(R"({"type":"gloamingforest:avoid_landmark_grid","frequency":0.5,"salt":34481210,"separation":5,"spacing":7,"spread_type":"triangular"})");
    long long seed = 20260521LL;
    int       placed = 0;
    for (int cx = -100; cx <= 100; ++cx) {
        for (int cz = -100; cz <= 100; ++cz) {
            if (!isPlacementChunk(hollow, seed, cx, cz, {})) continue;
            ++placed;
            auto pot = potentialStructureChunk(hollow, seed, cx, cz);
            EXPECT_EQ(pot, (ChunkXZ{cx, cz}));
            auto c = nearestCenter(cx, cz);
            EXPECT_TRUE(std::abs(c.x - cx * 16) >= 80 || std::abs(c.z - cz * 16) >= 80);
        }
    }
    EXPECT_GT(placed, 0);
}

TEST(LandmarkPlacement, ExclusionZoneAndAvoidedSets) {
    auto hollow = parse(R"({"type":"gloamingforest:avoid_landmark_grid","frequency":0.5,"salt":34481210,"separation":5,"spacing":7,"spread_type":"triangular"})");
    auto trunk  = parse(R"({"type":"gloamingforest:avoid_landmark_grid","exclusion_zone":{"chunk_count":1,"other_set":"gloamingforest:hollow_tree"},"frequency":0.8,"salt":1275623845,"separation":5,"spacing":7,"spread_type":"triangular"})");
    auto camp   = parse(R"({"type":"gloamingforest:avoid_landmark_grid","avoid_additional_structures":{"gloamingforest:fallen_trunk":2,"gloamingforest:hollow_tree":1},"salt":701432212,"separation":14,"spacing":18,"spread_type":"triangular"})");
    std::map<std::string, Placement> sets{{"gloamingforest:hollow_tree", hollow}, {"gloamingforest:fallen_trunk", trunk}};
    SetLookup                        lookup = [&](std::string const& id) -> Placement const* {
        auto it = sets.find(id);
        return it == sets.end() ? nullptr : &it->second;
    };
    long long seed = 99LL;
    int       trunks = 0, camps = 0;
    for (int cx = -150; cx <= 150; ++cx) {
        for (int cz = -150; cz <= 150; ++cz) {
            if (isStructureChunk(trunk, seed, cx, cz, {}, lookup)) {
                ++trunks;
                EXPECT_FALSE(hasStructureChunkInRange(hollow, seed, cx, cz, 1, {}, lookup)) << cx << "," << cz;
            }
            if (isStructureChunk(camp, seed, cx, cz, {}, lookup)) {
                ++camps;
                EXPECT_FALSE(hasStructureChunkInRange(trunk, seed, cx, cz, 2, {}, lookup));
                EXPECT_FALSE(hasStructureChunkInRange(hollow, seed, cx, cz, 1, {}, lookup));
            }
        }
    }
    EXPECT_GT(trunks, 0);
    EXPECT_GT(camps, 0);
}

TEST(LandmarkPlacement, LocatePosIsTheChunkCorner) {
    auto p = parse(R"({"type":"gloamingforest:landmark_grid","locate_offset":[3,0,-2]})");
    EXPECT_EQ(locatePos(p, 2, -1), (std::pair<int, int>{35, -18}));
}

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/ThornFeature.java
//   src/main/java/twilightforest/world/components/feature/config/ThornsConfig.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the feature type twilightforest:thorns (the thornlands' brown thorn bushes). The
// configured feature (max_spread 7, chance_of_branch 3, chance_of_leaf 3, chance_leaf_is_rose 50) and
// the placed feature (U2's chunk_blanketing, biome lock thornlands) are F1's imported JSON.
//
// Java marks every thorn for post-processing (ProtoChunk: updateShape against its six neighbours when
// the chunk is finished); Bedrock's generation has no such pass, so the connections of the thorns
// placed are computed from their neighbours in the region when the feature is done (ThornsBlock:
// an air neighbour keeps the connection off).
#include <cmath>
#include <cstdlib>
#include <vector>

#include "mc/world/level/block/Block.h"

#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "feature/finalcastle/BlocksCommon.h"
#include "game/Blocks.h"

namespace gf::feature::finalcastle::blocks {

namespace kit = gf::features::kit;
using gf::features::FeatureContext;
using gf::worldgen::Heightmap;

namespace {

/// ThornsConfig.
struct ThornsConfig {
    int maxSpread        = 7;
    int chanceOfBranch   = 3;
    int chanceOfLeaf     = 3;
    int chanceLeafIsRose = 50;
};

/// WorldUtil.getGeneratorSeaLevel of the Twilight Forest's generator.
constexpr int kSeaLevel = 0;

struct Placer {
    worldgen::Region&            world;
    core::jrand::RandomSource&   rand;
    ThornsConfig                 config;
    std::vector<std::pair<BlockPos, int>> axes; ///< the thorns placed and their axes

    bool inside(BlockPos const& p) const { return world.isInside(p.x, p.z) && p.y >= world.minY() && p.y < world.maxY(); }

    bool isEmpty(BlockPos const& p) const { return inside(p) && kit::isAir(world.getBlock(p)); }

    /// canPlaceThorns: air or #minecraft:leaves.
    bool canPlaceThorns(BlockPos const& p) const {
        if (!inside(p)) return false;
        Block const& b = world.getBlock(p);
        return kit::isAir(b) || kit::hasTag(b, "minecraft:leaves");
    }

    /// checkIsUnderCloud: the column stays below sea level + 150 (the giants' cloud layer).
    bool underCloud(BlockPos const& pos, BlockPos const& dPos) const {
        if (!world.isInside(pos.x, pos.z)) return false; // world.hasChunk
        if (!world.isInside(dPos.x, dPos.z)) return dPos.y <= kSeaLevel + 150;
        return std::max(dPos.y, world.height(Heightmap::MotionBlockingNoLeaves, dPos.x, dPos.z)) <= kSeaLevel + 150;
    }

    void setThorn(BlockPos const& p, int axis) {
        Block const* b = kit::javaBlock(id::BrownThorns, {{"axis", axisName(axis)}});
        if (b && world.setBlock(p, *b, 2)) {
            axes.emplace_back(p, axis);
        }
    }

    void setLeaf(BlockPos const& p) {
        if (Block const* b = kit::javaBlock(id::ThornLeaves, {{"distance", 1}})) world.setBlock(p, *b, 3);
    }

    void setRose(BlockPos const& p, int dir) {
        if (Block const* b = kit::javaBlock(id::ThornRose, {{"facing", dirName(dir)}})) world.setBlock(p, *b, 3);
    }

    void placeThorns(BlockPos const& pos, int length, int dir, int maxLength, BlockPos const& oPos, bool avoidGiantCloud) {
        bool complete = false;
        for (int i = 0; i < length; ++i) {
            BlockPos dPos = offset(pos, dir, i);
            // makes it avoid the troll clouds
            if (avoidGiantCloud && !underCloud(pos, dPos)) break;
            if (!(std::abs(dPos.x - oPos.x) < config.maxSpread && std::abs(dPos.z - oPos.z) < config.maxSpread && canPlaceThorns(dPos))) break;
            setThorn(dPos, axisOf(dir));
            // did we make it to the end?
            if (i == length - 1) {
                complete = true;
                // maybe a leaf? or a rose?
                if (rand.nextInt(config.chanceOfLeaf) == 0 && isEmpty(offset(dPos, dir))) {
                    if (rand.nextInt(config.chanceLeafIsRose) > 0) setLeaf(offset(dPos, dir));
                    else setRose(offset(dPos, dir), dir);
                }
            }
        }
        // add another off the end
        if (complete && maxLength > 1) {
            int      nextDir    = rand.nextInt(6);
            BlockPos nextPos    = offset(offset(pos, dir, length - 1), nextDir);
            int      nextLength = 1 + rand.nextInt(maxLength);
            placeThorns(nextPos, nextLength, nextDir, maxLength - 1, oPos, false);
        }
        // maybe another branch off the middle
        if (complete && length > 3 && rand.nextInt(config.chanceOfBranch) == 0) {
            int      middle     = rand.nextInt(length);
            int      nextDir    = rand.nextInt(6);
            BlockPos nextPos    = offset(offset(pos, dir, middle), nextDir);
            int      nextLength = 1 + rand.nextInt(maxLength);
            placeThorns(nextPos, nextLength, nextDir, maxLength - 1, oPos, false);
        }
        // maybe a leaf
        if (complete && length > 3 && rand.nextInt(config.chanceOfLeaf) == 0) {
            int      middle  = rand.nextInt(length);
            int      nextDir = rand.nextInt(6);
            BlockPos nextPos = offset(offset(pos, dir, middle), nextDir);
            if (isEmpty(nextPos)) setLeaf(nextPos);
        }
    }

    /// The post-processing of the thorns placed: ThornsBlock#updateShape against each neighbour.
    void connect() {
        for (auto const& [p, axis] : axes) {
            if (!inside(p) || game::blockId(world.getBlock(p)) != id::BrownThorns) continue;
            Json props{{"axis", axisName(axis)}};
            for (int d = 0; d < 6; ++d) {
                BlockPos n  = offset(p, d);
                bool     on = false;
                if (inside(n)) {
                    Block const& nb = world.getBlock(n);
                    on              = !kit::isAir(nb) && thornsConnectTo(nb);
                }
                props[dirName(d)] = on;
            }
            if (Block const* b = kit::javaBlock(id::BrownThorns, props)) world.setBlock(p, *b, 2);
        }
    }
};

bool placeThornFeature(FeatureContext const& ctx) {
    ThornsConfig cfg;
    cfg.maxSpread        = ctx.config.value("max_spread", cfg.maxSpread);
    cfg.chanceOfBranch   = std::max(1, ctx.config.value("chance_of_branch", cfg.chanceOfBranch));
    cfg.chanceOfLeaf     = std::max(1, ctx.config.value("chance_of_leaf", cfg.chanceOfLeaf));
    cfg.chanceLeafIsRose = std::max(1, ctx.config.value("chance_leaf_is_rose", cfg.chanceLeafIsRose));
    Placer p{ctx.region, ctx.random, cfg, {}};
    // make a 3-5 long stack going up
    int nextLength = 2 + ctx.random.nextInt(4);
    int maxLength  = 2 + ctx.random.nextInt(4) + ctx.random.nextInt(4) + ctx.random.nextInt(4);
    p.placeThorns(ctx.origin, nextLength, Up, maxLength, ctx.origin, true);
    p.connect();
    return true;
}

} // namespace

void installThornFeature() { gf::features::registerType("twilightforest:thorns", placeThornFeature); }

} // namespace gf::feature::finalcastle::blocks

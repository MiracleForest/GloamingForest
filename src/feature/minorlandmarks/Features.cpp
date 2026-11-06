// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/feature/BlockSpikeFeature.java
//   src/main/java/twilightforest/world/components/feature/DarkForestFeature.java
//   src/main/java/twilightforest/world/components/feature/FallenHollowLogFeature.java
//   src/main/java/twilightforest/world/components/feature/SmallFallenLogFeature.java
//   src/main/java/twilightforest/world/components/feature/FoundationFeature.java
//   src/main/java/twilightforest/world/components/feature/LampostFeature.java
//   src/main/java/twilightforest/world/components/feature/MonolithFeature.java
//   src/main/java/twilightforest/world/components/feature/CheckAbovePatchFeature.java
//   src/main/java/twilightforest/world/components/feature/TFSmallLakeFeature.java
//   src/main/java/twilightforest/world/components/feature/WebFeature.java
//   src/main/java/twilightforest/world/components/feature/WoodRootFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/HollowStumpFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/HollowTreeFeature.java
//   src/main/java/twilightforest/world/components/feature/trees/TFTreeFeature.java
//   src/main/java/twilightforest/util/features/FeatureUtil.java
//   src/main/java/twilightforest/util/features/FeaturePlacers.java
//   src/main/java/twilightforest/util/features/FeatureLogic.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U18's feature types (F5's features contract): block_spike, dark_forest_placer, fallen_hollow_log,
// fallen_small_log, foundation, hollow_stump, lampposts, monolith, mycelium_blob, small_lake, webs,
// wood_roots. The configured / placed features are F1's imported JSON.
#include "feature/minorlandmarks/Internal.h"

#include <algorithm>
#include <functional>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"

#include "core/jrand/Mth.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/structure/Placement.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/features/Features.h"
#include "mod/Log.h"

namespace gf::feature::minorlandmarks {

namespace kit = gf::features::kit;
using gf::features::FeatureContext;

namespace {

BlockPos off(BlockPos const& p, int dx, int dy, int dz) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }

/// FeatureLogic#isBlockNotOk.
bool isBlockNotOk(Block const& b) {
    auto const& id = st::blocks::id(b);
    return st::blocks::isLiquid(b) || id == "minecraft:bedrock" || id.starts_with("twilightforest:giant_") || st::blocks::inTag(b, "twilightforest:clouds")
        || id == "twilightforest:hardened_dark_leaves";
}

/// BlockState#isSolidRender: an opaque full cube.
bool isSolidRender(Block const& b) { return !st::blocks::isAir(b) && kit::blocksMotion(b) && !st::blocks::isPartial(b) && !kit::isLeaves(b) && !st::blocks::isLiquid(b); }

/// IS_REPLACEABLE_AIR: canBeReplaced || isAir.
bool replaceableAir(Region& w, BlockPos const& p) {
    auto const& b = w.getBlock(p);
    return st::blocks::canBeReplaced(b) || st::blocks::isAir(b);
}

bool hasEmptyNeighborExceptBelow(Region& w, BlockPos const& p) {
    return replaceableAir(w, off(p, 0, 1, 0)) || replaceableAir(w, off(p, 0, 0, -1)) || replaceableAir(w, off(p, 0, 0, 1)) || replaceableAir(w, off(p, -1, 0, 0))
        || replaceableAir(w, off(p, 1, 0, 0));
}

bool hasSolidNeighbor(Region& w, BlockPos const& p) {
    return !(replaceableAir(w, off(p, 0, -1, 0)) && replaceableAir(w, off(p, 0, 0, -1)) && replaceableAir(w, off(p, 0, 0, 1)) && replaceableAir(w, off(p, -1, 0, 0))
             && replaceableAir(w, off(p, 1, 0, 0)) && replaceableAir(w, off(p, 0, 1, 0)));
}

bool canRootGrowIn(Region& w, BlockPos const& p) {
    if (replaceableAir(w, p)) return hasSolidNeighbor(w, p);
    return st::blocks::worldGenReplaceable(w.getBlock(p));
}

/// FeatureUtil#anyBelowMatch(pos, depth, predicate): pos.below(depth) .. pos.
bool anyBelowMatch(BlockPos const& pos, int depth, std::function<bool(BlockPos const&)> const& predicate) {
    for (int cy = 0; cy < depth + 1; cy++)
        if (predicate(BlockPos{pos.x, pos.y - depth + cy, pos.z})) return true;
    return false;
}

/// TF's RootPlacer: a placer and its root penetrability.
struct RootPlacer {
    std::function<void(BlockPos const&, Block const&)> place;
    int                                                penetrability = 1;
};

bool rootShouldSkip(Region& w, BlockPos const& p) { return st::blocks::inTag(w.getBlock(p), "twilightforest:root_trace_skip"); }

bool placeIfValidRootPos(Region& w, RootPlacer const& placer, RandomSource& random, BlockPos const& pos, Json const& provider) {
    if (!anyBelowMatch(pos, placer.penetrability - 1, [&](BlockPos const& b) { return !canRootGrowIn(w, b); })) {
        if (auto const* s = sampleProvider(provider, random, pos)) placer.place(pos, *s);
        return true;
    }
    return false;
}

/// FeaturePlacers#traceRoot over the tracer's positions from index `from`.
void traceRoot(Region& w, RootPlacer const& placer, RandomSource& random, Json const& dirtRoot, std::vector<cml::Pos> const& tracer, std::size_t from = 0) {
    for (std::size_t i = from; i < tracer.size(); i++) {
        BlockPos rootPos = toBlockPos(tracer[i]);
        if (anyBelowMatch(rootPos, placer.penetrability - 1, [&](BlockPos const& b) { return rootShouldSkip(w, b); })) return;
        if (!placeIfValidRootPos(w, placer, random, rootPos, dirtRoot)) return;
    }
}

/// FeaturePlacers#traceExposedRoot.
void traceExposedRoot(Region& w, RootPlacer const& placer, RandomSource& random, Json const& exposedRoot, Json const& dirtRoot, std::vector<cml::Pos> const& tracer) {
    for (std::size_t i = 0; i < tracer.size(); i++) {
        BlockPos exposedPos = toBlockPos(tracer[i]);
        if (rootShouldSkip(w, exposedPos)) continue;
        if (hasEmptyNeighborExceptBelow(w, exposedPos)) {
            if (anyBelowMatch(exposedPos, placer.penetrability - 1, [&](BlockPos const& b) {
                    Block const& state = w.getBlock(b);
                    if (st::blocks::worldGenReplaceable(state)) return false;
                    Block const* root = sampleProvider(exposedRoot, random, exposedPos);
                    return &state != root;
                }))
                return;
            if (auto const* s = sampleProvider(exposedRoot, random, exposedPos)) placer.place(exposedPos, *s);
        } else {
            if (placeIfValidRootPos(w, placer, random, exposedPos, dirtRoot)) traceRoot(w, placer, random, dirtRoot, tracer, i + 1);
            return;
        }
    }
}

Block const* state(Json const& j) { return st::blocks::fromJavaState(j); }

// ---- block_spike -------------------------------------------------------------------------------------

bool makeSpike(Region& level, BlockPos const& startPos, int length, int dY, RandomSource& random, bool hang) {
    int diameter = static_cast<int>(static_cast<float>(length) / 4.5F);
    if (!hang) {
        BlockPos     below      = off(startPos, 0, -2, 0);
        Block const& belowState = level.getBlock(below);
        if (!st::blocks::inTag(belowState, "twilightforest:supports_stalagmites")
            && (!st::blocks::worldGenReplaceable(belowState) || !(kit::blocksMotion(belowState) && !st::blocks::isPartial(belowState)) || isBlockNotOk(belowState)))
            return false;
    }
    auto const& stone = st::blocks::java("minecraft:stone");
    for (int dx = -diameter; dx <= diameter; dx++)
        for (int dz = -diameter; dz <= diameter; dz++) {
            int absx = std::abs(dx), absz = std::abs(dz);
            int dist = static_cast<int>(static_cast<float>(std::max(absx, absz)) + static_cast<float>(std::min(absx, absz)) * 0.5F);
            int spikeLength = dist <= 0 ? length : random.nextInt(static_cast<int>(static_cast<float>(length) / (static_cast<float>(dist) + 0.25F)));
            for (int i = -1; i < spikeLength; i++) {
                BlockPos placement = off(startPos, dx, i * dY, dz);
                if (st::blocks::worldGenReplaceable(level.getBlock(placement))
                    && (dY > 0 || placement.y < level.height(worldgen::Heightmap::MotionBlockingNoLeaves, placement.x, placement.z) - 2))
                    level.setBlock(placement, stone, 3);
            }
        }
    return true;
}

} // namespace

bool placeStoneSpike(Region& level, BlockPos const& startPos, RandomSource& random, bool hang) {
    // STONE_STALACTITE = Stalactite(STONE, 0.25F, 11, 1)
    int maxInclusive = 11;
    int minInclusive = static_cast<int>(static_cast<float>(maxInclusive) * 0.25F);
    int length       = random.nextInt(maxInclusive - minInclusive + 1) + minInclusive; // Mth.randomBetweenInclusive
    int lengthMin = minInclusive, lengthMax = maxInclusive, clearance = 4;
    if (lengthMax < std::max(lengthMin, 1)) return false;
    BlockPos moving       = startPos;
    int      clearedLength = 0;
    int      dY           = hang ? -1 : 1;
    for (int i = 0; i < length; i++) {
        clearedLength = i;
        if (st::blocks::worldGenReplaceable(level.getBlock(moving))) break;
        moving.y += dY;
    }
    moving.y += dY;
    int remainingScanLength = length - clearedLength + clearance;
    int finalLength         = clearedLength - clearance;
    for (int i = 0; i < remainingScanLength; i++) {
        finalLength = clearedLength + i;
        if (!st::blocks::worldGenReplaceable(level.getBlock(moving))) break;
        moving.y += dY;
    }
    finalLength = std::min(length, finalLength);
    if (finalLength < lengthMin || finalLength > lengthMax) return false;
    return makeSpike(level, startPos, finalLength, dY, random, hang);
}

bool isAreaSuitable(Region& world, BlockPos const& pos, int xWidth, int height, int zWidth, bool underwaterAllowed) {
    for (int cx = 0; cx < xWidth; cx++)
        for (int cz = 0; cz < zWidth; cz++) {
            BlockPos p = off(pos, cx, 0, cz);
            if (!world.isInside(p.x, p.z)) return false;
            Block const& below = world.getBlock(off(p, 0, -1, 0));
            if (!isSolidRender(below) || isBlockNotOk(below)) {
                if (underwaterAllowed && st::blocks::isLiquid(below)) continue;
                return false;
            }
            for (int cy = 0; cy < height; cy++) {
                Block const& b = world.getBlock(off(p, 0, cy, 0));
                if (!st::blocks::isAir(b) && !st::blocks::canBeReplaced(b)) {
                    if (underwaterAllowed && st::blocks::isLiquid(b)) continue;
                    return false;
                }
            }
        }
    return true;
}

namespace {

// ---- dark_forest_placer ------------------------------------------------------------------------------------

bool darkForest(FeatureContext const& ctx) {
    auto&    reader   = ctx.region;
    BlockPos pos      = ctx.origin;
    bool     foundDirt = false;
    if (pos.y <= 40) {
        // the chunk generator's sea level (the Twilight Forest's 0 for an in-memory scratch region)
        int seaLevel = reader.dimension() < 0 ? 0 : gf::landmark::ext::seaLevel(reader.dimension());
        for (int dy = pos.y; dy >= seaLevel; dy--) {
            Block const& s = reader.getBlock(BlockPos{pos.x, dy - 1, pos.z});
            if (st::blocks::inTag(s, "minecraft:dirt") && st::blocks::canBeReplaced(reader.getBlock(pos))) {
                foundDirt = true;
                pos       = BlockPos{pos.x, dy, pos.z};
                break;
            } else if (st::blocks::inTag(s, "minecraft:base_stone_overworld") || st::blocks::inTag(s, "minecraft:sand")) {
                break;
            }
        }
    }
    if (!foundDirt) return false;
    int tries = ctx.config.value("tries", 128), j = ctx.config.value("xz_spread", 7) + 1, k = ctx.config.value("y_spread", 3) + 1;
    int placed = 0;
    for (int l = 0; l < tries; ++l) {
        int dx = ctx.random.nextInt(j);
        dx -= ctx.random.nextInt(j);
        int dy = ctx.random.nextInt(k);
        dy -= ctx.random.nextInt(k);
        int dz = ctx.random.nextInt(j);
        dz -= ctx.random.nextInt(j);
        if (kit::placePlaced(ctx.config["feature"], reader, off(pos, dx, dy, dz), ctx.random)) ++placed;
    }
    return placed > 0;
}

// ---- fallen_hollow_log ------------------------------------------------------------------------------------

struct HollowLogBlocks {
    Block const* moss;
    Block const* leaves;
    Block const* logZ;
    Block const* logX;
    Block const* grass;
    Block const* firefly;
};
HollowLogBlocks hollowLogBlocks() {
    return {st::blocks::fromJava("twilightforest:moss_patch"),
            st::blocks::fromJava("twilightforest:twilight_oak_leaves", Json{{"persistent", "true"}}),
            st::blocks::fromJava("twilightforest:twilight_oak_log", Json{{"axis", "z"}}),
            st::blocks::fromJava("twilightforest:twilight_oak_log", Json{{"axis", "x"}}),
            st::blocks::fromJava("minecraft:grass_block"),
            st::blocks::fromJava("twilightforest:firefly")};
}

bool fallenHollowLog(FeatureContext const& ctx) {
    auto&    world = ctx.region;
    BlockPos pos   = ctx.origin;
    auto&    rand  = ctx.random;
    auto     b     = hollowLogBlocks();
    auto     set   = [&](BlockPos const& p, Block const* s) {
        if (s) world.setBlock(p, *s, 3);
    };
    bool alongZ = rand.nextBoolean();
    // makeLog4Z / makeLog4X: the same shape with x and z swapped (a = along, c = across)
    auto at = [&](int across, int y, int along) { return alongZ ? off(pos, across, y, along) : off(pos, along, y, across); };
    Block const* log = alongZ ? b.logZ : b.logX;
    if (alongZ ? !isAreaSuitable(world, pos, 4, 3, 9) : !isAreaSuitable(world, pos, 9, 3, 4)) return false;
    auto negJaggy = [&](int length, int across, int dy) {
        for (int d = -length; d < 0; d++) set(at(across, dy, d + 3), log);
    };
    auto posJaggy = [&](int length, int across, int dy) {
        for (int d = 0; d < length; d++) set(at(across, dy, d + 7), log);
    };
    negJaggy(rand.nextInt(3), 0, 0);
    negJaggy(rand.nextInt(3), 3, 0);
    negJaggy(rand.nextInt(3), 0, 1);
    negJaggy(rand.nextInt(3), 3, 1);
    negJaggy(rand.nextInt(3), 1, 2);
    negJaggy(rand.nextInt(3), 2, 2);
    posJaggy(rand.nextInt(3), 0, 0);
    posJaggy(rand.nextInt(3), 3, 0);
    posJaggy(rand.nextInt(3), 0, 1);
    posJaggy(rand.nextInt(3), 3, 1);
    posJaggy(rand.nextInt(3), 1, 2);
    posJaggy(rand.nextInt(3), 2, 2);
    for (int d = 0; d < 4; d++) {
        for (int across : {1, 2}) {
            if (rand.nextBoolean()) {
                set(at(across, -1, d + 3), log);
                if (rand.nextBoolean()) set(at(across, 0, d + 3), b.moss);
            } else {
                set(at(across, -1, d + 3), b.grass);
                set(at(across, 0, d + 3), b.moss);
            }
        }
        set(at(0, 0, d + 3), log);
        set(at(3, 0, d + 3), log);
        set(at(0, 1, d + 3), log);
        set(at(3, 1, d + 3), log);
        set(at(1, 2, d + 3), log);
        set(at(2, 2, d + 3), log);
        if (rand.nextBoolean()) set(at(1, 3, d + 3), b.moss);
        if (rand.nextBoolean()) set(at(2, 3, d + 3), b.moss);
    }
    int  offAlong = rand.nextInt(3) + 2;
    bool plus     = rand.nextBoolean();
    for (int d = 0; d < 3; d++) {
        if (rand.nextBoolean()) {
            set(at(plus ? 3 : 0, 2, d + offAlong), b.leaves);
            if (rand.nextBoolean()) set(at(plus ? 3 : 0, 3, d + offAlong), b.leaves);
            if (rand.nextBoolean()) set(at(plus ? 4 : -1, 2, d + offAlong), b.leaves);
        }
    }
    set(at(plus ? 0 : 3, 2, rand.nextInt(4) + 3), b.firefly);
    return true;
}

// ---- fallen_small_log ------------------------------------------------------------------------------------

bool smallFallenLog(FeatureContext const& ctx) {
    auto&    world = ctx.region;
    BlockPos pos   = ctx.origin;
    auto&    rand  = ctx.random;
    bool     shouldMakeAllHollow = rand.nextBoolean();
    bool     goingX              = rand.nextBoolean();
    int      length              = rand.nextInt(4) + 3;
    if (goingX ? !isAreaSuitable(world, pos, length, 2, 2, true) : !isAreaSuitable(world, pos, 2, 2, length, true)) return false;
    Block const* logState    = state(ctx.config.value("normal", Json::object()));
    Block const* hollowState = state(ctx.config.value("hollow", Json::object()));
    if (!logState) return false;
    if (hollowState && st::blocks::isAir(*hollowState)) hollowState = nullptr;
    if (rand.nextInt(5) == 0 && st::blocks::isLiquid(world.getBlock(pos))) {
        BlockPos floating = pos;
        for (int i = 0; i < 10; i++) {
            if (st::blocks::isAir(world.getBlock(off(floating, 0, 1, 0)))) {
                pos = floating;
                break;
            }
            floating.y += 1;
        }
    }
    auto variant = [&]() -> char const* {
        Block const& here = world.getBlock(pos);
        if (st::blocks::isWater(here)) return "waterlogged";
        if (st::blocks::inTag(here, "minecraft:snow")) return "snow";
        if (rand.nextInt(5) == 0) return "moss_and_grass";
        if (rand.nextInt(3) == 0) return "moss";
        return "empty";
    };
    auto mossOrSeagrass = [&](BlockPos const& p) -> Block const* {
        if (st::blocks::inTag(world.getBlock(off(p, 0, -2, 0)), "minecraft:snow")) return &st::blocks::air();
        return st::blocks::is(world.getBlock(p), "minecraft:water") ? st::blocks::fromJava("minecraft:seagrass") : st::blocks::fromJava("twilightforest:moss_patch");
    };
    auto hollowOrNormal = [&](Block const* hollow, Block const& normal) -> Block const& {
        bool roll = shouldMakeAllHollow || st::ext::worldRandom(world).nextInt(3) == 0;
        return (roll && hollow && !st::blocks::isAir(*hollow)) ? *hollow : normal;
    };
    auto set = [&](BlockPos const& p, Block const* s) {
        if (s) world.setBlock(p, *s, 3);
    };
    char const*  axis   = goingX ? "x" : "z";
    Block const& log    = st::blocks::withJava(*logState, Json{{"axis", axis}});
    Block const* hollow = hollowState ? &st::blocks::withJava(*hollowState, Json{{"axis", axis}, {"variant", variant()}}) : nullptr;
    Block const& branch = st::blocks::withJava(log, Json{{"axis", goingX ? "z" : "x"}});
    for (int l = 0; l < length; l++) {
        BlockPos p = goingX ? off(pos, l, 0, 1) : off(pos, 1, 0, l);
        world.setBlock(p, hollowOrNormal(hollow, log), 3);
        if (rand.nextInt(3) > 0) set(off(p, 0, 1, 0), mossOrSeagrass(off(p, 0, 1, 0)));
    }
    if (rand.nextInt(3) > 0) {
        int bx, bz;
        if (goingX) {
            bx = rand.nextInt(length);
            bz = rand.nextBoolean() ? 2 : 0;
        } else {
            bx = rand.nextBoolean() ? 2 : 0;
            bz = rand.nextInt(length);
        }
        world.setBlock(off(pos, bx, 0, bz), branch, 3);
        if (rand.nextBoolean()) set(off(pos, bx, 1, bz), mossOrSeagrass(off(pos, bx, 1, bz)));
    }
    return true;
}

// ---- foundation -------------------------------------------------------------------------------------------

st::Rotation toRotation(cml::Rot r) {
    switch (r) {
    case cml::Rot::Clockwise90: return st::Rotation::Clockwise90;
    case cml::Rot::Clockwise180: return st::Rotation::Clockwise180;
    case cml::Rot::Counterclockwise90: return st::Rotation::Counterclockwise90;
    default: return st::Rotation::None;
    }
}

void setAndUpdate(Region& world, RandomSource& rand, Json const& provider, BlockPos const& placeAt, st::Rotation rotation = st::Rotation::None) {
    Block const* s = sampleProvider(provider, rand, placeAt);
    if (!s) return;
    Block const& rotated = st::blocks::rotate(*s, rotation);
    bool water = st::liquids::waterloggable(rotated) && (st::liquids::waterSourceAt(world, placeAt) || st::liquids::waterSourceAt(world, off(placeAt, 0, 1, 0)));
    world.setBlock(placeAt, rotated, 3);
    if (water) st::liquids::setWaterlogged(world, placeAt, true);
}

Json const& rollDecay(RandomSource& rand, int decayRarity, Json const& main, Json const& decay) { return rand.nextInt(decayRarity + 1) >= 1 ? main : decay; }

int rollChestCoord(int width, RandomSource& rand) {
    if (width < 3) return rand.nextInt(std::max(0, width) + 1);
    return rand.nextInt(std::max(0, width - 1) + 1) + 1;
}

bool foundation(FeatureContext const& ctx) {
    auto&       world = ctx.region;
    BlockPos    pos   = ctx.origin;
    auto&       rand  = ctx.random;
    Json const& c     = ctx.config;
    int         xWidth = kit::sampleInt(c["wall_width"], rand);
    int         zWidth = kit::sampleInt(c["wall_width"], rand);
    if (!isAreaSuitable(world, off(pos, 1, 0, 1), xWidth - 1, 4, zWidth - 1)) return false;
    // generateFoundation
    for (int dX = 0; dX <= xWidth; dX++)
        for (int dZ = 0; dZ <= zWidth; dZ++) {
            auto wallRotation = cml::wallVolumeRotation(rand, dX, dZ, xWidth, zWidth);
            if (wallRotation) {
                int height = kit::sampleInt(c["wall_heights"], rand);
                for (int yBlock = 0; yBlock < height; yBlock++)
                    setAndUpdate(world, rand, rollDecay(rand, yBlock, c["wall_block"], c["decayed_wall_block"]), off(pos, dX, yBlock - 1, dZ), toRotation(*wallRotation));
                setAndUpdate(world, rand, rollDecay(rand, height, c["wall_top_block"], c["decayed_wall_top_block"]), off(pos, dX, height - 1, dZ), toRotation(*wallRotation));
            } else if (kit::sampleFloat(c["random_floor_chance"], rand) <= 0) {
                setAndUpdate(world, rand, c["floor"], off(pos, dX, -1, dZ));
            }
        }
    int basementDepth = kit::sampleInt(c["basement_height"], rand);
    if (basementDepth > 0) {
        BlockPos ceiling = off(pos, 1, -3, 1);
        int      xw = xWidth - 2, zw = zWidth - 2, depth = basementDepth;
        if (xw < 1 || zw < 1 || depth < 1) return true;
        int chestX = rollChestCoord(xw, rand);
        int chestZ = rollChestCoord(zw, rand);
        for (int dX = 0; dX <= xw; dX++)
            for (int dZ = 0; dZ <= zw; dZ++) {
                int corner = (dX == 0) + (dZ == 0) + (dX == xw) + (dZ == zw);
                for (int dY = 1 - depth; dY <= 0; dY++) {
                    BlockPos placeAt = off(ceiling, dX, dY, dZ);
                    world.setBlock(placeAt, st::blocks::air(), 3);
                    if (corner > 1) setAndUpdate(world, rand, c["basement_posts"], placeAt);
                }
                if ((dX == chestX && dZ == chestZ) || (corner == 0 && kit::sampleFloat(c["random_floor_chance"], rand) <= 0))
                    setAndUpdate(world, rand, c["floor"], off(ceiling, dX, -depth, dZ));
            }
        BlockPos     lootPos = off(ceiling, chestX, 1 - depth, chestZ);
        Json const&  lc      = c["loot_container"];
        std::string  id      = providerBlockId(lc);
        if (id.empty()) id = "minecraft:chest";
        std::optional<st::Orientation> facing;
        if (lc.contains("state") && lc["state"].contains("Properties")) {
            auto f = lc["state"]["Properties"].value("facing", std::string{});
            facing = st::blocks::parseDirection(f);
        }
        std::int64_t seed = core::mth::ladd(core::mth::lmul(world.seed(), lootPos.x), lootPos.y) ^ lootPos.z;
        (void)sampleProvider(lc, rand, lootPos); // lootContainer.getState(rand, pos)
        st::blockentities::container(world, lootPos, id, facing, c.value("loot_table", std::string("twilightforest:foundation_basement")), seed);
    }
    return true;
}

// ---- hollow_stump ---------------------------------------------------------------------------------------

bool hollowStump(FeatureContext const& ctx) {
    auto&       world  = ctx.region;
    BlockPos    pos    = ctx.origin;
    auto&       random = ctx.random;
    Json const& c      = ctx.config;
    bool        decoPlaced = false;
    RootPlacer  trunkPlacer{[&](BlockPos const& p, Block const& s) { world.setBlock(p, s, 19); }, 1};
    RootPlacer  decoPlacer{[&](BlockPos const& p, Block const& s) {
                              decoPlaced = true;
                              world.setBlock(p, s, 19);
                          },
                          1};
    int radius = random.nextInt(2) + 2;
    if (!isAreaSuitable(world, off(pos, -radius, 0, -radius), 2 * radius, 6, 2 * radius)) return false;
    // buildSmallTrunk(world, trunkPlacer, decorationPlacer, random, pos, radius, 6, config)
    int diameter = radius, hollow = diameter >> 1;
    for (int dx = -diameter; dx <= diameter; dx++)
        for (int dz = -diameter; dz <= diameter; dz++)
            for (int dy = -4; dy < 0; dy++) {
                int dist = cml::octagonalDist(std::abs(dx), std::abs(dz));
                if (dist > diameter) continue;
                BlockPos dPos = off(pos, dx, dy, dz);
                if (hasEmptyNeighborExceptBelow(world, dPos)) {
                    if (auto const* s = sampleProvider(dist > hollow ? c["trunk_provider"] : c["branch_provider"], random, dPos)) trunkPlacer.place(dPos, *s);
                } else {
                    placeIfValidRootPos(world, decoPlacer, random, dPos, c["roots_provider"]);
                }
            }
    for (int dx = -diameter; dx <= diameter; dx++)
        for (int dz = -diameter; dz <= diameter; dz++) {
            int a      = random.nextInt(3);
            int height = 2 + a + random.nextInt(2);
            for (int dy = 0; dy <= height; dy++) {
                int ax = std::abs(dx), az = std::abs(dz);
                int dist = std::max(ax, az) + (std::min(ax, az) >> 1);
                if (dist <= diameter && dist > hollow) {
                    BlockPos p = off(pos, dx, dy, dz);
                    // FeaturePlacers#placeIfValidTreePos: TreeFeature#validTreePos or a flower
                    if (validTreePos(world, p) || st::blocks::inTag(world.getBlock(p), "minecraft:flowers"))
                        if (auto const* s = sampleProvider(c["trunk_provider"], random, p)) trunkPlacer.place(p, *s);
                }
            }
        }
    // roots: buildBranchRing(..., radius, 3, 2, 6, 0.75, 3, 5, 3, false) and (..., 1, 2, 8, 0.9, 3, 5, 3, false)
    auto ring = [&](int branchHeight, int heightVar, int length, double tilt, int minBranches, int maxBranches) {
        int    numBranches    = random.nextInt(maxBranches - minBranches) + minBranches;
        double branchRotation = 1.0 / (numBranches + 1);
        double branchOffset   = random.nextDouble();
        for (int i = 0; i <= numBranches; i++) {
            int    dHeight = heightVar > 0 ? branchHeight - heightVar + random.nextInt(2 * heightVar) : branchHeight;
            double angle   = i * branchRotation + branchOffset;
            // makeRoot
            auto src  = cml::translate(toPos(off(pos, 0, dHeight, 0)), radius, angle, 0.5);
            auto dest = cml::translate(src, length, angle, tilt);
            RootPlacer rootPlacer{[&](BlockPos const& p, Block const& s) {
                                      world.setBlock(p, s, 3);
                                      world.setBlock(off(p, 0, -1, 0), s, 3);
                                  },
                                  2};
            traceExposedRoot(world, rootPlacer, random, c["branch_provider"], c["roots_provider"], cml::bresenham(src, dest));
        }
    };
    ring(3, 2, 6, 0.75, 3, 5);
    ring(1, 2, 8, 0.9, 3, 5);
    // TFTreeFeature#place: true when leaves or decorations were placed
    return decoPlaced;
}

// ---- lampposts -------------------------------------------------------------------------------------------

bool lampposts(FeatureContext const& ctx) {
    auto&    world = ctx.region;
    BlockPos pos   = ctx.origin;
    auto&    rand  = ctx.random;
    if (!st::blocks::is(world.getBlock(off(pos, 0, -1, 0)), "minecraft:grass_block")) return false;
    int height = 1 + rand.nextInt(4);
    for (int dy = 0; dy <= height; dy++) {
        Block const& s = world.getBlock(off(pos, 0, dy, 0));
        if (!st::blocks::isAir(s) && !st::blocks::canBeReplaced(s)) return false;
    }
    auto const* fence = st::blocks::fromJava("twilightforest:canopy_fence");
    for (int dy = 0; dy < height; dy++)
        if (fence) world.setBlock(off(pos, 0, dy, 0), *fence, 2 | 16);
    static constexpr st::Rotation kRotations[] = {st::Rotation::None, st::Rotation::Clockwise90, st::Rotation::Clockwise180, st::Rotation::Counterclockwise90};
    auto const*                   lamp         = state(ctx.config.value("state", Json::object()));
    st::Rotation                  rot          = kRotations[rand.nextInt(4)];
    if (lamp) world.setBlock(off(pos, 0, height, 0), st::blocks::rotate(*lamp, rot), 2 | 16);
    return true;
}

// ---- monolith ----------------------------------------------------------------------------------------------

bool monolith(FeatureContext const& ctx) {
    auto&    world = ctx.region;
    BlockPos pos   = ctx.origin;
    auto&    rand  = ctx.random;
    int      ht    = rand.nextInt(10) + 10;
    int      dir   = rand.nextInt(4);
    if (!isAreaSuitable(world, pos, 2, ht, 2)) return false;
    int h75 = static_cast<int>(ht * .75), h50 = static_cast<int>(ht * .5);
    int h[4];
    switch (dir) {
    case 0: h[0] = ht, h[1] = h75, h[2] = h75, h[3] = h50; break;
    case 1: h[0] = h50, h[1] = ht, h[2] = h75, h[3] = h75; break;
    case 2: h[0] = h75, h[1] = h50, h[2] = ht, h[3] = h75; break;
    default: h[0] = h75, h[1] = h75, h[2] = h50, h[3] = ht; break;
    }
    auto const&          lapis    = st::blocks::java("minecraft:lapis_block");
    auto const&          obsidian = st::blocks::java("minecraft:obsidian");
    static constexpr int kCols[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (int col = 0; col < 4; col++)
        for (int cy = 0; cy <= h[col]; cy++) world.setBlock(off(pos, kCols[col][0], cy - 1, kCols[col][1]), cy == ht ? lapis : obsidian, 3);
    // spawn a few ravens nearby (WorldGenLevel#addFreshEntity: queued for the chunk)
    for (int i = 0; i < 2; i++) {
        int dx = rand.nextInt(8);
        dx -= rand.nextInt(8);
        int dz = rand.nextInt(8);
        dz -= rand.nextInt(8);
        BlockPos d{pos.x + dx, 0, pos.z + dz};
        d.y = world.height(worldgen::Heightmap::MotionBlockingNoLeaves, d.x, d.z);
        if (d.y > 0) {
            float yaw = rand.nextFloat() * 360.0F;
            st::blockentities::entity(world, d.x + 0.5, d.y, d.z + 0.5, "twilightforest:raven", Json{{"Rotation", Json::array({yaw, 0.0F})}});
        }
    }
    return true;
}

// ---- mycelium_blob --------------------------------------------------------------------------------------

bool myceliumBlob(FeatureContext const& ctx) {
    auto&       level = ctx.region;
    BlockPos    pos   = ctx.origin;
    auto&       rand  = ctx.random;
    Json const& c     = ctx.config;
    bool        flag  = false;
    int         i     = pos.y;
    int         hh    = c.value("half_height", 0);
    int         top   = i + hh;
    int         end   = i - hh - 1;
    int         l     = kit::sampleInt(c["radius"], rand);
    for (int x = pos.x - l; x <= pos.x + l; x++)
        for (int z = pos.z - l; z <= pos.z + l; z++) {
            // BlockPos.betweenClosed iterates x fastest: (x, z) order matters only for the draws
            (void)0;
        }
    for (int z = pos.z - l; z <= pos.z + l; z++)
        for (int x = pos.x - l; x <= pos.x + l; x++) {
            int i1 = x - pos.x, j1 = z - pos.z;
            if (i1 * i1 + j1 * j1 > l * l) continue;
            for (int y = top; y > end; --y) {
                BlockPos p{x, y, z};
                if (kit::testPredicate(c["target"], level, p) && st::blocks::canBeReplaced(level.getBlock(off(p, 0, 1, 0)))) {
                    if (auto const* s = kit::sampleRuleState(c["state_provider"], level, rand, p)) level.setBlock(p, *s, 2);
                    flag = true;
                }
            }
        }
    return flag;
}

// ---- small_lake ---------------------------------------------------------------------------------------------

bool smallLake(FeatureContext const& ctx) {
    auto&       world  = ctx.region;
    BlockPos    origin = ctx.origin;
    auto&       random = ctx.random;
    Json const& c      = ctx.config;
    if (origin.y <= world.minY() + 4) return false;
    origin.y -= 4;
    auto         shape     = cml::lakeShape(random);
    Block const* fluidState = sampleProvider(c["fluid"], random, origin);
    if (!fluidState) return false;
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++)
            for (int y = 0; y < 8; y++) {
                if (!cml::lakeBorder(shape, x, y, z)) continue;
                Block const& b = world.getBlock(off(origin, x, y, z));
                if (y >= 4 && st::blocks::isLiquid(b)) return false;
                if (y < 4 && !kit::blocksMotion(b) && &b != fluidState) return false;
            }
    Block const* iceState = c.contains("ice") ? sampleProvider(c["ice"], random, origin) : nullptr;
    auto const&  caveAir  = st::blocks::java("minecraft:cave_air");
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++)
            for (int y = 0; y < 8; y++) {
                if (!shape[static_cast<std::size_t>((x * 16 + z) * 8 + y)]) continue;
                BlockPos p = off(origin, x, y, z);
                if (st::blocks::inTag(world.getBlock(p), "twilightforest:small_lakes_dont_replace")
                    || st::blocks::inTag(world.getBlock(off(p, 0, 1, 0)), "twilightforest:small_lakes_dont_replace"))
                    continue;
                if (y >= 4) {
                    world.setBlock(p, caveAir, 2);
                    continue;
                }
                if (y == 3 && iceState) {
                    world.setBlock(p, *iceState, 2);
                    continue;
                }
                world.setBlock(p, *fluidState, 2);
            }
    if (c.contains("barrier")) {
        Block const* barrier = sampleProvider(c["barrier"], random, origin);
        if (barrier && !st::blocks::isAir(*barrier)) {
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < 8; y++) {
                        if (cml::lakeBorder(shape, x, y, z) && (y < 4 || random.nextInt(2) != 0)) {
                            BlockPos     p = off(origin, x, y, z);
                            Block const& b = world.getBlock(p);
                            if (kit::blocksMotion(b) && !st::blocks::inTag(b, "minecraft:lava_pool_stone_cannot_replace")) world.setBlock(p, *barrier, 2);
                        }
                    }
        }
    }
    return true;
}

// ---- webs ----------------------------------------------------------------------------------------------------

bool webs(FeatureContext const& ctx) {
    auto&    world  = ctx.region;
    BlockPos origin = ctx.origin;
    int      span   = world.maxY() - origin.y;
    if (span <= 0) return false;
    BlockPos pos = off(origin, 0, ctx.random.nextInt(span), 0);
    while (pos.y > origin.y) {
        pos.y -= 1;
        Block const& s = world.getBlock(pos);
        bool         valid = st::blocks::inTag(s, "minecraft:logs") || st::blocks::inTag(s, "minecraft:leaves");
        if (st::blocks::isAir(world.getBlock(off(pos, 0, -1, 0))) && valid) {
            bool leaves = st::blocks::inTag(s, "minecraft:leaves") && ctx.random.nextBoolean();
            world.setBlock(leaves ? pos : off(pos, 0, -1, 0), st::blocks::java("minecraft:cobweb"), 2 | 16);
            return true;
        }
    }
    return false;
}

// ---- wood_roots ----------------------------------------------------------------------------------------------

bool placeRootBlock(Region& world, BlockPos const& pos, Json const& provider, RandomSource& random) {
    if (!canRootGrowIn(world, pos)) return false;
    if (auto const* s = sampleProvider(provider, random, pos)) return world.setBlock(pos, *s, 3);
    return false;
}

bool drawRoot(Region& world, RandomSource& rand, BlockPos const& oPos, BlockPos const& pos, float length, float angle, float tilt, Json const& rootBlock,
              Json const& oreBlock) {
    BlockPos  dest  = toBlockPos(cml::translate(toPos(pos), length, angle, tilt));
    int const limit = 6;
    if (oPos.x + limit < dest.x) dest.x = oPos.x + limit;
    if (oPos.x - limit > dest.x) dest.x = oPos.x - limit;
    if (oPos.z + limit < dest.z) dest.z = oPos.z + limit;
    if (oPos.z - limit > dest.z) dest.z = oPos.z - limit;
    if (!st::blocks::is(world.getBlock(dest), "minecraft:stone")) return false;
    RootPlacer placer{[&](BlockPos const& p, Block const& s) { world.setBlock(p, s, 3); }, 1};
    traceRoot(world, placer, rand, rootBlock, cml::bresenham(toPos(pos), toPos(dest)));
    if (length > 8) {
        if (rand.nextInt(3) > 0) {
            BlockPos nextSrc   = toBlockPos(cml::translate(toPos(pos), length / 2, angle, tilt));
            float    nextAngle = std::fmod(angle + 0.25F + (rand.nextFloat() * 0.5F), 1.0F);
            float    nextTilt  = 0.6F + rand.nextFloat() * 0.3F;
            drawRoot(world, rand, oPos, nextSrc, length / 2.0F, nextAngle, nextTilt, rootBlock, oreBlock);
        }
    }
    if (length > 6) {
        if (rand.nextInt(4) == 0) {
            BlockPos ballSrc  = toBlockPos(cml::translate(toPos(pos), length / 2, angle, tilt));
            BlockPos ballDest = toBlockPos(cml::translate(toPos(ballSrc), 1.5, std::fmod(angle + 0.5F, 1.0F), 0.75));
            placeRootBlock(world, ballSrc, oreBlock, rand);
            placeRootBlock(world, BlockPos{ballSrc.x, ballSrc.y, ballDest.z}, oreBlock, rand);
            placeRootBlock(world, BlockPos{ballDest.x, ballSrc.y, ballSrc.z}, oreBlock, rand);
            placeRootBlock(world, BlockPos{ballSrc.x, ballSrc.y, ballDest.z}, oreBlock, rand);
            placeRootBlock(world, BlockPos{ballSrc.x, ballDest.y, ballSrc.z}, oreBlock, rand);
            placeRootBlock(world, BlockPos{ballSrc.x, ballDest.y, ballDest.z}, oreBlock, rand);
            placeRootBlock(world, BlockPos{ballDest.x, ballDest.y, ballSrc.z}, oreBlock, rand);
            placeRootBlock(world, ballDest, oreBlock, rand);
        }
    }
    return true;
}

bool woodRoots(FeatureContext const& ctx) {
    auto&    world = ctx.region;
    BlockPos pos   = ctx.origin;
    auto&    rand  = ctx.random;
    if (!st::blocks::is(world.getBlock(pos), "minecraft:stone")) return false;
    float a      = rand.nextFloat() * 6.0F;
    float length = a + rand.nextFloat() * 6.0F + 4.0F;
    if (length > static_cast<float>(pos.y)) length = static_cast<float>(pos.y);
    float tilt  = 0.6F + rand.nextFloat() * 0.3F;
    float angle = rand.nextFloat();
    return drawRoot(world, rand, pos, pos, length, angle, tilt, ctx.config["root_block"], ctx.config["root_ore"]);
}

} // namespace

void installFeatures() {
    namespace f = gf::features;
    f::registerType("twilightforest:block_spike", [](FeatureContext const& c) { return placeStoneSpike(c.region, c.origin, c.random, false); });
    f::registerType("twilightforest:dark_forest_placer", darkForest);
    f::registerType("twilightforest:fallen_hollow_log", fallenHollowLog);
    f::registerType("twilightforest:fallen_small_log", smallFallenLog);
    f::registerType("twilightforest:foundation", foundation);
    f::registerType("twilightforest:hollow_stump", hollowStump);
    f::registerType("twilightforest:lampposts", lampposts);
    f::registerType("twilightforest:monolith", monolith);
    f::registerType("twilightforest:mycelium_blob", myceliumBlob);
    f::registerType("twilightforest:small_lake", smallLake);
    f::registerType("twilightforest:webs", webs);
    f::registerType("twilightforest:wood_roots", woodRoots);
    gf::log().info("minorlandmarks: 12 feature types registered");
}

} // namespace gf::feature::minorlandmarks

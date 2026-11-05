// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/features/FeatureLogic.java
//   src/main/java/twilightforest/util/features/FeaturePlacers.java
//   src/main/java/twilightforest/util/features/FeatureUtil.java
//   src/main/java/twilightforest/util/iterators/VoxelBresenhamIterator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/trees/TreeUtil.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "mc/world/level/block/Block.h"

#include "core/jrand/Mth.h"
#include "feature/ext/worldgen/FeatureKit.h"

#pragma clang fp contract(off)

namespace gf::feature::trees {

namespace kit = gf::features::kit;

// ---- directions ------------------------------------------------------------------------------------

BlockPos step(Dir d) {
    switch (d) {
    case Dir::Down:
        return {0, -1, 0};
    case Dir::Up:
        return {0, 1, 0};
    case Dir::North:
        return {0, 0, -1};
    case Dir::South:
        return {0, 0, 1};
    case Dir::West:
        return {-1, 0, 0};
    case Dir::East:
        return {1, 0, 0};
    }
    return {0, 0, 0};
}

BlockPos rel(BlockPos const& p, Dir d, int n) {
    auto s = step(d);
    return {p.x + s.x * n, p.y + s.y * n, p.z + s.z * n};
}

char const* dirName(Dir d) {
    static char const* const names[] = {"down", "up", "north", "south", "west", "east"};
    return names[static_cast<int>(d)];
}

Dir clockWise(Dir d) {
    switch (d) {
    case Dir::North:
        return Dir::East;
    case Dir::East:
        return Dir::South;
    case Dir::South:
        return Dir::West;
    case Dir::West:
        return Dir::North;
    default:
        return d;
    }
}

Dir counterClockWise(Dir d) {
    switch (d) {
    case Dir::North:
        return Dir::West;
    case Dir::East:
        return Dir::North;
    case Dir::South:
        return Dir::East;
    case Dir::West:
        return Dir::South;
    default:
        return d;
    }
}

Dir randomDir(RandomSource& random) { return kAll[random.nextInt(6)]; }
Dir randomHorizontal(RandomSource& random) { return kHorizontal[random.nextInt(4)]; }

// ---- VoxelBresenhamIterator --------------------------------------------------------------------------

Bresenham::Bresenham(BlockPos const& from, BlockPos const& to) : mVoxel(from) {
    int xVec = to.x - from.x, yVec = to.y - from.y, zVec = to.z - from.z;
    int absDx = std::abs(xVec), absDy = std::abs(yVec), absDz = std::abs(zVec);
    mXInc = xVec < 0 ? -1 : 1;
    mYInc = yVec < 0 ? -1 : 1;
    mZInc = zVec < 0 ? -1 : 1;
    mDx2  = absDx << 1;
    mDy2  = absDy << 1;
    mDz2  = absDz << 1;
    if (absDx >= absDy && absDx >= absDz) {
        mErr1   = mDy2 - absDx;
        mErr2   = mDz2 - absDx;
        mAxis   = 0;
        mLength = absDx + 1;
    } else if (absDy >= absDx && absDy >= absDz) {
        mErr1   = mDx2 - absDy;
        mErr2   = mDz2 - absDy;
        mAxis   = 1;
        mLength = absDy + 1;
    } else {
        mErr1   = mDy2 - absDz;
        mErr2   = mDx2 - absDz;
        mAxis   = 2;
        mLength = absDz + 1;
    }
}

bool Bresenham::next(BlockPos& out) {
    if (mI >= mLength) return false;
    out = mVoxel;
    // primeNext
    switch (mAxis) {
    case 0:
        if (mErr1 > 0) {
            mVoxel.y += mYInc;
            mErr1 -= mDx2;
        }
        if (mErr2 > 0) {
            mVoxel.z += mZInc;
            mErr2 -= mDx2;
        }
        mErr1 += mDy2;
        mErr2 += mDz2;
        mVoxel.x += mXInc;
        break;
    case 1:
        if (mErr1 > 0) {
            mVoxel.x += mXInc;
            mErr1 -= mDy2;
        }
        if (mErr2 > 0) {
            mVoxel.z += mZInc;
            mErr2 -= mDy2;
        }
        mErr1 += mDx2;
        mErr2 += mDz2;
        mVoxel.y += mYInc;
        break;
    default:
        if (mErr1 > 0) {
            mVoxel.y += mYInc;
            mErr1 -= mDz2;
        }
        if (mErr2 > 0) {
            mVoxel.x += mXInc;
            mErr2 -= mDz2;
        }
        mErr1 += mDy2;
        mErr2 += mDx2;
        mVoxel.z += mZInc;
        break;
    }
    ++mI;
    return true;
}

// ---- FeatureLogic -----------------------------------------------------------------------------------

namespace {
int javaRound(double v) { return static_cast<int>(static_cast<std::int64_t>(std::floor(v + 0.5))); }
} // namespace

BlockPos translate(BlockPos const& pos, double distance, double angle, double tilt) {
    double rangle = angle * 2.0 * std::numbers::pi;
    double rtilt  = tilt * std::numbers::pi;
    return at(pos, javaRound(std::sin(rangle) * std::sin(rtilt) * distance), javaRound(std::cos(rtilt) * distance), javaRound(std::cos(rangle) * std::sin(rtilt) * distance));
}

Block const* provide(Json const& provider, RandomSource& random, BlockPos const& pos) { return kit::sampleState(provider, random, pos); }

bool treeFeatureValidPos(Region& region, BlockPos const& pos) { return kit::validTreePos(region, pos); }

bool validTreePos(Region& region, BlockPos const& pos) {
    if (kit::validTreePos(region, pos)) return true;
    return kit::hasTag(region.getBlock(pos), "minecraft:flowers");
}

bool replaceableOrAir(Region& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    return b.isAir() || kit::isReplaceable(b);
}

bool isReplaceable(Block const& block, bool includeFlowers) {
    bool ok = kit::isReplaceable(block) || kit::hasTag(block, "twilightforest:worldgen_replaceables") || (includeFlowers && kit::hasTag(block, "minecraft:flowers"));
    return ok && !kit::hasTag(block, "minecraft:features_cannot_replace");
}

bool hasEmptyNeighborExceptBelow(Region& region, BlockPos const& pos) {
    return replaceableOrAir(region, above(pos)) || replaceableOrAir(region, rel(pos, Dir::North)) || replaceableOrAir(region, rel(pos, Dir::South))
        || replaceableOrAir(region, rel(pos, Dir::West)) || replaceableOrAir(region, rel(pos, Dir::East));
}

bool hasSolidNeighbor(Region& region, BlockPos const& pos) {
    return !(replaceableOrAir(region, below(pos)) && replaceableOrAir(region, rel(pos, Dir::North)) && replaceableOrAir(region, rel(pos, Dir::South))
             && replaceableOrAir(region, rel(pos, Dir::West)) && replaceableOrAir(region, rel(pos, Dir::East)) && replaceableOrAir(region, above(pos)));
}

bool canRootGrowIn(Region& region, BlockPos const& pos) {
    if (replaceableOrAir(region, pos)) return hasSolidNeighbor(region, pos); // through air near a solid block
    return isReplaceable(region.getBlock(pos), false);
}

bool hasAirAround(Region& region, BlockPos const& pos) {
    for (Dir d : {Dir::Up, Dir::North, Dir::South, Dir::West, Dir::East})
        if (region.getBlock(rel(pos, d)).isAir()) return true;
    return false;
}

bool anyBelowMatch(BlockPos const& pos, int depth, std::function<bool(BlockPos const&)> const& predicate) {
    // isAnyMatchInArea(pos.below(depth), 1, depth + 1, 1, predicate)
    BlockPos base = below(pos, depth);
    for (int cy = 0; cy < depth + 1; ++cy)
        if (predicate(above(base, cy))) return true;
    return false;
}

bool isSolidRender(Block const& block) {
    if (block.isAir() || kit::isLeaves(block) || kit::isFluid(block)) return false;
    return kit::blocksMotion(block);
}

bool saplingSoil(Block const& block) { return kit::hasTag(block, "minecraft:dirt") || kit::javaId(block) == "minecraft:farmland"; }

// ---- FeaturePlacers ---------------------------------------------------------------------------------

namespace {
/// FeatureLogic.ROOT_SHOULD_SKIP: BlockTagGenerator.ROOT_TRACE_SKIP, whose id is tree_roots_skip
/// (logs, root, liveroot, mangrove root, time wood, #features_cannot_replace).
constexpr char const* kRootTraceSkip = "twilightforest:tree_roots_skip";
} // namespace

void setBlock(Region& region, BlockPos const& pos, Block const& block) { region.setBlock(pos, block, 19); }

void placeProvided(Region& region, Placer const& placer, bool (*predicate)(Region&, BlockPos const&), BlockPos const& pos, Json const& provider, RandomSource& random) {
    if (!predicate(region, pos)) return;
    if (auto const* b = provide(provider, random, pos)) placer(pos, *b);
}

bool placeIfValidTreePos(Region& region, Placer const& placer, RandomSource& random, BlockPos const& pos, Json const& provider) {
    if (!validTreePos(region, pos)) return false;
    if (auto const* b = provide(provider, random, pos)) placer(pos, *b);
    return true;
}

bool placeIfValidRootPos(Region& region, RootPlacer const& placer, RandomSource& random, BlockPos const& pos, Json const& provider) {
    if (anyBelowMatch(pos, placer.penetrability - 1, [&](BlockPos const& p) { return !canRootGrowIn(region, p); })) return false;
    if (auto const* b = provide(provider, random, pos)) placer.placer(pos, *b);
    return true;
}

void drawBresenhamBranch(Region& region, Placer const& placer, RandomSource& random, BlockPos const& from, BlockPos const& to, Json const& provider) {
    Bresenham line(from, to);
    BlockPos  p;
    while (line.next(p)) placeIfValidTreePos(region, placer, random, p, provider);
}

namespace {
bool withinCircle(int x, int z, float radius, bool legacy) {
    if (legacy) {
        int ax = std::abs(x), az = std::abs(z);
        int d  = ax == 3 && az == 3 ? 6 : static_cast<int>(static_cast<float>(std::max(ax, az)) + static_cast<float>(std::min(ax, az)) * 0.5F);
        return static_cast<float>(d) <= radius;
    }
    return static_cast<float>(x * x + z * z) <= radius * radius;
}
} // namespace

void placeCircleOdd(Region& region, Placer const& placer, RandomSource& random, BlockPos const& c, float radius, Json const& provider, bool legacy) {
    placeProvided(region, placer, treeFeatureValidPos, c, provider, random);
    for (int x = 0; static_cast<float>(x) <= radius; ++x) {
        for (int z = 1; static_cast<float>(z) <= radius; ++z) {
            if (!withinCircle(x, z, radius, legacy)) continue;
            placeProvided(region, placer, treeFeatureValidPos, at(c, x, 0, z), provider, random);
            placeProvided(region, placer, treeFeatureValidPos, at(c, -x, 0, -z), provider, random);
            placeProvided(region, placer, treeFeatureValidPos, at(c, -z, 0, x), provider, random);
            placeProvided(region, placer, treeFeatureValidPos, at(c, z, 0, -x), provider, random);
        }
    }
}

void placeCircleEven(Region& region, Placer const& placer, RandomSource& random, BlockPos const& c, float radius, Json const& provider, bool legacy) {
    placeProvided(region, placer, treeFeatureValidPos, c, provider, random);
    for (int x = 0; static_cast<float>(x) <= radius; ++x) {
        for (int z = 0; static_cast<float>(z) <= radius; ++z) {
            if (!withinCircle(x, z, radius, legacy)) continue;
            placeProvided(region, placer, treeFeatureValidPos, at(c, 1 + x, 0, 1 + z), provider, random);
            placeProvided(region, placer, treeFeatureValidPos, at(c, -x, 0, -z), provider, random);
            placeProvided(region, placer, treeFeatureValidPos, at(c, -x, 0, 1 + z), provider, random);
            placeProvided(region, placer, treeFeatureValidPos, at(c, 1 + x, 0, -z), provider, random);
        }
    }
}

void placeSpheroidBiased(Region& region, Placer const& placer, RandomSource& random, BlockPos const& c, float xzRadius, float yRadius, float bias, Json const& provider) {
    float xz2    = xzRadius * xzRadius;
    float y2     = yRadius * yRadius;
    float super2 = xz2 * y2;
    auto  leaf   = [&](BlockPos const& p) { placeProvided(region, placer, treeFeatureValidPos, p, provider, random); };
    leaf(c);
    for (int y = 0; static_cast<float>(y) <= yRadius; ++y) {
        leaf(at(c, 0, y, 0));
        leaf(at(c, 0, -y, 0));
    }
    for (int x = 0; static_cast<float>(x) <= xzRadius; ++x) {
        for (int z = 1; static_cast<float>(z) <= xzRadius; ++z) {
            if (static_cast<float>(x * x + z * z) > xz2) continue;
            leaf(at(c, x, 0, z));
            leaf(at(c, -x, 0, -z));
            leaf(at(c, -z, 0, x));
            leaf(at(c, z, 0, -x));
            for (int y = 1; static_cast<float>(y) <= yRadius; ++y) {
                float xzSquare = static_cast<float>(x * x + z * z) * y2;
                float up       = static_cast<float>(y) - bias;
                float down     = static_cast<float>(y) + bias;
                if (xzSquare + up * up * xz2 <= super2) {
                    leaf(at(c, x, y, z));
                    leaf(at(c, -x, y, -z));
                    leaf(at(c, -z, y, x));
                    leaf(at(c, z, y, -x));
                }
                if (xzSquare + down * down * xz2 <= super2) {
                    leaf(at(c, x, -y, z));
                    leaf(at(c, -x, -y, -z));
                    leaf(at(c, -z, -y, x));
                    leaf(at(c, z, -y, -x));
                }
            }
        }
    }
}

void placeSpheroid(Region& region, Placer const& placer, RandomSource& random, BlockPos const& c, float xzRadius, float yRadius, Json const& provider) {
    float xz2    = xzRadius * xzRadius;
    float y2     = yRadius * yRadius;
    float super2 = xz2 * y2;
    auto  put    = [&](BlockPos const& p) { placeProvided(region, placer, treeFeatureValidPos, p, provider, random); };
    put(c);
    for (int y = 0; static_cast<float>(y) <= yRadius; ++y) {
        put(at(c, 0, y, 0));
        put(at(c, 0, -y, 0));
    }
    for (int x = 0; static_cast<float>(x) <= xzRadius; ++x) {
        for (int z = 1; static_cast<float>(z) <= xzRadius; ++z) {
            if (static_cast<float>(x * x + z * z) > xz2) continue;
            put(at(c, x, 0, z));
            put(at(c, -x, 0, -z));
            put(at(c, -z, 0, x));
            put(at(c, z, 0, -x));
            for (int y = 1; static_cast<float>(y) <= yRadius; ++y) {
                float ySquare = static_cast<float>(y * y) * xz2;
                if (static_cast<float>(x * x + z * z) * y2 + ySquare <= super2) {
                    if (static_cast<float>((x + 1) * (x + 1) + z * z) * y2 + ySquare > super2 && static_cast<float>(x * x + (z + 1) * (z + 1)) * y2 + ySquare > super2) {
                        // randomly leave out some blocks on the very edges of the blob's circles
                        if (random.nextInt(3) != 0) put(at(c, x, y, z));
                        if (random.nextInt(3) != 0) put(at(c, -x, y, -z));
                        if (random.nextInt(3) != 0) put(at(c, -z, y, x));
                        if (random.nextInt(3) != 0) put(at(c, z, y, -x));
                        if (random.nextInt(3) != 0) put(at(c, x, -y, z));
                        if (random.nextInt(3) != 0) put(at(c, -x, -y, -z));
                        if (random.nextInt(3) != 0) put(at(c, -z, -y, x));
                        if (random.nextInt(3) != 0) put(at(c, z, -y, -x));
                        continue;
                    }
                    put(at(c, x, y, z));
                    put(at(c, -x, y, -z));
                    put(at(c, -z, y, x));
                    put(at(c, z, y, -x));
                    put(at(c, x, -y, z));
                    put(at(c, -x, -y, -z));
                    put(at(c, -z, -y, x));
                    put(at(c, z, -y, -x));
                }
            }
        }
    }
}

void traceRoot(Region& region, RootPlacer const& placer, RandomSource& random, Json const& dirtRoot, Bresenham& line) {
    BlockPos p;
    while (line.next(p)) {
        // end the root where it cannot be penetrated
        if (anyBelowMatch(p, placer.penetrability - 1, [&](BlockPos const& q) { return kit::hasTag(region.getBlock(q), kRootTraceSkip); })) return;
        // stop where the position cannot be replaced or is detached from the ground mass
        if (!placeIfValidRootPos(region, placer, random, p, dirtRoot)) return;
    }
}

void traceExposedRoot(Region& region, RootPlacer const& placer, RandomSource& random, Json const& exposedRoot, Json const& dirtRoot, Bresenham& line) {
    BlockPos p;
    while (line.next(p)) {
        if (kit::hasTag(region.getBlock(p), kRootTraceSkip)) continue;
        if (hasEmptyNeighborExceptBelow(region, p)) {
            // not underground: the root must stop at what it cannot replace
            bool stop = anyBelowMatch(p, placer.penetrability - 1, [&](BlockPos const& q) {
                auto const& s = region.getBlock(q);
                return !isReplaceable(s, false) && &s != provide(exposedRoot, random, p);
            });
            if (stop) return;
            if (auto const* b = provide(exposedRoot, random, p)) placer.placer(p, *b);
        } else {
            // underground: an underground root from here on
            if (placeIfValidRootPos(region, placer, random, p, dirtRoot)) traceRoot(region, placer, random, dirtRoot, line);
            return;
        }
    }
}

void buildRoot(Region& region, RootPlacer const& placer, RandomSource& random, BlockPos const& start, double offset, int b, Json const& provider) {
    BlockPos  dest = translate(below(start, b + 2), 5, 0.3 * b + offset, 0.8);
    Bresenham line(below(start), dest);
    BlockPos  p;
    // stop drawing when the root heads too far into open air
    while (line.next(p))
        if (!placeIfValidRootPos(region, placer, random, p, provider)) return;
}

// ---- PosSet -----------------------------------------------------------------------------------------

namespace {
std::int64_t key(BlockPos const& p) { return core::mth::blockPosAsLong(p.x, p.y, p.z); }
} // namespace

bool PosSet::add(BlockPos const& p) {
    auto k  = key(p);
    auto it = std::lower_bound(mKeys.begin(), mKeys.end(), k);
    if (it != mKeys.end() && *it == k) return false;
    mKeys.insert(it, k);
    list.push_back(p);
    return true;
}

bool PosSet::contains(BlockPos const& p) const { return std::binary_search(mKeys.begin(), mKeys.end(), key(p)); }

} // namespace gf::feature::trees

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/features/FeatureLogic.java
//   src/main/java/twilightforest/util/iterators/VoxelBresenhamIterator.java
//   src/main/java/twilightforest/world/components/structures/HedgeMazeComponent.java
//   src/main/java/twilightforest/world/components/structures/type/HollowHillStructure.java
//   src/main/java/twilightforest/world/components/structures/fallentrunk/FallenTrunkPiece.java
//   src/main/java/twilightforest/world/components/structures/fallentrunk/Hole.java
//   src/main/java/twilightforest/world/components/structures/fallentrunk/TrunkUnderDensityFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/HollowHillFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/FocusedDensityFunction.java
//   src/main/java/twilightforest/world/components/chunkgenerators/TanhHillFunction.java
//   src/main/java/twilightforest/world/components/feature/TFSmallLakeFeature.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/minorlandmarks/MinorLandmarks.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <unordered_set>

#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"

namespace gf::core::minorlandmarks {

namespace {

namespace jr = core::jrand;

constexpr float kPi    = 3.1415927F; // Mth.PI
constexpr float kTwoPi = 6.2831855F; // Mth.TWO_PI

/// Java's int arithmetic (wrapping).
int wadd(int a, int b) { return core::mth::iadd(a, b); }
int wsub(int a, int b) { return static_cast<int>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }

long long javaRound(double v) { return static_cast<long long>(std::floor(v + 0.5)); }

float mthSqrt(float v) { return static_cast<float>(std::sqrt(static_cast<double>(v))); }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Mth.clampedMap(float...): clampedLerp(outMin, outMax, inverseLerp(v, inMin, inMax)).
float clampedMapF(float v, float inMin, float inMax, float outMin, float outMax) {
    float t = (v - inMin) / (inMax - inMin);
    if (t < 0.0F) return outMin;
    if (t > 1.0F) return outMax;
    return outMin + t * (outMax - outMin); // Mth.lerp(float delta, float start, float end)
}

std::int64_t posKey(Pos const& p) { return core::mth::blockPosAsLong(p.x, p.y, p.z); }

/// The iteration order of a java.util.HashSet<BlockPos> filled with `inserted` (duplicates dropped,
/// the first insertion kept).
std::vector<Pos> hashSetOrdered(std::vector<Pos> const& inserted) {
    std::vector<Pos>                 unique;
    std::unordered_set<std::int64_t> seen;
    for (auto const& p : inserted)
        if (seen.insert(posKey(p)).second) unique.push_back(p);
    std::vector<int> hashes;
    hashes.reserve(unique.size());
    for (auto const& p : unique) hashes.push_back(jr::blockPosHash(p.x, p.y, p.z));
    std::vector<Pos> out;
    out.reserve(unique.size());
    for (auto i : jr::hashSetOrder(hashes)) out.push_back(unique[i]);
    return out;
}

} // namespace

int nextIntRange(RandomSource& random, int origin, int bound) { return origin + random.nextInt(bound - origin); }

// ---- FeatureLogic / VoxelBresenhamIterator ------------------------------------------------------------

Pos translate(Pos const& pos, double distance, double angle, double tilt) {
    double rangle = angle * 2.0 * 3.141592653589793;
    double rtilt  = tilt * 3.141592653589793;
    return pos.offset(static_cast<int>(javaRound(std::sin(rangle) * std::sin(rtilt) * distance)), static_cast<int>(javaRound(std::cos(rtilt) * distance)),
                      static_cast<int>(javaRound(std::cos(rangle) * std::sin(rtilt) * distance)));
}

std::vector<Pos> bresenham(Pos const& from, Pos const& to) {
    int xVec = to.x - from.x, yVec = to.y - from.y, zVec = to.z - from.z;
    int absDx = std::abs(xVec), absDy = std::abs(yVec), absDz = std::abs(zVec);
    int xInc = xVec < 0 ? -1 : 1, yInc = yVec < 0 ? -1 : 1, zInc = zVec < 0 ? -1 : 1;
    int dDx = absDx << 1, dDy = absDy << 1, dDz = absDz << 1;
    int err1 = 0, err2 = 0, length = 0;
    char axis = 0;
    if (absDx >= absDy && absDx >= absDz) {
        err1   = dDy - absDx;
        err2   = dDz - absDx;
        axis   = 'x';
        length = absDx + 1;
    } else if (absDy >= absDx && absDy >= absDz) {
        err1   = dDx - absDy;
        err2   = dDz - absDy;
        axis   = 'y';
        length = absDy + 1;
    } else {
        err1   = dDy - absDz;
        err2   = dDx - absDz;
        axis   = 'z';
        length = absDz + 1;
    }
    std::vector<Pos> out;
    out.reserve(static_cast<std::size_t>(length));
    Pos v = from;
    for (int i = 0; i < length; i++) {
        out.push_back(v);
        // primeNext
        switch (axis) {
        case 'x':
            if (err1 > 0) {
                v.y += yInc;
                err1 -= dDx;
            }
            if (err2 > 0) {
                v.z += zInc;
                err2 -= dDx;
            }
            err1 += dDy;
            err2 += dDz;
            v.x += xInc;
            break;
        case 'y':
            if (err1 > 0) {
                v.x += xInc;
                err1 -= dDy;
            }
            if (err2 > 0) {
                v.z += zInc;
                err2 -= dDy;
            }
            err1 += dDx;
            err2 += dDz;
            v.y += yInc;
            break;
        default:
            if (err1 > 0) {
                v.y += yInc;
                err1 -= dDz;
            }
            if (err2 > 0) {
                v.x += xInc;
                err2 -= dDz;
            }
            err1 += dDy;
            err2 += dDx;
            v.z += zInc;
            break;
        }
    }
    return out;
}

// ---- hedge maze -------------------------------------------------------------------------------------------

bool hedgeMazeIsNearRoom(int dx, int dz, std::vector<int> const& rcoords) {
    if (dx == 1 && dz == 1) return true;
    for (std::size_t i = 0; i < rcoords.size() / 2; i++) {
        int rx = rcoords[i * 2];
        int rz = rcoords[i * 2 + 1];
        if (rx == 0 && rz == 0) continue;
        if (std::abs(dx - rx) < 3 && std::abs(dz - rz) < 3) return true;
    }
    return false;
}

// ---- density functions -------------------------------------------------------------------------------

double hollowHillFunction(Pos const& pos, float radius, float heightScale, int x, int y, int z) {
    float centerX = static_cast<float>(pos.x) + 0.5F, bottomY = static_cast<float>(pos.y) + 0.5F, centerZ = static_cast<float>(pos.z) + 0.5F;
    float dX             = static_cast<float>(x) - centerX;
    float dY             = static_cast<float>(y) - bottomY;
    float dZ             = static_cast<float>(z) - centerZ;
    float dist           = mthSqrt(dX * dX + dZ * dZ);
    float height         = jr::mthCos(dist / radius * kPi) * radius * 0.3333333334F;
    float normalizedDist = clampf(dist / std::fabs(radius), 0.0F, 1.0F);
    if (normalizedDist >= 1.0F) return 0.0;
    return clampf(height * heightScale - dY, -1.0F, 1.0F);
}

double focusedDensity(Pos const& pos, float radius, float nearValue, float farValue, int x, int y, int z) {
    float centerX = static_cast<float>(pos.x) + 0.5F, bottomY = static_cast<float>(pos.y) + 0.5F, centerZ = static_cast<float>(pos.z) + 0.5F;
    float dX   = centerX - static_cast<float>(x);
    float dY   = bottomY - static_cast<float>(y);
    float dZ   = centerZ - static_cast<float>(z);
    float dist = mthSqrt(dX * dX + dY * dY + dZ * dZ);
    return clampedMapF(dist, 0.0F, radius, nearValue, farValue);
}

Pos boxCenter(BlockBox const& box) {
    return {box.minX + (box.maxX - box.minX + 1) / 2, box.minY + (box.maxY - box.minY + 1) / 2, box.minZ + (box.maxZ - box.minZ + 1) / 2};
}

double hollowHillTerraformer(int hillSize, BlockBox const& structureBox, int x, int y, int z) {
    float const radius      = (static_cast<float>(hillSize) * 4.0F + 0.8F) * 8.0F;
    float const radiusInner = radius - 8.0F;
    int const   width       = std::min(structureBox.maxX - structureBox.minX + 1, structureBox.maxZ - structureBox.minZ + 1);
    int const   yCeiling    = structureBox.minY;
    Pos const   c           = boxCenter(structureBox);

    double hillMound = std::clamp(hollowHillFunction({c.x, yCeiling + 10, c.z}, radius, 0.7F, x, y, z), 0.0, 1.0);
    double innerCeiling = -1.0 * hollowHillFunction({c.x, yCeiling + 6, c.z}, radiusInner, 0.675F, x, y, z);
    double innerFloor   = hollowHillFunction({c.x, yCeiling + hillSize + hillSize / 2, c.z}, 2.0F - radiusInner, 1.0F / 10.0F, x, y, z);
    double interior     = std::max(innerCeiling, innerFloor);
    double interiorMask = focusedDensity({c.x, yCeiling, c.z}, radiusInner * 0.52F, -radiusInner, 1.0F, x, y, z);
    double interiorMasked = std::max(interiorMask, interior);
    double hollowHill     = std::min(hillMound, interiorMasked);
    int    below          = core::mth::ceil(static_cast<double>(radius * 0.1F)); // Mth.ceil(float)
    double maskingSphere  = std::clamp(
        focusedDensity({c.x, c.y - below, c.z}, static_cast<float>(width) * 0.5F + 5.0F, static_cast<float>(width) * 0.25F, 0.0F, x, y, z), 0.0, 1.0);
    return maskingSphere * (8.0 * hollowHill);
}

TanhHill TanhHill::withAngle(float cx, float by, float cz, float radius, float heightScale, float angle, bool xOriented, bool rightSide) {
    TanhHill h;
    h.centerX               = cx;
    h.bottomY               = by;
    h.centerZ               = cz;
    h.radius                = radius;
    h.heightScale           = heightScale;
    h.cosAngleBiasDirection = jr::mthCos(angle);
    h.sinAngleBiasDirection = jr::mthSin(angle);
    h.isXOriented           = xOriented;
    h.isOnRightSide         = rightSide;
    return h;
}

double TanhHill::compute(int blockX, int blockY, int blockZ) const {
    float dX = static_cast<float>(blockX) - centerX;
    float dY = static_cast<float>(blockY) - bottomY;
    float dZ = static_cast<float>(blockZ) - centerZ;
    if (isXOriented) dZ *= 1.3F; // PERPENDICULAR_BIAS
    else dX *= 1.3F;
    return compute(dX, dY, dZ);
}

double TanhHill::compute(float dX, float dY, float dZ) const {
    if (dY > 10 || dY < -10) return -1;
    float perpendicularDist = isXOriented ? dZ : dX;
    if (!isOnRightSide) perpendicularDist = -perpendicularDist;
    if (perpendicularDist < 0) return -1;
    double distSquared = static_cast<double>(dX * dX + dZ * dZ); // float maths, widened
    if (distSquared > static_cast<double>(9 * radius * radius)) return -1;
    double dist    = std::sqrt(distSquared);
    double cosPhi  = dX / dist;
    double sinPhi  = dZ / dist;
    double cos2Phi = cosPhi * cosPhi - sinPhi * sinPhi;
    double sin2Phi = 2 * cosPhi * sinPhi;
    dist /= (1 + (sin2Phi * cosAngleBiasDirection - cos2Phi * sinAngleBiasDirection) / 3);
    double tanhExp = std::exp(2 * (2 - 2 * dist / radius));
    double height  = ((tanhExp - 1) / (tanhExp + 1)) - dist * (1 - std::exp(-dist * radius * 0.01));
    if (heightScale * (height + 1) > dY) return 1;
    return -1;
}

// ---- vanilla piece geometry ------------------------------------------------------------------------------

Dir horizontalByIndex(int index) {
    static constexpr Dir kOrder[] = {Dir::North, Dir::East, Dir::South, Dir::West};
    return kOrder[((index % 4) + 4) % 4];
}

Dir clockwise(Dir d) {
    switch (d) {
    case Dir::North: return Dir::East;
    case Dir::East: return Dir::South;
    case Dir::South: return Dir::West;
    default: return Dir::North;
    }
}

Dir opposite(Dir d) { return clockwise(clockwise(d)); }

BlockBox orientBox(int x, int y, int z, int offX, int offY, int offZ, int width, int height, int depth, Dir facing) {
    switch (facing) {
    case Dir::North: return {x + offX, y + offY, z - depth + 1 + offZ, x + width - 1 + offX, y + height - 1 + offY, z + offZ};
    case Dir::West: return {x - depth + 1 + offZ, y + offY, z + offX, x + offZ, y + height - 1 + offY, z + width - 1 + offX};
    case Dir::East: return {x + offZ, y + offY, z + offX, x + depth - 1 + offZ, y + height - 1 + offY, z + width - 1 + offX};
    default: return {x + offX, y + offY, z + offZ, x + width - 1 + offX, y + height - 1 + offY, z + depth - 1 + offZ};
    }
}

Pos vanillaWorldPos(BlockBox const& box, Dir o, int x, int y, int z) {
    int wx = 0, wz = 0;
    switch (o) {
    case Dir::North:
        wx = box.minX + x;
        wz = box.maxZ - z;
        break;
    case Dir::South:
        wx = box.minX + x;
        wz = box.minZ + z;
        break;
    case Dir::West:
        wx = box.maxX - z;
        wz = box.minZ + x;
        break;
    case Dir::East:
        wx = box.minX + z;
        wz = box.minZ + x;
        break;
    }
    return {wx, y + box.minY, wz};
}

// ---- fallen trunk ------------------------------------------------------------------------------------------

FallenTrunk::FallenTrunk(BlockBox b, Dir o, int len, int r, std::int64_t seed) : box(b), orientation(o), length(len), radius(r), holeSeed(seed) {
    // Hole(piece, RandomSource.create(holeSeed))
    jr::LegacyRandom random(holeSeed);
    mSizeXY      = sideLength() * 4;
    mSizeZ       = length - (kTrunkErodedLength + 1) * 2;
    int height   = mSizeXY;
    int hlength  = mSizeZ;
    mHole.assign(static_cast<std::size_t>(std::max(height, 0)), std::vector<bool>(static_cast<std::size_t>(std::max(hlength, 0)), false));
    int xMin = length / 6, xMax = length / 4 + 1, yMin = sideLength(), yMax = sideLength() * 2;

    int ySize   = nextIntRange(random, yMin, yMax);
    int yOffset = nextIntRange(random, 1, yMax - ySize + 1);
    int prevX1 = INT_MIN, prevX2 = INT_MAX;
    for (int dy = yOffset; dy < yOffset + ySize; dy++) {
        int xSize = nextIntRange(random, xMin, xMax + 1);
        int x1    = random.nextInt(hlength - xSize - 1);
        int x2    = x1 + xSize - 1;
        int tries = 0;
        auto bad  = [&] {
            float num   = static_cast<float>(wadd(wsub(std::min(x2, prevX2), std::max(prevX1, x1)), 1));
            float den   = static_cast<float>(wadd(wsub(std::max(x2, prevX2), std::min(x1, prevX1)), 1));
            float ratio = num / den;
            return (tries < 1000 && (x1 == prevX1 || (x2 == prevX2 && dy % yMin != 0))) || x2 < prevX1 || x1 > prevX2 || ratio < 1.0F / 3.0F;
        };
        // Java loops until the shape fits; a bound below the origin throws there (IllegalArgumentException):
        // it gives up instead, as does a loop that would never end
        int guard = 0;
        while (bad()) {
            xSize      = nextIntRange(random, xMin, xMax + 1);
            int origin = std::max(0, wsub(prevX1, xSize));
            int bound  = std::min(hlength - xSize - 1, wadd(prevX2, xSize));
            if (origin >= bound || ++guard > 100000) {
                mGaveUp = true;
                break;
            }
            x1 = nextIntRange(random, origin, bound);
            x2 = x1 + xSize - 1;
            tries++;
        }
        if (tries >= 1000) mGaveUp = true;
        prevX1 = x1;
        prevX2 = x2;
        for (int x = x1; x <= x2; x++)
            if (dy >= 0 && dy < height && x >= 0 && x < hlength) mHole[static_cast<std::size_t>(dy)][static_cast<std::size_t>(x)] = true;
    }
}

std::array<int, 2> FallenTrunk::lengthToXY(int len) const {
    int s = sideLength();
    if (len < s) return {0, len + 1};
    if (len < 2 * s) return {len + 1 - s, s + 1};
    if (len < 3 * s) return {s + 1, 3 * s - len};
    return {4 * s - (len + 1), 0};
}

bool FallenTrunk::inHole(int xy, int z) const {
    if (xy < 0 || xy >= mSizeXY || z < 0 || z >= mSizeZ) return false;
    return mHole[static_cast<std::size_t>(xy)][static_cast<std::size_t>(z)];
}

std::vector<Pos> FallenTrunk::holeBlocks() const {
    int              zOffset = kTrunkErodedLength + 1;
    std::vector<Pos> inserted;
    for (int xy = 0; xy < mSizeXY; xy++)
        for (int z = 0; z < mSizeZ; z++)
            if (inHole(xy, z)) {
                auto s = lengthToXY(xy);
                inserted.push_back(worldPos(s[0], s[1], z + zOffset));
            }
    return hashSetOrdered(inserted);
}

bool FallenTrunk::isHoleBlock(Pos const& p) const {
    int zOffset = kTrunkErodedLength + 1;
    for (int xy = 0; xy < mSizeXY; xy++)
        for (int z = 0; z < mSizeZ; z++)
            if (inHole(xy, z)) {
                auto s = lengthToXY(xy);
                if (worldPos(s[0], s[1], z + zOffset) == p) return true;
            }
    return false;
}

std::vector<Pos> FallenTrunk::potentialMoundBases(bool rightSide) const {
    std::vector<Pos> inserted;
    int forbiddenLength = (length + 3) / 4 + kTrunkErodedLength; // Math.ceilDiv(length, 4) (length > 0)
    for (int offset = forbiddenLength; offset <= length - forbiddenLength; offset++) {
        if (rightSide) inserted.push_back(worldPos(std::min(box.maxX - box.minX + 1, box.maxZ - box.minZ + 1) - 1, 0, offset));
        else inserted.push_back(worldPos(0, 0, offset));
    }
    return hashSetOrdered(inserted);
}

std::vector<Pos> FallenTrunk::allowedMoundBases(bool rightSide) const {
    std::unordered_set<std::int64_t> forbidden;
    for (auto const& p : holeBlocks()) {
        forbidden.insert(posKey(p));
        static constexpr int kDirs[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        for (auto const& d : kDirs) forbidden.insert(posKey({p.x + d[0], box.minY, p.z + d[2]}));
    }
    std::vector<Pos> out;
    for (auto const& p : potentialMoundBases(rightSide))
        if (!forbidden.contains(posKey(p))) out.push_back(p);
    return out;
}

TrunkUnderDensity::TrunkUnderDensity(FallenTrunk const& piece, bool isBigTree, int minMounds, int maxMounds)
: mBox(piece.box),
  mBig(isBigTree),
  mXOriented(piece.box.maxX - piece.box.minX > piece.box.maxZ - piece.box.minZ) {
    jr::LegacyRandom random(static_cast<std::int64_t>(mBox.minX) * 14413411LL + static_cast<std::int64_t>(mBox.minZ) * 43387781LL);
    int attempted = nextIntRange(random, minMounds, maxMounds + 1);
    for (int i = 0; i < attempted; i++) {
        bool rightSide = i % 2 == 0;
        auto bases     = mBig ? piece.potentialMoundBases(rightSide) : piece.allowedMoundBases(rightSide);
        if (bases.empty()) continue; // (a big tree's list is never empty)
        Pos   base       = bases[static_cast<std::size_t>(random.nextInt(static_cast<int>(bases.size())))];
        int   maxIncrease = mBig ? 4 : 2;
        float moundRadius = 3.0F + static_cast<float>(nextIntRange(random, 0, maxIncrease));
        float moundHeight = mBig ? 3.0F : 1.5F;
        float angle       = kTwoPi * random.nextFloat();
        mMounds.push_back(TanhHill::withAngle(static_cast<float>(base.x), static_cast<float>(base.y), static_cast<float>(base.z), moundRadius, moundHeight, angle,
                                              mXOriented, rightSide));
    }
}

double TrunkUnderDensity::compute(int x, int y, int z) const {
    int horizontalDistanceX = std::max(0, std::max(mBox.minX - x, x - mBox.maxX));
    int horizontalDistanceZ = std::max(0, std::max(mBox.minZ - z, z - mBox.maxZ));
    int adjustedGroundLevel = mBox.minY + 0 + (mBig ? 1 : 0);
    int verticalDistance    = y - adjustedGroundLevel;
    double mound            = moundsContribution(x, y, z);
    if (mound > 0) return mound;
    return flatSurroundingTerrain(horizontalDistanceX, verticalDistance, horizontalDistanceZ);
}

double TrunkUnderDensity::flatSurroundingTerrain(double x, double y, double z) {
    if (y > 20 || y < -10) return 0;
    if (x == 0 && z == 0 && y > 0) return -1;
    double const a = kTrunkTerraformPieceSize, c = kTrunkTerraformPieceSize;
    double const verticalScale = 0.7;
    double normX = std::abs(x / a);
    double normZ = std::abs(z / c);
    double nsq   = normX * normX + normZ * normZ;
    if (nsq >= 1.0) return 0.0;
    double nd     = std::sqrt(nsq);
    double factor = 1.0 - nd;
    if (y < 0) y = nd * 0.5 + y / 5;
    return -y * factor / verticalScale * (std::exp(-nd / (std::max(1 - nd, 1e-8))));
}

double TrunkUnderDensity::moundsContribution(int bx, int by, int bz) const {
    int x      = bx - mBox.minX;
    int y      = by - mBox.minY;
    int z      = bz - mBox.minZ;
    int radius = static_cast<int>(std::ceil((mBox.maxY - mBox.minY + 1) / 2.0));
    double ax  = std::abs((mXOriented ? z : x) - radius + 1);
    double az  = std::abs(y - radius + 1);
    if (radius == 2) {
        if (std::abs((mXOriented ? z : x) - 1.5) + std::abs(y - 1.5) <= 2) return -1;
    } else {
        if (static_cast<int>(std::max(ax, az) + (std::min(ax, az) * 0.5)) < radius) return -1;
    }
    double max = -1.0;
    for (auto const& h : mMounds) {
        double v = h.compute(bx, by, bz);
        if (v > max) max = v;
    }
    return max;
}

// ---- lakes ------------------------------------------------------------------------------------------------

std::array<bool, 2048> lakeShape(RandomSource& random) {
    std::array<bool, 2048> b{};
    int                    n = random.nextInt(4) + 4;
    for (int j = 0; j < n; j++) {
        double d0 = random.nextDouble() * 6.0 + 3.0;
        double d1 = random.nextDouble() * 4.0 + 2.0;
        double d2 = random.nextDouble() * 6.0 + 3.0;
        double d3 = random.nextDouble() * (16.0 - d0 - 2.0) + 1.0 + d0 / 2.0;
        double d4 = random.nextDouble() * (8.0 - d1 - 4.0) + 2.0 + d1 / 2.0;
        double d5 = random.nextDouble() * (16.0 - d2 - 2.0) + 1.0 + d2 / 2.0;
        for (int l = 1; l < 15; l++)
            for (int i1 = 1; i1 < 15; i1++)
                for (int j1 = 1; j1 < 7; j1++) {
                    double d6 = (static_cast<double>(l) - d3) / (d0 / 2.0);
                    double d7 = (static_cast<double>(j1) - d4) / (d1 / 2.0);
                    double d8 = (static_cast<double>(i1) - d5) / (d2 / 2.0);
                    if (d6 * d6 + d7 * d7 + d8 * d8 < 1.0) b[static_cast<std::size_t>((l * 16 + i1) * 8 + j1)] = true;
                }
    }
    return b;
}

bool lakeBorder(std::array<bool, 2048> const& s, int x, int y, int z) {
    auto at = [&](int ax, int ay, int az) { return s[static_cast<std::size_t>((ax * 16 + az) * 8 + ay)]; };
    return !at(x, y, z)
        && ((x < 15 && at(x + 1, y, z)) || (x > 0 && at(x - 1, y, z)) || (z < 15 && at(x, y, z + 1)) || (z > 0 && at(x, y, z - 1)) || (y < 7 && at(x, y + 1, z))
            || (y > 0 && at(x, y - 1, z)));
}

// ---- foundations ------------------------------------------------------------------------------------------

std::optional<Rot> wallVolumeRotation(RandomSource& random, int deltaX, int deltaZ, int xBound, int zBound) {
    bool west = deltaX == 0, north = deltaZ == 0, east = deltaX == xBound, south = deltaZ == zBound;
    if (!(west || north || east || south)) return std::nullopt;
    std::vector<Rot> list;
    if (north) list.push_back(Rot::None);
    if (east) list.push_back(Rot::Clockwise90);
    if (south) list.push_back(Rot::Clockwise180);
    if (west) list.push_back(Rot::Counterclockwise90);
    // Util.shuffle
    for (int j = static_cast<int>(list.size()); j > 1; j--) {
        int k = random.nextInt(j);
        std::swap(list[static_cast<std::size_t>(j - 1)], list[static_cast<std::size_t>(k)]);
    }
    // Util.getRandom
    return list[static_cast<std::size_t>(random.nextInt(static_cast<int>(list.size())))];
}

} // namespace gf::core::minorlandmarks

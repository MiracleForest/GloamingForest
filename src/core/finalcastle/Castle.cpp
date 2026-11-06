// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleMural.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleBridgeComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/DestroyArea.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/finalcastle/Castle.h"

#include <algorithm>

#include "core/jrand/JavaOrder.h"
#include "core/jrand/Mth.h"

namespace gf::core::finalcastle {

using mth::ladd;
using mth::lmul;

std::int64_t decoSeed(std::int64_t seed, int minX, int minZ) {
    return ladd(seed, lmul(minX, 321534781LL)) ^ lmul(minZ, 756839LL);
}

std::int64_t thornBranchSeed(std::int64_t seed, int x, int y, int z) {
    return ladd(seed, lmul(x, 321534781LL)) ^ ladd(lmul(y, 756839LL), z);
}

int bridgeArchDepth(int l3, int i) {
    float angle = static_cast<float>(l3 - i) / static_cast<float>(l3) * 1.6F;
    return l3 - static_cast<int>(jrand::mthCos(angle) * static_cast<float>(l3));
}

// ---- FinalCastleMural -------------------------------------------------------------------------------------

Mural::Mural(int width, int height, std::int64_t seed) : mWidth(width), mHeight(height), mMural(static_cast<std::size_t>(width * height), 0) {
    generateMural(seed);
}

Mural::Mural(int width, int height, std::vector<std::int8_t> bytes) : mWidth(width), mHeight(height) {
    if (width != 0 && height != 0) mMural = std::move(bytes);
    // a short byte array would throw in Java on access: pad it so get / set stay in bounds
    if (mMural.size() < static_cast<std::size_t>(std::max(0, width * height))) mMural.resize(static_cast<std::size_t>(width * height), 0);
}

std::int8_t Mural::get(int x, int y) const {
    int index = x * mHeight + y;
    if (index < 0 || index >= static_cast<int>(mMural.size())) return 0;
    return mMural[static_cast<std::size_t>(index)];
}

void Mural::set(int x, int y, std::int8_t value) {
    int index = x * mHeight + y;
    if (index < 0 || index >= static_cast<int>(mMural.size())) return;
    mMural[static_cast<std::size_t>(index)] = value;
}

void Mural::generateMural(std::int64_t seed) {
    jrand::LegacyRandom decoRNG(seed);
    int                 startX = mWidth / 2 - 1;
    int                 startY = 2;
    // make mural, fill in dot by start
    for (int x = -1; x < 2; x++)
        for (int y = -1; y < 2; y++) set(startX + x, startY + y, 1);
    // side branches (Java evaluates the arguments left to right: the length is drawn first)
    makeHorizontalTree(decoRNG, startX + 1, startY, decoRNG.nextInt(mWidth / 6) + mWidth / 6, true);
    makeHorizontalTree(decoRNG, startX - 1, startY, decoRNG.nextInt(mWidth / 6) + mWidth / 6, false);
    // main tree
    makeVerticalTree(decoRNG, startX, startY + 1, decoRNG.nextInt(mHeight / 6) + mHeight / 6, true);
    // stripes
    makeStripes(decoRNG);
}

void Mural::makeHorizontalTree(jrand::RandomSource& rand, int centerX, int centerY, int branchLength, bool positive) {
    fillHorizontalLine(centerX, centerY, branchLength, positive);
    makeHorizontalBranch(rand, centerX, centerY, branchLength, positive, true);
    makeHorizontalBranch(rand, centerX, centerY, branchLength, positive, false);
}

void Mural::makeVerticalTree(jrand::RandomSource& rand, int centerX, int centerY, int branchLength, bool positive) {
    fillVerticalLine(centerX, centerY, branchLength, positive);
    makeVerticalBranch(rand, centerX, centerY, branchLength, positive, true);
    makeVerticalBranch(rand, centerX, centerY, branchLength, positive, false);
}

bool Mural::makeHorizontalBranch(jrand::RandomSource& rand, int sx, int sy, int length, bool plusX, bool plusY) {
    int downLine     = (length / 2) + 1 + rand.nextInt(std::max(length / 2, 2));
    int branchLength = rand.nextInt(mWidth / 8) + mWidth / 8;
    // check if the area is clear
    bool clear = true;
    for (int i = 0; i <= branchLength; i++) {
        int cx = sx + (plusX ? downLine - 1 + i : -(downLine - 1 + i));
        int cy = sy + (plusY ? 2 : -2);
        if (cx < 0 || cx >= mWidth || cy < 0 || cy >= mHeight || get(cx, cy) > 0) {
            clear = false;
            break;
        }
    }
    if (!clear) return false;
    int bx = sx + (plusX ? downLine : -downLine);
    int by = sy;
    // jag
    fillVerticalLine(bx, by, 1, plusY);
    by += (plusY ? 2 : -2);
    fillHorizontalLine(bx, by, branchLength, plusX);
    // recurse?
    if (bx > 0 && bx < mWidth && by > 0 && by < mHeight) {
        for (int n = 0; n < 4; n++)
            if (makeHorizontalBranch(rand, bx, by, branchLength, plusX, true)) break;
        for (int n = 0; n < 4; n++)
            if (makeHorizontalBranch(rand, bx, by, branchLength, plusX, false)) break;
    }
    return true;
}

bool Mural::makeVerticalBranch(jrand::RandomSource& rand, int sx, int sy, int length, bool plusY, bool plusX) {
    int downLine     = (length / 2) + 1 + rand.nextInt(std::max(length / 2, 2));
    int branchLength = rand.nextInt(mHeight / 8) + mHeight / 8;
    // check if the area is clear
    bool clear = true;
    for (int i = 0; i <= branchLength; i++) {
        int cx = sx + (plusX ? 2 : -2);
        int cy = sy + (plusY ? downLine - 1 + i : -(downLine - 1 + i));
        if (cx < 0 || cx >= mWidth || cy < 0 || cy >= mHeight || get(cx, cy) > 0) {
            clear = false;
            break;
        }
    }
    if (!clear) return false;
    int bx = sx;
    int by = sy + (plusY ? downLine : -downLine);
    // jag
    fillHorizontalLine(bx, by, 1, plusX);
    bx += (plusX ? 2 : -2);
    fillVerticalLine(bx, by, branchLength, plusY);
    // recurse?
    if (bx > 0 && bx < mWidth && by > 0 && by < mHeight) {
        for (int n = 0; n < 4; n++)
            if (makeVerticalBranch(rand, bx, by, branchLength, plusY, true)) break;
        for (int n = 0; n < 4; n++)
            if (makeVerticalBranch(rand, bx, by, branchLength, plusY, false)) break;
    }
    return true;
}

void Mural::fillHorizontalLine(int sx, int sy, int length, bool positive) {
    int x = sx;
    int y = sy;
    for (int i = 0; i <= length; i++) {
        if (x >= 0 && x < mWidth && y >= 0 && y < mHeight) {
            set(x, y, positive ? 1 : 4);
            x += positive ? 1 : -1;
        }
    }
}

void Mural::fillVerticalLine(int sx, int sy, int length, bool positive) {
    int x = sx;
    int y = sy;
    for (int i = 0; i <= length; i++) {
        if (x >= 0 && x < mWidth && y >= 0 && y < mHeight) {
            set(x, y, positive ? 2 : 3);
            y += positive ? 1 : -1;
        }
    }
}

void Mural::makeStripes(jrand::RandomSource& rand) {
    // stagger slightly on our way down
    for (int y = mHeight - 2; y > mHeight / 3; y -= (2 + rand.nextInt(2))) makeSingleStripe(y);
}

void Mural::makeSingleStripe(int y) {
    for (int x = 0; x < mWidth - 2; x++) {
        if (get(x + 1, y) == 0 && get(x + 1, y + 1) == 0) set(x, y, 1);
        else break;
    }
    for (int x = mWidth - 1; x > 2; x--) {
        if (get(x - 1, y) == 0 && get(x - 1, y + 1) == 0) set(x, y, 1);
        else break;
    }
}

// ---- DestroyArea ------------------------------------------------------------------------------------------

DestroyArea::DestroyArea(BlockBox const& tower, jrand::RandomSource& rand, int y) {
    // make a 4x4 area that's entirely within the tower bounding box
    int bx = tower.minX - 2 + rand.nextInt(tower.maxX - tower.minX + 1);
    int bz = tower.minZ - 2 + rand.nextInt(tower.maxZ - tower.minZ + 1);
    box    = BlockBox::of(bx, y - 10, bz, bx + 4, y, bz + 4);
}

bool DestroyArea::intersectsWith(DestroyArea const& other) const {
    // BoundingBox#intersects(int minX, int minZ, int maxX, int maxZ), TF's arguments
    int minX = other.box.maxX + 1;
    int minZ = other.box.minX - 1;
    int maxX = other.box.maxZ + 1;
    int maxZ = other.box.minZ - 1;
    return box.maxX >= minX && box.minX <= maxX && box.maxZ >= minZ && box.minZ <= maxZ;
}

std::optional<DestroyArea> DestroyArea::createNonIntersecting(BlockBox const& tower, jrand::RandomSource& rand, int y,
                                                              std::vector<std::optional<DestroyArea>> const& others) {
    std::optional<DestroyArea> area;
    for (int i = 0; i < 100 && !area; i++) {
        DestroyArea test(tower, rand, y);
        if (others.empty()) area = test;
        else
            for (auto const& other : others)
                if (!other || !test.intersectsWith(*other)) area = test;
    }
    return area;
}

} // namespace gf::core::finalcastle

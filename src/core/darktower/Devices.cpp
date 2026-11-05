// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/entity/CarminiteBuilderBlockEntity.java
//   src/main/java/twilightforest/block/entity/CarminiteReactorBlockEntity.java
//   src/main/java/twilightforest/block/entity/AntibuilderBlockEntity.java
//   src/main/java/twilightforest/block/SliderBlock.java
//   src/main/java/twilightforest/entity/boss/UrGhast.java
//   src/main/java/twilightforest/client/renderer/entity/CarminiteGhastRenderer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/darktower/Devices.h"

#include <algorithm>
#include <cmath>

namespace gf::core::darktower {

std::array<int, 3> offsetOf(Facing f) {
    switch (f) {
    case Facing::Down: return {0, -1, 0};
    case Facing::Up: return {0, 1, 0};
    case Facing::North: return {0, 0, -1};
    case Facing::South: return {0, 0, 1};
    case Facing::West: return {-1, 0, 0};
    case Facing::East: return {1, 0, 0};
    }
    return {0, 0, 0};
}

Facing builderFacing(float xRot, Facing horizontal) {
    int pitch = static_cast<int>(std::floor(xRot * 4.0f / 360.0f + 1.5)) & 3;
    if (pitch == 0) return Facing::Up;
    if (pitch == 2) return Facing::Down;
    return horizontal;
}

Facing horizontalFacing(float yRot) {
    // Direction.fromYRot: from2DDataValue(Mth.floor(yRot / 90 + 0.5) & 3), 0 south, 1 west, 2 north, 3 east
    int v = static_cast<int>(std::floor(yRot / 90.0 + 0.5)) & 3;
    static constexpr Facing k[4] = {Facing::South, Facing::West, Facing::North, Facing::East};
    return k[v];
}

void BuilderState::startBuilding(std::array<int, 3> const& self) {
    makingBlocks = true;
    resetStats(self);
}

void BuilderState::resetStats(std::array<int, 3> const& self) {
    blocksMade     = 0;
    cursor         = self;
    hasCursor      = true;
    ticksStopped   = 0;
    blockedCounter = 0;
}

BuilderTick tickBuilder(
    BuilderState&                                         s,
    std::function<bool()> const&                          findPlayer,
    std::function<bool(Facing&)> const&                   facing,
    std::function<bool(std::array<int, 3> const&)> const& isEmpty
) {
    BuilderTick out;
    if (s.makingBlocks) {
        if (!s.tracking) s.tracking = findPlayer();
        Facing next{};
        bool   hasNext = s.tracking && facing(next);
        ++s.ticksRunning;
        if (s.ticksRunning % 10 == 0 && s.hasCursor && hasNext) {
            auto o  = offsetOf(next);
            std::array<int, 3> at{s.cursor[0] + o[0], s.cursor[1] + o[1], s.cursor[2] + o[2]};
            if (s.blocksMade <= kBuilderRange && isEmpty(at)) {
                out.placed = true;
                out.at     = at;
                s.cursor   = at;
                s.blockedCounter = 0;
                s.blocksMade++;
            } else {
                s.blockedCounter++;
            }
        }
        if (s.blockedCounter > 0) {
            s.makingBlocks = false;
            s.tracking     = false;
            s.ticksStopped = 0;
        }
    } else {
        s.tracking = false;
        if (++s.ticksStopped == 60) out.timeout = true;
    }
    return out;
}

int sliderTickDelay(std::int64_t gameTime, int delay) {
    return static_cast<int>(80 - (gameTime - static_cast<std::int64_t>(delay) * 20) % 80);
}

std::array<FakeBlock, 26> const& reactorFakeBlocks() {
    static constexpr std::array<FakeBlock, 26> k{{
        {1, 1, 1, true},    {1, 1, -1, true},   {-1, 1, 1, true},  {-1, 1, -1, true}, {0, 1, 0, true},
        {0, 1, 1, false},   {0, 1, -1, false},  {1, 1, 0, false},  {-1, 1, 0, false},
        {1, 0, 1, false},   {1, 0, -1, false},  {-1, 0, 1, false}, {-1, 0, -1, false},
        {0, 0, 1, true},    {0, 0, -1, true},   {1, 0, 0, true},   {-1, 0, 0, true},  {0, -1, 0, true},
        {1, -1, 1, true},   {1, -1, -1, true},  {-1, -1, 1, true}, {-1, -1, -1, true},
        {0, -1, 1, false},  {0, -1, -1, false}, {1, -1, 0, false}, {-1, -1, 0, false},
    }};
    return k;
}

std::vector<BlobBlock> reactorBlob(int rad, int fuzz) {
    std::vector<BlobBlock> out;
    for (int dx = 0; dx <= rad; dx++) {
        int fuzzX = (fuzz + dx) % 8;
        for (int dy = 0; dy <= rad; dy++) {
            int fuzzY = (fuzz + dy) % 8;
            for (int dz = 0; dz <= rad; dz++) {
                auto part = [](int a, int b) {
                    return static_cast<int>(static_cast<std::int8_t>(static_cast<int>(std::max(a, b) * 0.5 + std::min(a, b) * 0.25)));
                };
                int dist;
                if (dx >= dy && dx >= dz) dist = static_cast<std::int8_t>(dx + part(dy, dz));
                else if (dy >= dx && dy >= dz) dist = static_cast<std::int8_t>(dy + part(dx, dz));
                else dist = static_cast<std::int8_t>(dz + part(dx, dy));
                if (dist == rad && !(dx == 0 && dy == 0 && dz == 0)) {
                    switch (fuzzX) {
                    case 0: out.push_back({dx, dy, dz, fuzzY}); break;
                    case 1: out.push_back({dx, dy, -dz, fuzzY}); break;
                    case 2: out.push_back({-dx, dy, dz, fuzzY}); break;
                    case 3: out.push_back({-dx, dy, -dz, fuzzY}); break;
                    case 4: out.push_back({dx, -dy, dz, fuzzY}); break;
                    case 5: out.push_back({dx, -dy, -dz, fuzzY}); break;
                    case 6: out.push_back({-dx, -dy, dz, fuzzY}); break;
                    case 7: out.push_back({-dx, -dy, -dz, fuzzY}); break;
                    default: break;
                    }
                }
            }
        }
    }
    return out;
}

std::vector<BlobCall> reactorBlobCalls(int counter) {
    std::vector<BlobCall> out;
    if (counter % 5 != 0) return out;
    int const offset  = 10;
    int       primary = counter - 80;
    if (primary >= offset && primary <= 249) out.push_back({0, (primary - offset) / 40, false, primary - offset, false});
    if (primary <= 200) out.push_back({0, primary / 40, true, counter, false});
    int secondary = counter - 120;
    if (secondary >= offset && secondary <= 129) out.push_back({1, (secondary - offset) / 40, false, secondary - offset, false});
    if (secondary >= 0 && secondary <= 160) out.push_back({1, secondary / 40, false, secondary, true});
    int tertiary = counter - 160;
    if (tertiary >= offset && tertiary <= 129) out.push_back({2, (tertiary - offset) / 40, false, tertiary - offset, false});
    if (tertiary >= 0 && tertiary <= 160) out.push_back({2, tertiary / 40, false, tertiary, true});
    return out;
}

ReactorBursts reactorBursts(RandomSource& rand) {
    ReactorBursts b;
    for (int i = 0; i < 3; ++i) b.secondary[i] = 3 * (rand.nextBoolean() ? 1 : -1);
    for (int i = 0; i < 3; ++i) b.tertiary[i] = 3 * (rand.nextBoolean() ? 1 : -1);
    if (b.secondary == b.tertiary)
        for (auto& v : b.tertiary) v = -v;
    return b;
}

std::array<int, 3> antibuilderOffset(int index) {
    int const d = kAntibuilderDiameter;
    return {index / (d * d) - kAntibuilderRadius, (index / d) % d - kAntibuilderRadius, index % d - kAntibuilderRadius};
}

std::vector<AntiRevert> tickAntibuilder(
    AntibuilderState&                    s,
    bool                                 playerInRange,
    bool                                 areaLoaded,
    std::function<AntiBlock(int)> const& read,
    RandomSource&                        rand
) {
    std::vector<AntiRevert> out;
    if (!playerInRange) {
        s.snapshot.clear();
        s.tickCount = 0;
        return out;
    }
    s.tickCount++;
    int const n = kAntibuilderDiameter * kAntibuilderDiameter * kAntibuilderDiameter;
    if (s.snapshot.empty() && areaLoaded) {
        s.snapshot.resize(n);
        for (int i = 0; i < n; ++i) s.snapshot[i] = read(i);
        s.slowScan = true;
    }
    if (!s.snapshot.empty() && (!s.slowScan || s.tickCount % 20 == 0)) {
        bool reverted = false;
        for (int i = 0; i < n; ++i) {
            AntiBlock there = read(i);
            AntiBlock& was  = s.snapshot[i];
            if (was.id == there.id) continue;
            // revertBlock(stateThere, replaceWith)
            bool revertible;
            if (there.air && !was.blocksMotion) revertible = false;
            else if (there.unbreakable || there.ignored || was.ignored) revertible = false;
            else {
                revertible = true;
                if (rand.nextInt(kAntibuilderRevertChance) == 0) out.push_back({i, !was.air, there.air});
            }
            if (revertible) reverted = true;
            else was = there;
        }
        if (reverted) {
            s.slowScan         = false;
            s.ticksSinceChange = 0;
        } else {
            s.ticksSinceChange++;
            if (s.ticksSinceChange > 20) s.slowScan = true;
        }
    }
    return out;
}

bool UrGhastPhase::afterHurt(float lost, bool freshHurt, bool dying, bool trapped) {
    if (freshHurt && !dying && !trapped) {
        damageUntilNextPhase -= lost;
        if (damageUntilNextPhase <= 0) {
            setTantrum(!tantrum);
            return true;
        }
    }
    return false;
}

Squash ghastSquash(float scale, float attackTimer) {
    float v = attackTimer / 20.0f;
    if (v < 0.0f) v = 0.0f;
    v = 1.0f / (v * v * v * v * v * 2.0f + 1.0f);
    return {(scale + 1.0f / v) / 2.0f, (scale + v) / 2.0f};
}

} // namespace gf::core::darktower

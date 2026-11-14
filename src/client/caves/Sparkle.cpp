// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/TrollsteinnBlock.java (animateTick, sparkle)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U27 on the client (client mod): the trollsteinn's purple sparkle. Java's client runs
// ClientLevel#animateTick every tick around the local player (667 samples at radius 16 and 667 at
// radius 32, each axis nextInt(r) - nextInt(r)); a sampled trollsteinn sparkles half the time: a
// purple dust speck (0.25, 0, 0.5, size 1) just outside each face whose neighbour is not a solid
// cube and whose light is 7 or less (RedstoneOreBlock#spawnParticles).
#ifdef GF_CLIENT

#include <cmath>
#include <cstdint>
#include <random>

#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BrightnessPair.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/fx/FxInternal.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::caves {

namespace {

constexpr char kTrollsteinn[] = "gloamingforest:trollsteinn";
constexpr char kSparkle[]     = "gloamingforest:caves_trollsteinn_sparkle";

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int    nextInt(int bound) { return static_cast<int>(rng()() % static_cast<unsigned>(bound)); }
bool   nextBoolean() { return (rng()() & 1u) != 0; }
double nextFloat() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

std::uint64_t gSparkles = 0;

/// Level#getMaxLocalRawBrightness on the client.
int rawBrightness(BlockSource const& region, BlockPos const& p) {
    auto pair = region.getBrightnessPair(p);
    return std::max(static_cast<int>(pair.block->mValue),
                    static_cast<int>(pair.sky->mValue) - gf::world::ext::skyDarken(const_cast<BlockSource&>(region)));
}

/// BlockState#isSolidRender (an opaque full cube).
bool solidRender(Block const& b) { return !b.isAir() && b.mDirectData->mIsOpaqueFullBlock; }

void sparkle(LocalPlayer& player, BlockSource const& region, BlockPos const& pos) {
    static auto* sEmpty = new gf::fx::MolangMap();
    static HashedString const name(kSparkle);
    // Direction.values(): down, up, north, south, west, east
    static constexpr int kDirs[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (auto const& d : kDirs) {
        BlockPos n{pos.x + d[0], pos.y + d[1], pos.z + d[2]};
        if (solidRender(region.getBlock(n)) || rawBrightness(region, n) > 7) continue;
        double x = d[0] != 0 ? 0.5 + 0.5625 * d[0] : nextFloat();
        double y = d[1] != 0 ? 0.5 + 0.5625 * d[1] : nextFloat();
        double z = d[2] != 0 ? 0.5 + 0.5625 * d[2] : nextFloat();
        player.getLevel().addParticleEffect(name, Vec3{static_cast<float>(pos.x + x), static_cast<float>(pos.y + y), static_cast<float>(pos.z + z)}, sEmpty->get());
        ++gSparkles;
    }
}

void animateTick() {
    auto* player = gf::client::currentPlayer();
    if (!player) return;
    auto const& region = player->getDimensionBlockSourceConst();
    auto const  pos    = player->getPosition();
    BlockPos    at{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    short       minY = region.getMinHeight(), maxY = region.getMaxHeight();
    auto        before = gSparkles;
    for (int j = 0; j < 667; ++j) {
        for (int range : {16, 32}) {
            int      dx = nextInt(range) - nextInt(range);
            int      dy = nextInt(range) - nextInt(range);
            int      dz = nextInt(range) - nextInt(range);
            BlockPos p{at.x + dx, at.y + dy, at.z + dz};
            if (p.y < minY || p.y >= maxY) continue;
            if (region.getBlock(p).getTypeName() != kTrollsteinn) continue;
            if (nextBoolean()) sparkle(*player, region, p);
        }
    }
    if (before == 0 && gSparkles > 0) log().info("caves: the client draws the trollsteinn sparkles");
}

class SparkleModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        gf::client::movement::onLocalTick(0, [](gf::client::movement::MoveState&) { animateTick(); });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::caves

GF_REGISTER_MODULE(gf::client::caves::SparkleModule);

#endif

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/OminousCandleBlock.java
//   src/main/java/gloamingforest/block/entity/OminousCandleBlockEntity.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the 17 ominous candles. An empty hand turns one back into the unlit vanilla
// candle of the same count; the matching vanilla candle adds one. The block entity only stores the
// renderer's flame heights (client-side, OminousCandleBlock.getCurrentY); the flames and smoke are
// client-side animate ticks. Drops are the imported loot tables (the vanilla candle x count).
//
// Bedrock: the vanilla candle's `candles` state is the count minus one (0..3); the ominous
// candle's gloamingforest:candles is the count (1..4).

#include <algorithm>
#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/lichdecor/LightablesInternal.h"
#include "game/Blocks.h"

namespace gf::feature::lichdecor::light {

namespace {

/// CandleBlock.PARTICLE_OFFSETS (AbstractCandleBlock#extinguish's smoke).
std::vector<Vec3> candleOffsets(int n) {
    switch (n) {
    case 1: return {{0.5F, 0.5F, 0.5F}};
    case 2: return {{0.375F, 0.44F, 0.5F}, {0.625F, 0.5F, 0.44F}};
    case 3: return {{0.5F, 0.313F, 0.625F}, {0.375F, 0.44F, 0.5F}, {0.56F, 0.5F, 0.44F}};
    case 4: return {{0.44F, 0.313F, 0.56F}, {0.625F, 0.44F, 0.56F}, {0.375F, 0.44F, 0.375F}, {0.56F, 0.5F, 0.375F}};
    default: return {};
    }
}

ia::Result useOminousCandle(ia::UseOnContext const& c) {
    int color = ominousColorOf(c.blockId);
    if (color < 0 || !mayBuild(c.player)) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    BlockPos    pos     = game::blockPos(c.pos);
    int         candles = std::clamp(intStateOr(c.block, "candles", 1), 1, 4);
    std::string vanilla = vanillaCandle(color);
    if (c.itemId.empty()) {
        if (!c.firstEvent) return ia::Result::Consume;
        auto const* nb = game::blockWithStates(vanilla, Json{{"candles", candles - 1}, {"lit", false}});
        if (!nb) return ia::Result::Pass;
        game::setBlock(*region, pos, *nb);
        extinguishFx(*region, pos, candleOffsets(candles));
        return ia::Result::Consume;
    }
    if (c.itemId == vanilla && candles < 4) {
        if (!c.firstEvent) return ia::Result::Consume;
        setStates(*region, pos, c.block, Json{{"candles", candles + 1}});
        // the candle sound type's place sound at (volume + 1) / 2, pitch * 0.8
        playSound(*region, centre(pos), "minecraft:block.candle.place", 1.0F, 0.8F);
        consumeOne(c.player, c.item);
        return ia::Result::Consume;
    }
    return ia::Result::Pass;
}

} // namespace

void registerOminousCandles() {
    for (int color = 0; color <= 16; ++color) ia::onUseOn({.block = ominousCandle(color)}, useOminousCandle);
}

/// OminousCandleBlock.getCurrentY: the flame's bob (the renderer's candle height).
double ominousCandleY(std::int64_t ticks, BlockPos const& pos, int candle) {
    double time = static_cast<double>(ticks) * 0.014;
    double x    = std::sin(time * 7.0 + (pos.x * candle));
    double y    = std::sin(time * 4.2 + (pos.y * candle));
    double z    = std::sin(time * 3.0 + (pos.z * candle));
    return (((x + y + z) / 4.0) * 0.5 + 0.5) * 0.25;
}

std::vector<AnimateFlame> animateFlames(Block const& block, BlockPos const& pos, std::int64_t gameTime) {
    std::vector<AnimateFlame> out;
    auto const&               id = block.getTypeName();
    if (ominousColorOf(id) >= 0) {
        // OminousCandleBlock.CANDLE_OFFSETS, always burning
        static constexpr float kXz[5][4][2] = {{},
                                               {{0.5F, 0.5F}},
                                               {{0.375F, 0.5F}, {0.625F, 0.44F}},
                                               {{0.5F, 0.625F}, {0.375F, 0.5F}, {0.56F, 0.44F}},
                                               {{0.44F, 0.56F}, {0.625F, 0.56F}, {0.375F, 0.375F}, {0.56F, 0.375F}}};
        int n = std::clamp(intStateOr(block, "candles", 1), 0, 4);
        for (int i = 0; i < n; ++i)
            out.push_back({Vec3{kXz[n][i][0], static_cast<float>(ominousCandleY(gameTime, pos, i + 1) + 0.5), kXz[n][i][1]}, "gloamingforest:ominous_flame", true});
        return out;
    }
    auto lighting = lightingOf(block);
    if (!lighting || *lighting == Lighting::None) return out;
    char const* particle = *lighting == Lighting::Dim ? "gloamingforest:dim_flame" : *lighting == Lighting::Ominous ? "gloamingforest:ominous_flame" : "minecraft:small_flame";
    bool        smoke    = *lighting == Lighting::Normal;
    std::vector<Vec3> offsets;
    if (id == kCandelabra) offsets = candelabraFlameOffsets(block);
    else if (auto kind = skullCandleKind(id)) offsets = skullCandleFlameOffsets(block, kind->wall);
    for (auto const& o : offsets) out.push_back({o, particle, smoke});
    return out;
}

} // namespace gf::feature::lichdecor::light

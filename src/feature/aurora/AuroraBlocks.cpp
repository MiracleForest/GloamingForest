// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/AuroraBrickBlock.java (getDestroyProgress)
//   src/main/java/twilightforest/client/model/block/variant/NoiseVaryingModel.java (the variant choice)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The aurora block: TF draws one of its 16 animated textures per position (NoiseVaryingModel with
// SimplexNoiseHelper#calcVariant). Bedrock materials cannot vary by position, so the block has a
// GloamingForest state `gloamingforest:variant` (0..15, one permutation per texture, F1's animated
// atlas tiles); every aurora block the palace or a player places gets calcVariant of its position.
// With progress_glacier a player mines it in 10 ticks (getDestroyProgress 0.1 per tick).
#include "feature/aurora/Structures.h"

#include <array>
#include <map>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "core/aurora/AuroraColor.h"
#include "feature/common/Interactions.h"
#include "feature/ext/aurora/W0Internals.h"
#include "feature/families/BlockEvents.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"

namespace gf::feature::aurora {

namespace fe = gf::feature::entity;

namespace {

constexpr char kAuroraBlock[] = "twilightforest:aurora_block";
constexpr char kVariant[]     = "gloamingforest:variant";

/// The 16 permutations (resolved once the block registry exists).
std::array<Block const*, 16> const& variants() {
    static std::array<Block const*, 16> v{};
    static bool                         resolved = false;
    if (!resolved) {
        bool all = true;
        for (int i = 0; i < 16; i++) {
            v[static_cast<std::size_t>(i)] = gf::game::blockWith(kAuroraBlock, {{kVariant, i}});
            all                            = all && v[static_cast<std::size_t>(i)] != nullptr;
        }
        resolved = all;
    }
    return v;
}

/// Whether `block` is an aurora block that still has the default variant.
bool isPlainAuroraBlock(Block const& block) {
    auto const& v = variants();
    if (&block == v[0]) return true;
    return gf::game::blockId(block) == kAuroraBlock && gf::game::intState(block, kVariant).value_or(0) == 0;
}

// ---- AuroraBrickBlock#getDestroyProgress ------------------------------------------------------------

struct Mining {
    BlockPos     pos;
    int          face        = 0;
    int          ticks       = 0;
    bool         progressed  = false; ///< the server saw destroy progress (server-authoritative breaking)
};
std::map<std::int64_t, Mining>& mining() {
    static std::map<std::int64_t, Mining> m;
    return m;
}

void tickMining() {
    for (auto it = mining().begin(); it != mining().end();) {
        auto* actor = fe::actorById(it->first);
        auto& m     = it->second;
        if (!actor || !actor->isPlayer() || !actor->isAlive()) {
            it = mining().erase(it);
            continue;
        }
        auto& player   = static_cast<Player&>(*actor);
        auto* gameMode = player.mGameMode.get();
        if (!gameMode) {
            it = mining().erase(it);
            continue;
        }
        // the player stopped (stopDestroyBlock resets the progress) or turned to another block:
        // Java only speeds up continuous mining
        BlockPos const& at       = gameMode->mDestroyBlockPos;
        float const     progress = gameMode->mDestroyProgress;
        if (progress > 0.0F) m.progressed = true;
        bool stopped = !(at == m.pos) || (m.progressed && progress <= 0.0F);
        if (stopped && m.ticks > 0) {
            it = mining().erase(it);
            continue;
        }
        // 0.1 per tick: the block breaks on the tenth tick of mining
        if (++m.ticks >= 10) {
            auto& region = player.getDimensionBlockSource();
            if (gf::game::blockId(region.getBlock(m.pos)) == kAuroraBlock) {
                auto   eye = player.getEyePos();
                double dx = m.pos.x + 0.5 - eye.x, dy = m.pos.y + 0.5 - eye.y, dz = m.pos.z + 0.5 - eye.z;
                if (dx * dx + dy * dy + dz * dz <= 36.0) gameMode->destroyBlock(m.pos, static_cast<uchar>(m.face));
            }
            it = mining().erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace

Block const* auroraBlockAt(int x, int y, int z) {
    auto const& v = variants();
    return v[static_cast<std::size_t>(core::aurora::calcVariant(x, y, z, 16) & 15)];
}

void applyAuroraVariants(structure::Region& region, structure::BlockBox const& piece, structure::BlockBox const& sbb) {
    int const x0 = std::max(piece.minX, sbb.minX), x1 = std::min(piece.maxX, sbb.maxX);
    int const y0 = std::max({piece.minY, sbb.minY, region.minY()}), y1 = std::min({piece.maxY, sbb.maxY, region.maxY() - 1});
    int const z0 = std::max(piece.minZ, sbb.minZ), z1 = std::min(piece.maxZ, sbb.maxZ);
    if (x0 > x1 || y0 > y1 || z0 > z1 || !variants()[15]) return;
    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                BlockPos pos{x, y, z};
                if (!isPlainAuroraBlock(region.getBlock(pos))) continue;
                auto const* b = auroraBlockAt(x, y, z);
                if (b && b != variants()[0]) region.setBlock(pos, *b, 2);
            }
}

bool isMiningAuroraBlockForTest(Player const& player) { return mining().contains(player.getOrCreateUniqueID().rawID); }

void installAuroraBlocks() {
    // a placed aurora block takes its position's variant (setPlacedBy; the model has no state in TF)
    gf::blockevents::onPlaced(kAuroraBlock, [](BlockSource& region, BlockPos const& pos, Block const& block, Actor*) {
        if (!isPlainAuroraBlock(block)) return;
        auto const* b = auroraBlockAt(pos.x, pos.y, pos.z);
        if (b && b != &block && b != variants()[0]) gf::game::setBlock(region, pos, *b);
    });
    gf::interactions::onStartDestroy({.block = kAuroraBlock}, [](gf::interactions::DestroyContext const& ctx) {
        // survival only (creative breaks at once; adventure cannot break it)
        if (ctx.player.getPlayerGameType() != GameType::Survival) return gf::interactions::Result::Pass;
        if (gf::progression::has(ctx.player, "twilightforest:progress_glacier"))
            mining()[ctx.player.getOrCreateUniqueID().rawID] = Mining{BlockPos{ctx.pos.x, ctx.pos.y, ctx.pos.z}, ctx.face};
        return gf::interactions::Result::Pass;
    });
    fe::onLevelTick(tickMining);
    gf::game::db::onWorldClosed([] { mining().clear(); });
}

} // namespace gf::feature::aurora

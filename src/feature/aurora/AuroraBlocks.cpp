// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/AuroraBrickBlock.java (getDestroyProgress)
//   src/main/java/twilightforest/client/model/block/variant/NoiseVaryingModel.java (the variant choice)
//   src/main/java/twilightforest/block/AuroralizedGlassBlock.java (getBeaconColorMultiplier)
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

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

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
#include "feature/ext/aurora/BeaconColor.h"
#include "feature/ext/aurora/W0Internals.h"
#include "feature/families/BlockEvents.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::aurora {

namespace fe = gf::feature::entity;

namespace {

constexpr char kAuroraBlock[] = "twilightforest:aurora_block";
constexpr char kVariant[]     = "gloamingforest:variant";

/// The block pointers of the current world. Pack blocks are registered per world (the client opens
/// many worlds in one process): reset when a world closes, resolved again on first use. Read by
/// chunk generation threads too: an immutable snapshot published through an atomic pointer.
struct BlockCache {
    std::array<Block const*, 16> variants{};
    Block const*                 java = nullptr; ///< F7's block of the aurora block (structures)
};
std::atomic<BlockCache const*>& published() {
    static std::atomic<BlockCache const*> p{nullptr};
    return p;
}
/// Every snapshot made (kept to the end of the process: a reader may still hold an old one).
std::vector<std::unique_ptr<BlockCache>>& snapshots() {
    static std::vector<std::unique_ptr<BlockCache>> v;
    return v;
}
std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}

BlockCache const* cache() {
    if (auto const* c = published().load(std::memory_order_acquire)) return c;
    std::lock_guard lock(cacheMutex());
    if (auto const* c = published().load(std::memory_order_acquire)) return c;
    auto next = std::make_unique<BlockCache>();
    for (int i = 0; i < 16; i++) {
        next->variants[static_cast<std::size_t>(i)] = gf::game::blockWith(kAuroraBlock, {{kVariant, i}});
        if (!next->variants[static_cast<std::size_t>(i)]) return nullptr; // the registry is not there yet
    }
    next->java = structure::blocks::fromJava(kAuroraBlock); // nullptr when unknown (never air)
    auto const* c = next.get();
    snapshots().push_back(std::move(next));
    published().store(c, std::memory_order_release);
    return c;
}

void resetCache() { published().store(nullptr, std::memory_order_release); }

/// The 16 permutations (all null until the block registry exists).
std::array<Block const*, 16> const& variants() {
    static std::array<Block const*, 16> const none{};
    auto const*                               c = cache();
    return c ? c->variants : none;
}

/// Whether `block` is an aurora block that still has the default variant.
bool isPlainAuroraBlock(Block const& block) {
    // pointers only: the scan of a piece's box asks this of every block
    auto const* c = cache();
    return c && (&block == c->variants[0] || (c->java && &block == c->java));
}
// ---- AuroraBrickBlock#getDestroyProgress ------------------------------------------------------------

struct Mining {
    BlockPos     pos;
    int          face        = 0;
    int          ticks       = 0;
    bool         progressed  = false; ///< the server saw destroy progress (server-authoritative breaking)
    int          idle        = 0;     ///< ticks without the engine's own progress since then
    float        written     = 0.0F;  ///< the progress this wrote last tick
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
        // (only once the server showed it tracks this mining: a client-authoritative break leaves
        // the game mode's destroy position and progress untouched, and then the timer decides)
        BlockPos const  at       = gameMode->mDestroyBlockPos;
        float const     progress = gameMode->mDestroyProgress;
        if (progress > 0.0F && at == m.pos) m.progressed = true;
        // stopDestroyBlock resets the progress; one tick at 0 can be the engine's own restart
        // (stopped: the engine reset it, or it no longer adds its own progress to what this wrote)
        m.idle       = (m.progressed && (progress <= 0.0F || (m.written > 0.0F && progress <= m.written + 1e-6F))) ? m.idle + 1 : 0;
        bool stopped = m.progressed && (!(at == m.pos) || m.idle >= 2);
        if (stopped && m.ticks > 0) {
            gf::log().info("aurora: aurora block mining stopped after {} ticks", m.ticks);
            it = mining().erase(it);
            continue;
        }
        if (!m.progressed && !gf::net::isCompanion(player) && m.ticks >= 3) {
            // no destroy progress on the server within 3 ticks: without it nothing tells a held button
            // from a tap, so a vanilla client's mining is not sped up (the engine's own break time
            // applies). A companion's client applies the 0.1 a tick itself (client/aurora).
            gf::log().info("aurora: aurora block mining not sped up: no server destroy progress");
            it = mining().erase(it);
            continue;
        }
        // 0.1 per tick: the block breaks on the tenth tick of mining
        ++m.ticks;
        if (m.progressed) {
            // server-authoritative breaking: the engine's own progress becomes TF's 0.1 a tick, so the
            // engine breaks the block itself (drops, sound, particles) when it reaches 1
            m.written                  = std::max(static_cast<float>(gameMode->mDestroyProgress), std::min(1.0F, 0.1F * static_cast<float>(m.ticks)));
            gameMode->mDestroyProgress = m.written;
            if (m.ticks <= 12) {
                ++it;
                continue;
            }
        }
        if (m.ticks >= 10 && !m.progressed && gf::net::isCompanion(player)) {
            // the companion's client breaks it itself (its own progress reaches 1 on this tick)
            if (m.ticks > 14) it = mining().erase(it);
            else ++it;
            continue;
        }
        if (m.ticks >= 10) {
            auto& region = player.getDimensionBlockSource();
            if (gf::game::blockId(region.getBlock(m.pos)) == kAuroraBlock) {
                auto   eye = player.getEyePos();
                double dx = m.pos.x + 0.5 - eye.x, dy = m.pos.y + 0.5 - eye.y, dz = m.pos.z + 0.5 - eye.z;
                bool ok = dx * dx + dy * dy + dz * dz <= 36.0 && gameMode->destroyBlock(m.pos, static_cast<uchar>(m.face));
                gf::log().info("aurora: aurora block mined in 10 ticks at {} {} {}: {}", m.pos.x, m.pos.y, m.pos.z, ok ? "broken" : "refused");
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
        // the next tick: a world edit inside the placement could be rolled back with it (R16)
        int const dim = gf::game::globalPos(region, pos).dim;
        gf::interactions::nextTick([dim, pos] {
            auto* r = gf::game::regionOf(dim);
            if (!r || !isPlainAuroraBlock(r->getBlock(pos))) return;
            auto const* b = auroraBlockAt(pos.x, pos.y, pos.z);
            if (b && b != variants()[0]) gf::game::setBlock(*r, pos, *b);
        });
    });
    gf::interactions::onStartDestroy({.block = kAuroraBlock}, [](gf::interactions::DestroyContext const& ctx) {
        // survival only (creative breaks at once; adventure cannot break it)
        if (ctx.player.getPlayerGameType() != GameType::Survival) return gf::interactions::Result::Pass;
        if (gf::progression::has(ctx.player, "twilightforest:progress_glacier"))
            mining()[ctx.player.getOrCreateUniqueID().rawID] = Mining{BlockPos{ctx.pos.x, ctx.pos.y, ctx.pos.z}, ctx.face};
        return gf::interactions::Result::Pass;
    });
    // one line per sped-up break (the client scenario's companion mining check reads it)
    gf::interactions::onDestroyed({.block = kAuroraBlock}, [](gf::interactions::DestroyContext const& ctx, bool destroyed) {
        auto it = mining().find(ctx.player.getOrCreateUniqueID().rawID);
        if (!destroyed || it == mining().end() || !(it->second.pos == BlockPos{ctx.pos.x, ctx.pos.y, ctx.pos.z})) return;
        gf::log().info("aurora: aurora block broken after {} ticks of mining ({})", it->second.ticks,
                       it->second.progressed ? "server progress" : (gf::net::isCompanion(ctx.player) ? "companion" : "no progress"));
    });
    fe::onLevelTick(tickMining);
    // AuroralizedGlassBlock#getBeaconColorMultiplier: the aurora colour of its position
    gf::feature::ext::aurora::beacon::registerColorMultiplier("twilightforest:auroralized_glass",
                                                             [](BlockPos const& p) { return core::aurora::auroraBlockColor(p.x, p.y, p.z); });
    gf::game::db::onWorldClosed([] {
        mining().clear();
        resetCache();
    });
}

} // namespace gf::feature::aurora

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/ColorHandler.java (aurora block colours)
//   src/main/java/gloamingforest/block/AuroraBrickBlock.java (getDestroyProgress, client side)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The client mod's aurora colours (U28): ColorHandler's per-position aurora block colour and the
// pale colour of the aurora pillar, slab and auroralized glass, through F12's tint registry. The
// blocks' definitions name baked textures without a tint method (vanilla clients draw upstream's texture
// times its item colour); the client mod swaps them for the untinted textures with a tint method
// when it resolves the materials (feature/ext/aurora/MaterialSwap), so the chunk builder asks F12's
// tint for the per-position colour. The block items' colours (ColorHandler#registerItemColors:
// the colour without a level, hue 0.45 and its pale form) need no client code: the blocks'
// minecraft:item_visual draws them with pre-tinted textures, the same on every client.
//
// AuroraBrickBlock#getDestroyProgress runs on both sides in Java: with progress_glacier the local
// player's own mining (the crack animation and the client's prediction of the break) advances 0.1
// a tick, as the server's (feature/aurora/AuroraBlocks.cpp) does.
#ifdef GF_CLIENT

#include <algorithm>
#include <cstdint>
#include <string>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Render.h"
#include "core/aurora/AuroraColor.h"
#include "feature/ext/aurora/MaterialSwap.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::aurora {

namespace {

/// The local player's continuous mining of one aurora block.
struct LocalMining {
    BlockPos pos{0, -10000, 0};
    int      ticks = 0;
    float    written = 0.0F;
};

void tickLocalMining() {
    static LocalMining m;
    auto*              player = gf::client::currentPlayer();
    if (!player || player->getPlayerGameType() != GameType::Survival || !gf::progression::client::has("gloamingforest:progress_glacier")) {
        m = {};
        return;
    }
    // another world or player: start over (the coordinates alone could repeat)
    static std::int64_t who = 0;
    if (auto uid = player->getOrCreateUniqueID().rawID; uid != who) {
        who = uid;
        m   = {};
    }
    auto* gameMode = player->mGameMode.get();
    if (!gameMode) return;
    BlockPos const at       = gameMode->mDestroyBlockPos;
    float const    progress = gameMode->mDestroyProgress;
    if (progress <= 0.0F || gf::game::blockId(player->getDimensionBlockSource().getBlock(at)) != "gloamingforest:aurora_block") {
        m = {};
        return;
    }
    // a new block, or the game restarted the progress (stopped and started again)
    if (!(at == m.pos) || progress + 1e-6F < m.written) m = LocalMining{at, 0, 0.0F};
    ++m.ticks;
    m.written                  = std::max(progress, std::min(1.0F, 0.1F * static_cast<float>(m.ticks)));
    gameMode->mDestroyProgress = m.written;
}

class AuroraClientModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        namespace ca = core::aurora;
        // the blocks' definitions name baked textures (upstream's texture times its item colour: what vanilla
        // clients draw); the client mod draws the untinted textures with the per-position colour below
        namespace mat = gf::feature::ext::aurora::materials;
        for (int n = 0; n < 16; n++) mat::swapTexture("gf_aurora_baked_aurora_block_" + std::to_string(n), "gf_block_aurora_block_" + std::to_string(n));
        for (char const* t : {"aurora_pillar", "aurora_pillar_top", "aurora_slab_side"}) mat::swapTexture(std::string("gf_aurora_baked_") + t, std::string("gf_block_") + t);
        tint::registerBlock("gloamingforest:aurora_block", [](BlockPos const& p) { return ca::auroraBlockColor(p.x, p.y, p.z); });
        for (char const* id : {"gloamingforest:aurora_pillar", "gloamingforest:aurora_slab", "gloamingforest:auroralized_glass"})
            tint::registerBlock(id, [](BlockPos const& p) { return ca::auroraPaleColor(ca::auroraBlockColor(p.x, p.y, p.z)); });
        gf::client::movement::onLocalTick(0, [](gf::client::movement::MoveState&) { tickLocalMining(); });
        gf::log().info("aurora: aurora block colours registered");
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::aurora

GF_REGISTER_MODULE(gf::client::aurora::AuroraClientModule);

#endif // GF_CLIENT

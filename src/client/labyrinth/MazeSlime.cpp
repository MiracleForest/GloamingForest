// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/MazeSlimeBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U23 on the client (client mod): the local player on a maze slime block. MazeSlimeBlock inherits
// net/minecraft/world/level/block/SlimeBlock's updateEntityAfterFallOn (bounceUp: the landing speed
// turned up again) and stepOn (the horizontal speed times 0.4 + |dy| * 0.2 while |dy| < 0.1), unless
// the player sneaks (isSuppressingBounce / isSteppingCarefully). Bedrock's movement is the client's,
// so the companion's client applies both itself; the server skips companions (feature/labyrinth/
// Blocks.cpp) and keeps its motion-packet bounce for vanilla players.
#ifdef GF_CLIENT

#include <cmath>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "mod/Module.h"

namespace gf::client::labyrinth {

namespace {

constexpr char const* kMazeSlimeBlock = "twilightforest:maze_slime_block";

struct State {
    bool  wasOnGround = true;
    float lastVy      = 0.0f; ///< the vertical speed the last tick moved with
};
State& st() {
    static State s;
    return s;
}

/// Entity#getBlockPosBelowThatAffectsMyMovement: the block 0.5 below the feet (stepOn uses
/// getOnPos, 0.2 below; both are the block under a player standing on a full block).
bool onMazeSlime(LocalPlayer const& player) {
    auto const& region = player.getDimensionBlockSourceConst();
    auto        feet   = player.getAABB().min;
    auto        c      = player.getPosition();
    BlockPos    below{static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(feet.y - 0.2f)), static_cast<int>(std::floor(c.z))};
    return region.getBlock(below).getTypeName() == kMazeSlimeBlock;
}

void tick(movement::MoveState& move) {
    auto* player = currentPlayer();
    auto& s      = st();
    if (!player) {
        s = State{};
        return;
    }
    bool landed = move.onGround && !s.wasOnGround;
    s.wasOnGround = move.onGround;
    if (move.onGround && !move.sneakHeld && onMazeSlime(*player)) {
        if (landed && s.lastVy < 0.0f) {
            // bounceUp: a living entity keeps all of its landing speed
            move.velocity.y = -s.lastVy;
        } else {
            // stepOn
            float d0 = std::fabs(move.velocity.y);
            if (d0 < 0.1f) {
                float d1 = 0.4f + d0 * 0.2f;
                move.velocity.x *= d1;
                move.velocity.z *= d1;
            }
        }
    }
    s.lastVy = move.velocity.y;
}

class LabyrinthClientModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        movement::onLocalTick(0, [](movement::MoveState& move) { tick(move); });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::labyrinth

GF_REGISTER_MODULE(gf::client::labyrinth::LabyrinthClientModule);

#endif // GF_CLIENT

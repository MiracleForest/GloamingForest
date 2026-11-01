// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFDimension.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "world" (feature/dimension/World.h) implemented by F4.

#include "feature/dimension/World.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/chrono/GameChrono.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/dimension/Internal.h"
#include "game/Blocks.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::dimension {

std::int64_t idOf(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Player* playerById(std::int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(ActorUniqueID{id});
}

namespace {
std::vector<portal::TeleportedFn>& teleportedListeners() {
    static std::vector<portal::TeleportedFn> v;
    return v;
}
} // namespace

void notifyTeleported(Player& player, int from, int to) {
    auto id        = idOf(player);
    auto listeners = teleportedListeners(); // a listener may register another
    for (auto const& fn : listeners) {
        auto* p = playerById(id);
        if (!p) return;
        fn(*p, DimensionType(from), DimensionType(to));
    }
}

void moveAcross(
    Player&                      player,
    int                          dim,
    Vec3 const&                  pos,
    float                        yaw,
    float                        pitch,
    std::function<void(Player&)> then,
    std::function<void(Player*)> failed
) {
    auto id = idOf(player);
    player.teleport(pos, DimensionType(dim), Vec2{pitch, yaw});
    ll::coro::keepThis([id, dim, pos, then = std::move(then), failed = std::move(failed)]() -> ll::coro::CoroTask<> {
        BlockPos at = game::blockPosOf(pos);
        for (int i = 0; i < 200; ++i) {
            co_await ll::chrono::ticks(1);
            auto* p = playerById(id);
            if (!p) {
                log().warn("dimension: player {} left before arriving in {}", id, dimensionName(dim));
                if (failed) failed(nullptr);
                co_return;
            }
            if (static_cast<int>(p->getDimensionId()) != dim) continue;
            auto* region = game::regionOf(dim);
            if (!region) continue;
            BlockPos probe{at.x, std::clamp(at.y, static_cast<int>(region->getMinHeight()), region->getMaxHeight() - 1), at.z};
            if (!region->hasChunksAt(probe, 2, false)) continue;
            if (then) then(*p);
            co_return;
        }
        auto* p = playerById(id);
        log().warn("dimension: {} did not arrive in {} within 10 s", p ? p->getRealName() : std::to_string(id), dimensionName(dim));
        if (failed) failed(p);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

} // namespace gf::feature::dimension

namespace gf::world {

namespace dim = feature::dimension;

std::optional<DimensionType> twilight() {
    int id = dim::twilightId();
    if (id < 0) return std::nullopt;
    return DimensionType(id);
}

bool isTwilight(Dimension const& dimension) { return dim::isTwilightDimension(dimension); }

bool isTwilight(DimensionType dimension) {
    int id = dim::twilightId();
    return id >= 0 && static_cast<int>(dimension) == id;
}

// Within a dimension the move is immediate. Across dimensions it is asynchronous on Bedrock (the
// client loads the dimension): true means the move was started; listeners of portal::onTeleported
// hear of it once the player is there, and a move that does not complete within 10 s is logged
// and told to the player.
bool teleport(Player& player, DimensionType dimension, Vec3 const& pos, float yaw) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    int to = static_cast<int>(dimension);
    if (!level->getDimension(dimension).lock() && !level->getOrCreateDimension(dimension).lock()) return false;
    int   from  = static_cast<int>(player.getDimensionId());
    float pitch = player.getRotation().x;
    if (from == to) {
        player.teleport(pos, dimension, Vec2{pitch, yaw});
        dim::notifyTeleported(player, from, to);
        return true;
    }
    dim::moveAcross(
        player,
        to,
        pos,
        yaw,
        pitch,
        [from, to](Player& p) { dim::notifyTeleported(p, from, to); },
        [to](Player* p) {
            if (p) p->sendMessage(trFor(*p, "gf.dimension.teleport_failed", "The teleport to {} did not complete.", dim::dimensionName(to)));
        }
    );
    return true;
}

double coordinateScale() { return 0.125; }

std::optional<long long> fixedTime() { return 13000; }

} // namespace gf::world

namespace gf::portal {

void onTeleported(TeleportedFn fn) { feature::dimension::teleportedListeners().push_back(std::move(fn)); }

} // namespace gf::portal

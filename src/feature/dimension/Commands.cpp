// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/command/TFTeleportCommand.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /gloamingforest tp (rule R14: subcommand owned by F4; upstream's functions tp_gloaming / tp_overworld
// are in Functions.cpp). A move to another dimension completes asynchronously: the success
// message is upstream's, a move that does not complete is reported to the player (gf::world::teleport)
// and logged.

#include <cstdio>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/dimension/Internal.h"
#include "feature/dimension/World.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::dimension {

namespace {

std::string format1(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

/// TFTeleportCommand#run.
void teleportCommand(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    auto* player = commands::playerOf(origin);
    if (!player) return output.error(tr("commands.tffeature.teleport.player_only", "Command must be run by a player."));
    auto gloaming = world::gloaming();
    auto tfHeight = gloaming ? heightOf(static_cast<int>(*gloaming)) : std::nullopt;
    if (!gloaming || !tfHeight) return output.error(tr("commands.tffeature.teleport.dimension_missing", "The Gloaming Forest dimension is unavailable."));
    auto here = heightOf(static_cast<int>(player->getDimensionId()));
    if (!here) return output.error(tr("commands.tffeature.teleport.dimension_missing", "The Gloaming Forest dimension is unavailable."));
    Vec3   pos        = player->getFeetPos();
    double yConverted = (pos.y - here->min) / static_cast<double>(here->max - here->min) * (tfHeight->max - tfHeight->min) + tfHeight->min;
    Vec3   teleportPos{pos.x, static_cast<float>(yConverted), pos.z};
    // isInWorldBounds
    int blockY = static_cast<int>(std::floor(teleportPos.y));
    if (blockY < tfHeight->min || blockY >= tfHeight->max || std::abs(pos.x) >= 30000000.0f || std::abs(pos.z) >= 30000000.0f) {
        return output.error(tr("commands.teleport.invalidPosition", "Invalid position for teleport"));
    }
    if (!world::teleport(*player, *gloaming, teleportPos, player->getRotation().y)) {
        return output.error(tr("commands.tffeature.teleport.dimension_missing", "The Gloaming Forest dimension is unavailable."));
    }
    output.success(trFor(
        *player,
        "commands.tffeature.teleport.success",
        "Teleported to The Gloaming Forest at %s %s %s",
        format1(teleportPos.x),
        format1(teleportPos.y),
        format1(teleportPos.z)
    ));
}

} // namespace

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("tp").execute([](CommandOrigin const& origin, CommandOutput& output) { teleportCommand(origin, output); });
});

} // namespace gf::feature::dimension

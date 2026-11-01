// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/command/TFTeleportCommand.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /twilightforest tp (rule R14: subcommand owned by F4), and the bodies of TF's data pack
// functions twilightforest:tp_twilight / tp_overworld ("execute in <dimension> run tp @p ~ ~ ~"),
// which Bedrock's /execute can't express for a custom dimension: /twilightforest tp twilight and
// /twilightforest tp overworld.

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
    auto twilight = world::twilight();
    auto tfHeight = twilight ? heightOf(static_cast<int>(*twilight)) : std::nullopt;
    if (!twilight || !tfHeight) return output.error(tr("commands.tffeature.teleport.dimension_missing", "The Twilight Forest dimension is unavailable."));
    auto here = heightOf(static_cast<int>(player->getDimensionId()));
    if (!here) return output.error(tr("commands.tffeature.teleport.dimension_missing", "The Twilight Forest dimension is unavailable."));
    Vec3   pos        = player->getFeetPos();
    double yConverted = (pos.y - here->min) / static_cast<double>(here->max - here->min) * (tfHeight->max - tfHeight->min) + tfHeight->min;
    Vec3   teleportPos{pos.x, static_cast<float>(yConverted), pos.z};
    // isInWorldBounds
    int blockY = static_cast<int>(std::floor(teleportPos.y));
    if (blockY < tfHeight->min || blockY >= tfHeight->max || std::abs(pos.x) >= 30000000.0f || std::abs(pos.z) >= 30000000.0f) {
        return output.error(tr("commands.teleport.invalidPosition", "Invalid position for teleport"));
    }
    world::teleport(*player, *twilight, teleportPos, player->getRotation().y);
    output.success(trFor(
        *player,
        "commands.tffeature.teleport.success",
        "Teleported to The Twilight Forest at %s %s %s",
        format1(teleportPos.x),
        format1(teleportPos.y),
        format1(teleportPos.z)
    ));
}

/// The data pack functions: the player moves to the same coordinates of another dimension.
void teleportSame(CommandOrigin const& origin, CommandOutput& output, bool toTwilight) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    auto* player = commands::playerOf(origin);
    if (!player) return output.error(tr("commands.tffeature.teleport.player_only", "Command must be run by a player."));
    std::optional<DimensionType> dest = toTwilight ? world::twilight() : std::optional<DimensionType>(DimensionType(0));
    if (!dest) return output.error(tr("commands.tffeature.teleport.dimension_missing", "The Twilight Forest dimension is unavailable."));
    Vec3 pos = player->getFeetPos();
    world::teleport(*player, *dest, pos, player->getRotation().y);
    output.success("Teleported " + player->getRealName() + " to " + dimensionName(static_cast<int>(*dest)));
}

} // namespace

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("tp").execute([](CommandOrigin const& origin, CommandOutput& output) { teleportCommand(origin, output); });
    cmd.overload().text("tp").text("twilight").execute([](CommandOrigin const& origin, CommandOutput& output) {
        teleportSame(origin, output, true);
    });
    cmd.overload().text("tp").text("overworld").execute([](CommandOrigin const& origin, CommandOutput& output) {
        teleportSame(origin, output, false);
    });
});

} // namespace gf::feature::dimension

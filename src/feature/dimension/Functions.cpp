// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/resources/data/gloamingforest/function/tp_gloaming.mcfunction
//   src/main/resources/data/gloamingforest/function/tp_overworld.mcfunction
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// upstream's data pack functions gloamingforest:tp_gloaming / tp_overworld are
//     execute in gloamingforest:gloaming_forest run tp @p ~ ~ ~
//     execute in minecraft:overworld run tp @p ~ ~ ~
// Java's `execute in` moves the execution position into the other dimension scaled by
// DimensionType#getTeleportationScale (CommandSourceStack#withLevel: X and Z times
// from.coordinateScale / to.coordinateScale, so (100, y, 100) in the overworld is (800, y, 800) in
// the Gloaming Forest, scale 0.125, and back). Bedrock's `execute in` keeps the position as it is.
// upstream's /gloamingforest has no subcommand for this, so the command tree stays upstream's: the behavior
// pack functions gloamingforest/dimension/tp_gloaming / tp_overworld run the vanilla command
// `scriptevent gloamingforest:tp_gloaming` / `...:tp_overworld`, and this file runs them with Java's
// semantics: the execution position scaled into the destination, the nearest living player
// (@p: every player, nearest to that position) teleported there with its rotation (TeleportCommand
// without a rotation argument).

#include <cmath>
#include <format>
#include <limits>
#include <string>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOriginType.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputParameter.h"
#include "mc/server/commands/ScriptEventCommand.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/dimension/Internal.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::dimension {

namespace {

constexpr char kTpGloaming[]  = "gloamingforest:tp_gloaming";
constexpr char kTpOverworld[] = "gloamingforest:tp_overworld";

std::string fixed2(double v) { return std::format("{:.2f}", v); }

} // namespace

std::optional<FunctionTeleport> runTpFunction(int srcDim, Vec3 const& source, bool toGloaming) {
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    int dest = toGloaming ? gloamingId() : 0;
    if (dest < 0) return std::nullopt;
    // CommandSourceStack#withLevel: the same level keeps the position, another one scales X and Z
    Vec3 pos = source;
    if (srcDim != dest) {
        double scale = coordinateScaleOf(srcDim) / coordinateScaleOf(dest);
        pos.x        = static_cast<float>(source.x * scale);
        pos.z        = static_cast<float>(source.z * scale);
    }
    // @p: ORDER_NEAREST over every player of the server (not limited to the level), alive ones
    std::int64_t best = 0;
    double       dist = std::numeric_limits<double>::max();
    level->forEachPlayer([&](Player& p) {
        if (!p.isAlive()) return true;
        Vec3   at = p.getFeetPos();
        double dx = at.x - pos.x, dy = at.y - pos.y, dz = at.z - pos.z;
        double d  = dx * dx + dy * dy + dz * dz;
        if (d < dist) {
            dist = d;
            best = idOf(p);
        }
        return true;
    });
    return FunctionTeleport{best, dest, pos};
}

namespace {

/// The function's teleport, as `tp @p ~ ~ ~` runs it in the destination.
void runTp(CommandOrigin const& commandOrigin, CommandOutput& output, bool toGloaming) {
    // Java runs functions at permission level 2
    if (!commands::isAdmin(commandOrigin)) return output.error("You need operator permission.");
    auto* dimension = commandOrigin.getDimension();
    auto  missing   = [&] { output.error(tr("commands.gloamingfeature.teleport.dimension_missing", "The Gloaming Forest dimension is unavailable.")); };
    if (!dimension) return missing();
    // the source position: the origin's (`execute ... at / positioned` set it); a player's own
    // origin reports its eyes, Java's source is the player's position (its feet)
    auto  type   = commandOrigin.getOriginType();
    auto* entity = commandOrigin.getEntity();
    Vec3  source = type == CommandOriginType::Player && entity ? entity->getFeetPos() : commandOrigin.getWorldPosition();
    int   src    = static_cast<int>(dimension->getDimensionId());
    auto  tp     = runTpFunction(src, source, toGloaming);
    if (!tp) return missing();
    auto* target = playerById(tp->player);
    if (!target) return output.error("commands.generic.noTargetMatch"); // "No player was found"
    // Level#isInSpawnableBounds
    if (std::abs(tp->pos.x) >= 3.0e7f || std::abs(tp->pos.z) >= 3.0e7f || std::abs(tp->pos.y) >= 2.0e7f) {
        return output.error(tr("commands.teleport.invalidPosition", "Invalid position for teleport"));
    }
    int   from = static_cast<int>(target->getDimensionId());
    Vec2  rot  = target->getRotation();
    auto  name = target->getRealName();
    log().info(
        "dimension: function {} (origin type {}) moves {} from {} {:.1f} {:.1f} {:.1f} to {} {:.1f} {:.1f} {:.1f}",
        toGloaming ? kTpGloaming : kTpOverworld,
        static_cast<int>(type),
        name,
        dimensionName(src),
        source.x,
        source.y,
        source.z,
        dimensionName(tp->dim),
        tp->pos.x,
        tp->pos.y,
        tp->pos.z
    );
    if (from == tp->dim) {
        target->teleport(tp->pos, DimensionType(tp->dim), rot);
    } else {
        // Java's tp is no portal trip: no portal::onTeleported. Java puts the player exactly at
        // the position; Bedrock's dimension change settles it off (its eye height higher), so once
        // it is there it is placed exactly, as finishTrip does after a portal trip
        auto at = tp->pos;
        moveAcross(*target, tp->dim, at, rot.y, rot.x, [at, dim = tp->dim, rot](Player& p) {
            p.teleport(at, DimensionType(dim), rot);
            p.setFallDistance(0.0f);
        }, [name, dim = tp->dim](Player* p) {
            log().warn("dimension: the function teleport of {} to {} did not complete", name, dimensionName(dim));
            (void)p;
        });
    }
    output.success(
        "commands.tp.success.coordinates",
        {CommandOutputParameter(name), CommandOutputParameter(fixed2(tp->pos.x)), CommandOutputParameter(fixed2(tp->pos.y)), CommandOutputParameter(fixed2(tp->pos.z))}
    );
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    GloamingFunctionHook,
    ll::memory::HookPriority::Normal,
    ScriptEventCommand,
    &ScriptEventCommand::$execute,
    void,
    CommandOrigin const& commandOrigin,
    CommandOutput&       output
) {
    std::string const& id = mMessageId;
    if (id == kTpGloaming || id == kTpOverworld) return runTp(commandOrigin, output, id == kTpGloaming);
    origin(commandOrigin, output);
}

class FunctionsModule {
public:
    void install() { GloamingFunctionHook::hook(); }
    void uninstall() { GloamingFunctionHook::unhook(); }
};

} // namespace gf::feature::dimension

GF_REGISTER_MODULE(gf::feature::dimension::FunctionsModule);

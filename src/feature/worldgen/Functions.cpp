// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/resources/data/twilightforest/function/dev_new_world.mcfunction
//   src/main/resources/data/twilightforest/function/dev_powerwash_chunk.mcfunction
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's two world generation dev functions as Bedrock functions, run as in Java with
// `/function twilightforest/dev_new_world` and `/function twilightforest/dev_powerwash_chunk`.
// F5 owns the function ids but no pack directory (its paths are code only), so the module writes
// them into the mod's behavior pack when the mod loads, before a world (and its functions) loads;
// the selftest worldgen.functions runs both in the world.
//
// dev_new_world, line by line against TF's:
// - doDaylightCycle, doEntityDrops, doMobLoot, doMobSpawning, doTileDrops, doWeatherCycle,
//   keepInventory: the same Bedrock gamerules.
// - randomTickSpeed -1 is written as 0: Java ticks no random block for any value <= 0
//   (ServerLevel#tickChunk `if (randomTickSpeed > 0)`), Bedrock's randomtickspeed means the same
//   at 0, so the effect is identical.
// - announceAdvancements, doPatrolSpawning, doTraderSpawning: Bedrock has no such gamerules (its
//   patrols and wandering traders have no switch; TF's advancements are the mod's, U14).
// - tfEnforcedProgression false and `execute in twilightforest:twilight_forest run teleport ~ 128 ~`:
//   a pack function cannot name the mod's /twilightforest gamerule (BDS parses functions before
//   mod commands exist) nor a custom dimension, so the function ends with
//   `scriptevent twilightforest:dev_new_world`, which this module answers: the rule is set through
//   gf::gamerules and the entity running the function (if any) goes to the Twilight Forest at
//   y 128, its x / z scaled like Java's `execute in` (coordinate scale of the source over the
//   Twilight Forest's).
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/ScriptEventCommand.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include "feature/dimension/World.h"
#include "feature/gamerules/GameRules.h"
#include "feature/worldgen/Internal.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::worldgen::detail {

namespace {

constexpr char kDevNewWorldEvent[] = "twilightforest:dev_new_world";

constexpr char kDevNewWorld[] = R"(# twilightforest:dev_new_world (The Twilight Forest 4.8): development world settings.
# Java's randomTickSpeed -1 is 0 here (no random ticks either way). Java's announceAdvancements,
# doPatrolSpawning and doTraderSpawning have no Bedrock gamerule. The scriptevent sets the mod's
# tfEnforcedProgression rule to false and sends the caller into the Twilight Forest at y 128.
gamerule dodaylightcycle false
gamerule doentitydrops false
gamerule domobloot false
gamerule domobspawning false
gamerule dotiledrops false
gamerule doweathercycle false
gamerule keepinventory true
gamerule randomtickspeed 0
scriptevent twilightforest:dev_new_world teleport
)";

constexpr char kDevPowerwashChunk[] = R"(# twilightforest:dev_powerwash_chunk (The Twilight Forest 4.8): clears the natural ground around the caller.
fill ~-10 -32 ~-10 ~10 15 ~10 air replace grass_block
fill ~-10 -32 ~-10 ~10 15 ~10 air replace dirt
fill ~-10 -32 ~-10 ~10 15 ~10 air replace stone
fill ~-10 -32 ~-10 ~10 15 ~10 air replace bedrock
fill ~-10 -32 ~-10 ~10 15 ~10 air replace gravel
fill ~-10 -32 ~-10 ~10 15 ~10 air replace andesite
fill ~-10 -32 ~-10 ~10 15 ~10 air replace diorite
fill ~-10 -32 ~-10 ~10 15 ~10 air replace granite
)";

std::atomic<int> gDevNewWorldRuns{0};

bool writeIfChanged(std::filesystem::path const& file, std::string_view content) {
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        std::ifstream     in(file, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        if (ss.str() == content) return true;
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << content;
    return static_cast<bool>(out);
}

/// Java's DimensionType#getTeleportationScale(from, twilight): the source's coordinate scale over
/// the Twilight Forest's (overworld and end 1, nether 8).
double teleportationScale(DimensionType from) {
    double to = world::coordinateScale();
    if (to <= 0) to = 0.125;
    double source = world::isTwilight(from) ? to : (static_cast<int>(from) == 1 ? 8.0 : 1.0);
    return source / to;
}

/// The Twilight Forest part of dev_new_world (tfEnforcedProgression, the teleport).
void runDevNewWorld(CommandOrigin const& origin, CommandOutput& output) {
    ++gDevNewWorldRuns;
    bool rule = gamerules::set("tfEnforcedProgression", false);
    gf::log().info("worldgen: dev_new_world: tfEnforcedProgression false ({})", rule ? "set" : "rule not registered");
    Actor* entity = origin.getEntity();
    if (!rule) output.success("dev_new_world: the tfEnforcedProgression gamerule is not registered (not changed)");
    if (!entity) {
        // Java: `teleport ~ 128 ~` needs an entity; from the console only the rules change
        output.success("dev_new_world: no entity to teleport");
        return;
    }
    auto twilight = world::twilight();
    if (!twilight) {
        gf::log().warn("worldgen: dev_new_world: the Twilight Forest dimension is not available, no teleport");
        output.success("dev_new_world: the Twilight Forest dimension is not available");
        return;
    }
    if (!entity->isPlayer()) {
        output.success("dev_new_world: only players are teleported");
        return;
    }
    auto&  player = static_cast<Player&>(*entity);
    double scale  = teleportationScale(player.getDimensionId());
    Vec3   pos    = player.getFeetPos();
    Vec3   target{static_cast<float>(pos.x * scale), 128.0F, static_cast<float>(pos.z * scale)};
    bool   ok = world::teleport(player, *twilight, target, player.getRotation().y);
    gf::log().info("worldgen: dev_new_world: {} -> Twilight Forest {:.1f} 128 {:.1f} ({})", player.getRealName(), target.x, target.z, ok ? "teleported" : "failed");
    output.success(ok ? "dev_new_world: teleported" : "dev_new_world: teleport failed");
}

} // namespace

/// The directory the functions are written to.
std::filesystem::path functionsDir() {
    return Mod::getInstance().getSelf().getModDir() / "behavior_packs" / "GloamingForest_BP" / "functions" / "twilightforest";
}

/// How often the scriptevent of dev_new_world was answered (selftests).
int devNewWorldRuns() { return gDevNewWorldRuns.load(); }

LL_TYPE_INSTANCE_HOOK(
    DevFunctionScriptEventHook,
    ll::memory::HookPriority::Normal,
    ScriptEventCommand,
    &ScriptEventCommand::$execute,
    void,
    CommandOrigin const& commandOrigin,
    CommandOutput&       output
) {
    if (mMessageId.get() == kDevNewWorldEvent) {
        try {
            runDevNewWorld(commandOrigin, output);
        } catch (std::exception const& e) {
            gf::log().error("worldgen: dev_new_world failed: {}", e.what());
        }
        return;
    }
    origin(commandOrigin, output);
}

class FunctionsModule {
public:
    void load() { write(); }
    void install() {
        write();
        if (!mHooked) {
            DevFunctionScriptEventHook::hook();
            mHooked = true;
        }
    }
    void uninstall() {
        if (mHooked) DevFunctionScriptEventHook::unhook();
        mHooked = false;
    }

private:
    void write() {
        if (mDone) return;
        mDone = true;
        try {
            auto dir = functionsDir();
            if (!std::filesystem::is_directory(dir.parent_path().parent_path())) {
                gf::log().warn("worldgen: behavior pack not found, functions not written ({})", dir.generic_string());
                return;
            }
            bool ok = writeIfChanged(dir / "dev_new_world.mcfunction", kDevNewWorld)
                   && writeIfChanged(dir / "dev_powerwash_chunk.mcfunction", kDevPowerwashChunk);
            if (!ok) gf::log().warn("worldgen: cannot write the dev functions to {}", dir.generic_string());
        } catch (std::exception const& e) {
            gf::log().warn("worldgen: dev functions: {}", e.what());
        }
    }
    bool mDone   = false;
    bool mHooked = false;
};

} // namespace gf::worldgen::detail

GF_REGISTER_MODULE_ORDERED(gf::worldgen::detail::FunctionsModule, gf::ModuleOrder::Contracts);

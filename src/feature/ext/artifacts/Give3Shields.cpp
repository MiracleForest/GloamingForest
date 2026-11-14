// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/resources/data/gloamingforest/function/give_3_shields.mcfunction
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract extension of U9 (rule R18, docs/spec/artifacts.md 4): upstream's function
// gloamingforest:give_3_shields ("gloamingforest shield @p add 3 false") as a Bedrock function.
// A pack function cannot name /gloamingforest (BDS parses functions before the mod's commands
// exist) and Bedrock functions have no namespace, so the module writes
// functions/gloamingforest/give_3_shields.mcfunction into the mod's behavior pack when the mod
// loads; it runs as `/function gloamingforest/give_3_shields`:
//     execute as @p run scriptevent gloamingforest:give_3_shields
// and the ScriptEventCommand hook answers that message id by adding 3 permanent shields to the
// entity running it (Java's @p: the nearest player; none: nothing happens). Every other message id
// passes on (F4's and F5's hooks do the same; A8 folds the three into one dispatcher).
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ll/api/memory/Hook.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/ScriptEventCommand.h"
#include "mc/world/actor/Actor.h"

#include "feature/artifacts/Artifacts.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::feature::artifacts::ext {

namespace {

constexpr char kEvent[] = "gloamingforest:give_3_shields";

constexpr char kFunction[] = R"(# gloamingforest:give_3_shields (the upstream 4.8): "gloamingforest shield @p add 3 false".
# Bedrock's function cannot name the mod's /gloamingforest command: the mod answers the scriptevent
# by adding 3 permanent fortification shields to the nearest player.
execute as @p run scriptevent gloamingforest:give_3_shields
)";

std::atomic<int> gRuns{0};

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

} // namespace

/// How often the scriptevent was answered with a player (selftests).
int give3ShieldsRuns() { return gRuns.load(); }

} // namespace gf::feature::artifacts::ext

LL_TYPE_INSTANCE_HOOK(
    ArtifactsGive3ShieldsHook,
    ll::memory::HookPriority::Normal,
    ScriptEventCommand,
    &ScriptEventCommand::$execute,
    void,
    CommandOrigin const& commandOrigin,
    CommandOutput&       output
) {
    if (mMessageId.get() != gf::feature::artifacts::ext::kEvent) return origin(commandOrigin, output);
    try {
        Actor* entity = commandOrigin.getEntity();
        if (entity && entity->isPlayer()) {
            gf::feature::artifacts::addShields(*entity, 3, false);
            ++gf::feature::artifacts::ext::gRuns;
            gf::log().info("artifacts: give_3_shields: 3 permanent shields for {}", entity->getTypeName());
            output.success("give_3_shields: 3 shields added");
        } else {
            output.error("No entity was found");
        }
    } catch (std::exception const& e) {
        gf::log().error("artifacts: give_3_shields failed: {}", e.what());
    }
}

namespace gf::feature::artifacts::ext {
namespace {

class Give3ShieldsModule {
public:
    void load() { write(); }
    void install() {
        write();
        if (!mHooked) {
            ArtifactsGive3ShieldsHook::hook();
            mHooked = true;
        }
    }
    void uninstall() {
        if (mHooked) ArtifactsGive3ShieldsHook::unhook();
        mHooked = false;
    }

private:
    void write() {
        if (mDone) return;
        mDone = true;
        try {
            auto dir = Mod::getInstance().getSelf().getModDir() / "behavior_packs" / "GloamingForest_BP" / "functions" / "gloamingforest";
            if (!std::filesystem::is_directory(dir.parent_path().parent_path())) {
                gf::log().warn("artifacts: behavior pack not found, give_3_shields not written ({})", dir.generic_string());
                return;
            }
            if (!writeIfChanged(dir / "give_3_shields.mcfunction", kFunction)) gf::log().warn("artifacts: cannot write give_3_shields to {}", dir.generic_string());
        } catch (std::exception const& e) {
            gf::log().warn("artifacts: give_3_shields: {}", e.what());
        }
    }
    bool mDone   = false;
    bool mHooked = false;
};

} // namespace
} // namespace gf::feature::artifacts::ext

GF_REGISTER_MODULE_ORDERED(gf::feature::artifacts::ext::Give3ShieldsModule, gf::ModuleOrder::Contracts);


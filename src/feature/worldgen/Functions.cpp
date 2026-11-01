// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/resources/data/twilightforest/function/dev_new_world.mcfunction
//   src/main/resources/data/twilightforest/function/dev_powerwash_chunk.mcfunction
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's two world generation dev functions as Bedrock functions (run with
// `/function twilightforest/dev_new_world`). F5 owns the function ids but no pack directory, so
// the module writes them into the mod's behavior pack when the mod loads, before any world (and
// its functions) is loaded. Only commands Bedrock accepts in a pack function are written: Java's
// announceAdvancements, doPatrolSpawning and doTraderSpawning rules have no Bedrock counterpart,
// tfEnforcedProgression is a mod command (`/tf gamerule`, unknown to pack functions) and
// `execute in` does not take custom dimensions.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::worldgen::detail {

namespace {

constexpr char kDevNewWorld[] = R"(# twilightforest:dev_new_world (The Twilight Forest 4.8): development world settings.
# Java also turns off announceAdvancements, doPatrolSpawning and doTraderSpawning (no Bedrock rules),
# sets tfEnforcedProgression false (run: /twilightforest gamerule tfEnforcedProgression false) and
# teleports into the Twilight Forest at y 128 (run: /twilightforest tp).
gamerule dodaylightcycle false
gamerule doentitydrops false
gamerule domobloot false
gamerule domobspawning false
gamerule dotiledrops false
gamerule doweathercycle false
gamerule keepinventory true
gamerule randomtickspeed 0
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

/// The directory the functions are written to.
std::filesystem::path functionsDir() {
    return Mod::getInstance().getSelf().getModDir() / "behavior_packs" / "GloamingForest_BP" / "functions" / "twilightforest";
}

class FunctionsModule {
public:
    void load() { write(); }
    void install() { write(); }
    void uninstall() {}

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
    bool mDone = false;
};

} // namespace gf::worldgen::detail

GF_REGISTER_MODULE_ORDERED(gf::worldgen::detail::FunctionsModule, gf::ModuleOrder::Contracts);

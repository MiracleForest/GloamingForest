// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Runtime part of the imported block behaviour (unit F1 "import"): Java's
// BlockBehaviour.Properties.requiresCorrectToolForDrops(). A Bedrock JSON block cannot declare it
// (vanilla blocks carry it in their vanilla block data), so the import lists those blocks in
// data/gf/import/harvest.json and this module sets the engine's flag on their block types, as
// vanilla stone and obsidian have it: the block then drops only for a tool that can harvest it
// (the tool and tier tags the import gives the block, the digger of TF's tools) and mines at
// Bedrock's slower no-harvest speed otherwise. Applied when a world is ready (BDS, local worlds:
// the integrated server and the client share the block registry) and, on the client, when a
// GloamingForest server accepts the companion handshake (its blocks are registered by then).
//
// F1 owns no source directory, so this lives in src/feature/ext/import/ (new files only, rule R18);
// A8 may fold it into a unit directory.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/BlockType.h"

#include "nlohmann/json.hpp"

#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

#ifdef GF_CLIENT
#include "feature/net/Net.h"
#endif

namespace gf::feature::ext::harvest {

namespace {

/// The block ids of data/gf/import/harvest.json with "requiresTool" (read once).
std::vector<std::string> const& requiresTool() {
    static std::vector<std::string> const ids = [] {
        std::vector<std::string> res;
        auto          path = Mod::getInstance().getSelf().getModDir() / "data" / "gf" / "import" / "harvest.json";
        std::ifstream in(path);
        if (!in) return res;
        auto doc = nlohmann::json::parse(in, nullptr, false);
        if (!doc.is_object() || !doc.contains("blocks") || !doc["blocks"].is_object()) return res;
        for (auto const& [id, e] : doc["blocks"].items())
            if (e.is_object() && e.value("requiresTool", false)) res.push_back(id);
        return res;
    }();
    return ids;
}

/// Sets the requires-correct-tool flag on every listed block type. Returns how many were set.
int apply() {
    int                      set = 0;
    std::vector<std::string> missing;
    for (auto const& id : requiresTool()) {
        auto type = BlockType::tryGetFromRegistry(HashedString(id));
        if (!type) {
            missing.push_back(id);
            continue;
        }
        // the registry's block type (a JSON block's flag is false; vanilla blocks get theirs from their data)
        auto& bt = const_cast<BlockType&>(*type);
        if (!bt.mRequiresCorrectToolForDrops) bt.mRequiresCorrectToolForDrops = true;
        ++set;
    }
    if (!missing.empty()) gf::log().warn("harvest: {} block(s) of harvest.json are not registered (first: {})", missing.size(), missing.front());
    return set;
}

} // namespace

/// The number of block types that drop only for a correct tool (selftests).
int requiresToolCount() { return static_cast<int>(requiresTool().size()); }

/// Applies the flag again (selftests; idempotent).
int applyNow() { return apply(); }

struct HarvestModule {
    void install() {
        auto now = [] {
            int n = apply();
            gf::log().info("harvest: {} TF block types drop only for a correct tool", n);
        };
        // BDS enables mods after the world exists (the world-ready listeners have already run): apply now; every
        // later world (the client's local worlds) through the listener
        if (game::db::available()) now();
        game::db::onWorldReady(now);
#ifdef GF_CLIENT
        net::client::onConnected([] { apply(); });
#endif
    }
    void uninstall() {}
};

} // namespace gf::feature::ext::harvest

GF_REGISTER_MODULE(gf::feature::ext::harvest::HarvestModule);

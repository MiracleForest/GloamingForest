// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// Contract extension of U12 (rule R18, folded by A8): JSON patches of resource pack files as the
// client mod's game reads them (client build only; the server build and vanilla clients read the
// packs unchanged). No W0 contract lets a unit change another unit's pack file for the client mod
// alone: the Travellers' wings' attachable (U11 / F2's geometry) plays U12's wing animation only
// with the client mod, which sets the Molang variables that animation reads (a vanilla client
// never sees the animation referenced, so it never reads the variables it does not have).
// It hooks ResourcePack::getResource and ResourcePackManager::load (both MCAPI), the reads of
// every pack file, and patches a matching JSON document before the game parses it.
//
//     gf::ext::packpatch::addJsonPatch("attachables/travellers_wings.json", [](nlohmann::json& doc) {
//         return addAnimation(doc); // true: changed
//     });

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

namespace gf::ext::packpatch {

/// Changes `doc` in place; returns whether it changed (an unchanged document is passed on as read).
using PatchFn = std::function<bool(nlohmann::json& doc)>;

/// Patches every pack file whose path ends with `pathSuffix` ('/' separators, case-insensitive).
/// Register in a module's load() (before the game loads its resource packs).
void addJsonPatch(std::string pathSuffix, PatchFn fn);

/// How many documents were patched so far (selftests, e2e).
[[nodiscard]] int patchedCount();

} // namespace gf::ext::packpatch

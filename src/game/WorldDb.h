// Adapted from LiteLDev/Levitate v0.1.0 (src/game/WorldDb.h), LGPL-3.0-or-later.
#pragma once

// Contract "db" (W0a): per-world JSON storage in the world's own LevelDB (server thread only).
//
// Keys are "gf:<unit>:<...>" strings (rule R16; core/Keys.h). Values are JSON documents. Data
// travels with the world folder (BDS worlds/, client local worlds). The world lifecycle hooks let
// modules reset their caches: on the client every local world load starts a new world in the
// same process. Never keep `Player*` or block pointers across ticks.
//
//     namespace db = gf::game::db;
//     db::put("gf:landmark:conquered", {{"x", 0}});
//     if (auto j = db::get("gf:landmark:conquered")) ...;
//     db::onWorldReady([] { reloadCaches(); });

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

namespace gf::game::db {

/// A level (and its DB) is loaded.
[[nodiscard]] bool available();

/// The document stored under `key` (nullopt: missing, unreadable or no world).
[[nodiscard]] std::optional<nlohmann::json> get(std::string const& key);
/// Stores `value` under `key` (no-op without a world). Malformed keys (not "gf:<unit>:...") are
/// refused with an error line.
void put(std::string const& key, nlohmann::json const& value);
void erase(std::string const& key);
/// Calls `fn(keySuffix)` for every stored key starting with `prefix`.
void forEachKey(std::string const& prefix, std::function<void(std::string_view suffix)> const& fn);

/// World lifecycle (server thread). Listeners registered at static init or in `install()`.
/// - `onWorldReady`: a world is loaded (BDS: at mod enable; client: each local world load).
/// - `onWorldSave`: autosave (every 30 s), server stopping and mod disable: write dirty data now.
/// - `onWorldClosed`: after the final save of a world: drop caches.
void onWorldReady(std::function<void()> fn);
void onWorldSave(std::function<void()> fn);
void onWorldClosed(std::function<void()> fn);

} // namespace gf::game::db

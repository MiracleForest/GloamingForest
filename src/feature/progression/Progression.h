#pragma once

// Contract "progression" (header owned by F0, implemented by F10; rule R21).
//
// The progression store (advancements and criteria per player, persisted in the world DB), the
// trigger bus for upstream's 13 triggers and the vanilla trigger events, and the client-side view of
// progression for locked biomes. U14 owns every advancement JSON and the criteria engine; other
// units only fire triggers by string and never award advancements directly:
//
//     gf::progression::trigger(player, "gloamingforest:kill_bug", {{"bug", "gloamingforest:firefly"}});
//     if (gf::progression::has(player, "gloamingforest:progress_lich")) ...;
//     gf::progression::events::onKilled([](Player& p, Actor& victim, std::string const& type) { ... });

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

class Actor;
class ItemStack;
class Player;

namespace gf::progression {

using Json = nlohmann::json;

/// The player completed advancement `advancementId` (every criterion).
[[nodiscard]] bool has(Player const& player, std::string const& advancementId);
/// Grants the advancement (empty `criterion`: every criterion), firing onGranted when it completes.
void grant(Player& player, std::string const& advancementId, std::string const& criterion = {});
/// Revokes the advancement (empty `criterion`: every criterion).
void revoke(Player& player, std::string const& advancementId, std::string const& criterion = {});
/// Whether one criterion of an advancement is done.
[[nodiscard]] bool done(Player const& player, std::string const& advancementId, std::string const& criterion);

/// Fires a trigger ("gloamingforest:structure_cleared", "minecraft:inventory_changed", ...).
void trigger(Player& player, std::string const& triggerId, Json const& data = {});
/// Listens to a trigger (U14's criteria engine; selftest fixtures).
using TriggerFn = std::function<void(Player& player, Json const& data)>;
void onTrigger(std::string const& triggerId, TriggerFn fn);
/// An advancement was completed.
using GrantedFn = std::function<void(Player& player, std::string const& advancementId)>;
void onGranted(GrantedFn fn);
/// Shows the "missing advancement" toast for `advancementId` (MissingAdvancementToast).
void showMissing(Player& player, std::string const& advancementId);

/// Central game events (one engine hook each, F10), for triggers and units.
namespace events {
void onInventoryChanged(std::function<void(Player& player)> fn);
void onKilled(std::function<void(Player& killer, Actor& victim, std::string const& damageType)> fn);
void onConsumed(std::function<void(Player& player, ItemStack const& stack)> fn);
void onDimensionChanged(std::function<void(Player& player, int from, int to)> fn);
void onBiomeEntered(std::function<void(Player& player, std::string const& biomeId)> fn);
/// Every player, once per second (20 ticks).
void onTick(std::function<void(Player& player)> fn);
} // namespace events

#ifdef GF_CLIENT
/// The local player's progression as the server synced it (client mod; client thread).
namespace client {
[[nodiscard]] bool has(std::string const& advancementId);
/// The gloamingEnforcedProgression gamerule of the current world.
[[nodiscard]] bool enforced();
/// Whether biome `biomeId` is locked for the local player (restrictions + progression).
[[nodiscard]] bool biomeLocked(std::string const& biomeId);
} // namespace client
#endif

} // namespace gf::progression

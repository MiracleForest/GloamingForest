// U15 (atmosphere): internals shared by the unit's server code and its selftests.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Player;

namespace gf::feature::atmosphere {

/// The vanilla fallback of one player (PlayerFogPacket): the stack for the player's biome and
/// height in the Twilight Forest (or empty elsewhere), sent when it changed since the last send.
/// `biomeOverride` replaces the biome at the player (selftests). Returns true when a packet went out.
bool updateFog(Player& player, std::optional<std::string> const& biomeOverride = std::nullopt);
/// The fog stack last sent to a player (nullopt: none sent yet).
[[nodiscard]] std::optional<std::vector<std::string>> sentFogStack(Player const& player);
/// Forgets what was sent to a player (selftests start clean).
void forgetFog(Player const& player);
/// Pins the biome the periodic update uses for a player (nullopt: the biome at the player again);
/// the vanilla e2e preview.
void pinFogBiome(Player const& player, std::optional<std::string> biome);
/// Fog packets sent since the start (tests).
[[nodiscard]] std::uint64_t fogPacketsSent();

} // namespace gf::feature::atmosphere

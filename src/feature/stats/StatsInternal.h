#pragma once

// Internals of the statistics store shared with F9's selftests.

#include <cstddef>
#include <string>

class Player;

namespace gf::stats::internal {

/// Whether a statistic is in the custom statistic registry (upstream's 10 and Minecraft 1.21.1's 75).
[[nodiscard]] bool isRegistered(std::string const& statId);
/// The registry's size.
[[nodiscard]] std::size_t registeredCount();
/// Whether award() warned about a statistic (one outside the registry).
[[nodiscard]] bool warnedAbout(std::string const& statId);

/// Drops a player's statistics from memory and the world DB (selftests' simulated players).
void forget(Player const& player);

} // namespace gf::stats::internal

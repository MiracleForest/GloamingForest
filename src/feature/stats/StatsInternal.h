#pragma once

// Internals of the statistics store shared with F9's selftests.

class Player;

namespace gf::stats::internal {

/// Drops a player's statistics from memory and the world DB (selftests' simulated players).
void forget(Player const& player);

} // namespace gf::stats::internal

#pragma once

// Internals of F8's projectile simulation for its selftests (not a contract).

#include <string>

namespace gf::feature::projectile {

/// Removes a projectile type registration (fixture projectiles live only while their test runs).
void unregisterTypeForTest(std::string const& entityId);
/// The number of projectiles being simulated.
[[nodiscard]] int liveCount();

} // namespace gf::feature::projectile

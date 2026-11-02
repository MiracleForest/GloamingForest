#pragma once

// Internals of F8's projectile simulation for its selftests (not a contract).

#include <string>

class Actor;
class ItemStack;

namespace gf::feature::projectile {

/// Removes a projectile type registration (fixture projectiles live only while their test runs).
void unregisterTypeForTest(std::string const& entityId);
/// The number of projectiles being simulated.
[[nodiscard]] int liveCount();
/// Arrow hits that landed on a living target and ran AbstractArrow#doPostHurtEffects so far.
[[nodiscard]] int postHurtsForTest();
/// Arrow#doPostHurtEffects of an arrow fired with `stack` on `target` (a tipped arrow's potion).
void applyFiredStackEffectsForTest(Actor& target, ItemStack const& stack);

} // namespace gf::feature::projectile

#pragma once

// Contract "projectile" (header owned by F0, implemented by F8).
//
// TF's throwables and arrows (TFThrowable / TFArrow bases): native flight with gravity and drag,
// hit callbacks, and steering (seeker arrow homing, moonworm shots).
//
//     gf::projectile::registerType("twilightforest:ice_snowball", {
//         .gravity = 0.03f,
//         .onHitEntity = [](Actor& p, Actor& target) { ...; return true; },
//     });
//     gf::projectile::shoot(shooter, "twilightforest:ice_snowball", look, 1.5f, 1.0f);

#include <functional>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

class Actor;
class BlockPos;

namespace gf::projectile {

struct Spec {
    float gravity = 0.03f; ///< subtracted from the y velocity every tick
    float drag    = 0.99f; ///< velocity multiplier every tick (in water: Java's 0.8)
    /// Hit an entity: true removes the projectile.
    std::function<bool(Actor& projectile, Actor& target)>                onHitEntity;
    /// Hit a block face (FacingID): true removes the projectile.
    std::function<bool(Actor& projectile, BlockPos const& pos, int face)> onHitBlock;
    std::function<void(Actor& projectile)>                               tick;
};
void registerType(std::string const& entityId, Spec spec);

/// Spawns projectile `entityId` from `shooter` along `direction` (Java's shoot(x, y, z, velocity,
/// inaccuracy)); `init` is stored as the projectile's data. nullptr on failure.
Actor* shoot(Actor& shooter, std::string const& entityId, Vec3 const& direction, float speed, float inaccuracy, nlohmann::json const& init = {});

/// Steering for every GloamingForest projectile each tick: return a new velocity, or nullopt to
/// leave it (seeker arrow homing).
using SteeringFn = std::function<std::optional<Vec3>(Actor& projectile, std::string const& entityId)>;
void addSteering(SteeringFn fn);

} // namespace gf::projectile

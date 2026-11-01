#pragma once

// Contract "world" (header owned by F0, implemented by F4; spike S1).
//
// The Twilight Forest dimension `twilightforest:twilight_forest` (TF dimension_type
// twilight_forest_type: min_y -32, height 288, fixed_time 13000, coordinate_scale 0.125, beds
// work, no respawn anchors), the TFTeleporter and the Twilight portal.
//
//     if (auto tf = gf::world::twilight()) gf::world::teleport(player, *tf, {0.5f, 80.f, 0.5f}, 0.f);
//     gf::portal::onTeleported([](Player& p, DimensionType from, DimensionType to) { ... });
//
// The backend (native DimensionDefinition or the MoreDimensions route) is chosen by spike S1;
// the API does not depend on it. Gamerules playersTfPortalDefaultDelay /
// playersTfPortalCreativeDelay are registered by F4 through gf::gamerules (feature/gamerules).
// Server thread only.

#include <functional>
#include <optional>

#include "mc/world/level/dimension/DimensionType.h"

#include "core/GlobalPos.h"

class BlockPos;
class BlockSource;
class Dimension;
class Player;
class Vec3;

namespace gf::world {

inline constexpr char kDimensionId[] = "twilightforest:twilight_forest";
inline constexpr int  kMinY          = -32;
inline constexpr int  kHeight        = 288;

/// The Twilight Forest's dimension, once registered with the game (nullopt: not available).
[[nodiscard]] std::optional<DimensionType> twilight();
/// Whether `dimension` is the Twilight Forest.
[[nodiscard]] bool isTwilight(Dimension const& dimension);
[[nodiscard]] bool isTwilight(DimensionType dimension);
/// Moves `player` (with riders) to `pos` in `dimension`, facing `yaw`, loading the destination
/// first (TFTeleporter#placeEntity semantics; fires portal::onTeleported). False on failure.
bool teleport(Player& player, DimensionType dimension, Vec3 const& pos, float yaw);
/// The dimension's coordinate scale relative to the overworld (TF: 0.125).
[[nodiscard]] double coordinateScale();
/// The fixed time of day of the dimension (TF: 13000), nullopt when time flows.
[[nodiscard]] std::optional<long long> fixedTime();

} // namespace gf::world

namespace gf::portal {

/// Tries to light a Twilight portal at `pos` (TFPortalBlock#tryToCreatePortal: a water pool ringed
/// by portal/decoration blocks, activated by an item of the portal/activator tag thrown in;
/// lightning and the portal sound included). True when a portal was created.
bool tryActivate(BlockSource& region, BlockPos const& pos);
/// Called after a player went through a Twilight portal (or gf::world::teleport).
using TeleportedFn = std::function<void(Player& player, DimensionType from, DimensionType to)>;
void onTeleported(TeleportedFn fn);
/// Where a portal trip of `player` would arrive right now (TFTeleporter destination search,
/// including the return-portal position); nullopt when unknown.
[[nodiscard]] std::optional<core::GlobalPos> destinationFor(Player const& player);

} // namespace gf::portal

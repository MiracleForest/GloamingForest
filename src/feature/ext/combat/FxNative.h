#pragma once

// Contract extension of "rules+fx" by F10 (rule R18; to be folded into feature/fx/Fx.h or
// client/world/Sky.h by A8).
//
// The client mod receives "fx.particle" {id, pos, vel, count, params} for particles played with
// gf::fx::particle. client/world/Sky.h lets units register native particles
// (gf::client::particles::registerNative) but has no call that spawns one, so the fx client
// handler asks a sink first: F12 (or A8) connects its native particle registry here. When no sink
// takes the particle, the handler spawns the resource pack effect locally (what a vanilla client
// shows).
//
//     gf::fx::client::setNativeSink([](auto const& id, Vec3 const& pos, Vec3 const& vel, int count, auto const& params) {
//         return spawnRegisteredNative(id, pos, vel, count, params);   // true: handled
//     });

#ifdef GF_CLIENT

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

namespace gf::fx::client {

/// Spawns a native particle on the client; returns false when `particleId` has no native version.
using NativeSink = std::function<bool(std::string const& particleId, Vec3 const& pos, Vec3 const& velocity, int count, nlohmann::json const& params)>;
/// Installs (or with an empty function removes) the native particle sink (client thread).
void setNativeSink(NativeSink sink);

} // namespace gf::fx::client

#endif

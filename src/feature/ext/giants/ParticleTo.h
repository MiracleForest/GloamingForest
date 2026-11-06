// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// Contract extension of "rules+fx" for U29 (rule R18: new file; A8 folds it into
// src/feature/fx/Fx.h together with U3's gf::fx::ext::particleTo). gf::fx::particle reaches every
// player within 32 blocks; the cloud weather (CloudEvents) is drawn by each client for its own
// camera: the client mod draws it itself, so the server sends the vanilla fallback only to the one
// vanilla player whose view it is (the resource pack effect with `variable.velocity`, as gf::fx
// sends it to a vanilla client).

#include <string>

#include "mc/deps/core/math/Vec3.h"

class Player;

namespace gf::fx::giants_ext {

/// Sends one particle effect of `particleId` at `pos` to `player` alone (vanilla clients only;
/// companions are skipped). Unknown ids are skipped (gf::fx warns once).
void vanillaParticleTo(Player& player, std::string const& particleId, Vec3 const& pos, Vec3 const& velocity = {});

} // namespace gf::fx::giants_ext

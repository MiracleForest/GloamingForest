// U15 (atmosphere), client mod: what the unit's client selftests and its own files see of the
// client state (client build only).

#pragma once

#ifdef GF_CLIENT

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "client/world/Sky.h"

namespace gf::client::atmosphere {

struct AtmosphereState {
    bool        inTwilight = false;
    int         aurora = 0, lastAurora = 0;
    float       urGhastRain = 0;
    std::string cameraBiome;
    bool        nearLocked = false;
    int         dimension  = -1; ///< the TF dimension the provider is set for (-1: none yet)
    bool        auroraGrid = false;
    int         ticks      = 0; ///< client ticks counted by the unit (selftests wait on these)
};
/// A snapshot of the client state (any thread).
[[nodiscard]] AtmosphereState state();
/// Applies an "atmosphere.preview" message ({} clears it).
void setPreview(nlohmann::json const& msg);
/// The fog the provider gives in `biome` now (one smoothing step).
[[nodiscard]] std::optional<sky::FogSpec> fogForTest(std::string const& biome);
/// Sets TF's sky provider for the Twilight Forest dimension (once it is known).
void installProvider();
/// TFConfig#getValidAuroraBiomes now: the config's biomes that parse and that the world knows (as
/// Bedrock names them: vanilla ones without "minecraft:").
[[nodiscard]] std::vector<std::string> validAuroraBiomes();

} // namespace gf::client::atmosphere

#endif // GF_CLIENT

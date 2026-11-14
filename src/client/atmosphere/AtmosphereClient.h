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
    bool        inGloaming = false;
    int         aurora = 0, lastAurora = 0;
    float       urGhastRain = 0;
    std::string cameraBiome;
    bool        nearLocked = false;
    int         dimension  = -1; ///< the upstream dimension the provider is set for (-1: none yet)
    bool        auroraGrid = false;
    int         ticks      = 0; ///< client ticks counted by the unit (selftests wait on these)
};
/// A snapshot of the client state (any thread).
[[nodiscard]] AtmosphereState state();
/// Applies an "atmosphere.preview" message ({} clears it).
void setPreview(nlohmann::json const& msg);
/// The fog the provider gives in `biome` now (one smoothing step).
[[nodiscard]] std::optional<sky::FogSpec> fogForTest(std::string const& biome);
/// The fog the provider gives now, as the game asks for it (nullopt in water, lava or powder snow).
[[nodiscard]] std::optional<sky::FogSpec> fogNow();
/// Whether the camera was in water, lava or powder snow at the last client tick (FogType not NONE).
[[nodiscard]] bool cameraInFluidNow();
/// Sets upstream's sky provider for the Gloaming Forest dimension (once it is known).
void installProvider();
/// GloamingConfig#getValidAuroraBiomes now: the config's biomes that parse and that the world knows (as
/// Bedrock names them: vanilla ones without "minecraft:").
[[nodiscard]] std::vector<std::string> validAuroraBiomes();
/// The configured auroraBiomes as written in the client's config.
[[nodiscard]] std::vector<std::string> auroraBiomesConfigured();
/// Sets auroraBiomes in the client's own config file (config/atmosphere.json) and applies it now
/// (the aurora biome list editor); false when the file could not be written.
bool setAuroraBiomes(std::vector<std::string> const& biomes);

} // namespace gf::client::atmosphere

#endif // GF_CLIENT

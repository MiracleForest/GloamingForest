#pragma once

// F10-internal parts of gf::fx (selftests, the client handler).

#include <cstdint>
#include <string>

namespace gf::fx {

struct Resolved {
    std::string effect;        ///< Bedrock particle effect to send to vanilla clients ("" = none)
    bool        native = false; ///< a mod particle companions may draw natively
    bool        vanilla = false; ///< a vanilla particle type (mapped)
};
/// How a particle id is played (warns once for unknown ids).
[[nodiscard]] Resolved resolveParticle(std::string const& particleId);
/// The Bedrock sound name of a sound event ("" = unknown, warned once).
[[nodiscard]] std::string resolveSound(std::string const& event);
/// Forgets the resource pack ids (reloaded on the next use).
void resetKnown();

struct Counters {
    std::uint64_t particlePackets = 0;
    std::uint64_t soundPackets    = 0;
};
/// Packets sent so far (selftests).
[[nodiscard]] Counters counters();

} // namespace gf::fx

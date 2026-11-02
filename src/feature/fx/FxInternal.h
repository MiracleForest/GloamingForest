#pragma once

// F10-internal parts of gf::fx (selftests, the client handler).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

class MolangVariableMap;

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
    std::uint64_t movingPackets   = 0; ///< particle packets with `variable.velocity`
};
/// Packets sent so far (selftests).
[[nodiscard]] Counters counters();

// ---- what both sides of gf::fx::particle share -------------------------------------------------

/// How many particles one gf::fx::particle call shows (`count` clamped to 1..cap). A vanilla client
/// gets one SpawnParticleEffectPacket per particle, so the server sends at most 8 (network). A
/// companion's client spawns the resource pack effect itself when no native factory draws the
/// particle: up to 64, Java's count as its native factories get it (F12's spawnFromFx allows 256).
inline constexpr int kMaxVanillaParticles = 8;
inline constexpr int kMaxClientEffects    = 64;

/// params "spread" [dx, dy, dz] (three numbers; anything else: none).
[[nodiscard]] inline std::optional<Vec3> spreadOf(nlohmann::json const& params) {
    if (!params.is_object()) return std::nullopt;
    auto s = params.find("spread");
    if (s == params.end() || !s->is_array() || s->size() != 3) return std::nullopt;
    for (auto const& v : *s)
        if (!v.is_number()) return std::nullopt;
    return Vec3{(*s)[0].get<float>(), (*s)[1].get<float>(), (*s)[2].get<float>()};
}

/// ServerLevel#sendParticles' offset of one particle: `pos` plus a gaussian times the spread per axis.
[[nodiscard]] inline Vec3 spreadAt(Vec3 const& pos, Vec3 const& spread, std::mt19937& rng) {
    std::normal_distribution<float> gauss(0.f, 1.f);
    float                           dx = gauss(rng) * spread.x, dy = gauss(rng) * spread.y, dz = gauss(rng) * spread.z;
    return Vec3{pos.x + dx, pos.y + dy, pos.z + dz};
}

/// A MolangVariableMap owned by gf::fx: an effect's molang variables (FxMolang.cpp). The game's
/// default constructor of the map is not exported; this one constructs the map's members in place
/// (layout checked at compile time) and destroys it with the game's destructor.
class MolangMap {
public:
    MolangMap();
    ~MolangMap();
    MolangMap(MolangMap const&)            = delete;
    MolangMap& operator=(MolangMap const&) = delete;

    [[nodiscard]] MolangVariableMap&       get();
    [[nodiscard]] MolangVariableMap const& get() const;
    /// `variable.velocity` {x, y, z}: the particle's velocity (blocks per tick), the name F12's
    /// native particle factories read (feature/ext/ui/Client.h) and a resource pack effect may use.
    void setVelocity(Vec3 const& velocity);

private:
    alignas(8) std::byte mStorage[56];
};

/// The map gf::fx gives an effect for `velocity`: `variable.velocity` when it moves, nothing otherwise.
void fillVariables(MolangMap& map, Vec3 const& velocity);

#ifdef GF_CLIENT
namespace client {
/// The client's "fx.particle" messages so far (selftests): drawn by the native sink (F12's
/// factories), spawned as the resource pack effect, or silent (no native version, no effect).
struct ClientCounters {
    std::uint64_t native  = 0;
    std::uint64_t effects = 0;
    std::uint64_t silent  = 0;
};
[[nodiscard]] ClientCounters clientCounters();
/// Whether a native sink is installed (F12 installs gf::client::particles::spawnFromFx).
[[nodiscard]] bool hasNativeSink();
/// The velocity the last resource pack effect of the fallback got as `variable.velocity`
/// (nullopt: none since the start, or it had no velocity). Selftests.
[[nodiscard]] std::optional<Vec3> lastEffectVelocity();
} // namespace client
#endif

} // namespace gf::fx

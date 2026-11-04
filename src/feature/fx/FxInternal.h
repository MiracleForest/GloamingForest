#pragma once

// F10-internal parts of gf::fx (selftests, the client handler).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

class MolangVariableMap;

namespace gf::fx {

/// The molang variables a Bedrock particle effect reads (and gf::fx sets), and what a legacy
/// particle's data comes from.
enum EffectVar : std::uint32_t {
    kVarVelocity        = 1u << 0, ///< `variable.velocity` {x, y, z}, set even when still
    kVarDirection       = 1u << 1, ///< `variable.direction` {x, y, z}: the particle's velocity
    kVarSplashDirection = 1u << 2, ///< with kVarDirection: SplashParticle's motion (splashDirection)
    kVarColorRgb        = 1u << 3, ///< `variable.color` {r, g, b}
    kVarColorRgba       = 1u << 4, ///< `variable.color` {r, g, b, a}
    kVarNoteColor       = 1u << 5, ///< `variable.note_color` {r, g, b}: NoteParticle's hue of the x speed
    kVarItem            = 1u << 6, ///< legacy data: params "item" (id, optional "aux")
    kVarBlock           = 1u << 7, ///< legacy data: params "block" (id, optional "states")
    kVarExplosionVelocity = 1u << 8, ///< with kVarVelocity: explosion_particle's `-velocity * 10` blocks/s (effectVelocity)
    kVarCritDirection   = 1u << 9,  ///< with kVarDirection: CritParticle's motion (critDirection)
    kVarDustColor       = 1u << 10, ///< params "color" (DustParticleOptions) picks the particle (dustFor)
};

struct Rgba {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
};

/// How a Java vanilla particle type is shown on Bedrock (Fx.cpp's table).
struct VanillaParticle {
    std::string         effect;     ///< the Bedrock effect ("" with `legacy`)
    std::uint32_t       vars = 0;   ///< EffectVar bits
    Vec3                offset{};   ///< added to the position
    int                 legacy = 0; ///< a legacy ParticleType (LevelEventPacket) instead of an effect
    std::optional<Rgba> color;      ///< the colour when params give none
};

struct Resolved {
    std::string         effect;          ///< Bedrock particle effect to send to vanilla clients ("" = none)
    bool                native  = false; ///< a mod particle companions may draw natively
    bool                vanilla = false; ///< a vanilla particle type (mapped)
    std::uint32_t       vars    = 0;     ///< EffectVar bits of the effect
    Vec3                offset{};        ///< added to the position
    int                 legacy = 0;      ///< legacy ParticleType (0: the effect)
    std::optional<Rgba> color;           ///< default colour (kVarColorRgb / kVarColorRgba)
    [[nodiscard]] bool  shown() const { return !effect.empty() || legacy != 0; }
};
/// The Java vanilla particle types gf::fx maps (selftests).
[[nodiscard]] std::vector<std::string> vanillaParticleIds();
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
    std::uint64_t variablePackets = 0; ///< particle packets with any molang variable
    std::uint64_t legacyPackets   = 0; ///< legacy particles (LevelEventPacket)
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
    /// `name` as a struct {x, y, z}.
    void setVec3(char const* name, Vec3 const& value);
    /// `name` as a struct {r, g, b} or, with `alpha`, {r, g, b, a}.
    void setColor(char const* name, Rgba const& color, bool alpha);
    /// How many variables the map holds (selftests).
    [[nodiscard]] std::size_t size() const;

private:
    alignas(8) std::byte mStorage[56];
};

/// The map gf::fx gives an effect for `velocity`: `variable.velocity` when it moves, nothing otherwise.
void fillVariables(MolangMap& map, Vec3 const& velocity);

/// The variables an effect with `vars` (EffectVar bits) reads: `variable.velocity` when it moves
/// or kVarVelocity asks for it, `variable.direction` (`direction`), `variable.color` (`color`),
/// `variable.note_color` (NoteParticle's colour of `velocity.x`).
void fillEffectVariables(MolangMap& map, std::uint32_t vars, Vec3 const& velocity, Vec3 const& direction, Rgba const& color);

/// NoteParticle's colour for the x speed (0..1: the note / 24).
[[nodiscard]] Rgba noteColor(float xSpeed);

/// The `variable.velocity` an effect with `vars` gets for Java's velocity (blocks per tick):
/// explosion_particle (kVarExplosionVelocity) starts at `-variable.velocity * 10` blocks per second,
/// so it gets -2 * velocity (Java's ExplodeParticle moves velocity blocks per tick: 20 * velocity
/// blocks per second); any other effect gets the velocity itself.
[[nodiscard]] Vec3 effectVelocity(std::uint32_t vars, Vec3 const& velocity);

/// Particle(level, x, y, z, xSpeed, ySpeed, zSpeed)'s initial motion (Java): the speed plus a
/// random +-0.4 per axis, rescaled to a random length (0.15..0.45 * 0.4 = 0.06..0.18) with 0.1
/// added to y.
[[nodiscard]] Vec3 particleMotion(std::mt19937& rng, Vec3 const& speed);

/// SplashParticle's motion: its speed with yd 0.1 when ySpeed is 0 and xSpeed or zSpeed is not;
/// any other speed is dropped for WaterDropParticle's own motion (Particle's motion of a still
/// particle, x and z times 0.3, y 0.1..0.3).
[[nodiscard]] Vec3 splashDirection(std::mt19937& rng, Vec3 const& velocity);

/// CritParticle's motion: Particle's motion of a still particle times 0.1 plus 0.4 * the speed.
[[nodiscard]] Vec3 critDirection(std::mt19937& rng, Vec3 const& velocity);

/// How a dust particle (kVarDustColor) of DustParticleOptions' colour (params "color", `r.color`
/// without) is shown: Java's redstone red (1, 0, 0) as Bedrock's legacy RedDust (its data is no
/// colour: the game draws it red), any other colour as the falling_dust effect tinted with it.
[[nodiscard]] Resolved dustFor(Resolved r, nlohmann::json const& params);

/// The colour a particle gets: params "color" (an integer 0xRRGGBB, or [r, g, b(, a)] in 0..1),
/// else `fallback` (white without).
[[nodiscard]] Rgba colorOf(nlohmann::json const& params, std::optional<Rgba> const& fallback);

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

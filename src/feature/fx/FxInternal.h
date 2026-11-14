#pragma once

// F10-internal parts of gf::fx (selftests, the client handler).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"

class Block;
class BlockSource;
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
    kVarDustDirection   = 1u << 10, ///< with kVarDirection: DustParticleBase's motion (dustDirection)
    kVarDustLook        = 1u << 11, ///< `variable.dust_size` and `variable.dust_ticks`: DustParticleBase's size and lifetime (dustLook)
};

/// DustParticleBase's random size and lifetime for DustParticleOptions' scale (dustLook).
struct DustLook {
    float size  = 0.15f * 0.75f; ///< quadSize: 0.1 * (0.5..1) * 2 * 0.75 * scale
    float ticks = 8.f;           ///< lifetime: max((int)(8 / (0.2..1)) * scale, 1) ticks
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
/// Whether the mod's resource pack(s) define the particle effect `effectId` (selftests).
[[nodiscard]] bool packHasEffect(std::string const& effectId);
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
    /// `name` as a number.
    void setFloat(char const* name, float value);
    /// How many variables the map holds (selftests).
    [[nodiscard]] std::size_t size() const;

private:
    alignas(8) std::byte mStorage[56];
};

/// The map gf::fx gives an effect for `velocity`: `variable.velocity` when it moves, nothing otherwise.
void fillVariables(MolangMap& map, Vec3 const& velocity);

/// The variables an effect with `vars` (EffectVar bits) reads: `variable.velocity` when it moves
/// or kVarVelocity asks for it, `variable.direction` (`direction`), `variable.color` (`color`),
/// `variable.note_color` (NoteParticle's colour of `velocity.x`), `variable.dust_size` and
/// `variable.dust_ticks` (`dust`).
void fillEffectVariables(MolangMap& map, std::uint32_t vars, Vec3 const& velocity, Vec3 const& direction, Rgba const& color, DustLook const& dust = {});

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

/// DustParticleBase's motion: Particle's motion for the speed times 0.1 (blocks per tick; the
/// effect gf:dust then slows it by the friction 0.96 per tick).
[[nodiscard]] Vec3 dustDirection(std::mt19937& rng, Vec3 const& velocity);

/// DustParticleOptions' scale: params "scale" clamped to 0.01..4 as its codec clamps it, 1 without
/// (DustParticleOptions.REDSTONE).
[[nodiscard]] float dustScale(nlohmann::json const& params);

/// DustParticleBase's size and lifetime of one particle for `scale` (Java's random draws; the
/// server rolls them so the effect needs no emitter expression).
[[nodiscard]] DustLook dustLook(std::mt19937& rng, float scale);

/// The colour a particle gets: params "color", else `fallback` (white without). An integer is
/// 0xAARRGGBB (Java's ColorParticleOption ARGB int) when any of bits 24-31 is set, else an opaque
/// 0xRRGGBB: an ARGB colour with alpha 0 (invisible in Java) cannot be told from an opaque RGB one,
/// so a caller that needs alpha 0 passes [r, g, b, 0]. An array is [r, g, b(, a)] in 0..1.
[[nodiscard]] Rgba colorOf(nlohmann::json const& params, std::optional<Rgba> const& fallback);

/// FallingDustParticle's colour of a block (FxBlockColor.cpp): a FallingBlock's dust colour, else
/// the block's BlockColor without a level (vanilla's and upstream's, tint index 0) as 0xRRGGBB, -1 for
/// one that draws white; nullopt when the block has none (the map colour applies). `intState`
/// reads the block's integer states (redstone_signal, growth, facing_direction, fill_level),
/// `stringState` its string states (cauldron_liquid).
[[nodiscard]] std::optional<int> fallingDustColor(std::string const& blockId, std::function<std::optional<int>(std::string_view)> const& intState,
                                                  std::function<std::optional<std::string>(std::string_view)> const& stringState = {});
/// A fallingDustColor value as a colour (-1: white).
[[nodiscard]] Rgba dustRgb(int color);
/// FallingDustParticle's colour of `block` (Fx.cpp): fallingDustColor with the block's integer and
/// string states, else the block state's map colour at `pos` of `region` (BlockColors#getColor:
/// state.getMapColor(level, pos)), the type's map colour without a region.
[[nodiscard]] Rgba blockDustColor(Block const& block, BlockSource* region, Vec3 const& pos);

/// The block of params "block" (+ "states"), and the map colour Java gives its state where Bedrock
/// keeps it out of the block state (a bed's: BedBlock's FOOT the dye's map colour, HEAD WOOL).
struct ParamBlock {
    Block const*        block = nullptr; ///< nullptr: params name no known block
    std::optional<Rgba> mapColor;
};
/// params "block" (+ "states") as a Bedrock block (Fx.cpp). Java's ids Bedrock folds into one
/// block are translated: minecraft:water_cauldron / powder_snow_cauldron (state "level" 1..3) and
/// lava_cauldron are minecraft:cauldron with cauldron_liquid and fill_level (level * 2, lava 6), the
/// 16 minecraft:<dye>_bed (state "part") minecraft:bed with head_piece_bit and Java's map colour.
[[nodiscard]] ParamBlock paramBlockOf(nlohmann::json const& params);
/// FallingDustParticle's colour of a params block at `pos`: its map colour when it carries one,
/// else blockDustColor; nullopt without a block.
[[nodiscard]] std::optional<Rgba> paramDustColor(ParamBlock const& block, BlockSource* region, Vec3 const& pos);

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

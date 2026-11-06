// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U24 (hydra) internals shared by the files of src/feature/hydra (not a contract: other units
// reference the Hydra, its mortar, the fire jets and smokers by their string ids only, rule R2).
//
//   Hydra.cpp     the boss (Hydra, HydraHeadContainer, HydraHead / HydraNeck / HydraSmallPart parts)
//   Mortar.cpp    twilightforest:hydra_mortar (HydraMortar)
//   Jets.cpp      fire_jet, encased_fire_jet, smoker, encased_smoker and their block entities
//   Lair.cpp      the hydra lair landmark (HydraLairStructure, HydraLairComponent) and the
//                 twilightforest:fire_jet feature type (FireJetFeature)
//   Module.cpp    registration
//   Tests.cpp     the hydra.* selftests (dx 2400..2499)

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"

#include "core/GlobalPos.h"
#include "core/hydra/Head.h"

class Actor;
class BlockPos;
class BlockSource;
class Mob;

namespace gf::feature::hydra {

inline constexpr char kHydra[]       = "twilightforest:hydra";
inline constexpr char kHydraPart[]   = "twilightforest:hydra_part";
inline constexpr char kMortar[]      = "twilightforest:hydra_mortar";
inline constexpr char kSpawner[]     = "twilightforest:hydra_boss_spawner";
inline constexpr char kLair[]        = "twilightforest:hydra_lair";
inline constexpr char kFireJet[]     = "twilightforest:fire_jet";
inline constexpr char kEncasedJet[]  = "twilightforest:encased_fire_jet";
inline constexpr char kSmoker[]      = "twilightforest:smoker";
inline constexpr char kEncasedSmoker[] = "twilightforest:encased_smoker";

// ---- the boss (Hydra.cpp) --------------------------------------------------------------------------

void installHydra();
void uninstallHydra();

/// What a selftest can read and drive of a live hydra (nullptr / nullopt: no such hydra).
struct HeadView {
    core::hydra::State state = core::hydra::State::Dead;
    int                damageTaken    = 0;
    int                respawnCounter = -1;
    Vec3               pos{};
    float              yRot = 0, xRot = 0;
    float              mouth = 0;
    bool               active = false;
    std::string        name;
};
[[nodiscard]] std::optional<HeadView> headView(Actor& hydra, int head);
/// The part actors' positions as F8 placed them (body, tail, then per head the head and necks).
[[nodiscard]] std::vector<Vec3> partPositions(Actor& hydra);
/// The positions the formulas give this tick (same order as partPositions).
[[nodiscard]] std::vector<Vec3> expectedPartPositions(Actor& hydra);
/// Forces a head's next state (selftests: "bite now").
void forceHeadState(Actor& hydra, int head, core::hydra::State state);
/// Adds to a head's damage counter (the hit path is tested separately).
void addHeadDamage(Actor& hydra, int head, int damage);
/// Points head `head` at `point` (no target entity): the flame states face it (selftests and the
/// client showcase).
void aimHeadForTest(Actor& hydra, int head, Vec3 const& point);
/// Sets the hydra's (and its heads') target (selftests).
void setTarget(Actor& hydra, Actor* target);
/// Turns the brain off (no target picking, no attacks chosen, no turning): the head state machine
/// and the forced states still run.
void setBrainEnabled(Actor& hydra, bool enabled);
/// Seeds the hydra's random (its brain: attack odds, targets, the head chosen to respawn, the
/// ambient growl); before its first tick the seed waits for it.
void seedForTest(Actor& hydra, std::int64_t seed);
/// Whether the hydra's actor has gravity now (false while the Java box rests on a block).
[[nodiscard]] bool hasGravityForTest(Actor& hydra);
/// Damage the hydra took through parts so far.
[[nodiscard]] float damageTakenForTest(Actor& hydra);
/// Hydra#ticksSinceDamaged (-1: no such hydra).
[[nodiscard]] int ticksSinceDamagedForTest(Actor& hydra);
/// The part id of a part actor of a hydra ("" when not one).
[[nodiscard]] std::string partIdOf(Actor& part);

// ---- the mortar (Mortar.cpp) -------------------------------------------------------------------

void installMortar();
void uninstallMortar();
/// HydraMortar(level, head): launches a mortar from head `head` of `hydra`.
Actor* launchMortar(Mob& hydra, int head, Vec3 const& headPos, float headXRot, float headYRot, bool blasting);
/// The mortar's state (selftests).
struct MortarView {
    int          fuse      = 0;
    bool         onGround  = false;
    bool         blasting  = false;
    std::int64_t owner     = 0;
};
[[nodiscard]] std::optional<MortarView> mortarView(Actor& mortar);
/// HydraMortar#hurt by `attacker` (the reflect), as a player's hit does. `projectile`: the causing
/// entity is a shooterless projectile, which looks along core::hydra::projectileLook of its motion
/// (Java's Projectile rotation); anything else looks along its view vector. The owner changes only
/// for a living attacker.
void reflectMortar(Actor& mortar, Actor& attacker, bool projectile = false);
/// Mortars detonated so far (selftests).
[[nodiscard]] int detonationsForTest();

// ---- jets and smokers (Jets.cpp) -----------------------------------------------------------------

void installJets();
void uninstallJets();
/// The state value of a fire jet / encased fire jet ("idle", "popping", "flame", "timeout"; "" when
/// the block is none).
[[nodiscard]] std::string jetState(BlockSource& region, BlockPos const& pos);
/// FireJetBlock#randomTick at `pos` (selftests).
void randomTickJet(BlockSource& region, BlockPos const& pos);
/// The neighbour change of an encased fire jet / smoker at `pos` (selftests).
void neighborChanged(BlockSource& region, BlockPos const& pos);
/// The block entity counter of a ticking jet (-1 when not ticking).
[[nodiscard]] int jetCounter(core::GlobalPos const& pos);
/// Whether the redstone of the encased block at `pos` is watched (selftests).
[[nodiscard]] bool encasedWatched(core::GlobalPos const& pos);
/// Whether the smoker at `pos` ticks (its TFSmokerBlockEntity; selftests).
[[nodiscard]] bool smokerTracked(core::GlobalPos const& pos);
/// The twilightforest:fire_jet feature (world generation) placed a jet or smoker at block (x, y, z):
/// its placement is not remembered (the palette scan around the players finds it). Any thread.
void noteGeneratedJet(int x, int y, int z);
/// Whether the jet / smoker at `pos` is remembered with the world (gf:hydra:jets; selftests).
[[nodiscard]] bool jetRemembered(core::GlobalPos const& pos);
/// Saves the remembered jets and smokers and forgets everything in memory, as a world reload does
/// (selftests).
void reloadJetsForTest();
/// Smoker particles emitted so far (selftests).
[[nodiscard]] int smokeParticlesForTest();

// ---- the lair and the feature (Lair.cpp) -----------------------------------------------------------

void installLair();
void uninstallLair();

} // namespace gf::feature::hydra

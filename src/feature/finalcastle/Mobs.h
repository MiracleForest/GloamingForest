#pragma once

// U30 (finalcastle): the mobs part (adherent, harbinger cube, roving cube, plateau boss and the final
// boss spawner) and the gazebo's WIP texts. Shared by Mobs.cpp, Wip.cpp and MobTests.cpp only.
// Server thread unless said otherwise; rule R16: nothing here keeps Actor* / Player* across ticks.

#include <cstdint>
#include <optional>
#include <string>

#include "mc/world/level/BlockPos.h"

#include "core/finalcastle/Mobs.h"
#include "feature/worldgen/Worldgen.h"

class Actor;
class BlockSource;
class Mob;

namespace gf::feature::finalcastle::mobs {

namespace id {
inline constexpr char Adherent[]      = "gloamingforest:adherent";
inline constexpr char HarbingerCube[] = "gloamingforest:harbinger_cube";
inline constexpr char RovingCube[]    = "gloamingforest:roving_cube";
inline constexpr char PlateauBoss[]   = "gloamingforest:plateau_boss";
inline constexpr char BossSpawner[]   = "gloamingforest:final_boss_boss_spawner";
inline constexpr char NatureBolt[]    = "gloamingforest:nature_bolt";
inline constexpr char FinalCastle[]   = "gloamingforest:final_castle";
inline constexpr char CanopyChest[]   = "gloamingforest:canopy_chest";
inline constexpr char Annihilate[]    = "gloamingforest:annihilate";
/// The helper actors of the WIP texts (not upstream content: GloamingForest's helper namespace).
inline constexpr char WipText[]  = "gloamingforest:final_castle_text";
inline constexpr char WipPunch[] = "gloamingforest:final_castle_punch";
/// The template marker of the gazebo (FinalCastleBossGazeboComponent.INTERACTION_TAG).
inline constexpr char WipMarker[] = "final_castle_wip";
} // namespace id

/// Installs the mob behaviours, goals, placements and the boss kit registrations.
void installMobs();

// ---- roving cube (test seams) -----------------------------------------------------------------------

/// RovingCube#symbolX / Y / Z and hasFoundSymbol of the cube `uid` (nullopt: none known).
struct Symbol {
    bool                      found = false;
    core::finalcastle::IPos   pos;
};
[[nodiscard]] std::optional<Symbol> symbolOf(std::int64_t uid);
void                                 setSymbolForTest(std::int64_t uid, core::finalcastle::IPos pos);
/// CubeMoveToRedstoneSymbolsGoal#isRedstoneSymbol in the live world.
[[nodiscard]] bool isRedstoneSymbol(BlockSource& region, BlockPos const& pos);

// ---- adherent (test seams) --------------------------------------------------------------------------

/// Adherent#performRangedAttack calls so far (every adherent).
[[nodiscard]] int adherentShotsForTest();

// ---- WIP texts (Wip.cpp) ----------------------------------------------------------------------------

/// Installs the template marker handler, the spawn queue and the punch handler.
void installWip();
/// The marker handler's work at block `pos` (willBeAddingFinalBossSoon): queues the four text
/// carriers and the punch carrier, spawned on the server thread once the chunk is loaded. Any thread.
void queueWip(worldgen::Region& region, BlockPos const& pos);
/// Spawns the queued carriers whose chunk is loaded now (also run every few ticks).
void flushWip();
/// upstream's removeCastleTextIfAttacked on the punch carrier `punch`: removes the text carriers inside its
/// box, then the punch carrier (deferred to the next tick).
void punch(Actor& punchCarrier);
/// Text carriers / punch carriers queued, not yet spawned.
[[nodiscard]] int pendingWipForTest();

} // namespace gf::feature::finalcastle::mobs

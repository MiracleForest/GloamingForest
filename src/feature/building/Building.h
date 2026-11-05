#pragma once

// U5 (building): the unit's internal declarations (not a contract; other units reach the unit's
// blocks by string id only, rule R2).

#include <optional>
#include <string>
#include <vector>

#include "feature/building/Util.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;

namespace gf::feature::building {

/// Config section "building" (R13, docs/spec/config.md): TFClientConfig.rotateTrophyHeadsGui.
struct BuildingConfig {
    int version = 1;
    /// client.rotateTrophyHeadsGui (animate_trophies): trophy heads spin in GUI slots (client mod).
    bool rotateTrophyHeadsGui = true;
};

// ---- trophies (Trophies.cpp) ----
[[nodiscard]] std::vector<std::string> const& trophyBosses();
/// The boss of a trophy block id ("naga" for naga_trophy / naga_wall_trophy).
[[nodiscard]] std::optional<std::string> trophyBoss(std::string const& blockId);
[[nodiscard]] bool isWallTrophy(std::string const& blockId);
/// TrophyBlock's rotation for a placer's yaw.
[[nodiscard]] int trophyRotation(float yaw);
struct TrophySound {
    std::string event;
    float       volume = 1.0f;
    float       pitch  = 1.0f; ///< before the + nextFloat() * 0.1
};
/// AbstractTrophyBlock#playSound's sound for `roll` = a uniform 0..1 draw (the alpha yeti roars
/// once in 50: roll < 1 / 50).
[[nodiscard]] std::optional<TrophySound> trophySound(std::string const& blockId, float roll);
void playTrophySound(BlockSource& region, BlockPos const& pos, std::string const& blockId);
void createTrophyParticles(BlockSource& region, BlockPos const& pos, std::string const& blockId);
void installTrophies();
void uninstallTrophies();

// ---- nagastone, spiral bricks, iron ladder (Nagastone.cpp) ----
/// NagastoneVariant.getVariantFromDoubleFacing for one (second = -1) or two connections (-1, -1: solid).
[[nodiscard]] std::string nagastoneVariant(int first, int second);
[[nodiscard]] std::string nagastoneVariantAt(BlockSource& region, BlockPos const& pos);
/// SpiralBrickBlock#getStateForPlacement's diagonal.
[[nodiscard]] std::string spiralDiagonal(bool sneaking, int horizontalDir, int playerDir, bool upper);
void installNagastone();
void uninstallNagastone();

// ---- rope (Rope.cpp) ----
inline constexpr int kRopeExtendRange = 7; ///< RopeItem.EXTEND_RANGE
[[nodiscard]] bool ropeHasAxis(Block const& rope, int axis);
[[nodiscard]] bool ropeCanConnectTo(BlockSource& region, BlockPos const& pos, int dir);
[[nodiscard]] bool ropeCheckConnection(BlockSource& region, BlockPos const& pos, int dir);
[[nodiscard]] bool ropeCanSurvive(BlockSource& region, BlockPos const& pos, Block const& rope);
/// RopeBlock#tick: drops the unsupported strands; returns the number of ropes dropped.
int  ropeTick(BlockSource& region, BlockPos const& pos);
void installRope();
void uninstallRope();

// ---- storage blocks (Storage.cpp) ----
inline constexpr float kKnightmetalDamage       = 4.0f; ///< KnightmetalBlock.BLOCK_DAMAGE
inline constexpr float kFieryDamage             = 1.0f; ///< FieryBlock#stepOn
inline constexpr float kArcticFurFallMultiplier = 0.1f; ///< ArcticFurBlock#fallOn
[[nodiscard]] float arcticFurFallDamage(float amount);
[[nodiscard]] bool  fieryHurts(Actor& entity);
void installStorage();

} // namespace gf::feature::building

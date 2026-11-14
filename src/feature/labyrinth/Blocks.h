// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U23 (labyrinth): the mazebreaker tool rule and the maze slime block (unit-internal).

#include <string>

class ItemStack;
class Player;

namespace gf::feature::labyrinth::blocks {

inline constexpr char kAcceleratedTag[] = "gloamingforest:mazebreaker_accelerated_mining";
inline constexpr char kMazebreaker[]    = "gloamingforest:mazebreaker_pickaxe";
inline constexpr char kMazeSlimeBlock[] = "gloamingforest:maze_slime_block";
/// ToolEvents#damageNonMazebreakerToolsMore: the extra durability lost.
inline constexpr int kMazestonePenalty = 16;

void install();

/// Whether breaking a mazebreaker-accelerated block with `stack` costs it the penalty (a damageable
/// tool other than the mazebreaker).
[[nodiscard]] bool damagesTool(std::string const& itemId, ItemStack const& stack);
/// ItemStack#hurtAndBreak(16, player, MAINHAND) on the held stack.
void applyToolPenalty(Player& player, ItemStack const& held);
/// SlimeBlock#bounceUp: the upward speed after landing with vertical speed `fallDy`.
[[nodiscard]] double bounceSpeed(double fallDy, bool living);

} // namespace gf::feature::labyrinth::blocks

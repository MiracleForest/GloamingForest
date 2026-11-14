// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
#pragma once

// U23 (labyrinth): the Minotaur, the Minoshroom boss and the Maze Slime (unit-internal).

#include <cstdint>
#include <string>

class Actor;
class Mob;

namespace gf::feature::labyrinth::mobs {

inline constexpr char kMinotaur[]   = "gloamingforest:minotaur";
inline constexpr char kMinoshroom[] = "gloamingforest:minoshroom";
inline constexpr char kMazeSlime[]  = "gloamingforest:maze_slime";

void install();

/// The Java ATTACK_DAMAGE attribute value of a minotaur: 2 (Monster) + the main hand axe's
/// modifier (golden axe and gold minotaur axe 6, diamond minotaur axe 9).
[[nodiscard]] float attackDamage(Mob& mob);
/// The modifier of a held item id (0 for anything else).
[[nodiscard]] float weaponModifier(std::string const& itemId);

/// MazeSlime#setSize (selftests): the size, max health 3 x size^2, xp size + 3.
void setSlimeSize(Mob& slime, int size);
[[nodiscard]] int slimeSize(Actor& slime);

/// Minotaur#populateDefaultEquipmentSlots: gold minotaur axe when (int)(nextInt(10) /
/// (effectiveDifficulty + 1)) == 0, else a golden axe.
[[nodiscard]] char const* minotaurAxe(int roll, float effectiveDifficulty);

/// GroundAttackGoal's slam now (selftests): the players hit.
int groundSlamForTest(Mob& minoshroom);

} // namespace gf::feature::labyrinth::mobs

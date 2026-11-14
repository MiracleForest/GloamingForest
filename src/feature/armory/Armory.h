// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U7 (armory): the unit's internal API (selftests). Behaviour notes in Module.cpp.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Actor;
class ItemStack;

namespace gf::feature::armory {

namespace ids {
inline constexpr char kFierySword[]         = "gloamingforest:fiery_sword";
inline constexpr char kFieryPickaxe[]       = "gloamingforest:fiery_pickaxe";
inline constexpr char kKnightmetalSword[]   = "gloamingforest:knightmetal_sword";
inline constexpr char kKnightmetalPickaxe[] = "gloamingforest:knightmetal_pickaxe";
inline constexpr char kKnightmetalAxe[]     = "gloamingforest:knightmetal_axe";
inline constexpr char kGoldMinotaurAxe[]    = "gloamingforest:gold_minotaur_axe";
inline constexpr char kDiamondMinotaurAxe[] = "gloamingforest:diamond_minotaur_axe";
inline constexpr char kMazebreaker[]        = "gloamingforest:mazebreaker_pickaxe";
inline constexpr char kArcticBoots[]        = "gloamingforest:arctic_boots";
inline constexpr char kYetiBoots[]          = "gloamingforest:yeti_boots";
inline constexpr char kEmperorsCloth[]      = "gloamingforest:emperors_cloth";
inline constexpr char kFrosted[]            = "gloamingforest:frosted";
inline constexpr char kMazebreakerTag[]     = "gloamingforest:mazebreaker_accelerated_mining";
inline constexpr char kSmeltingModifier[]   = "gloamingforest:fiery_pick_smelting";
inline constexpr char kNoTemplateSmithing[] = "gloamingforest:no_template_smithing";
inline constexpr char kClothSmithing[]      = "gloamingforest:emperors_cloth_smithing";
inline constexpr char kKnightmetalEquip[]   = "item.gloamingforest.knightmetal_armor.equip";
} // namespace ids

/// Worn pieces of an upstream armour material ("fiery", "yeti", ...).
[[nodiscard]] int countPieces(Actor const& wearer, std::string_view material);
/// Smelts the drops as the fiery pickaxe's loot modifier does (FieryToolSmeltingModifier#doApply);
/// returns the summed recipe XP.
float smeltDrops(std::vector<ItemStack>& drops);
/// Forces the fiery set reaction's rand.nextInt(25) (nullopt: random again).
void setFireReactRollForTest(std::optional<int> roll);
/// The no-template smithing result of `base` + `addition` (nullopt: no recipe matches).
[[nodiscard]] std::optional<ItemStack> noTemplateSmithing(ItemStack const& base, ItemStack const& addition);

} // namespace gf::feature::armory

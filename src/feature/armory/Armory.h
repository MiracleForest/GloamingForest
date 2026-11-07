// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
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
inline constexpr char kFierySword[]         = "twilightforest:fiery_sword";
inline constexpr char kFieryPickaxe[]       = "twilightforest:fiery_pickaxe";
inline constexpr char kKnightmetalSword[]   = "twilightforest:knightmetal_sword";
inline constexpr char kKnightmetalPickaxe[] = "twilightforest:knightmetal_pickaxe";
inline constexpr char kKnightmetalAxe[]     = "twilightforest:knightmetal_axe";
inline constexpr char kGoldMinotaurAxe[]    = "twilightforest:gold_minotaur_axe";
inline constexpr char kDiamondMinotaurAxe[] = "twilightforest:diamond_minotaur_axe";
inline constexpr char kMazebreaker[]        = "twilightforest:mazebreaker_pickaxe";
inline constexpr char kArcticBoots[]        = "twilightforest:arctic_boots";
inline constexpr char kYetiBoots[]          = "twilightforest:yeti_boots";
inline constexpr char kEmperorsCloth[]      = "twilightforest:emperors_cloth";
inline constexpr char kFrosted[]            = "twilightforest:frosted";
inline constexpr char kMazebreakerTag[]     = "twilightforest:mazebreaker_accelerated_mining";
inline constexpr char kSmeltingModifier[]   = "twilightforest:fiery_pick_smelting";
inline constexpr char kNoTemplateSmithing[] = "twilightforest:no_template_smithing";
inline constexpr char kClothSmithing[]      = "twilightforest:emperors_cloth_smithing";
inline constexpr char kKnightmetalEquip[]   = "item.twilightforest.knightmetal_armor.equip";
} // namespace ids

/// Worn pieces of a TF armour material ("fiery", "yeti", ...).
[[nodiscard]] int countPieces(Actor const& wearer, std::string_view material);
/// Smelts the drops as the fiery pickaxe's loot modifier does (FieryToolSmeltingModifier#doApply);
/// returns the summed recipe XP.
float smeltDrops(std::vector<ItemStack>& drops);
/// Forces the fiery set reaction's rand.nextInt(25) (nullopt: random again).
void setFireReactRollForTest(std::optional<int> roll);
/// The no-template smithing result of `base` + `addition` (nullopt: no recipe matches).
[[nodiscard]] std::optional<ItemStack> noTemplateSmithing(ItemStack const& base, ItemStack const& addition);

} // namespace gf::feature::armory

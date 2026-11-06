// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFArmorMaterials.java
//   src/main/java/twilightforest/util/TFToolMaterials.java
//   src/main/java/twilightforest/init/TFItems.java
//   src/main/java/twilightforest/init/TFTrimMaterials.java
//   src/main/java/twilightforest/events/ToolEvents.java
//   src/main/java/twilightforest/events/EntityEvents.java
//   src/main/java/twilightforest/enchantment/ApplyFrostedEffect.java
//   src/main/java/twilightforest/loot/modifiers/FieryToolSmeltingModifier.java
//   src/main/java/twilightforest/data/CraftingGenerator.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U7 (armory), pure logic: the armour materials and tool tiers with TF's numbers, the attack and
// set-reaction rules of ToolEvents / EntityEvents, Java's armour absorption (CombatRules) for the
// armour toughness Bedrock's data items lack, the default enchantments of the crafted gear, the
// trim materials and the fiery smelting XP rounding. No game headers (gtested in tests/armory).
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gf::core::armory {

/// Java's Rarity.
enum class Rarity { Common, Uncommon, Rare, Epic };

/// ArmorItem.Type in Java's order (BOOTS, LEGGINGS, CHESTPLATE, HELMET).
enum class ArmorType { Boots = 0, Leggings = 1, Chestplate = 2, Helmet = 3 };

/// TFArmorMaterials entry.
struct ArmorMaterial {
    std::string_view   name;               ///< "ironwood" (twilightforest:<name>)
    std::array<int, 4> defense;            ///< by ArmorType
    int                enchantability;
    float              toughness;
    std::string_view   repairItem;         ///< item id
    int                durabilityMultiplier; ///< ArmorItem.Type#getDurability(m)
    Rarity             rarity;
    bool               fireResistant;
    std::string_view   equipSound;         ///< "" = armor.equip_generic
};

/// A TF armour piece.
struct ArmorPiece {
    std::string_view id; ///< "twilightforest:ironwood_helmet"
    std::string_view material;
    ArmorType        type;
};

/// The 8 materials (travellers is U11's).
[[nodiscard]] std::vector<ArmorMaterial> const& armorMaterials();
/// The 28 pieces.
[[nodiscard]] std::vector<ArmorPiece> const& armorPieces();
[[nodiscard]] ArmorMaterial const* armorMaterial(std::string_view name);
[[nodiscard]] ArmorPiece const*    armorPiece(std::string_view itemId);
/// ArmorItem.Type#getDurability: {13, 15, 16, 11}[type] * multiplier.
[[nodiscard]] int armorDurability(ArmorType type, int multiplier);
[[nodiscard]] int defenseOf(ArmorPiece const& piece);
[[nodiscard]] float toughnessOf(ArmorPiece const& piece);

/// Tiers (TFToolMaterials and the vanilla tiers the minotaur axes / mazebreaker use).
struct ToolTier {
    std::string_view name;
    int              uses;
    float            speed;
    float            attackDamageBonus;
    int              enchantability;
    std::string_view repairItem; ///< the repair tag's member
};
enum class ToolKind { Sword, Shovel, Pickaxe, Axe, Hoe };
struct Tool {
    std::string_view id;
    std::string_view tier;
    ToolKind         kind;
    float            attackDamage; ///< createAttributes' item damage
    float            attackSpeed;  ///< createAttributes' speed modifier
    Rarity           rarity;
    bool             fireResistant;
    bool             noRepair;
    int              enchantability; ///< 0 = the tier's
};
[[nodiscard]] std::vector<ToolTier> const& toolTiers();
[[nodiscard]] std::vector<Tool> const&     tools();
[[nodiscard]] ToolTier const*              toolTier(std::string_view name);
[[nodiscard]] Tool const*                  tool(std::string_view itemId);
/// The attack damage Java's player deals with the tool: 1 (base) + tier bonus + item damage.
[[nodiscard]] float attackDamage(Tool const& tool);
/// The attack speed attribute: 4 + the item's modifier.
[[nodiscard]] float attackSpeed(Tool const& tool);
/// The Bedrock "minecraft:damage" value (added to the player's base 1): floor(attackDamage - 1).
[[nodiscard]] int bedrockDamage(Tool const& tool);
/// What the integer Bedrock damage misses of Java's (the shovels' 0.5): attackDamage - 1 - bedrockDamage.
[[nodiscard]] float fractionalDamage(Tool const& tool);
[[nodiscard]] int enchantability(Tool const& tool);

/// TF's crafting materials (59 items in all with the pieces and the tools).
struct MaterialItem {
    std::string_view id;
    Rarity           rarity;
    bool             fireResistant;
};
[[nodiscard]] std::vector<MaterialItem> const& materialItems();

// ---- ToolEvents ---------------------------------------------------------------------------------
inline constexpr int kKnightmetalBonusDamage = 2;
inline constexpr int kMinotaurAxeBonusDamage = 7;
inline constexpr int kMazebreakerSpeedFactor = 16;
inline constexpr int kMazebreakerExtraWear   = 16;
/// FierySword / FieryPick: 15 s on a hit; ToolEvents.fieryToolSetFire: 1 s on any damage.
inline constexpr int kFieryHitSeconds   = 15;
inline constexpr int kFieryToolSeconds  = 1;

enum class KnightmetalWeapon { None, SwordOrPickaxe, Axe };
struct Bonus {
    float damage   = 0;     ///< the new damage
    bool  sparkles = false; ///< ClientboundAnimatePacket(target, 5)
};
/// ToolEvents#doKnightmetalToolLogic: `armorValue` the target's armour value, `cover` its armour
/// cover percentage (0..1), `newDamage` / `originalDamage` the damage container's.
[[nodiscard]] Bonus knightmetal(KnightmetalWeapon weapon, int armorValue, float cover, float newDamage, float originalDamage);
/// ToolEvents#addExtraAxeChargingDamage: +7 while the attacker sprints with a minotaur axe.
[[nodiscard]] Bonus minotaurAxe(bool sprinting, float newDamage);
/// LivingEntity#getArmorCoverPercentage: worn armour slots / 4.
[[nodiscard]] float armorCover(int wornPieces);

// ---- EntityEvents#entityHurts -------------------------------------------------------------------
struct FireReact {
    int  fireLevel = 0;
    bool ignite    = false;
    int  seconds   = 0;
};
/// fiery pieces worn -> fireLevel = 5f; ignite when roll (rand.nextInt(25)) < fireLevel, for fireLevel / 2 s.
[[nodiscard]] FireReact fireReact(int fieryPieces, int roll);
struct ChillAura {
    bool apply     = false;
    int  duration  = 0; ///< ticks
    int  amplifier = 0;
};
/// yeti pieces worn y -> twilightforest:frosted for 5y + 5 ticks at amplifier y, only when y > 0.
[[nodiscard]] ChillAura chillAura(int yetiPieces);

// ---- CombatRules (Minecraft 1.21.1) -------------------------------------------------------------
/// CombatRules#getDamageAfterAbsorb (without the breach enchantment): damage * (1 - clamp(armor -
/// damage / (2 + toughness / 4), armor * 0.2, 20) / 25).
[[nodiscard]] float damageAfterAbsorb(float damage, float armor, float toughness);

// ---- FieryToolSmeltingModifier -------------------------------------------------------------------
/// ExperienceOrb.award's amount: Math.round(xp) (floor(xp + 0.5)).
[[nodiscard]] int smeltingXp(float xp);

// ---- CraftingGenerator: default enchantments of crafted gear ------------------------------------
struct DefaultEnchantments {
    std::string_view                                       recipe; ///< "twilightforest:equipment/ironwood_axe"
    std::string_view                                       item;
    std::vector<std::pair<std::string_view, int>>          enchantments; ///< Java ids
};
/// The 24 recipes whose result carries minecraft:enchantments.
[[nodiscard]] std::vector<DefaultEnchantments> const& defaultEnchantments();

// ---- TFTrimMaterials -------------------------------------------------------------------------------
struct TrimMaterial {
    std::string_view id;         ///< "twilightforest:fiery"
    std::string_view ingredient; ///< item id
    unsigned         rgb;        ///< description colour
    float            itemModelIndex;
    std::string_view descriptionKey;
};
[[nodiscard]] std::vector<TrimMaterial> const& trimMaterials();
/// The chat colour code (0..f) nearest to `rgb` (Bedrock's trim material colour is a § code).
[[nodiscard]] char nearestChatColor(unsigned rgb);

// ---- arctic armour -----------------------------------------------------------------------------------
/// ArcticArmorItem.DEFAULT_COLOR (opaque).
inline constexpr unsigned kArcticDefaultColor = 0xBDCFD9;

} // namespace gf::core::armory

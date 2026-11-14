// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFArmorMaterials.java
//   src/main/java/gloamingforest/util/TFToolMaterials.java
//   src/main/java/gloamingforest/init/TFItems.java
//   src/main/java/gloamingforest/init/TFTrimMaterials.java
//   src/main/java/gloamingforest/events/ToolEvents.java
//   src/main/java/gloamingforest/events/EntityEvents.java
//   src/main/java/gloamingforest/enchantment/ApplyFrostedEffect.java
//   src/main/java/gloamingforest/loot/modifiers/FieryToolSmeltingModifier.java
//   src/main/java/gloamingforest/data/CraftingGenerator.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/armory/Armory.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gf::core::armory {

std::vector<ArmorMaterial> const& armorMaterials() {
    // TFArmorMaterials: defense B/L/C/H, enchantability, toughness, repair ingredient; TFItems: the
    // durability multiplier, rarity and fireResistant of the pieces
    static std::vector<ArmorMaterial> const v{
        {"naga_scale", {3, 6, 7, 2}, 15, 0.5f, "gloamingforest:naga_scale", 21, Rarity::Uncommon, false, ""},
        {"ironwood", {2, 5, 7, 2}, 15, 0.0f, "gloamingforest:ironwood_ingot", 20, Rarity::Common, false, ""},
        {"fiery", {4, 7, 9, 4}, 10, 1.5f, "gloamingforest:fiery_ingot", 25, Rarity::Uncommon, true, ""},
        {"steeleaf", {3, 6, 8, 3}, 9, 0.0f, "gloamingforest:steeleaf_ingot", 10, Rarity::Common, false, ""},
        {"knightmetal", {3, 6, 8, 3}, 8, 1.0f, "gloamingforest:knightmetal_ingot", 20, Rarity::Common, false, "item.gloamingforest.knightmetal_armor.equip"},
        {"phantom", {3, 6, 8, 3}, 8, 2.5f, "gloamingforest:knightmetal_ingot", 30, Rarity::Uncommon, false, ""},
        {"yeti", {3, 6, 7, 4}, 15, 3.0f, "gloamingforest:alpha_yeti_fur", 20, Rarity::Uncommon, false, ""},
        {"arctic", {2, 5, 7, 2}, 8, 2.0f, "gloamingforest:arctic_fur", 10, Rarity::Common, false, ""},
    };
    return v;
}

std::vector<ArmorPiece> const& armorPieces() {
    using T = ArmorType;
    static std::vector<ArmorPiece> const v{
        {"gloamingforest:naga_chestplate", "naga_scale", T::Chestplate},
        {"gloamingforest:naga_leggings", "naga_scale", T::Leggings},
        {"gloamingforest:ironwood_helmet", "ironwood", T::Helmet},
        {"gloamingforest:ironwood_chestplate", "ironwood", T::Chestplate},
        {"gloamingforest:ironwood_leggings", "ironwood", T::Leggings},
        {"gloamingforest:ironwood_boots", "ironwood", T::Boots},
        {"gloamingforest:fiery_helmet", "fiery", T::Helmet},
        {"gloamingforest:fiery_chestplate", "fiery", T::Chestplate},
        {"gloamingforest:fiery_leggings", "fiery", T::Leggings},
        {"gloamingforest:fiery_boots", "fiery", T::Boots},
        {"gloamingforest:steeleaf_helmet", "steeleaf", T::Helmet},
        {"gloamingforest:steeleaf_chestplate", "steeleaf", T::Chestplate},
        {"gloamingforest:steeleaf_leggings", "steeleaf", T::Leggings},
        {"gloamingforest:steeleaf_boots", "steeleaf", T::Boots},
        {"gloamingforest:knightmetal_helmet", "knightmetal", T::Helmet},
        {"gloamingforest:knightmetal_chestplate", "knightmetal", T::Chestplate},
        {"gloamingforest:knightmetal_leggings", "knightmetal", T::Leggings},
        {"gloamingforest:knightmetal_boots", "knightmetal", T::Boots},
        {"gloamingforest:phantom_helmet", "phantom", T::Helmet},
        {"gloamingforest:phantom_chestplate", "phantom", T::Chestplate},
        {"gloamingforest:yeti_helmet", "yeti", T::Helmet},
        {"gloamingforest:yeti_chestplate", "yeti", T::Chestplate},
        {"gloamingforest:yeti_leggings", "yeti", T::Leggings},
        {"gloamingforest:yeti_boots", "yeti", T::Boots},
        {"gloamingforest:arctic_helmet", "arctic", T::Helmet},
        {"gloamingforest:arctic_chestplate", "arctic", T::Chestplate},
        {"gloamingforest:arctic_leggings", "arctic", T::Leggings},
        {"gloamingforest:arctic_boots", "arctic", T::Boots},
    };
    return v;
}

ArmorMaterial const* armorMaterial(std::string_view name) {
    for (auto const& m : armorMaterials())
        if (m.name == name) return &m;
    return nullptr;
}

ArmorPiece const* armorPiece(std::string_view itemId) {
    for (auto const& p : armorPieces())
        if (p.id == itemId) return &p;
    return nullptr;
}

int armorDurability(ArmorType type, int multiplier) {
    static constexpr std::array<int, 4> kFactor{13, 15, 16, 11};
    return kFactor[static_cast<int>(type)] * multiplier;
}

int defenseOf(ArmorPiece const& piece) {
    auto const* m = armorMaterial(piece.material);
    return m ? m->defense[static_cast<int>(piece.type)] : 0;
}

float toughnessOf(ArmorPiece const& piece) {
    auto const* m = armorMaterial(piece.material);
    return m ? m->toughness : 0.f;
}

std::vector<ToolTier> const& toolTiers() {
    // TFToolMaterials (repair tags #repairs_<tier>_tools = #c:ingots/<tier>) and Tiers.GOLD / DIAMOND
    static std::vector<ToolTier> const v{
        {"ironwood", 512, 6.5f, 2.0f, 25, "gloamingforest:ironwood_ingot"},
        {"fiery", 1024, 9.0f, 4.0f, 10, "gloamingforest:fiery_ingot"},
        {"steeleaf", 131, 8.0f, 3.0f, 9, "gloamingforest:steeleaf_ingot"},
        {"knightmetal", 512, 8.0f, 3.0f, 8, "gloamingforest:knightmetal_ingot"},
        {"gold", 32, 12.0f, 0.0f, 22, "minecraft:gold_ingot"},
        {"diamond", 1561, 8.0f, 3.0f, 10, "minecraft:diamond"},
    };
    return v;
}

std::vector<Tool> const& tools() {
    using K = ToolKind;
    using R = Rarity;
    static std::vector<Tool> const v{
        {"gloamingforest:ironwood_sword", "ironwood", K::Sword, 3.f, -2.4f, R::Common, false, false, 0},
        {"gloamingforest:ironwood_shovel", "ironwood", K::Shovel, 1.5f, -3.0f, R::Common, false, false, 0},
        {"gloamingforest:ironwood_pickaxe", "ironwood", K::Pickaxe, 1.f, -2.8f, R::Common, false, false, 0},
        {"gloamingforest:ironwood_axe", "ironwood", K::Axe, 6.f, -3.1f, R::Common, false, false, 0},
        {"gloamingforest:ironwood_hoe", "ironwood", K::Hoe, -2.f, -1.0f, R::Common, false, false, 0},
        {"gloamingforest:steeleaf_sword", "steeleaf", K::Sword, 3.f, -2.4f, R::Common, false, false, 0},
        {"gloamingforest:steeleaf_shovel", "steeleaf", K::Shovel, 1.5f, -3.0f, R::Common, false, false, 0},
        {"gloamingforest:steeleaf_pickaxe", "steeleaf", K::Pickaxe, 1.f, -2.8f, R::Common, false, false, 0},
        {"gloamingforest:steeleaf_axe", "steeleaf", K::Axe, 6.f, -3.0f, R::Common, false, false, 0},
        {"gloamingforest:steeleaf_hoe", "steeleaf", K::Hoe, -3.f, -0.5f, R::Common, false, false, 0},
        {"gloamingforest:fiery_sword", "fiery", K::Sword, 3.f, -2.4f, R::Uncommon, true, false, 0},
        {"gloamingforest:fiery_pickaxe", "fiery", K::Pickaxe, 1.f, -2.8f, R::Uncommon, true, false, 0},
        {"gloamingforest:knightmetal_sword", "knightmetal", K::Sword, 3.f, -2.4f, R::Common, false, false, 0},
        {"gloamingforest:knightmetal_pickaxe", "knightmetal", K::Pickaxe, 1.f, -2.8f, R::Common, false, false, 0},
        // KNIGHTMETAL.getSpeed() * 0.05f - 3.4f
        {"gloamingforest:knightmetal_axe", "knightmetal", K::Axe, 6.f, 8.0f * 0.05f - 3.4f, R::Common, false, false, 0},
        // MinotaurAxeItem#getEnchantmentValue: Tiers.GOLD's (22) for both
        {"gloamingforest:gold_minotaur_axe", "gold", K::Axe, 6.f, -3.2f, R::Common, false, false, 22},
        {"gloamingforest:diamond_minotaur_axe", "diamond", K::Axe, 6.f, -3.2f, R::Uncommon, false, false, 22},
        {"gloamingforest:mazebreaker_pickaxe", "diamond", K::Pickaxe, 1.f, -2.8f, R::Rare, false, true, 0},
    };
    return v;
}

ToolTier const* toolTier(std::string_view name) {
    for (auto const& t : toolTiers())
        if (t.name == name) return &t;
    return nullptr;
}

Tool const* tool(std::string_view itemId) {
    for (auto const& t : tools())
        if (t.id == itemId) return &t;
    return nullptr;
}

float attackDamage(Tool const& t) {
    auto const* tier = toolTier(t.tier);
    return 1.0f + (tier ? tier->attackDamageBonus : 0.f) + t.attackDamage;
}

float attackSpeed(Tool const& t) { return 4.0f + t.attackSpeed; }

int bedrockDamage(Tool const& t) { return std::max(0, static_cast<int>(std::floor(attackDamage(t) - 1.0f))); }

float fractionalDamage(Tool const& t) { return std::max(0.f, attackDamage(t) - 1.0f - static_cast<float>(bedrockDamage(t))); }

int enchantability(Tool const& t) {
    if (t.enchantability > 0) return t.enchantability;
    auto const* tier = toolTier(t.tier);
    return tier ? tier->enchantability : 0;
}

std::vector<MaterialItem> const& materialItems() {
    using R = Rarity;
    static std::vector<MaterialItem> const v{
        {"gloamingforest:naga_scale", R::Uncommon, false},       {"gloamingforest:raw_ironwood", R::Common, false},
        {"gloamingforest:ironwood_ingot", R::Common, false},     {"gloamingforest:steeleaf_ingot", R::Common, false},
        {"gloamingforest:fiery_blood", R::Uncommon, false},      {"gloamingforest:fiery_tears", R::Uncommon, false},
        {"gloamingforest:fiery_ingot", R::Uncommon, true},       {"gloamingforest:armor_shard", R::Common, false},
        {"gloamingforest:armor_shard_cluster", R::Common, false}, {"gloamingforest:knightmetal_ingot", R::Common, false},
        {"gloamingforest:knightmetal_ring", R::Common, false},   {"gloamingforest:arctic_fur", R::Common, false},
        {"gloamingforest:alpha_yeti_fur", R::Uncommon, false},
    };
    return v;
}

Bonus knightmetal(KnightmetalWeapon weapon, int armorValue, float cover, float newDamage, float originalDamage) {
    if (armorValue > 0 && weapon == KnightmetalWeapon::SwordOrPickaxe) {
        if (cover > 0) {
            int moreBonus = static_cast<int>(kKnightmetalBonusDamage * cover);
            return {newDamage + static_cast<float>(moreBonus), true};
        }
        return {newDamage + kKnightmetalBonusDamage, true};
    }
    if (armorValue == 0 && weapon == KnightmetalWeapon::Axe) return {originalDamage + kKnightmetalBonusDamage, true};
    return {newDamage, false};
}

Bonus minotaurAxe(bool sprinting, float newDamage) {
    if (!sprinting) return {newDamage, false};
    return {newDamage + kMinotaurAxeBonusDamage, true};
}

float armorCover(int wornPieces) {
    // LivingEntity#getArmorCoverPercentage: non-empty armour slots / all armour slots (4 for the
    // humanoid slots; Java counts the body slot only for mobs that have one)
    return static_cast<float>(std::clamp(wornPieces, 0, 4)) / 4.0f;
}

FireReact fireReact(int fieryPieces, int roll) {
    FireReact r;
    r.fireLevel = fieryPieces * 5;
    r.ignite    = r.fireLevel > 0 && roll < r.fireLevel;
    r.seconds   = r.fireLevel / 2;
    return r;
}

ChillAura chillAura(int yetiPieces) {
    ChillAura c;
    c.apply     = yetiPieces > 0;
    c.duration  = yetiPieces * 5 + 5;
    c.amplifier = yetiPieces;
    return c;
}

float damageAfterAbsorb(float damage, float armor, float toughness) {
    float f = 2.0f + toughness / 4.0f;
    float v = armor - damage / f;
    float lo = armor * 0.2f;
    float g  = v < lo ? lo : std::min(v, 20.0f); // Mth.clamp
    float h  = g / 25.0f;
    return damage * (1.0f - h);
}

int smeltingXp(float xp) { return static_cast<int>(std::floor(xp + 0.5f)); }

std::vector<DefaultEnchantments> const& defaultEnchantments() {
    static std::vector<DefaultEnchantments> const v{
        {"gloamingforest:equipment/ironwood_axe", "gloamingforest:ironwood_axe", {{"minecraft:fortune", 1}}},
        {"gloamingforest:equipment/ironwood_boots", "gloamingforest:ironwood_boots", {{"minecraft:feather_falling", 1}}},
        {"gloamingforest:equipment/ironwood_chestplate", "gloamingforest:ironwood_chestplate", {{"minecraft:protection", 1}}},
        {"gloamingforest:equipment/ironwood_helmet", "gloamingforest:ironwood_helmet", {{"minecraft:aqua_affinity", 1}}},
        {"gloamingforest:equipment/ironwood_hoe", "gloamingforest:ironwood_hoe", {{"minecraft:efficiency", 1}}},
        {"gloamingforest:equipment/ironwood_leggings", "gloamingforest:ironwood_leggings", {{"minecraft:protection", 1}}},
        {"gloamingforest:equipment/ironwood_pickaxe", "gloamingforest:ironwood_pickaxe", {{"minecraft:efficiency", 1}}},
        {"gloamingforest:equipment/ironwood_shovel", "gloamingforest:ironwood_shovel", {{"minecraft:unbreaking", 1}}},
        {"gloamingforest:equipment/ironwood_sword", "gloamingforest:ironwood_sword", {{"minecraft:knockback", 1}}},
        {"gloamingforest:equipment/naga_chestplate", "gloamingforest:naga_chestplate", {{"minecraft:fire_protection", 3}}},
        {"gloamingforest:equipment/naga_leggings", "gloamingforest:naga_leggings", {{"minecraft:protection", 3}}},
        {"gloamingforest:equipment/steeleaf_axe", "gloamingforest:steeleaf_axe", {{"minecraft:efficiency", 2}}},
        {"gloamingforest:equipment/steeleaf_boots", "gloamingforest:steeleaf_boots", {{"minecraft:feather_falling", 2}}},
        {"gloamingforest:equipment/steeleaf_chestplate", "gloamingforest:steeleaf_chestplate", {{"minecraft:blast_protection", 2}}},
        {"gloamingforest:equipment/steeleaf_helmet", "gloamingforest:steeleaf_helmet", {{"minecraft:projectile_protection", 2}}},
        {"gloamingforest:equipment/steeleaf_hoe", "gloamingforest:steeleaf_hoe", {{"minecraft:fortune", 2}}},
        {"gloamingforest:equipment/steeleaf_leggings", "gloamingforest:steeleaf_leggings", {{"minecraft:fire_protection", 2}}},
        {"gloamingforest:equipment/steeleaf_pickaxe", "gloamingforest:steeleaf_pickaxe", {{"minecraft:fortune", 2}}},
        {"gloamingforest:equipment/steeleaf_shovel", "gloamingforest:steeleaf_shovel", {{"minecraft:efficiency", 2}}},
        {"gloamingforest:equipment/steeleaf_sword", "gloamingforest:steeleaf_sword", {{"minecraft:looting", 2}}},
        {"gloamingforest:equipment/yeti_boots", "gloamingforest:yeti_boots", {{"minecraft:feather_falling", 4}, {"minecraft:protection", 2}}},
        {"gloamingforest:equipment/yeti_chestplate", "gloamingforest:yeti_chestplate", {{"minecraft:protection", 2}}},
        {"gloamingforest:equipment/yeti_helmet", "gloamingforest:yeti_helmet", {{"minecraft:protection", 2}}},
        {"gloamingforest:equipment/yeti_leggings", "gloamingforest:yeti_leggings", {{"minecraft:protection", 2}}},
    };
    return v;
}

std::vector<TrimMaterial> const& trimMaterials() {
    static std::vector<TrimMaterial> const v{
        {"gloamingforest:carminite", "gloamingforest:carminite", 0x9A0000, 0.4f, "trim_material.gloamingforest.carminite"},
        {"gloamingforest:fiery", "gloamingforest:fiery_ingot", 0xFFB53C, 0.3f, "trim_material.gloamingforest.fiery"},
        {"gloamingforest:ironwood", "gloamingforest:ironwood_ingot", 0x6B6161, 0.2f, "trim_material.gloamingforest.ironwood"},
        {"gloamingforest:knightmetal", "gloamingforest:knightmetal_ingot", 0x808C72, 0.1f, "trim_material.gloamingforest.knightmetal"},
        {"gloamingforest:naga_scale", "gloamingforest:naga_scale", 0x245712, 0.7f, "trim_material.gloamingforest.naga_scale"},
        {"gloamingforest:steeleaf", "gloamingforest:steeleaf_ingot", 0x497733, 0.7f, "trim_material.gloamingforest.steeleaf"},
    };
    return v;
}

char nearestChatColor(unsigned rgb) {
    static constexpr std::array<unsigned, 16> kColors{0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xFFAA00, 0xAAAAAA,
                                                      0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF};
    static constexpr char kCodes[] = "0123456789abcdef";
    long best = std::numeric_limits<long>::max();
    int  idx  = 15;
    for (int i = 0; i < 16; ++i) {
        long dr = static_cast<long>((rgb >> 16) & 0xFF) - static_cast<long>((kColors[i] >> 16) & 0xFF);
        long dg = static_cast<long>((rgb >> 8) & 0xFF) - static_cast<long>((kColors[i] >> 8) & 0xFF);
        long db = static_cast<long>(rgb & 0xFF) - static_cast<long>(kColors[i] & 0xFF);
        long d  = dr * dr + dg * dg + db * db;
        if (d < best) {
            best = d;
            idx  = i;
        }
    }
    return kCodes[idx];
}

} // namespace gf::core::armory

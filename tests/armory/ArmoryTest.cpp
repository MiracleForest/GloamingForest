// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Tests of src/core/armory/Armory.h (translated from TF 4.8's TFArmorMaterials, TFToolMaterials,
// TFItems, ToolEvents, EntityEvents, ApplyFrostedEffect, FieryToolSmeltingModifier and
// CraftingGenerator; LGPL-2.1-or-later code, see the header of Armory.h).
#include <cmath>
#include <set>
#include <gtest/gtest.h>

#include "core/armory/Armory.h"

using namespace gf::core::armory;

TEST(ArmoryItems, FiftyNineItems) {
    std::set<std::string_view> ids;
    for (auto const& p : armorPieces()) ids.insert(p.id);
    for (auto const& t : tools()) ids.insert(t.id);
    for (auto const& m : materialItems()) ids.insert(m.id);
    EXPECT_EQ(armorPieces().size(), 28u);
    EXPECT_EQ(tools().size(), 18u);
    EXPECT_EQ(materialItems().size(), 13u);
    EXPECT_EQ(ids.size(), 59u) << "no id twice";
}

TEST(ArmoryArmor, MaterialsAndDurability) {
    EXPECT_EQ(armorMaterials().size(), 8u);
    auto const* fiery = armorMaterial("fiery");
    ASSERT_NE(fiery, nullptr);
    EXPECT_EQ(fiery->defense[static_cast<int>(ArmorType::Chestplate)], 9);
    EXPECT_FLOAT_EQ(fiery->toughness, 1.5f);
    EXPECT_TRUE(fiery->fireResistant);
    // ArmorItem.Type#getDurability: 13 / 15 / 16 / 11 x multiplier
    EXPECT_EQ(armorDurability(ArmorType::Boots, 25), 325);
    EXPECT_EQ(armorDurability(ArmorType::Leggings, 25), 375);
    EXPECT_EQ(armorDurability(ArmorType::Chestplate, 30), 480);
    EXPECT_EQ(armorDurability(ArmorType::Helmet, 10), 110);
    auto const* naga = armorPiece("twilightforest:naga_chestplate");
    ASSERT_NE(naga, nullptr);
    EXPECT_EQ(defenseOf(*naga), 7);
    EXPECT_FLOAT_EQ(toughnessOf(*naga), 0.5f);
    auto const* yetiHelm = armorPiece("twilightforest:yeti_helmet");
    ASSERT_NE(yetiHelm, nullptr);
    EXPECT_EQ(defenseOf(*yetiHelm), 4);
    EXPECT_EQ(armorMaterial("phantom")->repairItem, "twilightforest:knightmetal_ingot");
    EXPECT_EQ(armorMaterial("knightmetal")->equipSound, "item.twilightforest.knightmetal_armor.equip");
    EXPECT_EQ(armorPiece("twilightforest:naga_helmet"), nullptr) << "naga scale has a chestplate and leggings only";
    EXPECT_EQ(armorPiece("twilightforest:phantom_boots"), nullptr) << "phantom has a helmet and a chestplate only";
}

TEST(ArmoryTools, AttackDamageAndSpeed) {
    auto dmg = [](char const* id) { return attackDamage(*tool(id)); };
    EXPECT_FLOAT_EQ(dmg("twilightforest:ironwood_sword"), 6.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:fiery_sword"), 8.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:steeleaf_sword"), 7.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:knightmetal_axe"), 10.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:ironwood_shovel"), 4.5f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:ironwood_hoe"), 1.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:steeleaf_hoe"), 1.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:gold_minotaur_axe"), 7.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:diamond_minotaur_axe"), 10.f);
    EXPECT_FLOAT_EQ(dmg("twilightforest:mazebreaker_pickaxe"), 5.f);
    EXPECT_NEAR(attackSpeed(*tool("twilightforest:knightmetal_axe")), 1.0f, 1e-5);
    EXPECT_NEAR(attackSpeed(*tool("twilightforest:steeleaf_hoe")), 3.5f, 1e-5);
    // Bedrock's integer damage plus the fraction U7 adds
    auto const& shovel = *tool("twilightforest:steeleaf_shovel");
    EXPECT_EQ(bedrockDamage(shovel), 4);
    EXPECT_FLOAT_EQ(fractionalDamage(shovel), 0.5f);
    EXPECT_EQ(bedrockDamage(*tool("twilightforest:ironwood_hoe")), 0);
    EXPECT_EQ(enchantability(*tool("twilightforest:diamond_minotaur_axe")), 22);
    EXPECT_EQ(enchantability(*tool("twilightforest:ironwood_axe")), 25);
    EXPECT_TRUE(tool("twilightforest:mazebreaker_pickaxe")->noRepair);
}

TEST(ArmoryKnightmetal, SwordVersusArmour) {
    // full cover: (int)(2 * 1.0) = +2; half cover: (int)(2 * 0.5) = +1; quarter: (int)0.5 = +0
    EXPECT_FLOAT_EQ(knightmetal(KnightmetalWeapon::SwordOrPickaxe, 10, 1.0f, 7.f, 7.f).damage, 9.f);
    EXPECT_FLOAT_EQ(knightmetal(KnightmetalWeapon::SwordOrPickaxe, 5, 0.5f, 7.f, 7.f).damage, 8.f);
    auto quarter = knightmetal(KnightmetalWeapon::SwordOrPickaxe, 2, 0.25f, 7.f, 7.f);
    EXPECT_FLOAT_EQ(quarter.damage, 7.f);
    EXPECT_TRUE(quarter.sparkles) << "the sparkles show even when the cast bonus is 0";
    // natural armour without worn pieces: +2
    EXPECT_FLOAT_EQ(knightmetal(KnightmetalWeapon::SwordOrPickaxe, 4, 0.f, 7.f, 7.f).damage, 9.f);
    // no armour: nothing for the sword
    auto none = knightmetal(KnightmetalWeapon::SwordOrPickaxe, 0, 0.f, 7.f, 7.f);
    EXPECT_FLOAT_EQ(none.damage, 7.f);
    EXPECT_FALSE(none.sparkles);
}

TEST(ArmoryKnightmetal, AxeVersusUnarmoured) {
    // the axe sets original + 2 (not new + 2)
    EXPECT_FLOAT_EQ(knightmetal(KnightmetalWeapon::Axe, 0, 0.f, 12.f, 10.f).damage, 12.f);
    EXPECT_FLOAT_EQ(knightmetal(KnightmetalWeapon::Axe, 0, 0.f, 10.f, 10.f).damage, 12.f);
    auto armoured = knightmetal(KnightmetalWeapon::Axe, 3, 0.25f, 10.f, 10.f);
    EXPECT_FLOAT_EQ(armoured.damage, 10.f);
    EXPECT_FALSE(armoured.sparkles);
    EXPECT_FLOAT_EQ(armorCover(3), 0.75f);
}

TEST(ArmoryMinotaur, SprintBonus) {
    EXPECT_FLOAT_EQ(minotaurAxe(true, 10.f).damage, 17.f);
    EXPECT_TRUE(minotaurAxe(true, 10.f).sparkles);
    EXPECT_FLOAT_EQ(minotaurAxe(false, 10.f).damage, 10.f);
}

TEST(ArmorySetReactions, FieryProbabilityAndDuration) {
    // f pieces: fireLevel 5f, ignite when nextInt(25) < 5f, for 5f/2 seconds
    int ignited = 0;
    for (int roll = 0; roll < 25; ++roll) ignited += fireReact(2, roll).ignite ? 1 : 0;
    EXPECT_EQ(ignited, 10) << "2 pieces: 10 / 25";
    EXPECT_EQ(fireReact(2, 0).seconds, 5);
    EXPECT_EQ(fireReact(3, 0).seconds, 7);
    EXPECT_EQ(fireReact(4, 0).seconds, 10);
    ignited = 0;
    for (int roll = 0; roll < 25; ++roll) ignited += fireReact(4, roll).ignite ? 1 : 0;
    EXPECT_EQ(ignited, 20);
    EXPECT_FALSE(fireReact(0, 0).ignite);
}

TEST(ArmorySetReactions, YetiChill) {
    EXPECT_FALSE(chillAura(0).apply);
    auto c = chillAura(4);
    EXPECT_TRUE(c.apply);
    EXPECT_EQ(c.duration, 25);
    EXPECT_EQ(c.amplifier, 4);
    EXPECT_EQ(chillAura(1).duration, 10);
}

TEST(ArmoryCombatRules, DamageAfterAbsorb) {
    // no toughness, armour 20, damage 10: clamp(20 - 10 / 2, 4, 20) = 15 -> 10 * (1 - 0.6) = 4
    EXPECT_NEAR(damageAfterAbsorb(10.f, 20.f, 0.f), 4.f, 1e-5);
    // toughness 12 (full yeti): f = 5 -> clamp(20 - 2, 4, 20) = 18 -> 10 * 0.28 = 2.8
    EXPECT_NEAR(damageAfterAbsorb(10.f, 20.f, 12.f), 2.8f, 1e-5);
    // the floor: armour * 0.2
    EXPECT_NEAR(damageAfterAbsorb(100.f, 10.f, 0.f), 100.f * (1.f - 2.f / 25.f), 1e-3);
    // the ceiling of 20
    EXPECT_NEAR(damageAfterAbsorb(1.f, 30.f, 0.f), 1.f * (1.f - 20.f / 25.f), 1e-5);
}

TEST(ArmorySmelting, XpRounding) {
    EXPECT_EQ(smeltingXp(0.7f), 1);
    EXPECT_EQ(smeltingXp(0.49f), 0);
    EXPECT_EQ(smeltingXp(0.5f), 1);
    EXPECT_EQ(smeltingXp(2.1f), 2);
}

TEST(ArmoryCrafting, DefaultEnchantments) {
    auto const& v = defaultEnchantments();
    EXPECT_EQ(v.size(), 24u);
    std::set<std::string_view> recipes;
    for (auto const& d : v) {
        recipes.insert(d.recipe);
        EXPECT_FALSE(d.enchantments.empty());
    }
    EXPECT_EQ(recipes.size(), 24u);
    for (auto const& d : v)
        if (d.item == "twilightforest:yeti_boots") {
            ASSERT_EQ(d.enchantments.size(), 2u);
            EXPECT_EQ(d.enchantments[0].first, "minecraft:feather_falling");
            EXPECT_EQ(d.enchantments[0].second, 4);
        }
}

TEST(ArmoryTrims, MaterialsAndColours) {
    EXPECT_EQ(trimMaterials().size(), 6u);
    EXPECT_EQ(nearestChatColor(0x9A0000), '4');
    EXPECT_EQ(nearestChatColor(0xFFB53C), '6');
    EXPECT_EQ(nearestChatColor(0x10A010), '2');
    EXPECT_EQ(nearestChatColor(0x245712), '8') << "naga scale's dark green is nearest dark gray";
    EXPECT_EQ(nearestChatColor(0xFFFFFF), 'f');
    EXPECT_EQ(nearestChatColor(0x000000), '0');
}

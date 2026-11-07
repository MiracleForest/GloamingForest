// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
#pragma once

// U17 (hostiles): the spawn sweep's table of every TF entity type (80), from F13's Java goldens
// tests/golden/mobs.json (attributes of an instance made with EntityType#create, default config)
// and tests/golden/loot.json (which entities have a loot table), plus three facts the goldens do not
// carry: the BaseTFBoss subclasses, the mobs whose hurt and death sounds are null (MosquitoSwarm)
// and the monsters that stay on Peaceful (LoyalZombie, a TamableAnimal). Pure data (no game
// includes); tests/hostiles/MobSweepTest.cpp keeps it equal to the goldens, the selftest
// hostiles.spawn_sweep (src/feature/hostiles/SpawnSweep.cpp) checks the live actors against it.

#include <iterator>

namespace gf::core::hostiles {

struct MobSweepRow {
    char const* id;
    char const* owner;      ///< the owning unit (docs/spec/owners.tsv)
    bool        living;     ///< a LivingEntity (the rest are projectiles, falling blocks, markers)
    bool        boss;       ///< a BaseTFBoss
    float       maxHealth;  ///< living only (0 otherwise), as every attribute below
    float       movementSpeed;
    float       attackDamage; ///< 0: no attack damage attribute (or 0)
    float       followRange;
    float       knockbackResistance;
    float       width;
    float       height;
    bool        fireImmune;
    bool        canSummon;
    bool        peacefulDespawn; ///< Mob#shouldDespawnInPeaceful
    bool        hurtDeathSounds; ///< getHurtSound / getDeathSound are not null
    bool        lootTable;       ///< twilightforest:entities/<path> exists
    float       armor;           ///< Attributes.ARMOR value
    int         experienceMin;   ///< the experience reward (min / max of 256 draws)
    int         experienceMax;
};

// clang-format off
inline constexpr MobSweepRow kMobSweep[] = {
    {"twilightforest:adherent", "U30", true, false, 20.0f, 0.25f, 2.0f, 16.0f, 0.0f, 0.800000011920929f, 2.200000047683716f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:alpha_yeti", "U27", true, true, 200.0f, 0.38f, 1.0f, 40.0f, 0.5f, 3.799999952316284f, 5.0f, false, true, true, true, true, 0.0f, 317, 317},
    {"twilightforest:armored_giant", "U29", true, false, 80.0f, 0.23f, 2.0f, 40.0f, 0.0f, 2.4000000953674316f, 7.199999809265137f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:bighorn_sheep", "U16", true, false, 8.0f, 0.23000000417232513f, 0.0f, 16.0f, 0.0f, 0.8999999761581421f, 1.2999999523162842f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:blockchain_goblin", "U25", true, false, 20.0f, 0.28f, 8.0f, 16.0f, 0.0f, 0.8999999761581421f, 1.399999976158142f, false, true, true, true, true, 11.0f, 5, 5},
    {"twilightforest:boar", "U16", true, false, 10.0f, 0.25f, 0.0f, 16.0f, 0.0f, 0.8999999761581421f, 0.8999999761581421f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:carminite_broodling", "U26", true, false, 7.0f, 0.30000001192092896f, 4.0f, 16.0f, 0.0f, 0.699999988079071f, 0.5f, false, true, true, true, true, 0.0f, 3, 3},
    {"twilightforest:carminite_ghastguard", "U26", true, false, 30.0f, 0.7f, 0.0f, 64.0f, 0.0f, 4.0f, 6.0f, true, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:carminite_ghastling", "U26", true, false, 10.0f, 0.7f, 0.0f, 16.0f, 0.0f, 1.100000023841858f, 1.5f, true, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:carminite_golem", "U26", true, false, 40.0f, 0.25f, 9.0f, 16.0f, 0.0f, 1.399999976158142f, 2.9000000953674316f, false, true, true, true, true, 2.0f, 5, 5},
    {"twilightforest:chain_block", "U8", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6000000238418579f, 0.6000000238418579f, true, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:charm_effect", "U9", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false, false, false, false, 0.0f, 0, 0},
    {"twilightforest:cube_of_annihilation", "U8", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, true, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:death_tome", "U21", true, false, 20.0f, 0.25f, 4.0f, 16.0f, 0.0f, 0.75f, 1.5f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:deer", "U16", true, false, 10.0f, 0.2f, 0.0f, 16.0f, 0.0f, 0.699999988079071f, 1.7999999523162842f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:dwarf_rabbit", "U16", true, false, 3.0f, 0.3f, 0.0f, 16.0f, 0.0f, 0.4000000059604645f, 0.4000000059604645f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:falling_ice", "U27", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, true, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:fire_beetle", "U17", true, false, 25.0f, 0.23f, 4.0f, 16.0f, 0.0f, 1.100000023841858f, 0.5f, true, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:giant_miner", "U29", true, false, 80.0f, 0.23f, 2.0f, 40.0f, 0.0f, 2.4000000953674316f, 7.199999809265137f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:harbinger_cube", "U30", true, false, 40.0f, 0.23f, 2.0f, 16.0f, 0.0f, 1.899999976158142f, 2.4000000953674316f, true, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:hedge_spider", "U17", true, false, 16.0f, 0.30000001192092896f, 2.0f, 16.0f, 0.0f, 1.399999976158142f, 0.8999999761581421f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:helmet_crab", "U25", true, false, 13.0f, 0.28f, 3.0f, 16.0f, 0.0f, 0.800000011920929f, 1.100000023841858f, false, true, true, true, true, 6.0f, 5, 5},
    {"twilightforest:hostile_wolf", "U17", true, false, 20.0f, 0.30000001192092896f, 2.0f, 16.0f, 0.0f, 0.6000000238418579f, 0.8500000238418579f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:hydra", "U24", true, true, 360.0f, 0.28f, 0.0f, 16.0f, 0.0f, 16.0f, 12.0f, true, true, true, true, true, 0.0f, 511, 511},
    {"twilightforest:hydra_mortar", "U24", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.75f, 0.75f, true, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:ice_arrow", "U8", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:ice_crystal", "U28", true, false, 10.0f, 0.23f, 5.0f, 16.0f, 0.0f, 0.6000000238418579f, 1.7999999523162842f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:ice_snowball", "U28", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:king_spider", "U17", true, false, 30.0f, 0.35f, 6.0f, 16.0f, 0.0f, 1.600000023841858f, 1.600000023841858f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:knight_phantom", "U25", true, true, 35.0f, 0.7f, 1.0f, 16.0f, 0.0f, 1.25f, 2.5f, false, true, true, true, true, 0.0f, 93, 93},
    {"twilightforest:kobold", "U16", true, false, 13.0f, 0.28f, 4.0f, 16.0f, 0.0f, 0.800000011920929f, 1.100000023841858f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:lich", "U21", true, true, 100.0f, 0.45f, 3.0f, 35.0f, 0.0f, 0.800000011920929f, 2.5f, false, true, true, true, true, 0.0f, 217, 217},
    {"twilightforest:lich_bolt", "U21", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:lich_bomb", "U21", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:lich_minion", "U21", true, false, 20.0f, 0.23000000417232513f, 3.0f, 35.0f, 0.0f, 0.6000000238418579f, 1.9500000476837158f, false, true, true, true, true, 2.0f, 5, 5},
    {"twilightforest:lower_goblin_knight", "U25", true, false, 20.0f, 0.28f, 4.0f, 16.0f, 0.0f, 0.699999988079071f, 1.100000023841858f, false, true, true, true, true, 17.0f, 5, 5},
    {"twilightforest:loyal_zombie", "U9", true, false, 40.0f, 0.3f, 0.0f, 16.0f, 0.0f, 0.6000000238418579f, 1.9500000476837158f, false, true, false, true, true, 3.0f, 1, 3},
    {"twilightforest:magic_painting", "U22", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:maze_slime", "U23", true, false, 20.0f, 0.7f, 2.0f, 16.0f, 0.0f, 0.5199999809265137f, 0.5199999809265137f, false, true, true, true, true, 0.0f, 0, 0},
    {"twilightforest:minoshroom", "U23", true, true, 120.0f, 0.25f, 2.0f, 16.0f, 0.5f, 1.4900000095367432f, 2.5f, false, true, true, true, true, 0.0f, 100, 100},
    {"twilightforest:minotaur", "U23", true, false, 30.0f, 0.25f, 2.0f, 16.0f, 0.0f, 0.6000000238418579f, 2.0999999046325684f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:mist_wolf", "U17", true, false, 30.0f, 0.30000001192092896f, 6.0f, 16.0f, 0.0f, 1.399999976158142f, 1.899999976158142f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:moonworm_shot", "U8", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:mosquito_swarm", "U16", true, false, 12.0f, 0.23f, 3.0f, 16.0f, 0.0f, 0.699999988079071f, 1.899999976158142f, false, true, true, false, true, 0.0f, 5, 5},
    {"twilightforest:naga", "U20", true, true, 120.0f, 0.5f, 5.0f, 80.0f, 0.25f, 2.0f, 3.0f, true, true, true, true, true, 0.0f, 217, 217},
    {"twilightforest:nature_bolt", "U17", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:penguin", "U16", true, false, 10.0f, 0.2f, 0.0f, 16.0f, 0.0f, 0.5f, 0.8999999761581421f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:pinch_beetle", "U17", true, false, 40.0f, 0.23f, 4.0f, 16.0f, 0.0f, 1.2000000476837158f, 0.5f, false, true, true, true, true, 2.0f, 5, 5},
    {"twilightforest:plateau_boss", "U30", true, true, 20.0f, 0.7f, 2.0f, 16.0f, 0.0f, 1.0f, 1.0f, true, true, true, true, true, 0.0f, 647, 647},
    {"twilightforest:protection_box", "F6", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false, false, false, false, 0.0f, 0, 0},
    {"twilightforest:quest_ram", "U19", true, false, 70.0f, 0.23f, 0.0f, 16.0f, 0.0f, 1.25f, 2.9000000953674316f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:raven", "U16", true, false, 10.0f, 0.2f, 0.0f, 16.0f, 0.0f, 0.30000001192092896f, 0.5f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:redcap", "U17", true, false, 20.0f, 0.28f, 2.0f, 16.0f, 0.0f, 0.8999999761581421f, 1.399999976158142f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:redcap_sapper", "U17", true, false, 30.0f, 0.28f, 2.0f, 16.0f, 0.0f, 0.8999999761581421f, 1.399999976158142f, false, true, true, true, true, 2.0f, 5, 5},
    {"twilightforest:rising_zombie", "U21", true, false, 20.0f, 0.23000000417232513f, 3.0f, 35.0f, 0.0f, 0.6000000238418579f, 1.9500000476837158f, false, true, true, true, true, 2.0f, 5, 5},
    {"twilightforest:roving_cube", "U30", true, false, 10.0f, 0.23f, 5.0f, 16.0f, 0.0f, 1.2000000476837158f, 2.0999999046325684f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:seeker_arrow", "U8", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:skeleton_druid", "U17", true, false, 20.0f, 0.25f, 2.0f, 16.0f, 0.0f, 0.6000000238418579f, 1.9900000095367432f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:slider", "U26", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.9800000190734863f, 0.9800000190734863f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:slime_beetle", "U17", true, false, 25.0f, 0.23f, 4.0f, 16.0f, 0.0f, 0.8999999761581421f, 0.5f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:slime_blob", "U17", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:snow_guardian", "U28", true, false, 10.0f, 0.23f, 3.0f, 16.0f, 0.0f, 0.6000000238418579f, 1.7999999523162842f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:snow_queen", "U28", true, true, 200.0f, 0.23f, 7.0f, 40.0f, 0.75f, 0.699999988079071f, 2.5f, false, true, true, true, true, 0.0f, 317, 317},
    {"twilightforest:squirrel", "U16", true, false, 6.0f, 0.3f, 0.0f, 16.0f, 0.0f, 0.30000001192092896f, 0.5f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:stable_ice_core", "U28", true, false, 20.0f, 0.23f, 3.0f, 16.0f, 0.0f, 0.800000011920929f, 1.7999999523162842f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:swarm_spider", "U17", true, false, 3.0f, 0.30000001192092896f, 1.0f, 16.0f, 0.0f, 0.800000011920929f, 0.4000000059604645f, false, true, true, true, true, 0.0f, 2, 2},
    {"twilightforest:thrown_block", "U27", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, true, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:thrown_ice", "U8", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, true, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:thrown_wep", "U25", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:tiny_bird", "U16", true, false, 4.0f, 0.2f, 0.0f, 16.0f, 0.0f, 0.30000001192092896f, 0.30000001192092896f, false, true, false, true, true, 0.0f, 1, 3},
    {"twilightforest:tome_bolt", "U21", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:towerwood_borer", "U26", true, false, 15.0f, 0.27f, 5.0f, 8.0f, 0.0f, 0.4000000059604645f, 0.30000001192092896f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:troll", "U27", true, false, 30.0f, 0.25f, 7.0f, 16.0f, 0.0f, 1.399999976158142f, 2.4000000953674316f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:unstable_ice_core", "U28", true, false, 20.0f, 0.23f, 3.0f, 16.0f, 0.0f, 0.800000011920929f, 1.7999999523162842f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:upper_goblin_knight", "U25", true, false, 30.0f, 0.28f, 8.0f, 16.0f, 0.0f, 1.100000023841858f, 1.2999999523162842f, false, true, true, true, true, 20.0f, 5, 5},
    {"twilightforest:ur_ghast", "U26", true, true, 250.0f, 0.7f, 0.0f, 128.0f, 1.0f, 14.0f, 18.0f, true, true, true, true, true, 0.0f, 317, 317},
    {"twilightforest:wand_bolt", "U9", false, false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.25f, false, true, false, false, false, 0.0f, 0, 0},
    {"twilightforest:winter_wolf", "U17", true, false, 30.0f, 0.30000001192092896f, 6.0f, 16.0f, 0.0f, 1.399999976158142f, 1.899999976158142f, false, true, true, true, true, 0.0f, 5, 5},
    {"twilightforest:wraith", "U17", true, false, 20.0f, 0.5f, 5.0f, 16.0f, 0.0f, 0.6000000238418579f, 2.0999999046325684f, true, true, true, true, true, 0.0f, 0, 0},
    {"twilightforest:yeti", "U27", true, false, 20.0f, 0.38f, 0.0f, 4.0f, 0.0f, 1.399999976158142f, 2.4000000953674316f, false, true, true, true, true, 0.0f, 5, 5},
};
// clang-format on

static_assert(std::size(kMobSweep) == 80, "TF 4.8 registers 80 entity types");

} // namespace gf::core::hostiles

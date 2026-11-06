// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFItems.java
//   src/main/java/twilightforest/block/Experiment115Block.java
//   src/main/java/twilightforest/potions/FrostedEffect.java
//   src/main/java/twilightforest/events/EntityEvents.java
//   src/main/java/twilightforest/init/TFEnchantments.java
//   src/main/java/twilightforest/init/TFJukeboxSongs.java
//   src/main/java/twilightforest/item/ChainBlockItem.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions), pure rules: TF's food properties, Experiment 115's state machine and redstone,
// the frosted effect's numbers, the enchantments' level-based values and the jukebox songs.
#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace gf::core::provisions {

// ---- food -------------------------------------------------------------------------------------------

/// A vanilla effect a food gives (MobEffectInstance, probability 1.0 in every TF food).
struct FoodEffect {
    std::string_view effect; ///< "minecraft:regeneration"
    int              ticks     = 0;
    int              amplifier = 0;
};

/// TF's FoodProperties of an item.
struct Food {
    std::string_view          id;
    int                       nutrition  = 0;
    float                     saturation = 0; ///< the saturation modifier
    bool                      alwaysEdible = false;
    bool                      bowl         = false; ///< usingConvertsTo(Items.BOWL)
    std::array<FoodEffect, 2> effects{};
};

/// The 23 foods of U10 (the 22 food items and Experiment 115's item).
[[nodiscard]] std::array<Food, 23> const& foods();
[[nodiscard]] Food const* food(std::string_view itemId);
/// FoodData#eat: the saturation a food gives (nutrition * modifier * 2).
[[nodiscard]] constexpr float saturationOf(int nutrition, float modifier) { return static_cast<float>(nutrition) * modifier * 2.0f; }

// ---- Experiment 115 ---------------------------------------------------------------------------------

inline constexpr int kE115FullSlices = 8; ///< bites taken 0 = 8 slices, 7 = one slice

/// The block's state (omnomnom = bites taken 0..7, regenerate).
struct E115 {
    int  bites      = 7;
    bool regenerate = false;
    bool operator==(E115 const&) const = default;
};

/// What a use of the block does (Experiment115Block#useItemOn / useWithoutItem).
enum class E115Action {
    Pass,        ///< nothing (the item use goes on)
    Fail,        ///< adding a slice to a full block
    AddSlice,    ///< an E115 item put one slice back (bites - 1), the item consumed
    Regenerate,  ///< redstone on a full non-regenerating block: regenerate set, the redstone consumed
    TakeSlice,   ///< sneaking, empty hands, not regenerating: a slice back into the inventory
    Eat,         ///< eat one slice (4 / 0.3)
};

struct E115Use {
    bool holdsE115      = false; ///< the hand holds twilightforest:experiment_115
    bool holdsRedstone  = false; ///< the hand holds minecraft:redstone
    bool emptyHands     = true;  ///< both hands empty
    bool sneaking       = false; ///< isSecondaryUseActive
    bool canEat         = true;  ///< Player#canEat(false): food level below 20 (or creative-like invulnerable)
};

/// Java's order: (not sneaking) E115 item -> add / fail, redstone on a full non-regenerating block;
/// (sneaking with empty hands, not regenerating) take a slice back; otherwise a use without item
/// (empty hand, any other item, sneaking on a regenerating block) eats when the player can. A
/// sneaking player holding an item never reaches the block (Java skips useItemOn then).
[[nodiscard]] E115Action e115Action(E115 const& state, E115Use const& use);
/// The state after an action (nullopt: the block is removed: the last slice taken or eaten).
[[nodiscard]] std::optional<E115> e115After(E115 const& state, E115Action action);
/// The consume-item trigger's stack count of an eaten slice (8 - bites before eating).
[[nodiscard]] constexpr int e115EatenCount(E115 const& before) { return kE115FullSlices - before.bites; }
/// Random tick: a regenerating block regrows a slice (nullopt: unchanged).
[[nodiscard]] std::optional<E115> e115RandomTick(E115 const& state);
/// getAnalogOutputSignal: (8 - bites) + 7 when regenerating.
[[nodiscard]] constexpr int e115Comparator(E115 const& s) { return (kE115FullSlices - s.bites) + (s.regenerate ? 7 : 0); }
/// getSignal: 15 - 2 * bites while regenerating, else 0.
[[nodiscard]] constexpr int e115Signal(E115 const& s) { return s.regenerate ? 15 - s.bites * 2 : 0; }

// ---- frosted ----------------------------------------------------------------------------------------

inline constexpr unsigned kFrostedColor       = 0x56CBFD;
inline constexpr double   kFrostMultiplier    = -0.15; ///< FrostedEffect.FROST_MULTIPLIER per level (ADD_MULTIPLIED_TOTAL)
inline constexpr int      kTicksRequiredToFreeze = 140; ///< Entity#getTicksRequiredToFreeze
inline constexpr int      kFreezeDamageInterval  = 40;  ///< LivingEntity#aiStep: freeze damage every 40 ticks when fully frozen

/// The movement speed factor of frosted at `amplifier` (1 - 0.15 (amplifier + 1)).
[[nodiscard]] constexpr double frostedSpeedFactor(int amplifier) { return 1.0 + kFrostMultiplier * (amplifier + 1); }
/// One tick of the frozen counter of a frosted entity (FrostedEffect#applyEffectTick with the
/// powder-snow rule of LivingEntity#aiStep): +1 (in powder snow) +amplifier, capped at 140.
[[nodiscard]] int frostedFrozenTick(int frozenTicks, int amplifier, bool canFreeze);
/// Frozen ticks of an entity no longer frosted (aiStep: - 2 per tick, down to 0).
[[nodiscard]] constexpr int thawTick(int frozenTicks) { return frozenTicks > 2 ? frozenTicks - 2 : 0; }
/// The freeze damage of a fully frozen entity (1, 5 for #minecraft:freeze_hurts_extra_types).
[[nodiscard]] constexpr float freezeDamage(bool hurtsExtra) { return hurtsExtra ? 5.0f : 1.0f; }
/// EntityEvents#reduceFrostedEffectIfOnFire: freeze damage while frosted (integer division).
[[nodiscard]] constexpr float frostedFreezeDamage(float original, int amplifier) { return original + static_cast<float>(amplifier / 2); }
/// The amplifier after fire damage (-1: the effect is removed).
[[nodiscard]] constexpr int frostedAfterFire(int amplifier) { return amplifier - 1; }

// ---- enchantments -----------------------------------------------------------------------------------

/// LevelBasedValue.Linear: base + perLevelAboveFirst * (level - 1).
[[nodiscard]] constexpr float linear(float base, float perLevelAboveFirst, int level) { return base + perLevelAboveFirst * static_cast<float>(level - 1); }
/// fire_react / chill_aura: random_chance of enchantment_level linear(0.15, 0.15).
[[nodiscard]] constexpr float reactChance(int level) { return linear(0.15f, 0.15f, level); }
/// fire_react: ignite linear(2, 3) seconds.
[[nodiscard]] constexpr float fireReactSeconds(int level) { return linear(2.0f, 3.0f, level); }
/// chill_aura: apply_frosted duration 200, amplifier linear(0, 1) (max(0, round)).
inline constexpr int kChillAuraTicks = 200;
[[nodiscard]] int chillAuraAmplifier(int level);
/// Both reactions damage the enchanted armour piece by 2.
inline constexpr int kReactItemDamage = 2;
/// destruction: damage add linear(-1.5, -1.5).
[[nodiscard]] constexpr float destructionDamage(int level) { return linear(-1.5f, -1.5f, level); }
/// destruction: smash_blocks max_smash 12, radius 1.
inline constexpr int kSmashMax    = 12;
inline constexpr int kSmashRadius = 1;
/// ChainBlockItem#getHarvestLevel: destruction 1 wood, 2 stone, else iron (the vanilla
/// #minecraft:incorrect_for_<tier>_tool tag of that tier).
[[nodiscard]] std::string_view destructionIncorrectTag(int level);

// ---- music ------------------------------------------------------------------------------------------

/// A jukebox song (data/twilightforest/jukebox_song): key, disc track, length, comparator output.
struct Song {
    std::string_view key;   ///< "warfarer" (TF's typo) for the wayfarer disc
    std::string_view track; ///< "wayfarer": the disc item music_disc_<track>, sound music_disc.twilightforest.<track>
    int              seconds = 0;
    int              comparator = 15;
};
[[nodiscard]] std::array<Song, 9> const& songs();
/// TF's background music of every biome (TFConfiguredFeatures.TFMUSICTYPE): event, delays (ticks).
inline constexpr std::string_view kDimensionMusic = "music.twilightforest.twilight_forest";
inline constexpr int              kMusicMinDelay  = 1200;
inline constexpr int              kMusicMaxDelay  = 12000;

} // namespace gf::core::provisions

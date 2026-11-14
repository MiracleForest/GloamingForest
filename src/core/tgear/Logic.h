// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/components/item/ItemDisplayContents.java
//   src/main/java/gloamingforest/item/travellers_gear/TravellersArmorBeltItem.java
//   src/main/java/gloamingforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/gloamingforest/item/travellers_gear/modifiers/TooltipStringInterpolator.java
//   src/main/java/gloamingforest/client/overlay/display/ClockDisplay.java
//   src/main/java/gloamingforest/client/overlay/display/MoonDialDisplay.java
//   src/main/java/gloamingforest/util/GloamingMathUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The game-independent rules of U11 (Travellers' Gear): the goggles' item display storage, the belt's
// hotbar swap, the auto repair chance, the clock / moon dial HUD texts and the ${gloamingkeybinds/...}
// interpolation of the modifier names. The game adapters (src/feature/tgear, src/client/tgear) map
// item stacks onto these plain values.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gf::core::tgear {

// ---- item display (ItemDisplayContents) -----------------------------------------------------------

/// ItemDisplays: the kinds of stored items, in display order (MAP draws on TOP, the others DEFAULT).
enum class DisplayType { None, Map, Compass, Clock, MoonDial };

/// ItemDisplayContents.LAYOUT: MAP, MAP, MAP, COMPASS, CLOCK, MOON_DIAL.
inline constexpr std::array<DisplayType, 6> kDisplayLayout{
    DisplayType::Map, DisplayType::Map, DisplayType::Map, DisplayType::Compass, DisplayType::Clock, DisplayType::MoonDial};
inline constexpr int kDisplaySlots = static_cast<int>(kDisplayLayout.size());
/// EMPTY's chosen map slot: the first MAP slot of the layout.
inline constexpr int kFirstMapSlot = 0;

/// A stored stack as the rules see it: `identity` is "" for an empty slot, else a string equal for
/// two stacks exactly when Java's isSameItemSameComponents holds (item id + data); `type` is the
/// display kind the item is valid for (None: none).
struct DisplayItem {
    std::string        identity;
    DisplayType        type = DisplayType::None;
    [[nodiscard]] bool empty() const { return identity.empty(); }
};

/// ItemDisplayContents.Mutable: always kDisplaySlots items.
struct DisplayContents {
    std::array<DisplayItem, kDisplaySlots> items{};
    int                                    chosenMapSlot = kFirstMapSlot;
};

/// The outcome of trySwap: the slot the source's first item went into and what it replaced (empty:
/// nothing; then the source keeps the rest of its stack, else the source slot gets `replaced` and
/// the rest of the source stack goes to the player).
struct SwapResult {
    bool        ok   = false;
    int         slot = -1;
    DisplayItem replaced;
};

/// Mutable#trySwap for a non-empty source stack that may go inside containers (callers check
/// canFitInsideContainerItems): the first empty slot of its type, else the first slot of its type;
/// refused when the item fits no slot or the target holds the same item.
[[nodiscard]] SwapResult trySwap(DisplayContents& contents, DisplayItem const& source);
/// Mutable#removeFirstFree: the index of the first stored item `mayPlace` accepts (cleared, the
/// chosen map slot moved on when it was the chosen one), -1 for none.
[[nodiscard]] int removeFirstFree(DisplayContents& contents, std::function<bool(DisplayItem const&)> const& mayPlace = {});
/// Mutable#cycleChosenMapSlot: the next filled MAP slot after the chosen one, else -1.
int cycleChosenMapSlot(DisplayContents& contents);
/// The slot of the map the HUD shows and the goggles tick (-1: none), as findActiveMapSlot.
[[nodiscard]] inline int activeMapSlot(DisplayContents const& contents) { return contents.chosenMapSlot; }

// ---- belt (TravellersArmorBeltItem#travellersTrySwapHotbar) ----------------------------------------

/// One hotbar slot for the swap: whether it holds an item, and whether that item may be exchanged
/// (canFitInsideContainerItems and not #gloamingforest:travellers_belt_blacklisted).
struct HotbarSlot {
    bool empty   = true;
    bool allowed = true;
};
struct BeltSwapPlan {
    std::array<bool, 9> exchange{}; ///< exchange the hotbar slot with the stored slot
    bool                changed = false; ///< play the swap sound
};
/// Which slots exchange: allowed ones, and while the swap is inactive (a broken piece) only into empty
/// hotbar slots. `beltEmpty[i]`: the stored slot is empty. Java's `!beltStack.equals(inventoryStack)`
/// is reference equality, so a slot changes unless both stacks are the shared EMPTY.
[[nodiscard]] BeltSwapPlan planHotbarSwap(std::array<HotbarSlot, 9> const& hotbar, std::array<bool, 9> const& beltEmpty, bool active);

// ---- auto repair (TravellersGearLogic#getAutoRepairChance) -----------------------------------------

inline constexpr double kAutoRepairSunlightBoost = 3;
inline constexpr double kAutoRepairGloamingBoost = kAutoRepairSunlightBoost / 2;
/// Ticks after the last armour damage during which nothing repairs (10 s; `<=` returns).
inline constexpr std::int64_t kAutoRepairQuietTicks = 10 * 20;
/// GloamingMathUtil#probabilityOfAtLeastOneSuccess.
[[nodiscard]] double probabilityOfAtLeastOneSuccess(double successProbability, double tries);
[[nodiscard]] double autoRepairChance(double baseProbability, bool canSeeSky, bool gloamingDimension, bool isDay);

// ---- HUD texts (ClockDisplay, MoonDialDisplay) ------------------------------------------------------

/// ClockDisplay's time of the clock: (dayTime + 6000) % 24000 as a real 24 h day, "HH:mm" or
/// "hh:mm a" (Java's DateTimeFormatter, English AM / PM).
[[nodiscard]] std::string clockText(std::int64_t dayTime, bool use24HourFormat);
/// ClockDisplay#getFrameForTime: 0 sunrise (0..500, 22000..), 1 day (501..12499), 2 sunset
/// (12500..13999), 3 night (14000..21999); the icon is (frame % 2 * 8, frame / 2 % 2 * 8) of time.png.
[[nodiscard]] int clockFrame(std::int64_t dayTime);
/// DimensionType#moonPhase(dayTime) = (dayTime / 24000 % 8 + 8) % 8 (the level's day time, never the
/// dimension's fixed_time: day 3 in the Gloaming Forest is phase 3).
[[nodiscard]] inline int moonPhaseOf(std::int64_t dayTime) { return static_cast<int>((dayTime / 24000 % 8 + 8) % 8); }
/// MoonDialDisplay's icon of moon phase k: (k % 4 * 8, k / 4 % 2 * 8) of moon.png (32 x 16).
[[nodiscard]] std::array<int, 2> moonIconUv(int phase);

// ---- perfect dodge (TravellersGearEvents#performPerfectDodge) -----------------------------------------

/// Whether a damage type is a projectile's own impact on an entity (ProjectileImpactEvent with an
/// EntityHitResult): arrows, tridents, thrown items, mob projectiles, fireballs, wither skulls, wind
/// charges, llama spit. Explosions, fire, magic and the rest are not.
[[nodiscard]] bool isProjectileImpactType(std::string const& damageType);

// ---- TooltipStringInterpolator -------------------------------------------------------------------

/// Replaces every ${gloamingkeybinds/<key name>} in `text` with `keyName(<key name>)`. Other forms are
/// left as they are (upstream throws for them: none of its strings has one).
[[nodiscard]] std::string interpolate(std::string const& text, std::function<std::string(std::string const&)> const& keyName);

} // namespace gf::core::tgear

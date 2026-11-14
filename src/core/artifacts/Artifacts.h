// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/events/CharmEvents.java
//   src/main/java/gloamingforest/util/GloamingItemStackUtils.java
//   src/main/java/gloamingforest/components/entity/FortificationShieldAttachment.java
//   src/main/java/gloamingforest/components/item/PotionFlaskComponent.java
//   src/main/java/gloamingforest/components/entity/PotionFlaskTrackingAttachment.java
//   src/main/java/gloamingforest/item/BrittleFlaskItem.java
//   src/main/java/gloamingforest/components/item/OreScannerComponent.java
//   src/main/java/gloamingforest/components/item/OreScannerData.java
//   src/main/java/gloamingforest/item/recipe/ScepterRepairRecipe.java
//   src/main/java/gloamingforest/item/MoonDialItem.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U9 (artifacts): the game-independent rules of upstream's charms, keepsake casket, fortification
// shields, potion flasks, ore meter and scepter repairs (gtested in tests/artifacts). The feature
// layer (src/feature/artifacts) maps the game's stacks and entities onto these.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gf::core::artifacts {

inline constexpr char kCasket[]   = "gloamingforest:keepsake_casket";
inline constexpr char kKeeping1[] = "gloamingforest:charm_of_keeping_1";
inline constexpr char kKeeping2[] = "gloamingforest:charm_of_keeping_2";
inline constexpr char kKeeping3[] = "gloamingforest:charm_of_keeping_3";

// ---- charms of keeping and the keepsake casket (CharmEvents) ------------------------------------------

/// One slot's stack: the item id ("" = empty) and its count.
struct Slot {
    std::string id;
    int         count = 0;
    [[nodiscard]] bool empty() const { return id.empty() || count <= 0; }
    bool               operator==(Slot const&) const = default;
};

/// A player's inventory in Java's layout: `main` 0..35 (hotbar 0..8), `armor` in Java's order (0 feet,
/// 1 legs, 2 chest, 3 head), the off-hand, and the selected hotbar slot.
struct Inventory {
    std::array<Slot, 36> main{};
    std::array<Slot, 4>  armor{};
    Slot                 offhand{};
    int                  selected = 0;
    [[nodiscard]] bool   empty() const;
};

/// Where a stack sits (GloamingItemStackUtils#consumeInventoryItem searches armor, main, off-hand).
struct SlotRef {
    enum class Area { Main, Armor, Offhand } area = Area::Main;
    int  index = 0;
    bool operator==(SlotRef const&) const = default;
};

/// CharmEvents#handleCharmOfKeeping: what the charm tiers keep (III whole inventory, II hotbar, I no
/// main slot: upstream's NonNullList.of quirk), with armour and off-hand. `inv` keeps what is not kept
/// (and loses the consumed charm); `kept` holds the kept stacks at their slots.
struct KeepingResult {
    int                    tier = 0;            ///< 3, 2, 1, or 0 when no charm applied
    std::optional<SlotRef> consumedFrom;        ///< the slot one charm was taken from
    Inventory              kept;                ///< kept stacks by slot (empty elsewhere)
};
[[nodiscard]] KeepingResult applyKeeping(Inventory& inv);

/// GloamingItemStackUtils#consumeInventoryItem: takes one `id` (armor, main, off-hand order); nullopt when
/// none.
std::optional<SlotRef> consumeOne(Inventory& inv, std::string const& id);

/// CharmEvents#stockKeepsakeCasket's condition: a non-casket stack left anywhere.
[[nodiscard]] bool hasNonCasketItem(Inventory const& inv);

/// The 45 casket slots: armour reversed (head first), 4 empty, off-hand, main 9..35, then the hotbar.
[[nodiscard]] std::vector<Slot> casketLayout(Inventory const& inv);
/// The layout as indices into the inventory (-1: an empty filler slot): same order as casketLayout.
/// Index i < 36 is main[i], 36..39 armor[i - 36] (Java order), 40 the off-hand.
[[nodiscard]] std::vector<int> casketSourceSlots();

/// The y a casket is placed at before the replaceable search (stockKeepsakeCasket): below minY + 2
/// -> minY + 2, above logicalHeight -> logicalHeight - 1.
[[nodiscard]] int casketStartY(int y, int minY, int logicalHeight);

/// The casket's state after the 15 % damage roll: {damage, skullChest}.
struct CasketRoll {
    int  damage     = 0;
    bool skullChest = false;
};
[[nodiscard]] CasketRoll rollCasketDamage(int damage, float roll);

/// Java's Direction#from2DDataValue for the facing roll (0 south, 1 west, 2 north, 3 east).
[[nodiscard]] char const* facingName(int dataValue);

/// The casket's name: the player's name cut to 12 characters + "'s " + the block name.
[[nodiscard]] std::string casketName(std::string const& playerName, std::string const& blockName);

// ---- fortification shields (FortificationShieldAttachment) ---------------------------------------------

inline constexpr int kShieldTimer = 240;
inline constexpr int kShieldMax   = 115;

struct Shields {
    int temporary = 0;
    int permanent = 0;
    int timer     = kShieldTimer;

    [[nodiscard]] int left() const { return temporary + permanent; }
    /// One tick (server): true when a temporary shield expired this tick. `invulnerable` = a player
    /// with the invulnerable ability; `crown` = wearing the mystic crown; `tickCount` = the entity's.
    bool tick(bool invulnerable, bool crown, int tickCount);
    /// Breaks one shield (temporary first, resetting the timer).
    void breakOne();
    void set(int amount, bool temp);
    void add(int amount, bool temp);
};

// ---- potions and flasks ---------------------------------------------------------------------------------

struct EffectSpec {
    std::string id;         ///< Bedrock effect name ("speed", "instant_damage")
    int         ticks  = 0; ///< duration (0 for instant effects)
    int         amp    = 0;
    bool        instant = false;
    unsigned    color  = 0; ///< MobEffect#getColor
};
struct PotionSpec {
    int                     aux = 0;  ///< Bedrock potion data value
    std::string             javaId;   ///< Java's potion id ("minecraft:strong_harming")
    std::vector<EffectSpec> effects;
};
/// The potion of a Bedrock data value (nullptr: unknown).
[[nodiscard]] PotionSpec const* potionByAux(int aux);
/// PotionContents#getColor (0xRRGGBB): the effects' colours weighted by amplifier + 1, else water's.
[[nodiscard]] unsigned potionColor(int aux);

inline constexpr int kFlaskDoses = 3;

/// PotionFlaskComponent (`potion` -1: empty).
struct Flask {
    int  potion    = -1;
    int  doses     = 0;
    int  breakage  = 0;
    bool breakable = true;

    [[nodiscard]] bool empty() const { return potion < 0; }
    /// BrittleFlaskItem#overrideOtherStackedOnMe's test: empty or the same potion, and room.
    [[nodiscard]] bool canFill(int potionAux) const;
    [[nodiscard]] Flask addDose(int potionAux) const;
    [[nodiscard]] Flask removeDose() const;
    /// BrittleFlaskItem#getBarWidth (0..13).
    [[nodiscard]] int barWidth() const;
};

/// PotionFlaskTrackingAttachment#trackDrink: {doses, seconds} for drink_from_flask.
struct FlaskTracking {
    std::string lastPotion;    ///< Java id ("" = none)
    int         dosesDrank = 0;
    std::int64_t startTime = 0;
    std::pair<int, int> track(std::string const& javaPotion, std::int64_t gameTime);
    void                reset();
};

// ---- ore meter (OreScannerComponent / OreScannerData) ---------------------------------------------------

struct OreScan {
    int originX = 0, originZ = 0;
    int xSpan = 0, zSpan = 0;
    int duration = 0;
    int progress = 0;
    std::map<std::string, int> counts; ///< block id -> count (every block scanned so far)

    /// OreScannerComponent#scanFromCenter.
    [[nodiscard]] static OreScan fromCenter(int blockX, int blockZ, int range, int durationTicks);
    [[nodiscard]] int  area() const { return xSpan * zSpan; }
    [[nodiscard]] bool isEmpty() const { return area() <= 0; }
    [[nodiscard]] bool finished() const { return isEmpty() || progress >= duration; }
    /// OreScannerComponent#tickScan over [minY, minY + maxY) (Java's volume = area * maxBuildHeight).
    void tick(int minY, int maxY, std::function<std::string(int x, int y, int z)> const& blockAt);
    [[nodiscard]] int volume(int maxY) const { return area() * maxY; }
    /// OreScannerComponent#centerChunkPos.
    [[nodiscard]] std::pair<int, int> centerChunk() const;
};
/// OreMeterItem: 50 + 25 per range step.
[[nodiscard]] int oreScanTicks(int range);
/// OreScannerData#create's universal id (Java's Map#hashCode of the counts with String#hashCode).
[[nodiscard]] std::int64_t oreUniversalId(std::map<std::string, int> const& counts, int chunkX, int chunkZ, int range, int total);
/// Java's String#hashCode (UTF-16 code units of ASCII / BMP text).
[[nodiscard]] std::int32_t javaStringHash(std::string const& s);
/// The "%.3f" ratio upstream shows (count * 100 / total, DecimalFormat "0.000").
[[nodiscard]] std::string oreRatio(int count, int total);

// ---- scepters ------------------------------------------------------------------------------------------

/// ScepterRepairRecipe#matches for a single repair ingredient type: `n` ingredients on a scepter of
/// `damage`.
[[nodiscard]] bool scepterRepairMatches(int damage, int n, int durability);
/// The damage after the repair (clamped at 0 as ItemStack#setDamageValue).
[[nodiscard]] int scepterRepairResult(int damage, int n, int durability);

// ---- moon dial ------------------------------------------------------------------------------------------

/// MoonDialItem#getMoonPhase's key suffix: the phase 0..7 in natural dimensions, else "unknown"
/// ("unknown_fools" on April 1st).
[[nodiscard]] std::string moonPhaseKey(bool natural, std::int64_t dayTime, bool aprilFools);

} // namespace gf::core::artifacts

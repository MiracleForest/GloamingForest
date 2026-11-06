// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/events/CharmEvents.java
//   src/main/java/twilightforest/util/TFItemStackUtils.java
//   src/main/java/twilightforest/components/entity/FortificationShieldAttachment.java
//   src/main/java/twilightforest/components/item/PotionFlaskComponent.java
//   src/main/java/twilightforest/components/entity/PotionFlaskTrackingAttachment.java
//   src/main/java/twilightforest/item/BrittleFlaskItem.java
//   src/main/java/twilightforest/components/item/OreScannerComponent.java
//   src/main/java/twilightforest/components/item/OreScannerData.java
//   src/main/java/twilightforest/item/recipe/ScepterRepairRecipe.java
//   src/main/java/twilightforest/item/MoonDialItem.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/artifacts/Artifacts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gf::core::artifacts {

namespace {

constexpr int kMain = 36;

/// keepWholeListAndCheckCasket for one list (`to` and `from` are parallel slot lists).
bool keepList(std::vector<Slot*> const& to, std::vector<Slot*> const& from, bool skipCasketCheck) {
    bool keptCasket = false;
    for (std::size_t i = 0; i < from.size(); ++i) {
        Slot item = *from[i];
        if (skipCasketCheck || item.id != kCasket || keptCasket) {
            *to[i]   = item;
            *from[i] = {};
        } else {
            keptCasket = true;
            if (item.count > 1) {
                *to[i]   = Slot{item.id, item.count - 1};
                *from[i] = Slot{item.id, 1};
            }
        }
    }
    return keptCasket || skipCasketCheck;
}

/// CharmEvents#applyCharm: `scope` = the main slots the tier keeps.
bool applyCharm(Inventory& inv, Inventory& kept, std::string const& charm, std::vector<int> const& scope, KeepingResult& out) {
    // every affected slot, the charm itself left out, must hold something
    bool any = false;
    auto look = [&](Slot const& s) {
        if (!s.empty() && s.id != charm) any = true;
    };
    for (int i : scope) look(inv.main[static_cast<std::size_t>(i)]);
    for (auto const& a : inv.armor) look(a);
    look(inv.offhand);
    if (!any) return false;
    auto from = consumeOne(inv, charm);
    if (!from) return false;
    out.consumedFrom = from;
    std::vector<Slot*> to, src;
    for (int i : scope) {
        to.push_back(&kept.main[static_cast<std::size_t>(i)]);
        src.push_back(&inv.main[static_cast<std::size_t>(i)]);
    }
    bool keptCasket = keepList(to, src, charm == kKeeping3);
    to.clear(), src.clear();
    for (std::size_t i = 0; i < 4; ++i) {
        to.push_back(&kept.armor[i]);
        src.push_back(&inv.armor[i]);
    }
    keptCasket = keepList(to, src, keptCasket);
    keepList({&kept.offhand}, {&inv.offhand}, keptCasket);
    return true;
}

} // namespace

bool Inventory::empty() const {
    for (auto const& s : main)
        if (!s.empty()) return false;
    for (auto const& s : armor)
        if (!s.empty()) return false;
    return offhand.empty();
}

std::optional<SlotRef> consumeOne(Inventory& inv, std::string const& id) {
    auto take = [&](Slot& s) {
        if (s.empty() || s.id != id) return false;
        if (--s.count <= 0) s = {};
        return true;
    };
    for (int i = 0; i < 4; ++i)
        if (take(inv.armor[static_cast<std::size_t>(i)])) return SlotRef{SlotRef::Area::Armor, i};
    for (int i = 0; i < kMain; ++i)
        if (take(inv.main[static_cast<std::size_t>(i)])) return SlotRef{SlotRef::Area::Main, i};
    if (take(inv.offhand)) return SlotRef{SlotRef::Area::Offhand, 0};
    return std::nullopt;
}

KeepingResult applyKeeping(Inventory& inv) {
    KeepingResult out;
    std::vector<int> all, hotbar;
    for (int i = 0; i < kMain; ++i) all.push_back(i);
    for (int i = 0; i < 9; ++i) hotbar.push_back(i);
    if (applyCharm(inv, out.kept, kKeeping3, all, out)) out.tier = 3;
    else if (applyCharm(inv, out.kept, kKeeping2, hotbar, out)) out.tier = 2;
    // NonNullList.of(items.get(selected)) is an empty list: charm I keeps no main slot
    else if (inv.selected >= 0 && inv.selected < 9 && applyCharm(inv, out.kept, kKeeping1, {}, out)) out.tier = 1;
    return out;
}

bool hasNonCasketItem(Inventory const& inv) {
    auto other = [](Slot const& s) { return !s.empty() && s.id != kCasket; };
    return std::any_of(inv.main.begin(), inv.main.end(), other) || std::any_of(inv.armor.begin(), inv.armor.end(), other) || other(inv.offhand);
}

std::vector<int> casketSourceSlots() {
    std::vector<int> out;
    for (int i = 3; i >= 0; --i) out.push_back(kMain + i); // sortArmorForCasket: head first
    for (int i = 0; i < 4; ++i) out.push_back(-1);         // the filler
    out.push_back(kMain + 4);                              // the off-hand
    for (int i = 9; i < kMain; ++i) out.push_back(i);      // sortInvForCasket: hotbar at the bottom
    for (int i = 0; i < 9; ++i) out.push_back(i);
    return out;
}

std::vector<Slot> casketLayout(Inventory const& inv) {
    std::vector<Slot> out;
    for (int src : casketSourceSlots()) {
        if (src < 0) out.push_back({});
        else if (src < kMain) out.push_back(inv.main[static_cast<std::size_t>(src)]);
        else if (src < kMain + 4) out.push_back(inv.armor[static_cast<std::size_t>(src - kMain)]);
        else out.push_back(inv.offhand);
    }
    return out;
}

int casketStartY(int y, int minY, int logicalHeight) {
    if (y < minY + 2) return minY + 2;
    if (y > logicalHeight) return logicalHeight - 1;
    return y;
}

CasketRoll rollCasketDamage(int damage, float roll) {
    if (roll <= 0.15F) {
        if (damage >= 2) return {damage, true};
        return {damage + 1, false};
    }
    return {damage, false};
}

char const* facingName(int dataValue) {
    switch (((dataValue % 4) + 4) % 4) {
    case 0: return "south";
    case 1: return "west";
    case 2: return "north";
    default: return "east";
    }
}

std::string casketName(std::string const& playerName, std::string const& blockName) {
    // String#substring counts UTF-16 units; names are ASCII on both editions
    return playerName.substr(0, std::min<std::size_t>(12, playerName.size())) + "'s " + blockName;
}

// ---- shields ----------------------------------------------------------------------------------------

bool Shields::tick(bool invulnerable, bool crown, int tickCount) {
    if (temporary > 0 && !invulnerable) {
        if (timer <= 0) {
            breakOne();
            return true;
        }
        // the mystic crown skips every third tick of the decay
        if (!crown || (tickCount % 3) != 0) --timer;
    }
    return false;
}

void Shields::breakOne() {
    if (temporary > 0) {
        --temporary;
        timer = kShieldTimer;
    } else if (permanent > 0) {
        --permanent;
    }
}

void Shields::set(int amount, bool temp) {
    if (temp) {
        temporary = std::clamp(amount, 0, kShieldMax);
        timer     = kShieldTimer;
    } else {
        permanent = std::clamp(amount, 0, kShieldMax);
    }
}

void Shields::add(int amount, bool temp) {
    if (temp) {
        if (temporary <= 0) timer = kShieldTimer;
        temporary = std::clamp(temporary + amount, 0, kShieldMax);
    } else {
        permanent = std::clamp(permanent + amount, 0, kShieldMax);
    }
}

// ---- potions ----------------------------------------------------------------------------------------

namespace {

// MobEffect#getColor (Java 1.21)
constexpr unsigned kSpeed = 0x33EBFF, kSlow = 0x8BAFE0, kStrength = 0xFFC700, kHeal = 0xF82423, kHarm = 0xA9656A, kJump = 0xFDFF84,
                   kRegen = 0xCD5CAB, kResist = 0x9146F0, kFireRes = 0xFF9900, kWater = 0x98DAC0, kInvis = 0xF6F6F6, kNight = 0xC2FF66,
                   kWeak = 0x484D48, kPoison = 0x87A363, kWither = 0x736156, kSlowFall = 0xF3CFB9, kWind = 0xBDC9FF, kWeaving = 0x78695A,
                   kOozing = 0x99FFA3, kInfested = 0x8C9B8C;
constexpr unsigned kWaterColor = 0x385DC6; // PotionContents.BASE_POTION_COLOR

EffectSpec e(char const* id, int ticks, int amp, unsigned color) { return {id, ticks, amp, false, color}; }
EffectSpec instant(char const* id, int amp, unsigned color) { return {id, 0, amp, true, color}; }

std::vector<PotionSpec> const& potions() {
    static std::vector<PotionSpec> const table{
        {0, "minecraft:water", {}},
        {1, "minecraft:mundane", {}},
        {2, "minecraft:mundane", {}},
        {3, "minecraft:thick", {}},
        {4, "minecraft:awkward", {}},
        {5, "minecraft:night_vision", {e("night_vision", 3600, 0, kNight)}},
        {6, "minecraft:long_night_vision", {e("night_vision", 9600, 0, kNight)}},
        {7, "minecraft:invisibility", {e("invisibility", 3600, 0, kInvis)}},
        {8, "minecraft:long_invisibility", {e("invisibility", 9600, 0, kInvis)}},
        {9, "minecraft:leaping", {e("jump_boost", 3600, 0, kJump)}},
        {10, "minecraft:long_leaping", {e("jump_boost", 9600, 0, kJump)}},
        {11, "minecraft:strong_leaping", {e("jump_boost", 1800, 1, kJump)}},
        {12, "minecraft:fire_resistance", {e("fire_resistance", 3600, 0, kFireRes)}},
        {13, "minecraft:long_fire_resistance", {e("fire_resistance", 9600, 0, kFireRes)}},
        {14, "minecraft:swiftness", {e("speed", 3600, 0, kSpeed)}},
        {15, "minecraft:long_swiftness", {e("speed", 9600, 0, kSpeed)}},
        {16, "minecraft:strong_swiftness", {e("speed", 1800, 1, kSpeed)}},
        {17, "minecraft:slowness", {e("slowness", 1800, 0, kSlow)}},
        {18, "minecraft:long_slowness", {e("slowness", 4800, 0, kSlow)}},
        {19, "minecraft:water_breathing", {e("water_breathing", 3600, 0, kWater)}},
        {20, "minecraft:long_water_breathing", {e("water_breathing", 9600, 0, kWater)}},
        {21, "minecraft:healing", {instant("instant_health", 0, kHeal)}},
        {22, "minecraft:strong_healing", {instant("instant_health", 1, kHeal)}},
        {23, "minecraft:harming", {instant("instant_damage", 0, kHarm)}},
        {24, "minecraft:strong_harming", {instant("instant_damage", 1, kHarm)}},
        {25, "minecraft:poison", {e("poison", 900, 0, kPoison)}},
        {26, "minecraft:long_poison", {e("poison", 1800, 0, kPoison)}},
        {27, "minecraft:strong_poison", {e("poison", 432, 1, kPoison)}},
        {28, "minecraft:regeneration", {e("regeneration", 900, 0, kRegen)}},
        {29, "minecraft:long_regeneration", {e("regeneration", 1800, 0, kRegen)}},
        {30, "minecraft:strong_regeneration", {e("regeneration", 450, 1, kRegen)}},
        {31, "minecraft:strength", {e("strength", 3600, 0, kStrength)}},
        {32, "minecraft:long_strength", {e("strength", 9600, 0, kStrength)}},
        {33, "minecraft:strong_strength", {e("strength", 1800, 1, kStrength)}},
        {34, "minecraft:weakness", {e("weakness", 1800, 0, kWeak)}},
        {35, "minecraft:long_weakness", {e("weakness", 4800, 0, kWeak)}},
        {36, "minecraft:wither", {e("wither", 800, 0, kWither)}}, // Bedrock's potion of decay (no Java potion)
        {37, "minecraft:turtle_master", {e("slowness", 400, 3, kSlow), e("resistance", 400, 2, kResist)}},
        {38, "minecraft:long_turtle_master", {e("slowness", 800, 3, kSlow), e("resistance", 800, 2, kResist)}},
        {39, "minecraft:strong_turtle_master", {e("slowness", 400, 5, kSlow), e("resistance", 400, 3, kResist)}},
        {40, "minecraft:slow_falling", {e("slow_falling", 1800, 0, kSlowFall)}},
        {41, "minecraft:long_slow_falling", {e("slow_falling", 4800, 0, kSlowFall)}},
        {42, "minecraft:strong_slowness", {e("slowness", 400, 3, kSlow)}},
        {43, "minecraft:wind_charged", {e("wind_charged", 3600, 0, kWind)}},
        {44, "minecraft:weaving", {e("weaving", 3600, 0, kWeaving)}},
        {45, "minecraft:oozing", {e("oozing", 3600, 0, kOozing)}},
        {46, "minecraft:infested", {e("infested", 3600, 0, kInfested)}},
    };
    return table;
}

} // namespace

PotionSpec const* potionByAux(int aux) {
    for (auto const& p : potions())
        if (p.aux == aux) return &p;
    return nullptr;
}

unsigned potionColor(int aux) {
    auto const* p = potionByAux(aux);
    if (!p || p->effects.empty()) return kWaterColor;
    long r = 0, g = 0, b = 0, w = 0;
    for (auto const& ef : p->effects) {
        int amp = ef.amp + 1;
        r += amp * static_cast<long>((ef.color >> 16) & 0xFF);
        g += amp * static_cast<long>((ef.color >> 8) & 0xFF);
        b += amp * static_cast<long>(ef.color & 0xFF);
        w += amp;
    }
    return static_cast<unsigned>(((r / w) << 16) | ((g / w) << 8) | (b / w));
}

bool Flask::canFill(int potionAux) const { return (empty() || potion == potionAux) && doses < kFlaskDoses - breakage; }

Flask Flask::addDose(int potionAux) const { return Flask{potionAux, doses + 1, breakage, breakable}; }

Flask Flask::removeDose() const {
    int newDose = doses - 1;
    return Flask{newDose < 1 ? -1 : potion, newDose, breakable ? breakage + 1 : breakage, breakable};
}

int Flask::barWidth() const { return static_cast<int>(std::lround(13.0F - static_cast<float>(std::abs(doses - kFlaskDoses)) * 13.0F / kFlaskDoses)); }

std::pair<int, int> FlaskTracking::track(std::string const& javaPotion, std::int64_t gameTime) {
    if (lastPotion.empty() || lastPotion != javaPotion) {
        dosesDrank = 1;
        startTime  = gameTime;
    } else {
        ++dosesDrank;
    }
    lastPotion = javaPotion;
    return {dosesDrank, static_cast<int>(std::floor(static_cast<float>(gameTime - startTime) / 20.0F))};
}

void FlaskTracking::reset() {
    dosesDrank = 0;
    lastPotion.clear();
}

// ---- ore meter --------------------------------------------------------------------------------------

OreScan OreScan::fromCenter(int blockX, int blockZ, int range, int durationTicks) {
    int     cx = blockX >> 4, cz = blockZ >> 4;
    OreScan s;
    s.originX  = (cx - range) * 16;
    s.originZ  = (cz - range) * 16;
    s.xSpan    = (cx + range) * 16 + 15 - s.originX; // SectionPos#sectionToBlockCoord(c, 15)
    s.zSpan    = (cz + range) * 16 + 15 - s.originZ;
    s.duration = durationTicks;
    return s;
}

void OreScan::tick(int minY, int maxY, std::function<std::string(int, int, int)> const& blockAt) {
    if (isEmpty()) return;
    long long vol      = static_cast<long long>(area()) * maxY;
    int       march    = static_cast<int>(std::ceil(static_cast<float>(vol) / static_cast<float>(std::abs(duration == 0 ? 1 : duration))));
    long long progress0 = static_cast<long long>(progress) * march;
    for (int step = 0; step < march && progress0 + step < vol; ++step) {
        long long n  = progress0 + step;
        int       dx = static_cast<int>(n % xSpan);
        int       dz = static_cast<int>(n % (static_cast<long long>(xSpan) * zSpan) / xSpan);
        int       dy = static_cast<int>(n / (static_cast<long long>(xSpan) * zSpan));
        ++counts[blockAt(originX + dx, minY + dy, originZ + dz)];
    }
    ++progress;
}

std::pair<int, int> OreScan::centerChunk() const {
    int x = static_cast<int>(std::floor(static_cast<float>(originX) + static_cast<float>(xSpan) / 2.0F)) >> 4;
    int z = static_cast<int>(std::floor(static_cast<float>(originZ) + static_cast<float>(zSpan) / 2.0F)) >> 4;
    return {x, z};
}

int oreScanTicks(int range) { return 50 + range * 25; }

std::int32_t javaStringHash(std::string const& s) {
    std::uint32_t h = 0;
    for (unsigned char c : s) h = 31u * h + c;
    return static_cast<std::int32_t>(h);
}

std::int64_t oreUniversalId(std::map<std::string, int> const& counts, int chunkX, int chunkZ, int range, int total) {
    std::int32_t hash = 0; // AbstractMap#hashCode: the sum of the entries' key.hashCode() ^ value.hashCode()
    for (auto const& [k, v] : counts) hash = static_cast<std::int32_t>(static_cast<std::uint32_t>(hash) + static_cast<std::uint32_t>(javaStringHash(k) ^ v));
    std::uint64_t chunk = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX))) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkZ)) << 32);
    std::int64_t  left  = static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(hash)) ^ chunk);
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) * static_cast<std::uint64_t>(static_cast<std::int64_t>(range ^ total)));
}

std::string oreRatio(int count, int total) {
    float pct = total > 0 ? static_cast<float>(count) * 100.0F / static_cast<float>(total) : 0.0F;
    char  buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", pct);
    return buf;
}

// ---- scepters -----------------------------------------------------------------------------------------

bool scepterRepairMatches(int damage, int n, int durability) { return damage > 0 && n > 0 && damage + durability - n * durability > 0; }

int scepterRepairResult(int damage, int n, int durability) { return std::max(0, damage - durability * std::max(n, 1)); }

// ---- moon dial ----------------------------------------------------------------------------------------

std::string moonPhaseKey(bool natural, std::int64_t dayTime, bool aprilFools) {
    if (natural) return std::to_string(static_cast<int>((dayTime / 24000 % 8 + 8) % 8));
    return aprilFools ? "unknown_fools" : "unknown";
}

} // namespace gf::core::artifacts

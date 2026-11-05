// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/components/item/ItemDisplayContents.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorBeltItem.java
//   src/main/java/twilightforest/item/travellers_gear/TravellersGearLogic.java
//   src/main/java/twilightforest/item/travellers_gear/modifiers/TooltipStringInterpolator.java
//   src/main/java/twilightforest/client/overlay/display/ClockDisplay.java
//   src/main/java/twilightforest/client/overlay/display/MoonDialDisplay.java
//   src/main/java/twilightforest/util/TFMathUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/tgear/Logic.h"

#include <cmath>
#include <cstdio>

namespace gf::core::tgear {

namespace {

int findInsertSlot(DisplayContents const& c, DisplayType type) {
    for (int i = 0; i < kDisplaySlots; ++i)
        if (kDisplayLayout[i] == type && c.items[i].empty()) return i;
    return -1;
}

int findSwapSlot(DisplayType type) {
    for (int i = 0; i < kDisplaySlots; ++i)
        if (kDisplayLayout[i] == type) return i;
    return -1;
}

bool hasOtherMaps(DisplayContents const& c, int mapIndex) {
    for (int i = 0; i < kDisplaySlots; ++i)
        if (!c.items[i].empty() && kDisplayLayout[i] == DisplayType::Map && i != mapIndex) return true;
    return false;
}

void tryResetChosenMapSlot(DisplayContents& c, int index) {
    if (index >= 0 && index < kDisplaySlots && kDisplayLayout[index] == DisplayType::Map && !hasOtherMaps(c, index))
        c.chosenMapSlot = index;
}

} // namespace

SwapResult trySwap(DisplayContents& contents, DisplayItem const& source) {
    SwapResult r;
    if (source.empty() || source.type == DisplayType::None) return r;
    int slot = findInsertSlot(contents, source.type);
    if (slot < 0) slot = findSwapSlot(source.type);
    if (slot < 0) return r;
    auto const& target = contents.items[slot];
    if (!target.empty() && target.identity == source.identity) return r;
    r.ok       = true;
    r.slot     = slot;
    r.replaced = contents.items[slot];
    contents.items[slot] = source;
    if (r.replaced.empty()) tryResetChosenMapSlot(contents, slot);
    return r;
}

int removeFirstFree(DisplayContents& contents, std::function<bool(DisplayItem const&)> const& mayPlace) {
    for (int i = 0; i < kDisplaySlots; ++i) {
        auto const& item = contents.items[i];
        if (item.empty() || (mayPlace && !mayPlace(item))) continue;
        if (i == contents.chosenMapSlot) cycleChosenMapSlot(contents);
        contents.items[i] = {};
        return i;
    }
    return -1;
}

int cycleChosenMapSlot(DisplayContents& contents) {
    for (int index = contents.chosenMapSlot + 1; index < kDisplaySlots; ++index) {
        if (index < 0) continue;
        if (kDisplayLayout[index] == DisplayType::Map && !contents.items[index].empty()) {
            contents.chosenMapSlot = index;
            return index;
        }
    }
    contents.chosenMapSlot = -1;
    return -1;
}

BeltSwapPlan planHotbarSwap(std::array<HotbarSlot, 9> const& hotbar, std::array<bool, 9> const& beltEmpty, bool active) {
    BeltSwapPlan plan;
    for (int i = 0; i < 9; ++i) {
        auto const& h = hotbar[i];
        if (!h.allowed || !(active || h.empty)) continue;
        plan.exchange[i] = true;
        if (!(h.empty && beltEmpty[i])) plan.changed = true;
    }
    return plan;
}

double probabilityOfAtLeastOneSuccess(double p, double tries) { return 1 - std::pow(1 - p, tries); }

double autoRepairChance(double base, bool canSeeSky, bool twilight, bool isDay) {
    if (!canSeeSky) return base;
    double boost;
    if (twilight) boost = kAutoRepairTwilightBoost;
    else if (isDay) boost = kAutoRepairSunlightBoost;
    else return base;
    return probabilityOfAtLeastOneSuccess(base, boost);
}

std::string clockText(std::int64_t dayTime, bool use24) {
    std::int64_t ticks = ((dayTime + 6000) % 24000 + 24000) % 24000;
    std::int64_t secs  = ticks * 86400 / 24000;
    int          h     = static_cast<int>(secs / 3600);
    int          m     = static_cast<int>(secs / 60 % 60);
    char         buf[16];
    if (use24) std::snprintf(buf, sizeof buf, "%02d:%02d", h, m);
    else {
        int h12 = h % 12 == 0 ? 12 : h % 12;
        std::snprintf(buf, sizeof buf, "%02d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
    }
    return buf;
}

int clockFrame(std::int64_t dayTime) {
    std::int64_t t = dayTime % 24000; // Java's % keeps the sign; day time is never negative
    if (t >= 22000) return 0;
    if (t >= 14000) return 3;
    if (t >= 12500) return 2;
    if (t >= 501) return 1;
    return 0;
}

std::array<int, 2> moonIconUv(int phase) { return {phase % 4 * 8, phase / 4 % 2 * 8}; }

std::string interpolate(std::string const& text, std::function<std::string(std::string const&)> const& keyName) {
    std::string out;
    std::size_t pos = 0;
    while (true) {
        auto open = text.find("${", pos);
        if (open == std::string::npos) break;
        auto close = text.find('}', open + 2);
        if (close == std::string::npos) break;
        out.append(text, pos, open - pos);
        std::string var   = text.substr(open + 2, close - open - 2);
        auto        slash = var.find('/');
        if (slash != std::string::npos && var.find('/', slash + 1) == std::string::npos && var.substr(0, slash) == "tfkeybinds")
            out += keyName(var.substr(slash + 1));
        else out.append(text, open, close - open + 1);
        pos = close + 1;
    }
    out.append(text, pos);
    return out;
}

} // namespace gf::core::tgear

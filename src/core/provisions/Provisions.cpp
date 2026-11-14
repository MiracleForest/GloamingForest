// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFItems.java
//   src/main/java/gloamingforest/block/Experiment115Block.java
//   src/main/java/gloamingforest/potions/FrostedEffect.java
//   src/main/java/gloamingforest/init/TFJukeboxSongs.java
//   src/main/java/gloamingforest/item/ChainBlockItem.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/provisions/Provisions.h"

#include <algorithm>
#include <cmath>

namespace gf::core::provisions {

std::array<Food, 23> const& foods() {
    static std::array<Food, 23> const table{{
        {"gloamingforest:raw_venison", 2, 0.3f},
        {"gloamingforest:cooked_venison", 6, 0.6f},
        {"gloamingforest:raw_meef", 2, 0.2f},
        {"gloamingforest:cooked_meef", 7, 0.6f},
        {"gloamingforest:meef_stroganoff", 14, 1.2f, true, true, {{{"minecraft:fire_resistance", 3600, 0}, {"minecraft:strength", 2400, 0}}}},
        {"gloamingforest:hydra_chop", 18, 2.0f, false, false, {{{"minecraft:regeneration", 100, 0}, {}}}},
        {"gloamingforest:maze_wafer", 4, 0.6f},
        {"gloamingforest:berry_medley", 5, 0.6f, false, true},
        {"gloamingforest:moss_soup", 5, 0.6f, false, true},
        {"gloamingforest:shika_senbei", 4, 1.0f},
        {"gloamingforest:monster_jerky", 4, 0.275f},
        {"gloamingforest:beef_jerky", 6, 1.18f},
        {"gloamingforest:pork_jerky", 6, 1.18f},
        {"gloamingforest:venison_jerky", 6, 1.18f},
        {"gloamingforest:meef_jerky", 6, 1.18f},
        {"gloamingforest:chicken_jerky", 4, 1.075f},
        {"gloamingforest:mutton_jerky", 4, 1.35f},
        {"gloamingforest:salmon_jerky", 4, 1.35f},
        {"gloamingforest:rabbit_jerky", 4, 0.925f},
        {"gloamingforest:cod_jerky", 4, 0.875f},
        {"gloamingforest:tropical_fish_jerky", 2, 0.6f},
        {"gloamingforest:fugu_jerky", 2, 0.6f},
        {"gloamingforest:experiment_115", 4, 0.3f},
    }};
    return table;
}

Food const* food(std::string_view itemId) {
    for (auto const& f : foods())
        if (f.id == itemId) return &f;
    return nullptr;
}

E115Action e115Action(E115 const& s, E115Use const& u) {
    if (!u.sneaking) {
        if (u.holdsE115) return s.bites == 0 ? E115Action::Fail : E115Action::AddSlice;
        if (u.holdsRedstone && !s.regenerate && s.bites == 0) return E115Action::Regenerate;
    } else {
        if (!u.emptyHands) return E115Action::Pass; // a sneaking player with an item skips the block
        if (!s.regenerate) return E115Action::TakeSlice;
    }
    // PASS_TO_DEFAULT_BLOCK_INTERACTION -> useWithoutItem (Java calls it for any hand content that
    // the item interaction passed)
    return u.canEat ? E115Action::Eat : E115Action::Pass;
}

std::optional<E115> e115After(E115 const& s, E115Action a) {
    E115 n = s;
    switch (a) {
    case E115Action::AddSlice: n.bites = s.bites - 1; return n;
    case E115Action::Regenerate: n.regenerate = true; return n;
    case E115Action::TakeSlice:
    case E115Action::Eat:
        if (s.bites < 7) {
            n.bites = s.bites + 1;
            return n;
        }
        return std::nullopt;
    default: return s;
    }
}

std::optional<E115> e115RandomTick(E115 const& s) {
    if (!s.regenerate || s.bites == 0) return std::nullopt;
    return E115{s.bites - 1, true};
}

int frostedFrozenTick(int t, int amplifier, bool canFreeze) {
    if (!canFreeze) return thawTick(t);
    if (amplifier > 0) t = std::min(kTicksRequiredToFreeze, t + amplifier);
    return std::min(kTicksRequiredToFreeze, t + 1);
}

int chillAuraAmplifier(int level) { return std::max(0, static_cast<int>(std::lround(linear(0.0f, 1.0f, level)))); }

std::string_view destructionIncorrectTag(int level) {
    switch (level) {
    case 1: return "minecraft:incorrect_for_wooden_tool";
    case 2: return "minecraft:incorrect_for_stone_tool";
    default: return "minecraft:incorrect_for_iron_tool";
    }
}

std::array<Song, 9> const& songs() {
    static std::array<Song, 9> const table{{
        {"radiance", "radiance", 123},
        {"steps", "steps", 195},
        {"superstitious", "superstitious", 192},
        {"home", "home", 215},
        {"warfarer", "wayfarer", 173},
        {"findings", "findings", 196},
        {"maker", "maker", 207},
        {"thread", "thread", 201},
        {"motion", "motion", 169},
    }};
    return table;
}

} // namespace gf::core::provisions

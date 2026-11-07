// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757) and the
// Minecraft WorldlyContainer contract it relies on:
//   src/main/java/twilightforest/block/entity/CinderFurnaceBlockEntity.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "inventory" (F10): the side rules of container blocks (feature/ext/combat/InventoryFaces.h).
// The native backend's game container (Inventory.cpp, GfBlockContainer::canPushInItem /
// canPullOutItem) asks faceAllows for the game's hoppers, hopper minecarts and droppers.

#include "feature/ext/combat/InventoryFaces.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "mc/world/item/ItemStack.h"

namespace gf::inventory::ext {

namespace {

struct FaceStore {
    std::mutex                                 mutex;
    std::unordered_map<std::string, FaceRules> rules;
};

FaceStore& store() {
    static FaceStore s;
    return s;
}

} // namespace

void setFaceRules(std::string const& blockId, FaceRules rules) {
    std::lock_guard lock(store().mutex);
    store().rules[blockId] = std::move(rules);
}

void clearFaceRules(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    store().rules.erase(blockId);
}

std::optional<FaceRules> faceRules(std::string const& blockId) {
    std::lock_guard lock(store().mutex);
    auto            it = store().rules.find(blockId);
    if (it == store().rules.end()) return std::nullopt;
    return it->second;
}

bool faceAllows(std::string const& blockId, core::GlobalPos const& pos, int slot, ItemStack const& stack, int face, bool place) {
    if (face < 0 || face > 5) return true;
    FaceRules rules;
    {
        std::lock_guard lock(store().mutex);
        if (store().rules.empty()) return true; // the common case: no block has side rules
        auto it = store().rules.find(blockId);
        if (it == store().rules.end()) return true;
        rules = it->second; // a copy: the rules may run code that sets rules
    }
    if (rules.slotsForFace) {
        auto slots = rules.slotsForFace(pos, face);
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) return false;
    }
    auto const& rule = place ? rules.canPlaceThroughFace : rules.canTakeThroughFace;
    return !rule || rule(pos, slot, stack, face);
}

} // namespace gf::inventory::ext

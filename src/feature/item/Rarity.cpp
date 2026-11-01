// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/enums/extensions/TFRarityEnumExtension.java
//   src/main/java/twilightforest/TFEnumExtensions.java
//   src/main/java/twilightforest/init/TFItems.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's TWILIGHT rarity (Rarity_TWILIGHT: ChatFormatting.DARK_GREEN names) and its items: the nine
// boss trophies and the nine banner patterns of TFItems. The client mod colours their names
// (client/item/ItemClient.cpp); Bedrock's Rarity has only common / uncommon / rare / epic.
#include <map>
#include <mutex>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"

#include "feature/ext/item/Item.h"

namespace gf::item::ext {

namespace {

std::mutex& mutex() {
    static std::mutex m;
    return m;
}

std::map<std::string, Rarity>& table() {
    static std::map<std::string, Rarity> t = [] {
        std::map<std::string, Rarity> m;
        for (char const* boss : {"naga", "lich", "minoshroom", "hydra", "knight_phantom", "ur_ghast", "alpha_yeti", "snow_queen", "quest_ram"}) {
            m[std::string("twilightforest:") + boss + "_trophy"]         = Rarity::Twilight;
            m[std::string("twilightforest:") + boss + "_banner_pattern"] = Rarity::Twilight;
        }
        return m;
    }();
    return t;
}

} // namespace

char const* rarityColor(Rarity rarity) {
    switch (rarity) {
    case Rarity::Uncommon:
        return "\xC2\xA7" "e";
    case Rarity::Rare:
        return "\xC2\xA7" "b";
    case Rarity::Epic:
        return "\xC2\xA7" "d";
    case Rarity::Twilight:
        return "\xC2\xA7" "2";
    default:
        return "\xC2\xA7" "f";
    }
}

void setRarity(std::string const& itemId, Rarity rarity) {
    std::lock_guard lock(mutex());
    table()[itemId] = rarity;
}

Rarity rarityOf(std::string const& itemId) {
    std::lock_guard lock(mutex());
    auto            it = table().find(itemId);
    return it == table().end() ? Rarity::Common : it->second;
}

// ItemStack#getRarity: an enchanted stack shows COMMON / UNCOMMON as RARE and RARE as EPIC; EPIC and
// the extensions (TWILIGHT) keep theirs.
Rarity rarityOf(ItemStackBase const& stack) {
    if (stack.isNull() || !stack.mItem) return Rarity::Common;
    auto rarity = rarityOf(stack.mItem->mFullName->getString());
    if (!stack.isEnchanted()) return rarity;
    switch (rarity) {
    case Rarity::Common:
    case Rarity::Uncommon:
        return Rarity::Rare;
    case Rarity::Rare:
        return Rarity::Epic;
    default:
        return rarity;
    }
}

} // namespace gf::item::ext

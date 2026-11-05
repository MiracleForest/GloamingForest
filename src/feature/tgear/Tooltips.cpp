// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorItem.java (appendHoverText, getName)
//   src/main/java/twilightforest/client/renderer/tooltip/TravellersBeltTooltipComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The gear's hover text as lore (feature/item/Item.h: server-written, in the holder's language):
// "Ability: <name>" (gold, gray name) per ability, "- <name>" (gray) per insertable modifier, "- Empty"
// (dark gray) up to the slot count, the gloves' "Cosmetic", the broken mark and "Hold Shift for info".
// Bedrock lore cannot follow Shift: the client mod adds the description lines while Shift is held
// (client/tgear). The belt's / goggles' stored items (TF's tooltip images) are listed as lines.
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/item/Item.h"
#include "feature/tgear/Config.h"
#include "feature/tgear/Gear.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Module.h"

namespace gf::feature::tgear {

namespace {

namespace items = game::items;

std::string localeFor(Player const* viewer) { return viewer ? localeOf(*viewer) : std::string(); }

std::string t(Player const* viewer, std::string_view key, std::string_view fallback) {
    return std::string(translate(key, fallback, localeFor(viewer)));
}

void gearLines(ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer) {
    auto locale = localeFor(viewer);
    if (isBroken(stack)) lines.push_back("§7" + t(viewer, "travellers_gear.broken", " (Broken)"));
    auto abilities = abilityModifiers(stack);
    for (auto const& id : abilities)
        lines.push_back("§6" + core::formatMessageArgs(t(viewer, "travellers_gear.ability", "Ability: %s"), "§7" + modifierName(id, locale) + "§6"));
    auto inserted = insertableModifiers(stack);
    for (auto const& id : inserted) lines.push_back("§f- §7" + modifierName(id, locale));
    int slots = modifierSlots(items::idOf(stack));
    for (int i = static_cast<int>(inserted.size()); i < slots; ++i) lines.push_back("§f- §8" + t(viewer, "travellers_gear.modifier.empty", "Empty"));
    if (items::idOf(stack) == ids::kGloves) lines.push_back("§7" + t(viewer, "item.twilightforest.travellers_gloves.desc", "Cosmetic"));
    // the tooltip images: the belt's container, the goggles' display
    if (hasContainer(stack)) {
        auto content = container(stack);
        for (auto const& s : content)
            if (!items::isEmpty(s)) lines.push_back("§7  " + items::displayName(s) + (items::countOf(s) > 1 ? " x" + std::to_string(items::countOf(s)) : ""));
    }
    if (auto d = displayOf(stack)) {
        for (int i = 0; i < core::tgear::kDisplaySlots; ++i)
            if (!d->items[i].is_null()) lines.push_back("§7  " + items::displayName(items::fromJson(d->items[i])) + (i == d->chosenMapSlot ? " §e*" : ""));
    }
    bool described = false;
    for (auto const& list : {abilities, inserted})
        for (auto const& id : list)
            if (auto m = modifier(id); m && m->hasDescription) described = true;
    if (described)
        lines.push_back("§f" + core::formatMessageArgs(t(viewer, "travellers_gear.shift_info", "Hold %s for info"), "§eShift§f"));
}

class TgearTooltips {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        for (auto const* id : kGearItems) gf::item::addTooltip(id, &gearLines);
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::feature::tgear

GF_REGISTER_MODULE(gf::feature::tgear::TgearTooltips);
GF_REGISTER_CONFIG("tgear", gf::feature::tgear::TgearConfig);

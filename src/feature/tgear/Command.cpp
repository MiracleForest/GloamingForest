// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/command/TravellersGearCommand.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /twilightforest travellers_gear add_modifier|remove_modifier <modifier> (permission 2): the checks
// in TF's order (the registry key first, then validate(): a player, Travellers' Gear in the main
// hand, not an ability; then the add / remove checks). The modifier name is rendered once on the
// server in the server's language with TF's default key names (docs/units/tgear.md: TF's dedicated
// server fails with NoClassDefFoundError for the three names with a key placeholder; GloamingForest
// puts in the default binding). The ten commands.tffeature.* keys are F6's: tr with TF's English.
#include "feature/tgear/Command.h"

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::tgear {

namespace items = game::items;

CommandOutcome runCommand(Player* player, bool add, std::string const& typed) {
    // ResourceKeyArgument: a location without a namespace is minecraft's
    std::string id = typed.find(':') == std::string::npos ? "minecraft:" + typed : typed;
    if (!modifierExists(id)) return {false, tr("commands.tffeature.invalid_modifier", "%s is not a valid Traveller's Gear Modifier", id)};
    if (!player) return {false, tr("commands.tffeature.not_player", "This command must be run by a valid player!")};
    ItemStack stack = player->getSelectedItem();
    if (items::isEmpty(stack) || !isGear(items::idOf(stack)))
        return {false, tr("commands.tffeature.not_travellers_gear", "You are not holding a piece of Traveller's Gear")};
    auto m = modifier(id);
    if (!m || m->ability) return {false, tr("commands.tffeature.ability_modifier", "Abilities cannot be added or removed from Traveller's Gear")};
    auto name = modifierName(id);
    if (add) {
        if (static_cast<int>(insertableModifiers(stack).size()) >= modifierSlots(items::idOf(stack)))
            return {false, tr("commands.tffeature.too_many_modifiers", "This piece of Traveller's Gear already has the maximum amount of modifiers")};
        if (hasModifier(stack, id)) return {false, tr("commands.tffeature.has_modifier", "This piece of Traveller's Gear already has %s", name)};
        if (!groupCovers(m->group, armorSlotOf(items::idOf(stack))))
            return {false, tr("commands.tffeature.wrong_modifier_slot", "%s is not allowed on this piece of Traveller's Gear", name)};
        if (!addModifier(stack, id)) return {false, tr("commands.tffeature.wrong_modifier_slot", "%s is not allowed on this piece of Traveller's Gear", name)};
    } else {
        if (!hasModifier(stack, id)) return {false, tr("commands.tffeature.no_modifier", "%s is not applied to this piece of Traveller's Gear", name)};
        removeModifier(stack, id, nullptr); // the stored items are deleted
    }
    player->setSelectedItem(stack);
    player->sendInventory(false);
    auto stackName = items::displayName(stack);
    auto text      = add ? tr("commands.tffeature.added_modifier", "Added %s to %s!", name, stackName)
                         : tr("commands.tffeature.removed_modifier", "Removed %s from %s!", name, stackName);
    log().info("tgear: {} {} {}", player->getRealName(), add ? "added" : "removed", id);
    return {true, text};
}

} // namespace gf::feature::tgear

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    namespace tg = gf::feature::tgear;
    for (bool add : {true, false}) {
        cmd.overload<tg::TravellersGearParams>()
            .text("travellers_gear")
            .text(add ? "add_modifier" : "remove_modifier")
            .required("modifier")
            .execute([add](CommandOrigin const& origin, CommandOutput& output, tg::TravellersGearParams const& p) {
                if (!gf::commands::isAdmin(origin)) return output.error("You need operator permission.");
                auto r = tg::runCommand(gf::commands::playerOf(origin), add, p.modifier);
                if (r.ok) output.success(r.message);
                else output.error(r.message);
            });
    }
});

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/SkullChestBlock.java
//   src/main/java/twilightforest/block/entity/SkullChestBlockEntity.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): twilightforest:skull_chest. The 45-slot container itself (storage, screen, open /
// close sounds at the first / last opener, comparator, contents spilling) is F10's data-declared
// chest-like container (assets/data/containers/lichdecor.json, 5 rows). This file adds:
//   - the owner lock (SkullChestBlockEntity#canOpen): only the owner or permission level 3 opens
//     it; the others hear block.twilightforest.skull_chest.locked (0.5, 0.5) and see the red action
//     bar message "block.twilightforest.skull_chest.locked" with the owner's name;
//   - a chest filled by a solid block (multilogged obsidian / stone / basalt) does not open;
//   - SkullChestBlock#playerWillDestroy: survival breaks drop the chest (named after the owner) and
//     the logged solid block;
//   - lava-logged next to water turns obsidian-logged with the fizz (reactWithNeighbors).
//
// The owner lives in BlockData "lichdecor"."owner" = {"name", "uuid"} (U9's keepsake casket writes
// it when it places the chest; selftests too).

#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/lichdecor/Lichdecor.h"
#include "feature/lichdecor/TestAccess.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Module.h"

namespace gf::feature::lichdecor {

namespace {

namespace bd    = gf::blockdata;
namespace ia    = gf::interactions;
namespace items = gf::game::items;

constexpr char kChest[] = "twilightforest:skull_chest";

Json ownerOf(BlockSource& region, BlockPos const& pos) {
    if (auto const* d = bd::get(region, pos, kDataKey); d && d->is_object() && d->contains("owner")) return (*d)["owner"];
    return nullptr;
}

/// SkullChestBlockEntity#canOpen.
bool canOpen(Player& player, Json const& owner) {
    if (!owner.is_object()) return true;
    if (static_cast<int>(player.getCommandPermissionLevel()) >= 3) return true;
    auto uuid = owner.value("uuid", std::string());
    if (!uuid.empty()) return player.getUuid().asString() == uuid;
    return player.getRealName() == owner.value("name", std::string());
}

void actionBar(Player& player, std::string const& text) {
    auto                           pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(text);
    pkt.mBody = message;
    pkt.sendTo(player);
}

/// SkullChestBlockEntity#displayLockedInfo.
void displayLockedInfo(Player& player, Json const& owner) {
    gf::fx::soundTo(player, "block.twilightforest.skull_chest.locked", 0.5F, 0.5F);
    actionBar(player, std::string("\xC2\xA7") + "c" + trFor(player, "block.twilightforest.skull_chest.locked", "This chest is locked by %s", owner.value("name", std::string())));
}

bool solidLogged(Block const& block) {
    auto m = state(block, "multilogged");
    return m == "obsidian" || m == "stone" || m == "basalt";
}

std::string loggedBlockItem(Block const& block) {
    auto m = state(block, "multilogged");
    if (m == "obsidian" || m == "stone" || m == "basalt") return "minecraft:" + m;
    return {};
}

class SkullChestModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        // before F10's declared container opens it (priority Normal)
        ia::onUseOn({.block = kChest}, [](ia::UseOnContext const& c) {
            bool emptyHands = c.itemId.empty();
            if (c.sneaking && !emptyHands) return ia::Result::Pass;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            BlockPos pos = game::blockPos(c.pos);
            if (solidLogged(c.block)) return ia::Result::Deny; // filled by a block: no access
            Json owner = ownerOf(*region, pos);
            if (canOpen(c.player, owner)) return ia::Result::Pass;
            if (c.firstEvent) displayLockedInfo(c.player, owner);
            return ia::Result::Deny;
        }, ia::priority::High);
        // playerWillDestroy: the chest (named after the owner) and the logged solid block
        ia::onDestroy({.block = kChest}, [](ia::DestroyContext const& c) {
            if (c.player.isCreative() || !doBlockDrops()) return ia::Result::Pass;
            std::string logged = loggedBlockItem(c.block);
            // the chest has no custom name on Bedrock (TF names only the item entity of a
            // custom-named chest): the plain item
            ia::nextTick([gp = c.pos, logged] {
                auto* r = game::regionOf(gp.dim);
                if (!r || game::blockIdAt(*r, game::blockPos(gp)) == kChest) return; // not broken
                if (auto stack = items::make(kChest, 1)) popResource(*r, game::blockPos(gp), *stack);
                if (!logged.empty())
                    if (auto s = items::make(logged, 1)) popResource(*r, game::blockPos(gp), *s);
            });
            return ia::Result::Pass;
        }, ia::priority::Low);
        // reactWithNeighbors: lava-logged beside water becomes obsidian-logged
        gf::blockevents::onNeighborChanged(kChest, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
            auto const& block = region.getBlock(pos);
            if (state(block, "multilogged") != "lava") return;
            for (int f = 1; f < 6; ++f) {
                auto id = game::blockIdAt(region, relative(pos, f));
                if (id == "minecraft:water" || id == "minecraft:flowing_water") {
                    setStates(region, pos, block, Json{{"multilogged", "obsidian"}});
                    playSound(region, Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, "minecraft:block.lava.extinguish", 0.5F, 2.6F);
                    return;
                }
            }
        });
    }
    void uninstall() {}
};

} // namespace

/// Selftest / U9 access: sets (or with null clears) the owner of the skull chest at `pos`.
void setSkullChestOwner(BlockSource& region, BlockPos const& pos, Json const& owner) {
    Json doc = Json::object();
    if (auto const* d = bd::get(region, pos, kDataKey); d && d->is_object()) doc = *d;
    if (owner.is_null()) doc.erase("owner");
    else doc["owner"] = owner;
    bd::set(region, pos, kDataKey, doc.empty() ? Json(nullptr) : doc);
}

} // namespace gf::feature::lichdecor

GF_REGISTER_MODULE(gf::feature::lichdecor::SkullChestModule);

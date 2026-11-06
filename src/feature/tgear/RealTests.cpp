// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Real-path selftests of U11 (prefix "tgear.real_", testPos dx 1190..1199): the client e2e presses TF's
// keys between "arm" (dresses every real player in Travellers' Gear: goggles with item display holding a
// compass, a clock and a moon dial, red thread vision; a vest; a belt; red thread on a wall next to the
// player) and "check" (reads what the keys did: the belt's storage swapped with the hotbar). On a server
// without real players (the 'full' run) arm and check do nothing.
#include <string>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

namespace tg    = gf::feature::tgear;
namespace items = gf::game::items;
using gf::selftest::Context;
using Slot = SharedTypes::Legacy::ArmorSlot;

template <class Fn>
int forRealPlayers(Fn&& fn) {
    int  n     = 0;
    auto level = ll::service::getLevel();
    if (!level) return 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer() || p.isClientSide()) return true;
        fn(p);
        ++n;
        return true;
    });
    return n;
}

ItemStack make(std::string const& id, int count = 1) {
    auto s = items::make(id, count);
    return s ? *s : ItemStack::EMPTY_ITEM();
}

} // namespace

GF_REGISTER_SELFTEST("tgear.real_arm", [](Context& ctx) {
    int n = forRealPlayers([&](Player& p) {
        auto goggles = make(tg::ids::kGoggles);
        tg::addModifier(goggles, tg::ids::kItemDisplay);
        tg::addModifier(goggles, tg::ids::kRedThreadVision);
        // a real map: the filled map the player made (the client e2e uses an empty map first), else a
        // plain one
        auto&     inv0 = p.getInventory();
        ItemStack map  = make("minecraft:filled_map");
        for (int i = 0; i < inv0.getContainerSize(); ++i) {
            auto const& s = inv0.getItem(i);
            if (items::idOf(s) == "minecraft:filled_map" && tg::mapIdOf(tg::displayJsonOf(s))) {
                map = s;
                inv0.setItem(i, ItemStack::EMPTY_ITEM());
                gf::log().info("tgear: storing {}'s map {}", p.getRealName(), *tg::mapIdOf(tg::displayJsonOf(s)));
                break;
            }
        }
        for (auto s : {map, make("minecraft:compass"), make("minecraft:clock"), make("twilightforest:moon_dial")}) {
            ItemStack replaced;
            if (!items::isEmpty(s)) tg::displayInsert(goggles, s, replaced);
        }
        p.setArmor(Slot::Head, goggles);
        auto vest = make(tg::ids::kVest);
        tg::setGloves(vest, true);
        tg::addModifier(vest, tg::ids::kHaste);
        p.setArmor(Slot::Torso, vest);
        // the wings with swap_hotbar (TravellersWingsModel draws the belt on them)
        auto wings = make(tg::ids::kWings);
        tg::addModifier(wings, tg::ids::kSwapHotbar);
        p.setArmor(Slot::Legs, wings);
        auto boots = make(tg::ids::kBoots);
        tg::addModifier(boots, tg::ids::kAutoRepair);
        p.setArmor(Slot::Feet, boots);
        auto& inv = p.getInventory();
        for (int i = 0; i < 9; ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
        inv.setItem(0, make("minecraft:stone", 16));
        inv.setItem(1, make("minecraft:torch", 8));
        inv.setItem(2, make(tg::ids::kRedThread, 32));
        // a broken vest with modifiers (its icon: travellers_vest_broken and the broken overlays)
        auto broken = make(tg::ids::kVest);
        tg::addModifier(broken, tg::ids::kStealth);
        tg::setGloves(broken, true);
        broken.setDamageValue(static_cast<short>(tg::maxDamageOf(broken) - 1));
        inv.setItem(3, broken);
        // the red thread's size models (count 1 / 4 / 16 next to the 32 above: red_thread, bundle_0..2)
        inv.setItem(4, make(tg::ids::kRedThread, 1));
        inv.setItem(5, make(tg::ids::kRedThread, 4));
        inv.setItem(6, make(tg::ids::kRedThread, 16));
        // a whole piece with three modifier overlays
        auto goggles2 = make(tg::ids::kGoggles);
        for (auto const* m : {tg::ids::kItemDisplay, tg::ids::kAllNight, tg::ids::kAutoRepair}) tg::addModifier(goggles2, m);
        inv.setItem(7, goggles2);
        p.sendInventory(false);
        gf::log().info("tgear: armed {}", p.getRealName());
    });
    ctx.info("real players armed: " + std::to_string(n));
});

GF_REGISTER_SELFTEST("tgear.real_check", [](Context& ctx) {
    forRealPlayers([&](Player& p) {
        auto stored = tg::container(p.getArmor(Slot::Legs));
        bool swapped = items::idOf(stored[0]) == "minecraft:stone" && items::idOf(stored[1]) == "minecraft:torch";
        ctx.check(swapped, "the swap key moved the hotbar into the belt");
        auto d = tg::displayOf(p.getArmor(Slot::Head));
        ctx.check(d.has_value(), "the goggles keep their display");
        gf::log().info("tgear: checked {}: swapped {}", p.getRealName(), swapped);
    });
});

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/item/MoonDialItem.java
//   src/main/java/gloamingforest/item/PocketWatchItem.java
//   src/main/java/gloamingforest/item/MysticCrownItem.java
//   src/main/java/gloamingforest/events/ToolEvents.java (preventFatigueWithPocketWatch)
//   src/main/java/gloamingforest/events/MiscEvents.java (washOffCloth)
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java (the cloth tooltip)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The moon dial's phase tooltip, the pocket watch's hidden Speed / Jump Boost (hotbar or off-hand)
// and Haste (held), and its Mining Fatigue immunity while held (Bedrock has no "effect applicable"
// event: an instance that reached the holder is removed on the next tick), the emperor's cloth
// tooltip and washing. The mystic crown is a head-slot wearable (BP minecraft:wearable, +2 armour);
// its scepter and shield bonuses are read where they apply.
#include <algorithm>
#include <chrono>
#include <string>
#include <ctime>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/common/Interactions.h"
#include "feature/dimension/World.h"
#include "feature/item/Item.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace ia    = gf::feature::common::interactions;
namespace items = game::items;

namespace {

void hiddenEffect(Actor& a, char const* name, int ticks) {
    auto const* e = MobEffect::getByName(name);
    if (!e) return;
    MobEffectInstance inst(e->mId, EffectDuration{ticks});
    inst.mAmbient       = false;
    inst.mEffectVisible = false;
    a.addEffect(inst);
}

bool aprilFools() {
    auto t  = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm.tm_mon == 3 && tm.tm_mday == 1;
}

std::string text(Player const* viewer, std::string const& key, std::string_view fallback) { return viewer ? trFor(*viewer, key, fallback) : tr(key, fallback); }

void clothTooltip(ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer) {
    if (!hasCloth(stack)) return;
    lines.insert(lines.begin(), "§7" + text(viewer, "item.gloamingforest.emperors_cloth.desc", "Shrouded"));
}

} // namespace

std::string moonPhaseKeyFor(Actor const* viewer) {
    auto level = ll::service::getLevel();
    bool natural = false;
    if (viewer) {
        int dim = dimOf(*viewer);
        natural = dim == 0 || gf::world::isGloaming(viewer->getDimension());
    }
    std::int64_t day = level ? static_cast<std::int64_t>(level->getTime()) : 0;
    return core::artifacts::moonPhaseKey(natural && level, day, aprilFools());
}

bool hasCloth(ItemStack const& stack) { return !items::getData(stack, kDataCloth).is_null() || !items::getData(stack, kDataClothLegacy).is_null(); }

void registerMisc() {
    static char const* const kPhases[] = {"Full Moon", "Waning Gibbous", "Third Quarter", "Waning Crescent", "New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous"};
    gf::item::addTooltip(kMoonDial, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
        auto        key = moonPhaseKeyFor(viewer);
        std::string fallback = key == "unknown" ? "Moon phase indeterminate" : key == "unknown_fools" ? "404 moon not found" : kPhases[std::stoi(key) % 8];
        lines.push_back("§7" + text(viewer, "item.gloamingforest.moon_dial.phase_" + key, fallback));
    });
    gf::item::registerItem(kPocketWatch, {
        .inventoryTick = [](ItemStack&, Player& player, int slot, bool) {
            if ((slot >= 0 && slot <= 8) || slot == 40) {
                hiddenEffect(player, "speed", 5);
                hiddenEffect(player, "jump_boost", 5);
            }
            if (holding(player, kPocketWatch)) {
                hiddenEffect(player, "haste", 5);
                // ToolEvents#preventFatigueWithPocketWatch
                if (auto const* f = MobEffect::getByName("mining_fatigue"); f && player.getEffect(f->mId)) player.removeEffect(static_cast<int>(f->mId));
            }
        },
    });
    gf::item::addTooltip(kPocketWatch, [](ItemStack const&, std::vector<std::string>& lines, Player const* viewer) {
        lines.push_back("§7" + text(viewer, "item.gloamingforest.pocket_watch.desc", "It seems to always be running late"));
    });
    // the cloth tooltip on everything it can go on
    auto applicable = gf::tags::members(gf::tags::Registry::Item, "gloamingforest:emperors_cloth_applicable");
    for (char const* v : {"minecraft:leather_helmet", "minecraft:leather_chestplate", "minecraft:leather_leggings", "minecraft:leather_boots", "minecraft:chainmail_helmet",
                          "minecraft:chainmail_chestplate", "minecraft:chainmail_leggings", "minecraft:chainmail_boots", "minecraft:iron_helmet", "minecraft:iron_chestplate",
                          "minecraft:iron_leggings", "minecraft:iron_boots", "minecraft:golden_helmet", "minecraft:golden_chestplate", "minecraft:golden_leggings",
                          "minecraft:golden_boots", "minecraft:diamond_helmet", "minecraft:diamond_chestplate", "minecraft:diamond_leggings", "minecraft:diamond_boots",
                          "minecraft:netherite_helmet", "minecraft:netherite_chestplate", "minecraft:netherite_leggings", "minecraft:netherite_boots",
                          "minecraft:turtle_helmet", "minecraft:elytra"})
        if (std::find(applicable.begin(), applicable.end(), v) == applicable.end()) applicable.push_back(v);
    for (auto const& id : applicable) gf::item::addTooltip(id, clothTooltip);
    // MiscEvents#washOffCloth: a water cauldron washes the cloth off
    ia::onUseOn({.block = "minecraft:cauldron"}, [](ia::UseOnContext const& c) {
        if (items::isEmpty(c.item) || !hasCloth(c.item)) return ia::Result::Pass;
        auto states = game::statesOf(c.block);
        int  level  = game::intState(c.block, "fill_level").value_or(0);
        if (level <= 0 || (states.contains("cauldron_liquid") && states["cauldron_liquid"] != "water")) return ia::Result::Pass;
        if (!c.firstEvent) return ia::Result::Consume;
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        // LayeredCauldronBlock#lowerFillLevel: one Java level (two of Bedrock's six)
        int  next = std::max(0, level - 2);
        Json j{{"fill_level", next}, {"cauldron_liquid", "water"}};
        if (auto const* b = game::blockWithStates("minecraft:cauldron", j)) game::setBlock(*region, game::blockPos(c.pos), *b);
        auto& held = ia::mutableItem(c.item);
        items::setData(held, kDataCloth, nullptr);
        items::setData(held, kDataClothLegacy, nullptr);
        c.player.sendInventory(false);
        gf::stats::award(c.player, "minecraft:clean_armor");
        log().info("artifacts: {} washed the emperor's cloth off {}", c.player.getRealName(), c.itemId);
        return ia::Result::Consume;
    }, ia::priority::High);
}

} // namespace gf::feature::artifacts


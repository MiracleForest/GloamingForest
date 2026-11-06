// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/OreMeterItem.java
//   src/main/java/twilightforest/components/item/OreScannerComponent.java
//   src/main/java/twilightforest/components/item/OreScannerData.java
//   src/main/java/twilightforest/network/WipeOreMeterPacket.java
//   src/main/java/twilightforest/events/EntityEvents.java (ore meter wipe)
//   src/main/java/twilightforest/client/event/OverlayHandler.java (the stats, vanilla fallback)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The ore meter: use starts a scan of the chunks within its range (0..2) over 50 + 25 * range
// ticks (the scan walks the column volume in equal steps per tick, counting every block); the
// result keeps the #c:ores counts (or only the filter block's). Sneak-use in the air cycles the
// range, sneak-use on a #twilightforest:ore_meter_targetable block sets the filter; a swing at
// nothing with data or a filter wipes both. Item data: ore_scanner (the scan in progress),
// ore_loading, ore_data, ore_filter, ore_range (TF's components). The client mod draws TF's
// overlay from the held stack; vanilla players get "Loading..." on the action bar and the results
// as chat lines.
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerSwingEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/item/Item.h"
#include "feature/net/Net.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace ia    = gf::feature::common::interactions;
namespace items = game::items;
namespace ca    = core::artifacts;

namespace {

int rangeOf(ItemStack const& s) {
    auto j = items::getData(s, kDataOreRange);
    return j.is_number_integer() ? std::max(0, j.get<int>()) : 1;
}

bool loading(ItemStack const& s) { return !items::getData(s, kDataOreLoading).is_null(); }

Json scanToJson(ca::OreScan const& s) {
    return Json{{"origin", {s.originX, 0, s.originZ}}, {"span_x", s.xSpan}, {"span_z", s.zSpan}, {"duration", s.duration}, {"counts", s.counts}, {"progression", s.progress}};
}

ca::OreScan scanFromJson(Json const& j) {
    ca::OreScan s;
    if (!j.is_object()) return s;
    auto o     = j.value("origin", Json::array({0, 0, 0}));
    s.originX  = o.is_array() && o.size() == 3 ? o[0].get<int>() : 0;
    s.originZ  = o.is_array() && o.size() == 3 ? o[2].get<int>() : 0;
    s.xSpan    = j.value("span_x", 0);
    s.zSpan    = j.value("span_z", 0);
    s.duration = j.value("duration", 0);
    s.progress = j.value("progression", 0);
    if (auto c = j.find("counts"); c != j.end() && c->is_object())
        for (auto const& [k, v] : c->items()) s.counts[k] = v.get<int>();
    return s;
}

/// The block id of a block (cached per permutation: thousands per tick).
std::string const& idOfBlock(Block const& b) {
    static std::unordered_map<Block const*, std::string> cache;
    auto it = cache.find(&b);
    if (it != cache.end()) return it->second;
    return cache.emplace(&b, game::blockId(b)).first->second;
}

std::string blockName(std::string const& id) {
    if (auto s = items::make(id)) return items::displayName(*s);
    auto colon = id.find(':');
    return colon == std::string::npos ? id : id.substr(colon + 1);
}

/// Vanilla clients: the results as chat lines (TF's overlay rows, unprettified).
void sendResults(Player& player, Json const& data, int range) {
    if (net::isCompanion(player)) return;
    auto chunk = data.value("chunk", Json::array({0, 0}));
    int  total = data.value("total", 0);
    player.sendMessage(trFor(player, "misc.twilightforest.ore_meter_range", "Radius: %s, Origin: [%s, %s]", std::to_string(range), chunk[0].dump(), chunk[1].dump()));
    player.sendMessage(trFor(player, "misc.twilightforest.ore_meter_total", "Total blocks scanned: %s", std::to_string(total)));
    std::vector<std::pair<std::string, int>> rows;
    for (auto const& [k, v] : data.value("counts", Json::object()).items()) rows.emplace_back(k, v.get<int>());
    std::stable_sort(rows.begin(), rows.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    if (rows.empty()) player.sendMessage(trFor(player, "misc.twilightforest.ore_meter_no_blocks", "No blocks found nearby"));
    auto names = data.value("names", Json::object());
    for (auto const& [id, n] : rows) {
        std::string name = names.value(id, blockName(id));
        player.sendMessage(name + " " + trFor(player, "misc.twilightforest.ore_meter_separator", "-") + " " + std::to_string(n) + " "
                           + trFor(player, "misc.twilightforest.ore_meter_ratio", "(%s%%)", ca::oreRatio(n, total)));
    }
}

void wipe(Player& player, ItemStack& stack) {
    if (items::getData(stack, kDataOreData).is_null() && items::getData(stack, kDataOreFilter).is_null()) return;
    items::setData(stack, kDataOreData, nullptr);
    items::setData(stack, kDataOreFilter, nullptr);
    auto& r = levelRandom();
    playSound(player, "item.twilightforest.ore_meter.clear", 1.25F, 0.6F + r.nextFloat() * 0.2F);
    log().info("artifacts: {} wiped an ore meter", player.getRealName());
}

/// Wipes the held ore meter (WipeOreMeterPacket: the hand holding it).
void wipeHeld(Player& player) {
    if (items::idOf(mainHand(player)) == kOreMeter) {
        ItemStack s = mainHand(player);
        wipe(player, s);
        player.setSelectedItem(s);
    } else if (items::idOf(offHand(player)) == kOreMeter) {
        ItemStack s = offHand(player);
        wipe(player, s);
        player.setOffhandSlot(s);
    } else {
        return;
    }
    player.sendInventory(false);
}

/// The player's block ray (fluids included): true when it hits a block.
bool looksAtBlock(Player& player, bool fluids) {
    Vec3   eye   = player.getEyePos();
    Vec3   look  = viewVector(player);
    double reach = player.isCreative() ? 5.0 : 4.5;
    Vec3   to{static_cast<float>(eye.x + look.x * reach), static_cast<float>(eye.y + look.y * reach), static_cast<float>(eye.z + look.z * reach)};
    HitResult hr = player.getDimensionBlockSource().clip(eye, to, fluids, ShapeType::Outline, 200, false, false, nullptr,
                                                         [](BlockSource const&, Block const&, bool) { return true; }, false);
    return hr.mType == HitResultType::Tile;
}

} // namespace

void beginOreScan(Player& player, ItemStack& stack) {
    int  range = rangeOf(stack);
    auto feet  = player.getFeetPos();
    auto scan  = ca::OreScan::fromCenter(static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.z)), range, ca::oreScanTicks(range));
    items::setData(stack, kDataOreScanner, scanToJson(scan));
    auto& r = levelRandom();
    playSound(player, "item.twilightforest.ore_meter.crackle", 0.5F, r.nextFloat() * 0.1F + 0.9F);
    log().info("artifacts: {} started an ore scan (range {})", player.getRealName(), range);
}

void tickOreMeter(ItemStack& stack, Player& player) {
    auto j = items::getData(stack, kDataOreScanner);
    if (j.is_null()) return;
    auto scan = scanFromJson(j);
    if (scan.isEmpty()) {
        items::setData(stack, kDataOreScanner, nullptr);
        return;
    }
    auto& region = player.getDimensionBlockSource();
    int   minY   = region.getMinHeight();
    // the volume: Java's volume is area * getMaxBuildHeight (the top y, 320 in the overworld), walked up from
    // the bottom: TF's scan stops short of the top
    int maxY = std::max(1, region.getMaxHeight());
    scan.tick(minY, maxY, [&](int x, int y, int z) { return idOfBlock(region.getBlock(BlockPos{x, y, z})); });
    if (!scan.finished()) {
        items::setData(stack, kDataOreLoading, scan.progress);
        items::setData(stack, kDataOreScanner, scanToJson(scan));
        if (!net::isCompanion(player)) {
            std::string dots(static_cast<std::size_t>((scan.progress / 5) % 3 + 1), '.');
            actionBar(player, trFor(player, "misc.twilightforest.ore_meter_loading", "Loading") + dots);
        }
        return;
    }
    // done: the ores (or the filter block) with the scanned volume
    auto filter = items::getData(stack, kDataOreFilter);
    Json counts = Json::object(), names = Json::object();
    std::map<std::string, int> result;
    if (filter.is_string()) {
        auto f    = filter.get<std::string>();
        result[f] = scan.counts.contains(f) ? scan.counts[f] : 0;
    } else {
        for (auto const& [id, n] : scan.counts)
            if (n > 0 && gf::tags::has(gf::tags::Registry::Block, "c:ores", id)) result[id] = n;
    }
    for (auto const& [id, n] : result) {
        counts[id] = n;
        names[id]  = blockName(id);
    }
    auto [cx, cz] = scan.centerChunk();
    int  range    = rangeOf(stack);
    int  total    = scan.volume(maxY);
    Json data{{"counts", counts}, {"names", names}, {"chunk", {cx, cz}}, {"total", total}, {"range", range}, {"id", ca::oreUniversalId(result, cx, cz, range, total)}};
    items::setData(stack, kDataOreData, data);
    items::setData(stack, kDataOreLoading, nullptr);
    items::setData(stack, kDataOreScanner, nullptr);
    log().info("artifacts: {}'s ore scan finished ({} kind(s) found)", player.getRealName(), result.size());
    sendResults(player, data, range);
}

void registerOreMeter() {
    gf::item::registerItem(kOreMeter, {
        .use = [](Player& player, ItemStack& stack) {
            if (player.isClientSide()) return gf::item::Result::Fail;
            if (loading(stack)) return gf::item::Result::Pass;
            if (!player.isSneaking()) {
                beginOreScan(player, stack);
                return gf::item::Result::Pass;
            }
            // toggleRange: only when the ray hits nothing
            if (looksAtBlock(player, true)) return gf::item::Result::Pass;
            int next = (rangeOf(stack) + 1) % 3;
            items::setData(stack, kDataOreRange, next);
            actionBar(player, trFor(player, "misc.twilightforest.ore_meter_new_range", "Range set to %s chunks", std::to_string(next)));
            playSound(player, "minecraft:ui.button.click", 0.25F, 0.75F + next * 0.1F);
            return gf::item::Result::Success;
        },
        .inventoryTick = [](ItemStack& stack, Player& player, int, bool) { tickOreMeter(stack, player); },
    });
    // OreMeterItem#useOn: sneaking on a targetable block sets the filter
    ia::onUseOn({.item = kOreMeter}, [](ia::UseOnContext const& c) {
        if (!c.sneaking) return ia::Result::Pass;
        if (!gf::tags::has(gf::tags::Registry::Block, "twilightforest:ore_meter_targetable", c.blockId)) return ia::Result::Pass;
        if (!c.firstEvent) return ia::Result::Consume;
        auto& held = ia::mutableItem(c.item);
        items::setData(held, kDataOreFilter, c.blockId);
        actionBar(c.player, trFor(c.player, "misc.twilightforest.ore_meter_set_block", "Targeted block set to %s", blockName(c.blockId)));
        auto& r = levelRandom();
        playSound(c.player, "item.twilightforest.ore_meter.target_block", 0.5F, r.nextFloat() * 0.1F + 0.9F);
        c.player.sendInventory(false);
        return ia::Result::Consume;
    });
    gf::item::addTooltip(kOreMeter, [](ItemStack const& stack, std::vector<std::string>& lines, Player const* viewer) {
        auto f = items::getData(stack, kDataOreFilter);
        if (!f.is_string()) return;
        auto name = blockName(f.get<std::string>());
        lines.push_back("§7" + (viewer ? trFor(*viewer, "misc.twilightforest.ore_meter_targeted_block", "Targeted block: %s", name)
                                       : tr("misc.twilightforest.ore_meter_targeted_block", "Targeted block: %s", name)));
    });
    // WipeOreMeterPacket (a companion's left click at nothing) and the swing at nothing of any client
    net::on(kChanWipeOre, [](Player& player, Json const&) { wipeHeld(player); });
    static ll::event::ListenerPtr swing;
    swing = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerSwingEvent>([](ll::event::PlayerSwingEvent& ev) {
        auto& p = ev.self();
        if (p.isClientSide() || !holding(p, kOreMeter)) return;
        if (looksAtBlock(p, false) || lookTarget(p, p.isCreative() ? 5.0 : 3.0)) return; // Java's LeftClickEmpty only
        wipeHeld(p);
    });
}

} // namespace gf::feature::artifacts

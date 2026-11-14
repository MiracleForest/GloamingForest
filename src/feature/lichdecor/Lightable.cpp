// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/LightableBlock.java
//   src/main/java/gloamingforest/block/AbstractSkullCandleBlock.java (CandleColors)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): what the lightable blocks share (LightableBlock), the drop stash of broken
// blocks, and the module that registers the candelabra, skull candles, ominous candles, ominous
// fire and brazier.
//
// LightableBlock#animateTick / addParticlesAndSound run on the client in Java (random display
// ticks): the ambient flames, smoke and candle sounds are client-side and not emitted here.

#include <map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/common/BlockData.h"
#include "feature/enchant/Enchant.h"
#include "feature/lichdecor/LightablesInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Module.h"

namespace gf::feature::lichdecor::light {

namespace {

/// upstream's CandleColors in value order (0 plain .. 16 brown).
constexpr std::array<std::string_view, 17> kGloamingColors{"",     "white", "light_gray", "gray", "black",  "red",    "orange",  "yellow", "green",
                                                     "lime", "blue",  "cyan",       "light_blue", "purple", "magenta", "pink", "brown"};

struct Stashed {
    std::uint64_t tick = 0;
    Json          data;
};
std::map<GlobalPos, Stashed>& stash() {
    static std::map<GlobalPos, Stashed> m;
    return m;
}

std::uint64_t now() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
}

GlobalPos gpos(BlockSource& region, BlockPos const& pos) { return GlobalPos{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z}; }

} // namespace

std::string candleOfGloamingColor(int value) {
    if (value <= 0 || value > 16) return "minecraft:candle";
    return "minecraft:" + std::string(kGloamingColors[static_cast<std::size_t>(value)]) + "_candle";
}

int gloamingColorOfCandle(std::string_view id) {
    for (int i = 0; i <= 16; ++i)
        if (id == candleOfGloamingColor(i)) return i;
    return -1;
}

bool isFirestarter(std::string const& itemId) { return itemId == "minecraft:flint_and_steel" || itemId == "minecraft:fire_charge"; }

void useFirestarter(Player& player, ItemStack const& held, std::string const& itemId) {
    if (player.isCreative()) return;
    auto& item = ia::mutableItem(held);
    if (item.isNull()) return;
    if (itemId == "minecraft:flint_and_steel") item.hurtAndBreak(1, &player);
    else {
        item.remove(1);
        if (item.mCount <= 0) item = ItemStack::EMPTY_ITEM();
    }
    player.sendInventory(false);
}

bool mayBuild(Player const& player) {
    auto t = player.getPlayerGameType();
    return t != GameType::Adventure && t != GameType::Spectator;
}

void giveCandleBack(Player& player, std::string const& candleId) {
    auto stack = game::items::make(candleId, 1);
    if (!stack) return;
    if (player.isCreative()) {
        // Player#hasInfiniteMaterials: added only when the inventory does not hold one yet
        auto& inv = player.getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i)
            if (game::items::idOf(inv.getItem(i)) == candleId) return;
    }
    giveOrDrop(player, *stack);
}

int silkTouch(ItemStack const* tool) { return tool ? gf::enchant::level(*tool, "minecraft:silk_touch") : 0; }

Json dataAt(BlockSource& region, BlockPos const& pos) {
    auto const* j = gf::blockdata::get(region, pos, kDataKey);
    return j && j->is_object() ? *j : Json::object();
}

void setDataAt(BlockSource& region, BlockPos const& pos, Json const& value) {
    gf::blockdata::set(region, pos, kDataKey, value.is_object() && value.empty() ? Json() : value);
}

Json dataForDrops(BlockSource& region, BlockPos const& pos) {
    auto it = stash().find(gpos(region, pos));
    if (it != stash().end() && it->second.tick + 2 >= now()) return it->second.data;
    return dataAt(region, pos);
}

void stashForDrops(BlockSource& region, BlockPos const& pos) {
    auto& s = stash();
    auto  t = now();
    std::erase_if(s, [&](auto const& e) { return e.second.tick + 2 < t; });
    s[gpos(region, pos)] = Stashed{t, dataAt(region, pos)};
}

Vec3 centre(BlockPos const& pos) { return Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}; }

void extinguishFx(BlockSource& region, BlockPos const& pos, std::vector<Vec3> const& offsets) {
    for (auto const& o : offsets) particle(region, "minecraft:smoke", Vec3{pos.x + o.x, pos.y + o.y, pos.z + o.z}, Vec3{0.0F, 0.025F, 0.0F});
    playSound(region, centre(pos), "minecraft:block.candle.extinguish", 1.0F, 1.0F);
}

void eruptFlames(BlockSource& region, BlockPos const& pos, std::string const& id, std::vector<Vec3> const& offsets) {
    for (auto const& o : offsets)
        for (int j = 0; j < 5; ++j)
            particle(region, id, Vec3{pos.x + o.x, pos.y + o.y, pos.z + o.z},
                     Vec3{(randomFloat() - 0.5F) * 0.05F, 0.015F, (randomFloat() - 0.5F) * 0.05F});
}

std::optional<BlockPos> placementPos(BlockSource& region, BlockPos const& clicked, int face) {
    if (game::replaceable(region, clicked)) return clicked;
    BlockPos next = relative(clicked, face);
    if (game::replaceable(region, next)) return next;
    return std::nullopt;
}

namespace {

class LightablesModule {
public:
    void install() {
        static bool once = false; // the contracts have no removal: register once per process
        if (once) return;
        once = true;
        registerLightable();
        registerCandelabra();
        registerSkullCandles();
        registerOminousCandles();
        registerOminousFire();
        registerBrazier();
    }
    void uninstall() {}
};

} // namespace

void registerLightable() {
    // the data of a broken lightable block survives until its drops rolled (the loot modifiers)
    auto stashIt = [](ia::DestroyContext const& c) {
        if (auto* region = game::regionOf(c.pos.dim)) stashForDrops(*region, game::blockPos(c.pos));
        return ia::Result::Pass;
    };
    ia::onDestroy({.block = kCandelabra}, stashIt, ia::priority::Fallback);
    for (char const* type : {"zombie", "skeleton", "wither_skeleton", "creeper", "player", "piglin"}) {
        ia::onDestroy({.block = std::string("gloamingforest:") + type + "_skull_candle"}, stashIt, ia::priority::Fallback);
        ia::onDestroy({.block = std::string("gloamingforest:") + type + "_wall_skull_candle"}, stashIt, ia::priority::Fallback);
    }
}

} // namespace gf::feature::lichdecor::light

GF_REGISTER_MODULE(gf::feature::lichdecor::light::LightablesModule);

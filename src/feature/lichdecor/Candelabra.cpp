// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/CandelabraBlock.java
//   src/main/java/gloamingforest/block/entity/CandelabraBlockEntity.java
//   src/main/java/gloamingforest/components/item/CandelabraData.java
//   src/main/java/gloamingforest/block/LightableBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the candelabra. The block entity's three candle slots live in BlockData
// ({"candles": [candle block id | null] x 3}); the has_candle_N states mirror them. The item keeps
// them as item data gloamingforest:candelabra_data (a list of up to 3 ids, "minecraft:air" for an
// empty slot, as CandelabraData's ordered list).
//
// Bedrock notes: the block has no waterlogged state (Bedrock's liquid layer stands in): a
// candelabra with water in its liquid layer counts as waterlogged for canBeLit. The DIM
// candelabra's strong power into its support goes through the U22 extension
// (feature/ext/lichdecor/SupportPower.h) and is re-applied every second for the known DIM
// candelabras (the circuit graph forgets it when a chunk unloads). The animateTick flames are
// client-side (not emitted here).

#include <algorithm>
#include <cmath>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"

#include "feature/common/Redstone.h"
#include "feature/ext/lichdecor/SupportPower.h"
#include "feature/families/BlockEvents.h"
#include "feature/item/Item.h"
#include "feature/lichdecor/LightablesInternal.h"
#include "feature/loot/Loot.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"

namespace gf::feature::lichdecor::light {

namespace {

namespace ext = gf::feature::ext::lichdecor;

constexpr char kDataComponent[] = "gloamingforest:candelabra_data";
constexpr char kDimKey[]        = "gf:lichdecor:dim_candelabras";

int facingOf(Block const& b) {
    int f = faceOf(state(b, "facing"));
    return f < 2 ? 2 : f;
}
bool onWall(Block const& b) { return boolState(b, "on_wall"); }
Lighting lightingAt(Block const& b) { return lightingOf(b).value_or(Lighting::None); }

/// CandelabraBlock#getParticleOffsets.
std::vector<Vec3> offsets(Block const& b) {
    int f = facingOf(b);
    if (onWall(b)) {
        switch (f) {
        case 3: return {{0.1875F, 0.9F, 0.75F}, {0.5F, 0.9F, 0.75F}, {0.8125F, 0.9F, 0.75F}};
        case 4: return {{0.25F, 0.9F, 0.1875F}, {0.25F, 0.9F, 0.5F}, {0.25F, 0.9F, 0.8125F}};
        case 5: return {{0.75F, 0.9F, 0.1875F}, {0.75F, 0.9F, 0.5F}, {0.75F, 0.9F, 0.8125F}};
        default: return {{0.1875F, 0.9F, 0.25F}, {0.5F, 0.9F, 0.25F}, {0.8125F, 0.9F, 0.25F}};
        }
    }
    if (f == 4 || f == 5) return {{0.5F, 0.9F, 0.1875F}, {0.5F, 0.9F, 0.5F}, {0.5F, 0.9F, 0.8125F}};
    return {{0.1875F, 0.9F, 0.5F}, {0.5F, 0.9F, 0.5F}, {0.8125F, 0.9F, 0.5F}};
}

/// The offsets of the slots that hold a candle (eruptFlameParticles).
std::vector<Vec3> filledOffsets(Block const& b) {
    auto              all = offsets(b);
    std::vector<Vec3> out;
    for (int i = 0; i < 3; ++i)
        if (boolState(b, "has_candle_" + std::to_string(i + 1))) out.push_back(all[static_cast<std::size_t>(i)]);
    return out;
}

bool waterlogged(BlockSource& region, BlockPos const& pos) {
    auto const& id = region.getExtraBlock(pos).getTypeName();
    return id == "minecraft:water" || id == "minecraft:flowing_water";
}

/// CandelabraBlock#canBeLit.
bool canBeLit(BlockSource& region, BlockPos const& pos, Block const& b) {
    return lightingAt(b) == Lighting::None && !waterlogged(region, pos) && candelabraCandles(b) > 0;
}

/// CandelabraBlock.canSurvive(reader, pos, onWall, facing): the top face centre of the support.
bool canSurvive(BlockSource& region, BlockPos const& pos, bool wall, int facing) {
    BlockPos support = wall ? relative(pos, facing) : relative(pos, 0);
    return region.getBlock(support).canProvideSupport(1, BlockSupportType::Center);
}

/// The three slots of the block entity ("" = empty).
std::array<std::string, 3> slotsAt(BlockSource& region, BlockPos const& pos) {
    std::array<std::string, 3> out;
    auto                       d = dataAt(region, pos);
    auto const&                c = d.value("candles", Json::array());
    for (std::size_t i = 0; i < 3 && i < c.size(); ++i)
        if (c[i].is_string() && c[i].get<std::string>() != "minecraft:air") out[i] = c[i].get<std::string>();
    return out;
}

void writeSlots(BlockSource& region, BlockPos const& pos, std::array<std::string, 3> const& slots) {
    auto d = dataAt(region, pos);
    if (slots[0].empty() && slots[1].empty() && slots[2].empty()) d.erase("candles");
    else {
        Json arr = Json::array();
        for (auto const& s : slots) arr.push_back(s.empty() ? Json() : Json(s));
        d["candles"] = arr;
    }
    setDataAt(region, pos, d);
}

/// CandelabraBlockEntity#updateState: the has_candle states from the slots; no candles left
/// turns the light off.
void updateState(BlockSource& region, BlockPos const& pos) {
    auto const& b     = region.getBlock(pos);
    auto        slots = slotsAt(region, pos);
    Json        ch    = Json::object();
    int         n     = 0;
    for (int i = 0; i < 3; ++i) {
        bool has = !slots[static_cast<std::size_t>(i)].empty();
        n += has ? 1 : 0;
        ch["has_candle_" + std::to_string(i + 1)] = has;
    }
    if (n == 0 && lightingAt(b) != Lighting::None) ch["lighting"] = "none";
    setStates(region, pos, b, ch);
    syncCandelabraPower(region, pos);
}

void setLighting(BlockSource& region, BlockPos const& pos, Lighting l) {
    setStates(region, pos, region.getBlock(pos), Json{{"lighting", lightingName(l)}});
    syncCandelabraPower(region, pos);
}

/// CandelabraBlock#getSlot.
int slotOf(int facing, ia::Vec const& hit) {
    auto n       = faceStep(facing);
    int  dirX    = n[2], dirZ = -n[0]; // up x normal
    bool reverse = facing == 2 || facing == 5;
    double cx    = dirX * hit[0] + dirZ * hit[2];
    if (cx <= 0.0) cx += 1.0;
    if (cx <= 0.375) return reverse ? 2 : 0;
    return cx <= 0.6875 ? 1 : reverse ? 0 : 2;
}

float pickPitch() { return randomFloat() * 0.1F + 0.9F; }

// ---- the DIM candelabras whose power is re-applied --------------------------------------------------

std::set<GlobalPos>& dimSet() {
    static std::set<GlobalPos> s;
    return s;
}

void loadDimSet() {
    dimSet().clear();
    if (auto j = game::db::get(kDimKey); j && j->is_array())
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4) dimSet().insert(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
}

void saveDimSet() {
    Json arr = Json::array();
    for (auto const& p : dimSet()) arr.push_back(Json::array({p.dim, p.x, p.y, p.z}));
    if (arr.empty()) game::db::erase(kDimKey);
    else game::db::put(kDimKey, arr);
}

void resyncDim() {
    std::vector<GlobalPos> drop;
    for (auto const& p : dimSet()) {
        auto* region = game::regionOf(p.dim);
        if (!region) continue;
        BlockPos bp{p.x, p.y, p.z};
        auto     id = game::blockIdAt(*region, bp);
        if (id.empty()) continue; // not loaded
        if (id != kCandelabra) {
            drop.push_back(p);
            continue;
        }
        syncCandelabraPower(*region, bp);
    }
    for (auto const& p : drop) dimSet().erase(p);
}

/// The item's candelabra data as three slot ids.
std::array<std::string, 3> slotsOfItem(ItemStack const& stack) {
    std::array<std::string, 3> out;
    auto                       j = gf::item::getData(stack, kDataComponent);
    if (!j.is_array()) return out;
    for (std::size_t i = 0; i < 3 && i < j.size(); ++i)
        if (j[i].is_string() && j[i].get<std::string>() != "minecraft:air") out[i] = j[i].get<std::string>();
    return out;
}

Json slotsToItemData(std::array<std::string, 3> const& slots) {
    Json arr = Json::array();
    for (auto const& s : slots) arr.push_back(s.empty() ? "minecraft:air" : s);
    return arr;
}

// ---- interactions ------------------------------------------------------------------------------------

/// CandelabraBlock#getStateForPlacement after vanilla placed the default block.
void onPlaced(ia::PlaceContext const& c) {
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return;
    BlockPos pos     = game::blockPos(c.pos);
    int      horiz   = lookFace(c.player);
    auto     nearest = nearestLookingDirections(c.player);
    std::optional<std::pair<bool, int>> chosen; // (onWall, facing)
    auto tryWalls = [&] {
        for (int d : nearest)
            if (d >= 2 && canSurvive(*region, pos, true, d)) return std::optional<std::pair<bool, int>>{{true, d}};
        return std::optional<std::pair<bool, int>>{};
    };
    if (c.face == 1) {
        if (canSurvive(*region, pos, false, horiz)) chosen = {false, horiz};
        else chosen = tryWalls();
    } else {
        chosen = tryWalls();
        if (!chosen && canSurvive(*region, pos, false, horiz)) chosen = {false, horiz};
    }
    if (!chosen) {
        // the placement fails: no block, the item stays with the player
        game::clearBlock(*region, pos);
        if (!c.player.isCreative() && c.itemBefore) giveOrDrop(c.player, game::items::withCount(*c.itemBefore, 1));
        return;
    }
    auto slots = c.itemBefore ? slotsOfItem(*c.itemBefore) : std::array<std::string, 3>{};
    writeSlots(*region, pos, slots);
    setStates(*region, pos, region->getBlock(pos), Json{{"facing", faceName(chosen->second)}, {"on_wall", chosen->first}, {"lighting", "none"}});
    updateState(*region, pos);
}

ia::Result onUse(ia::UseOnContext const& c) {
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    BlockPos     pos = game::blockPos(c.pos);
    Block const& b   = region->getBlock(pos);
    // a sneaking player holding an item skips the block's use (Player#isSecondaryUseActive)
    if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
    bool candleItem  = gloamingColorOfCandle(c.itemId) >= 0;
    Lighting l       = lightingAt(b);
    if (candleItem || c.sneaking) {
        if (!c.firstEvent) return ia::Result::Consume;
        int  i     = slotOf(facingOf(b), c.hit);
        auto slots = slotsAt(*region, pos);
        auto& slot = slots[static_cast<std::size_t>(i)];
        if (!slot.empty() && c.sneaking) {
            std::string removed = slot;
            slot.clear();
            writeSlots(*region, pos, slots);
            updateState(*region, pos);
            playSound(*region, centre(pos), "minecraft:block.candle.place", 1.0F, pickPitch());
            giveCandleBack(c.player, removed);
            return ia::Result::Consume;
        }
        if (slot.empty() && candleItem) {
            slot = c.itemId;
            writeSlots(*region, pos, slots);
            updateState(*region, pos);
            playSound(*region, centre(pos), "minecraft:block.candle.place", 1.0F, pickPitch());
            consumeOne(c.player, c.item);
            return ia::Result::Consume;
        }
        return ia::Result::Consume;
    }
    if (c.itemId == "minecraft:redstone" && l == Lighting::Normal) {
        if (!c.firstEvent) return ia::Result::Consume;
        setLighting(*region, pos, Lighting::Dim);
        consumeOne(c.player, c.item);
        playSound(*region, centre(pos), "block.gloamingforest.candelabra.light", 1.0F, pickPitch());
        eruptFlames(*region, pos, "gloamingforest:dim_flame", filledOffsets(region->getBlock(pos)));
        return ia::Result::Consume;
    }
    bool scepter = !c.itemId.empty() && gf::tags::has(gf::tags::Registry::Item, "gloamingforest:scepters", c.itemId);
    if ((scepter || c.itemId == kEssence) && l == Lighting::Normal) {
        if (!c.firstEvent) return ia::Result::Consume;
        setLighting(*region, pos, Lighting::Ominous);
        playSound(*region, centre(pos), "block.gloamingforest.candelabra.ominous", 1.0F, pickPitch());
        eruptFlames(*region, pos, "gloamingforest:ominous_flame", filledOffsets(region->getBlock(pos)));
        return ia::Result::Consume;
    }
    // LightableBlock#tryLightCandles
    if (c.itemId.empty() && mayBuild(c.player) && l != Lighting::None) {
        if (!c.firstEvent) return ia::Result::Consume;
        auto offs = offsets(b);
        setLighting(*region, pos, Lighting::None);
        extinguishFx(*region, pos, offs);
        return ia::Result::Consume;
    }
    if (canBeLit(*region, pos, b) && isFirestarter(c.itemId)) {
        // FlintAndSteelItem / FireChargeItem#useOn: getToolModifiedState(FIRESTARTER_LIGHT)
        if (!c.firstEvent) return ia::Result::Consume;
        setLighting(*region, pos, Lighting::Normal);
        bool flint = c.itemId == "minecraft:flint_and_steel";
        playSound(*region, centre(pos), flint ? "minecraft:item.flintandsteel.use" : "minecraft:item.firecharge.use", 1.0F,
                  flint ? randomFloat() * 0.4F + 0.8F : (randomFloat() - randomFloat()) * 0.2F + 1.0F);
        useFirestarter(c.player, c.item, c.itemId);
        return ia::Result::Consume;
    }
    return ia::Result::Pass;
}

} // namespace

std::vector<Vec3> candelabraFlameOffsets(Block const& b) { return filledOffsets(b); }

int candelabraCandles(Block const& block) {
    int n = 0;
    for (int i = 1; i <= 3; ++i) n += boolState(block, "has_candle_" + std::to_string(i)) ? 1 : 0;
    return n;
}

void syncCandelabraPower(BlockSource& region, BlockPos const& pos) {
    auto const& b  = region.getBlock(pos);
    GlobalPos   gp{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z};
    if (b.getTypeName() != kCandelabra) {
        ext::setDirectedPower(region, pos, 0, 0);
        dimSet().erase(gp);
        return;
    }
    int n = candelabraCandles(b);
    if (lightingAt(b) == Lighting::Dim && n > 0) {
        ext::setDirectedPower(region, pos, onWall(b) ? facingOf(b) : 0, 3 * n);
        dimSet().insert(gp);
    } else {
        ext::setDirectedPower(region, pos, 0, 0);
        dimSet().erase(gp);
    }
}

void registerCandelabra() {
    ia::onPlace({.block = kCandelabra}, onPlaced);
    ia::onUseOn({.block = kCandelabra}, onUse);

    // LightableBlock#lightCandlesWithProjectile
    gf::blockevents::onProjectileHit(kCandelabra, [](BlockSource& region, BlockPos const& pos, Actor& projectile) {
        if (projectile.isOnFire() && canBeLit(region, pos, region.getBlock(pos))) setLighting(region, pos, Lighting::Normal);
    });
    gf::blockevents::onRemoved(kCandelabra, [](BlockSource& region, BlockPos const& pos, Block const&) { syncCandelabraPower(region, pos); });

    // getSignal / getDirectSignal: DIM only, 3 per candle, into the support (wall: towards FACING;
    // standing: down). `towards` is the side from the candelabra to the neighbour.
    gf::redstone::registerEmitter(kCandelabra, [](BlockSource&, BlockPos const&, Block const& b, int towards) {
        if (lightingAt(b) != Lighting::Dim) return 0;
        int target = onWall(b) ? facingOf(b) : 0;
        return towards == target ? 3 * candelabraCandles(b) : 0;
    });
    // getAnalogOutputSignal
    gf::redstone::registerAnalog(kCandelabra, [](BlockSource&, BlockPos const&, Block const& b) -> std::optional<int> {
        int n = candelabraCandles(b);
        switch (lightingAt(b)) {
        case Lighting::Dim: return 3 + n;
        case Lighting::Ominous: return 6 + n;
        case Lighting::Normal: return 9 + n;
        default: return n;
        }
    });

    // CandelabraBlock#getDrops: the candles drop separately, or under silk touch stay in the item
    gf::loot::addGlobalModifier("gf:lichdecor/candelabra_drops", [](std::vector<ItemStack>& loot, gf::loot::LootContext const& ctx, std::string const& tableId) {
        if (tableId != "gloamingforest:blocks/candelabra" || !ctx.region) return;
        auto base = std::find_if(loot.begin(), loot.end(), [](ItemStack const& s) { return game::items::idOf(s) == kCandelabra; });
        if (base == loot.end()) return;
        BlockPos pos{static_cast<int>(std::floor(ctx.origin.x)), static_cast<int>(std::floor(ctx.origin.y)), static_cast<int>(std::floor(ctx.origin.z))};
        auto     d = dataForDrops(*ctx.region, pos);
        std::array<std::string, 3> slots;
        auto const& c = d.value("candles", Json::array());
        for (std::size_t i = 0; i < 3 && i < c.size(); ++i)
            if (c[i].is_string()) slots[i] = c[i].get<std::string>();
        if (silkTouch(ctx.tool) > 0) {
            auto stack = game::items::make(kCandelabra, 1);
            if (!stack) return;
            if (!slots[0].empty() || !slots[1].empty() || !slots[2].empty()) gf::item::setData(*stack, kDataComponent, slotsToItemData(slots));
            *base = *stack;
        } else {
            for (auto const& s : slots)
                if (!s.empty())
                    if (auto st = game::items::make(s, 1)) loot.push_back(*st);
        }
    });

    game::db::onWorldReady([] { loadDimSet(); });
    game::db::onWorldSave([] { saveDimSet(); });
    game::db::onWorldClosed([] { dimSet().clear(); });
    static ll::event::ListenerPtr tick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) {
        static int counter = 0;
        if (++counter % 20 == 0) resyncDim();
    });
    (void)tick;
}

} // namespace gf::feature::lichdecor::light

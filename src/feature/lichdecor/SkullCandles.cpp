// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/AbstractSkullCandleBlock.java
//   src/main/java/twilightforest/block/SkullCandleBlock.java
//   src/main/java/twilightforest/block/WallSkullCandleBlock.java
//   src/main/java/twilightforest/block/entity/SkullCandleBlockEntity.java
//   src/main/java/twilightforest/components/item/SkullCandles.java
//   src/main/java/twilightforest/item/SkullCandleItem.java
//   src/main/java/twilightforest/events/EntityEvents.java (createSkullCandle, makeSkullCandle)
//   src/main/java/twilightforest/block/LightableBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the 12 skull candles. The block entity's candle colour (TF's CandleColors value)
// lives in BlockData ({"color": n}); rotation / facing, candle count and lighting are block states.
// The items keep item data twilightforest:skull_candles ({"color": n, "count": n}, default {0, 1}).
//
// Bedrock notes: vanilla heads are the flattened head blocks (minecraft:zombie_head, ...) with
// facing_direction (1 floor, 2..5 wall) and the floor rotation in the skull block actor (degrees,
// 22.5 per Java rotation step). Bedrock heads carry no player profile, so a player skull candle has
// no owner to keep. The flames (animateTick) and the redstone head animation are client-side.

#include <algorithm>
#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/SkullBlockActor.h"

#include "feature/families/BlockEvents.h"
#include "feature/item/Item.h"
#include "feature/lichdecor/LightablesInternal.h"
#include "feature/loot/Loot.h"
#include "feature/stats/Stats.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"

namespace gf::feature::lichdecor::light {

namespace {

constexpr char kDataComponent[] = "twilightforest:skull_candles";
constexpr char kStat[]          = "twilightforest:skull_candles_made";

/// The six skull kinds and their vanilla head blocks (Bedrock ids; the same names as Java's).
struct Kind {
    char const* type;
    char const* head; ///< vanilla head block / item id
};
constexpr Kind kKinds[] = {
    {"zombie", "minecraft:zombie_head"},     {"skeleton", "minecraft:skeleton_skull"}, {"wither_skeleton", "minecraft:wither_skeleton_skull"},
    {"creeper", "minecraft:creeper_head"},   {"player", "minecraft:player_head"},      {"piglin", "minecraft:piglin_head"},
};

std::string candleBlock(std::string const& type, bool wall) { return "twilightforest:" + type + (wall ? "_wall_skull_candle" : "_skull_candle"); }

char const* headOf(std::string const& type) {
    for (auto const& k : kKinds)
        if (type == k.type) return k.head;
    return nullptr;
}

char const* typeOfHead(std::string const& head) {
    for (auto const& k : kKinds)
        if (head == k.head) return k.type;
    return nullptr;
}

int colorAt(BlockSource& region, BlockPos const& pos) { return dataAt(region, pos).value("color", 0); }

/// The skull block actor of a vanilla head (nullptr when there is none yet).
SkullBlockActor* skullActor(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::Skull) return nullptr;
    return static_cast<SkullBlockActor*>(actor);
}

int rotation16Of(float degrees) { return static_cast<int>(std::lround(degrees / 22.5F)) & 15; }

/// SkullCandleBlock / WallSkullCandleBlock#getParticleOffsets (the wall form's flames are moved by
/// the client's animateTick only).
std::vector<Vec3> offsets(Block const& b, bool wall) {
    int n = intStateOr(b, "candles", 1);
    float lo = wall ? 1.063F : 0.73F, mid = wall ? 1.19F : 0.88F, hi = wall ? 1.25F : 0.96F;
    switch (n) {
    case 1: return {{0.5F, hi, 0.5F}};
    case 2: return {{0.375F, mid, 0.5F}, {0.625F, hi, 0.44F}};
    case 3: return {{0.5F, lo, 0.625F}, {0.375F, mid, 0.5F}, {0.56F, hi, 0.44F}};
    case 4: return {{0.44F, lo, 0.56F}, {0.625F, mid, 0.56F}, {0.375F, mid, 0.375F}, {0.56F, hi, 0.375F}};
    default: return {};
    }
}

void setLighting(BlockSource& region, BlockPos const& pos, Lighting l) { setStates(region, pos, region.getBlock(pos), Json{{"lighting", lightingName(l)}}); }

/// Puts the vanilla head back (the last candle removed), turned as the skull candle was.
void restoreSkull(BlockSource& region, BlockPos const& pos, Block const& b, SkullCandleKind const& kind) {
    auto const* head = headOf(kind.type);
    if (!head) return;
    int facing = kind.wall ? std::max(2, faceOf(state(b, "facing"))) : 1;
    int rot    = kind.wall ? 0 : intStateOr(b, "rotation", 0);
    setDataAt(region, pos, Json());
    if (auto const* nb = game::blockWithStates(head, Json{{"facing_direction", facing}})) game::setBlock(region, pos, *nb);
    if (kind.wall) return;
    int dim = static_cast<int>(region.getDimensionId());
    auto apply = [dim, pos, rot, head = std::string(head)] {
        auto* r = game::regionOf(dim);
        if (!r || game::blockIdAt(*r, pos) != head) return;
        if (auto* sk = skullActor(*r, pos)) {
            sk->mRotation = static_cast<float>(rot) * 22.5F;
            sk->refresh(*r);
        }
    };
    if (skullActor(region, pos)) apply();
    else ia::nextTick(apply); // the block actor is created with the block, sometimes a tick later
}

// ---- interactions ------------------------------------------------------------------------------------

/// EntityEvents#createSkullCandle: a vanilla candle on a vanilla head.
ia::Result createSkullCandle(ia::UseOnContext const& c) {
    if (gf::config::get<LichdecorConfig>().disableSkullCandleCreation) return ia::Result::Pass;
    int color = tfColorOfCandle(c.itemId);
    if (color < 0 || c.sneaking) return ia::Result::Pass;
    auto const* type = typeOfHead(c.blockId);
    if (!type) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    BlockPos pos    = game::blockPos(c.pos);
    int      facing = game::intState(c.block, "facing_direction").value_or(1);
    bool     wall   = facing >= 2 && facing <= 5;
    int      rot    = 0;
    if (!wall)
        if (auto* sk = skullActor(*region, pos)) rot = rotation16Of(sk->mRotation);
    Json states{{"twilightforest:lighting", "none"}, {"twilightforest:candles", 1}};
    if (wall) states["twilightforest:facing"] = faceName(facing);
    else states["twilightforest:rotation"] = rot;
    auto const* nb = game::blockWithStates(candleBlock(type, wall), states);
    if (!nb) return ia::Result::Pass; // the skull candle blocks are not in this build
    playSound(*region, centre(pos), "minecraft:block.candle.place", 1.0F, 1.0F);
    game::setBlock(*region, pos, *nb);
    setDataAt(*region, pos, Json{{"color", color}});
    consumeOne(c.player, c.item);
    gf::stats::award(c.player, kStat);
    return ia::Result::Consume; // the arm swings; nothing is placed afterwards
}

ia::Result useSkullCandle(ia::UseOnContext const& c) {
    auto kind = skullCandleKind(c.blockId);
    if (!kind) return ia::Result::Pass;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    BlockPos     pos     = game::blockPos(c.pos);
    Block const& b       = region->getBlock(pos);
    int          color   = colorAt(*region, pos);
    int          candles = intStateOr(b, "candles", 1);
    Lighting     l       = lightingOf(b).value_or(Lighting::None);
    // useItemOn: the same colour's candle adds one (not sneaking)
    if (!c.itemId.empty() && c.itemId == candleOfTfColor(color) && !c.sneaking && candles < 4) {
        if (!c.firstEvent) return ia::Result::Consume;
        setStates(*region, pos, b, Json{{"candles", candles + 1}});
        playSound(*region, centre(pos), "minecraft:block.candle.place", 1.0F, 1.0F);
        consumeOne(c.player, c.item);
        return ia::Result::Consume;
    }
    // LightableBlock#tryLightCandles
    if (c.itemId.empty() && mayBuild(c.player) && l != Lighting::None) {
        if (!c.firstEvent) return ia::Result::Consume;
        auto offs = offsets(b, kind->wall);
        setLighting(*region, pos, Lighting::None);
        extinguishFx(*region, pos, offs);
        return ia::Result::Consume;
    }
    if (l == Lighting::None && isFirestarter(c.itemId)) {
        if (!c.firstEvent) return ia::Result::Consume;
        setLighting(*region, pos, Lighting::Normal);
        bool flint = c.itemId == "minecraft:flint_and_steel";
        playSound(*region, centre(pos), flint ? "minecraft:item.flintandsteel.use" : "minecraft:item.firecharge.use", 1.0F,
                  flint ? randomFloat() * 0.4F + 0.8F : (randomFloat() - randomFloat()) * 0.2F + 1.0F);
        useFirestarter(c.player, c.item, c.itemId);
        return ia::Result::Consume;
    }
    // useWithoutItem: sneaking with an empty hand takes a candle off
    if (c.itemId.empty() && c.sneaking && candles > 0) {
        if (!c.firstEvent) return ia::Result::Consume;
        if (candles - 1 > 0) setStates(*region, pos, b, Json{{"candles", candles - 1}});
        else restoreSkull(*region, pos, b, *kind);
        playSound(*region, centre(pos), "minecraft:block.candle.break", 1.0F, 1.0F);
        giveCandleBack(c.player, candleOfTfColor(color));
        return ia::Result::Consume;
    }
    return ia::Result::Pass;
}

/// SkullCandleItem (StandingAndWallBlockItem, attachment DOWN) placing its block.
ia::Result placeSkullCandleItem(ia::UseOnContext const& c, std::string const& type) {
    if (!c.firstEvent) return ia::Result::Consume;
    auto* region = game::regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    auto at = placementPos(*region, game::blockPos(c.pos), c.face);
    if (!at) return ia::Result::Pass;
    auto data    = gf::item::getData(c.item, kDataComponent);
    int  color   = data.is_object() ? data.value("color", 0) : 0;
    int  count   = data.is_object() ? std::clamp(data.value("count", 1), 1, 4) : 1;
    Json states{{"twilightforest:lighting", "none"}, {"twilightforest:candles", count}};
    std::string id;
    for (int dir : nearestLookingDirections(c.player)) {
        if (dir == 1) continue;
        if (dir == 0) {
            // SkullCandleBlock#getStateForPlacement: the rotation from the player's yaw
            float yaw = c.player.getRotation().y;
            states["twilightforest:rotation"] = static_cast<int>(std::floor(yaw * 16.0F / 360.0F + 0.5F)) & 15;
            id = candleBlock(type, false);
            break;
        }
        // WallSkullCandleBlock#getStateForPlacement: the first horizontal look direction whose
        // neighbour is not replaceable; the skull faces away from it
        bool found = false;
        for (int d : nearestLookingDirections(c.player)) {
            if (d < 2) continue;
            if (!game::replaceable(*region, relative(*at, d))) {
                states["twilightforest:facing"] = faceName(oppositeFace(d));
                found                           = true;
                break;
            }
        }
        if (found) {
            id = candleBlock(type, true);
            break;
        }
    }
    if (id.empty()) return ia::Result::Pass;
    auto const* nb = game::blockWithStates(id, states);
    if (!nb) return ia::Result::Pass;
    game::setBlock(*region, *at, *nb);
    setDataAt(*region, *at, Json{{"color", std::clamp(color, 0, 16)}});
    playSound(*region, centre(*at), "minecraft:block.stone.place", 1.0F, 0.8F);
    consumeOne(c.player, c.item);
    return ia::Result::Consume;
}

} // namespace

std::vector<Vec3> skullCandleFlameOffsets(Block const& b, bool wall) {
    auto out = offsets(b, wall);
    if (!wall) return out;
    // WallSkullCandleBlock#animateTick: off the wall by a quarter block (-facing step * 0.25)
    auto f  = std::string(state(b, "facing"));
    float sx = f == "west" ? -1.0F : f == "east" ? 1.0F : 0.0F, sz = f == "north" ? -1.0F : f == "south" ? 1.0F : 0.0F;
    for (auto& o : out) o = Vec3{o.x - sx * 0.25F, o.y, o.z - sz * 0.25F};
    return out;
}

std::optional<SkullCandleKind> skullCandleKind(std::string_view id) {
    for (auto const& k : kKinds) {
        if (id == candleBlock(k.type, false)) return SkullCandleKind{k.type, false};
        if (id == candleBlock(k.type, true)) return SkullCandleKind{k.type, true};
    }
    return std::nullopt;
}

void registerSkullCandles() {
    for (auto const& k : kKinds) {
        // the event runs before the block's own interaction (PlayerInteractEvent.RightClickBlock)
        ia::onUseOn({.block = k.head}, createSkullCandle, ia::priority::High);
        for (bool wall : {false, true}) {
            std::string id = candleBlock(k.type, wall);
            ia::onUseOn({.block = id}, useSkullCandle);
            gf::blockevents::onProjectileHit(id, [](BlockSource& region, BlockPos const& pos, Actor& projectile) {
                Block const& b = region.getBlock(pos);
                if (projectile.isOnFire() && lightingOf(b).value_or(Lighting::None) == Lighting::None) setLighting(region, pos, Lighting::Normal);
            });
        }
        std::string type = k.type;
        ia::onUseOn({.item = candleBlock(type, false)}, [type](ia::UseOnContext const& c) { return placeSkullCandleItem(c, type); }, ia::priority::Low);
    }

    // AbstractSkullCandleBlock#getDrops: the vanilla head plus the candles, or under silk touch
    // the skull candle item with its candles
    gf::loot::addGlobalModifier("gf:lichdecor/skull_candle_drops", [](std::vector<ItemStack>& loot, gf::loot::LootContext const& ctx, std::string const& tableId) {
        if (!ctx.region || !ctx.block || tableId.rfind("twilightforest:blocks/", 0) != 0) return;
        auto kind = skullCandleKind("twilightforest:" + tableId.substr(22));
        if (!kind) return;
        auto const* head = headOf(kind->type);
        auto        skull = std::find_if(loot.begin(), loot.end(), [&](ItemStack const& s) { return game::items::idOf(s) == head; });
        if (skull == loot.end()) return;
        BlockPos pos{static_cast<int>(std::floor(ctx.origin.x)), static_cast<int>(std::floor(ctx.origin.y)), static_cast<int>(std::floor(ctx.origin.z))};
        int      color   = dataForDrops(*ctx.region, pos).value("color", 0);
        int      candles = intStateOr(*ctx.block, "candles", 1);
        if (silkTouch(ctx.tool) > 0) {
            auto stack = game::items::make(candleBlock(kind->type, false), 1);
            if (!stack) return;
            gf::item::setData(*stack, kDataComponent, Json{{"color", color}, {"count", candles}});
            *skull = *stack;
        } else if (candles > 0) {
            if (auto st = game::items::make(candleOfTfColor(color), candles)) loot.push_back(*st);
        }
    });
}

} // namespace gf::feature::lichdecor::light

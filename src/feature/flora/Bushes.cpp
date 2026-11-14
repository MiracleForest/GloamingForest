// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/GloamingBushBlock.java
//   src/main/java/gloamingforest/block/BerryBushBlock.java
//   src/main/java/gloamingforest/block/DarkTowerBerryBushBlock.java
//   src/main/java/gloamingforest/block/OreBerryBlock.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U6 flora: the 12 berry bushes. The snow inside them (SnowLoggable: layers, adding snow, breaking
// the snow first, melting) is F11's snowloggable family; this file gives them GloamingBushBlock's growth
// (random ticks, light gates, upward growth), bone meal, the harvest into the player's inventory,
// the dark tower bushes' death on nylium and the oreberries' thorns.

#include "feature/flora/Flora.h"

#include <algorithm>
#include <array>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/families/BlockEvents.h"
#include "feature/loot/Loot.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::flora {

namespace ia = interactions;

namespace bushes {

namespace {

constexpr std::array<char const*, 4> kOverworld{"gloamingforest:raspberry_bush", "gloamingforest:blueberry_bush", "gloamingforest:blackberry_bush",
                                                "gloamingforest:maloberry_bush"};
constexpr std::array<char const*, 4> kDarkTower{"gloamingforest:blightberry_bush", "gloamingforest:duskberry_bush", "gloamingforest:skyberry_bush",
                                                "gloamingforest:stingberry_bush"};
constexpr std::array<char const*, 3> kOre{"gloamingforest:iron_oreberry", "gloamingforest:gold_oreberry", "gloamingforest:copper_oreberry"};
constexpr char                       kEssence[] = "gloamingforest:essence_oreberry";

constexpr int kMaxAge = 3;

template <std::size_t N>
bool in(std::array<char const*, N> const& a, std::string const& id) {
    return std::find_if(a.begin(), a.end(), [&](char const* s) { return id == s; }) != a.end();
}

/// GloamingBushBlock#canBePlacedAt: the "placed on" tag of the bush.
bool canBePlacedAt(Kind kind, BlockSource& region, BlockPos const& below) {
    switch (kind) {
    case Kind::Overworld: return tagged(region, below, "gloamingforest:gf_berry_bushes_survive");
    case Kind::DarkTower: return tagged(region, below, "gloamingforest:dark_tower_berry_bushes_survive");
    default: return tagged(region, below, "gloamingforest:oreberry_bushes_survive");
    }
}

/// GloamingBushBlock#grow.
void grow(BlockSource& region, BlockPos const& pos, Block const& state, int age) {
    if (auto const* grown = withStates(state, {{st::Age, age}})) game::setBlock(region, pos, *grown);
    if (age == 2 && intState(state, st::Layers, 0) > 0) {
        BlockPos     up    = relative(pos, Up);
        Block const* above = blockAt(region, up);
        if (!above) return;
        if (replaceable(region, up)) {
            place(region, up, "minecraft:snow_layer");
        } else if (isBush(above->getTypeName()) && intState(*above, st::Layers, -1) == 0) {
            setStates(region, up, {{st::Layers, 1}});
        }
    }
}

/// BerryBushBlock#tryGrowUpwards (with the state before this tick's growth).
void tryGrowUpwards(BlockSource& region, BlockPos const& pos, Block const& state, RandomSource& random) {
    if (random.nextInt(3) == 0 && intState(state, st::Age, 0) >= 2) {
        BlockPos     up    = relative(pos, Up);
        Block const* above = blockAt(region, up);
        if (!above) return;
        if (above->isAir()) {
            place(region, up, state.getTypeName());
        } else if (above->getTypeName() == "minecraft:snow_layer") {
            // Bedrock's snow layer height is Java's layers - 1
            int layers = std::clamp(intState(*above, "height", 0) + 1, 1, 8);
            place(region, up, state.getTypeName(), {{st::Layers, layers}});
        }
    }
}

/// Java's harvest (GloamingBushBlock#useWithoutItem at age 3).
void harvest(Player& player, BlockSource& region, BlockPos const& pos, Block const& state) {
    loot::LootContext ctx;
    ctx.region = &region;
    ctx.origin = Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f};
    ctx.block  = &state;
    ctx.tool   = &game::items::empty();
    for (auto const& stack : loot::roll(berryTable(state.getTypeName()), ctx)) give(player, stack);
    vanillaSound(region, pos, "block.sweet_berry_bush.pick", 1.0f, 0.8f + levelRandom().nextFloat() * 0.4f);
    if (auto const* picked = withStates(state, {{st::Age, kMaxAge - 1}})) game::setBlock(region, pos, *picked);
    log().info("flora: {} picked {} at {} {} {}", player.getRealName(), state.getTypeName(), pos.x, pos.y, pos.z);
}

/// BerryBushBlock#isValidBonemealTarget.
bool validBonemealTarget(BlockSource& region, BlockPos const& pos, Block const& state) {
    if (intState(state, st::Age, 0) < kMaxAge - 1) return true;
    BlockPos up = relative(pos, Up);
    return isAir(region, up) || idAt(region, up) == "minecraft:snow_layer";
}

} // namespace

bool isBush(std::string const& blockId) { return in(kOverworld, blockId) || in(kDarkTower, blockId) || in(kOre, blockId) || blockId == kEssence; }

Kind kindOf(std::string const& blockId) {
    if (in(kDarkTower, blockId)) return Kind::DarkTower;
    if (in(kOre, blockId)) return Kind::Oreberry;
    if (blockId == kEssence) return Kind::Essence;
    return Kind::Overworld;
}

std::string berryTable(std::string const& blockId) {
    if (blockId == kEssence) return "gloamingforest:blocks/essence_berry_bush_berries";
    if (in(kOre, blockId)) return "gloamingforest:blocks/" + blockId.substr(15) + "_bush_berries";
    return "gloamingforest:blocks/" + blockId.substr(15) + "_berries";
}

/// GloamingBushBlock#canSurvive (+ OreBerryBlock's light limit).
bool canSurvive(BlockSource& region, BlockPos const& pos, Block const& state) {
    auto        id    = state.getTypeName();
    Kind        kind  = kindOf(id);
    BlockPos    down  = relative(pos, Down);
    auto const* below = blockAt(region, down);
    if (!below) return false;
    bool sameMature = below->getTypeName() == id && intState(*below, st::Age, 0) >= kMaxAge - 1;
    bool ok         = canBePlacedAt(kind, region, down) || sameMature;
    if (kind == Kind::Oreberry) ok = ok && rawBrightness(region, pos) < 13;
    return ok;
}

bool canGrowAt(BlockSource& region, BlockPos const& pos, Block const& state) {
    switch (kindOf(state.getTypeName())) {
    case Kind::Overworld: return rawBrightness(region, pos) >= 8 && canSurvive(region, pos, state);
    case Kind::DarkTower: return rawBrightness(region, pos) >= 8 && canSurvive(region, pos, state) && intState(state, st::Layers, 0) == 0;
    case Kind::Oreberry: return canSurvive(region, pos, state) && rawBrightness(region, pos) < 10;
    case Kind::Essence: return canSurvive(region, pos, state);
    }
    return false;
}

void randomTick(BlockSource& region, BlockPos const& pos, RandomSource& random) {
    Block const* b = blockAt(region, pos);
    if (!b || !isBush(b->getTypeName())) return;
    Block const& state = *b;
    Kind         kind  = kindOf(state.getTypeName());
    // GloamingBushBlock#randomTick (the snow melts in F11's snowloggable tick)
    int age = intState(state, st::Age, 0);
    if (age < kMaxAge && random.nextInt(20) == 0 && canGrowAt(region, pos, state)) grow(region, pos, state, age + 1);
    // BerryBushBlock#randomTick: the column below this bush
    if (kind == Kind::Overworld || kind == Kind::DarkTower) {
        int height = 0;
        while (height < 3) {
            auto const* below = blockAt(region, relative(pos, Down, height + 1));
            if (!below || below->getTypeName() != state.getTypeName()) break;
            ++height;
        }
        if (random.nextInt(20) == 0 && height < 2 && canGrowAt(region, pos, state)) tryGrowUpwards(region, pos, state, random);
    }
}

void performBonemeal(BlockSource& region, BlockPos const& pos, RandomSource& random) {
    Block const* b = blockAt(region, pos);
    if (!b) return;
    Block const& state = *b;
    int          age   = intState(state, st::Age, 0);
    if (age < 2) grow(region, pos, state, std::min(age + 1 + random.nextInt(2), kMaxAge - 1));
    tryGrowUpwards(region, pos, state, random);
}

} // namespace bushes

void installBushes() {
    using namespace bushes;
    std::vector<std::string> all;
    for (auto const* id : kOverworld) all.emplace_back(id);
    for (auto const* id : kDarkTower) all.emplace_back(id);
    for (auto const* id : kOre) all.emplace_back(id);
    all.emplace_back(kEssence);

    for (auto const& bush : all) {
        blockevents::onRandomTick(bush, [](BlockSource& region, BlockPos const& pos, Block const&, RandomSource& random) { randomTick(region, pos, random); });

        // GloamingBushBlock#useItemOn / useWithoutItem: the harvest at age 3 (any item but the snow F11
        // layers in, which passes here when it can't), bone meal on the berry bushes
        ia::onUseOn({.block = bush}, [](ia::UseOnContext const& c) {
            // ServerPlayerGameMode#useItemOn: sneaking with an item skips the block's use
            bool emptyHands = c.itemId.empty() && game::items::isEmpty(c.player.getOffhandSlot());
            if (c.sneaking && !emptyHands) return ia::Result::Pass;
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos pos    = game::blockPos(c.pos);
            if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
            Block const* b = blockAt(region, pos);
            if (!b) return ia::Result::Pass;
            if (intState(*b, st::Age, 0) == 3) {
                harvest(c.player, region, pos, *b);
                return ia::Result::Consume;
            }
            Kind kind = kindOf(b->getTypeName());
            if (c.itemId == "minecraft:bone_meal" && (kind == Kind::Overworld || kind == Kind::DarkTower)) {
                // BoneMealItem#growCrop: a valid target always uses the bone meal (isBonemealSuccess: true)
                if (!validBonemealTarget(region, pos, *b)) return ia::Result::Consume;
                consumeHeld(c.player, c.item);
                boneMealEffect(region, pos);
                performBonemeal(region, pos, levelRandom());
                return ia::Result::Consume;
            }
            return ia::Result::Pass;
        });
        // BlockItem#canPlace: a bush is placed only where it survives
        ia::onPlace({.block = bush}, [](ia::PlaceContext const& c) {
            auto&    region = c.player.getDimensionBlockSource();
            BlockPos pos    = game::blockPos(c.pos);
            Block const* b  = blockAt(region, pos);
            if (!b || canSurvive(region, pos, *b)) return;
            removeBlock(region, pos);
            if (!c.player.isCreative()) {
                if (auto s = makeItem(b->getTypeName())) give(c.player, *s);
            }
        });
    }
    // DarkTowerBerryBushBlock#updateShape / tick: dies on the die tag (destroyed with drops a tick later)
    for (auto const* bush : kDarkTower) {
        blockevents::onNeighborChanged(bush, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
            if (!tagged(region, relative(pos, Down), "gloamingforest:dark_tower_berry_bushes_die")) return;
            GlobalPos gp = game::globalPos(region, pos);
            schedule(1, [gp] {
                auto* r = game::regionOf(gp.dim);
                if (!r) return;
                BlockPos p = game::blockPos(gp);
                if (!isBush(idAt(*r, p)) || !tagged(*r, relative(p, Down), "gloamingforest:dark_tower_berry_bushes_die")) return;
                destroyBlock(*r, p, true);
                log().info("flora: {} at {} {} {} died on nylium", "dark tower bush", p.x, p.y, p.z);
            });
        });
    }
    // OreBerryBlock#entityInside: 1 damage of gloamingforest:oreberry to anything but item entities
    for (std::string const& ore : {"gloamingforest:iron_oreberry", "gloamingforest:gold_oreberry", "gloamingforest:copper_oreberry", "gloamingforest:essence_oreberry"}) {
        blockevents::onEntityInside(ore, [](BlockSource&, BlockPos const&, Actor& entity) {
            if (entity.hasType(ActorType::ItemEntity) || !entity.isAlive()) return;
            combat::hurt(entity, 1.0f, "gloamingforest:oreberry");
        });
    }
}

} // namespace gf::feature::flora

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/CinderFurnaceBlock.java
//   src/main/java/gloamingforest/block/entity/CinderFurnaceBlockEntity.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U30 (finalcastle): the cinder furnace (a work-in-progress block of upstream 4.8: hidden from the creative
// tab, no recipe). Its block entity is emulated: the three slots (0 input, 1 fuel, 2 result) are the
// inventory contract's (F10) native container of the block (the BP declares minecraft:block_entity),
// which hoppers reach (F11's hopper lookup); the furnace fields (lit time, cooking progress) are block
// data, ticked every server tick while the block is tracked. Right-clicking opens nothing (upstream's
// openContainer is empty). Smelting uses upstream's minecraft:smelting data recipes (F9) and else the
// game's furnace recipes (cooking time 200); cinder logs in the 3 x 3 x 3 around the furnace speed
// it up and multiply its output, and while it burns it turns logs around it into cinder logs.
//
// Deviations: upstream multiplies the recipe's shared result stack (its count grows with every smelt); the
// result here is a copy. Hoppers see one plain container (no furnace faces: they fill and empty any
// slot). The client-side crackle / smoke / flame of animateTick (FurnaceBlock's vanilla copy) are
// sent by the server while the furnace burns, at the rate Java's ClientLevel#animateTick samples the
// block for the nearest player (one 32^3 and one 64^3 cube around it, 667 picks each, every tick).
#include <algorithm>
#include <cmath>
#include <map>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/ext/families/Hoppers.h"
#include "feature/families/BlockEvents.h"
#include "feature/fx/Fx.h"
#include "feature/finalcastle/BlocksCommon.h"
#include "feature/inventory/Inventory.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::finalcastle::blocks {

namespace ia    = gf::interactions;
namespace items = gf::game::items;

namespace {

constexpr char kDataKey[]          = "cinder_furnace";
constexpr char kLit[]              = "gloamingforest:lit";
constexpr char kFacing[]           = "gloamingforest:facing";
constexpr int  kSmeltLogFactor     = 10;
constexpr int  kDefaultCookingTime = 200;
/// ClientLevel#animateTick's chance to pick a given block in a tick: 667 picks in the 32^3 cube
/// around the player (|d| < 16) and 667 in the 64^3 cube (|d| < 32).
constexpr float kAnimateNear = 667.0F / (32.0F * 32.0F * 32.0F);
constexpr float kAnimateFar  = 667.0F / (64.0F * 64.0F * 64.0F);

/// The chance for the player nearest (by cube distance) to `pos` in its dimension: 0 without one
/// within 32 blocks.
float animateChance(BlockSource& region, BlockPos const& pos) {
    auto* l = level();
    if (!l) return 0.0F;
    int  dim  = static_cast<int>(region.getDimensionId());
    float best = 0.0F;
    l->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !p.isAlive()) return true;
        auto  q = p.getFeetPos();
        float d = std::max({std::abs(q.x - (static_cast<float>(pos.x) + 0.5F)), std::abs(q.y - static_cast<float>(pos.y)),
                            std::abs(q.z - (static_cast<float>(pos.z) + 0.5F))});
        if (d < 16.0F) best = std::max(best, kAnimateNear + kAnimateFar);
        else if (d < 32.0F) best = std::max(best, kAnimateFar);
        return true;
    }));
    return best;
}

/// The visuals' own random (the effects must not shift the furnace's gameplay random).
core::jrand::RandomSource& visualRandom() {
    static core::jrand::LegacyRandom r(0x43494E444552LL);
    return r;
}

/// The c:ores members of NeoForge 21.1 as Bedrock item ids (docs/spec/finalcastle.md), for when the
/// tag data does not answer.
constexpr std::array<char const*, 19> kOres{
    "minecraft:coal_ore",    "minecraft:deepslate_coal_ore",    "minecraft:copper_ore",   "minecraft:deepslate_copper_ore",
    "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", "minecraft:emerald_ore",  "minecraft:deepslate_emerald_ore",
    "minecraft:gold_ore",    "minecraft:deepslate_gold_ore",    "minecraft:nether_gold_ore", "minecraft:iron_ore",
    "minecraft:deepslate_iron_ore", "minecraft:lapis_ore",      "minecraft:deepslate_lapis_ore", "minecraft:redstone_ore",
    "minecraft:deepslate_redstone_ore", "minecraft:quartz_ore", "minecraft:ancient_debris",
};

std::map<GlobalPos, FurnaceState>& cache() {
    static std::map<GlobalPos, FurnaceState> m;
    return m;
}

struct Smelt {
    ItemStack result;
    int       cookingTime = kDefaultCookingTime;
};

bool ingredientMatches(recipes::Ingredient const& in, std::string const& itemId) {
    if (std::find(in.items.begin(), in.items.end(), itemId) != in.items.end()) return true;
    return !in.tag.empty() && tags::has(tags::Registry::Item, in.tag, itemId);
}

/// RecipeManager#getRecipeFor(SMELTING, input): upstream's data recipes, then the game's furnace recipes.
std::optional<Smelt> recipeFor(ItemStack const& input) {
    if (items::isEmpty(input)) return std::nullopt;
    auto id = items::idOf(input);
    if (auto const* r = recipes::findFirst("minecraft:smelting", [&](recipes::CraftingRecipe const& rc) {
            return !rc.ingredients.empty() && ingredientMatches(rc.ingredients.front(), id);
        })) {
        auto out = items::make(r->resultItem, std::max(1, r->resultCount));
        if (out && !items::isEmpty(*out)) return Smelt{*out, r->data.value("cookingtime", kDefaultCookingTime)};
    }
    auto* l = level();
    if (!l) return std::nullopt;
    ItemInstance result = l->getRecipes().getFurnaceRecipeResult(input, HashedString("furnace"));
    if (result.isNull() || result.mCount == 0) return std::nullopt;
    auto out = items::make(result.getTypeName(), std::max<int>(1, result.mCount), result.getAuxValue());
    if (!out || items::isEmpty(*out)) return std::nullopt;
    return Smelt{*out, kDefaultCookingTime};
}

int burnDuration(ItemStack const& fuel) {
    if (items::isEmpty(fuel)) return 0;
    return static_cast<int>(FurnaceBlockActor::getBurnDuration(fuel, 200.0f));
}

/// AbstractFurnaceBlockEntity#canPlaceItem.
bool canPlaceItem(int slot, ItemStack const& stack, ItemStack const& fuelSlot) {
    if (slot == 2) return false;
    if (slot != 1) return true;
    return burnDuration(stack) > 0 || (items::idOf(stack) == "minecraft:bucket" && items::idOf(fuelSlot) != "minecraft:bucket");
}

/// canMultiply: #minecraft:logs or #c:ores.
bool canMultiply(ItemStack const& input) {
    auto id = items::idOf(input);
    if (tags::has(tags::Registry::Item, "minecraft:logs", id) || tags::has(tags::Registry::Item, "c:ores", id)) return true;
    return std::find(kOres.begin(), kOres.end(), id) != kOres.end();
}

/// The pillar axis of a log ("x" / "y" / "z"; "" without one).
std::string axisOfLog(Block const& b) {
    auto a = textState(b, "pillar_axis");
    if (a.empty()) a = textState(b, "gloamingforest:axis");
    return a;
}

struct Furnace {
    BlockSource&             region;
    BlockPos                 pos;
    GlobalPos                gpos;
    inventory::Handler       inv;
    FurnaceState             st;

    ItemStack get(int slot) { return inv.get(gpos, slot); }
    void      put(int slot, ItemStack const& s) { inv.set(gpos, slot, s); }
    bool      burning() const { return st.litTime > 0; }

    /// getCurrentMaxSmeltMultiplier: ceil(logs / 10).
    int maxSmeltMultiplier() { return static_cast<int>(std::ceil(static_cast<float>(countCinderLogs(region, pos)) / kSmeltLogFactor)); }

    int maxOutputStacks(ItemStack const& input, ItemStack const& output) {
        return canMultiply(input) ? items::countOf(output) * maxSmeltMultiplier() : items::countOf(output);
    }

    bool canBurn(Smelt const& recipe) {
        ItemStack input = get(0);
        if (items::isEmpty(input) || items::isEmpty(recipe.result)) return false;
        ItemStack out = get(2);
        if (items::isEmpty(out)) return true;
        if (items::idOf(out) != items::idOf(recipe.result) || out.getAuxValue() != recipe.result.getAuxValue()) return false;
        int result = items::countOf(out) + maxOutputStacks(input, recipe.result);
        return result <= 64 && result <= static_cast<int>(out.getMaxStackSize());
    }

    void smeltItem(Smelt const& recipe) {
        if (!canBurn(recipe)) return;
        ItemStack input  = get(0);
        int       count  = items::countOf(recipe.result) * cinderMultiplier(region, pos, kSmeltLogFactor);
        ItemStack result = items::withCount(recipe.result, count);
        ItemStack out    = get(2);
        if (items::isEmpty(out)) put(2, result);
        else if (items::idOf(out) == items::idOf(result)) {
            // ItemStack#grow (Java lets the stack pass its maximum; Bedrock's stays within it)
            put(2, items::withCount(out, std::min(items::countOf(out) + count, static_cast<int>(out.getMaxStackSize()))));
        }
        if (items::idOf(input) == "minecraft:wet_sponge" && items::idOf(get(1)) == "minecraft:bucket") {
            if (auto water = items::make("minecraft:water_bucket")) put(1, *water);
        }
        put(0, items::withCount(input, items::countOf(input) - 1));
    }

    /// cinderizeNearbyLog.
    void cinderize() {
        auto& rand = random();
        int   dx   = rand.nextInt(2) - rand.nextInt(2);
        int   dy   = rand.nextInt(2) - rand.nextInt(2);
        int   dz   = rand.nextInt(2) - rand.nextInt(2);
        BlockPos at{pos.x + dx, pos.y + dy, pos.z + dz};
        if (!loaded(region, at)) return;
        Block const& nearby = region.getBlock(at);
        if (game::blockId(nearby) == id::CinderLog || !isLog(nearby)) return;
        // withPropertiesOf: the axis carries over
        Json s    = Json::object();
        auto axis = axisOfLog(nearby);
        if (axis == "x" || axis == "y" || axis == "z") s["gloamingforest:axis"] = axis;
        set(region, at, id::CinderLog, s);
        // LevelEvent.PARTICLES_MOBBLOCK_SPAWN twice, then the fire's ambient sound
        levelEvent(region, 2004, center(at));
        levelEvent(region, 2004, center(at));
        sound(region, at, "block.fire.ambient", 1.0f, 1.0f);
    }

    /// CinderFurnaceBlock#animateTick ([VanillaCopy] FurnaceBlock#animateTick) while LIT.
    void animate() {
        auto& rand = visualRandom();
        if (rand.nextFloat() >= animateChance(region, pos)) return;
        auto& dim = region.getDimension();
        float d0  = static_cast<float>(pos.x) + 0.5F;
        float d1  = static_cast<float>(pos.y);
        float d2  = static_cast<float>(pos.z) + 0.5F;
        if (rand.nextDouble() < 0.1) fx::sound(dim, "block.furnace.fire_crackle", Vec3{d0, d1, d2}, 1.0F, 1.0F);
        auto   facing = textState(region.getBlock(pos), kFacing);
        int    stepX  = facing == "east" ? 1 : facing == "west" ? -1 : 0;
        int    stepZ  = facing == "south" ? 1 : facing == "north" ? -1 : 0;
        double d4     = rand.nextDouble() * 0.6 - 0.3;
        double d5     = stepX != 0 ? stepX * 0.52 : d4;
        double d6     = rand.nextDouble() * 6.0 / 16.0;
        double d7     = stepZ != 0 ? stepZ * 0.52 : d4;
        Vec3   at{static_cast<float>(d0 + d5), static_cast<float>(d1 + d6), static_cast<float>(d2 + d7)};
        particle(region, "minecraft:smoke", at);
        particle(region, "minecraft:flame", at);
    }

    void tick() {
        bool wasBurning = burning();
        bool changed    = false;
        // AbstractFurnaceBlockEntity#setItem: another input item restarts the cooking
        ItemStack input = get(0);
        auto      inId  = items::idOf(input);
        if (inId != st.lastInput) {
            if (!inId.empty()) {
                auto r              = recipeFor(input);
                st.cookingTotalTime = r ? r->cookingTime : kDefaultCookingTime;
                st.cookingProgress  = 0;
            }
            st.lastInput = inId;
            changed      = true;
        }
        if (burning()) {
            --st.litTime;
            changed = true;
        }
        ItemStack fuel = get(1);
        if (burning() || (!items::isEmpty(fuel) && !items::isEmpty(input))) {
            auto recipe = recipeFor(input);
            if (recipe && !burning() && canBurn(*recipe)) {
                st.litTime     = burnDuration(fuel);
                st.litDuration = st.litTime;
                if (burning()) {
                    changed = true;
                    if (!items::isEmpty(fuel)) {
                        auto fuelId = items::idOf(fuel);
                        int  left   = items::countOf(fuel) - 1;
                        if (left > 0) put(1, items::withCount(fuel, left));
                        else if (fuelId == "minecraft:lava_bucket") {
                            // Item#getCraftingRemainingItem
                            auto bucket = items::make("minecraft:bucket");
                            put(1, bucket ? *bucket : items::empty());
                        } else put(1, items::empty());
                    }
                }
            }
            if (recipe && burning() && canBurn(*recipe)) {
                // upstream: cook faster
                st.cookingProgress += cinderMultiplier(region, pos, 2);
                if (st.cookingProgress >= st.cookingTotalTime) { // upstream: >= since it may step by more than 1
                    st.cookingProgress  = 0;
                    st.cookingTotalTime = recipe->cookingTime;
                    smeltItem(*recipe);
                    st.lastInput = items::idOf(get(0));
                }
                changed = true;
            } else if (st.cookingProgress != 0) {
                st.cookingProgress = 0;
                changed            = true;
            }
        } else if (st.cookingProgress > 0) {
            st.cookingProgress = std::clamp(st.cookingProgress - 2, 0, st.cookingTotalTime);
            changed            = true;
        }
        if (wasBurning != burning()) {
            changed = true;
            setStates(region, pos, {{kLit, burning()}});
        }
        // upstream: occasionally cinderize nearby logs
        if (burning() && st.litTime % 5 == 0) cinderize();
        if (burning()) animate();
        if (changed) setFurnaceState(gpos, st);
    }
};

} // namespace

FurnaceState furnaceState(GlobalPos const& pos) {
    if (auto it = cache().find(pos); it != cache().end()) return it->second;
    FurnaceState s;
    if (auto const* doc = blockdata::get(pos); doc && doc->is_object() && doc->contains(kDataKey)) {
        auto const& j      = (*doc)[kDataKey];
        s.litTime          = j.value("lit_time", 0);
        s.litDuration      = j.value("lit_duration", 0);
        s.cookingProgress  = j.value("cooking_progress", 0);
        s.cookingTotalTime = j.value("cooking_total_time", 0);
        s.lastInput        = j.value("input", std::string{});
    }
    cache()[pos] = s;
    return s;
}

void setFurnaceState(GlobalPos const& pos, FurnaceState const& s) {
    cache()[pos] = s;
    auto* region = game::regionOf(pos.dim);
    if (!region) return;
    blockdata::set(
        *region,
        game::blockPos(pos),
        kDataKey,
        Json{{"lit_time", s.litTime}, {"lit_duration", s.litDuration}, {"cooking_progress", s.cookingProgress}, {"cooking_total_time", s.cookingTotalTime}, {"input", s.lastInput}}
    );
}

int countCinderLogs(BlockSource& region, BlockPos const& pos) {
    int count = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                BlockPos p{pos.x + dx, pos.y + dy, pos.z + dz};
                if (loaded(region, p) && idAt(region, p) == id::CinderLog) ++count;
            }
    return count;
}

int cinderMultiplier(BlockSource& region, BlockPos const& pos, int factor) {
    int logs = countCinderLogs(region, pos);
    if (logs < factor) return 1;
    return logs / factor + (random().nextInt(factor) >= logs % factor ? 0 : 1);
}

void tickFurnace(BlockSource& region, BlockPos const& pos) {
    auto gp = game::globalPos(region, pos);
    auto h  = inventory::find(region, pos);
    if (!h || !h->get || !h->set) return;
    Furnace f{region, pos, gp, *h, furnaceState(gp)};
    f.tick();
}

void installCinderFurnace() {
    // the block entity's three slots: the native container (BP minecraft:block_entity), which
    // hoppers reach; inserts follow AbstractFurnaceBlockEntity#canPlaceItem. Deviation: Java's hopper
    // treats the furnace as a sided WorldlyContainer (top -> input, sides -> fuel, bottom -> output);
    // F10's inventory::Handler has no face, so every face fills the first slot that accepts the stack
    inventory::Handler h;
    h.slots  = 3;
    h.insert = [](GlobalPos const& pos, ItemStack const& stack, bool simulate) -> ItemStack {
        auto* region = game::regionOf(pos.dim);
        if (!region) return stack;
        auto raw = inventory::find(*region, game::blockPos(pos));
        if (!raw || !raw->get || !raw->set) return stack;
        ItemStack rest = stack;
        for (int pass = 0; pass < 2 && !items::isEmpty(rest); ++pass) {
            for (int slot = 0; slot < 3 && !items::isEmpty(rest); ++slot) {
                ItemStack cur = raw->get(pos, slot);
                if (!canPlaceItem(slot, rest, raw->get(pos, 1))) continue;
                int max = std::min(64, static_cast<int>(rest.getMaxStackSize()));
                if (pass == 0 && !items::isEmpty(cur) && items::sameItemSameData(cur, rest)) {
                    int move = std::min(max - items::countOf(cur), items::countOf(rest));
                    if (move <= 0) continue;
                    if (!simulate) raw->set(pos, slot, items::withCount(cur, items::countOf(cur) + move));
                    rest = items::withCount(rest, items::countOf(rest) - move);
                } else if (pass == 1 && items::isEmpty(cur)) {
                    int move = std::min(max, items::countOf(rest));
                    if (!simulate) raw->set(pos, slot, items::withCount(rest, move));
                    rest = items::withCount(rest, items::countOf(rest) - move);
                }
            }
        }
        return rest;
    };
    inventory::registerBlock(id::CinderFurnace, h);
    gf::families_ext::setHopperVisible(id::CinderFurnace, true);
    // getAnalogOutputSignal: AbstractContainerMenu#getRedstoneSignalFromBlockEntity
    redstone::registerAnalog(id::CinderFurnace, [](BlockSource& region, BlockPos const& pos, Block const&) -> std::optional<int> {
        auto h = inventory::find(region, pos);
        if (!h || !h->get) return 0;
        auto  gp = game::globalPos(region, pos);
        float f  = 0;
        for (int i = 0; i < 3; ++i) {
            ItemStack s = h->get(gp, i);
            if (!items::isEmpty(s)) f += static_cast<float>(items::countOf(s)) / std::min(64, static_cast<int>(s.getMaxStackSize()));
        }
        return redstone::fractionToRedstoneLevel(f / 3.0f);
    });

    onDeviceTick(id::CinderFurnace, [](BlockSource& region, BlockPos const& pos, Block const&) { tickFurnace(region, pos); });
    onForget(id::CinderFurnace, [](GlobalPos const& p) { cache().erase(p); });
    blockevents::onPlaced(id::CinderFurnace, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { track(region, pos); });
    blockevents::onNeighborChanged(id::CinderFurnace, [](BlockSource& region, BlockPos const& pos, BlockPos const&) { track(region, pos); });
    blockevents::onRemoved(id::CinderFurnace, [](BlockSource& region, BlockPos const& pos, Block const&) {
        cache().erase(game::globalPos(region, pos));
    });
    // AbstractFurnaceBlock#getStateForPlacement: facing the player
    ia::onPlace({.block = id::CinderFurnace}, [](ia::PlaceContext const& c) {
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return;
        static constexpr std::array<char const*, 4> k2d{"south", "west", "north", "east"};
        int towards = game::opposite(game::playerFacing(c.player));
        setStates(*region, game::blockPos(c.pos), {{kFacing, k2d[static_cast<std::size_t>(towards & 3)]}});
        track(*region, game::blockPos(c.pos));
    });
    // useWithoutItem -> openContainer, which is empty in upstream 4.8: the click is taken, nothing opens
    ia::onUseOn({.block = id::CinderFurnace}, [](ia::UseOnContext const& c) {
        if (c.sneaking && !c.itemId.empty()) return ia::Result::Pass;
        if (auto* region = game::regionOf(c.pos.dim)) track(*region, game::blockPos(c.pos));
        return ia::Result::Consume;
    }, ia::priority::High);
}

} // namespace gf::feature::finalcastle::blocks

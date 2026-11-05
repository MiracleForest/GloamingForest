// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/DryingRackBlock.java
//   src/main/java/twilightforest/block/entity/DryingRackBlockEntity.java
//   src/main/java/twilightforest/item/recipe/DryingRecipe.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Drying racks (19 DryingRackBlocks). Custom Bedrock blocks have no block entities: the rack's
// DryingRackBlockEntity (item, dry_time, total_dry_time, drying) lives in block data under the key
// "drying_rack", and the racks holding an item are kept in an index (world DB
// gf:woodwork:racks) so they dry every server tick while their chunk is loaded, as Java's block
// entity ticker does. The water of a waterlogged rack is Bedrock's liquid layer: the rack's tick
// sees it come or go (Java's onRemove on the WATERLOGGED state change drops the item). Recipes
// are the `twilightforest:drying` recipes F9 loads (DryingRecipe: input, result, filter_time).
// Automation (hoppers, the structure kit's loot fill) goes through the inventory contract's
// handler: one slot, inserts one item into an empty rack, refuses extraction while drying
// (DryingRackHandler).

#include "feature/woodwork/DryingRack.h"

#include <algorithm>
#include <random>
#include <map>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/recipes/Recipes.h"
#include "feature/tags/Tags.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::woodwork::rack {

namespace ia = interactions;
namespace bd = blockdata;

namespace {

constexpr char kIndexKey[] = "gf:woodwork:racks";

struct Live {
    RackState r;
    bool      dirty = false; ///< dry_time not yet in block data
};

struct State {
    std::set<GlobalPos>        active; ///< racks holding an item (they tick)
    std::map<GlobalPos, Live>  live;   ///< the ticking racks' state (dry_time is written back lazily)
    bool                       indexDirty = false;
    std::vector<ia::HandlerId> handlers;
    ll::event::ListenerPtr     tick;
    std::vector<ChangeFn>      listeners;
};

State& state() {
    static State s;
    return s;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

bool isRackId(std::string const& id) { return std::find(dryingRackIds().begin(), dryingRackIds().end(), id) != dryingRackIds().end(); }

BlockSource* regionFor(GlobalPos const& pos) { return game::regionOf(pos.dim); }

Json const* rackDoc(BlockSource& region, BlockPos const& pos) { return bd::get(region, pos, kKey); }

void saveIndex() {
    auto& s = state();
    if (!s.indexDirty || !game::db::available()) return;
    Json list = Json::array();
    for (auto const& p : s.active) list.push_back({p.dim, p.x, p.y, p.z});
    if (list.empty()) game::db::erase(kIndexKey);
    else game::db::put(kIndexKey, list);
    s.indexDirty = false;
}

void loadIndex() {
    auto& s = state();
    s.active.clear();
    s.indexDirty = false;
    auto list    = game::db::get(kIndexKey);
    if (!list || !list->is_array()) return;
    for (auto const& e : *list) {
        if (e.is_array() && e.size() == 4) s.active.insert(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    }
}

void track(GlobalPos const& pos, bool holding) {
    auto& s       = state();
    bool  changed = holding ? s.active.insert(pos).second : s.active.erase(pos) > 0;
    if (changed) {
        s.indexDirty = true;
        saveIndex();
    }
}

void notify(GlobalPos const& pos) {
    for (auto const& fn : state().listeners) fn(pos);
}

/// The ingredient test of a DryingRecipe (Ingredient#test): an item of the list or of the tag.
bool accepts(recipes::Ingredient const& in, std::string const& itemId) {
    if (std::find(in.items.begin(), in.items.end(), itemId) != in.items.end()) return true;
    return !in.tag.empty() && tags::has(tags::Registry::Item, in.tag, itemId);
}

/// The time of a recipe (filter_time; the fixture spells it time).
int timeOf(recipes::CraftingRecipe const& r) {
    if (r.data.contains("filter_time") && r.data["filter_time"].is_number_integer()) return r.data["filter_time"].get<int>();
    if (r.data.contains("time") && r.data["time"].is_number_integer()) return r.data["time"].get<int>();
    return kDefaultDryingTime;
}

/// The state written to block data.
Json toDoc(RackState const& r) {
    Json d = Json::object();
    if (!game::items::isEmpty(r.item)) d["item"] = game::items::toJson(r.item);
    d["dry_time"]       = r.dryTime;
    d["total_dry_time"] = r.totalDryTime;
    d["drying"]         = r.drying;
    d["wet"]            = r.waterlogged;
    return d;
}

RackState fromDoc(Json const* d) {
    RackState r;
    if (!d || !d->is_object()) return r;
    if (d->contains("item")) r.item = game::items::fromJson((*d)["item"]);
    r.dryTime      = d->value("dry_time", 0);
    r.totalDryTime = d->value("total_dry_time", 0);
    r.drying       = d->value("drying", false);
    r.waterlogged  = d->value("wet", false);
    return r;
}

void writeDoc(BlockSource& region, BlockPos const& pos, RackState const& r) {
    bool empty = game::items::isEmpty(r.item);
    if (empty && !r.waterlogged && r.dryTime == 0) bd::set(region, pos, kKey, nullptr);
    else bd::set(region, pos, kKey, toDoc(r));
}

/// The rack's state: the live copy of a ticking rack, else its block data.
RackState load(BlockSource& region, BlockPos const& pos) {
    auto& live = state().live;
    if (auto it = live.find(game::globalPos(region, pos)); it != live.end()) return it->second.r;
    return fromDoc(rackDoc(region, pos));
}

/// Keeps the rack's state; `persist`: into block data now (an item or flag change), else only in
/// the live copy until the next write-back (dry_time counting up).
void store(BlockSource& region, BlockPos const& pos, RackState const& r, bool persist = true) {
    auto gp      = game::globalPos(region, pos);
    bool holding = !game::items::isEmpty(r.item);
    if (holding) state().live[gp] = Live{r, !persist};
    else state().live.erase(gp);
    if (persist) writeDoc(region, pos, r);
    track(gp, holding);
}

/// blockdata::onWriteBack: the live dry_time into block data (before a save, or before `pos` moves).
void writeBack(GlobalPos const* only) {
    for (auto& [gp, l] : state().live) {
        if (!l.dirty || (only && *only != gp)) continue;
        if (auto* region = game::regionOf(gp.dim)) {
            writeDoc(*region, game::blockPos(gp), l.r);
            l.dirty = false;
        }
    }
}

/// DryingRackBlockEntity#setTheItem: the stack (one item), and the timers when the item changed.
void setTheItem(BlockSource& region, BlockPos const& pos, RackState& r, ItemStack const& newItem) {
    bool update = game::items::isEmpty(newItem) || !game::items::sameItemSameData(r.item, newItem);
    r.item      = game::items::isEmpty(newItem) ? ItemStack{} : game::items::withCount(newItem, 1); // limitSize(1)
    if (update) {
        auto const* recipe = game::items::isEmpty(r.item) ? nullptr : recipeFor(r.item);
        r.totalDryTime     = recipe ? timeOf(*recipe) : kDefaultDryingTime;
        r.dryTime          = 0;
        r.drying           = !game::items::isEmpty(r.item) && !r.waterlogged && recipe != nullptr;
    }
    (void)region;
    (void)pos;
}

/// Java's addParticle of ParticleUtils.spawnParticleOnFace(level, pos, dir, DRYING_RACK, ZERO, 0.3).
void dryingParticle(BlockSource& region, BlockPos const& pos, int face) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    auto                                   step = faceStep(face);
    std::uniform_real_distribution<double> u(-0.5, 0.5);
    double x = pos.x + 0.5 + (step[0] == 0 ? u(rng()) : step[0] * 0.55);
    double y = pos.y + 0.5 + (step[1] == 0 ? u(rng()) : step[1] * 0.55);
    double z = pos.z + 0.5 + (step[2] == 0 ? u(rng()) : step[2] * 0.55);
    Vec3   vel{static_cast<float>(0.3 * step[0]), static_cast<float>(0.3 * step[1]), static_cast<float>(0.3 * step[2])};
    fx::particle(*dim, "twilightforest:drying_rack", Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}, vel);
}

/// One tick of one rack (DryingRackBlockEntity#tick); false when the rack is gone.
bool tickRack(GlobalPos const& gp) {
    auto* region = regionFor(gp);
    if (!region) return true; // the dimension is not loaded: keep it for later
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 0, false)) return true;
    Block const& block = region->getBlock(pos);
    if (!isRackId(idOf(block))) return false;
    RackState r = load(*region, pos);
    if (game::items::isEmpty(r.item)) return false;
    bool wet = waterSourceAt(*region, pos);
    if (wet != r.waterlogged) {
        // DryingRackBlock#onRemove on the WATERLOGGED change: the item drops, the rack stays
        r.waterlogged = wet;
        ItemStack item = r.item;
        setTheItem(*region, pos, r, ItemStack{});
        store(*region, pos, r);
        dropItemStack(*region, pos, item);
        log().info("woodwork: the drying rack at {} {} {} dropped its item ({})", pos.x, pos.y, pos.z, wet ? "waterlogged" : "drained");
        notify(gp);
        return true;
    }
    if (wet) return true; // no drying while waterlogged
    auto const* recipe  = recipeFor(r.item);
    bool        drying  = recipe != nullptr;
    bool        changed = drying != r.drying;
    r.drying            = drying;
    if (drying) {
        ++r.dryTime;
        if (r.dryTime >= r.totalDryTime) {
            // recipe.assemble: the result (setTheItem limits it to one)
            auto result = game::items::make(recipe->resultItem, std::max(1, recipe->resultCount));
            setTheItem(*region, pos, r, result ? *result : ItemStack{});
            changed = true;
            log().info("woodwork: the drying rack at {} {} {} dried {}", pos.x, pos.y, pos.z, recipe->id);
        }
        // DryingRackBlock#animateTick (client, every tick, 1 in 5): the particle on the facing face
        if (std::uniform_int_distribution<int>(0, 4)(rng()) == 0) {
            int face = faceOf(stateOf(region->getBlock(pos), "twilightforest:facing"));
            dryingParticle(*region, pos, face < 0 ? 2 : face);
        }
    }
    store(*region, pos, r, changed);
    if (changed) notify(gp);
    return true;
}

void tickAll() {
    auto& s = state();
    if (s.active.empty()) return;
    std::vector<GlobalPos> gone;
    std::vector<GlobalPos> racks(s.active.begin(), s.active.end());
    for (auto const& p : racks) {
        if (!tickRack(p)) gone.push_back(p);
    }
    for (auto const& p : gone) track(p, false);
}

// ---- interactions --------------------------------------------------------------------------------------

/// DryingRackBlock#getStateForPlacement: the player's horizontal direction.
void onPlace(ia::PlaceContext const& c) {
    auto* region = regionFor(c.pos);
    if (!region) return;
    BlockPos     pos   = game::blockPos(c.pos);
    Block const& block = region->getBlock(pos);
    if (!isRackId(idOf(block))) return;
    if (auto const* b = withStates(block, {{"twilightforest:facing", faceName(lookFace(c.player))}}); b && b != &block) game::setBlock(*region, pos, *b);
    RackState r;
    r.waterlogged = waterSourceAt(*region, pos);
    store(*region, pos, r);
}

/// DryingRackBlock#useItemOn.
ia::Result onUseOn(ia::UseOnContext const& c) {
    auto& region = c.player.getDimensionBlockSource();
    BlockPos pos = game::blockPos(c.pos);
    RackState r  = load(region, pos);
    bool emptyRack = game::items::isEmpty(r.item);
    bool emptyHand = game::items::isEmpty(c.item);
    if (emptyRack && emptyHand) return ia::Result::Pass; // SKIP_DEFAULT_BLOCK_INTERACTION
    if (c.sneaking && !emptyHand) return ia::Result::Pass; // Java: sneaking with an item skips the block's use
    if (repeatedUse(c.player, c.pos, c.firstEvent)) return ia::Result::Consume;
    r.waterlogged = waterSourceAt(region, pos);
    if (emptyRack) {
        // creative players keep their stack (copyWithCount(1)), others give one (split(1))
        ItemStack one = game::items::withCount(c.item, 1);
        setTheItem(region, pos, r, one);
        consumeOne(c.player, c.item);
        store(region, pos, r);
        playSound(region, pos, "block.twilightforest.drying_rack.add_item", 1.0f, 0.75f + randomFloat() * 0.5f);
        log().info("woodwork: {} put {} on the drying rack at {} {} {}", c.player.getRealName(), game::items::idOf(one), pos.x, pos.y, pos.z);
    } else {
        ItemStack item = r.item;
        setTheItem(region, pos, r, ItemStack{});
        store(region, pos, r);
        giveOrDrop(c.player, item);
        playSound(region, pos, "block.twilightforest.drying_rack.remove_item", 0.75f, 0.75f + randomFloat() * 0.5f);
        log().info("woodwork: {} took {} from the drying rack at {} {} {}", c.player.getRealName(), game::items::idOf(item), pos.x, pos.y, pos.z);
    }
    notify(c.pos);
    return ia::Result::Consume;
}

// ---- inventory handler (DryingRackHandler) --------------------------------------------------------------

inventory::Handler handler() {
    inventory::Handler h;
    h.slots = 1;
    h.get   = [](GlobalPos const& pos, int slot) -> ItemStack {
        auto* region = regionFor(pos);
        if (!region || slot != 0) return ItemStack{};
        return load(*region, game::blockPos(pos)).item;
    };
    // setStackInSlot
    h.set = [](GlobalPos const& pos, int slot, ItemStack const& stack) {
        auto* region = regionFor(pos);
        if (!region || slot != 0) return;
        BlockPos  bp = game::blockPos(pos);
        RackState r  = load(*region, bp);
        r.waterlogged = waterSourceAt(*region, bp);
        setTheItem(*region, bp, r, stack);
        store(*region, bp, r);
        notify(pos);
    };
    h.insert = [](GlobalPos const& pos, ItemStack const& stack, bool simulate) -> ItemStack {
        auto* region = regionFor(pos);
        if (!region || game::items::isEmpty(stack)) return stack;
        BlockPos  bp = game::blockPos(pos);
        RackState r  = load(*region, bp);
        if (!game::items::isEmpty(r.item)) return stack;
        if (!simulate) {
            r.waterlogged = waterSourceAt(*region, bp);
            setTheItem(*region, bp, r, game::items::withCount(stack, 1));
            store(*region, bp, r);
            notify(pos);
        }
        return game::items::withCount(stack, game::items::countOf(stack) - 1);
    };
    h.extract = [](GlobalPos const& pos, int slot, int count, bool simulate) -> ItemStack {
        auto* region = regionFor(pos);
        if (!region || slot != 0 || count <= 0) return ItemStack{};
        BlockPos  bp = game::blockPos(pos);
        RackState r  = load(*region, bp);
        if (r.drying) return ItemStack{};
        if (simulate || game::items::isEmpty(r.item)) return r.item;
        ItemStack item = r.item;
        setTheItem(*region, bp, r, ItemStack{});
        store(*region, bp, r);
        notify(pos);
        return item;
    };
    return h;
}

} // namespace

recipes::CraftingRecipe const* recipeFor(ItemStack const& item) {
    if (game::items::isEmpty(item)) return nullptr;
    auto id = game::items::idOf(item);
    return recipes::findFirst("twilightforest:drying", [&](recipes::CraftingRecipe const& r) {
        return !r.ingredients.empty() && accepts(r.ingredients.front(), id);
    });
}

std::optional<RackState> at(GlobalPos const& pos) {
    auto* region = regionFor(pos);
    if (!region) return std::nullopt;
    BlockPos bp = game::blockPos(pos);
    if (!isRackId(game::blockIdAt(*region, bp))) return std::nullopt;
    return load(*region, bp);
}

void onChange(ChangeFn fn) { state().listeners.push_back(std::move(fn)); }

std::vector<GlobalPos> activeRacks() { return {state().active.begin(), state().active.end()}; }

} // namespace gf::feature::woodwork::rack

namespace gf::feature::woodwork {

void installDryingRacks() {
    namespace r = rack;
    auto&       s    = r::state();
    static bool once = false;
    for (auto const& id : dryingRackIds()) {
        ia::Match m;
        m.block = id;
        s.handlers.push_back(ia::onPlace(m, r::onPlace));
        s.handlers.push_back(ia::onUseOn(m, r::onUseOn));
        inventory::registerBlock(id, r::handler());
    }
    if (!once) {
        once = true;
        game::db::onWorldReady([] { r::loadIndex(); });
        game::db::onWorldSave([] { r::saveIndex(); });
        game::db::onWorldClosed([] {
            r::state().active.clear();
            r::state().live.clear();
            r::state().indexDirty = false;
        });
        // DryingRackBlock#onRemove: the held item drops when the rack goes (block data is erased
        // when the block type changes)
        bd::onWriteBack([](GlobalPos const* pos) { r::writeBack(pos); });
        bd::onErase([](GlobalPos const& pos, Json const& doc, std::string const& previous) {
            if (previous.empty() || !r::isRackId(previous) || !doc.contains(r::kKey)) return;
            auto item = game::items::fromJson(doc[r::kKey].value("item", Json{}));
            if (game::items::isEmpty(item)) return;
            if (auto* region = r::regionFor(pos)) {
                dropItemStack(*region, game::blockPos(pos), item);
                log().info("woodwork: a removed drying rack at {} {} {} dropped {}", pos.x, pos.y, pos.z, game::items::idOf(item));
            }
            r::state().live.erase(pos);
            r::track(pos, false);
            r::notify(pos);
        });
    }
    if (game::db::available()) r::loadIndex();
    s.tick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { r::tickAll(); });
}

void uninstallDryingRacks() {
    auto& s = rack::state();
    for (auto id : s.handlers) ia::remove(id);
    s.handlers.clear();
    if (s.tick) ll::event::EventBus::getInstance().removeListener(s.tick);
    s.tick = nullptr;
    rack::saveIndex();
}

} // namespace gf::feature::woodwork

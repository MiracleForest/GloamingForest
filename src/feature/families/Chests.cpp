// Block families (F11): chests and trapped chests (Java ChestBlock / TrappedChestBlock /
// ChestBlockEntity / ChestLidController).
//
// The container is the inventory contract's (F10, spike S3: src/feature/inventory): tools/families
// declares every chest family block as a container (data/gf/containers/families.json; a unit's own
// declaration of the block wins), so the slots (each half its own 27), the screen (the client
// mod's "inventory.chest"; on vanilla clients the game's chest screen through a proxy, a form for a
// double chest), a double chest's 54 slots (ChestBlock#combine: the right half first), the open /
// close sounds, the opening stat (TrappedChestBlock: trigger_trapped_chest), the comparator output,
// the blocked rule (a redstone conductor or a sitting cat above), loot tables (LootTable#fill on the
// first access) and the contents a broken half drops are F10's.
// This file keeps what the chest block itself does:
// - facing and type on placement (ChestBlock#getStateForPlacement) and when a neighbour changes
//   (ChestBlock#updateShape): the pairs F10's screens follow;
// - the lid: gloamingforest:lid 0..4, the lid's angle in 22.5 degree steps, moved every tick as
//   ChestLidController's openness (0.1 per tick toward open while players look inside, drawn eased
//   as ChestRenderer: 1 - (1 - o)^3), from the openers the inventory contract counts
//   (feature/ext/combat/InventoryViewers.h; a double chest's halves count the same players);
// - the trapped chest's signal: gloamingforest:open_count = the openers (0..15; the BP's
//   redstone_producer, TrappedChestBlock#getSignal and signalOpenCount).
// After a reload nobody is inside: a chest whose lid or signal was left up closes in its chunk's
// first tick.

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "feature/common/Interactions.h"
#include "feature/ext/combat/InventoryViewers.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace ia = interactions;

namespace detail {
void onChunkLoaded(std::function<void(BlockSource&, LevelChunk&)> fn);
void forEachInChunk(BlockSource& region, LevelChunk& chunk, std::function<bool(BlockType const&)> const& want, std::function<void(BlockPos const&, Block const&)> const& fn);
} // namespace detail

namespace {

constexpr int kLidSteps      = 4;  ///< gloamingforest:lid 0..4 (22.5 degrees each)
constexpr int kOpennessTicks = 10; ///< ChestLidController: openness 0..1 by 0.1 per tick

bool isChest(Entry const* e) { return e && (e->family == Family::Chest || e->family == Family::TrappedChest); }

std::string typeOf(Block const& b) {
    auto t = strState(b, state::Type);
    return t.empty() ? "single" : t;
}

/// Java ChestBlock.getConnectedDirection.
int connectedDirection(Block const& b) {
    int f = facingOf(b);
    return typeOf(b) == "left" ? clockwise(f) : counterClockwise(f);
}

/// Java ChestBlock.candidatePartnerFacing: the facing of a single chest of the same kind there.
std::optional<int> candidatePartnerFacing(BlockSource& region, BlockPos const& pos, std::string const& id, int dir) {
    Block const* b = blockAt(region, offset(pos, dir));
    if (!b || b->getTypeName() != id || typeOf(*b) != "single") return std::nullopt;
    return facingOf(*b);
}

/// Java ChestBlock.updateShape for the neighbour on `dir`.
void updateChest(BlockSource& region, BlockPos const& pos, int dir) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b))) return;
    Block const* n    = blockAt(region, offset(pos, dir));
    Json         s    = Json::object();
    bool         same = n && n->getTypeName() == b->getTypeName();
    if (same && horizontal(dir)) {
        auto nt = typeOf(*n);
        if (typeOf(*b) == "single" && nt != "single" && facingOf(*b) == facingOf(*n) && connectedDirection(*n) == opposite(dir)) {
            s[state::Type] = nt == "left" ? "right" : "left";
        }
    } else if (typeOf(*b) != "single" && connectedDirection(*b) == dir) {
        s[state::Type] = "single";
    }
    if (s.empty()) return;
    if (auto const* next = withStates(*b, s); next && next != b) game::setBlock(region, pos, *next);
}

// ---- the lid and the trapped chest's signal ----------------------------------------------------------

/// A half's ChestLidController: its openness in tenths and whether players are inside.
struct Lid {
    int  openness = 0;
    bool open     = false;
};
/// The lids that move or stand open (a closed lid at rest is not kept).
std::map<GlobalPos, Lid>& lids() {
    static std::map<GlobalPos, Lid> m;
    return m;
}

/// ChestRenderer: the lid's angle of an openness (eased 1 - (1 - o)^3) in the kit's steps.
int lidStep(int openness) {
    double o     = static_cast<double>(std::clamp(openness, 0, kOpennessTicks)) / kOpennessTicks;
    double eased = 1.0 - std::pow(1.0 - o, 3);
    return std::clamp(static_cast<int>(std::lround(eased * kLidSteps)), 0, kLidSteps);
}

/// The smallest openness drawn at `step` (a lid found up, e.g. after a reload, closes from there).
int opennessAt(int step) {
    for (int o = 0; o <= kOpennessTicks; ++o)
        if (lidStep(o) >= step) return o;
    return kOpennessTicks;
}

/// The openers of the container at `pos` changed (feature/ext/combat/InventoryViewers.h):
/// ChestBlockEntity's ContainerOpenersCounter.openerCountChanged.
void viewersChanged(GlobalPos const& pos, int count) {
    auto* region = regionOf(pos.dim);
    if (!region) return;
    BlockPos     bp = game::blockPos(pos);
    Block const* b  = blockAt(*region, bp);
    auto const*  e  = b ? entryOf(*b) : nullptr;
    if (!isChest(e)) return;
    // TrappedChestBlockEntity.signalOpenCount: the signal follows the count at once
    if (e->family == Family::TrappedChest) setStates(*region, bp, {{state::OpenCount, std::clamp(count, 0, 15)}});
    // ChestBlockEntity.triggerEvent(1, count): the lid opens while players are inside
    auto [it, fresh] = lids().try_emplace(pos);
    if (fresh) it->second.openness = opennessAt(intState(*b, state::Lid).value_or(0));
    it->second.open = count > 0;
}

/// ChestLidController.tickLid of every lid that moves.
void tickLids() {
    for (auto it = lids().begin(); it != lids().end();) {
        auto const&  pos    = it->first;
        auto&        lid    = it->second;
        auto*        region = regionOf(pos.dim);
        BlockPos     bp     = game::blockPos(pos);
        Block const* b      = region ? blockAt(*region, bp) : nullptr;
        if (!b || !isChest(entryOf(*b))) {
            it = lids().erase(it); // broken or unloaded: nothing to draw
            continue;
        }
        lid.openness = lid.open ? std::min(lid.openness + 1, kOpennessTicks) : std::max(lid.openness - 1, 0);
        int step     = lidStep(lid.openness);
        if (intState(*b, state::Lid).value_or(0) != step) setStates(*region, bp, {{state::Lid, step}});
        if (!lid.open && lid.openness == 0) it = lids().erase(it);
        else ++it;
    }
}

/// A chunk's first tick after it loaded: nobody is inside its chests yet, so a lid or a trapped
/// chest's signal left up (saved while open) goes down.
void settleChunk(BlockSource& region, LevelChunk& chunk) {
    int                                           dim = static_cast<int>(region.getDimensionId());
    std::vector<std::pair<BlockPos, Block const*>> up;
    detail::forEachInChunk(region, chunk, [](BlockType const& t) { return isChest(entryOf(t)); }, [&](BlockPos const& p, Block const& b) {
        if (intState(b, state::Lid).value_or(0) > 0 || intState(b, state::OpenCount).value_or(0) > 0) up.emplace_back(p, &b);
    });
    for (auto const& [p, b] : up) {
        auto gp = game::globalPos(dim, p);
        if (lids().contains(gp) || inventory::viewers(gp) > 0) continue;
        if (intState(*b, state::OpenCount).value_or(0) > 0) setStates(region, p, {{state::OpenCount, 0}});
        if (int step = intState(*b, state::Lid).value_or(0); step > 0) lids()[gp] = Lid{opennessAt(step), false};
    }
}

} // namespace

std::optional<BlockPos> chestPartner(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b)) || typeOf(*b) == "single") return std::nullopt;
    BlockPos     p = offset(pos, connectedDirection(*b));
    Block const* o = blockAt(region, p);
    // DoubleBlockCombiner.combineWithNeigbour: the same block, the other type, the same facing
    if (!o || o->getTypeName() != b->getTypeName() || typeOf(*o) == "single" || typeOf(*o) == typeOf(*b) || facingOf(*o) != facingOf(*b)) return std::nullopt;
    return p;
}

void pairChest(BlockSource& region, BlockPos const& pos, int facing, bool sneaking, std::optional<int> clickedFace) {
    // Java ChestBlock.getStateForPlacement
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b))) return;
    std::string const& id   = b->getTypeName();
    std::string        type = "single";
    int                dir  = facing;
    if (clickedFace && horizontal(*clickedFace) && sneaking) {
        auto d = candidatePartnerFacing(region, pos, id, opposite(*clickedFace));
        if (d && axisOf(*d) != axisOf(*clickedFace)) {
            dir  = *d;
            type = counterClockwise(*d) == opposite(*clickedFace) ? "right" : "left";
        }
    }
    if (type == "single" && !sneaking) {
        if (auto d = candidatePartnerFacing(region, pos, id, clockwise(dir)); d && *d == dir) type = "left";
        else if (auto d2 = candidatePartnerFacing(region, pos, id, counterClockwise(dir)); d2 && *d2 == dir) type = "right";
    }
    if (auto const* next = withStates(*b, {{state::Facing, dirName(dir)}, {state::Type, type}}); next && next != b) game::setBlock(region, pos, *next);
}

int chestLidStep(int openness) { return lidStep(openness); }

namespace detail {
void placeChest(ia::PlaceContext const& c) {
    auto* region = regionOf(c.pos.dim);
    if (!region) return;
    // Java: the chest faces the player; sneaking against a chest's side joins that one
    pairChest(*region, game::blockPos(c.pos), opposite(lookDir(c.player)), c.sneaking, c.face);
    if (auto const* b = blockAt(*region, game::blockPos(c.pos))) log().info("families: {} placed at {} {} {} ({})", c.blockId, c.pos.x, c.pos.y, c.pos.z, typeOf(*b));
}
void chestsTick(int) { tickLids(); }
void clearChests() { lids().clear(); }
} // namespace detail

void installChests() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto family : {Family::Chest, Family::TrappedChest}) {
        for (auto const* e : entriesOf(family)) {
            inventory::onViewersChanged(e->id, viewersChanged);
            blockevents::onNeighborChanged(e->id, [](BlockSource& region, BlockPos const& pos, BlockPos const& from) {
                for (int d : kHorizontal) {
                    BlockPos n = offset(pos, d);
                    if (n.x == from.x && n.y == from.y && n.z == from.z) updateChest(region, pos, d);
                }
            });
        }
    }
    detail::onChunkLoaded(settleChunk);
}

void uninstallChests() { detail::clearChests(); }

} // namespace gf::feature::families

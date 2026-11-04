// Block families (F11): chests and trapped chests (Java ChestBlock / TrappedChestBlock /
// ChestBlockEntity / ChestLidController).
//
// The container is the inventory contract's (F10, spike S3: src/feature/inventory): tools/families
// declares every chest family block as a container (data/gf/containers/families.json; a unit's own
// declaration of the block wins), so the slots (each half its own 27), the screen (the client
// mod's "inventory.chest"; on vanilla clients the game's chest screen through a proxy, the game's
// large chest screen for a double chest), a double chest's 54 slots (ChestBlock#combine: the right
// half first), the open / close sounds, the opening stat (TrappedChestBlock: trigger_trapped_chest),
// the comparator output, the blocked rule (a redstone conductor or a sitting cat above), loot tables
// (LootTable#fill on the first access) and the contents a broken half drops are F10's.
// The slots are F10's native backend: tools/families gives the chests minecraft:block_entity
// (format 1.26.50, spike S7), so each half has a data-driven block actor holding a game container,
// saved with its chunk; Hoppers.cpp shows that container to hoppers, hopper minecarts and droppers.
// A chest saved without its block actor gets one, and stacks still in block data (placed while the
// chests had no block actor) move into its container (migrateChest, through F10's
// feature/ext/combat/InventoryMigrate.h), at the end of the tick its chunk loaded (ticking or not:
// the game's block actor chunk listener tells, BlockActorLevelListener::onChunkLoaded), in its
// chunk's first tick or a neighbouring chunk's (chunks loaded before the mod), before a hopper
// reads it (into the block actor it has: none is made while the game iterates block actors) and on
// the tick after a player opens it (no block actor is made inside the click). Until then the
// inventory contract reads the waiting stacks itself (its screens and its comparator output show
// each where a move would put it), and its first write moves them. Broken before that, it loses
// nothing: F10 drops its block data stacks with the block (blockdata::onErase) and the block
// actor's own (families.chest_migrate_removed).
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
#include <atomic>
#include <cmath>
#include <exception>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockActorLevelListener.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/common/Interactions.h"
#include "feature/ext/combat/InventoryMigrate.h"
#include "feature/ext/combat/InventoryViewers.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
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

/// The chests of `chunk`.
std::vector<BlockPos> chestsIn(BlockSource& region, LevelChunk& chunk) {
    std::vector<BlockPos> chests;
    detail::forEachInChunk(region, chunk, [](BlockType const& t) { return isChest(entryOf(t)); }, [&](BlockPos const& p, Block const&) { chests.push_back(p); });
    return chests;
}

/// The chunks (dimension, x, z) whose chests a neighbour's first tick settled in this world: every
/// chest of theirs has its block actor and no stacks in block data. A chunk with a chest left
/// waiting (no block actor yet: F10 reads its block data meanwhile) is not one of them, so the next
/// first tick of one of its neighbours tries again.
std::set<std::tuple<int, int, int>>& neighbourSettled() {
    static std::set<std::tuple<int, int, int>> s;
    return s;
}

/// Whether the chest at `pos` still waits for migrateChest: no block actor (native container) yet,
/// or stacks in block data (F10's feature/ext/combat/InventoryMigrate.h).
bool unsettled(BlockSource& region, BlockPos const& pos) {
    if (!inventory::nativeContainer(region, pos)) return true;
    return inventory::ext::hasBlockDataStacks(game::globalPos(region, pos));
}

/// The chests of chunk (cx, cz) that wait for migrateChest (the chunk's sections whose palette holds
/// no chest are skipped: a chunk without chests loads no block data).
std::vector<BlockPos> chestsWaitingIn(BlockSource& region, int cx, int cz) {
    auto* chunk = region.getChunk(cx, cz);
    if (!chunk) return {};
    auto out = chestsIn(region, *chunk);
    std::erase_if(out, [&](BlockPos const& p) { return !unsettled(region, p); });
    return out;
}

/// Moves the block data stacks of the chests of chunk (cx, cz) into their block actors (made
/// first with `makeActor`); the stacks moved and whether a chest still waits.
std::pair<int, bool> settleChestsOf(BlockSource& region, int cx, int cz, bool makeActor) {
    int  moved   = 0;
    bool waiting = false;
    for (auto const& p : chestsWaitingIn(region, cx, cz)) {
        moved += migrateChest(region, p, makeActor);
        waiting = waiting || unsettled(region, p);
    }
    return {moved, waiting};
}

/// The loaded chunks around (cx, cz) (that chunk excepted); `once`: only those not settled yet.
int settleAround(BlockSource& region, int cx, int cz, bool once) {
    int dim   = static_cast<int>(region.getDimensionId());
    int moved = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            auto key = std::make_tuple(dim, cx + dx, cz + dz);
            if (once && neighbourSettled().contains(key)) continue;
            if (!region.areChunksFullyLoaded(BlockPos{(cx + dx) * 16 + 8, 0, (cz + dz) * 16 + 8}, 0)) continue;
            auto [n, waiting] = settleChestsOf(region, cx + dx, cz + dz, false);
            moved += n;
            if (!waiting) neighbourSettled().insert(key);
        }
    return moved;
}

// ---- chunks that load (BlockActorLevelListener::onChunkLoaded) --------------------------------------

struct LoadedChunk {
    int          dim = 0, cx = 0, cz = 0;
    std::int64_t since = -1; ///< families::now() of the first tick that saw it (-1: none yet)
};

/// The chunks the game loaded since the last tick (the hook may run off the server thread: only
/// noted here, settled in the tick).
struct LoadedChunks {
    std::mutex               mutex;
    std::vector<LoadedChunk> chunks;
    std::atomic<long long>   seen{0}; ///< chunk loads noted (selftests)
};
LoadedChunks& loadedChunks() {
    static LoadedChunks l;
    return l;
}

/// The chunks loaded since the last tick: their chests with stacks still in block data get their
/// block actor (if the chunk was saved without it) and the stacks, at once, whether the chunk ticks
/// or not (beyond the simulation distance a chunk is loaded without ticking, while a comparator or a
/// hopper next to it, or any code through the inventory contract, reads its chests). A chunk not
/// fully loaded yet is tried again for a while.
void settleLoadedChunks() {
    std::vector<LoadedChunk> chunks;
    {
        auto&           l = loadedChunks();
        std::lock_guard lock(l.mutex);
        chunks.swap(l.chunks);
    }
    if (chunks.empty()) return;
    auto                     now = families::now();
    std::vector<LoadedChunk> later;
    for (auto c : chunks) {
        if (c.since < 0) c.since = now;
        auto* region = regionOf(c.dim);
        if (!region) continue;
        if (!region->areChunksFullyLoaded(BlockPos{c.cx * 16 + 8, 0, c.cz * 16 + 8}, 0)) {
            if (now - c.since < 100) later.push_back(c);
            continue;
        }
        (void)settleChestsOf(*region, c.cx, c.cz, true);
    }
    if (later.empty()) return;
    auto&           l = loadedChunks();
    std::lock_guard lock(l.mutex);
    l.chunks.insert(l.chunks.end(), later.begin(), later.end());
}

// ---- a hopper's or a comparator's read -------------------------------------------------------------------

/// The halves settled this tick (settleForAccess).
struct AccessMemo {
    std::int64_t        tick = -1;
    std::set<GlobalPos> settled;
};
AccessMemo& accessMemo() {
    static AccessMemo m;
    return m;
}

/// A chunk's first tick after it loaded: a chest whose stacks are still in block data moves them
/// into its block actor (migrateChest); nobody is inside its chests yet, so a lid or a trapped
/// chest's signal left up (saved while open) goes down. The loaded chunks around it (loaded before
/// the mod heard chunks load, they may not tick) have their chests' block data stacks moved too (into
/// a block actor the chunk already has: none is made there).
void settleChunk(BlockSource& region, LevelChunk& chunk) {
    int            dim = static_cast<int>(region.getDimensionId());
    ChunkPos const cp  = chunk.mPosition;
    settleAround(region, cp.x, cp.z, true);
    for (auto const& p : chestsIn(region, chunk)) {
        migrateChest(region, p);
        Block const* b = blockAt(region, p);
        if (!b || !isChest(entryOf(*b))) continue;
        if (intState(*b, state::Lid).value_or(0) == 0 && intState(*b, state::OpenCount).value_or(0) == 0) continue;
        auto gp = game::globalPos(dim, p);
        if (lids().contains(gp) || inventory::viewers(gp) > 0) continue;
        if (intState(*b, state::OpenCount).value_or(0) > 0) setStates(region, p, {{state::OpenCount, 0}});
        if (int step = intState(*b, state::Lid).value_or(0); step > 0) lids()[gp] = Lid{opennessAt(step), false};
    }
}

} // namespace

int migrateChest(BlockSource& region, BlockPos const& pos, bool makeActor) {
    Block const* b = blockAt(region, pos);
    if (!b || !isChest(entryOf(*b))) return 0;
    auto gp = game::globalPos(region, pos);
    if (!inventory::nativeContainer(region, pos)) {
        // the chunk was saved without the chest's block actor (the chests had none then; a state
        // change of the block does not make one): the game's factory makes the one its type
        // declares (with the inventory contract's container) and the chunk takes it
        if (!makeActor || region.getBlockEntity(pos) || b->getBlockType().getBlockEntityType() == BlockActorType::Undefined) return 0;
        auto* chunk = region.getChunkAt(pos);
        if (!chunk) return 0;
        if (auto actor = VanillaBlockActorFactory::createBlockActor(pos, b->getBlockType()); actor && actor->getType() == BlockActorType::DataDriven) {
            chunk->_placeBlockEntity(actor);
            region.fireBlockEntityChanged(*actor); // saved with the chunk
        }
        if (!inventory::nativeContainer(region, pos)) return 0; // F10 keeps reading its block data
        log().info("families: the chest at {} {} {} got its block actor", pos.x, pos.y, pos.z);
    }
    if (!inventory::ext::hasBlockDataStacks(gp)) return 0;
    // F10's move: each stack into its own slot when that is empty, else the first empty one; the
    // block data keeps what is not a slot (a pending loot table)
    if (makeActor) return inventory::ext::migrateBlockDataToNative(gp, true);
    // the lazy move (a hopper's look, maybe inside the game's block actor ticks): only when every
    // stack finds an empty slot; one that would drop waits for the end of the tick
    int moved = inventory::ext::migrateBlockDataToNative(gp, false);
    if (moved == 0 && inventory::ext::hasBlockDataStacks(gp))
        defer([gp] {
            if (auto* r = regionOf(gp.dim)) migrateChest(*r, game::blockPos(gp));
        });
    return moved;
}

int detail::settleNeighbourChests(BlockSource& region, int cx, int cz) { return settleAround(region, cx, cz, false); }

void detail::settleForAccess(BlockSource& region, GlobalPos const& pos) {
    auto& memo = accessMemo();
    if (auto now = families::now(); memo.tick != now) {
        memo.tick = now;
        memo.settled.clear();
    }
    if (!memo.settled.insert(pos).second) return;
    if (inventory::ext::hasBlockDataStacks(pos)) migrateChest(region, game::blockPos(pos), false);
    if (inventory::pendingLoot(pos)) inventory::unpackLoot(pos, nullptr);
}

void detail::chunkLoaded(int dim, int cx, int cz) {
    auto&           l = loadedChunks();
    std::lock_guard lock(l.mutex);
    l.chunks.push_back(LoadedChunk{dim, cx, cz, -1}); // (the tick is noted on the server thread)
    ++l.seen;
}

long long detail::chunkLoadsSeen() { return loadedChunks().seen.load(); }

void detail::settleLoadedChunksNow() { settleLoadedChunks(); }

std::optional<BlockPos> chestPairingNeighbour(Block const& block, BlockPos const& pos) {
    if (!isChest(entryOf(block)) || typeOf(block) == "single") return std::nullopt;
    return offset(pos, connectedDirection(block));
}

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
void chestsTick(int) {
    settleLoadedChunks();
    tickLids();
}
void clearChests() {
    lids().clear();
    neighbourSettled().clear();
    accessMemo() = AccessMemo{};
    auto&           l = loadedChunks();
    std::lock_guard lock(l.mutex);
    l.chunks.clear();
}
} // namespace detail

namespace {

/// A click on a chest: a half saved without its block actor, or whose stacks still wait in block
/// data, is settled on the next tick (no block actor is made and nothing moves inside
/// GameMode::useItemOn). Meanwhile the screen the inventory contract's own use handler opens for the
/// click shows the waiting stacks itself (F10's BlockData backend without a block actor, else the
/// native container with each waiting stack where the move puts it) and follows the move once it
/// happens (feature/ext/combat/InventoryChanges.h).
ia::Result useChest(ia::UseOnContext const& c) {
    if (!c.firstEvent || c.offhand) return ia::Result::Pass;
    auto* region = regionOf(c.pos.dim);
    if (!region) return ia::Result::Pass;
    auto parts = inventory::partsOf(c.pos);
    if (std::any_of(parts.begin(), parts.end(), [&](GlobalPos const& p) { return unsettled(*region, game::blockPos(p)); }))
        ia::nextTick([parts] {
            for (auto const& p : parts)
                if (auto* r = regionOf(p.dim)) migrateChest(*r, game::blockPos(p));
        });
    return ia::Result::Pass;
}

} // namespace

} // namespace gf::feature::families

// The game's block actor chunk listener hears every chunk that loads (BlockActor::onChunkLoaded of
// its block actors, ticking or not): the server's are noted for settleLoadedChunks.
LL_TYPE_INSTANCE_HOOK(
    FamiliesChunkLoadedHook,
    ll::memory::HookPriority::Normal,
    BlockActorLevelListener,
    &BlockActorLevelListener::$onChunkLoaded,
    void,
    ChunkSource& source,
    LevelChunk&  lc
) {
    origin(source, lc);
    try {
        ILevel& level = lc.mLevel;
        if (level.isClientSide()) return;
        Dimension&     dimension = lc.mDimension;
        ChunkPos const cp        = lc.mPosition;
        gf::feature::families::detail::chunkLoaded(static_cast<int>(dimension.getDimensionId()), cp.x, cp.z);
    } catch (...) {
        // (a chunk not noted is settled by its first tick or a neighbour's)
    }
}

namespace gf::feature::families {

void installChests() {
    FamiliesChunkLoadedHook::hook();
    static bool registered = false;
    if (registered) return;
    registered = true;
    for (auto family : {Family::Chest, Family::TrappedChest}) {
        for (auto const* e : entriesOf(family)) {
            inventory::onViewersChanged(e->id, viewersChanged);
            ia::onUseOn({.block = e->id}, useChest, ia::priority::High);
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

void uninstallChests() {
    FamiliesChunkLoadedHook::unhook();
    detail::clearChests();
}

} // namespace gf::feature::families

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// The hopper access dispatch of the "structure" contract extension (HopperAccess.h): the engine
// hooks on Bedrock's hopper (outside the W0 contracts; disclosed for A8) and the watched positions.
#include "feature/ext/structure/HopperAccess.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/world/Container.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/block/HopperBlock.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/item/ItemStack.h"

#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::structure::ext::hoppers {

namespace {

// ---- the watched positions ---------------------------------------------------------------------------

/// A counting filter of the watched positions (x, y, z hashed, every dimension in one): the hooks
/// test it without a lock and look the exact map up only when the position's counter is not 0.
constexpr std::size_t                                 kFilterSize = 1u << 12;
std::array<std::atomic<std::uint32_t>, kFilterSize> gFilter{};

std::size_t filterSlot(int x, int y, int z) {
    std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 0xC2B2AE3D27D4EB4Full;
    h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) * 0x165667B19E3779F9ull;
    h ^= h >> 31;
    return static_cast<std::size_t>(h & (kFilterSize - 1));
}

bool mayBeWatched(BlockPos const& pos) { return gFilter[filterSlot(pos.x, pos.y, pos.z)].load(std::memory_order_acquire) != 0; }

/// (x, y, z, dim) -> watch count (x, y, z first: the container hooks look every dimension up).
using Key = std::tuple<int, int, int, int>;
std::mutex          gMutex;
std::map<Key, int>  gWatched;
std::vector<AccessFn>& listeners() {
    static std::vector<AccessFn> l;
    return l;
}

/// The dimensions with a watch at (x, y, z).
std::vector<int> watchedDims(BlockPos const& pos) {
    std::vector<int> dims;
    std::lock_guard  lock(gMutex);
    for (auto it = gWatched.lower_bound(Key{pos.x, pos.y, pos.z, std::numeric_limits<int>::min()});
         it != gWatched.end() && std::get<0>(it->first) == pos.x && std::get<1>(it->first) == pos.y && std::get<2>(it->first) == pos.z; ++it)
        dims.push_back(std::get<3>(it->first));
    return dims;
}

// ---- the dispatch ------------------------------------------------------------------------------------

/// The server thread (noted on every server tick, at install on BDS and by a watched server
/// hopper's tick): listeners run there only.
std::atomic<std::thread::id> gServerThread{};
void                         noteServerThread() { gServerThread.store(std::this_thread::get_id(), std::memory_order_relaxed); }
bool                         onServerThread() { return std::this_thread::get_id() == gServerThread.load(std::memory_order_relaxed); }

/// Set while listeners run on this thread: the accesses they cause are not dispatched again.
thread_local bool tDispatching = false;
/// The hopper whose own tick runs on this thread now: its own reads and writes are not Container
/// accesses.
thread_local void const* tOwnTick = nullptr;

void dispatch(BlockSource& region, BlockPos const& pos, Access access) {
    struct Guard {
        bool outer = tDispatching;
        Guard() { tDispatching = true; }
        ~Guard() { tDispatching = outer; }
    } guard;
    for (auto const& fn : listeners()) {
        try {
            fn(region, pos, access);
        } catch (std::exception const& e) {
            log().error("hopper access listener failed at {} {} {}: {}", pos.x, pos.y, pos.z, e.what());
        }
    }
}

/// A hopper at `pos` of `region` (its tick, removal, comparator reading).
void atPosition(BlockSource& region, BlockPos const& pos, Access access) {
    if (tDispatching || !mayBeWatched(pos)) return;
    if (!onServerThread()) {
        // a server level's block actor ticks on the server thread (before the world's first
        // server tick noted it); anything else is not dispatched
        if (access != Access::Tick || region.getILevel().isClientSide()) return;
        noteServerThread();
    }
    if (!watched(static_cast<int>(region.getDimensionId()), pos)) return;
    dispatch(region, pos, access);
}

/// A hopper's container (no BlockSource: every dimension with a watch at the actor's position whose
/// block actor it is).
void atContainer(HopperBlockActor& actor) {
    if (tDispatching || tOwnTick == &actor) return;
    BlockPos const& pos = actor.getPosition();
    if (!mayBeWatched(pos) || !onServerThread() || actor.getType() != BlockActorType::Hopper) return;
    for (int dim : watchedDims(pos)) {
        auto* region = game::regionOf(dim);
        if (!region || region->getBlockEntity(pos) != &actor) continue;
        dispatch(*region, pos, Access::Container);
    }
}

} // namespace

// ---- the hooks -----------------------------------------------------------------------------------------
//   - HopperBlockActor's Container#getItem / #setItem: Container is HopperBlockActor's second base
//     and declares them, so the hooks get the Container subobject as `this` (thisFor<Container>).
//     Bedrock's hopper transfers, Container#isEmpty / #removeItem / #getSlots /
//     #dropContents and the comparator's slot reads go through them.
//   - HopperBlockActor#tick: the Tick access, then the hopper's own reads and writes are marked.
//   - HopperBlock#onRemove: before Bedrock drops the contents.
//   - HopperBlock#getComparatorSignal: before it reads the slots.

LL_TYPE_INSTANCE_HOOK(HopperAccessTickHook, ll::memory::HookPriority::Normal, HopperBlockActor, &HopperBlockActor::$tick, void, ::BlockSource& region) {
    atPosition(region, getPosition(), Access::Tick);
    struct OwnTick {
        void const* outer = tOwnTick;
        explicit OwnTick(void const* self) { tOwnTick = self; }
        ~OwnTick() { tOwnTick = outer; }
    } own(static_cast<HopperBlockActor const*>(this));
    origin(region);
}

LL_TYPE_INSTANCE_HOOK(HopperAccessGetItemHook, ll::memory::HookPriority::Normal, HopperBlockActor, &HopperBlockActor::$getItem, ::ItemStack const&, int slot) {
    atContainer(*thisFor<Container>());
    return origin(slot);
}

LL_TYPE_INSTANCE_HOOK(
    HopperAccessSetItemHook,
    ll::memory::HookPriority::Normal,
    HopperBlockActor,
    &HopperBlockActor::$setItem,
    void,
    int                slot,
    ::ItemStack const& item
) {
    atContainer(*thisFor<Container>());
    origin(slot, item);
}

LL_TYPE_INSTANCE_HOOK(
    HopperAccessRemoveHook,
    ll::memory::HookPriority::Normal,
    HopperBlock,
    &HopperBlock::$onRemove,
    void,
    ::BlockSource&    region,
    ::BlockPos const& pos
) {
    atPosition(region, pos, Access::Removed);
    origin(region, pos);
}

LL_TYPE_INSTANCE_HOOK(
    HopperAccessComparatorHook,
    ll::memory::HookPriority::Normal,
    HopperBlock,
    &HopperBlock::$getComparatorSignal,
    int,
    ::BlockSource&    region,
    ::BlockPos const& pos,
    ::Block const&    block,
    uchar             dir
) {
    atPosition(region, pos, Access::Comparator);
    return origin(region, pos, block, dir);
}

// ---- the API -------------------------------------------------------------------------------------------

void onAccess(AccessFn fn) { listeners().push_back(std::move(fn)); }

void watch(int dim, BlockPos const& pos) {
    std::lock_guard lock(gMutex);
    if (++gWatched[Key{pos.x, pos.y, pos.z, dim}] == 1) gFilter[filterSlot(pos.x, pos.y, pos.z)].fetch_add(1, std::memory_order_acq_rel);
}

void unwatch(int dim, BlockPos const& pos) {
    std::lock_guard lock(gMutex);
    auto            it = gWatched.find(Key{pos.x, pos.y, pos.z, dim});
    if (it == gWatched.end()) return;
    if (--it->second > 0) return;
    gWatched.erase(it);
    gFilter[filterSlot(pos.x, pos.y, pos.z)].fetch_sub(1, std::memory_order_acq_rel);
}

bool watched(int dim, BlockPos const& pos) {
    std::lock_guard lock(gMutex);
    return gWatched.contains(Key{pos.x, pos.y, pos.z, dim});
}

std::vector<std::array<int, 4>> watchedPositions() {
    std::vector<std::array<int, 4>> out;
    std::lock_guard                 lock(gMutex);
    out.reserve(gWatched.size());
    for (auto const& [key, n] : gWatched) out.push_back({std::get<3>(key), std::get<0>(key), std::get<1>(key), std::get<2>(key)});
    return out;
}

} // namespace gf::structure::ext::hoppers

namespace gf::feature::structure {

/// Installs the hopper hooks and notes the server thread (every server tick).
class HopperAccessModule {
public:
    void install() {
        namespace h = gf::structure::ext::hoppers;
        // (BDS enables mods on the server thread with the world already there; the client's local
        // worlds are noted by their first server tick or a watched hopper's tick)
#ifndef GF_CLIENT
        if (game::db::available()) h::noteServerThread();
#endif
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { h::noteServerThread(); });
        h::HopperAccessTickHook::hook();
        h::HopperAccessGetItemHook::hook();
        h::HopperAccessSetItemHook::hook();
        h::HopperAccessRemoveHook::hook();
        h::HopperAccessComparatorHook::hook();
    }
    void uninstall() {
        namespace h = gf::structure::ext::hoppers;
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
        h::HopperAccessTickHook::unhook();
        h::HopperAccessGetItemHook::unhook();
        h::HopperAccessSetItemHook::unhook();
        h::HopperAccessRemoveHook::unhook();
        h::HopperAccessComparatorHook::unhook();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace gf::feature::structure

GF_REGISTER_MODULE_ORDERED(gf::feature::structure::HopperAccessModule, gf::ModuleOrder::Contracts);

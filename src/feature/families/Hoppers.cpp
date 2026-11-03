// Block families (F11): what hoppers, hopper minecarts and droppers see of a chest (Java's
// HopperBlockEntity#getContainerAt -> ChestBlock#getContainer(state, level, pos, true): the chest
// block entity's container, a double chest's CompoundContainer of both halves (DoubleBlockCombiner:
// the right half first), the blocked rule ignored; RandomizableContainerBlockEntity rolls a pending
// loot table on any access of its slots).
//
// TF's chests are vanilla ChestBlock / TrappedChestBlock in Java, so hoppers, hopper minecarts and
// droppers fill and empty them. Here a chest keeps its slots in the inventory contract's native
// backend (F10, spike S3): the game container F10 gives the data-driven block actor that
// tools/families declares with minecraft:block_entity. The game's hopper and dropper ask a block
// actor for its container through the vanilla main component, which a data-driven block actor does
// not have, so they would see nothing there: three hooks hand them the native container instead
// (Hopper::_getContainerInBlock and Hopper::_getContainersAt for hoppers and hopper minecarts,
// DropperBlockActor::getContainerAt for droppers). Only blocks hoppers may reach get one
// (feature/ext/families/Hoppers.h: by default the chest-like declarations, TF's chests among them;
// other units opt their Container block entities in); a block kept in block data (no
// minecraft:block_entity) stays invisible to them.
//
// Java's ChestMenu shows a hopper's change at once (AbstractContainerMenu#broadcastChanges every
// tick). The container a hopper gets is a view that notes the halves it changed; after the tick the
// client mod's chest screens showing one of them get fresh data (the screens contract's refresh of
// the inventory contract's "inventory.chest" screen; the game's own chest screen of a vanilla client
// follows through F10's proxy already).
//
// A lookup runs for every hopper next to a chest on every tick it looks, so it stays cheap: what it
// found at a position (whether hoppers reach the block, the halves) is kept while the block and the
// neighbour its pairing depends on stay the same, and block data is read only for a key that is
// there (stacks not yet moved into the block actor, a pending loot table).

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"

#include "feature/common/BlockData.h"
#include "feature/common/Screens.h"
#include "feature/ext/families/Hoppers.h"
#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "mod/Log.h"

// ---- which blocks hoppers reach (feature/ext/families/Hoppers.h) ---------------------------------------

namespace {

struct Visibility {
    std::mutex                  mutex;
    std::map<std::string, bool> set;           ///< setHopperVisible
    std::atomic<unsigned>       generation{0}; ///< bumped on every change (the lookups below are redone)
};

Visibility& visibility() {
    static Visibility v;
    return v;
}

} // namespace

namespace gf::families_ext {

void setHopperVisible(std::string const& blockId, bool visible) {
    auto& v = visibility();
    {
        std::lock_guard lock(v.mutex);
        v.set[blockId] = visible;
    }
    ++v.generation;
}

void resetHopperVisible(std::string const& blockId) {
    auto& v = visibility();
    {
        std::lock_guard lock(v.mutex);
        v.set.erase(blockId);
    }
    ++v.generation;
}

bool hopperVisible(std::string const& blockId) {
    {
        auto&           v = visibility();
        std::lock_guard lock(v.mutex);
        if (auto it = v.set.find(blockId); it != v.set.end()) return it->second;
    }
    auto d = gf::inventory::declared(blockId);
    return d && d->chest;
}

} // namespace gf::families_ext

namespace gf::feature::families {

namespace {

/// Java's CompoundContainer over the two halves of a double chest (slots 0..26 the first (right)
/// half's, 27..53 the second's), or one chest's own container (no second half). Every write notes
/// the half it changed (screens to refresh). The halves' containers are the native ones of this
/// tick (set again on every lookup; nobody keeps the view across ticks).
class ChestView final : public Container {
public:
    ChestView() : Container(SharedTypes::Legacy::ContainerType::Container) {}

    Container*   first  = nullptr;
    Container*   second = nullptr;
    GlobalPos    firstPos, secondPos;
    std::int64_t lastUsed = 0; ///< the tick of the last lookup that handed it out

    ItemStack const& getItem(int slot) const override {
        auto [c, i] = part(slot);
        return c ? c->getItem(i) : ItemStack::EMPTY_ITEM();
    }
    void setItem(int slot, ItemStack const& item) override {
        if (auto [c, i] = part(slot); c) {
            c->setItem(i, item);
            changed(c);
        }
    }
    void removeItem(int slot, int count) override {
        if (auto [c, i] = part(slot); c) {
            c->removeItem(i, count);
            changed(c);
        }
    }
    int getContainerSize() const override { return sizeOf(first) + sizeOf(second); }
    /// CompoundContainer#getMaxStackSize: the first container's.
    int  getMaxStackSize() const override { return first ? first->getMaxStackSize() : 64; }
    void startOpen(Actor&) override {}
    void stopOpen(Actor&) override {}
    bool canPushInItem(int slot, int face, ItemStack const& item) const override {
        auto [c, i] = part(slot);
        return c && c->canPushInItem(i, face, item);
    }
    bool canPullOutItem(int slot, int face, ItemStack const& item) const override {
        auto [c, i] = part(slot);
        return c && c->canPullOutItem(i, face, item);
    }
    void setContainerChanged(int slot) override {
        if (auto [c, i] = part(slot); c) c->setContainerChanged(i);
    }
    void serverInitItemStackIds(int slot, int count, std::function<void(int, ItemStack const&)> onNetIdChanged) override {
        for (int s = std::max(0, slot); s < slot + count && s < getContainerSize(); ++s) {
            auto [c, i] = part(s);
            if (!c) continue;
            int offset = s - i;
            c->serverInitItemStackIds(i, 1, [onNetIdChanged, offset](int at, ItemStack const& stack) {
                if (onNetIdChanged) onNetIdChanged(at + offset, stack);
            });
        }
    }

private:
    static int sizeOf(Container const* c) { return c ? c->getContainerSize() : 0; }
    /// The half and its slot of a compound slot.
    [[nodiscard]] std::pair<Container*, int> part(int slot) const {
        if (slot < 0) return {nullptr, 0};
        int n = sizeOf(first);
        if (slot < n) return {first, slot};
        if (slot - n < sizeOf(second)) return {second, slot - n};
        return {nullptr, 0};
    }
    void changed(Container const* c) const;
};

/// The views handed out (by the first half's position). Dropped between ticks once unused for a
/// while (a hopper holds one only during its own transfer).
std::map<GlobalPos, std::unique_ptr<ChestView>>& views() {
    static std::map<GlobalPos, std::unique_ptr<ChestView>> m;
    return m;
}

/// The halves hoppers changed this tick (their client mod chest screens are refreshed after it).
std::set<GlobalPos>& changedHalves() {
    static std::set<GlobalPos> s;
    return s;
}

void ChestView::changed(Container const* c) const { changedHalves().insert(c == first ? firstPos : secondPos); }

/// What a lookup found at a position, kept while it still holds.
struct Seen {
    Block const*            self = nullptr;     ///< the block at the position
    std::optional<BlockPos> neighbour;          ///< the block its pairing depends on (a chest half)
    Block const*            neighbourBlock = nullptr;
    bool                    exact      = false; ///< a family chest: the blocks above are all it depends on
    std::int64_t            tick       = 0;     ///< made then (other blocks: valid for that tick only)
    unsigned                generation = 0;     ///< the visibility settings it saw
    bool                    visible    = false; ///< hoppers reach the block (feature/ext/families/Hoppers.h)
    std::vector<GlobalPos>  parts;              ///< the inventory contract's halves, in slot order
};

std::map<GlobalPos, Seen>& seen() {
    static std::map<GlobalPos, Seen> m;
    return m;
}

/// The players' last used chest half (the context the inventory contract opened their screen for).
std::map<std::int64_t, GlobalPos>& usedChests() {
    static std::map<std::int64_t, GlobalPos> m;
    return m;
}

bool isChestEntry(Entry const* e) { return e && (e->family == Family::Chest || e->family == Family::TrappedChest); }

/// The lookup of `gp` (whose block is `block`), redone when something it depends on changed.
Seen const& lookup(BlockSource& region, BlockPos const& pos, GlobalPos const& gp, Block const& block) {
    auto     now        = families::now();
    unsigned generation = visibility().generation.load();
    auto&    m          = seen();
    if (auto it = m.find(gp); it != m.end()) {
        auto const& s     = it->second;
        bool        valid = s.self == &block && s.generation == generation;
        if (valid && s.exact) valid = !s.neighbour || &region.getBlock(*s.neighbour) == s.neighbourBlock;
        else if (valid) valid = s.tick == now;
        if (valid) return s;
    }
    Seen s;
    s.self       = &block;
    s.tick       = now;
    s.generation = generation;
    s.visible    = families_ext::hopperVisible(std::string(block.getTypeName()));
    if (!s.visible) {
        s.exact = true; // nothing more to know until the block or the settings change
    } else {
        s.parts = inventory::partsOf(gp);
        if (isChestEntry(entryOf(block))) {
            // inventory::partsOf of a family chest: its own states and the block on its connected side
            s.exact = true;
            if ((s.neighbour = chestPairingNeighbour(block, pos))) s.neighbourBlock = &region.getBlock(*s.neighbour);
        }
    }
    return m[gp] = std::move(s);
}

/// A half's block data still holds what its block actor should: its stacks (moved now, without
/// making a block actor: the game may be iterating the chunk's block actors) and a pending loot
/// table (rolled now: RandomizableContainer#unpackLootTable(null) on a hopper's access).
void settleHalf(BlockSource& region, GlobalPos const& p) {
    auto bp      = game::blockPos(p);
    bool stacks  = false, loot = false;
    if (auto const* data = blockdata::get(region, bp, kInventoryDataKey); data && data->is_object()) {
        stacks = data->contains("items");
        loot   = data->contains("loot");
    }
    if (stacks) migrateChest(region, bp, false);
    if (loot) inventory::unpackLoot(p, nullptr);
}

} // namespace

Container* hopperContainerAt(BlockSource& region, BlockPos const& pos) {
    if (region.getLevel().isClientSide()) return nullptr;
    // a data-driven block actor (minecraft:block_entity) of a block hoppers may reach
    auto* actor = region.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::DataDriven) return nullptr;
    auto        gp = game::globalPos(region, pos);
    auto const& s  = lookup(region, pos, gp, region.getBlock(pos));
    if (!s.visible || s.parts.empty()) return nullptr;
    // ChestBlock#getContainer(.., ignoreBlocked = true): the halves of a declared double chest
    auto parts = s.parts;
    for (auto const& p : parts) settleHalf(region, p);
    std::vector<Container*> halves;
    for (auto const& p : parts) halves.push_back(inventory::nativeContainer(region, game::blockPos(p)));
    std::size_t self = 0;
    while (self < parts.size() && !(parts[self] == gp)) ++self;
    if (self == parts.size() || !halves[self]) return nullptr;
    bool  both = parts.size() == 2 && halves[0] && halves[1]; // else each half on its own
    auto& view = views()[both ? parts[0] : gp];
    if (!view) view = std::make_unique<ChestView>();
    view->first     = both ? halves[0] : halves[self];
    view->second    = both ? halves[1] : nullptr;
    view->firstPos  = both ? parts[0] : gp;
    view->secondPos = both ? parts[1] : gp;
    view->lastUsed  = families::now();
    return view.get();
}

void chestUsedBy(Player& player, GlobalPos const& pos) { usedChests()[player.getOrCreateUniqueID().rawID] = pos; }

namespace detail {

/// After a tick: the client mod's chest screens that show a half hoppers changed get fresh data
/// (Java's ChestMenu#broadcastChanges). The screen's context is the position the player opened it
/// at: a single chest's own, for a double chest the half the player used (chestUsedBy, kept only
/// while that player has a chest screen open, so a screen code opened later has none); without one
/// of the shown halves there the screen waits for its next click (a wrong context would close it).
void refreshHopperScreens() {
    auto level = ll::service::getLevel();
    // a use that opened no screen (refused, blocked, an item placed against the chest), or a screen
    // closed since: the half noted no longer names a screen (a later one opened by code has none)
    if (level)
        std::erase_if(usedChests(), [&](auto const& e) {
            ActorUniqueID id;
            id.rawID     = e.first;
            auto* player = level->getPlayer(id);
            return !player || inventory::openedParts(*player).empty();
        });
    if (changedHalves().empty()) return;
    auto changed = std::move(changedHalves());
    changedHalves().clear();
    // only halves somebody has open
    std::erase_if(changed, [](GlobalPos const& p) { return inventory::viewerCount(p) == 0; });
    if (!level || changed.empty()) return;
    auto shows = [](std::vector<GlobalPos> const& parts, GlobalPos const& p) { return std::any_of(parts.begin(), parts.end(), [&](GlobalPos const& q) { return q == p; }); };
    std::vector<std::int64_t> players;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& player) {
        if (gf::net::isCompanion(player)) players.push_back(player.getOrCreateUniqueID().rawID);
        return true;
    }));
    for (auto uid : players) {
        ActorUniqueID id;
        id.rawID     = uid;
        auto* player = level->getPlayer(id);
        if (!player) continue;
        auto parts = inventory::openedParts(*player);
        if (parts.empty() || std::none_of(changed.begin(), changed.end(), [&](GlobalPos const& p) { return shows(parts, p); })) continue;
        std::optional<GlobalPos> at;
        if (parts.size() == 1) at = parts[0];
        else if (auto it = usedChests().find(uid); it != usedChests().end() && shows(parts, it->second)) at = it->second;
        if (!at) continue;
        screens::refresh(*player, "inventory.chest", {{"pos", {at->dim, at->x, at->y, at->z}}});
    }
}

/// Between ticks: the screens hoppers changed are refreshed; views unused for a second go, and the
/// kept lookups are redone now and then (a container registered or declared later).
void hoppersTick() {
    refreshHopperScreens();
    auto now = families::now();
    if (now % 20 == 0) std::erase_if(views(), [now](auto const& e) { return now - e.second->lastUsed > 20; });
    if (now % 100 == 0 || seen().size() > 4096) seen().clear();
}

void clearHopperViews() {
    views().clear();
    seen().clear();
    changedHalves().clear();
    usedChests().clear();
}

} // namespace detail

} // namespace gf::feature::families

namespace {

/// _getContainersAt calls _getContainerInBlock itself (or not, when the compiler folded it in):
/// the outer hook adds the block's container only when the inner one did not run.
thread_local int  containersAtDepth = 0;
thread_local bool blockLookupSeen   = false;

Container* familiesContainerAt(BlockSource& region, Vec3 const& pos) {
    try {
        return gf::feature::families::hopperContainerAt(region, gf::game::blockPosOf(pos));
    } catch (std::exception const& e) {
        gf::log().error("families: hopper container lookup failed: {}", e.what());
        return nullptr;
    }
}

} // namespace

// A hopper (block or minecart) looks for the container of a block it pushes into or pulls from.
LL_TYPE_INSTANCE_HOOK(
    FamiliesHopperContainerInBlockHook,
    ll::memory::HookPriority::Normal,
    Hopper,
    &Hopper::_getContainerInBlock,
    Container*,
    BlockSource& region,
    Vec3 const&  pos
) {
    if (containersAtDepth > 0) blockLookupSeen = true;
    if (auto* c = origin(region, pos)) return c;
    return familiesContainerAt(region, pos);
}

// Every container at a position (the block's, then entities'): the block's when the lookup above
// did not run inside it.
LL_TYPE_INSTANCE_HOOK(
    FamiliesHopperContainersAtHook,
    ll::memory::HookPriority::Normal,
    Hopper,
    &Hopper::_getContainersAt,
    std::vector<Container*>,
    BlockSource& region,
    Vec3 const&  pos
) {
    ++containersAtDepth;
    bool outerSeen  = blockLookupSeen;
    blockLookupSeen = false;
    std::vector<Container*> out;
    try {
        out = origin(region, pos);
    } catch (...) {
        --containersAtDepth;
        blockLookupSeen = outerSeen;
        throw;
    }
    bool seen = blockLookupSeen;
    --containersAtDepth;
    blockLookupSeen = outerSeen;
    if (!seen) {
        if (auto* c = familiesContainerAt(region, pos); c && std::find(out.begin(), out.end(), c) == out.end()) out.insert(out.begin(), c);
    }
    return out;
}

// A dropper looks for the container in front of it (Java DropperBlock#dispenseFrom:
// HopperBlockEntity.getContainerAt).
LL_STATIC_HOOK(
    FamiliesDropperContainerHook,
    ll::memory::HookPriority::Normal,
    &DropperBlockActor::getContainerAt,
    Container*,
    BlockSource& region,
    Vec3 const&  pos
) {
    if (auto* c = origin(region, pos)) return c;
    return familiesContainerAt(region, pos);
}

namespace gf::feature::families {

void installHoppers() {
    FamiliesHopperContainerInBlockHook::hook();
    FamiliesHopperContainersAtHook::hook();
    FamiliesDropperContainerHook::hook();
}

void uninstallHoppers() {
    FamiliesDropperContainerHook::unhook();
    FamiliesHopperContainersAtHook::unhook();
    FamiliesHopperContainerInBlockHook::unhook();
    detail::clearHopperViews();
}

} // namespace gf::feature::families

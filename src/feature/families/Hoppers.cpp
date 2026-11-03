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
// Java's ChestMenu shows every change at once to every player looking inside
// (AbstractContainerMenu#broadcastChanges every tick: a hopper's, another player's, any code's).
// While anyone looks inside a TF chest half, a change listener sits on its native container (the
// game's Container content change listeners: every write of a slot calls them), and the container a
// hopper gets is a view that notes the halves it changed; at the end of the tick a client mod chest
// screen showing a changed half gets fresh data (the screens contract's refresh of the inventory
// contract's "inventory.chest" screen; the game's own chest screen of a vanilla client follows
// through F10's proxy already). A half without a native container yet (its stacks still in block
// data) is compared with the last tick's slots instead. The refresh names the screen's context, the
// position the inventory contract opened the screen for: a single chest's own; for a double chest
// the half the player clicked, taken only when the inventory contract's use handler opened the
// screen for that click (the openers count rises during the click). A double chest screen opened
// by code has no context known here and is not refreshed (a wrong context would close it; only the
// inventory contract knows the position).
//
// A lookup runs for every hopper next to a chest on every tick it looks, so it stays cheap: what it
// found at a position (whether hoppers reach the block, the halves and their containers) is kept
// while the block, the neighbour its pairing depends on and the inventory contract's declaration of
// the block (checked once a tick per block type) stay the same; a half's container is handed out
// again while the live block actor there still holds it (compared, never dereferenced from the
// cache), and a half's block data (stacks not yet moved into the block actor, a pending loot table)
// is read once a tick (Chests.cpp settleForAccess).

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
#include "mc/world/ContainerContentChangeListener.h"
#include "mc/world/DataDrivenBlockContainer.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"
#include "mc/world/level/block/actor/component/ContainerBlockActorComponent.h"

#include "feature/common/Screens.h"
#include "feature/ext/families/Hoppers.h"
#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
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

bool contains(std::vector<GlobalPos> const& parts, GlobalPos const& p) {
    // (no std::find: MSVC's vectorised find refuses a 16-byte element)
    return std::any_of(parts.begin(), parts.end(), [&](GlobalPos const& q) { return q == p; });
}

bool sameParts(std::vector<GlobalPos> const& a, std::vector<GlobalPos> const& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!(a[i] == b[i])) return false;
    return true;
}

Player* playerById(std::int64_t uid) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->getPlayer(id);
}

/// The halves whose slots changed this tick (their client mod chest screens are refreshed after it):
/// the change listeners' and the hopper views'.
std::set<GlobalPos>& changedHalves() {
    static std::set<GlobalPos> s;
    return s;
}

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
    void changed(Container const* c) const { changedHalves().insert(c == first ? firstPos : secondPos); }
};

/// The views handed out (by the first half's position). Dropped between ticks once unused for a
/// while (a hopper holds one only during its own transfer).
std::map<GlobalPos, std::unique_ptr<ChestView>>& views() {
    static std::map<GlobalPos, std::unique_ptr<ChestView>> m;
    return m;
}

/// One container a lookup found (a single chest, or a half of a double chest): its position and
/// the native container it resolved to with the block actor and component that held it. The cached
/// pointers are only compared with the live ones read from the block actor at the position, never
/// dereferenced: the container is handed out again only while that live block actor still holds it.
struct Half {
    GlobalPos         pos;
    BlockActor const* actor     = nullptr;
    void const*       component = nullptr;
    Container*        container = nullptr;
};

/// What a lookup found at a position, kept while it still holds.
struct Seen {
    Block const*            self = nullptr;     ///< the block at the position
    std::optional<BlockPos> neighbour;          ///< the block its pairing depends on (a chest half)
    Block const*            neighbourBlock = nullptr;
    bool                    exact      = false; ///< a family chest: the blocks above are all it depends on
    std::int64_t            tick       = 0;     ///< made then (other blocks: valid for that tick only)
    unsigned                generation = 0;     ///< the visibility settings it saw
    std::uint64_t           registry   = 0;     ///< the inventory contract's declarations it saw
    bool                    visible    = false; ///< hoppers reach the block (feature/ext/families/Hoppers.h)
    std::vector<Half>       halves;             ///< the inventory contract's halves, in slot order
};

std::map<GlobalPos, Seen>& seen() {
    static std::map<GlobalPos, Seen> m;
    return m;
}

/// The inventory contract's declaration of a block type, as a lookup depends on it (whether it is
/// declared, chest-like (hoppers' default) and paired (its halves)), checked once a tick per type:
/// a declaration made or removed later (inventory::declare / unregisterBlock) redoes the lookups.
struct TypeCheck {
    std::int64_t  tick  = -1;
    std::uint64_t print = 0;
};
struct Registry {
    std::map<BlockType const*, TypeCheck> types;
    std::uint64_t                         generation = 1;
};
Registry& registry() {
    static Registry r;
    return r;
}

std::uint64_t declarationPrint(std::string const& blockId) {
    auto d = inventory::declared(blockId);
    if (!d) return 1;
    return 2u | (d->chest ? 4u : 0u) | (d->pairs ? 8u : 0u) | (static_cast<std::uint64_t>(std::max(0, d->slots)) << 8);
}

/// The declarations generation after checking `block`'s type (once a tick).
std::uint64_t checkDeclaration(Block const& block) {
    auto& r     = registry();
    auto& check = r.types[&block.getBlockType()];
    if (auto now = families::now(); check.tick != now) {
        check.tick = now;
        auto print = declarationPrint(std::string(block.getTypeName()));
        if (print != check.print) {
            check.print = print;
            ++r.generation;
        }
    }
    return r.generation;
}

bool isChestEntry(Entry const* e) { return e && (e->family == Family::Chest || e->family == Family::TrappedChest); }

/// The lookup of `gp` (whose block is `block`), redone when something it depends on changed.
Seen& lookup(BlockSource& region, BlockPos const& pos, GlobalPos const& gp, Block const& block) {
    auto     now        = families::now();
    unsigned generation = visibility().generation.load();
    auto     declared   = checkDeclaration(block);
    auto&    m          = seen();
    if (auto it = m.find(gp); it != m.end()) {
        auto& s     = it->second;
        bool  valid = s.self == &block && s.generation == generation && s.registry == declared;
        if (valid && s.exact) valid = !s.neighbour || &region.getBlock(*s.neighbour) == s.neighbourBlock;
        else if (valid) valid = s.tick == now;
        if (valid) return s;
    }
    Seen s;
    s.self       = &block;
    s.tick       = now;
    s.generation = generation;
    s.registry   = declared;
    s.visible    = families_ext::hopperVisible(std::string(block.getTypeName()));
    if (!s.visible) {
        s.exact = true; // nothing more to know until the block, the settings or the declaration change
    } else {
        for (auto const& p : inventory::partsOf(gp)) s.halves.push_back(Half{p});
        if (isChestEntry(entryOf(block))) {
            // inventory::partsOf of a family chest: its own states and the block on its connected side
            s.exact = true;
            if ((s.neighbour = chestPairingNeighbour(block, pos))) s.neighbourBlock = &region.getBlock(*s.neighbour);
        }
    }
    return m[gp] = std::move(s);
}

/// The container a block actor's container component holds, as the game lays the component out
/// (ContainerBlockActorComponent: its vtable, then the container; the inventory contract's own
/// component has the same layout, src/feature/inventory/Inventory.cpp). Only compared.
void const* containerFieldOf(BlockActor const& actor) {
    auto const* component = actor.mContainer.get();
    return component ? static_cast<void const*>(component->mContainer.get()) : nullptr;
}

/// The native container of a half: the one resolved before while the live block actor at the
/// position, its component and the component's container are the same objects, else resolved
/// again through the inventory contract (inventory::nativeContainer: the block's registration, the
/// container's position noted).
Container* containerOf(BlockSource& region, Half& h) {
    auto  bp = game::blockPos(h.pos);
    auto* a  = region.getBlockEntity(bp);
    if (!a || a->getType() != BlockActorType::DataDriven) {
        h.container = nullptr;
        return nullptr;
    }
    if (h.container && a == h.actor && static_cast<void const*>(a->mContainer.get()) == h.component
        && containerFieldOf(*a) == static_cast<void const*>(h.container))
        return h.container;
    h.container = inventory::nativeContainer(region, bp);
    h.actor     = a;
    h.component = a->mContainer.get();
    return h.container;
}

// ---- the client mod's chest screens --------------------------------------------------------------------

/// A click on a TF chest half that the inventory contract's use handler answers with a screen
/// (Chests.cpp): its position becomes the screen's context when the screen opens during the click.
struct PendingOpen {
    std::int64_t           uid = 0;
    GlobalPos              pos;
    std::vector<GlobalPos> parts;
    std::int64_t           tick = 0;
};
std::optional<PendingOpen>& pendingOpen() {
    static std::optional<PendingOpen> p;
    return p;
}

/// The position each player's open chest screen was opened for (the inventory contract's context of
/// the screen), known when the player's own click opened it.
std::map<std::int64_t, GlobalPos>& screenContexts() {
    static std::map<std::int64_t, GlobalPos> m;
    return m;
}

/// The opener counts of the TF chest halves someone looks into (inventory::onViewersChanged).
std::map<GlobalPos, int>& openerCounts() {
    static std::map<GlobalPos, int> m;
    return m;
}

/// The change listener of a watched half: every write of one of its container's slots notes the half.
class HalfWatch final : public ContainerContentChangeListener {
public:
    GlobalPos pos;
    bool      active = false;
    void      containerContentChanged(int) override {
        if (active) changedHalves().insert(pos);
    }
};

/// The halves watched (someone looks inside) and the native container each listener sits on, with
/// the block actor and component that held it when the listener went on.
struct Watched {
    Container*        container = nullptr; ///< (compared with the live one, dereferenced only while it is)
    HalfWatch*        listener  = nullptr;
    BlockActor const* actor     = nullptr;
    void const*       component = nullptr;
};
struct Watches {
    std::map<GlobalPos, Watched> byPos;
    /// Every listener ever made. A listener left on a container that may still live elsewhere (a
    /// chunk unloaded, a block actor moved: the container at the position is not the one it sits
    /// on) stays inactive and is never freed nor reused; one taken off a live container is reused.
    /// Leaked on purpose: a container the game keeps may still name one at shutdown.
    std::vector<HalfWatch*>* all = new std::vector<HalfWatch*>();
    std::vector<HalfWatch*>  free;
};
Watches& watches() {
    static Watches w;
    return w;
}

/// The native container of the half at `pos` now (nullptr: none, or not loaded).
Container* liveContainer(GlobalPos const& pos) {
    auto* region = regionOf(pos.dim);
    auto  bp     = game::blockPos(pos);
    if (!region || !region->hasChunksAt(bp, 0, false)) return nullptr;
    return inventory::nativeContainer(*region, bp);
}

/// Takes the listener off the half at `pos`.
void unwatch(GlobalPos const& pos) {
    auto& w  = watches();
    auto  it = w.byPos.find(pos);
    if (it == w.byPos.end()) return;
    auto* container = it->second.container;
    auto* listener  = it->second.listener;
    w.byPos.erase(it);
    listener->active = false;
    if (liveContainer(pos) == container) {
        container->removeContentChangeListener(listener);
        w.free.push_back(listener);
    }
}

/// Forgets the listener of the half at `pos` without touching the world (its chunk unloaded, or the
/// world closes): it stays inactive on whatever container still holds it, never reused.
void abandonWatch(GlobalPos const& pos) {
    auto& w  = watches();
    auto  it = w.byPos.find(pos);
    if (it == w.byPos.end()) return;
    it->second.listener->active = false;
    w.byPos.erase(it);
}

/// Puts a listener on the native container of the half at `pos` (none while it has none: its slots
/// are compared every tick instead).
void watch(GlobalPos const& pos) {
    auto& w = watches();
    auto* c = liveContainer(pos);
    if (auto it = w.byPos.find(pos); it != w.byPos.end()) {
        // (the live container at the same address may be a new one that never had the listener)
        if (c && it->second.container == c && c->mContentChangeListeners.contains(it->second.listener)) return;
        unwatch(pos);
    }
    if (!c) return;
    HalfWatch* listener = nullptr;
    if (!w.free.empty()) {
        listener = w.free.back();
        w.free.pop_back();
    } else {
        listener = new HalfWatch();
        w.all->push_back(listener);
    }
    listener->pos    = pos;
    listener->active = true;
    c->addContentChangeListener(listener);
    auto* actor  = regionOf(pos.dim)->getBlockEntity(game::blockPos(pos)); // (liveContainer found the region and the actor)
    w.byPos[pos] = Watched{c, listener, actor, actor ? static_cast<void const*>(actor->mContainer.get()) : nullptr};
}

/// The listener of the watched half at `pos` still sits on the container its block actor holds (a
/// chest replaced while its screen stays open has a new block actor and container): compared, never
/// dereferenced from what was kept.
bool watchHolds(GlobalPos const& pos, Watched const& w) {
    auto* region = regionOf(pos.dim);
    auto  bp     = game::blockPos(pos);
    if (!region || !region->hasChunksAt(bp, 0, false)) return false;
    auto* a = region->getBlockEntity(bp);
    return a && a == w.actor && static_cast<void const*>(a->mContainer.get()) == w.component && containerFieldOf(*a) == static_cast<void const*>(w.container);
}

/// The slots each unwatched half a client mod chest screen showed at the last tick held.
std::map<GlobalPos, std::vector<ItemStack>>& shownSlots() {
    static std::map<GlobalPos, std::vector<ItemStack>> m;
    return m;
}

bool sameSlots(std::vector<ItemStack> const& a, std::vector<ItemStack> const& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        bool ea = game::items::isEmpty(a[i]), eb = game::items::isEmpty(b[i]);
        if (ea != eb) return false;
        if (!ea && (game::items::countOf(a[i]) != game::items::countOf(b[i]) || !game::items::sameItemSameData(a[i], b[i]))) return false;
    }
    return true;
}

/// Forgets the contexts of the screens that no longer show their context's position (closed, or
/// replaced by a screen whose context is unknown).
void pruneContexts() {
    std::erase_if(screenContexts(), [](auto const& e) {
        auto* player = playerById(e.first);
        return !player || !contains(inventory::openedParts(*player), e.second);
    });
}

} // namespace

Container* hopperContainerAt(BlockSource& region, BlockPos const& pos) {
    if (region.getLevel().isClientSide()) return nullptr;
    // a data-driven block actor (minecraft:block_entity) of a block hoppers may reach
    auto* actor = region.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::DataDriven) return nullptr;
    auto  gp = game::globalPos(region, pos);
    auto& s  = lookup(region, pos, gp, region.getBlock(pos));
    if (!s.visible || s.halves.empty()) return nullptr;
    // ChestBlock#getContainer(.., ignoreBlocked = true): the halves of a declared double chest,
    // settled first (once a tick: several hoppers, a push and a pull, look in one tick)
    for (std::size_t i = 0; i < s.halves.size(); ++i) {
        auto p = s.halves[i].pos; // (a copy: nothing below may outlive the entry)
        detail::settleForAccess(region, p);
    }
    std::vector<Container*> halves;
    for (auto& h : s.halves) halves.push_back(containerOf(region, h));
    std::size_t self = 0;
    while (self < s.halves.size() && !(s.halves[self].pos == gp)) ++self;
    if (self == s.halves.size() || !halves[self]) return nullptr;
    bool  both  = s.halves.size() == 2 && halves[0] && halves[1]; // else each half on its own
    auto  first = both ? s.halves[0].pos : gp, second = both ? s.halves[1].pos : gp;
    auto& view  = views()[first];
    if (!view) view = std::make_unique<ChestView>();
    view->first     = both ? halves[0] : halves[self];
    view->second    = both ? halves[1] : nullptr;
    view->firstPos  = first;
    view->secondPos = second;
    view->lastUsed  = families::now();
    return view.get();
}

namespace detail {

void chestClicked(Player& player, GlobalPos const& pos, std::vector<GlobalPos> const& parts, bool opens) {
    auto uid = player.getOrCreateUniqueID().rawID;
    if (!opens || !net::isCompanion(player)) {
        if (pendingOpen() && pendingOpen()->uid == uid) pendingOpen().reset();
        return;
    }
    pendingOpen() = PendingOpen{uid, pos, parts, families::now()};
}

void chestOpenersChanged(GlobalPos const& pos, int count) {
    auto& counts = openerCounts();
    int   before = 0;
    if (auto it = counts.find(pos); it != counts.end()) before = it->second;
    if (count > 0) counts[pos] = count;
    else counts.erase(pos);
    // a screen that closed (or was replaced) no longer has the context it had
    pruneContexts();
    // the screen a click asked for opened: the inventory contract opened it for the clicked half
    // (it counts the openers of the new screen's halves right after it opened it)
    if (auto& pending = pendingOpen(); pending && count > before && contains(pending->parts, pos)) {
        if (pending->tick == families::now())
            if (auto* player = playerById(pending->uid); player && sameParts(inventory::openedParts(*player), pending->parts))
                screenContexts()[pending->uid] = pending->pos;
        pending.reset();
    }
    // a change listener while someone looks inside
    if (count > 0) watch(pos);
    else unwatch(pos);
}

std::optional<GlobalPos> chestScreenContext(Player& player) {
    auto parts = inventory::openedParts(player);
    if (parts.size() == 1) return parts[0];
    auto it = screenContexts().find(player.getOrCreateUniqueID().rawID);
    if (it == screenContexts().end() || !contains(parts, it->second)) return std::nullopt;
    return it->second;
}

bool chestHalfWatched(GlobalPos const& pos) { return watches().byPos.contains(pos); }

/// Every tick, Java's AbstractContainerMenu#broadcastChanges for the client mod's TF chest screens:
/// a screen showing a half whose slots changed this tick (the listeners, the hopper views, or for a
/// half without a native container the comparison with the last tick's slots) gets fresh data, named
/// by its context (chestScreenContext).
void refreshChestScreens() {
    auto level = ll::service::getLevel();
    if (!level) {
        changedHalves().clear();
        shownSlots().clear();
        return;
    }
    pendingOpen().reset(); // a click whose screen did not open in its tick
    pruneContexts();
    // a half whose chunk unloaded: the inventory contract counts no openers there (it tells nobody
    // when the chunk is gone), so its count and listener are dropped here (its container went with
    // the chunk, or is kept by the game: never touched)
    std::vector<GlobalPos> gone;
    for (auto const& [pos, _] : openerCounts()) {
        auto* region = regionOf(pos.dim);
        if (!region || !region->hasChunksAt(game::blockPos(pos), 0, false)) gone.push_back(pos);
    }
    for (auto const& [pos, _] : watches().byPos) {
        auto* region = regionOf(pos.dim);
        if (!region || !region->hasChunksAt(game::blockPos(pos), 0, false)) gone.push_back(pos);
    }
    for (auto const& pos : gone) {
        openerCounts().erase(pos);
        abandonWatch(pos);
    }
    struct Viewer {
        std::int64_t             uid = 0;
        std::vector<GlobalPos>   parts;
        std::optional<GlobalPos> at;
    };
    std::vector<Viewer> viewers;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& player) {
        if (!gf::net::isCompanion(player)) return true;
        auto parts = inventory::openedParts(player);
        // TF chest screens (the halves whose openers the families count)
        if (parts.empty() || std::any_of(parts.begin(), parts.end(), [](GlobalPos const& p) { return !openerCounts().contains(p); })) return true;
        viewers.push_back({player.getOrCreateUniqueID().rawID, std::move(parts), chestScreenContext(player)});
        return true;
    }));
    auto changed = std::move(changedHalves());
    changedHalves().clear();
    // a half without a listener: a native container found since gets one; until then (its stacks in
    // block data) its slots are compared with the last tick's (a half shown for the first time has
    // nothing to compare with: its screen just got its data)
    std::map<GlobalPos, std::vector<ItemStack>> slotsNow;
    for (auto const& v : viewers)
        for (auto const& p : v.parts) {
            // a watched half whose block actor or container is not the one the listener went on (the
            // chest replaced while the screen stays open): watched anew, its screen refreshed
            if (auto it = watches().byPos.find(p); it != watches().byPos.end() && !watchHolds(p, it->second)) {
                unwatch(p);
                watch(p);
                changed.insert(p);
            }
            if (chestHalfWatched(p) || slotsNow.contains(p)) continue;
            auto slots = inventory::contents(p);
            if (auto it = shownSlots().find(p); it != shownSlots().end() && !sameSlots(it->second, slots)) changed.insert(p);
            watch(p);
            if (!chestHalfWatched(p)) slotsNow.emplace(p, std::move(slots));
        }
    shownSlots() = std::move(slotsNow);
    if (changed.empty()) return;
    for (auto const& v : viewers) {
        if (!v.at || std::none_of(v.parts.begin(), v.parts.end(), [&](GlobalPos const& p) { return changed.contains(p); })) continue;
        if (auto* player = playerById(v.uid)) screens::refresh(*player, "inventory.chest", {{"pos", {v.at->dim, v.at->x, v.at->y, v.at->z}}});
    }
}

/// Between ticks: the chest screens are refreshed; views unused for a second go, and the kept
/// lookups are dropped now and then (they hold positions of chunks that may have gone).
void hoppersTick() {
    refreshChestScreens();
    auto now = families::now();
    if (now % 20 == 0) std::erase_if(views(), [now](auto const& e) { return now - e.second->lastUsed > 20; });
    if (now % 100 == 0 || seen().size() > 4096) seen().clear();
    if (registry().types.size() > 4096) registry().types.clear();
}

void clearHopperViews() {
    views().clear();
    seen().clear();
    registry().types.clear();
    ++registry().generation;
    changedHalves().clear();
    pendingOpen().reset();
    screenContexts().clear();
    openerCounts().clear();
    shownSlots().clear();
    // the world closes (or the module goes): the listeners stay inactive on the containers, which
    // are not touched while the world is torn down
    std::vector<GlobalPos> watched;
    for (auto const& [pos, _] : watches().byPos) watched.push_back(pos);
    for (auto const& pos : watched) abandonWatch(pos);
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

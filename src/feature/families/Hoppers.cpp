// Block families (F11): what hoppers, hopper minecarts and droppers see of a chest (Java's
// HopperBlockEntity#getContainerAt -> ChestBlock#getContainer(state, level, pos, true): the chest
// block entity's container, a double chest's CompoundContainer of both halves (DoubleBlockCombiner:
// the right half first), the blocked rule ignored; RandomizableContainerBlockEntity rolls a pending
// loot table on any access of its slots).
//
// upstream's chests are vanilla ChestBlock / TrappedChestBlock in Java, so hoppers, hopper minecarts and
// droppers fill and empty them. Here a chest keeps its slots in the inventory contract's native
// backend (F10, spike S3): the game container F10 gives the data-driven block actor that
// tools/families declares with minecraft:block_entity. The game's hopper and dropper ask a block
// actor for its container through the vanilla main component, which a data-driven block actor does
// not have, so they would see nothing there: three hooks hand them the native container instead
// (Hopper::_getContainerInBlock and Hopper::_getContainersAt for hoppers and hopper minecarts,
// DropperBlockActor::getContainerAt for droppers). Only blocks hoppers may reach get one
// (feature/ext/families/Hoppers.h: by default the chest-like declarations, upstream's chests among them;
// other units opt their Container block entities in); a block kept in block data (no
// minecraft:block_entity) stays invisible to them.
//
// Java's ChestMenu shows every change at once to every player looking inside: the inventory contract
// notes every write of its native containers (GfBlockContainer::setItem, the hoppers' included) and
// refreshes the client mod's chest screens after the tick (feature/ext/combat/InventoryChanges.h).
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

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/Container.h"
#include "mc/world/DataDrivenBlockContainer.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"
#include "mc/world/level/block/actor/component/ContainerBlockActorComponent.h"

#include "feature/ext/combat/InventoryChanges.h"
#include "feature/ext/families/Hoppers.h"
#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
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

/// Java's CompoundContainer over the two halves of a double chest (slots 0..26 the first (right)
/// half's, 27..53 the second's), or one chest's own container (no second half). The halves'
/// containers are the native ones of this tick (set again on every lookup; nobody keeps the view
/// across ticks).
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
            c->setItem(i, item); // (the inventory contract notes the change)
        }
    }
    void removeItem(int slot, int count) override {
        if (auto [c, i] = part(slot); c) {
            c->removeItem(i, count);
            inventory::ext::markChanged(c == first ? firstPos : secondPos); // (the game's removeItem may not go through setItem)
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

} // namespace

Container* hopperContainerAt(BlockSource& region, BlockPos const& pos) {
    if (region.getLevel().isClientSide()) return nullptr;
    // a data-driven block actor (minecraft:block_entity) of a block hoppers may reach
    auto* actor = region.getBlockEntity(pos);
    if (!actor) {
        // an upstream chest half saved without its block actor: it gets one at the end of the tick (with
        // its partner), and the hopper sees the chest from the next tick on
        detail::settleWithoutActor(region, pos);
        return nullptr;
    }
    if (actor->getType() != BlockActorType::DataDriven) return nullptr;
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

/// Between ticks: views unused for a second go, and the kept lookups are dropped now and then (they
/// hold positions of chunks that may have gone).
void hoppersTick() {
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

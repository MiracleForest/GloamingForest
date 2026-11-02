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
// DropperBlockActor::getContainerAt for droppers). It covers every block of the inventory contract
// that has a native container, TF's other container block entities included (Java: the skull chest
// and keepsake casket are RandomizableContainerBlockEntity too); a block kept in block data (no
// minecraft:block_entity) stays invisible to them.

#include <algorithm>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"

#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace {

/// Java's CompoundContainer over the two halves of a double chest: slots 0..26 the first (right)
/// half's, 27..53 the second's. The halves' containers are the native ones of this tick (set again
/// on every lookup; nobody keeps the view across ticks).
class DoubleChestView final : public Container {
public:
    DoubleChestView() : Container(SharedTypes::Legacy::ContainerType::Container) {}

    Container* first  = nullptr;
    Container* second = nullptr;

    ItemStack const& getItem(int slot) const override {
        auto [c, i] = part(slot);
        return c ? c->getItem(i) : ItemStack::EMPTY_ITEM();
    }
    void setItem(int slot, ItemStack const& item) override {
        if (auto [c, i] = part(slot); c) c->setItem(i, item);
    }
    void removeItem(int slot, int count) override {
        if (auto [c, i] = part(slot); c) c->removeItem(i, count);
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

/// The double chest views handed out (by the first half's position). Freed only between ticks
/// (trimViews), never while a hopper may still hold one.
std::map<GlobalPos, std::unique_ptr<DoubleChestView>>& views() {
    static std::map<GlobalPos, std::unique_ptr<DoubleChestView>> m;
    return m;
}

} // namespace

Container* hopperContainerAt(BlockSource& region, BlockPos const& pos) {
    // a data-driven block actor (minecraft:block_entity) with the inventory contract's container
    auto* actor = region.getBlockEntity(pos);
    if (!actor || actor->getType() != BlockActorType::DataDriven || region.getLevel().isClientSide()) return nullptr;
    auto* own = inventory::nativeContainer(region, pos);
    if (!own) return nullptr;
    // ChestBlock#getContainer(.., ignoreBlocked = true): the halves of a declared double chest
    auto parts = inventory::partsOf(game::globalPos(region, pos));
    // RandomizableContainerBlockEntity#getItem / removeItem / setItem: unpackLootTable(null) first
    for (auto const& p : parts) inventory::unpackLoot(p, nullptr);
    if (parts.size() < 2) return own;
    auto* first  = inventory::nativeContainer(region, game::blockPos(parts[0]));
    auto* second = inventory::nativeContainer(region, game::blockPos(parts[1]));
    if (!first || !second) return own; // a half without a native container: each half on its own
    auto& view = views()[parts[0]];
    if (!view) view = std::make_unique<DoubleChestView>();
    view->first  = first;
    view->second = second;
    return view.get();
}

namespace detail {
/// Between ticks: drops the double chest views once there are many (a hopper holds one only during
/// its own transfer).
void trimHopperViews() {
    if (views().size() > 256) views().clear();
}
void clearHopperViews() { views().clear(); }
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
    views().clear();
}

} // namespace gf::feature::families

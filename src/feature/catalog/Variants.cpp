// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFCreativeTabs.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Creative entry variants of the catalog (F9, gf::catalog::addVariant): upstream's tabs accept a few
// stacks with data next to the plain item (TFCreativeTabs#createGlassSwordAndLoreVer). The behavior
// pack's item_catalog lists item ids only, so the variant is a new entry of the server's
// CreativeItemRegistry: a copy of the item's entry with the variant's data, placed right after it
// in its group, added when a world is ready (before the creative content goes to the clients).
//
// The dedicated server's registry has no entry factory (CreativeItemRegistry::newItemEntry is
// client-only): the entry is constructed in place at the end of the registry's entry vector. An
// entry is an EnableNonOwnerReferences: when the vector has to grow, the copies share the control
// block that the game's destructor of the old entry marks invalid, so every entry gets a fresh
// valid control block after the growth (the server keeps entries by index: groups and the net id
// map; no NonOwnerPointer to an entry is held while a world becomes ready). The client build (local
// worlds) uses the game's own newItemEntry.

#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <utility>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/CreativeGroupInfo.h"
#include "mc/world/item/registry/CreativeItemEntry.h"
#include "mc/world/item/registry/CreativeItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/Level.h"

#include "feature/catalog/Variants.h"
#include "feature/catalog/CatalogInternal.h"
#include "feature/item/ItemInternal.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

// The game hashes its net ids by their raw id; the SDK declares no std::hash for them.
// hashMatchesGame() checks this one against the registry's own map before the map is written.
template <>
struct std::equal_to<CreativeItemNetId> {
    bool operator()(CreativeItemNetId const& a, CreativeItemNetId const& b) const noexcept { return a.mRawId == b.mRawId; }
};
template <>
struct std::hash<CreativeItemNetId> {
    std::size_t operator()(CreativeItemNetId const& id) const noexcept { return std::hash<decltype(id.mRawId)>()(id.mRawId); }
};

namespace gf::catalog {

namespace {

/// Whether this file's std::hash<CreativeItemNetId> finds every entry of the registry's net id map
/// (then it is the game's hash): only then may the map be written here.
bool hashMatchesGame(CreativeItemRegistry& reg) {
    auto& map = reg.mCreativeNetIdIndex.get();
    for (auto const& e : reg.mCreativeItems.get())
        if (map.find(e.mCreativeNetId.get()) == map.end()) return false;
    return true;
}

struct Variants {
    std::mutex                                                   mutex;
    std::map<std::pair<std::string, std::string>, VariantFn>     fns;
};
Variants& variants() {
    static Variants v;
    return v;
}

CreativeItemRegistry* registry() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto ref = level->getItemRegistry();
    return ref.getCreativeItemRegistry().get();
}

/// The item's own entry: its first entry without user data, else its first entry (nullopt: none).
std::optional<unsigned> baseEntry(std::vector<CreativeItemEntry> const& entries, std::string const& itemId) {
    std::optional<unsigned> base;
    for (unsigned i = 0; i < entries.size(); ++i) {
        auto const& inst = entries[i].mItemInstance.get();
        if (inst.isNull() || inst.getTypeName() != itemId) continue;
        if (!base || (!inst.mUserData && entries[*base].mItemInstance->mUserData)) base = i;
    }
    return base;
}

bool sameData(ItemStackBase const& a, ItemStackBase const& b) {
    auto const& ta = a.mUserData;
    auto const& tb = b.mUserData;
    if (!ta || !tb) return !ta && !tb;
    return ta->equals(*tb);
}

/// The group holding entry `index` and the position of the index in it (nullptr: none).
std::pair<CreativeGroupInfo*, std::size_t> groupOfEntry(CreativeItemRegistry& reg, unsigned index) {
    for (auto& group : reg.mCreativeGroups.get()) {
        auto& idx = group.mItemIndexes.get();
        if (auto it = std::find(idx.begin(), idx.end(), index); it != idx.end()) return {&group, static_cast<std::size_t>(it - idx.begin())};
    }
    return {nullptr, 0};
}

/// Appends a registry entry for `item` (see the file comment).
CreativeItemEntry* appendEntry(CreativeItemRegistry& reg, ItemInstance const& item) {
    auto&    entries = reg.mCreativeItems.get();
    unsigned rawId   = 0;
    for (auto const& e : entries) rawId = std::max(rawId, static_cast<unsigned>(e.mCreativeNetId->mRawId));
    CreativeItemNetId netId(rawId + 1);
#ifdef LL_PLAT_C
    auto* entry = reg.newItemEntry(netId, item);
    reg.updateNetIdMap();
    return entry;
#else
    if (entries.size() >= entries.capacity()) {
        entries.reserve(entries.size() + 16);
        for (auto& e : entries)
            if (!e.mControlBlock || !e.mControlBlock->mIsValid)
                e.mControlBlock = std::make_shared<Bedrock::EnableNonOwnerReferences::ControlBlock>(Bedrock::EnableNonOwnerReferences::ControlBlock{true});
    }
    auto index = static_cast<unsigned>(entries.size());
    bool mapped = hashMatchesGame(reg);
    entries.emplace_back(&reg, netId, item, index);
    if (mapped) reg.mCreativeNetIdIndex.get()[netId] = index;
    else log().warn("catalog: the creative net id map hashes differently: the variant entry is listed but not mapped by net id");
    return &entries.back();
#endif
}

} // namespace

void addVariant(std::string const& itemId, std::string const& key, VariantFn fn) {
    auto& v = variants();
    {
        std::lock_guard lock(v.mutex);
        v.fns[{itemId, key}] = std::move(fn);
    }
    // a world is ready already (BDS enables the units after the world exists): add it now
    if (game::db::available()) internal::applyVariants();
}

namespace internal {

int applyVariants() {
    std::vector<std::pair<std::pair<std::string, std::string>, VariantFn>> todo;
    {
        auto&           v = variants();
        std::lock_guard lock(v.mutex);
        todo.assign(v.fns.begin(), v.fns.end());
    }
    auto* reg = registry();
    if (!reg || todo.empty()) return 0;
    int added = 0;
    for (auto const& [k, fn] : todo) {
        auto const& [itemId, key] = k;
        try {
            auto& entries = reg->mCreativeItems.get();
            auto base = baseEntry(entries, itemId);
            if (!base) {
                log().warn("catalog: variant {} of {}: the item has no creative entry", key, itemId);
                continue;
            }
            auto stack = item::internal::stackOf(entries[*base].mItemInstance.get());
            fn(stack);
            bool there = false;
            for (auto const& e : entries)
                if (!e.mItemInstance->isNull() && e.mItemInstance->getTypeName() == itemId && sameData(e.mItemInstance.get(), stack)) there = true;
            if (there) continue;
            auto [group, at] = groupOfEntry(*reg, *base);
            unsigned groupIndex = entries[*base].mGroupIndex;
            ItemInstance inst(stack);
            auto*        entry = appendEntry(*reg, inst);
            if (!entry) {
                log().warn("catalog: variant {} of {} skipped: the server's creative registry has no room for another entry", key, itemId);
                continue;
            }
            entry->mGroupIndex = groupIndex;
            if (group) {
                auto& idx = group->mItemIndexes.get();
                idx.insert(idx.begin() + static_cast<std::ptrdiff_t>(at + 1), entry->mIndex);
            }
            ++added;
        } catch (std::exception const& e) {
            log().error("catalog: variant {} of {} failed: {}", key, itemId, e.what());
        }
    }
    if (added > 0) {
        invalidate();
        log().info("catalog: {} creative entry variant(s) added", added);
    }
    return added;
}

bool removeVariant(std::string const& itemId, std::string const& key) {
    VariantFn fn;
    {
        auto&           v = variants();
        std::lock_guard lock(v.mutex);
        auto            it = v.fns.find({itemId, key});
        if (it == v.fns.end()) return false;
        fn = std::move(it->second);
        v.fns.erase(it);
    }
    auto* reg = registry();
    if (!reg) return false;
    auto& entries = reg->mCreativeItems.get();
    if (entries.empty()) return false;
    auto& last = entries.back();
    if (last.mItemInstance->isNull() || last.mItemInstance->getTypeName() != itemId) return false;
    // the last entry is this variant when it carries the variant's data (applyVariants appends it)
    auto base = baseEntry(entries, itemId);
    if (!base || *base + 1 == entries.size()) return false;
    auto stack = item::internal::stackOf(entries[*base].mItemInstance.get());
    fn(stack);
    if (!sameData(last.mItemInstance.get(), stack)) return false;
    unsigned index = last.mIndex;
    for (auto& group : reg->mCreativeGroups.get()) std::erase(group.mItemIndexes.get(), index);
#ifdef LL_PLAT_C
    entries.pop_back();
    reg->updateNetIdMap();
#else
    if (hashMatchesGame(*reg)) reg->mCreativeNetIdIndex.get().erase(last.mCreativeNetId.get());
    entries.pop_back();
#endif
    invalidate();
    return true;
}

std::vector<std::pair<unsigned, bool>> entriesInGroupOrder(std::string const& itemId) {
    std::vector<std::pair<unsigned, bool>> out;
    auto*                                  reg = registry();
    if (!reg) return out;
    auto const& entries = reg->mCreativeItems.get();
    for (auto const& group : reg->mCreativeGroups.get())
        for (auto i : group.mItemIndexes.get())
            if (i < entries.size() && !entries[i].mItemInstance->isNull() && entries[i].mItemInstance->getTypeName() == itemId)
                out.emplace_back(i, static_cast<bool>(entries[i].mItemInstance->mUserData));
    return out;
}

} // namespace internal

} // namespace gf::catalog

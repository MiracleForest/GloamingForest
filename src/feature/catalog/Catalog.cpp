// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/GloamingCreativeTabs.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The catalog part of contract "data" (F9; spike S6). upstream's four creative tabs (blocks, items,
// equipment, food, in that order) are Bedrock creative catalog groups: the behavior pack's
// item_catalog (F1's import of GloamingCreativeTabs, item_catalog.d fragments) declares them, the server
// builds its CreativeItemRegistry from it and sends it to every client. `groups()` reads that
// registry back (the groups whose name is an upstream tab, their items in catalog order); before a world
// exists, or without the catalog data, the four tabs are listed with their icon and name only.
// The creative entries' data (the default enchantments of showEnchantmentsOnItems, units' default
// data through ext/item/Catalog.h) is applied to the registry's entries when a world is ready.
#include "feature/catalog/Catalog.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/registry/CreativeGroupInfo.h"
#include "mc/world/item/registry/CreativeItemEntry.h"
#include "mc/world/item/registry/CreativeItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/Level.h"

#include "feature/catalog/CatalogInternal.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::catalog {

namespace {

/// GloamingCreativeTabs: id, icon, title, in upstream's order (withTabsBefore).
std::vector<Group> defaults() {
    return {
        {"gloamingforest:blocks", "gloamingforest:naga_courtyard_miniature_structure", "itemGroup.gloamingforest.blocks", {}},
        {"gloamingforest:items", "gloamingforest:gloaming_portal_miniature_structure", "itemGroup.gloamingforest.items", {}},
        {"gloamingforest:equipment", "gloamingforest:knightmetal_pickaxe", "itemGroup.gloamingforest.equipment", {}},
        {"gloamingforest:food", "gloamingforest:cooked_meef", "itemGroup.gloamingforest.food", {}},
    };
}

struct Store {
    std::mutex                         mutex;
    std::vector<Group>                 groups = defaults();
    std::map<std::string, std::string> groupOf;
    bool                               dirty = true;
};

Store& store() {
    static Store s;
    return s;
}

/// The upstream tab a catalog group name stands for ("" when none): "gloamingforest:blocks", or a name
/// ending in the tab's path after ':' / '.' / '_' with "gloamingforest" in it.
std::string tabOf(std::string const& groupName) {
    for (auto const& g : defaults()) {
        if (groupName == g.id) return g.id;
        auto path = g.id.substr(g.id.find(':') + 1);
        if (groupName.find("gloamingforest") == std::string::npos) continue;
        if (groupName.size() > path.size() && groupName.ends_with(path)) {
            char sep = groupName[groupName.size() - path.size() - 1];
            if (sep == ':' || sep == '.' || sep == '_') return g.id;
        }
    }
    return {};
}

CreativeItemRegistry* registry() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto ref = level->getItemRegistry();
    return ref.getCreativeItemRegistry().get();
}

void rebuild(Store& s) {
    s.dirty  = false;
    s.groups = defaults();
    s.groupOf.clear();
    auto* reg = registry();
    if (!reg) {
        s.dirty = true; // again once a level exists
        return;
    }
    auto const& items = reg->mCreativeItems.get();
    for (auto const& info : reg->mCreativeGroups.get()) {
        auto tab = tabOf(info.mName->getString());
        if (tab.empty()) continue;
        auto g = std::find_if(s.groups.begin(), s.groups.end(), [&](Group const& x) { return x.id == tab; });
        if (g == s.groups.end()) continue;
        if (!info.mIcon->isNull()) g->iconItem = info.mIcon->getTypeName();
        for (auto idx : info.mItemIndexes.get()) {
            if (idx >= items.size()) continue;
            auto const& inst = items[idx].mItemInstance.get();
            if (inst.isNull()) continue;
            auto id = inst.getTypeName();
            if (std::find(g->items.begin(), g->items.end(), id) == g->items.end()) g->items.push_back(id);
            s.groupOf.emplace(id, tab);
        }
    }
}

} // namespace

std::vector<Group> const& groups() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    if (s.dirty) rebuild(s);
    return s.groups;
}

std::string groupOf(std::string const& itemId) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    if (s.dirty) rebuild(s);
    auto it = s.groupOf.find(itemId);
    return it == s.groupOf.end() ? std::string() : it->second;
}

namespace internal {

void invalidate() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.dirty = true;
}

int forEachEntry(std::string const& itemId, std::function<void(ItemStackBase&)> const& fn) {
    auto* reg = registry();
    if (!reg) return 0;
    int n = 0;
    for (auto& entry : reg->mCreativeItems.get()) {
        auto& inst = entry.mItemInstance.get();
        if (inst.isNull() || inst.getTypeName() != itemId) continue;
        fn(inst);
        ++n;
    }
    return n;
}

std::vector<std::string> groupNames() {
    std::vector<std::string> out;
    auto*                    reg = registry();
    if (!reg) return out;
    for (auto const& info : reg->mCreativeGroups.get()) out.push_back(info.mName->getString());
    return out;
}

} // namespace internal

namespace {
class CatalogModule {
public:
    void install() {
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { // client: every local world
                internal::invalidate();
                internal::applyDefaults();
                internal::applyVariants();
            });
            game::db::onWorldClosed([] { internal::invalidate(); });
        }
        // BDS enables mods after the world exists: the world DB module fired "ready" before this
        // listener was registered
        if (game::db::available()) {
            internal::invalidate();
            internal::applyDefaults();
            internal::applyVariants();
        }
    }
    void uninstall() {}
};
} // namespace

} // namespace gf::catalog

GF_REGISTER_MODULE_ORDERED(gf::catalog::CatalogModule, gf::ModuleOrder::Contracts);

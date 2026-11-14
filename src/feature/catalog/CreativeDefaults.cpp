// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFCreativeTabs.java
//   src/main/java/gloamingforest/config/TFCommonConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The data of the creative catalog's entries: TFCreativeTabs#generateGearWithEnchants (the
// equipment tab's ironwood, steeleaf, mazebreaker, yeti and naga gear with their default
// enchantments while common.showEnchantmentsOnItems is on) and the units' defaults
// (ext/item/Catalog.h), written into the server's CreativeItemRegistry entries, which is what the
// creative inventory of every client shows and gives.
#include <map>
#include <mutex>
#include <utility>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"

#include "feature/catalog/CatalogInternal.h"
#include "feature/ext/item/Catalog.h"
#include "feature/item/ItemInternal.h"
#include "feature/item/ItemsConfig.h"
#include "mod/Config.h"
#include "mod/Log.h"

GF_REGISTER_CONFIG("items", gf::feature::item::ItemsConfig);

namespace gf::catalog {

namespace {

using Enchants = std::vector<std::pair<char const*, int>>;

/// TFCreativeTabs EQUIPMENT: generateGearWithEnchants(item, enchantments...).
std::vector<std::pair<char const*, Enchants>> const& gearWithEnchants() {
    static std::vector<std::pair<char const*, Enchants>> const table{
        {"gloamingforest:ironwood_helmet", {{"aqua_affinity", 1}}},
        {"gloamingforest:ironwood_chestplate", {{"protection", 1}}},
        {"gloamingforest:ironwood_leggings", {{"protection", 1}}},
        {"gloamingforest:ironwood_boots", {{"feather_falling", 1}}},
        {"gloamingforest:ironwood_sword", {{"knockback", 1}}},
        {"gloamingforest:ironwood_shovel", {{"unbreaking", 1}}},
        {"gloamingforest:ironwood_pickaxe", {{"efficiency", 1}}},
        {"gloamingforest:ironwood_axe", {{"fortune", 1}}},
        {"gloamingforest:ironwood_hoe", {{"efficiency", 1}}},
        {"gloamingforest:steeleaf_helmet", {{"projectile_protection", 2}}},
        {"gloamingforest:steeleaf_chestplate", {{"blast_protection", 2}}},
        {"gloamingforest:steeleaf_leggings", {{"fire_protection", 2}}},
        {"gloamingforest:steeleaf_boots", {{"feather_falling", 2}}},
        {"gloamingforest:steeleaf_sword", {{"looting", 2}}},
        {"gloamingforest:steeleaf_shovel", {{"efficiency", 2}}},
        {"gloamingforest:steeleaf_pickaxe", {{"fortune", 2}}},
        {"gloamingforest:steeleaf_axe", {{"efficiency", 2}}},
        {"gloamingforest:steeleaf_hoe", {{"fortune", 2}}},
        {"gloamingforest:mazebreaker_pickaxe", {{"efficiency", 4}, {"unbreaking", 3}, {"fortune", 2}}},
        {"gloamingforest:yeti_helmet", {{"protection", 2}}},
        {"gloamingforest:yeti_chestplate", {{"protection", 2}}},
        {"gloamingforest:yeti_leggings", {{"protection", 2}}},
        {"gloamingforest:yeti_boots", {{"protection", 2}, {"feather_falling", 4}}},
        {"gloamingforest:naga_chestplate", {{"fire_protection", 3}}},
        {"gloamingforest:naga_leggings", {{"protection", 3}}},
    };
    return table;
}

std::mutex& defaultsMutex() {
    static std::mutex m;
    return m;
}
std::vector<std::pair<std::string, ext::DataFn>>& unitDefaults() {
    static std::vector<std::pair<std::string, ext::DataFn>> v;
    return v;
}

} // namespace

namespace ext {

void setDefaultData(std::string const& itemId, DataFn fn) {
    std::lock_guard lock(defaultsMutex());
    unitDefaults().emplace_back(itemId, std::move(fn));
}

} // namespace ext

namespace internal {

int applyDefaults() {
    int changed = 0;
    if (config::get<feature::item::ItemsConfig>().showEnchantmentsOnItems) {
        auto& names = Enchant::mEnchantNameToType();
        for (auto const& [itemId, enchants] : gearWithEnchants()) {
            changed += forEachEntry(itemId, [&](ItemStackBase& stack) {
                for (auto const& [name, level] : enchants) {
                    auto it = names.find(HashedString(name));
                    if (it == names.end()) continue;
                    if (EnchantUtils::getEnchantLevel(it->second, stack) >= level) continue;
                    EnchantmentInstance inst;
                    inst.mEnchantType = it->second;
                    inst.mLevel       = level;
                    EnchantUtils::applyEnchant(stack, inst, true);
                }
            });
        }
    }
    std::vector<std::pair<std::string, ext::DataFn>> defaults;
    {
        std::lock_guard lock(defaultsMutex());
        defaults = unitDefaults();
    }
    for (auto const& [itemId, fn] : defaults) {
        changed += forEachEntry(itemId, [&](ItemStackBase& base) {
            try {
                auto stack = item::internal::stackOf(base);
                fn(stack);
                base.setUserData(stack.mUserData ? stack.mUserData->clone() : nullptr);
            } catch (std::exception const& e) {
                log().error("catalog: default data of {} failed: {}", itemId, e.what());
            }
        });
    }
    if (changed > 0) log().info("catalog: {} creative entries got their default data", changed);
    return changed;
}

} // namespace internal

} // namespace gf::catalog

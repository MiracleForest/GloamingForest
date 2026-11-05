// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFBannerPatterns.java
//   src/main/java/twilightforest/data/tags/CustomTagGenerator.java (BannerPatternTagGenerator)
//   src/main/java/twilightforest/init/TFItems.java (the BannerPatternItems)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): TF's nine banner patterns (TFBannerPatterns: twilightforest:<boss>, the quest ram's
// is questing_ram) and their pattern items (<boss>_banner_pattern, each the only member of
// #twilightforest:pattern_item/<boss>), used in the loom like vanilla pattern items (LoomMenu: the
// pattern item in the pattern slot unlocks its pattern; the item is kept).
//
// Bedrock (spike S6): the banner pattern registry (BannerPattern::mPatterns) is a plain vector with
// MCAPI constructors. Both builds append TF's nine at mod load (the BDS validates the loom's craft
// request against it, the client's loom lists and draws from it), as patterns that need an item
// (BannerPattern(name, nameID, itemFunctor, patternItemType, ignoreAux)). A banner stores the
// pattern's nameID (kPatternIds); the renderer draws textures/entity/banner/banner_<name> (TF's
// textures/entity/banner/<boss>.png, renamed by tools/import/overrides/building.json). The engine
// knows pattern items only as the BannerPatternItem class: two hooks let TF's data-driven items in,
// BannerPattern::matchesPatternItem (the pattern whose item is in the slot) and the loom's pattern
// slot validation (LoomMaterialContainerValidation::isItemAllowedInSlot).
// Vanilla clients (no client mod on a BDS) do not know the nine ids: Bedrock clients keep a banner
// with an unknown pattern and draw it as a plain field (spike S6), with an untranslated line.

#include "feature/building/Building.h"

#include <algorithm>

#include "ll/api/memory/Hook.h"

#include "mc/world/inventory/simulation/validation/LoomMaterialContainerValidation.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/block/actor/BannerPattern.h"

#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::building {

std::vector<BannerSpec> const& bannerPatterns() {
    // TFBannerPatterns (registry id, asset id) and the pattern item of each tag
    static std::vector<BannerSpec> const specs{
        {"naga", "twilightforest:naga", "tf_naga"},
        {"lich", "twilightforest:lich", "tf_lich"},
        {"minoshroom", "twilightforest:minoshroom", "tf_minoshroom"},
        {"hydra", "twilightforest:hydra", "tf_hydra"},
        {"knight_phantom", "twilightforest:knight_phantom", "tf_knight_phantom"},
        {"ur_ghast", "twilightforest:ur_ghast", "tf_ur_ghast"},
        {"alpha_yeti", "twilightforest:alpha_yeti", "tf_alpha_yeti"},
        {"snow_queen", "twilightforest:snow_queen", "tf_snow_queen"},
        {"quest_ram", "twilightforest:questing_ram", "tf_quest_ram"},
    };
    return specs;
}

std::optional<BannerSpec> bannerOfPatternId(std::string_view nameId) {
    for (auto const& s : bannerPatterns())
        if (s.nameId == nameId) return s;
    return std::nullopt;
}

std::optional<BannerSpec> bannerOfItem(std::string_view itemId) {
    for (auto const& s : bannerPatterns())
        if (itemId == std::string(kNs) + s.boss + "_banner_pattern") return s;
    return std::nullopt;
}

namespace {

// BannerPatternItem::Type has 10 vanilla values; TF's patterns take the values after them (the type
// is the pattern's own index into the item types, never a vanilla item's)
constexpr short kFirstPatternItemType = 10;

int& appendedCount() {
    static int n = 0;
    return n;
}

std::vector<BannerPattern>& registry() { return const_cast<std::vector<BannerPattern>&>(BannerPattern::mPatterns()); }

} // namespace

int registerBannerPatterns() {
    auto& patterns = registry();
    int   added    = 0;
    short type     = kFirstPatternItemType;
    for (auto const& s : bannerPatterns()) {
        bool known = std::any_of(patterns.begin(), patterns.end(), [&](BannerPattern const& p) { return *p.mNameID == s.nameId; });
        ++type;
        if (known) continue;
        std::string item = std::string(kNs) + s.boss + "_banner_pattern";
        patterns.emplace_back(s.nameId, s.nameId, [item] { return game::items::make(item).value_or(ItemStack{}); }, static_cast<short>(type - 1), true);
        patterns.back().mID = static_cast<uchar>(patterns.size() - 1);
        ++added;
    }
    appendedCount() += added;
    return added;
}

bool bannerPatternsRegistered() {
    auto const& patterns = BannerPattern::mPatterns();
    for (auto const& s : bannerPatterns())
        if (std::none_of(patterns.begin(), patterns.end(), [&](BannerPattern const& p) { return *p.mNameID == s.nameId; })) return false;
    return true;
}

BannerPattern const* findBannerPattern(std::string_view nameId) {
    for (auto const& p : BannerPattern::mPatterns())
        if (*p.mNameID == nameId) return &p;
    return nullptr;
}

} // namespace gf::feature::building

namespace {

namespace fb = gf::feature::building;

/// The TF pattern a TF pattern item unlocks (the item's tag #twilightforest:pattern_item/<boss>).
bool tfMatch(BannerPattern const& pattern, ItemStackBase const& item, bool& isTf) {
    auto spec = fb::bannerOfPatternId(*pattern.mNameID);
    isTf      = spec.has_value();
    if (!spec || item.isNull()) return false;
    return item.getTypeName() == std::string(fb::kNs) + spec->boss + "_banner_pattern";
}

LL_TYPE_INSTANCE_HOOK(BannerMatchHook, ll::memory::HookPriority::Normal, BannerPattern, &BannerPattern::matchesPatternItem, bool, ItemStackBase const& item) {
    bool isTf  = false;
    bool match = tfMatch(*this, item, isTf);
    if (isTf) return match;
    // a vanilla pattern never takes a TF item
    if (!item.isNull() && fb::bannerOfItem(item.getTypeName())) return false;
    return origin(item);
}

LL_TYPE_INSTANCE_HOOK(
    LoomPatternSlotHook,
    ll::memory::HookPriority::Normal,
    LoomMaterialContainerValidation,
    &LoomMaterialContainerValidation::$isItemAllowedInSlot,
    bool,
    ContainerScreenContext const& screenContext,
    int const                     slot,
    ItemStackBase const&          item,
    int const                     amount,
    bool                          flag
) {
    // BannerPatternItem in LoomMenu's pattern slot: TF's pattern items too
    if (!item.isNull() && fb::bannerOfItem(item.getTypeName())) return true;
    return origin(screenContext, slot, item, amount, flag);
}

} // namespace

namespace gf::feature::building {

void installBannerHooks() {
    BannerMatchHook::hook();
    LoomPatternSlotHook::hook();
}

void uninstallBannerHooks() {
    BannerMatchHook::unhook();
    LoomPatternSlotHook::unhook();
}

} // namespace gf::feature::building

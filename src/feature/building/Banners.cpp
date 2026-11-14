// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/init/TFBannerPatterns.java
//   src/main/java/gloamingforest/data/tags/CustomTagGenerator.java (BannerPatternTagGenerator)
//   src/main/java/gloamingforest/init/TFItems.java (the BannerPatternItems)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building): upstream's nine banner patterns (TFBannerPatterns: gloamingforest:<boss>, the quest ram's
// is questing_ram) and their pattern items (<boss>_banner_pattern, each the only member of
// #gloamingforest:pattern_item/<boss>), used in the loom like vanilla pattern items (LoomMenu: the
// pattern item in the pattern slot unlocks its pattern; the item is kept).
//
// Bedrock (spike S6): the banner pattern registry (BannerPattern::mPatterns) is a plain vector with
// MCAPI constructors. Both builds append upstream's nine at mod load (the BDS validates the loom's craft
// request against it, the client's loom lists and draws from it), as patterns that need an item
// (BannerPattern(name, nameID, itemFunctor, patternItemType, ignoreAux)). A banner stores the
// pattern's nameID (kPatternIds); the renderer draws textures/entity/banner/banner_<name> (upstream's
// textures/entity/banner/<boss>.png, renamed by tools/import/overrides/building.json). The engine
// knows pattern items only as the BannerPatternItem class: the contract extension
// feature/ext/building/PatternItems.h (two engine hooks, for A8 to fold into W0) lets upstream's
// data-driven items in: BannerPattern::matchesPatternItem (the pattern whose item is in the slot)
// and the loom's pattern slot validation (LoomMaterialContainerValidation::isItemAllowedInSlot).
// Vanilla clients (no client mod on a BDS) do not know the nine ids: Bedrock clients keep a banner
// with an unknown pattern and draw it as a plain field (spike S6), with an untranslated line.

#include "feature/building/Building.h"

#include <algorithm>

#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/block/actor/BannerPattern.h"

#include "feature/ext/building/PatternItems.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::building {

std::vector<BannerSpec> const& bannerPatterns() {
    // TFBannerPatterns (registry id, asset id) and the pattern item of each tag
    static std::vector<BannerSpec> const specs{
        {"naga", "gloamingforest:naga", "tf_naga"},
        {"lich", "gloamingforest:lich", "tf_lich"},
        {"minoshroom", "gloamingforest:minoshroom", "tf_minoshroom"},
        {"hydra", "gloamingforest:hydra", "tf_hydra"},
        {"knight_phantom", "gloamingforest:knight_phantom", "tf_knight_phantom"},
        {"ur_ghast", "gloamingforest:ur_ghast", "tf_ur_ghast"},
        {"alpha_yeti", "gloamingforest:alpha_yeti", "tf_alpha_yeti"},
        {"snow_queen", "gloamingforest:snow_queen", "tf_snow_queen"},
        {"quest_ram", "gloamingforest:questing_ram", "tf_quest_ram"},
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

// BannerPatternItem::Type has 10 vanilla values; upstream's patterns take the values after them (the type
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
    // the client loads mods before the game's static initialisers ran: the vanilla registry is still
    // empty then (and is constructed over whatever was appended), so wait for install()
    if (patterns.empty()) return -1;
    int   added    = 0;
    // one allocation for all nine: the vector moves at most once, before the engine holds pattern
    // pointers (both builds append before a world exists)
    auto const* before   = patterns.data();
    auto const  capacity = patterns.capacity();
    patterns.reserve(patterns.size() + bannerPatterns().size());
    // the evidence for the review: whether the vanilla patterns moved (they move only when the
    // vector had no room; nothing in the engine keeps BannerPattern pointers across this: banners
    // and block actors store pattern ids, BannerItem resolves layers on demand)
    log().info("building: banner pattern registry {} (capacity {} -> {})", patterns.data() == before ? "kept in place" : "moved once, before any world", capacity, patterns.capacity());
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

void logBannerIds() {
    std::string line;
    for (auto const& s : bannerPatterns()) {
        auto const* p = findBannerPattern(s.nameId);
        line += " " + s.nameId + "=" + (p ? std::to_string(static_cast<int>(p->mID)) : std::string("?"));
    }
    log().info("building: banner pattern ids (of {}){}", BannerPattern::mPatterns().size(), line);
}

BannerPattern const* findBannerPattern(std::string_view nameId) {
    for (auto const& p : BannerPattern::mPatterns())
        if (*p.mNameID == nameId) return &p;
    return nullptr;
}

} // namespace gf::feature::building

namespace gf::feature::building {

void installBannerHooks() {
    // each pattern item is the only member of #gloamingforest:pattern_item/<boss>
    for (auto const& s : bannerPatterns()) ::gf::building::ext::registerPatternItem(s.nameId, std::string(kNs) + s.boss + "_banner_pattern");
    ::gf::building::ext::installPatternItems();
}

void uninstallBannerHooks() { ::gf::building::ext::uninstallPatternItems(); }

} // namespace gf::feature::building

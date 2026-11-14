#pragma once

// Extension of contract "recipes" (loom) by U5 (rule R18; to be folded into W0 by A8).
//
// Banner pattern items that are data-driven items. Bedrock knows loom pattern items only as its
// BannerPatternItem class: BannerPattern::matchesPatternItem decides which pattern an item in the
// loom's pattern slot unlocks, and LoomMaterialContainerValidation::isItemAllowedInSlot which items
// that slot takes. This extension owns the two engine hooks (rule R8: one hook per engine event)
// and lets a unit pair a pattern (its nameID, as banners store it) with the one item that unlocks
// it (Java: the item tag #<ns>:pattern_item/<name> of LoomMenu):
//
//   gf::building::ext::registerPatternItem("gf_naga", "gloamingforest:naga_banner_pattern");
//
// A registered pattern is unlocked only by its item; a registered item unlocks no other pattern
// (vanilla patterns included); the loom's pattern slot (and only it, on a loom screen) takes a
// registered item.

#include <string>

namespace gf::building::ext {

void registerPatternItem(std::string const& patternNameId, std::string const& itemId);
/// The registered item of a pattern ("" when none).
[[nodiscard]] std::string patternItemOf(std::string const& patternNameId);
/// Whether `itemId` is a registered pattern item.
[[nodiscard]] bool isPatternItem(std::string const& itemId);
void installPatternItems();
void uninstallPatternItems();

} // namespace gf::building::ext

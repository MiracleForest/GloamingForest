#pragma once

// Contract "data", creative catalog (header owned by F0, implemented by F9; spike S6).
//
// upstream's 4 creative tabs (GloamingCreativeTabs: blocks, items, equipment, ... ) become Bedrock creative
// catalog groups (Bedrock has no top-level tabs). The catalog content is imported data
// (assets/gen/import, F1); this is the runtime query and the extension point.

#include <string>
#include <vector>

namespace gf::catalog {

struct Group {
    std::string              id;        ///< "gloamingforest:blocks"
    std::string              iconItem;
    std::string              nameKey;   ///< "itemGroup.gloamingforest.blocks"
    std::vector<std::string> items;     ///< in upstream's order
};

/// The groups in upstream's order.
[[nodiscard]] std::vector<Group> const& groups();
/// The group an item is listed in ("" when none).
[[nodiscard]] std::string groupOf(std::string const& itemId);

} // namespace gf::catalog

#pragma once

// The vanilla side of the tag runtime (F9). TF's data only *adds* members to vanilla tags
// (`minecraft:logs` holds TF's logs, `replace: false`); the vanilla members themselves are
// Mojang's data, which Bedrock does not have in that form. This bridge answers the vanilla tags TF
// and its data reference (the 52 `#minecraft:` references of TF's data and the BlockTags / ItemTags
// / EntityTypeTags / FluidTags of its code) for Bedrock ids, from Bedrock's own block and item tags
// (`log`, `minecraft:is_sword`, `minecraft:is_pickaxe_item_destructible`, ...), the item's armour
// slot and durability, and id lists / name patterns of the vanilla content. Pattern rules apply to
// `minecraft:` ids only; Bedrock's native tags apply to any id. Tags without a bridge have only
// their data members.

#include <string>
#include <vector>

#include "feature/tags/Tags.h"

namespace gf::tags::vanilla {

/// Whether the bridge knows `tag` of `registry` ("minecraft:logs").
[[nodiscard]] bool known(Registry registry, std::string const& tag);
/// Whether the vanilla tag holds `id` (false for unknown tags).
[[nodiscard]] bool has(Registry registry, std::string const& tag, std::string const& id);
/// The ids of the game's registry that the bridge puts into `tag` (blocks / items: every registered
/// id; entity types and fluids: the bridge's id lists). Empty without a level.
[[nodiscard]] std::vector<std::string> members(Registry registry, std::string const& tag);
/// The vanilla tags the bridge knows for `registry` (diagnostics, selftests).
[[nodiscard]] std::vector<std::string> knownTags(Registry registry);

} // namespace gf::tags::vanilla

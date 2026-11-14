#pragma once

// Contract "data", tags (header owned by F0, implemented by F9).
//
// upstream's 154 tags plus the vanilla / c: tags they reference, imported as data (F1) and merged
// from union fragments assets/tags/<registry>/<tag>.d/<u>.json (rule R6: members are added only
// through fragments). Tag ids are written without '#': "gloamingforest:portal/activator".
//
//     if (gf::tags::has(gf::tags::Registry::Block, "gloamingforest:mazestone", blockId)) ...;
//
// Interactions matchers (feature/common/Interactions.h, Match::tag) use these.

#include <string>
#include <vector>

namespace gf::tags {

enum class Registry { Block, Item, EntityType, Biome, Fluid, DamageType, Enchantment, Structure, BannerPattern, PaintingVariant, GameEvent, Instrument };

/// Whether `id` is a member of `tag` in `registry` (nested tags resolved).
[[nodiscard]] bool has(Registry registry, std::string const& tag, std::string const& id);
/// Every member of `tag` (nested tags resolved, sorted).
[[nodiscard]] std::vector<std::string> members(Registry registry, std::string const& tag);
/// The registry's directory name in data packs ("block", "item", "entity_type", "worldgen/biome", ...).
[[nodiscard]] char const* registryName(Registry registry);

} // namespace gf::tags

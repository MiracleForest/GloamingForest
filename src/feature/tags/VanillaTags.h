#pragma once

// The vanilla side of the tag runtime (F9). TF's data only *adds* members to vanilla tags
// (`minecraft:logs` holds TF's logs, `replace: false`); the vanilla members themselves are
// Mojang's data, which Bedrock does not have in that form. This bridge answers the vanilla tags TF
// and its data reference (the 52 `#minecraft:` references of TF's data and the BlockTags / ItemTags
// / EntityTypeTags / FluidTags / BiomeTags of its code and the W0 runtimes, Minecraft 1.21.1's
// structure tags) and the NeoForge common tags (`c:seeds`, `c:ores`, `c:rods/wooden`, ... with
// NeoForge 21.1's vanilla members: TF's data adds only TF entries to them) for Bedrock ids, from
// Bedrock's own block and item tags (`log`, `minecraft:is_sword`,
// `minecraft:is_pickaxe_item_destructible`, ...), the item's armour slot and durability, and id
// lists / name patterns of the vanilla content. Pattern rules apply to `minecraft:` ids only;
// Bedrock's native tags apply to any id. Tags without a bridge have only their data members.
//
// A rule is one vanilla tag file: its own members (the id list and the patterns) and the tags it
// includes (Java's "#minecraft:..." entries). The tag runtime (Tags.cpp) walks the includes itself,
// so an included tag brings its data members (TF's additions) as well as its rule.

#include <string>
#include <vector>

#include "feature/tags/Tags.h"

namespace gf::tags::vanilla {

/// One bridged vanilla tag (opaque; VanillaTags.cpp).
struct Rule;

/// The bridge's rule of `tag` ("minecraft:logs"), or null.
[[nodiscard]] Rule const* find(Registry registry, std::string const& tag);
/// Whether the bridge knows `tag` of `registry`.
[[nodiscard]] bool known(Registry registry, std::string const& tag);
/// The tags a rule includes ("minecraft:dirt", ...: Java's "#" entries of the tag file).
[[nodiscard]] std::vector<std::string> includes(Rule const& rule);
/// The ids a rule lists by name ("minecraft:oak_log", ...; the rule's excludes removed).
[[nodiscard]] std::vector<std::string> listedIds(Rule const& rule);
/// Whether a rule also matches by pattern (name suffix, Bedrock native tag, predicate).
[[nodiscard]] bool hasPatterns(Rule const& rule);
/// Whether `id` (namespaced) matches the rule's patterns (its listed ids and includes not checked).
[[nodiscard]] bool matchesPattern(Registry registry, Rule const& rule, std::string const& id);
/// The ids of the game's registry the bridge enumerates for members (blocks / items: every
/// registered id, empty without a level; other registries: every id the bridge's rules list).
[[nodiscard]] std::vector<std::string> registryIds(Registry registry);
/// The vanilla tags the bridge knows for `registry` (diagnostics, selftests).
[[nodiscard]] std::vector<std::string> knownTags(Registry registry);

} // namespace gf::tags::vanilla

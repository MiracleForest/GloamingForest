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
//
// Policy: the vanilla members are exactly Java 1.21.1's (TF 4.8's Minecraft; NeoForge 21.1 for c:),
// spelled with Bedrock's ids (a Bedrock id that names the same block or item, legacy aliases
// included). Bedrock content newer than 1.21.1 (pale oak, resin, the 1.21.5 plants, copper tools,
// armour, chests, bars, chains, lanterns and golems with their waxed and oxidised variants, ...) is
// a member of no bridged tag through an id list, a name pattern or a predicate. Bedrock's native
// tags are the exception and are taken as they are, newer content included: they are Bedrock's own
// mechanics (what a pickaxe mines, what a sword, a log or a trim material is), and Java 1.21.1 has
// no reference for these blocks to follow, so leaving them out would only take behaviour away (pale
// oak planks no axe mines faster, a resin brick no pickaxe mines faster). The trade-off: a newer
// block or item is in a tag only where a Bedrock native tag puts it (#mineable/axe holds
// pale_oak_planks through is_axe_item_destructible, while #planks, a suffix rule, does not; #logs
// holds pale_oak_log only where Bedrock's own log tags do (item.tags_java_1211_only logs which); #swords holds the
// copper sword through minecraft:is_sword, while the armour tags, Java's id lists, hold no copper
// armour). Bedrock wearables that are no armour in Java (carved pumpkins,
// skulls, mob heads) are in no armour tag. TF's data members are added on top, as Java's tag loader
// does.

#include <cstddef>
#include <memory>
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
/// The ids of the game's blocks and items that Bedrock's native tags put in each rule (the rules'
/// `native` part), read from the game's registries once (opaque; VanillaTags.cpp). Queries never
/// touch the game's registries: world generation asks from its own threads.
struct NativeIndex;
/// Reads the game's block and item registries into a new index (empty for a registry the game has
/// not filled yet: no level). Call it on the server thread only.
[[nodiscard]] std::shared_ptr<NativeIndex const> buildNativeIndex();
/// Whether two indexes hold the same members (null and empty are the same).
[[nodiscard]] bool sameNativeIndex(NativeIndex const* a, NativeIndex const* b);
/// The number of (rule, id) pairs of an index (diagnostics).
[[nodiscard]] std::size_t nativeIndexSize(NativeIndex const* native);
/// Whether `id` (namespaced) matches the rule's patterns (its listed ids and includes not checked):
/// name suffixes and predicates by the id, Bedrock's native tags through `native` (none without one).
/// Thread-safe: reads no game state.
[[nodiscard]] bool matchesPattern(Rule const& rule, std::string const& id, NativeIndex const* native);
/// The ids of the game's registry the bridge enumerates for members (blocks / items: every
/// registered id, empty without a level; other registries: every id the bridge's rules list).
[[nodiscard]] std::vector<std::string> registryIds(Registry registry);
/// Whether `id` (namespaced) is Bedrock content newer than Java 1.21.1 (the policy above).
[[nodiscard]] bool newerThanJava1211(std::string const& id);
/// The vanilla tags the bridge knows for `registry` (diagnostics, selftests).
[[nodiscard]] std::vector<std::string> knownTags(Registry registry);

} // namespace gf::tags::vanilla

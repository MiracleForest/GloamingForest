#pragma once

// Extension of contract "data" (tags) by F9 (rule R18; to be folded into feature/tags/Tags.h by A8).
//
// `gf::tags::Registry` names the vanilla registries only. upstream's own registries keep tags too
// (data/gloamingforest/tags/gloaming/wood_palettes/*.json, the `gloaming/#gloamingforest:...` rows of
// owners.tsv): these functions query any tag directory by name. A directory of a `tags::Registry`
// ("block", "item", "worldgen/biome", ...) answers exactly as gf::tags::has / members (the vanilla
// and c: bridge included); other directories have their data members only.
//
//     if (gf::tags::ext::hasIn("gloaming", "gloamingforest:wood_palettes/common", "gloamingforest:canopy")) ...;
//
// Java ids: upstream's data (tags, data maps, recipes and every other file of the data tree) names vanilla
// content by its Java id; the tag, data map and recipe runtimes hold the game's Bedrock ids. A unit
// that reads Java data itself (its own recipe loader, a data file of its kind) renames the vanilla
// ids it reads with bedrockIds / bedrockId (never ids that are Bedrock already: the renaming is not
// idempotent, Bedrock's minecraft:snow is Java's snow_block).
//
//     auto id = gf::tags::ext::bedrockId("item", "minecraft:cobweb"); // "minecraft:web"

#include <string>
#include <vector>

namespace gf::tags::ext {

/// Whether `id` is in `tag` of the tag directory `dir` ("gloaming", "block", "worldgen/biome", ...).
[[nodiscard]] bool hasIn(std::string const& dir, std::string const& tag, std::string const& id);
/// The members of `tag` in directory `dir` (nested tags resolved, sorted).
[[nodiscard]] std::vector<std::string> membersIn(std::string const& dir, std::string const& tag);

/// The Bedrock ids of a Java id of registry directory `dir` ("block", "item", "entity_type"): more
/// than one for a split id (the block minecraft:water: water and flowing_water), else the id itself.
[[nodiscard]] std::vector<std::string> bedrockIds(std::string const& dir, std::string const& javaId);
/// The main Bedrock id of a Java id (the first of bedrockIds).
[[nodiscard]] std::string bedrockId(std::string const& dir, std::string const& javaId);

} // namespace gf::tags::ext

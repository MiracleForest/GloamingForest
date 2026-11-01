#pragma once

// Extension of contract "data" (tags) by F9 (rule R18; to be folded into feature/tags/Tags.h by A8).
//
// `gf::tags::Registry` names the vanilla registries only. TF's own registries keep tags too
// (data/twilightforest/tags/twilight/wood_palettes/*.json, the `twilight/#twilightforest:...` rows of
// owners.tsv): these functions query any tag directory by name. A directory of a `tags::Registry`
// ("block", "item", "worldgen/biome", ...) answers exactly as gf::tags::has / members (the vanilla
// and c: bridge included); other directories have their data members only.
//
//     if (gf::tags::ext::hasIn("twilight", "twilightforest:wood_palettes/common", "twilightforest:canopy")) ...;

#include <string>
#include <vector>

namespace gf::tags::ext {

/// Whether `id` is in `tag` of the tag directory `dir` ("twilight", "block", "worldgen/biome", ...).
[[nodiscard]] bool hasIn(std::string const& dir, std::string const& tag, std::string const& id);
/// The members of `tag` in directory `dir` (nested tags resolved, sorted).
[[nodiscard]] std::vector<std::string> membersIn(std::string const& dir, std::string const& tag);

} // namespace gf::tags::ext

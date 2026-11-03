#pragma once

// Internals of the tag runtime shared with F9's selftests and extension headers.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "feature/tags/Tags.h"

namespace gf::tags::internal {

/// The registry directories that have tag files ("block", "item", "worldgen/biome", "twilight", ...).
[[nodiscard]] std::vector<std::string> registryDirs();
/// The tags of one directory.
[[nodiscard]] std::vector<std::string> tagsOf(std::string const& dir);
/// Membership / members by directory name: a `tags::Registry` directory answers as has / members
/// (bridge included); TF's own registries ("twilight": wood palettes, ...) have no vanilla bridge.
[[nodiscard]] bool                     hasIn(std::string const& dir, std::string const& tag, std::string const& id);
[[nodiscard]] std::vector<std::string> membersIn(std::string const& dir, std::string const& tag);
/// The data members of a tag (nested data tags flattened, the data's Java ids renamed to Bedrock's;
/// without the vanilla bridge, whose members come from the game's registries).
[[nodiscard]] std::vector<std::string> dataMembers(std::string const& dir, std::string const& tag);
/// The bridged vanilla tags a tag reaches through its includes (data and bridge), sorted.
[[nodiscard]] std::vector<std::string> bridgedOf(Registry registry, std::string const& tag);
[[nodiscard]] std::size_t              fileCount();
/// The published snapshot's generation (every load publishes a new one).
[[nodiscard]] std::uint64_t generation();
void                        reload();

} // namespace gf::tags::internal

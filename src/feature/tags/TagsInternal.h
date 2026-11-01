#pragma once

// Internals of the tag runtime shared with F9's selftests and extension headers.

#include <cstddef>
#include <string>
#include <vector>

namespace gf::tags::internal {

/// The registry directories that have tag files ("block", "item", "worldgen/biome", "twilight", ...).
[[nodiscard]] std::vector<std::string> registryDirs();
/// The tags of one directory.
[[nodiscard]] std::vector<std::string> tagsOf(std::string const& dir);
/// Membership / members by directory name, for registries outside `tags::Registry` (TF's
/// "twilight" registries: wood palettes, ...); no vanilla bridge there.
[[nodiscard]] bool                     hasIn(std::string const& dir, std::string const& tag, std::string const& id);
[[nodiscard]] std::vector<std::string> membersIn(std::string const& dir, std::string const& tag);
[[nodiscard]] std::size_t              fileCount();
void                                   reload();

} // namespace gf::tags::internal

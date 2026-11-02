#pragma once

// Internals of the data map runtime shared with F9's selftests.

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gf::datamaps::internal {

/// Every loaded data map: (map id, registry directory), sorted by id.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> maps();
/// The ids a map names directly (its tag entries left out), sorted.
[[nodiscard]] std::vector<std::string> directKeys(std::string const& mapId);
/// The value fields of a map known to hold ids (field -> registry directory).
[[nodiscard]] std::map<std::string, std::string> idFields(std::string const& mapId);
/// Removes a field registered with ext::registerIdField (selftests); the maps load again.
void unregisterIdField(std::string const& mapId, std::string const& field);

} // namespace gf::datamaps::internal

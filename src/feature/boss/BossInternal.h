#pragma once

// Internals of F8's boss kit for its selftests (not a contract).

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "core/GlobalPos.h"

namespace gf::feature::boss {

/// Treats `type` as a member of twilightforest:multiplayer_inclusive_entities (fixtures).
void markInclusiveForTest(std::string const& type);
/// The last death chest placed and the last conquest requested (selftests).
[[nodiscard]] std::optional<core::GlobalPos>                         lastDeathChest();
[[nodiscard]] std::optional<std::pair<core::GlobalPos, std::string>> lastConquest();
void                                                                 resetTestRecords();
/// Whether the boss with this unique id has a bar.
[[nodiscard]] bool hasBar(std::int64_t uid);
/// Removes a spawner registration (fixture spawners live only while their test runs).
void unregisterSpawnerForTest(std::string const& blockId);

} // namespace gf::feature::boss

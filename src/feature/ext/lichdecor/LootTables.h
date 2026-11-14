#pragma once

// U22 (lichdecor) contract extension (rule R18, for A8 to fold into feature/loot/Loot.h): whether a
// loot table id is known to F10's table store (the data file <mod>/data/<ns>/loot_table/<path>.json
// that gf::loot::roll reads). The loot contract has no existence query; /gloamingforest
// sinister_spawner set_loot needs one to report Java's "Failed to get element" error.

#include <string>

namespace gf::loot::ext {

/// True when `tableId` ("gloamingforest:blocks/ominous_spawner_drops") has a table file.
[[nodiscard]] bool tableExists(std::string const& tableId);

} // namespace gf::loot::ext

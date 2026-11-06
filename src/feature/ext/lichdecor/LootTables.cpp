// U22 (lichdecor) contract extension: gf::loot::ext::tableExists (see LootTables.h).

#include "feature/ext/lichdecor/LootTables.h"

#include <filesystem>
#include <system_error>

#include "mod/Main.h"

namespace gf::loot::ext {

bool tableExists(std::string const& tableId) {
    auto colon = tableId.find(':');
    if (colon == std::string::npos) return false;
    try {
        auto data = Mod::getInstance().getSelf().getModDir() / "data" / tableId.substr(0, colon);
        for (char const* dir : {"loot_table", "loot_tables"}) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(data / dir / (tableId.substr(colon + 1) + ".json"), ec)) return true;
        }
    } catch (...) {}
    return false;
}

} // namespace gf::loot::ext

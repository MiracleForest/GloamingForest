#pragma once

// The mod's data tree, shared by the F9 runtimes (tags, data maps, recipes, enchantments, Travellers'
// Gear modifiers): `<mod>/data/<namespace>/<registry dir>/<path>.json`, the Java-format data that
// assemble copies from the gen layer (assets/gen/import/<unit>/data/**, F1), the unit data
// (assets/data/<kind>/<unit>.json -> data/gf/<kind>/<unit>.json) and the fixture data
// (assets/test/data/** -> data/gf_test/**, development builds).
//
// Loaded on the server thread at mod enable and again on `/gloamingforest reload` (config::onReload);
// `onReload` listeners rebuild their caches then.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::feature::item::data {

/// `<mod dir>/data` (an override for selftests: `setRoot`).
[[nodiscard]] std::filesystem::path root();
void                                setRoot(std::filesystem::path dir);
/// Whether a selftest's override is set (the tag runtime then publishes nothing: the test reads its
/// tags through a private tags::internal::ScopedSnapshot, other threads keep the mod's tags).
[[nodiscard]] bool rootOverridden();

struct File {
    std::string           ns;   ///< "gloamingforest"
    std::string           rel;  ///< the path below `<ns>/<dir>/`, '/'-separated, without ".json"
    std::filesystem::path path; ///< the file
    /// "<ns>:<rel>"
    [[nodiscard]] std::string id() const { return ns + ":" + rel; }
};

/// Every JSON file below `data/<ns>/<dir>/` for every namespace, sorted by namespace and path.
[[nodiscard]] std::vector<File> list(std::string_view dir);
/// The same below another data tree (`base` in place of `root()`: selftests' private trees).
[[nodiscard]] std::vector<File> list(std::string_view dir, std::filesystem::path const& base);
/// The file parsed (null with one warning when it is not valid JSON).
[[nodiscard]] nlohmann::json read(std::filesystem::path const& file);

/// Called after the data was (re)loaded: mod enable (in order of registration) and
/// `/gloamingforest reload`.
void onReload(std::function<void()> fn);
/// Runs every reload listener (the module does it; selftests may too).
void reloadAll();

/// "minecraft:stone" for "stone"; ids with a namespace stay as they are.
[[nodiscard]] std::string withNamespace(std::string_view id);

} // namespace gf::feature::item::data

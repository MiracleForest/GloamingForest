#pragma once

// F10-internal parts of the gamerule store (not a contract: other units use GameRules.h).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "feature/gamerules/GameRules.h"

namespace gf::gamerules {

/// Every known rule with its current value ({"tfEnforcedProgression": true, ...}).
[[nodiscard]] nlohmann::json snapshot();
/// Called after `set` changed a rule (server thread).
void onChanged(std::function<void(std::string const& id, Value const& value)> fn);
/// The known rule ids, sorted.
[[nodiscard]] std::vector<std::string> ids();
/// A command value for rule `id`: "true" / "false" for booleans, an integer otherwise.
[[nodiscard]] std::optional<Value> parse(std::string const& id, std::string const& text);

#ifdef GF_CLIENT
namespace client {
/// The value the server synced for `id` (null when unknown).
[[nodiscard]] nlohmann::json value(std::string const& id);
void                         apply(nlohmann::json const& rules);
void                         reset();
} // namespace client
#endif

} // namespace gf::gamerules

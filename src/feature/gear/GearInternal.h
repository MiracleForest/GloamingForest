#pragma once

// Internals of the Travellers' Gear query shared with F9's selftests.

#include <cstddef>
#include <string>

namespace gf::gear::internal {

/// Adds a modifier to the registry as if the data had it (selftests; the data's entries load from
/// data/twilightforest/twilight/travellers_modifiers).
void registerModifier(std::string const& id, std::string const& group, bool builtin = false);
void unregisterModifier(std::string const& id);
[[nodiscard]] std::size_t modifierCount();

} // namespace gf::gear::internal

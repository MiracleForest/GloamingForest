#pragma once

// Contract "rules+fx", gamerules (header owned by F0, implemented by F10).
//
// TF's gamerules (tfEnforcedProgression, playersTfPortalDefaultDelay,
// playersTfPortalCreativeDelay) are per-world values with `/twilightforest gamerule <id> [value]`
// (Bedrock's /gamerule only knows vanilla rules). Owners register them at install:
//
//     gf::gamerules::registerBool("tfEnforcedProgression", true);
//     bool on = gf::gamerules::getBool("tfEnforcedProgression");

#include <string>
#include <variant>

namespace gf::gamerules {

using Value = std::variant<bool, int>;

void registerBool(std::string const& id, bool defaultValue);
void registerInt(std::string const& id, int defaultValue);
/// The rule's value in the current world (its default when unset; false / 0 when unknown).
[[nodiscard]] Value get(std::string const& id);
[[nodiscard]] bool  getBool(std::string const& id);
[[nodiscard]] int   getInt(std::string const& id);
/// Sets the rule in the current world; false when unknown or of the other type.
bool set(std::string const& id, Value value);

} // namespace gf::gamerules

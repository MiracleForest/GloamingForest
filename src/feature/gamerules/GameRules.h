#pragma once

// Contract "rules+fx", gamerules (header owned by F0, implemented by F10).
//
// upstream's gamerules (gloamingEnforcedProgression, playersGloamingPortalDefaultDelay,
// playersGloamingPortalCreativeDelay) are per-world values with `/gloamingforest gamerule <id> [value]`
// (Bedrock's /gamerule only knows vanilla rules). Owners register them at install:
//
//     gf::gamerules::registerBool("gloamingEnforcedProgression", true);
//     bool on = gf::gamerules::getBool("gloamingEnforcedProgression");

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

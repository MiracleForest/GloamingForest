// Adapted from LiteLDev/Levitate v0.1.0 (src/core/Glob.h), LGPL-3.0-or-later.
#pragma once

#include <string_view>

namespace gf::core {

/// Shell-style match: `*` = any run of characters, `?` = one character, everything else literal.
[[nodiscard]] bool globMatch(std::string_view pattern, std::string_view text);

} // namespace gf::core

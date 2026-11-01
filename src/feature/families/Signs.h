#pragma once

// Contract "families+blockevents", sign text (header owned by F0, implemented by F11; S3 decides
// whether TF's 32 sign blocks keep their text in data-driven block actors or BlockData).
//
//     gf::signs::setText(pos, {"Here lies", "a naga"});

#include <string>
#include <vector>

#include "core/GlobalPos.h"

namespace gf::signs {

/// The lines of the sign at `pos` (`front` or back side); empty when it is not a sign.
[[nodiscard]] std::vector<std::string> text(core::GlobalPos const& pos, bool front = true);
/// Sets the lines (at most 4) of the sign at `pos`; false when it is not a sign.
bool setText(core::GlobalPos const& pos, std::vector<std::string> const& lines, bool front = true);

} // namespace gf::signs

// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Log.h), LGPL-3.0-or-later.
#pragma once

#include "ll/api/io/Logger.h"

namespace gf {

/// The mod's logger (LeviLamina console + log file, prefixed "GloamingForest").
/// Every message is flushed immediately so e2e harnesses can tail the log. No per-tick logging:
/// log one line per user-visible action that e2e scenarios can wait for.
[[nodiscard]] ll::io::Logger& log();

} // namespace gf

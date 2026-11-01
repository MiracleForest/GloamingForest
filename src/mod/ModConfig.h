// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/ModConfig.h), LGPL-3.0-or-later.
#pragma once

#include <string>

namespace gf {

/// `config/mod.json`: foundation-wide settings. Feature options live in their own sections,
/// owned by the units that use them (`GF_REGISTER_CONFIG`, rule R13).
struct ModConfig {
    int         version = 1;
    /// Language for server messages and GUI text when a player's locale has no translation
    /// ("en_US", "zh_CN", ...).
    std::string language = "en_US";
    /// Use each player's client locale for GUI text when a translation exists.
    bool        usePlayerLocale = true;
    /// BDS only: require clients to accept the resource pack (textures / names of the blocks).
    bool        requireResourcePack = true;
    /// Log one warning the first time each contract stub is called (W0 development aid).
    bool        logContractStubs = true;
};

} // namespace gf

// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Main.h), LGPL-3.0-or-later.
#pragma once

#include "ll/api/mod/NativeMod.h"

namespace gf {

/// The mod's name: DLL GloamingForest.dll, folder plugins/GloamingForest (BDS) or
/// mods/GloamingForest (client), log prefix "GloamingForest".
inline constexpr char kModName[] = "GloamingForest";

/// Mod entry point. It only drives the module registry (`GF_REGISTER_MODULE`); features never
/// need to edit this file.
class Mod {
public:
    static Mod& getInstance();

    Mod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

    [[nodiscard]] bool isEnabled() const { return mEnabled; }

private:
    ll::mod::NativeMod& mSelf;
    bool                mEnabled = false;
};

} // namespace gf

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/ClientGameEvents.java (killVignette, customizeSplashes)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U15 contract extension (rule R18: a new file, to be folded into F12's client.ui by A8). Client
// build only. Two client UI events no W0 contract covers:
// - killVignette: the HUD vignette (HudVignetteRenderer) is not drawn while the local player is in
//   the Twilight Forest (Java sets Gui.vignetteBrightness to 0 there, which leaves the darkening
//   vignette without effect). Java's vignette also carries the world border warning tint, which
//   that leaves alone; Bedrock has no world border and its HudVignetteRenderer is the darkening
//   vignette only (its state is mLastVignetteOpacity / mLastAB, no warning term), so skipping it
//   is the same as zeroing the brightness term;
// - customizeSplashes: on August 19 the title screen splash (SplashTextRenderer) reads "Happy
//   <ordinal> birthday to the Twilight Forest!" (year - 2011, core/Holiday.h).

#ifdef GF_CLIENT

#include <atomic>
#include <ctime>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HudVignetteRenderer.h"
#include "mc/client/gui/controls/renderers/SplashTextRenderer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/world/level/dimension/Dimension.h"

#include "client/Client.h"
#include "core/Holiday.h"
#include "core/atmosphere/Atmosphere.h"
#include "feature/dimension/World.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::ext::atmosphere {

/// The birthday splash for today (empty on any other day).
std::string birthdaySplash() {
    auto today = ::gf::core::holiday::today();
    if (!::gf::core::holiday::isTwilightBirthday(today)) return {};
    std::time_t t = std::time(nullptr);
    std::tm     tm{};
    localtime_s(&tm, &t);
    return ::gf::core::atmosphere::birthdaySplash(tm.tm_year + 1900);
}

namespace {
std::atomic<bool> gVignetteLogged{false};
std::atomic<bool> gSplashLogged{false};

bool inTwilight() {
    auto* p = ::gf::client::currentPlayer();
    return p && ::gf::world::isTwilight(p->getDimension());
}
} // namespace

} // namespace gf::feature::ext::atmosphere

namespace atmosphere_hooks {
namespace ext = gf::feature::ext::atmosphere;

LL_TYPE_INSTANCE_HOOK(
    KillVignetteHook,
    ll::memory::HookPriority::Normal,
    HudVignetteRenderer,
    &HudVignetteRenderer::$render,
    void,
    MinecraftUIRenderContext& renderContext,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    bool kill = false;
    try {
        kill = ext::inTwilight();
    } catch (...) {}
    if (kill) {
        if (!ext::gVignetteLogged.exchange(true)) gf::log().info("atmosphere: vignette off in the Twilight Forest");
        return;
    }
    origin(renderContext, client, owner, pass);
}

LL_TYPE_INSTANCE_HOOK(
    BirthdaySplashHook,
    ll::memory::HookPriority::Normal,
    SplashTextRenderer,
    &SplashTextRenderer::$render,
    void,
    MinecraftUIRenderContext& renderContext,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    std::string splash;
    try {
        splash = ext::birthdaySplash();
    } catch (...) {}
    if (!splash.empty() && mHasAttemptedLoad) mSplashText = splash;
    origin(renderContext, client, owner, pass);
    if (!splash.empty()) {
        mSplashText = splash; // after the game picked its own the first time
        if (!ext::gSplashLogged.exchange(true)) gf::log().info("atmosphere: title splash '{}'", splash);
    }
}

} // namespace atmosphere_hooks

namespace gf::feature::ext::atmosphere {
namespace {
class ClientHooksModule {
public:
    void install() {
        atmosphere_hooks::KillVignetteHook::hook();
        atmosphere_hooks::BirthdaySplashHook::hook();
    }
    void uninstall() {
        atmosphere_hooks::BirthdaySplashHook::unhook();
        atmosphere_hooks::KillVignetteHook::unhook();
    }
};
} // namespace
} // namespace gf::feature::ext::atmosphere

GF_REGISTER_MODULE(gf::feature::ext::atmosphere::ClientHooksModule);

#endif // GF_CLIENT

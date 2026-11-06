// U15 contract extension (rule R18, a new file; see SkyView.h). Client build only.
//
// The view override runs before the game draws a frame of LevelRendererCamera (the same event F12's
// client.world adjusts the frame's fog colour and clouds in; this hook runs outside F12's and only
// writes the cloud colour and the sunrise colour of the frame's render object). A8 folds it into
// F12's SkyProvider (a `cloudColor` and `sunrise` member).

#ifdef GF_CLIENT

#include "feature/ext/atmosphere/SkyView.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/minecraft_renderer/objects/CloudRenderObject.h"
#include "mc/deps/minecraft_renderer/objects/SkyRenderObject.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderData.h"
#include "mc/deps/minecraft_renderer/objects/ViewRenderObject.h"

#include "client/Client.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::ext::atmosphere::client {

namespace {
struct Overrides {
    std::mutex                                   mutex;
    std::map<int, std::shared_ptr<ViewFn const>> byDim;
};
Overrides& overrides() {
    static Overrides o;
    return o;
}
std::atomic<unsigned long long> gFrames{0};
std::atomic<bool>               gLogged{false};
} // namespace

void setViewOverride(int dimension, ViewFn fn) {
    std::lock_guard lock(overrides().mutex);
    overrides().byDim[dimension] = std::make_shared<ViewFn const>(std::move(fn));
}

void clearViewOverride(int dimension) {
    std::lock_guard lock(overrides().mutex);
    overrides().byDim.erase(dimension);
}

unsigned long long viewOverrideFrames() { return gFrames.load(); }

/// The override for the local player's dimension now (nullopt: none).
std::optional<ViewOverride> current() {
    auto* p = ::gf::client::currentPlayer();
    if (!p) return std::nullopt;
    std::shared_ptr<ViewFn const> fn;
    {
        std::lock_guard lock(overrides().mutex);
        auto            it = overrides().byDim.find(static_cast<int>(p->getDimensionId()));
        if (it == overrides().byDim.end()) return std::nullopt;
        fn = it->second;
    }
    return fn && *fn ? (*fn)() : std::nullopt;
}

} // namespace gf::feature::ext::atmosphere::client

namespace atmosphere_view_hooks {
namespace vc = gf::feature::ext::atmosphere::client;

LL_TYPE_INSTANCE_HOOK(
    AtmosphereViewHook,
    ll::memory::HookPriority::Low,
    LevelRendererCamera,
    &LevelRendererCamera::$render,
    void,
    BaseActorRenderContext& baseEntityRenderContext,
    ViewRenderObject const& renderObj,
    IClientInstance&        ci
) {
    try {
        if (auto o = vc::current()) {
            auto& obj = const_cast<ViewRenderObject&>(renderObj);
            if (o->cloud) {
                auto& c = obj.mCloudState->mCloudColor.get();
                c.r     = (*o->cloud)[0];
                c.g     = (*o->cloud)[1];
                c.b     = (*o->cloud)[2];
            }
            if (o->noSunrise) {
                auto& s = obj.mViewData->mSunriseColor.get();
                s       = mce::Color{0.0f, 0.0f, 0.0f, 0.0f};
            }
            if (o->starBrightness) obj.mSkyState->mStarBrightness = *o->starBrightness;
            if (o->timeOfDay) {
                obj.mSkyState->mTimeOfDay   = *o->timeOfDay;
                obj.mSkyState->mSunAngleOne = *o->timeOfDay;
            }
            vc::gFrames.fetch_add(1, std::memory_order_relaxed);
            if (!vc::gLogged.exchange(true)) gf::log().info("atmosphere: cloud colour and sunrise of the frame overridden");
        }
    } catch (...) {}
    origin(baseEntityRenderContext, renderObj, ci);
}

} // namespace atmosphere_view_hooks

namespace gf::feature::ext::atmosphere::client {
namespace {
class SkyViewModule {
public:
    void install() { atmosphere_view_hooks::AtmosphereViewHook::hook(); }
    void uninstall() { atmosphere_view_hooks::AtmosphereViewHook::unhook(); }
};
} // namespace
} // namespace gf::feature::ext::atmosphere::client

GF_REGISTER_MODULE(gf::feature::ext::atmosphere::client::SkyViewModule);

#endif // GF_CLIENT

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// Contract "client.ui" (F12): hiding effect icons of the game's HUD effect list
// (feature/ext/ui/Client.h, hud::hideEffectIcons). Java leaves an effect whose MobEffectInstance
// has showIcon false out of Gui#renderEffects; Bedrock's HudMobEffectsRenderer draws an icon for
// every active effect of the local player and its entries carry no effect id. Around its render the
// hidden effects are swapped out of the local player's effect list (the client's copy) and put back
// right after: the HUD draws without them, nothing else sees the change (same thread, no tick in
// between). Client build only.

#ifdef GF_CLIENT

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/renderers/HudMobEffectsRenderer.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"

#include "client/Client.h"
#include "feature/ext/ui/Client.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::hud {

namespace {

struct IconState {
    std::mutex                                       mutex;
    std::vector<std::pair<std::size_t, EffectIconFn>> fns;
    std::size_t                                      next = 0;
    std::atomic<bool>                                any{false};
    std::atomic<std::uint64_t>                       hidden{0};
};

IconState& icons() {
    static auto* s = new IconState(); // the render thread may outlive static destruction
    return *s;
}

EffectIcon viewOf(MobEffectInstance const& inst) {
    EffectIcon e;
    e.id        = "minecraft:" + MobEffect::getNameById(inst.mId);
    e.amplifier = inst.mAmplifier;
    e.duration  = inst.mDuration->mValue;
    e.ambient   = inst.mAmbient;
    e.particles = inst.mEffectVisible;
    return e;
}

} // namespace

std::size_t hideEffectIcons(EffectIconFn when) {
    if (!when) return 0;
    auto&           s = icons();
    std::lock_guard lock(s.mutex);
    auto            handle = ++s.next;
    s.fns.emplace_back(handle, std::move(when));
    s.any = true;
    return handle;
}

bool showEffectIcons(std::size_t handle) {
    auto&           s = icons();
    std::lock_guard lock(s.mutex);
    bool            had = std::erase_if(s.fns, [handle](auto const& f) { return f.first == handle; }) > 0;
    s.any               = !s.fns.empty();
    return had;
}

namespace {
std::vector<EffectIconFn> functions() {
    auto&                     s = icons();
    std::vector<EffectIconFn> fns;
    std::lock_guard           lock(s.mutex);
    for (auto const& [h, fn] : s.fns) fns.push_back(fn);
    return fns;
}

bool hiddenBy(std::vector<EffectIconFn> const& fns, EffectIcon const& effect) {
    for (auto const& fn : fns) {
        try {
            if (fn(effect)) return true;
        } catch (...) {}
    }
    return false;
}
} // namespace

bool effectIconHidden(EffectIcon const& effect) {
    auto& s = icons();
    if (!s.any.load(std::memory_order_relaxed)) return false;
    auto fns = functions();
    for (auto const& fn : fns) {
        try {
            if (fn(effect)) return true;
        } catch (...) {}
    }
    return false;
}

std::uint64_t effectIconsHidden() { return icons().hidden.load(std::memory_order_relaxed); }

} // namespace gf::client::hud

LL_TYPE_INSTANCE_HOOK(
    UiEffectIconsHook,
    ll::memory::HookPriority::High, // outside the other HUD effect hooks: they see the list without the hidden icons
    HudMobEffectsRenderer,
    &HudMobEffectsRenderer::$render,
    void,
    MinecraftUIRenderContext& renderContext,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    namespace hud = gf::client::hud;
    std::vector<std::pair<std::size_t, MobEffectInstance>> taken;
    std::vector<MobEffectInstance>*                        list = nullptr;
    if (hud::icons().any.load(std::memory_order_relaxed)) {
        try {
            if (auto* lp = gf::client::currentPlayer()) {
                auto const fns = hud::functions();
                list           = &lp->_getAllEffectsNonConst();
                for (std::size_t i = 0; i < list->size(); ++i) {
                    auto& inst = (*list)[i];
                    if (inst.mId == 0 || !hud::hiddenBy(fns, hud::viewOf(inst))) continue;
                    taken.emplace_back(i, inst);
                    inst = MobEffectInstance::NO_EFFECT();
                }
                if (!taken.empty() && hud::icons().hidden.fetch_add(taken.size(), std::memory_order_relaxed) == 0)
                    gf::log().info("ui: effect icon hidden from the HUD ({})", hud::viewOf(taken.front().second).id);
            }
        } catch (...) {}
    }
    origin(renderContext, client, owner, pass);
    if (list)
        for (auto& [i, inst] : taken)
            if (i < list->size()) (*list)[i] = std::move(inst);
}

namespace gf::client::hud {
namespace {
class EffectIconsModule {
public:
    void install() { UiEffectIconsHook::hook(); }
    void uninstall() { UiEffectIconsHook::unhook(); }
};
} // namespace
} // namespace gf::client::hud

GF_REGISTER_MODULE_ORDERED(gf::client::hud::EffectIconsModule, gf::ModuleOrder::Contracts);

#endif // GF_CLIENT

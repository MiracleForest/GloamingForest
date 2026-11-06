// Contract extension of U4 woodwork for "client.render" (EntityRenderers.h): which renderer the game
// draws an entity with. Client build only.

#ifdef GF_CLIENT

#include "feature/ext/woodwork/EntityRenderers.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/actor/ActorRenderDispatcher.h"
#include "mc/client/renderer/actor/ActorRenderer.h"
#include "mc/client/renderer/actor/DataDrivenRenderer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"

#include "mod/Log.h"

namespace gf::woodwork_ext::renderers {

namespace {

struct Rules {
    std::set<std::string>                                                      dataDriven;
    std::vector<std::pair<std::size_t, std::function<bool(Actor const&)>>>    hidden;
    std::size_t                                                                next = 1;
};

/// The rules as an immutable snapshot: the render thread loads it for every entity without a lock;
/// the writers (main thread, rare) copy, change and publish it under their own mutex.
struct Store {
    std::mutex                                writer;
    std::atomic<std::shared_ptr<Rules const>> rules{std::make_shared<Rules const>()};
};
Store& store() {
    static auto* s = new Store; // kept for the process (render hooks may run during shutdown)
    return *s;
}
std::shared_ptr<Rules const> snap() { return store().rules.load(std::memory_order_acquire); }
template <class Fn>
void update(Fn&& fn) {
    auto&           s = store();
    std::lock_guard lock(s.writer);
    auto            copy = std::make_shared<Rules>(*s.rules.load(std::memory_order_acquire));
    fn(*copy);
    s.rules.store(std::shared_ptr<Rules const>(std::move(copy)), std::memory_order_release);
}

std::atomic<std::size_t> gDataDriven{0}, gHidden{0};
std::atomic<bool>        gAny{false};

} // namespace

void useDataDriven(std::string const& entityId) {
    update([&](Rules& r) { r.dataDriven.insert(entityId); });
    gAny = true;
}

std::size_t hideWhen(std::function<bool(Actor const&)> hidden) {
    if (!hidden) return 0;
    std::size_t handle = 0;
    update([&](Rules& r) {
        handle = r.next++;
        r.hidden.emplace_back(handle, std::move(hidden));
    });
    gAny = true;
    return handle;
}

void show(std::size_t handle) {
    update([&](Rules& r) { std::erase_if(r.hidden, [handle](auto const& h) { return h.first == handle; }); });
}

std::size_t dataDrivenUses() { return gDataDriven.load(); }
std::size_t hiddenUses() { return gHidden.load(); }

} // namespace gf::woodwork_ext::renderers

namespace wr = gf::woodwork_ext::renderers;

LL_TYPE_INSTANCE_HOOK(
    WoodworkExtRendererHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    &ActorRenderDispatcher::_getRenderer,
    std::shared_ptr<ActorRenderer>,
    Actor& actor
) {
    if (!wr::gAny.load(std::memory_order_relaxed)) return origin(actor);
    try {
        auto rules = wr::snap();
        if (!rules->dataDriven.empty()) {
            auto const& type = actor.getTypeName();
            if (rules->dataDriven.contains(type)) {
                if (auto ddr = getDataDrivenRenderer(HashedString(type))) {
                    if (wr::gDataDriven.fetch_add(1) == 0) gf::log().info("woodwork: {} drawn with its client entity", type);
                    return ddr;
                }
            }
        }
    } catch (...) {}
    return origin(actor);
}

namespace {
/// Whether a hide rule takes `actor` (the render calls skip it; _getRenderer never returns null).
bool hiddenActor(Actor& actor) {
    if (!wr::gAny.load(std::memory_order_relaxed)) return false;
    try {
        auto rules = wr::snap();
        for (auto const& [handle, fn] : rules->hidden) {
            if (fn(actor)) {
                if (wr::gHidden.fetch_add(1) == 0) gf::log().info("woodwork: an entity drawn by the client mod is hidden ({})", actor.getTypeName());
                return true;
            }
        }
    } catch (...) {}
    return false;
}
} // namespace

LL_TYPE_INSTANCE_HOOK(
    WoodworkExtRenderHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    static_cast<void (ActorRenderDispatcher::*)(BaseActorRenderContext&, Actor&, Vec3 const&, Vec3 const&, Vec2 const&, bool)>(&ActorRenderDispatcher::render),
    void,
    BaseActorRenderContext& context,
    Actor&                  actor,
    Vec3 const&             cameraTargetPos,
    Vec3 const&             pos,
    Vec2 const&             rot,
    bool                    ignoreLighting
) {
    if (hiddenActor(actor)) return;
    origin(context, actor, cameraTargetPos, pos, rot, ignoreLighting);
}

LL_TYPE_INSTANCE_HOOK(
    WoodworkExtRenderShortHook,
    ll::memory::HookPriority::Normal,
    ActorRenderDispatcher,
    static_cast<void (ActorRenderDispatcher::*)(BaseActorRenderContext&, Actor&, bool)>(&ActorRenderDispatcher::render),
    void,
    BaseActorRenderContext& context,
    Actor&                  actor,
    bool                    ignoreLighting
) {
    if (hiddenActor(actor)) return;
    origin(context, actor, ignoreLighting);
}

namespace gf::woodwork_ext::renderers {

void install() {
    WoodworkExtRendererHook::hook();
    WoodworkExtRenderHook::hook();
    WoodworkExtRenderShortHook::hook();
}
void uninstall() {
    WoodworkExtRenderShortHook::unhook();
    WoodworkExtRenderHook::unhook();
    WoodworkExtRendererHook::unhook();
}

} // namespace gf::woodwork_ext::renderers

#endif // GF_CLIENT

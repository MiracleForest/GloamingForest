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

std::mutex& mutex() {
    static std::mutex m;
    return m;
}
/// The rules as an immutable snapshot (the render thread reads it every entity).
std::shared_ptr<Rules const>& current() {
    static auto* p = new std::shared_ptr<Rules const>(std::make_shared<Rules const>());
    return *p;
}
std::shared_ptr<Rules const> snap() {
    std::lock_guard lock(mutex());
    return current();
}
template <class Fn>
void update(Fn&& fn) {
    std::lock_guard lock(mutex());
    auto            copy = std::make_shared<Rules>(*current());
    fn(*copy);
    current() = std::move(copy);
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
        for (auto const& [handle, fn] : rules->hidden) {
            if (fn(actor)) {
                if (wr::gHidden.fetch_add(1) == 0) gf::log().info("woodwork: an entity drawn by the client mod is hidden ({})", actor.getTypeName());
                return nullptr;
            }
        }
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

namespace gf::woodwork_ext::renderers {

void install() { WoodworkExtRendererHook::hook(); }
void uninstall() { WoodworkExtRendererHook::unhook(); }

} // namespace gf::woodwork_ext::renderers

#endif // GF_CLIENT

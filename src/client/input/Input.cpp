// Client half of "client.input+movement" (F12). Client build only.
//
// Keys: a wrapper of LeviLamina's key registry (ll::input::KeyRegistry): the binding appears in
// the game's keyboard controls and can be remapped there. Like Java's KeyMappings with the
// IN_GAME conflict context, the handlers run only while the HUD is the top screen (no chat, no
// container open). Every press and release is also forwarded to the server ("input.key" {id,
// down}, gf::input::onKey) when it runs GloamingForest.
//
// Movement: one hook on LocalPlayer::normalTick (rule R8). Before the player's tick the local
// player's state is read into a MoveState, the handlers change it (by descending priority) and
// the changed velocity, step height, fall distance and sprinting are written back, so the
// game's own movement integrates them in the same tick (client-authoritative movement).

#ifdef GF_CLIENT

#include "client/input/Input.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "ll/api/input/KeyHandle.h"
#include "ll/api/input/KeyRegistry.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/MaxAutoStepComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"

#include "client/Client.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::keys {

namespace {
struct Key {
    std::string           category;
    std::function<void()> onPress, onRelease;
    bool                  down   = false;
    bool                  logged = false; ///< first press logged (e2e, spike S5)
};
struct State {
    std::mutex                 mutex;
    std::map<std::string, Key> keys;
};
State& st() {
    static State s;
    return s;
}

/// Java's IN_GAME conflict context: only while the HUD is the top screen.
bool inGame() {
    auto* c = currentClient();
    if (!c || !c->getLocalPlayer()) return false;
    return c->getScreenName() == "hud_screen";
}

void forward(std::string const& id, bool down) {
    if (net::client::connected()) net::client::send("input.key", {{"id", id}, {"down", down}});
}

void handle(std::string const& id, bool down) {
    std::function<void()> fn;
    {
        auto&           s = st();
        std::lock_guard lock(s.mutex);
        auto            it = s.keys.find(id);
        if (it == s.keys.end()) return;
        if (it->second.down == down) return; // key repeat (or a handler bound twice)
        bool game = inGame();
        if (down && !it->second.logged) {
            it->second.logged = true;
            log().info("keys: {} pressed ({})", id, game ? "in game" : "a screen is open");
        }
        if (down && !game) return; // not while a screen is open
        it->second.down = down;
        fn              = down ? it->second.onPress : it->second.onRelease;
    }
    try {
        if (fn) fn();
    } catch (std::exception const& e) {
        log().error("keys: handler of {} failed: {}", id, e.what());
    }
    forward(id, down);
}
} // namespace

void registerKey(std::string const& id, int defaultKey, std::string const& category, std::function<void()> onPress, std::function<void()> onRelease) {
    {
        auto&           s = st();
        std::lock_guard lock(s.mutex);
        if (s.keys.contains(id)) {
            log().warn("keys: {} registered twice; the second registration replaces the handlers", id);
            auto& k     = s.keys[id];
            k.onPress   = std::move(onPress);
            k.onRelease = std::move(onRelease);
            return;
        }
        s.keys.emplace(id, Key{category, std::move(onPress), std::move(onRelease), false});
    }
    try {
        std::vector<int> codes;
        if (defaultKey > 0) codes.push_back(defaultKey);
        auto& registry = ll::input::KeyRegistry::getInstance();
        auto& handle   = registry.getOrCreateKey(id, codes, true);
        handle.registerButtonDownHandler([id](FocusImpact, IClientInstance&) { keys::handle(id, true); });
        handle.registerButtonUpHandler([id](FocusImpact, IClientInstance&) { keys::handle(id, false); });
        // LeviLamina binds the keys when the game builds its input handler: register them from a
        // module's load() (mod load), as the fixture does
        if (currentClient() && currentClient()->getLocalPlayer()) log().warn("keys: {} registered in a running game may stay unbound until restart", id);
    } catch (std::exception const& e) {
        log().error("keys: could not register {}: {}", id, e.what());
    }
}

bool down(std::string const& id) {
    auto&           s = st();
    std::lock_guard lock(s.mutex);
    auto            it = s.keys.find(id);
    return it != s.keys.end() && it->second.down;
}

std::vector<std::string> registered() {
    auto&                    s = st();
    std::lock_guard          lock(s.mutex);
    std::vector<std::string> out;
    for (auto const& [k, _] : s.keys) out.push_back(k);
    return out;
}

} // namespace gf::client::keys

namespace gf::client::movement {

namespace {
struct Handler {
    int                              priority;
    int                              seq;
    std::function<void(MoveState&)> fn;
};
struct MoveRegistry {
    std::mutex           mutex;
    std::vector<Handler> handlers; ///< sorted: descending priority, then registration order
    int                  seq      = 0;
    bool                 jumpLast = false;
    bool                 logged   = false;
};
MoveRegistry& reg() {
    static MoveRegistry r;
    return r;
}

bool flag(MoveInputComponent const& in, MoveInputState::Flag f) { return in.mInputState->mFlagValues->test(static_cast<std::size_t>(f)); }

void runHandlers(LocalPlayer& player) {
    std::vector<Handler> handlers;
    {
        std::lock_guard lock(reg().mutex);
        if (reg().handlers.empty()) return;
        handlers = reg().handlers;
    }
    MoveState s;
    s.velocity     = player.getPosDelta();
    s.onGround     = player.isOnGround();
    s.inWater      = player.isInWater();
    s.sprinting    = player.isSprinting();
    s.gliding      = player.getStatusFlag(ActorFlags::Gliding);
    s.fallDistance = player.getFallDistance();
    s.stepHeight   = player.getMaxAutoStep();
    auto& ctx      = player.getEntityContext();
    if (auto in = ctx.tryGetComponent<MoveInputComponent>()) {
        Vec2 move   = in->mMove;
        s.strafe    = move.x;
        s.forward   = move.y;
        s.jumpHeld  = flag(*in, MoveInputState::Flag::JumpDown);
        s.sneakHeld = flag(*in, MoveInputState::Flag::SneakDown);
    }
    s.jumpEdge      = s.jumpHeld && !reg().jumpLast;
    reg().jumpLast  = s.jumpHeld;
    MoveState before = s;
    for (auto& h : handlers) {
        try {
            h.fn(s);
        } catch (std::exception const& e) {
            log().error("movement: local tick handler failed: {}", e.what());
        }
    }
    if (s.velocity != before.velocity) player.mBuiltInComponents->mStateVectorComponent->mPosDelta = s.velocity;
    if (s.stepHeight != before.stepHeight) {
        if (auto step = ctx.tryGetComponent<MaxAutoStepComponent>()) step->mValue = s.stepHeight;
    }
    if (s.fallDistance != before.fallDistance) player.setFallDistance(s.fallDistance); // e.g. a glide cancels fall damage
    if (s.sprinting != before.sprinting) player.setSprinting(s.sprinting);
    // onGround, inWater, jump / sneak input and gliding describe the tick: changing them has no effect
    if (!reg().logged) {
        reg().logged = true;
        log().info("movement: local tick handlers active ({})", handlers.size());
    }
}
} // namespace

void onLocalTick(int priority, std::function<void(MoveState&)> fn) {
    if (!fn) return;
    std::lock_guard lock(reg().mutex);
    auto&           v = reg().handlers;
    v.push_back({priority, reg().seq++, std::move(fn)});
    std::stable_sort(v.begin(), v.end(), [](Handler const& a, Handler const& b) { return a.priority > b.priority; });
}

LL_TYPE_INSTANCE_HOOK(MovementLocalTickHook, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::$normalTick, void) {
    try {
        runHandlers(*this);
    } catch (...) {}
    origin();
}

namespace {
class InputModule {
public:
    // the hook at mod load, before the game's threads run (see screen::ScreenModule)
    void load() {
        if (!mHooked) MovementLocalTickHook::hook();
        mHooked = true;
    }
    void install() { load(); }
    void uninstall() {
        if (mHooked) MovementLocalTickHook::unhook();
        mHooked = false;
    }

private:
    bool mHooked = false;
};
} // namespace

} // namespace gf::client::movement

GF_REGISTER_MODULE_ORDERED(gf::client::movement::InputModule, gf::ModuleOrder::Contracts);

#endif // GF_CLIENT

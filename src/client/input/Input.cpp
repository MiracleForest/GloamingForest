// Client half of "client.input+movement" (F12). Client build only.
//
// Keys: a wrapper of LeviLamina's key registry (ll::input::KeyRegistry): the binding appears in
// the game's keyboard controls and can be remapped there. Like Java's KeyMappings with the
// IN_GAME conflict context, the handlers run only while the HUD is the top screen (no chat, no
// container open). Every press and release is also forwarded to the server ("input.key" {id,
// down}, gf::input::onKey) when it runs GloamingForest. LeviLamina binds a key into the game's
// controls only when the game builds its input handler (after mod load, before the modules'
// install()); a key registered later (from any module's install(), whatever its order, or later)
// is delivered from the raw keyboard events (ll KeyInputEvent) with its default key, with a warning
// (feature/ext/ui/Client.h).
//
// Movement: one hook on LocalPlayer::normalTick (rule R8). Before the player's tick the local
// player's state is read into a MoveState, the handlers change it (by descending priority) and
// the changed velocity, step height, fall distance and sprinting are written back, so the
// game's own movement integrates them in the same tick (client-authoritative movement).

#ifdef GF_CLIENT

#include "client/input/Input.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/input/KeyHandle.h"
#include "ll/api/input/KeyRegistry.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/input/KeyboardRemappingLayout.h"
#include "mc/client/input/Keymapping.h"
#include "mc/client/options/IOptionRegistry.h"
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
    int                   code   = 0;     ///< the default key
    bool                  late   = false; ///< registered after the input handler was built
};
struct State {
    std::mutex                 mutex;
    std::map<std::string, Key> keys;
    /// Set at the end of mod load (the last module load()) and at the first install() of any module:
    /// the game builds its input handler after mod load, so LeviLamina no longer binds new keys.
    bool                       inputBuilt = false;
    ll::event::ListenerPtr     rawKeys; ///< raw key events for late keys
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
        Key k{category, std::move(onPress), std::move(onRelease)};
        k.code = defaultKey;
        k.late = s.inputBuilt;
        s.keys.emplace(id, std::move(k));
    }
    try {
        std::vector<int> codes;
        if (defaultKey > 0) codes.push_back(defaultKey);
        auto& registry = ll::input::KeyRegistry::getInstance();
        auto& handle   = registry.getOrCreateKey(id, codes, true);
        handle.registerButtonDownHandler([id](FocusImpact, IClientInstance&) { keys::handle(id, true); });
        handle.registerButtonUpHandler([id](FocusImpact, IClientInstance&) { keys::handle(id, false); });
    } catch (std::exception const& e) {
        log().error("keys: could not register {}: {}", id, e.what());
    }
    if (lateBound(id)) {
        log().warn(
            "keys: {} was registered after the game built its input handler: it works with its default key {} but is missing "
            "from the controls until the next start (register keys from a module's load())",
            id,
            defaultKey
        );
    }
}

bool lateBound(std::string const& id) {
    auto&           s = st();
    std::lock_guard lock(s.mutex);
    auto            it = s.keys.find(id);
    return it != s.keys.end() && it->second.late;
}

namespace {
/// Raw key events: the late keys bound to `code` (the bound ones come through their key handles).
void rawKey(int code, bool down) {
    std::vector<std::string> ids;
    {
        auto&           s = st();
        std::lock_guard lock(s.mutex);
        for (auto const& [id, k] : s.keys)
            if (k.late && k.code > 0 && k.code == code) ids.push_back(id);
    }
    for (auto const& id : ids) handle(id, down);
}

void markInputBuilt() {
    auto&           s = st();
    std::lock_guard lock(s.mutex);
    s.inputBuilt = true;
}

void listenRawKeys(bool on) {
    auto& s   = st();
    auto& bus = ll::event::EventBus::getInstance();
    if (on && !s.rawKeys) {
        s.rawKeys = bus.emplaceListener<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
            try {
                rawKey(ev.keyCode(), ev.isDown());
            } catch (...) {}
        });
    } else if (!on && s.rawKeys) {
        bus.removeListener(s.rawKeys);
        s.rawKeys = nullptr;
    }
}
} // namespace

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

// The current binding: LeviLamina adds a bound key to the game's keyboard remapping layout under an
// action named after the key handle (its full name, "key." prefixed as the game's own actions); the
// player's remapping in the controls changes that mapping's keys. A late key (or a client without
// its controls loaded) answers with the default key it was registered with.
std::optional<Binding> binding(std::string const& id) {
    int  defaultKey = 0;
    bool late       = false;
    {
        auto&           s = st();
        std::lock_guard lock(s.mutex);
        auto            it = s.keys.find(id);
        if (it == s.keys.end()) return std::nullopt;
        defaultKey = it->second.code;
        late       = it->second.late;
    }
    Binding out;
    if (defaultKey > 0) out.keys.push_back(defaultKey);
    std::shared_ptr<KeyboardRemappingLayout> layout;
    try {
        if (auto* c = currentClient()) layout = c->getOptions().getCurrentKeyboardRemapping();
    } catch (...) {}
    if (!layout) return out;
    try {
        if (!late) {
            std::vector<std::string> names{id, "key." + id};
            auto& registry = ll::input::KeyRegistry::getInstance();
            if (registry.hasKey(id)) {
                auto full = registry.getOrCreateKey(id, {}, true).getFullName();
                names.push_back(full);
                names.push_back("key." + full);
            }
            for (auto const& m : layout->mKeymappings.get()) {
                auto const& action = m.mAction.get();
                if (std::find(names.begin(), names.end(), action) == names.end()) continue;
                out.keys     = m.mKeys.get();
                std::erase_if(out.keys, [](int k) { return k <= 0; });
                out.remapped = true;
                break;
            }
        }
        if (!out.keys.empty()) out.name = layout->getMappedKeyName(out.keys.front());
    } catch (...) {}
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
        keys::listenRawKeys(true);
    }
    void install() {
        load();
        keys::markInputBuilt(); // the keys of every module's load() are bound by now
    }
    void uninstall() {
        if (mHooked) MovementLocalTickHook::unhook();
        mHooked = false;
        keys::listenRawKeys(false);
    }

private:
    bool mHooked = false;
};

/// Marks the input handler as built whatever the order of the module that registers a key: the
/// last module load() (the end of mod load; the game builds its input handler after it, spike S5)
/// and the first install() of any module (should load() not run). A key registered from any
/// install() or later is then a late key (delivered from raw key events, with a warning).
class InputBuiltAtLoadEnd {
public:
    void load() { keys::markInputBuilt(); }
    void install() { keys::markInputBuilt(); }
    void uninstall() {}
};
class InputBuiltAtFirstInstall {
public:
    void install() { keys::markInputBuilt(); }
    void uninstall() {}
};
} // namespace

} // namespace gf::client::movement

GF_REGISTER_MODULE_ORDERED(gf::client::movement::InputModule, gf::ModuleOrder::Contracts);
GF_REGISTER_MODULE_ORDERED(gf::client::movement::InputBuiltAtLoadEnd, std::numeric_limits<int>::max());
GF_REGISTER_MODULE_ORDERED(gf::client::movement::InputBuiltAtFirstInstall, std::numeric_limits<int>::min());

#endif // GF_CLIENT

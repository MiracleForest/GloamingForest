#pragma once

// The world lifecycle of the "db" contract (game/WorldDb.h), independent of the game: who is told
// when a world opens, saves and closes. game/WorldDb.cpp forwards the engine's events here
// (ServerStartedEvent -> open(), ServerStoppingEvent and the mod's disable -> close(),
// ServerLevelTickEvent -> tick()), so the rules are gtested (tests/core/WorldLifecycleTest.cpp).
//
// Save listeners run only while a world is open. BDS keeps ticking between ServerStoppingEvent and
// the mod's disable: an autosave there would hand units' listeners the state their closed
// listeners have already cleared (and write it over the world's records).

#include <functional>
#include <vector>

namespace gf::core {

class WorldLifecycle {
public:
    using Listener = std::function<void()>;
    /// Calls one listener; `what` is "ready", "save" or "closed". The default calls it directly
    /// (game/WorldDb.cpp catches and logs what a listener throws).
    using Invoker = std::function<void(Listener const& fn, char const* what)>;

    explicit WorldLifecycle(int autosaveTicks, Invoker invoker = {});

    void onReady(Listener fn) { mReady.push_back(std::move(fn)); }
    void onSave(Listener fn) { mSave.push_back(std::move(fn)); }
    void onClosed(Listener fn) { mClosed.push_back(std::move(fn)); }

    /// A world was loaded: the ready listeners (again for every local world of a client), and
    /// the autosave counts from zero.
    void open();
    /// The world closes: one last save, then the closed listeners. Nothing when no world is open
    /// (BDS: ServerStoppingEvent, then the mod's disable).
    void close();
    /// One server tick: the save listeners every `autosaveTicks` ticks of an open world, nothing
    /// while no world is open.
    void tick();

    [[nodiscard]] bool isOpen() const { return mOpen; }
    [[nodiscard]] int  autosaveTicks() const { return mAutosaveTicks; }

private:
    void fire(std::vector<Listener> const& list, char const* what);

    int                   mAutosaveTicks;
    Invoker               mInvoker;
    std::vector<Listener> mReady, mSave, mClosed;
    bool                  mOpen  = false;
    int                   mTicks = 0;
};

} // namespace gf::core

#include "core/WorldLifecycle.h"

#include <algorithm>

namespace gf::core {

WorldLifecycle::WorldLifecycle(int autosaveTicks, Invoker invoker)
: mAutosaveTicks(std::max(autosaveTicks, 1)),
  mInvoker(std::move(invoker)) {}

void WorldLifecycle::fire(std::vector<Listener> const& list, char const* what) {
    // a copy: a listener may register another one while the list runs
    auto const copy = list;
    for (auto const& fn : copy) {
        if (!fn) continue;
        if (mInvoker) mInvoker(fn, what);
        else fn();
    }
}

void WorldLifecycle::open() {
    mOpen  = true;
    mTicks = 0;
    fire(mReady, "ready");
}

void WorldLifecycle::close() {
    if (!mOpen) return;
    mOpen = false;
    fire(mSave, "save");
    fire(mClosed, "closed");
}

void WorldLifecycle::tick() {
    if (!mOpen) return;
    if (++mTicks < mAutosaveTicks) return;
    mTicks = 0;
    fire(mSave, "save");
}

} // namespace gf::core

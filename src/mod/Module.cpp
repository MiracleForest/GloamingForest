// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Module.cpp), LGPL-3.0-or-later.
#include "mod/Module.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#include "mod/Log.h"

namespace gf {

namespace {
std::vector<ModuleEntry> gInstalled;

std::vector<ModuleEntry> sortedEntries() {
    auto entries = ModuleRegistry::entries();
    std::stable_sort(entries.begin(), entries.end(), [](ModuleEntry const& a, ModuleEntry const& b) {
        return a.order < b.order;
    });
    return entries;
}

using Clock = std::chrono::steady_clock;

// A module whose install() takes longer than this is named in the log ("module X installed in N ms").
constexpr auto kSlowInstall = std::chrono::milliseconds(1000);
// While the modules install, a watcher thread names the module that is still running after this long,
// and again every as long (the client once hung in "Enabling mods" with no CPU use: the e2e harness
// reads these lines when its watchdog kills such a game, docs/spec/e2e.md).
constexpr auto kStillRunningEvery = std::chrono::seconds(20);

// The watcher of one installModules() / uninstallModules() pass.
class PhaseWatcher {
public:
    explicit PhaseWatcher(char const* phase) : mPhase(phase), mThread([this] { run(); }) {}
    PhaseWatcher(PhaseWatcher const&)            = delete;
    PhaseWatcher& operator=(PhaseWatcher const&) = delete;
    ~PhaseWatcher() {
        {
            std::lock_guard lock(mMutex);
            mDone = true;
        }
        mCv.notify_all();
        mThread.join();
    }
    void begin(std::string_view name) {
        std::lock_guard lock(mMutex);
        mCurrent = name;
        mSince   = Clock::now();
    }
    void end() {
        std::lock_guard lock(mMutex);
        mCurrent.clear();
    }

private:
    void run() {
        std::unique_lock lock(mMutex);
        while (!mDone) {
            mCv.wait_for(lock, kStillRunningEvery);
            if (mDone || mCurrent.empty()) continue;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - mSince).count();
            if (ms < std::chrono::duration_cast<std::chrono::milliseconds>(kStillRunningEvery).count()) continue;
            log().warn("{}: module {} still running after {} ms", mPhase, mCurrent, ms);
        }
    }

    char const*             mPhase;
    std::mutex              mMutex;
    std::condition_variable mCv;
    bool                    mDone = false;
    std::string             mCurrent;
    Clock::time_point       mSince;
    std::thread             mThread; // last: starts after the members above exist
};

void logTiming(char const* what, std::string_view name, Clock::time_point start) {
    auto took = Clock::now() - start;
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(took).count();
    if (took >= kSlowInstall) log().info("module {} {} in {} ms (slow)", name, what, ms);
    else log().debug("module {} {} in {} ms", name, what, ms);
}
} // namespace

void loadModules() {
    for (auto const& e : sortedEntries()) {
        if (!e.load) continue;
        auto start = Clock::now();
        try {
            e.load();
            logTiming("loaded", e.name, start);
        } catch (std::exception const& ex) {
            log().error("module {} failed to load: {}", e.name, ex.what());
        }
    }
}

void installModules() {
    auto entries = sortedEntries();
    log().info("installing {} modules", entries.size());
    PhaseWatcher watcher("install");
    auto         all = Clock::now();
    for (auto const& e : entries) {
        watcher.begin(e.name);
        auto start = Clock::now();
        try {
            e.install();
            gInstalled.push_back(e);
            logTiming("installed", e.name, start);
        } catch (std::exception const& ex) {
            log().error("module {} failed to install: {}", e.name, ex.what());
        }
        watcher.end();
    }
    log().info(
        "{} modules installed in {} ms",
        gInstalled.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - all).count()
    );
}

void uninstallModules() {
    PhaseWatcher watcher("uninstall");
    for (auto it = gInstalled.rbegin(); it != gInstalled.rend(); ++it) {
        watcher.begin(it->name);
        try {
            it->uninstall();
        } catch (std::exception const& ex) {
            log().error("module {} failed to uninstall: {}", it->name, ex.what());
        }
        watcher.end();
    }
    gInstalled.clear();
}

} // namespace gf
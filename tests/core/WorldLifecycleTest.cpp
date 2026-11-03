#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "core/WorldLifecycle.h"

using gf::core::WorldLifecycle;

namespace {
// A lifecycle whose listeners append "ready" / "save" / "closed" to `events`.
struct Recorder {
    std::vector<std::string> events;
    WorldLifecycle           life;

    explicit Recorder(int autosaveTicks) : life(autosaveTicks) {
        life.onReady([this] { events.push_back("ready"); });
        life.onSave([this] { events.push_back("save"); });
        life.onClosed([this] { events.push_back("closed"); });
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i) life.tick();
    }
};
} // namespace

TEST(WorldLifecycle, NothingBeforeAWorldOpens) {
    Recorder r(3);
    r.ticks(10);
    r.life.close();
    EXPECT_TRUE(r.events.empty());
    EXPECT_FALSE(r.life.isOpen());
}

TEST(WorldLifecycle, AutosaveEveryNTicksWhileOpen) {
    Recorder r(3);
    r.life.open();
    EXPECT_TRUE(r.life.isOpen());
    r.ticks(2);
    EXPECT_EQ(r.events, (std::vector<std::string>{"ready"}));
    r.ticks(1);
    EXPECT_EQ(r.events, (std::vector<std::string>{"ready", "save"}));
    r.ticks(6);
    EXPECT_EQ(r.events, (std::vector<std::string>{"ready", "save", "save", "save"}));
}

TEST(WorldLifecycle, CloseSavesOnceThenTellsTheClosedListeners) {
    Recorder r(100);
    r.life.open();
    r.life.close();
    EXPECT_EQ(r.events, (std::vector<std::string>{"ready", "save", "closed"}));
    // BDS: ServerStoppingEvent, then the mod's disable with the level still alive
    r.life.close();
    EXPECT_EQ(r.events, (std::vector<std::string>{"ready", "save", "closed"}));
}

// The W0c bug: BDS keeps ticking while the mods are disabled, and the autosave handed units' save
// listeners the state their closed listeners had cleared (F8 lost its boss-deaths record).
TEST(WorldLifecycle, NoSaveListenerRunsAfterClose) {
    Recorder r(5);
    r.life.open();
    r.ticks(3); // part of an autosave period
    r.life.close();
    auto const atClose = r.events;
    r.ticks(1000);
    EXPECT_EQ(r.events, atClose);
    EXPECT_EQ(r.events.back(), "closed");
}

TEST(WorldLifecycle, ANewWorldStartsAFreshAutosavePeriod) {
    Recorder r(4);
    r.life.open();
    r.ticks(3);
    r.life.close();
    r.events.clear();
    // the client's next local world
    r.life.open();
    r.ticks(3);
    EXPECT_EQ(r.events, (std::vector<std::string>{"ready"}));
    r.ticks(1);
    EXPECT_EQ(r.events, (std::vector<std::string>{"ready", "save"}));
}

TEST(WorldLifecycle, TheInvokerRunsEveryListener) {
    std::vector<std::string> seen;
    int                      calls = 0;
    WorldLifecycle           life(1, [&](WorldLifecycle::Listener const& fn, char const* what) {
        seen.emplace_back(what);
        try {
            fn();
        } catch (std::exception const&) {
            seen.emplace_back("caught");
        }
    });
    life.onSave([] { throw std::runtime_error("broken listener"); });
    life.onSave([&] { ++calls; });
    life.open();
    life.tick();
    EXPECT_EQ(seen, (std::vector<std::string>{"save", "caught", "save"}));
    EXPECT_EQ(calls, 1);
}

TEST(WorldLifecycle, AListenerMayRegisterAnother) {
    WorldLifecycle life(1);
    int            later = 0;
    life.onReady([&] { life.onReady([&] { ++later; }); });
    life.open();
    EXPECT_EQ(later, 0); // registered during the call: from the next open on
    life.close();
    life.open();
    EXPECT_EQ(later, 1);
}

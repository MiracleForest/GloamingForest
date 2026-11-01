// Adapted from LiteLDev/Levitate v0.1.0 (src/selftest/SelfTest.h), LGPL-3.0-or-later.
#pragma once

// Contract "selftest" (W0a): in-game tests, run with `/twilightforest selftest ["glob"]`.
//
//     GF_REGISTER_SELFTEST("trees.canopy_sapling", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> {
//         auto box = co_await ctx.flatArea(300, 16);           // dx 300..399 belong to U3 (rule R12)
//         ctx.require(ctx.setBlock(ctx.testPos(300), "twilightforest:canopy_sapling"), "sapling placed");
//         co_await ctx.ticks(5);
//         ctx.check(ctx.snapshotVolume(box) == "0123456789abcdef", "the tree matches the golden hash");
//     });
//
// Rules (R12): a unit's tests are named "<u>.<case>" and use only its own dx range: unit U<n>
// owns dx 100n..100n+99, W0b unit F<k> owns 5000+100k..5000+100k+99 (the foundation F0 runs
// "core.*" in 5000..5099), W2 owns 9000 and up. Tests depend only on W0 and the unit itself
// (fixture ids gf_test:* from assets/test) and must pass inside the 'full' run.
//
// Output lines: `SELFTEST PASS <name>`, `SELFTEST FAIL <name>: <reason>`,
// `SELFTEST DONE pass=<N> fail=<M>`, `SELFTEST INFO <name>: ...`.

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/coro/CoroTask.h"

#include "core/GlobalPos.h"
#include "mod/Registry.h"

class ItemStack;
class Player;
class SimulatedPlayer;
class BlockPos;
class BlockSource;

namespace gf::selftest {

/// Thrown by `require()` / `fail()` to abort the current test.
struct Abort : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// One BossEventPacket the server sent while a capture was active.
struct BossEvent {
    std::int64_t bossId  = 0;
    std::string  type;          ///< "add", "player_added", "remove", "player_removed", "update_percent",
                                ///< "update_name", "update_properties", "update_style", "query"
    std::string  name;          ///< the bar's title (unredacted)
    float        percent = 0;   ///< health fraction 0..1
    int          color   = 0;   ///< BossBarColor (0 pink .. 7 rebecca purple / 8 colours)
    int          overlay = 0;   ///< BossBarOverlay
};

/// Records the boss bar packets sent to any player while it is alive (`Context::captureBossEvent`).
class BossEventCapture {
public:
    BossEventCapture();
    ~BossEventCapture();
    BossEventCapture(BossEventCapture const&)            = delete;
    BossEventCapture& operator=(BossEventCapture const&) = delete;

    [[nodiscard]] std::vector<BossEvent> events() const;
    [[nodiscard]] bool                   empty() const { return events().empty(); }
    void                                 clear();

    /// Called by the packet hook (server thread).
    static void record(BossEvent const& e);

private:
    std::vector<BossEvent> mEvents;
};

/// Helpers and assertions for one selftest run. Everything runs on the server thread.
///
/// Players: `player()` returns a SimulatedPlayer spawned for this test (created lazily near the
/// test area, removed afterwards). Blocks set with `setBlock()` are restored to air after the
/// test; `flatArea()` volumes are restored to what they were.
class Context {
public:
    explicit Context(std::string name);
    ~Context();
    Context(Context const&)            = delete;
    Context& operator=(Context const&) = delete;

    [[nodiscard]] std::string const& name() const { return mName; }

    // ---- players ------------------------------------------------------------------------------
    /// The test's simulated player (spawned on first use; the first of the test's players that is
    /// still in the level, a new one when all are gone). Aborts the test if it can't spawn.
    /// Rule R16: the reference is valid for the current tick only; call `player()` again after a
    /// `co_await` instead of keeping it (the context itself keeps unique ids, not pointers).
    [[nodiscard]] Player& player();
    /// Spawns an extra simulated player (removed after the test). Same lifetime rule as `player()`:
    /// keep `getOrCreateUniqueID()` and look it up again (`Level::getPlayer`) after a `co_await`.
    [[nodiscard]] SimulatedPlayer& spawnPlayer(std::string const& name = {});

    // ---- world --------------------------------------------------------------------------------
    /// The dimension the test works in (0 = overworld until `inTwilight()` succeeded).
    [[nodiscard]] int dimension() const { return mDim; }
    /// A free test position for `dx` (rule R12 ranges) at the surface of the test area:
    /// x = spawn + 5 + dx % 100, and one z row per hundred of dx (U1..U30 rows 1..30, F0..F16 rows
    /// 31..47, W2 rows 48..57). Rows are only 5 blocks apart in z, so `dz` must stay within 0..4:
    /// a larger dz lands in the next unit's row. Distinct tests use distinct dx.
    [[nodiscard]] core::GlobalPos testPos(int dx, int dz = 0) const;
    /// Largest `flatArea` side: 144 blocks span at most 10 chunks per axis whatever the alignment,
    /// and a Bedrock ticking area holds at most 100 chunks (larger sizes are clamped, with an info line).
    static constexpr int kMaxFlatAreaSize = 144;
    /// Bedrock allows 10 ticking areas per world and the run keeps 3 for the test rows: a test
    /// may hold at most 7 flat areas at once (one more fails the test with a clear message).
    static constexpr int kMaxFlatAreasPerTest = 7;
    /// Prepares a flat `size` x `size` area whose minimum corner is `testPos(dx)` (one layer of
    /// grass below, `height` layers of air from the surface up), keeps it loaded with its own
    /// ticking area and returns its box (surface layer to the top). The volume is restored after
    /// the test. A side larger than 5 reaches into the following units' z rows: that is safe in the
    /// 'full' run only because tests run one at a time and each flat area is restored, so never
    /// leave blocks behind outside dz 0..4 of your own row.
    [[nodiscard]] ll::coro::CoroTask<core::BlockBox> flatArea(int dx, int size, int height = 32);
    /// Moves the test into the Twilight Forest dimension (gf::world, feature/dimension/World.h):
    /// the player is sent there and later testPos / flatArea positions are in it. False when the
    /// dimension is not available (then the test should `require` it).
    [[nodiscard]] ll::coro::CoroTask<bool> inTwilight();
    /// Sets a block by id with states ({"name": int | bool | "text"}; restored to air after the
    /// test). False when the id or a state is unknown.
    bool setBlock(core::GlobalPos const& pos, std::string const& id, nlohmann::json const& states = nlohmann::json::object());
    /// The block id at `pos` ("" when not loaded).
    [[nodiscard]] std::string blockAt(core::GlobalPos const& pos) const;
    /// Brings the test player next to `pos` and waits (up to 5 s) until its chunks are loaded:
    /// `ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded")`.
    [[nodiscard]] ll::coro::CoroTask<bool> ensureLoaded(core::GlobalPos pos);
    /// Remembers a position to clear (set to air) after the test.
    void cleanupBlock(core::GlobalPos const& pos);
    /// The golden volume hash of `box` in the test dimension (core/VolumeHash.h; 16 hex digits).
    [[nodiscard]] std::string snapshotVolume(core::BlockBox const& box) const;
    /// The main block source of the test dimension (nullptr without a level).
    [[nodiscard]] BlockSource* region() const;

    // ---- items --------------------------------------------------------------------------------
    /// Puts `item` into the player's inventory slot (or the first free slot when `slot < 0`).
    /// Returns the slot used, -1 on failure.
    int give(Player& player, ItemStack const& item, int slot = -1);
    /// Gives `count` of item `id` to the test player; returns the slot, -1 if the id is unknown.
    int give(std::string const& id, int count = 1);

    // ---- packets ------------------------------------------------------------------------------
    /// Starts recording boss bar packets (BossEventPacket) for the rest of the test.
    [[nodiscard]] BossEventCapture& captureBossEvent();

    // ---- waiting ------------------------------------------------------------------------------
    /// `co_await ctx.ticks(5);` (async tests only).
    [[nodiscard]] static ll::coro::CoroTask<> ticks(int n);

    // ---- assertions ---------------------------------------------------------------------------
    /// Records a failure and continues.
    void check(bool condition, std::string const& message);
    /// Records a failure and aborts the test.
    void require(bool condition, std::string const& message);
    [[noreturn]] void fail(std::string const& message);
    /// Logs a progress line ("SELFTEST INFO <name>: ...").
    void info(std::string const& message) const;

    [[nodiscard]] std::vector<std::string> const& failures() const { return mFailures; }

private:
    struct Restore; ///< a flat area's original blocks
    std::string                            mName;
    std::vector<std::string>               mFailures;
    std::vector<std::int64_t>              mPlayers; ///< ActorUniqueIDs of the spawned players (R16)
    std::vector<core::GlobalPos>           mBlocks;
    std::vector<std::unique_ptr<Restore>>  mRestores;
    std::vector<std::pair<std::string, int>> mTickingAreas; ///< (name, dimension)
    std::unique_ptr<BossEventCapture>      mBossCapture;
    int                                    mDim = 0;
    core::GlobalPos                        mOrigin{}; ///< inTwilight(): the dimension's test origin
    bool                                   mHasOrigin = false;
};

using SyncTest  = std::function<void(Context&)>;
using AsyncTest = std::function<ll::coro::CoroTask<>(Context&)>;

struct TestEntry {
    std::string name; ///< "unit.case"
    AsyncTest   run;
};
using TestRegistry = StaticRegistry<TestEntry>;

/// Runs all tests whose name matches `glob` (`*`, `?`), one after another, printing
/// `SELFTEST PASS <name>` / `SELFTEST FAIL <name>: <reason>` and finally
/// `SELFTEST DONE pass=<N> fail=<M>`. `out` receives every line too (command output).
void runAll(std::string const& glob, std::function<void(std::string const&)> out);
[[nodiscard]] bool running();

namespace detail {
struct TestRegistrar {
    template <class F>
    TestRegistrar(std::string name, F fn) {
        if constexpr (std::is_invocable_r_v<ll::coro::CoroTask<>, F, Context&>) {
            TestRegistry::add({std::move(name), AsyncTest(std::move(fn))});
        } else {
            static_assert(std::is_invocable_v<F, Context&>, "selftest must take (gf::selftest::Context&)");
            TestRegistry::add({std::move(name), [fn = std::move(fn)](Context& ctx) -> ll::coro::CoroTask<> {
                                   fn(ctx);
                                   co_return;
                               }});
        }
    }
};
} // namespace detail

} // namespace gf::selftest

/// Registers a selftest. `fn` is `void(gf::selftest::Context&)` or
/// `ll::coro::CoroTask<>(gf::selftest::Context&)` (to `co_await ctx.ticks(n)`).
///     GF_REGISTER_SELFTEST("unit.case", [](gf::selftest::Context& ctx) { ... });
#define GF_REGISTER_SELFTEST(name, ...)                                                                                \
    static ::gf::selftest::detail::TestRegistrar GF_UNIQUE(gf_selftest_registrar_)(name, __VA_ARGS__)

// Client-only probe of the client scenario 'models' (unit F2, prefix "models."): the game's hide-GUI
// options as the client holds them in memory (F1 toggles them; options.txt gfx_hidehud /
// gfx_hidehand keep them for the next start). tools/e2e/client/scenarios/models.ps1 hides the HUD
// with F1 for the model preview shots and shows it again afterwards; it runs this test after each
// press to know that the press took effect, because a lost F1 once left the shared game instance
// without a HUD for every later client run. Info only (one line, "hud hidden|shown, hand
// hidden|shown"): the scenario decides; the test always passes. No world edits, no testPos.
//
// Lives in src/feature/ext/models/ (rule R18): F2 owns no source directory; this adds no contract API.
#ifdef GF_CLIENT

#include <atomic>
#include <memory>
#include <string>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/thread/ClientThreadExecutor.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"

#include "client/Client.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;

// Answer slots of one ask: no answer yet, no client instance, or the two flags.
constexpr int kAsked    = -1;
constexpr int kNoClient = 0;
constexpr int kAnswered = 4;
constexpr int kHudBit   = 2;
constexpr int kHandBit  = 1;

GF_REGISTER_SELFTEST("models.client_hud", [](Context& ctx) -> ll::coro::CoroTask<> {
    // read on the client thread, which owns the options (the key handler changes them there)
    auto answer = std::make_shared<std::atomic<int>>(kAsked);
    ll::thread::ClientThreadExecutor::getDefault().execute([answer] {
        auto* client = gf::client::currentClient();
        if (!client) {
            answer->store(kNoClient);
            return;
        }
        auto const& options = client->getOptions();
        answer->store(kAnswered | (options.getHideHud() ? kHudBit : 0) | (options.getHideHand() ? kHandBit : 0));
    });
    for (int t = 0; t < 100 && answer->load() == kAsked; ++t) co_await ctx.ticks(1);
    int const a = answer->load();
    if (a == kAsked) {
        ctx.info("hud unknown (the client thread did not answer within 100 ticks)");
        co_return;
    }
    if (a == kNoClient) {
        ctx.info("hud unknown (no client instance)");
        co_return;
    }
    ctx.info(std::string("hud ") + ((a & kHudBit) ? "hidden" : "shown") + ", hand " + ((a & kHandBit) ? "hidden" : "shown"));
});

} // namespace

#endif

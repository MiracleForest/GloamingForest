// U15 (atmosphere) selftests of the client mod ("atmosphere.client_*", client build only). They run
// on the server thread of the local world and read the client state; the aurora and weather tests
// need the real player in the Twilight Forest (the client scenario teleports it first) and only
// report when it is elsewhere.

#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>

#include "client/Client.h"
#include "client/atmosphere/AtmosphereClient.h"
#include "client/world/Sky.h"
#include "core/atmosphere/Atmosphere.h"
#include "feature/atmosphere/Atmosphere.h"
#include "feature/ext/atmosphere/SkyView.h"
#include "feature/ext/ui/Client.h"
#include "feature/progression/Progression.h"
#include "mc/client/player/LocalPlayer.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace atc = gf::client::atmosphere;
namespace ca  = gf::core::atmosphere;
namespace at  = gf::feature::atmosphere;
namespace sky = gf::client::sky;

/// Waits until the unit's client tick counter advanced by `n` (at most 30 s of server ticks).
ll::coro::CoroTask<> clientTicks(Context& ctx, int n) {
    int const start = atc::state().ticks;
    for (int i = 0; i < 600 && atc::state().ticks - start < n; ++i) co_await ctx.ticks(1);
}

GF_REGISTER_SELFTEST("atmosphere.client_provider", [](Context& ctx) {
    auto s = atc::state();
    ctx.require(s.dimension >= 0, "the Twilight Forest dimension is known on the client");
    auto p = sky::providerOf(s.dimension);
    ctx.require(p != nullptr, "TF's sky provider is set for the Twilight Forest");
    ctx.check(static_cast<bool>(p->sky) && static_cast<bool>(p->stars) && static_cast<bool>(p->fog) && static_cast<bool>(p->clouds) && static_cast<bool>(p->weather), "sky, stars, fog, clouds and weather are provided");
    auto clouds = p->clouds();
    ctx.check(clouds && *clouds == 128.0f, "clouds at y 128 (TwilightForestRenderInfo)");
});

GF_REGISTER_SELFTEST("atmosphere.client_fog", [](Context& ctx) {
    if (!gf::client::currentPlayer()) {
        ctx.info("no local player");
        return;
    }
    atc::setPreview(nlohmann::json::object());
    auto plain = atc::fogForTest("minecraft:plains");
    ctx.check(!plain, "a biome that is not TF's keeps the game's fog");
    auto spooky = atc::fogForTest("twilightforest:spooky_forest");
    ctx.require(spooky.has_value(), "the spooky forest has TF's fog");
    auto look = ca::biomeLook("twilightforest:spooky_forest");
    auto want = ca::brightnessDependentFogColor(ca::rgbOf(look->fog), ca::daylight(ca::timeOfDay(ca::kFixedTime)));
    ctx.check(spooky->color.r >= 0 && spooky->color.r <= 1 && spooky->end > spooky->start, "fog planes and colour are sane");
    ctx.info("spooky fog " + std::to_string(spooky->start) + ".." + std::to_string(spooky->end) + ", brightness-dependent colour r " + std::to_string(want.r) + " -> frame colour r " + std::to_string(spooky->color.r));
    auto forest = atc::fogForTest("twilightforest:forest");
    ctx.check(forest.has_value(), "the forest has TF's fog colour");
});

GF_REGISTER_SELFTEST("atmosphere.client_aurora", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!atc::state().inTwilight) {
        ctx.info("the local player is not in the Twilight Forest: aurora not checked");
        co_return;
    }
    // counted in client ticks (the counter's own clock; a loaded client ticks slower than the server)
    atc::setPreview({{"biome", "twilightforest:glacier"}});
    co_await clientTicks(ctx, 2);
    auto s0 = atc::state();
    co_await clientTicks(ctx, 30);
    auto s1   = atc::state();
    int  want = std::min(ca::kAuroraMax, s0.aurora + (s1.ticks - s0.ticks));
    ctx.check(std::abs(s1.aurora - want) <= 1, "the aurora rises by 1 a client tick (" + std::to_string(s0.aurora) + " -> " + std::to_string(s1.aurora) + " in " + std::to_string(s1.ticks - s0.ticks) + " ticks)");
    co_await clientTicks(ctx, 70);
    int full = atc::state().aurora;
    ctx.check(full == ca::kAuroraMax, "the aurora is full after 60 client ticks (" + std::to_string(full) + ")");
    // (an empty auroraBiomes list turning it off is core::atmosphere::tickAurora's gtest: the config
    // is read by the client and render threads, a selftest must not change it under them)
    atc::setPreview({{"biome", "twilightforest:forest"}});
    auto f0 = atc::state();
    co_await clientTicks(ctx, 10);
    auto f1     = atc::state();
    int  fading = f1.aurora;
    int  fell   = full - fading;
    ctx.check(fell >= std::max(1, f1.ticks - f0.ticks - 2) && fell <= f1.ticks - f0.ticks + 1, "outside the glacier the aurora fades out by 1 a client tick (" + std::to_string(fading) + " after " + std::to_string(f1.ticks - f0.ticks) + ")");
    atc::setPreview(nlohmann::json::object());
});

GF_REGISTER_SELFTEST("atmosphere.client_weather", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!atc::state().inTwilight) {
        ctx.info("the local player is not in the Twilight Forest: locked weather not checked");
        co_return;
    }
    atc::setPreview({{"biome", "twilightforest:snowy_forest"}, {"locked", true}});
    co_await clientTicks(ctx, 2);
    ctx.check(atc::state().nearLocked, "a locked biome around the player draws its weather");
    atc::setPreview({{"biome", "twilightforest:forest"}, {"locked", true}});
    co_await clientTicks(ctx, 2);
    ctx.check(!atc::state().nearLocked, "a biome without a restriction draws none");
    atc::setPreview({{"biome", "twilightforest:forest"}, {"urGhast", true}});
    co_await clientTicks(ctx, 12);
    float full = atc::state().urGhastRain;
    ctx.check(full > 0.95f, "the Ur-Ghast's tantrum raises its rain by 0.1 a client tick to 1 (" + std::to_string(full) + ")");
    atc::setPreview(nlohmann::json::object());
    auto r0 = atc::state();
    co_await clientTicks(ctx, 10);
    auto  r1    = atc::state();
    float after = r1.urGhastRain;
    float want  = std::max(0.0f, r0.urGhastRain - 0.02f * static_cast<float>(r1.ticks - r0.ticks));
    ctx.check(std::abs(after - want) < 0.03f, "and it falls by 0.02 a client tick after (" + std::to_string(after) + ", " + std::to_string(r1.ticks - r0.ticks) + " ticks)");
});

GF_REGISTER_SELFTEST("atmosphere.client_config", [](Context& ctx) {
    // getValidAuroraBiomes: the default glacier is valid (a TF biome) whatever the world knows
    auto valid = atc::validAuroraBiomes();
    ctx.check(std::find(valid.begin(), valid.end(), "twilightforest:glacier") != valid.end(), "the glacier is a valid aurora biome");
    ctx.check(ca::parseResourceLocation("plains") == std::optional<std::string>("minecraft:plains"), "a bare path reads as minecraft:");
    ctx.check(!ca::parseResourceLocation("not/a:valid id"), "an invalid id is skipped");
});

GF_REGISTER_SELFTEST("atmosphere.client_clouds", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!atc::state().inTwilight) {
        ctx.info("the local player is not in the Twilight Forest: the cloud colour not checked");
        co_return;
    }
    auto before = gf::feature::ext::atmosphere::client::viewOverrideFrames();
    co_await ctx.ticks(10);
    auto after = gf::feature::ext::atmosphere::client::viewOverrideFrames();
    ctx.check(after > before, "the clouds take getCloudColor and the sunrise colour is off in the Twilight Forest (" + std::to_string(after - before) + " frames)");
    auto c = ca::cloudColor(ca::timeOfDay(ca::kFixedTime), 0, 0);
    ctx.check(c.r < 0.5f && c.r == c.g && c.b > c.r, "the clouds at 13000 are a darkened grey, no sunset tint");
});

GF_REGISTER_SELFTEST("atmosphere.client_weather_real", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the real locked-biome data: U14's restrictions and F10's progression (no preview lock)
    if (!atc::state().inTwilight) {
        ctx.info("the local player is not in the Twilight Forest: real locked weather not checked");
        co_return;
    }
    if (!gf::progression::client::enforced()) {
        ctx.info("tfEnforcedProgression is off: the biomes are not locked");
        co_return;
    }
    auto* lp = gf::client::currentPlayer();
    if (!lp) co_return;
    std::string const mode = lp->isCreative() ? "creative" : lp->isSpectator() ? "spectator" : lp->isAdventure() ? "adventure" : "survival";
    (void)gf::selftest::runServerCommand("tag @a add gf_atmosphere_real");
    (void)gf::selftest::runServerCommand("gamemode survival @a[tag=gf_atmosphere_real]");
    bool const locked = gf::progression::client::biomeLocked("twilightforest:snowy_forest");
    atc::setPreview({{"biome", "twilightforest:snowy_forest"}});
    co_await ctx.ticks(25);
    bool const near = atc::state().nearLocked;
    ctx.info(std::string("snowy forest locked by progression: ") + (locked ? "yes" : "no") + ", weather drawn: " + (near ? "yes" : "no"));
    ctx.check(near == locked, "the snowy forest's real restriction draws its weather exactly when progression locks it");
    atc::setPreview({{"biome", "twilightforest:forest"}});
    co_await ctx.ticks(25);
    ctx.check(!atc::state().nearLocked, "the forest has no restriction");
    atc::setPreview(nlohmann::json::object());
    (void)gf::selftest::runServerCommand("gamemode " + mode + " @a[tag=gf_atmosphere_real]");
    (void)gf::selftest::runServerCommand("tag @a remove gf_atmosphere_real");
});

} // namespace

#endif // GF_CLIENT

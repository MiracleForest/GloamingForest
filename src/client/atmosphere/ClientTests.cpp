// U15 (atmosphere) selftests of the client mod ("atmosphere.client_*", client build only). They run
// on the server thread of the local world and read the client state; the aurora and weather tests
// need the real player in the Twilight Forest (the client scenario teleports it first) and only
// report when it is elsewhere.

#ifdef GF_CLIENT

#include <algorithm>

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
    atc::setPreview({{"biome", "twilightforest:glacier"}});
    co_await ctx.ticks(30);
    int half = atc::state().aurora;
    co_await ctx.ticks(40);
    int full = atc::state().aurora;
    ctx.check(half > 10 && half < 50, "the aurora fades in over 60 ticks (" + std::to_string(half) + " after 30)");
    ctx.check(full == ca::kAuroraMax, "the aurora is full after 70 ticks (" + std::to_string(full) + ")");
    // (an empty auroraBiomes list turning it off is core::atmosphere::tickAurora's gtest: the config
    // is read by the client and render threads, a selftest must not change it under them)
    atc::setPreview({{"biome", "twilightforest:forest"}});
    co_await ctx.ticks(10);
    int fading = atc::state().aurora;
    ctx.check(fading < full && fading > 30, "outside the glacier the aurora fades out by 1 a tick (" + std::to_string(fading) + " after 10)");
    atc::setPreview(nlohmann::json::object());
});

GF_REGISTER_SELFTEST("atmosphere.client_weather", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!atc::state().inTwilight) {
        ctx.info("the local player is not in the Twilight Forest: locked weather not checked");
        co_return;
    }
    atc::setPreview({{"biome", "twilightforest:snowy_forest"}, {"locked", true}});
    co_await ctx.ticks(3);
    ctx.check(atc::state().nearLocked, "a locked biome around the player draws its weather");
    atc::setPreview({{"biome", "twilightforest:forest"}, {"locked", true}});
    co_await ctx.ticks(3);
    ctx.check(!atc::state().nearLocked, "a biome without a restriction draws none");
    atc::setPreview({{"biome", "twilightforest:forest"}, {"urGhast", true}});
    co_await ctx.ticks(14);
    float full = atc::state().urGhastRain;
    ctx.check(full > 0.95f, "the Ur-Ghast's tantrum raises its rain by 0.1 a tick to 1 (" + std::to_string(full) + ")");
    atc::setPreview(nlohmann::json::object());
    co_await ctx.ticks(10);
    float after = atc::state().urGhastRain;
    ctx.check(after < 0.95f && after > 0.6f, "and it falls by 0.02 a tick after (" + std::to_string(after) + ")");
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
    bool const creative = lp->isCreative();
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
    if (creative) (void)gf::selftest::runServerCommand("gamemode creative @a[tag=gf_atmosphere_real]");
    (void)gf::selftest::runServerCommand("tag @a remove gf_atmosphere_real");
});

} // namespace

#endif // GF_CLIENT

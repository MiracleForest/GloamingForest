// U15 (atmosphere) selftests, server side (both builds): the vanilla fog fallback, the shipped fog
// definitions, the acid rain damage type and sound, the config section; and the atmosphere.show_*
// previews of the client e2e scenario (they send "atmosphere.preview" to the companions tagged
// gf_atmosphere_show and pass without one, so the 'full' run on BDS is unaffected). dx 1500..1599.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "core/atmosphere/Atmosphere.h"
#include "feature/atmosphere/Atmosphere.h"
#include "feature/atmosphere/Internal.h"
#include "feature/combat/Combat.h"
#include "feature/fx/Fx.h"
#include "feature/net/Net.h"
#include "mod/Config.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using Json   = nlohmann::json;
namespace at = gf::feature::atmosphere;
namespace ca = gf::core::atmosphere;
namespace net = gf::feature::net;

GF_REGISTER_SELFTEST("atmosphere.config", [](Context& ctx) {
    at::AtmosphereConfig d{};
    ctx.check(d.auroraBiomes == std::vector<std::string>{"twilightforest:glacier"}, "auroraBiomes defaults to the glacier (TFClientConfig)");
    ctx.check(d.sky && d.fog && d.weather && d.aurora, "the client mod switches are on by default");
    auto sections = gf::config::sections();
    ctx.check(std::find(sections.begin(), sections.end(), "atmosphere") != sections.end(), "config section 'atmosphere' registered");
});

GF_REGISTER_SELFTEST("atmosphere.fog_stack", [](Context& ctx) {
    using V = std::vector<std::string>;
    ctx.check(at::fogStackFor("twilightforest:spooky_forest", 120) == V{"twilightforest:spooky_forest"}, "spooky forest: its own (near) fog at any height");
    ctx.check(at::fogStackFor("twilightforest:dark_forest", 10) == V{"twilightforest:dark_forest_near"}, "dark forest below y 20: near fog");
    ctx.check(at::fogStackFor("twilightforest:dark_forest", 25) == V{"twilightforest:dark_forest"}, "dark forest above y 20: its biome fog");
    ctx.check(at::fogStackFor("twilightforest:dark_forest_center", 20) == V{"twilightforest:dark_forest_center_near"}, "dark forest center at y 20: near fog");
    ctx.check(at::fogStackFor("twilightforest:glacier", 0) == V{"twilightforest:glacier"}, "glacier: its biome fog");
    ctx.check(at::fogStackFor("minecraft:plains", 0).empty(), "a biome that is not TF's: no fog");
    // every fog definition ships in the resource pack with its identifier
    auto ids = at::fogDefinitionIds();
    ctx.check(ids.size() == 24, "24 fog definitions");
    auto rp = gf::Mod::getInstance().getSelf().getModDir() / "resource_packs" / "GloamingForest_RP" / "fogs" / "atmosphere";
    int  found = 0;
    for (auto const& id : ids) {
        auto           file = rp / (id.substr(id.find(':') + 1) + ".json");
        std::ifstream  in(file);
        if (!in) {
            ctx.check(false, "fog definition file " + file.filename().string());
            continue;
        }
        try {
            auto j = Json::parse(in, nullptr, true, true);
            auto got = j["minecraft:fog_settings"]["description"].value("identifier", std::string{});
            ctx.check(got == id, "fog " + id + " has identifier " + got);
            ++found;
        } catch (std::exception const& e) {
            ctx.check(false, "fog " + id + " parses: " + e.what());
        }
    }
    ctx.info(std::to_string(found) + " fog definitions found in " + rp.string());
});

GF_REGISTER_SELFTEST("atmosphere.fog_packet", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inTwilight(), "the Twilight Forest is available");
    auto& p = ctx.player();
    at::forgetFog(p);
    auto before = at::fogPacketsSent();
    ctx.check(at::updateFog(p, "twilightforest:spooky_forest"), "entering the spooky forest sends a fog stack");
    auto sent = at::sentFogStack(p);
    ctx.check(sent && *sent == std::vector<std::string>{"twilightforest:spooky_forest"}, "the spooky forest fog was sent");
    ctx.check(!at::updateFog(p, "twilightforest:spooky_forest"), "the same stack is not sent again");
    double y      = p.getPosition().y;
    auto   expect = at::fogStackFor("twilightforest:dark_forest", y);
    ctx.check(at::updateFog(p, "twilightforest:dark_forest"), "moving to the dark forest sends its fog");
    ctx.check(at::sentFogStack(p) == expect, "dark forest fog at y " + std::to_string(static_cast<int>(y)) + ": " + expect.front());
    ctx.check(at::updateFog(p, "minecraft:plains"), "a biome without TF fog clears the stack");
    ctx.check(at::sentFogStack(p) && at::sentFogStack(p)->empty(), "the cleared stack is empty");
    ctx.check(at::fogPacketsSent() - before >= 3, "the PlayerFogPackets went out (other players may get theirs meanwhile)");
    at::forgetFog(p);
});

GF_REGISTER_SELFTEST("atmosphere.acid_rain", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the damage type and sound event U14's acid rain enforcement uses (TFDamageTypes.ACID_RAIN,
    // TFSounds.ACID_RAIN)
    auto& p      = ctx.player();
    int   before = p.getHealth();
    ctx.check(gf::combat::hurt(p, 2.0f, "twilightforest:acid_rain"), "twilightforest:acid_rain damage applies");
    co_await ctx.ticks(1);
    auto& q = ctx.player();
    ctx.check(q.getHealth() < before, "the acid rain hurt the player (" + std::to_string(before) + " -> " + std::to_string(q.getHealth()) + ")");
    gf::fx::sound(q.getDimension(), "environment.twilightforest.acid_rain", q.getPosition(), 1.0f, 1.0f);
});

// ---- previews of the client scenario -------------------------------------------------------------------

/// The quad winding the following previews ask for (0 as built, 1 reversed, 2 both): the scenario's
/// calibration of which side the engine's materials cull.
int gWinding = 0;

int preview(Context& ctx, Json const& msg) {
    int  n     = 0;
    auto level = ll::service::getLevel();
    if (!level) return 0;
    level->forEachPlayer([&](Player& p) {
        if (!p.hasTag(at::kShowTag) || !net::isCompanion(p)) return true;
        Json m = msg;
        if (gWinding != 0 && !m.empty()) m["winding"] = gWinding;
        if (m.contains("protectedAround")) { // boxes around the player: [dx0, dy0, dz0, dx1, dy1, dz1, protected]
            auto const& pos = p.getPosition();
            int         px = static_cast<int>(std::floor(pos.x)), py = static_cast<int>(std::floor(pos.y)), pz = static_cast<int>(std::floor(pos.z));
            Json        boxes = Json::array();
            for (auto const& b : m["protectedAround"])
                boxes.push_back(Json::array({px + b[0].get<int>(), py + b[1].get<int>(), pz + b[2].get<int>(), px + b[3].get<int>(), py + b[4].get<int>(), pz + b[5].get<int>(), b[6].get<bool>()}));
            m.erase("protectedAround");
            m["protected"] = boxes;
        }
        if (net::send(p, at::kChannelPreview, m)) ++n;
        return true;
    });
    ctx.info(n ? "preview " + msg.dump() + " sent to " + std::to_string(n) + " player(s)" : std::string("no companion tagged ") + at::kShowTag);
    return n;
}

GF_REGISTER_SELFTEST("atmosphere.show_clear", [](Context& ctx) {
    gWinding = 0;
    preview(ctx, Json::object());
});
GF_REGISTER_SELFTEST("atmosphere.show_winding_reversed", [](Context& ctx) {
    gWinding = 1;
    ctx.info("previews ask for reversed quads");
});
GF_REGISTER_SELFTEST("atmosphere.show_winding_both", [](Context& ctx) {
    gWinding = 2;
    ctx.info("previews ask for both windings");
});
GF_REGISTER_SELFTEST("atmosphere.show_forest", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:forest"}}); });
GF_REGISTER_SELFTEST("atmosphere.show_spooky", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:spooky_forest"}}); });
GF_REGISTER_SELFTEST("atmosphere.show_dark_forest", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:dark_forest"}}); });
GF_REGISTER_SELFTEST("atmosphere.show_glacier", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}}); });
// calibration of the e2e (GF_ATMOSPHERE_CALIBRATE=1): the aurora and the stars through every pass and
// material the client can use (auroraMode 1 sky + ui_fill_gradient, 2 world + particle blend, 3 sky +
// stars, 4 world + wireframe, 5 sky + particle blend; starsMode 1 sky + stars, 2 world + particle
// blend, 3 world + stars), the Vibrant Visuals fog stretch, the void plate and the boss world fog
GF_REGISTER_SELFTEST("atmosphere.show_glacier_stars_material", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}, {"auroraMode", 3}}); });
GF_REGISTER_SELFTEST("atmosphere.show_aurora_sky_ui", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}, {"auroraMode", 1}}); });
GF_REGISTER_SELFTEST("atmosphere.show_aurora_world_blend", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}, {"auroraMode", 2}}); });
GF_REGISTER_SELFTEST("atmosphere.show_aurora_world_wire", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}, {"auroraMode", 4}}); });
GF_REGISTER_SELFTEST("atmosphere.show_aurora_sky_blend", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}, {"auroraMode", 5}}); });
GF_REGISTER_SELFTEST("atmosphere.show_stars_world_blend", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:forest"}, {"starsMode", 2}}); });
GF_REGISTER_SELFTEST("atmosphere.show_stars_world_stars", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:forest"}, {"starsMode", 3}}); });
GF_REGISTER_SELFTEST("atmosphere.show_vv_fog_1", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}, {"vvFog", 1.0}}); });
GF_REGISTER_SELFTEST("atmosphere.show_vv_fog_24", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:glacier"}, {"vvFog", 24.0}}); });
GF_REGISTER_SELFTEST("atmosphere.show_void_plate", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:forest"}, {"voidPlate", true}}); });
GF_REGISTER_SELFTEST("atmosphere.show_boss_fog", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:forest"}, {"bossFog", true}}); });
GF_REGISTER_SELFTEST("atmosphere.show_blizzard", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:snowy_forest"}, {"locked", true}}); });
GF_REGISTER_SELFTEST("atmosphere.show_mosquitoes", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:swamp"}, {"locked", true}}); });
GF_REGISTER_SELFTEST("atmosphere.show_ashes", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:fire_swamp"}, {"locked", true}}); });
GF_REGISTER_SELFTEST("atmosphere.show_dark_streams", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:dark_forest"}, {"locked", true}}); });
GF_REGISTER_SELFTEST("atmosphere.show_big_rain", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:highlands"}, {"locked", true}}); });
GF_REGISTER_SELFTEST("atmosphere.show_sparkles", [](Context& ctx) {
    // "locked": the preview stands in for enforced progression (TF draws the sparkles only then)
    preview(ctx, {{"biome", "twilightforest:forest"}, {"locked", true}, {"protectedAround", Json::array({Json::array({-8, -4, -8, 8, 12, 8, true}), Json::array({-1, -4, -1, 1, 12, 1, false})})}});
});
GF_REGISTER_SELFTEST("atmosphere.show_ur_ghast", [](Context& ctx) { preview(ctx, {{"biome", "twilightforest:forest"}, {"urGhast", true}}); });

// The vanilla fallback on a real (vanilla) client (the remote-vanilla e2e): the tagged players that
// are not companions get the fog of `biome` (pinned until show_vanilla_clear), as if they stood in it.
int pinVanilla(Context& ctx, std::optional<std::string> const& biome) {
    int  n     = 0;
    auto level = ll::service::getLevel();
    if (level)
        level->forEachPlayer([&](Player& p) {
            if (!p.hasTag(at::kShowTag) || net::isCompanion(p) || p.isSimulatedPlayer()) return true;
            at::pinFogBiome(p, biome);
            at::updateFog(p);
            ++n;
            return true;
        });
    ctx.info(n ? std::string(biome ? *biome : "the player's own biome") + " fog for " + std::to_string(n) + " vanilla player(s)"
               : std::string("no vanilla player tagged ") + at::kShowTag);
    return n;
}
GF_REGISTER_SELFTEST("atmosphere.show_vanilla_spooky", [](Context& ctx) { pinVanilla(ctx, "twilightforest:spooky_forest"); });
GF_REGISTER_SELFTEST("atmosphere.show_vanilla_glacier", [](Context& ctx) { pinVanilla(ctx, "twilightforest:glacier"); });
GF_REGISTER_SELFTEST("atmosphere.show_vanilla_clear", [](Context& ctx) { pinVanilla(ctx, std::nullopt); });

} // namespace

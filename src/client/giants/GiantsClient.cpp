// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/CloudEvents.java
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java (beanstalk rumble)
//   src/main/java/gloamingforest/client/renderer/TFGiantBlockOutlines (ClientEvents#renderGiantBlockOutlines)
//   src/main/java/gloamingforest/block/UberousSoilBlock.java (animateTick)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U29's client mod:
// - CloudEvents: every 10 ticks the clouds within 10 columns of the camera that rain or snow (and
//   have room below) are collected; each tick drops fall from them to the stop height (the
//   resource pack's gf:giants_cloud_rain / _snow effects: the sheets as streaks) with rain splashes
//   (smoke on lava, magma, lit campfires) and the vanilla rain sound now and then;
//   client.cloudBlockPrecipitationDistance (-1: the common value, 0: off).
// - the beanstalk rumble: "giants.rumble" shakes the camera within 16 blocks (I = 1 - d^2 / 256).
// - the giant block outline: holding a giant pickaxe or a giant block, the targeted cell gets a
//   4 x 4 x 4 box (black, alpha 0.45) besides Bedrock's own highlight.
// - UberousSoilBlock#animateTick: with magic beans in hand, uberous soil sparkles (happy villager).
#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <mutex>
#include <random>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/comprehensive/ParticleType.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "client/Client.h"
#include "client/render/Render.h"
#include "client/world/Sky.h"
#include "core/giants/Giants.h"
#include "feature/fx/FxInternal.h"
#include "feature/giants/Giants.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::giants {

namespace fg = gf::feature::giants;
namespace cg = gf::core::giants;

namespace {

struct State {
    std::vector<fg::PrecipitationColumn> columns;
    int                    ticks         = 0;
    int                    rainSoundTime = 0;
    ll::event::ListenerPtr listener;
    bool                   loggedRain = false;
};
State& st() {
    static State s;
    return s;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float randf() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }
int   randi(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(rng()); }

/// The molang map an effect is added with (never destroyed: the game's destructor must not run at
/// process exit).
gf::fx::MolangMap& molang() {
    static auto* m = new gf::fx::MolangMap();
    return *m;
}

int clientDistance() { return cg::clientPrecipitationDistance(fg::config().clientCloudBlockPrecipitationDistance, fg::commonPrecipitationDistance()); }

void weatherTick(LocalPlayer& p) {
    auto& s        = st();
    int   distance = clientDistance();
    if (distance <= 0) {
        s.columns.clear();
        return;
    }
    auto& region = p.getDimensionBlockSource();
    auto  at     = game::blockPosOf(p.getPosition());
    // CloudEvents: r = 10 columns with fancy graphics (the radius upstream uses by default)
    if (s.ticks % 10 == 0) s.columns = fg::collectPrecipitationColumns(region, at, distance, 10);
    if (s.columns.empty()) return;
    auto& level = p.getLevel();
    // the sheets: a few drops per column and tick
    int drops = std::min<int>(static_cast<int>(s.columns.size()) * 2, 40);
    for (int i = 0; i < drops; ++i) {
        auto const& c    = s.columns[static_cast<std::size_t>(randi(static_cast<int>(s.columns.size())))];
        float       fall = static_cast<float>(c.cloud.y - c.stopY);
        molang().setVelocity(Vec3{0.0f, fall, 0.0f});
        Vec3 from{c.cloud.x + randf(), static_cast<float>(c.cloud.y), c.cloud.z + randf()};
        level.addParticleEffect(HashedString(c.p == fg::Precipitation::Snow ? "gf:giants_cloud_snow" : "gf:giants_cloud_rain"), from, molang().get());
    }
    // the splashes: up to 100 random columns within 10 blocks (CloudEvents#addRainParticles)
    std::optional<BlockPos> splash;
    for (int j = 0; j < 100; ++j) {
        auto const& c = s.columns[static_cast<std::size_t>(randi(static_cast<int>(s.columns.size())))];
        if (c.p != fg::Precipitation::Rain) continue;
        if (std::abs(c.cloud.x - at.x) > 10 || std::abs(c.cloud.z - at.z) > 10) continue;
        BlockPos below{c.cloud.x, c.stopY - 1, c.cloud.z};
        bool     hot = fg::smokesInRain(region, below);
        Vec3 where{below.x + randf(), static_cast<float>(c.stopY), below.z + randf()};
        level.addParticle(hot ? ParticleType::Smoke : ParticleType::RainSplash, where, Vec3{}, 0, nullptr, false);
        splash = below;
        if (j >= 30) break;
    }
    if (splash && randi(3) < s.rainSoundTime++) {
        s.rainSoundTime = 0;
        if (auto* client = currentClient()) {
            bool above = splash->y > at.y + 1;
            Vec3 where{static_cast<float>(splash->x), static_cast<float>(splash->y), static_cast<float>(splash->z)};
            client->getSoundEngine()->play("ambient.weather.rain", where, above ? 0.1f : 0.2f, above ? 0.5f : 1.0f, std::nullopt);
        }
    }
    if (!s.loggedRain) {
        s.loggedRain = true;
        gf::log().info("giants: client draws cloud precipitation ({} columns)", s.columns.size());
    }
}

bool holdingBeans(LocalPlayer& p) { return game::items::idOf(p.getCarriedItem()) == fg::id::MagicBeans; }

/// UberousSoilBlock#animateTick over the blocks Level#animateTick visits (a sample per tick).
void soilSparkles(LocalPlayer& p) {
    if (!holdingBeans(p)) return;
    auto& region = p.getDimensionBlockSource();
    auto  at     = game::blockPosOf(p.getPosition());
    auto& level  = p.getLevel();
    for (int i = 0; i < 667; ++i) {
        BlockPos q{at.x + randi(16) - randi(16), at.y + randi(16) - randi(16), at.z + randi(16) - randi(16)};
        if (q.y < region.getMinHeight() || q.y >= region.getMaxHeight()) continue;
        if (game::blockId(region.getBlock(q)) != fg::id::UberousSoil) continue;
        if (randi(5) != 0) continue;
        for (int k = 0; k < 2; ++k)
            level.addParticleEffect(HashedString("minecraft:villager_happy"), Vec3{q.x + randf(), q.y + 1.25f, q.z + randf()}, molang().get());
    }
}

void onTick() {
    auto* p = currentPlayer();
    if (!p) return;
    auto& s = st();
    ++s.ticks;
    molang().setVelocity(Vec3{});
    weatherTick(*p);
    soilSparkles(*p);
}

/// "giants.rumble" {pos, ticks}: ClientGameEvents' shake while isBeanstalkRumbling.
void onRumble(nlohmann::json const& msg) {
    auto* p = currentPlayer();
    if (!p || !msg.contains("pos")) return;
    auto const& pos = msg["pos"];
    Vec3        c{pos[0].get<float>() + .5f, pos[1].get<float>() + .5f, pos[2].get<float>() + .5f};
    auto        me    = p->getPosition();
    float       dx = me.x - c.x, dy = me.y - c.y, dz = me.z - c.z;
    float       dist2 = dx * dx + dy * dy + dz * dz;
    if (dist2 > 256.0f) return;
    float intensity = 1.0f - dist2 / 256.0f;
    gf::client::camera::shake(intensity, msg.value("ticks", 110));
}

bool holdsGiantTool(LocalPlayer& p) {
    auto held = game::items::idOf(p.getCarriedItem());
    return held == fg::id::GiantPickaxe || fg::isGiantBlock(held);
}

/// ClientEvents#renderGiantBlockOutlines.
void drawOutline(render::RenderContext& ctx, float) {
    auto* p = currentPlayer();
    if (!p || !holdsGiantTool(*p)) return;
    auto const& hit = p->getLevel().getHitResult();
    if (hit.mType != HitResultType::Tile) return;
    auto o = cg::cellOrigin(hit.mBlock.x, hit.mBlock.y, hit.mBlock.z);
    render::Lines lines(ctx);
    float const e = 0.002f;
    lines.box(Vec3{o[0] - e, o[1] - e, o[2] - e}, Vec3{o[0] + 4 + e, o[1] + 4 + e, o[2] + 4 + e}, 0x73000000);
}

class GiantsClientModule {
public:
    void install() {
        auto& s    = st();
        s.listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) {
            try {
                onTick();
            } catch (std::exception const& e) {
                gf::log().error("giants: client tick failed: {}", e.what());
            }
        });
        static bool registered = false;
        if (registered) return;
        registered = true;
        gf::feature::net::client::on("giants.rumble", onRumble);
        render::onWorld(drawOutline);
    }
    void uninstall() {
        auto& s = st();
        if (s.listener) ll::event::EventBus::getInstance().removeListener(s.listener);
        s.listener = {};
    }
};

} // namespace

} // namespace gf::client::giants

GF_REGISTER_MODULE(gf::client::giants::GiantsClientModule);

#endif // GF_CLIENT

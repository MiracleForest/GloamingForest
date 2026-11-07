// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/renderer/entity/layers/IceLayer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions), client mod: IceLayer, the ice cubes on a frosted living entity. The server names
// the frosted entities near the player (provisions.frosted: unique id and amplifier; Java reads the
// frosted_slowdown modifier from the synced attributes). Per entity, a java.util.Random seeded with
// id * id * 3121 + id * 45238971 (the int product first, as Java computes it; the id is the entity's
// runtime id, Java's network id) places int(height / 0.4) + (amplifier + 1) + 1 half-block ice cubes:
// x and z within a tenth of the width around the body, y = max(1.5 - rand * (height - special),
// -0.1) - special in the model's space (special 1 for the death tome), each tumbled by three random
// angles. The model space is LivingEntityRenderer's (body yaw, scale(-1, -1, 1), translate(0, -1.501,
// 0)). Drawn with the ice texture through drawGeo (alpha test: Java draws them translucent).
#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <numbers>

#include "nlohmann/json.hpp"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "client/Client.h"
#include "client/render/Render.h"
#include "core/jrand/JRand.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "feature/provisions/Internal.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::provisions {

namespace {

constexpr char kIceGeo[]     = "geometry.twilightforest.provisions.ice_cube";
constexpr char kIceTexture[] = "textures/blocks/ice";

struct State {
    std::mutex                        mutex;
    std::map<std::int64_t, int>       frosted; ///< unique id -> amplifier
    bool                              logged = false;
};
State& st() {
    static auto* s = new State;
    return *s;
}

void onFrosted(nlohmann::json const& msg) {
    std::map<std::int64_t, int> next;
    for (auto const& e : msg.value("entities", nlohmann::json::array()))
        if (e.is_array() && e.size() == 2 && e[0].is_number_integer() && e[1].is_number_integer()) next[e[0].get<std::int64_t>()] = e[1].get<int>();
    auto&           s = st();
    std::lock_guard lock(s.mutex);
    s.frosted = std::move(next);
}

/// IceLayer#render for one entity.
void drawIce(render::RenderContext& ctx, Actor& entity, int amplifier, float partial) {
    AABB const& box = entity.getAABB();
    float       w   = box.max.x - box.min.x;
    float       h   = box.max.y - box.min.y;
    // Java's int arithmetic: id * id overflows as an int before the long multiply
    auto         id   = static_cast<std::int32_t>(static_cast<std::uint32_t>(entity.getRuntimeID().rawID));
    auto         sq   = static_cast<std::int32_t>(static_cast<std::uint32_t>(id) * static_cast<std::uint32_t>(id));
    std::int64_t seed = static_cast<std::int64_t>(sq) * 3121LL + static_cast<std::int64_t>(id) * 45238971LL;
    core::jrand::LegacyRandom random(seed);
    // (int) (bbHeight / 0.4F) + (int) (frost.amount() / FROST_MULTIPLIER) + 1, the amount being -0.15 x (amplifier + 1)
    int   cubes   = static_cast<int>(h / 0.4f) + (amplifier + 1) + 1;
    float special = entity.getTypeName() == "twilightforest:death_tome" ? 1.0f : 0.0f;
    Vec3  pos     = entity.getInterpolatedPosition(partial);
    pos.y += entity.getFeetPos().y - entity.getPosition().y; // the feet (a player's position is its eyes)
    float yaw = entity.getInterpolatedBodyRot(partial);
    float rad = (180.0f - yaw) * std::numbers::pi_v<float> / 180.0f;
    float c = std::cos(rad), sn = std::sin(rad);
    for (int i = 0; i < cubes; ++i) {
        float dx = ((random.nextFloat() * (w * 2.0f)) - w) * 0.1f;
        float dy = std::max(1.5f - random.nextFloat() * (h - special), -0.1f) - special;
        float dz = ((random.nextFloat() * (w * 2.0f)) - w) * 0.1f;
        float rx = random.nextFloat() * 360.0f;
        float ry = random.nextFloat() * 360.0f;
        float rz = random.nextFloat() * 360.0f;
        // model space -> world: scale(-1, -1, 1) and translate(0, -1.501, 0), then the body yaw (Axis.YP 180 - yaw)
        float lx = -dx, ly = 1.501f - dy, lz = dz;
        Vec3  at{pos.x + lx * c + lz * sn, pos.y + ly, pos.z - lx * sn + lz * c};
        render::drawGeo(ctx, kIceGeo, kIceTexture, render::Transform{.translation = at, .rotationDeg = {rx, ry, rz}, .scale = {0.5f, 0.5f, 0.5f}});
    }
}

void drawAll(render::RenderContext& ctx, float partial) {
    auto* player = currentPlayer();
    if (!player) return;
    std::map<std::int64_t, int> frosted;
    {
        auto&           s = st();
        std::lock_guard lock(s.mutex);
        if (s.frosted.empty()) return;
        frosted = s.frosted;
    }
    auto& level = player->getLevel();
    // a render layer of the entity's model: the local player's own model is not drawn in first person
    bool firstPerson = ctx.actors && ctx.actors->mClientInstance.getOptions().getPlayerViewPerspective() == 0;
    std::int64_t self = player->getOrCreateUniqueID().rawID;
    int          drawn = 0;
    for (auto const& [uid, amp] : frosted) {
        if (firstPerson && uid == self) continue;
        Actor* a = level.fetchEntity(ActorUniqueID{uid}, false);
        if (!a || !a->isAlive()) continue;
        drawIce(ctx, *a, amp, partial);
        ++drawn;
    }
    if (drawn > 0 && !st().logged) {
        st().logged = true;
        log().info("provisions: ice cubes drawn on {} frosted entit{}", drawn, drawn == 1 ? "y" : "ies");
    }
}

} // namespace

class FrostedClientModule {
public:
    void install() {
        static bool const once = [] {
            feature::net::client::on(feature::provisions::kChanFrosted, onFrosted);
            render::onWorld(drawAll);
            return true;
        }();
        (void)once;
    }
    void uninstall() {
        auto&           s = st();
        std::lock_guard lock(s.mutex);
        s.frosted.clear();
    }
};

} // namespace gf::client::provisions

GF_REGISTER_MODULE(gf::client::provisions::FrostedClientModule);

#endif // GF_CLIENT

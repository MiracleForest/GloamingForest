// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/ISTER.java
//   src/main/java/twilightforest/client/renderer/block/TrophyRenderer.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U5 (building), client mod: trophy items in GUI slots (ISTER#renderByItem, GUI): the item's icon is
// the trophy type's backdrop sprite (item/trophy, trophy_minor, trophy_quest); the boss's head is
// drawn over it, tilted 30 degrees and turning ClientGameEvents.time % 360 degrees (one degree a
// client tick) while client.rotateTrophyHeadsGui is on (config section "building"), else at -45;
// the ur-ghast drawn 0.5 higher, the alpha yeti 0.15 lower. Held, worn and dropped trophies are
// F2's attachables.
#ifdef GF_CLIENT

#include <chrono>
#include <cmath>
#include <map>
#include <string>

#include "glm/gtc/matrix_transform.hpp"

#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/item/ItemStack.h"

#include "client/render/Render.h"
#include "feature/building/Building.h"
#include "feature/ext/ui/Client.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::building {

namespace r  = gf::client::render;
namespace fb = gf::feature::building;

namespace {

struct Head {
    std::string geo, texture;
    std::string geo2, texture2; ///< a second layer (the knight phantom's helmet, the questing ram's lines)
    float       lift = 0;       ///< ISTER: ur-ghast +0.5, alpha yeti -0.15
};

Head headOf(std::string const& boss) {
    static std::map<std::string, std::pair<std::string, std::string>> const textures{
        {"naga", {"nagahead", ""}},
        {"lich", {"twilightlich64", ""}},
        {"hydra", {"hydra4", ""}},
        {"ur_ghast", {"towerboss", ""}},
        {"knight_phantom", {"phantomskeleton", "../models/armor/phantom_layer_1"}},
        {"snow_queen", {"snowqueen", ""}},
        {"minoshroom", {"minoshroomtaur", ""}},
        {"alpha_yeti", {"yetialpha", ""}},
        {"quest_ram", {"questram", "questram_lines"}},
    };
    Head h;
    auto it = textures.find(boss);
    if (it == textures.end()) return h;
    h.geo     = "geometry.twilightforest." + boss + "_trophy";
    h.texture = "textures/twilightforest/entity/" + it->second.first;
    if (!it->second.second.empty()) {
        h.geo2     = h.geo + (boss == "knight_phantom" ? "_armor" : "_lines");
        h.texture2 = boss == "knight_phantom" ? "textures/twilightforest/models/armor/phantom_layer_1" : "textures/twilightforest/entity/questram_lines";
    }
    if (boss == "ur_ghast") h.lift = 0.5f;
    if (boss == "alpha_yeti") h.lift = -0.15f;
    return h;
}

/// ClientGameEvents.time in client ticks (20 a second).
float clientTicks() {
    using namespace std::chrono;
    static auto const start = steady_clock::now();
    return duration<float>(steady_clock::now() - start).count() * 20.0f;
}

void drawGui(r::RenderContext& ctx, std::string const& boss) {
    auto head = headOf(boss);
    if (head.geo.empty()) return;
    float spin = gf::config::get<fb::BuildingConfig>().rotateTrophyHeadsGui ? std::fmod(clientTicks(), 360.0f) : -45.0f;
    float size = ctx.guiSize;
    // ISTER (GUI): translate(0.5, 0.5, 0.5), rotate x 30, rotate -y spin, translate(-0.5, -0.5, -0.5),
    // translate(0, 0.25 + lift, 0), then TrophyRenderer's floor pose (F2's floor geometry): the
    // block's unit square is the slot (y down in GUI pixels)
    glm::mat4 m = glm::translate(glm::mat4{1.f}, glm::vec3{ctx.guiX + size * 0.5f, ctx.guiY + size * 0.5f, 0.f});
    m           = glm::scale(m, glm::vec3{size, -size, size});
    m           = glm::rotate(m, glm::radians(30.f), glm::vec3{1, 0, 0});
    m           = glm::rotate(m, glm::radians(-spin), glm::vec3{0, 1, 0});
    m           = glm::translate(m, glm::vec3{0.f, -0.5f + 0.25f + head.lift, 0.f});
    auto& world = ctx.screen.camera.worldMatrixStack.get();
    auto  ref   = world.push(false);
    ref.mat->_m.get() = ref.mat->_m.get() * m;
    r::drawGeo(ctx, head.geo, head.texture, {});
    if (!head.geo2.empty()) r::drawGeo(ctx, head.geo2, head.texture2, {});
}

struct TrophyItemsModule {
    void install() {
        for (auto const& boss : fb::trophyBosses())
            r::registerItemRenderer("twilightforest:" + boss + "_trophy", [boss](r::RenderContext& ctx, ItemStack const&, r::ItemContext where) {
                if (where == r::Gui) drawGui(ctx, boss);
            }, r::Gui);
        log().info("building: trophy item renderers registered");
    }
    void uninstall() {
        for (auto const& boss : fb::trophyBosses()) r::unregisterItemRenderer("twilightforest:" + boss + "_trophy");
    }
};

} // namespace

} // namespace gf::client::building

GF_REGISTER_MODULE(gf::client::building::TrophyItemsModule);

#endif // GF_CLIENT

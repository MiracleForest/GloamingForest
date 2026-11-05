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
// the ur-ghast drawn 0.5 higher, the alpha yeti 0.15 lower; the ur-ghast waves its tentacles with
// the animation time ISTER passes (drawn bone by bone). Held, worn and dropped trophies are F2's
// attachables (the ur-ghast's with F2's tentacle animation).
#ifdef GF_CLIENT

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "glm/gtc/matrix_transform.hpp"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Geo.h"
#include "client/render/Render.h"
#include "feature/building/Building.h"
#include "feature/ext/ui/Client.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
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
    // ISTER lifts the ur-ghast 0.5; F2's floor geometry already holds UrGhastModel#renderTrophy's
    // block-only translate (0, -1, 0) under scale(-1, -1, 1) (the body 1 block higher than in GUI form)
    if (boss == "ur_ghast") h.lift = 0.5f - 1.0f;
    if (boss == "alpha_yeti") h.lift = -0.15f;
    return h;
}

/// ClientGameEvents.time: client ticks counted while the game runs (ClientTickEvent.Post, not while
/// paused: a paused single-player game does not tick the local player either).
std::atomic<std::uint64_t>& clientTime() {
    static std::atomic<std::uint64_t> t{0};
    return t;
}

std::atomic<std::int64_t>& lastTickNs() {
    static std::atomic<std::int64_t> t{0};
    return t;
}

float clientTicks() { return static_cast<float>(clientTime().load(std::memory_order_relaxed) % 360); }

/// ClientGameEvents.time + the frame's part of the running tick (ISTER's realtime delta ticks).
float clientTimeWithPartial() {
    using namespace std::chrono;
    auto  now     = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    float partial = std::clamp(static_cast<float>(now - lastTickNs().load(std::memory_order_relaxed)) / 50'000'000.0f, 0.0f, 1.0f);
    return static_cast<float>(clientTime().load(std::memory_order_relaxed) % 72000) + partial;
}

// ---- the ur-ghast's waving tentacles in GUI slots ------------------------------------------------
// ISTER hands TrophyRenderer the animation time (ClientGameEvents.time + delta), and the ur-ghast
// trophy waves its tentacles with it (UrGhastModel#setupRotationsForTrophy: waveTentacles(x, x * 0.2),
// x = time * 4.5). F2's floor geometry holds the pose at time 0; each bone of it is drawn on its own
// here (one geometry per bone, cubes about the bone's pivot) with the rotation of the moment: the
// baked rotation plus the Java rotation's change since time 0 (F2's base animation's terms).

struct Bone {
    std::string id; ///< the per-bone geometry ("geometry.gf.building.ur_ghast_gui.<bone>"), empty without cubes
    int         parent = -1;
    glm::vec3   pivot{0.f}, rotation{0.f}; ///< json space, degrees (Bedrock: Z, then Y, then X)
    int         tentacle = -1, segment = -1;
};

std::vector<Bone> const* urGhastBones() {
    static std::vector<Bone> bones;
    static bool              tried = false;
    if (tried) return bones.empty() ? nullptr : &bones;
    tried = true;
    namespace fs = std::filesystem;
    fs::path file;
    std::error_code ec;
    auto root = gf::Mod::getInstance().getSelf().getModDir() / "resource_packs";
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && file.empty(); it.increment(ec))
        if (!ec && it->path().filename() == "ur_ghast_trophy.geo.json") file = it->path();
    if (file.empty()) {
        log().warn("building: ur_ghast_trophy.geo.json not found: the GUI ur-ghast trophy is drawn still");
        return nullptr;
    }
    std::ifstream in(file);
    auto          json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded()) return nullptr;
    nlohmann::json geo;
    for (auto const& g : json.value("minecraft:geometry", nlohmann::json::array()))
        if (g.contains("description") && g["description"].value("identifier", "") == "geometry.twilightforest.ur_ghast_trophy") geo = g;
    if (geo.is_null()) return nullptr;
    auto vec = [](nlohmann::json const& j) { return j.is_array() && j.size() == 3 ? glm::vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()} : glm::vec3{0.f}; };
    std::map<std::string, int>                       index;
    std::map<std::string, r::geo::Model>             models;
    for (auto const& b : geo.value("bones", nlohmann::json::array())) {
        Bone bone;
        auto name     = b.value("name", "");
        bone.pivot    = vec(b.value("pivot", nlohmann::json()));
        bone.rotation = vec(b.value("rotation", nlohmann::json()));
        if (auto it = index.find(b.value("parent", "")); it != index.end()) bone.parent = it->second;
        int t = -1, k = -1;
        if (std::sscanf(name.c_str(), "tentacle_%d", &t) == 1) {
            k = name.ends_with("_tip") ? 3 : name.ends_with("_extension_2") ? 2 : name.ends_with("_extension") ? 1 : 0;
            bone.tentacle = t, bone.segment = k;
        }
        if (b.contains("cubes")) {
            // the bone alone, about its pivot, unrotated
            nlohmann::json one = {{"format_version", "1.12.0"}, {"minecraft:geometry", nlohmann::json::array()}};
            nlohmann::json cubes = b["cubes"];
            for (auto& c : cubes) {
                auto o      = vec(c.value("origin", nlohmann::json())) - bone.pivot;
                c["origin"] = {o.x, o.y, o.z};
                if (c.contains("pivot")) {
                    auto cp    = vec(c["pivot"]) - bone.pivot;
                    c["pivot"] = {cp.x, cp.y, cp.z};
                }
            }
            bone.id = "geometry.gf.building.ur_ghast_gui." + name;
            one["minecraft:geometry"].push_back({{"description", geo["description"]}, {"bones", {{{"name", name}, {"pivot", {0, 0, 0}}, {"cubes", cubes}}}}});
            one["minecraft:geometry"][0]["description"]["identifier"] = bone.id;
            for (auto& [id, model] : r::geo::parse(one)) models.emplace(id, std::move(model));
        }
        index[name] = static_cast<int>(bones.size());
        bones.push_back(std::move(bone));
    }
    r::geo::add(std::move(models));
    return &bones;
}

/// UrGhastModel#waveTentacles' rotations (radians) of tentacle `i`, segment `k` at animation time
/// `x` (setupRotationsForTrophy(x, ...): ageInTicks = x * 0.2): {xRot, yRot}.
glm::vec2 waveTentacle(int i, int k, float x) {
    float time = (x * 0.2f + static_cast<float>(i) * 9.0f) / 2.0f;
    switch (k) {
    case 0: return {0.2f + std::cos(time * 0.3335f) * 0.15f, 0.4f * std::sin(time * 0.3f)};
    case 1: return {0.1f + std::cos(time * 0.4445f) * 0.20f, 0.f};
    case 2: return {0.1f + std::cos(time * 0.5555f) * 0.25f, 0.f};
    default: return {0.1f + std::cos(time * 0.6665f) * 0.30f, 0.f};
    }
}

/// Draws the ur-ghast floor trophy bone by bone (the current world matrix: F2's floor geometry space).
bool drawUrGhastAnimated(r::RenderContext& ctx, std::string const& texture, float progress) {
    auto const* bones = urGhastBones();
    if (!bones) return false;
    constexpr float kDeg = 180.0f / 3.14159265358979f;
    float           x    = progress * 4.5f; // TrophyRenderer: setupRotationsForTrophy(animationProgress * 4.5, ...)
    // json space (pixels, Bedrock's +x) -> the parsed quads' space (blocks, x mirrored)
    glm::mat4 const s    = glm::scale(glm::mat4{1.f}, glm::vec3{-1.f / 16.f, 1.f / 16.f, 1.f / 16.f});
    glm::mat4 const sInv = glm::inverse(s);
    std::vector<glm::mat4> world(bones->size());
    auto& stack = ctx.screen.camera.worldMatrixStack.get();
    glm::mat4 const base = stack.top()._m.get();
    for (std::size_t b = 0; b < bones->size(); ++b) {
        auto const& bone = (*bones)[b];
        glm::vec3   rot  = bone.rotation;
        if (bone.tentacle >= 0) {
            auto now  = waveTentacle(bone.tentacle, bone.segment, x);
            auto zero = waveTentacle(bone.tentacle, bone.segment, 0.f);
            rot.x += (now.x - zero.x) * kDeg;
            rot.y += (now.y - zero.y) * kDeg;
        }
        glm::mat4 local = glm::translate(glm::mat4{1.f}, bone.pivot);
        local           = glm::rotate(local, glm::radians(rot.x), glm::vec3{1, 0, 0});
        local           = glm::rotate(local, glm::radians(rot.y), glm::vec3{0, 1, 0});
        local           = glm::rotate(local, glm::radians(rot.z), glm::vec3{0, 0, 1});
        local           = glm::translate(local, -bone.pivot);
        world[b]        = (bone.parent >= 0 ? world[bone.parent] : glm::mat4{1.f}) * local;
        if (bone.id.empty()) continue;
        auto ref          = stack.push(false);
        ref.mat->_m.get() = base * s * world[b] * glm::translate(glm::mat4{1.f}, bone.pivot) * sInv;
        r::drawGeo(ctx, bone.id, texture, {});
    }
    return true;
}

void drawGui(r::RenderContext& ctx, std::string const& boss) {
    auto head = headOf(boss);
    if (head.geo.empty()) return;
    // TFConfig.rotateTrophyHeadsGui && !minecraft.isPaused() ? ClientGameEvents.time % 360 : -45
    auto* client = gf::client::currentClient();
    bool  paused = client && client->isShowingPauseScreen();
    float spin   = gf::config::get<fb::BuildingConfig>().rotateTrophyHeadsGui && !paused ? std::fmod(clientTicks(), 360.0f) : -45.0f;
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
    if (boss == "ur_ghast" && drawUrGhastAnimated(ctx, head.texture, paused ? 0.f : clientTimeWithPartial())) return;
    r::drawGeo(ctx, head.geo, head.texture, {});
    if (!head.geo2.empty()) r::drawGeo(ctx, head.geo2, head.texture2, {});
}

struct TrophyItemsModule {
    void install() {
        gf::client::movement::onLocalTick(-1000, [](gf::client::movement::MoveState&) {
            clientTime().fetch_add(1, std::memory_order_relaxed);
            lastTickNs().store(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
        });
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

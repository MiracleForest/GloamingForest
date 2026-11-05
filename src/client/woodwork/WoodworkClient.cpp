// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/renderer/block/DryingRackRenderer.java
//   src/main/java/twilightforest/block/ClimbableHollowLogBlock.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Unit U4 (woodwork), client mod:
// - DryingRackRenderer: the item on a drying rack, drawn by the block renderer of the 19 racks
//   (contract client.render) from what the server sends ("woodwork.rack", feature/woodwork/
//   DryingRack.h): at 0.5 + 0.4 x the facing's step on x / z, 0.5 high for an item drawn as a 3D
//   block, 0.325 for #twilightforest:lower_on_drying_rack, else 0.45 (banners 0.4 lower, shields 0.1
//   lower), scaled 0.99, turned by -facing.toYRot about y, in the FIXED display context (an item:
//   turned 180 degrees about y, scale 0.5; a block: scale 0.5) and drawn with the game's item
//   renderer (ItemInHandRenderer::renderItem with the matrix as it is). The server's display entity
//   (gloamingforest:drying_rack_item, the vanilla clients' fallback) is not drawn here.
// - The TF boats are drawn with their client entities (TF geometry, textures, paddles) instead of
//   the game's boat renderer (feature/ext/woodwork/EntityRenderers.h).
// - Climbing a climbable hollow log (BlockProperty::Climbable, F11) is the game's ladder movement;
//   one trace line when the local player first climbs one (e2e).

#ifdef GF_CLIENT

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>

#include "glm/gtc/matrix_transform.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"

#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/deps/core/math/Matrix.h"
#include "mc/deps/minecraft_renderer/game/ItemContextFlags.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/MatrixStack.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/render/Render.h"
#include "feature/ext/ui/Client.h"
#include "feature/ext/woodwork/EntityRenderers.h"
#include "feature/net/Net.h"
#include "feature/woodwork/Woodwork.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::client::woodwork {

namespace ww    = gf::feature::woodwork;
namespace wr    = gf::woodwork_ext::renderers;
using Json      = nlohmann::json;
using Key       = std::tuple<int, int, int, int>; ///< dim, x, y, z

// ---- what the server sent ----------------------------------------------------------------------------

struct Rack {
    ItemStack    item;
    int          facing  = 2; ///< FacingID
    bool         lower   = false;
    std::int64_t display = 0;
};

namespace {

struct Store {
    std::mutex                                     mutex;
    std::map<Key, std::shared_ptr<Rack const>>     racks;
    std::set<std::int64_t>                         displays; ///< the display entities of the racks drawn here
};
Store& store() {
    static auto* s = new Store; // kept for the process
    return *s;
}

std::optional<Key> keyOf(Json const& p) {
    if (!p.is_array() || p.size() != 4) return std::nullopt;
    for (auto const& v : p)
        if (!v.is_number_integer()) return std::nullopt;
    return Key{p[0].get<int>(), p[1].get<int>(), p[2].get<int>(), p[3].get<int>()};
}

void rebuildDisplays(Store& s) {
    s.displays.clear();
    for (auto const& [k, r] : s.racks)
        if (r->display) s.displays.insert(r->display);
}

std::atomic<bool> gReceived{false};

void onRacks(Json const& msg) {
    if (!msg.is_object()) return;
    std::size_t n = 0;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        if (msg.value("reset", false)) s.racks.clear();
        if (auto g = msg.find("gone"); g != msg.end() && g->is_array())
            for (auto const& p : *g)
                if (auto k = keyOf(p)) s.racks.erase(*k);
        if (auto list = msg.find("racks"); list != msg.end() && list->is_array()) {
            for (auto const& e : *list) {
                if (!e.is_object()) continue;
                auto k = keyOf(e.value("pos", Json()));
                if (!k) continue;
                auto r = std::make_shared<Rack>();
                if (auto it = e.find("item"); it != e.end()) r->item = game::items::fromJson(*it);
                if (game::items::isEmpty(r->item)) {
                    s.racks.erase(*k);
                    continue;
                }
                int face  = ww::faceOf(e.value("facing", std::string("north")));
                r->facing = face < 2 ? 2 : face;
                r->lower  = e.value("lower", false);
                if (auto d = e.find("display"); d != e.end() && d->is_number_integer()) r->display = d->get<std::int64_t>();
                s.racks[*k] = std::move(r);
                ++n;
            }
        }
        rebuildDisplays(s);
    }
    if (n && !gReceived.exchange(true)) log().info("woodwork: drying rack items received for the client mod ({} racks)", n);
}

void clearRacks() {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.racks.clear();
    s.displays.clear();
}

std::shared_ptr<Rack const> rackAt(int dim, BlockPos const& p) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    auto            it = s.racks.find(Key{dim, p.x, p.y, p.z});
    return it == s.racks.end() ? nullptr : it->second;
}

bool isRackDisplay(Actor const& a) {
    if (a.getTypeName() != "gloamingforest:drying_rack_item") return false;
    auto            id = const_cast<Actor&>(a).getOrCreateUniqueID().rawID;
    auto&           s  = store();
    std::lock_guard lock(s.mutex);
    return s.displays.contains(id);
}

// ---- the item transform's last step: the game's item mesh in Java's item space ------------------------
//
// ItemInHandRenderer::renderItem with the matrix as it is draws the game's item mesh in the game's
// own item units; this maps Java's item model space (a 16 px item centred on the origin) onto it,
// measured in game (tools/e2e/client/scenarios/woodwork.ps1, the rack close-ups). A development
// file <mod>/woodwork_rack_calibration.json {"item": {"scale", "offset": [x, y, z], "rotation":
// [x, y, z]}, "block": {...}} replaces it while it exists (read every second).

struct Calibration {
    float     scale = 1.0f;
    glm::vec3 offset{0.0f};
    glm::vec3 rotation{0.0f};
};
struct Calibrations {
    Calibration item{1.0f, glm::vec3{0.0f}, glm::vec3{0.0f}};
    Calibration block{1.0f, glm::vec3{0.0f}, glm::vec3{0.0f}};
};

Calibration calibrationOf(Json const& j, Calibration fallback) {
    if (!j.is_object()) return fallback;
    Calibration c = fallback;
    c.scale       = j.value("scale", c.scale);
    auto vec      = [&](char const* key, glm::vec3 v) {
        auto a = j.value(key, Json());
        if (a.is_array() && a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) return glm::vec3{a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
        return v;
    };
    c.offset   = vec("offset", c.offset);
    c.rotation = vec("rotation", c.rotation);
    return c;
}

Calibrations const& calibrations() {
    static Calibrations cal;
    static auto         last = std::chrono::steady_clock::time_point{};
    auto                now  = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(1)) return cal;
    last = now;
    try {
        auto path = gf::Mod::getInstance().getSelf().getModDir() / "woodwork_rack_calibration.json";
        if (!std::filesystem::exists(path)) {
            cal = Calibrations{};
            return cal;
        }
        std::ifstream in(path);
        auto          doc = Json::parse(in, nullptr, false);
        Calibrations  c;
        c.item  = calibrationOf(doc.value("item", Json()), c.item);
        c.block = calibrationOf(doc.value("block", Json()), c.block);
        cal     = c;
    } catch (...) {}
    return cal;
}

glm::mat4 apply(glm::mat4 m, Calibration const& c) {
    m = glm::translate(m, c.offset);
    m = glm::rotate(m, glm::radians(c.rotation.y), glm::vec3{0, 1, 0});
    m = glm::rotate(m, glm::radians(c.rotation.x), glm::vec3{1, 0, 0});
    m = glm::rotate(m, glm::radians(c.rotation.z), glm::vec3{0, 0, 1});
    return glm::scale(m, glm::vec3{c.scale});
}

// ---- DryingRackRenderer ----------------------------------------------------------------------------------

constexpr float kViewDistance = 64.0f; ///< BlockEntityRenderer.getViewDistance

/// Direction#toYRot: south 0, west 90, north 180, east 270.
float yRotOf(int face) {
    switch (face) {
    case 3: return 0.0f;
    case 4: return 90.0f;
    case 5: return 270.0f;
    default: return 180.0f;
    }
}

std::atomic<bool> gDrawn{false};

void drawRack(render::RenderContext& ctx, BlockPos const& pos, Block const&, float) {
    auto* player = currentPlayer();
    if (!player || !ctx.actors) return;
    auto rack = rackAt(static_cast<int>(player->getDimensionId()), pos);
    if (!rack) return;
    float dx = pos.x + 0.5f - ctx.camera.x, dy = pos.y + 0.5f - ctx.camera.y, dz = pos.z + 0.5f - ctx.camera.z;
    if (dx * dx + dy * dy + dz * dz > kViewDistance * kViewDistance) return;
    ItemStack const& item  = rack->item;
    auto             id    = game::items::idOf(item);
    bool             block = ItemInHandRenderer::canTessellateAsBlockItem(item); // model.isGui3d()
    auto             step  = ww::faceStep(rack->facing);
    float            y     = block ? 0.5f : rack->lower ? 0.325f : 0.45f;
    if (id.ends_with("_banner")) y -= 0.4f;                     // ItemTags.BANNERS
    else if (id == "minecraft:shield" || id.ends_with("_shield")) y -= 0.1f; // Tags.Items.TOOLS_SHIELD
    // the renderer's pose, in camera-relative world space
    glm::mat4 m = glm::translate(glm::mat4{1.0f}, glm::vec3{pos.x + 0.4f * step[0] + 0.5f - ctx.camera.x, pos.y + y - ctx.camera.y, pos.z + 0.4f * step[2] + 0.5f - ctx.camera.z});
    m           = glm::scale(m, glm::vec3{0.99f});
    m           = glm::rotate(m, glm::radians(-yRotOf(rack->facing)), glm::vec3{0, 1, 0});
    // ItemDisplayContext.FIXED: item/generated turns 180 degrees about y, scale 0.5; block/block
    // scale 0.5
    if (!block) m = glm::rotate(m, glm::radians(180.0f), glm::vec3{0, 1, 0});
    m = glm::scale(m, glm::vec3{0.5f});
    auto const& cal = calibrations();
    m               = apply(m, block ? cal.block : cal.item);
    // the light at the rack (BlockEntityRenderer's packedLight)
    if (ctx.lightAt) render::setupLight(ctx, *ctx.lightAt, ctx.fullBright);
    auto& world       = ctx.screen.camera.worldMatrixStack.get();
    auto  ref         = world.push(false);
    ref.mat->_m.get() = ref.mat->_m.get() * m;
    BaseActorRenderContext& rc = *ctx.actors;
    ItemInHandRenderer&     ir = rc.mItemInHandRenderer;
    ir.renderItem(rc, *player, item, false, static_cast<ItemContextFlags>(static_cast<unsigned char>(ItemContextFlags::WorldPass)), true, true);
    if (!gDrawn.exchange(true)) log().info("woodwork: a drying rack item drawn by the client mod ({} at {} {} {})", id, pos.x, pos.y, pos.z);
}

// ---- climbing ------------------------------------------------------------------------------------------

std::atomic<bool> gClimbed{false};

/// Whether `id` is a climbable hollow log.
bool climbableLog(std::string const& id) {
    auto const* set = ww::hollowLogOf(id);
    return set && set->climbable == id;
}

std::atomic<bool> gCrawled{false};

void watchClimb(movement::MoveState& s) {
    if (gClimbed.load(std::memory_order_relaxed) && gCrawled.load(std::memory_order_relaxed)) return;
    auto* player = currentPlayer();
    if (!player) return;
    auto     feet = player->getFeetPos();
    BlockPos at{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y + 0.1f)), static_cast<int>(std::floor(feet.z))};
    auto&    region = player->getDimensionBlockSource();
    auto     id     = game::blockId(region.getBlock(at));
    if (!gClimbed.load(std::memory_order_relaxed) && s.velocity.y > 0.05f && climbableLog(id) && !gClimbed.exchange(true))
        log().info("woodwork: the local player climbs a climbable hollow log ({}) at y {:.2f}, moving up {:.3f}", id, feet.y, s.velocity.y);
    // the crawling pose in a horizontal hollow log (Java: Pose.SWIMMING from the sneak-click)
    auto const* set = ww::hollowLogOf(id);
    if (set && set->horizontal == id && (player->isCrawling() || player->isSwimming()) && !gCrawled.exchange(true))
        log().info("woodwork: the local player crawls in a horizontal hollow log ({})", id);
}

class WoodworkClientModule {
public:
    void install() {
        net::client::on("woodwork.rack", onRacks);
        mJoin = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientJoinLevelEvent>([](auto&) { clearRacks(); });
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { clearRacks(); });
        for (auto const& id : ww::dryingRackIds()) render::registerBlockRenderer(id, drawRack);
        for (auto const* type : {"twilightforest:boat", "twilightforest:chest_boat"}) wr::useDataDriven(type);
        mHide = wr::hideWhen(isRackDisplay);
        wr::install();
        movement::onLocalTick(0, watchClimb);
        log().info("woodwork: client mod parts installed (drying rack renderer on {} racks, TF boat client entities)", ww::dryingRackIds().size());
    }
    void uninstall() {
        wr::uninstall();
        if (mHide) wr::show(mHide);
        mHide = 0;
        for (auto const& id : ww::dryingRackIds()) render::unregisterBlockRenderer(id);
        if (mJoin) ll::event::EventBus::getInstance().removeListener(mJoin);
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        mJoin = nullptr;
        mExit = nullptr;
        clearRacks();
    }

private:
    ll::event::ListenerPtr mJoin;
    ll::event::ListenerPtr mExit;
    std::size_t            mHide = 0;
};

} // namespace

} // namespace gf::client::woodwork

GF_REGISTER_MODULE(gf::client::woodwork::WoodworkClientModule);

#endif // GF_CLIENT

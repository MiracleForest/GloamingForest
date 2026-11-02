// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/boss/bar/ClientTFBossBar.java
//   src/main/java/twilightforest/network/TFBossBarPacket.java
//   src/main/java/twilightforest/network/UpdateDeathTimePacket.java
//   src/main/java/twilightforest/network/MovePlayerPacket.java
//   src/main/java/twilightforest/network/UpdateThrownPacket.java
//   src/main/java/twilightforest/network/UpdateTFMultipartPacket.java
//   src/main/java/twilightforest/client/event/ClientGameEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The client mod's side of F8's channels:
//   boss.bar            ClientTFBossBar: the HUD's boss bar takes the boss's exact RGB (the vanilla
//                       packet only carries the nearest of 8 colours). The HUD asks the screen
//                       model for each bar's colour by index (ClientInstanceScreenModel::
//                       getBossBarColor); the hook answers with the RGB of the TF boss whose title
//                       (and, between equal titles, progress) that bar shows, matched once per
//                       change of the index's title or of the TF bars. The bar's overlay
//                       (BossBarOverlay NOTCHED_6/10/12/20: the Naga, the shielded Lich) is drawn
//                       over the HUD's bar after the HUD rendered (Bedrock draws no notches).
//   boss.death_time     UpdateDeathTimePacket (read with gf::client::entity::deathTime).
//   entity.move_player  MovePlayerPacket: the local player is pushed.
//   entity.hostile_mount  ClientGameEvents#removeHostileMountHealth: no mount hearts on a hostile
//                       mount (HudHorseHeartRenderer).
//   entity.multipart, entity.data, projectile.thrown: kept per entity for client renderers
//                       (feature/ext/entity/EntityClientExt.h).

#ifdef GF_CLIENT

#include "feature/ext/entity/EntityClientExt.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/controls/renderers/HudHorseHeartRenderer.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/models/ClientInstanceScreenModel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/entity/components/ActorUniqueIDComponent.h"
#include "mc/entity/components/death/DeathTickingComponent.h"
#include "mc/entity/systems/death/ClientMobTickDeathSystemImpl.h"
#include "mc/deps/core/math/Color.h"
#include "mc/safety/RedactableString.h"

#include "client/Client.h"
#include "client/screen/base/Screen.h"
#include "feature/net/Net.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::entity {

namespace {

using Json  = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct DeathTime {
    int               value = 0;
    Clock::time_point at;
};

/// The TF boss a HUD bar index shows, as last matched (by the bar's raw title).
struct IndexMatch {
    std::string   rawTitle;
    std::uint64_t generation = 0; ///< ClientState::generation when matched
    std::int64_t  boss       = 0; ///< 0: no TF boss
    Clock::time_point seen;       ///< the last frame the HUD asked for this bar
};

struct ClientState {
    std::mutex                                       mutex; ///< everything below: net handlers vs. HUD / renderers
    std::unordered_map<std::int64_t, BossBar>        bars;
    std::uint64_t                                    generation = 1; ///< bumped by every "boss.bar" message
    std::unordered_map<int, IndexMatch>              matches;        ///< HUD bar index -> TF boss
    std::set<std::int64_t>                           notchLogged;    ///< bars whose notches were drawn (one log line each)
    std::unordered_map<std::int64_t, DeathTime>      deathTimes;
    std::unordered_map<std::int64_t, std::vector<Part>> multipart;
    std::unordered_map<std::int64_t, std::int64_t>   partParents;
    std::unordered_map<std::int64_t, Json>           data;
    std::unordered_map<std::int64_t, Thrown>         thrown;
    std::set<std::int64_t>                           logged;   ///< bars whose colour was applied (one log line each)
    std::set<std::string>                            unmatched; ///< bar titles without a companion bar (one line each)
    std::atomic<bool>                                ridingUnfriendly{false};
};
ClientState& C() {
    static ClientState s;
    return s;
}

/// The text of a bar title without formatting codes.
std::string plain(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        // "§" is 0xC2 0xA7 in UTF-8, followed by the code character
        if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 2;
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

} // namespace

/// Whether the server still runs the death animation of the TF boss `uid` (its death time arrived
/// and its bar was not removed yet).
bool holdsDyingBody(std::int64_t uid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    return s.deathTimes.contains(uid);
}

/// ClientTFBossBar#renderBossBar's tint: the exact RGB of the TF boss the HUD's bar `index` shows.
/// The bar's title is matched against the TF bars once per change (a new raw title at the index or
/// a "boss.bar" message), not on every frame.
std::optional<mce::Color> exactColour(ClientInstanceScreenModel const& model, int index) {
    auto& s = C();
    {
        std::scoped_lock lock(s.mutex);
        if (s.bars.empty()) return std::nullopt;
    }
    std::string raw = model.getBossName(index).mUnredactedString;
    std::scoped_lock lock(s.mutex);
    auto& match = s.matches[index];
    match.seen  = Clock::now();
    if (match.generation != s.generation || match.rawTitle != raw) {
        std::string title  = plain(raw);
        float       health = model.getBossHealthPercentage(index);
        std::int64_t best  = 0;
        float        diff  = 2.0f;
        for (auto const& [id, b] : s.bars) {
            if (plain(b.title) != title) continue;
            float d = std::abs(b.progress - health);
            if (!best || d < diff) best = id, diff = d;
        }
        match.rawTitle   = std::move(raw);
        match.generation = s.generation;
        match.boss       = best;
        if (!best && s.unmatched.insert(title).second) log().info("client boss bar '{}' has no TF colour", title);
    }
    auto it = s.bars.find(match.boss);
    if (!match.boss || it == s.bars.end()) return std::nullopt;
    if (s.logged.insert(match.boss).second) log().info("client boss bar {} coloured #{:06X}", match.boss, it->second.rgb);
    return mce::Color(static_cast<uint>(it->second.rgb));
}

namespace {

/// The boss bar controls of the HUD (hud_screen.json: boss_health_grid -> one boss_health_panel
/// per bar, collection "boss_bars", with its progress_bar_for_collections).
UIControl* findControl(UIControl& root, std::string_view name, int depth = 0) {
    if (root.mName.get() == name) return &root;
    if (depth > 40) return nullptr;
    for (auto const& child : root.mChildren.get())
        if (child)
            if (auto* hit = findControl(*child, name, depth + 1)) return hit;
    return nullptr;
}

/// BossHealthOverlay#drawBar's overlay sprites (BossBarOverlay NOTCHED_6 / 10 / 12 / 20) over the
/// HUD's bar: Bedrock's HUD draws no notches, so the client mod adds them after the HUD rendered.
/// Java's notch sprites are Mojang's textures (not TF's assets, not on Bedrock): the notches are
/// drawn as 1-pixel dividers, darker over the bar's background than over its progress.
void drawNotches(ScreenView& view, MinecraftUIRenderContext& ctx) {
    auto& s = C();
    struct Want {
        int          index    = 0;
        int          notches  = 0;
        float        progress = 0;
        std::int64_t boss     = 0;
    };
    std::vector<Want> wants;
    {
        std::scoped_lock lock(s.mutex);
        auto             now = Clock::now();
        for (auto const& [index, match] : s.matches) {
            if (!match.boss || now - match.seen > std::chrono::milliseconds(500)) continue;
            auto it = s.bars.find(match.boss);
            if (it == s.bars.end() || it->second.overlay <= 0) continue;
            static constexpr int kNotches[] = {0, 6, 10, 12, 20};
            wants.push_back({index, kNotches[std::clamp(it->second.overlay, 0, 4)], it->second.progress, match.boss});
        }
    }
    if (wants.empty() || !view.mVisualTree.get()) return;
    // the grid control, found once per HUD tree (render thread only)
    static std::weak_ptr<UIControl> gridCache;
    std::shared_ptr<UIControl>      gridPtr = gridCache.lock();
    if (!gridPtr) {
        auto const& root = view.mVisualTree.get()->mRootControl;
        if (!root) return;
        UIControl* found = findControl(*root, "boss_health_grid");
        if (!found) return;
        // its owning pointer: the parent's child entry
        if (auto parent = found->mParent.lock())
            for (auto const& child : parent->mChildren.get())
                if (child.get() == found) gridPtr = child;
        if (!gridPtr) return;
        gridCache = gridPtr;
    }
    UIControl* grid = gridPtr.get();
    // after the HUD rendered, the context still clips to the last clipped control: draw unclipped
    ctx.saveCurrentClippingRectangle();
    ctx.setFullClippingRectangle();
    struct RestoreClip {
        MinecraftUIRenderContext& c;
        ~RestoreClip() { c.restoreSavedClippingRectangle(); }
    } restoreClip{ctx};
    gf::client::screen::Canvas canvas(ctx);
    for (auto const& item : grid->mChildren.get()) {
        if (!item || !item->mIsVisibleInTree) continue;
        int index = item->findCollectionIndex("boss_bars");
        auto want = std::find_if(wants.begin(), wants.end(), [&](Want const& w) { return w.index == index; });
        if (want == wants.end()) continue;
        UIControl* bar = findControl(*item, "progress_bar_for_collections");
        if (!bar) continue;
        if (bar->mCachedPositionDirty) bar->_setCachedPosition();
        auto  pos = bar->mCachedPosition.get();
        auto  sz  = bar->mSize.get();
        float x = pos.x, y = pos.y, w = sz.x, h = sz.y;
        if (w <= 0 || h <= 0) continue;
        float filled = x + w * std::clamp(want->progress, 0.0f, 1.0f);
        for (int k = 1; k < want->notches; ++k) {
            float nx = std::floor(x + w * static_cast<float>(k) / static_cast<float>(want->notches));
            canvas.fill({nx, y, 1.0f, h}, nx < filled ? 0x80000000u : 0x50000000u);
        }
        std::scoped_lock lock(s.mutex);
        if (s.notchLogged.insert(want->boss).second)
            log().info("client boss bar {} notched {} at {:.0f} {:.0f} ({:.0f} x {:.0f})", want->boss, want->notches, x, y, w, h);
    }
}

} // namespace

} // namespace gf::client::entity

// The HUD reads each bar's colour through the screen model: the TF bosses' bars get their RGB.
LL_TYPE_INSTANCE_HOOK(
    EntityClientBossColourHook,
    ll::memory::HookPriority::Normal,
    ClientInstanceScreenModel,
    &ClientInstanceScreenModel::getBossBarColor,
    mce::Color,
    int index
) {
    auto colour = origin(index);
    if (auto exact = gf::client::entity::exactColour(*this, index)) return *exact;
    return colour;
}

// A dying TF boss keeps its body for its whole death animation (the server holds its removal): the
// client's own death ticking would end the death at 20 ticks (death smoke, the body gone) while the
// server still has it, so it is held at 19 (the body lies on its side) until the server ends the
// death (the bar's "remove", then RemoveActor). Vanilla clients end it at 20 on their own.
LL_STATIC_HOOK(
    EntityClientDeathTickHook,
    ll::memory::HookPriority::Normal,
    &ClientMobTickDeathSystemImpl::_tick,
    void,
    StrictEntityContext&                                         entity,
    AABBShapeComponent const&                                    aabbShape,
    ActorDefinitionIdentifierComponent const&                    actorIdentifier,
    ActorUniqueIDComponent const&                                actorUniqueID,
    DimensionTypeComponent const&                                dimensionType,
    OffsetsComponent const&                                      offsets,
    SoundEventPlayerComponent const&                             soundEventPlayer,
    StateVectorComponent const&                                  stateVector,
    ActorDataFlagComponent&                                      actorFlags,
    ActorDataDirtyFlagsComponent&                                actorDirtyFlags,
    ActorOwnerComponent&                                         actorOwner,
    DeathTickingComponent&                                       deathTicking,
    Optional<PlayerComponent>                                    player,
    ViewT<StrictEntityContext, SoundEventRequestQueueComponent>& soundRequestQueueView
) {
    if (deathTicking.mTicks >= 19 && gf::client::entity::holdsDyingBody(actorUniqueID.mActorUniqueID->rawID)) return;
    origin(
        entity,
        aabbShape,
        actorIdentifier,
        actorUniqueID,
        dimensionType,
        offsets,
        soundEventPlayer,
        stateVector,
        actorFlags,
        actorDirtyFlags,
        actorOwner,
        deathTicking,
        player,
        soundRequestQueueView
    );
}

LL_TYPE_INSTANCE_HOOK(
    EntityClientMountHeartsHook,
    ll::memory::HookPriority::Normal,
    HudHorseHeartRenderer,
    &HudHorseHeartRenderer::$render,
    void,
    MinecraftUIRenderContext& renderContext,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    if (gf::client::entity::ridingUnfriendly()) return; // removeHostileMountHealth
    origin(renderContext, client, owner, pass);
}

namespace gf::client::entity {

// ---- readers (EntityClientExt.h) ----------------------------------------------------------------

Json data(std::int64_t uid, std::string const& key) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.data.find(uid);
    if (it == s.data.end() || !it->second.is_object() || !it->second.contains(key)) return Json();
    return it->second[key];
}

std::vector<std::string> stringList(std::int64_t uid, std::string const& key) {
    std::vector<std::string> out;
    auto                     v = data(uid, key);
    if (v.is_array())
        for (auto const& e : v)
            if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

std::vector<Part> parts(std::int64_t parentUid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.multipart.find(parentUid);
    return it != s.multipart.end() ? it->second : std::vector<Part>{};
}

std::int64_t partParent(std::int64_t partUid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.partParents.find(partUid);
    return it != s.partParents.end() ? it->second : 0;
}

int deathTime(std::int64_t uid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.deathTimes.find(uid);
    if (it == s.deathTimes.end()) return 0;
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - it->second.at).count() / 50;
    return it->second.value + static_cast<int>(ticks);
}

std::optional<Thrown> thrown(std::int64_t playerUid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.thrown.find(playerUid);
    return it != s.thrown.end() ? std::optional<Thrown>(it->second) : std::nullopt;
}

bool ridingUnfriendly() { return C().ridingUnfriendly.load(); }

std::optional<BossBar> bossBar(std::int64_t uid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    auto             it = s.bars.find(uid);
    return it != s.bars.end() ? std::optional<BossBar>(it->second) : std::nullopt;
}

namespace {

/// Forgets everything the previous server sent (a new world or server: its bosses and parts).
void clearState() {
    auto& s = C();
    {
        std::scoped_lock lock(s.mutex);
        s.bars.clear();
        s.matches.clear();
        s.notchLogged.clear();
        ++s.generation;
        s.logged.clear();
        s.unmatched.clear();
        s.deathTimes.clear();
        s.multipart.clear();
        s.partParents.clear();
        s.data.clear();
        s.thrown.clear();
    }
    s.ridingUnfriendly.store(false);
}

void registerHandlers() {
    namespace nc = gf::net::client;
    nc::onConnected(clearState);
    nc::on("boss.bar", [](Json const& m) {
        auto             id = m.value("id", std::int64_t{0});
        auto&            s  = C();
        std::scoped_lock lock(s.mutex);
        ++s.generation; // the HUD's title matches are made again
        auto             op = m.value("op", std::string("add"));
        if (op == "remove") {
            s.bars.erase(id);
            s.logged.erase(id);
            s.deathTimes.erase(id);
            return;
        }
        if (!s.bars.contains(id) && op == "add") log().info("client boss bar {} received (#{:06X})", id, m.value("rgb", 0u));
        auto& bar = s.bars[id];
        if (op == "progress") {
            bar.progress = m.value("progress", bar.progress);
            return;
        }
        if (op == "name") {
            bar.title = m.value("title", bar.title);
            return;
        }
        bar.rgb      = m.value("rgb", bar.rgb);
        bar.overlay  = m.value("overlay", bar.overlay);
        bar.darken   = m.value("darken", bar.darken);
        bar.fog      = m.value("fog", bar.fog);
        bar.progress = m.value("progress", bar.progress);
        bar.title    = m.value("title", bar.title);
        if (op == "style") s.logged.erase(id);
    });
    nc::on("boss.death_time", [](Json const& m) {
        auto&            s = C();
        std::scoped_lock lock(s.mutex);
        auto             id = m.value("id", std::int64_t{0});
        if (!s.deathTimes.contains(id)) log().info("client boss {} dying (death time {})", id, m.value("deathTime", 0));
        s.deathTimes[id] = DeathTime{m.value("deathTime", 0), Clock::now()};
    });
    nc::on("entity.move_player", [](Json const& m) {
        auto* player = gf::client::currentPlayer();
        if (!player) return;
        auto v = player->getPosDelta();
        const_cast<Vec3&>(player->getPosDelta()) = Vec3{v.x + m.value("x", 0.0f), v.y + m.value("y", 0.0f), v.z + m.value("z", 0.0f)};
    });
    nc::on("entity.hostile_mount", [](Json const& m) { C().ridingUnfriendly.store(m.value("riding", false)); });
    nc::on("entity.multipart", [](Json const& m) {
        auto&             s      = C();
        std::scoped_lock  lock(s.mutex);
        auto              parent = m.value("parent", std::int64_t{0});
        std::vector<Part> list;
        if (auto ps = m.find("parts"); ps != m.end() && ps->is_array())
            for (auto const& p : *ps)
                list.push_back(Part{p.value("id", std::string()), p.value("uid", std::int64_t{0}), p.value("rid", std::int64_t{0}), p.value("index", 0), p.value("width", 1.0f), p.value("height", 1.0f)});
        std::erase_if(s.partParents, [&](auto const& kv) { return kv.second == parent; });
        for (auto const& p : list) s.partParents[p.uid] = parent;
        s.multipart[parent] = std::move(list);
    });
    nc::on("entity.data", [](Json const& m) {
        auto&            s = C();
        std::scoped_lock lock(s.mutex);
        auto             id = m.value("id", std::int64_t{0});
        auto&            d  = s.data[id];
        if (!d.is_object()) d = Json::object();
        auto key = m.value("key", std::string());
        // the selftest fixtures' keys are logged (once per key) for the client e2e
        if (key.starts_with("gf_test:") && !d.contains(key)) log().info("client entity data {} {} = {}", id, key, m.value("data", Json()).dump());
        d[key] = m.value("data", Json());
    });
    nc::on("projectile.thrown", [](Json const& m) {
        auto&            s = C();
        std::scoped_lock lock(s.mutex);
        s.thrown[m.value("id", std::int64_t{0})] = Thrown{m.value("thrown", false), m.value("thrower", std::int64_t{0}), m.value("cooldown", 0)};
    });
}

class EntityClientModule {
public:
    // the render listener is added at mod load, before the game's threads run (as the screen
    // framework's: adding render listeners while the client runs can deadlock the game)
    void load() {
        auto& bus = ll::event::EventBus::getInstance();
        mListener = bus.emplaceListener<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
            if (ev.screenView().getScreenName() != "hud_screen") return;
            drawNotches(ev.screenView(), ev.uiRenderContext());
        });
    }
    void install() {
        static bool handlers = false;
        if (!std::exchange(handlers, true)) registerHandlers();
        EntityClientBossColourHook::hook();
        EntityClientMountHeartsHook::hook();
        EntityClientDeathTickHook::hook();
    }
    void uninstall() {
        EntityClientDeathTickHook::unhook();
        EntityClientMountHeartsHook::unhook();
        EntityClientBossColourHook::unhook();
        clearState();
    }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace
} // namespace gf::client::entity

GF_REGISTER_MODULE(gf::client::entity::EntityClientModule);

#endif

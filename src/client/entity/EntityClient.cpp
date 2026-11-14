// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/boss/bar/ClientTFBossBar.java
//   src/main/java/gloamingforest/network/TFBossBarPacket.java
//   src/main/java/gloamingforest/network/UpdateDeathTimePacket.java
//   src/main/java/gloamingforest/network/MovePlayerPacket.java
//   src/main/java/gloamingforest/network/UpdateThrownPacket.java
//   src/main/java/gloamingforest/network/UpdateTFMultipartPacket.java
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The client mod's side of F8's channels:
//   boss.bar            ClientTFBossBar: the HUD's boss bar takes the boss's exact RGB (the vanilla
//                       packet only carries the nearest of 8 colours). The HUD asks the screen
//                       model for each bar's colour by index (ClientInstanceScreenModel::
//                       getBossBarColor); the hook answers with the RGB of the upstream boss whose title
//                       (and, between equal titles, progress) that bar shows, matched once per
//                       change of the index's title or of the upstream bars. The bar's overlay
//                       (BossBarOverlay NOTCHED_6/10/12/20: the Naga, the shielded Lich): the
//                       HUD bar's two images get textures with the notch dividers in them (Bedrock
//                       draws no overlay), tinted like the bar as Java tints its notch sprites.
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
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/gui/controls/RenderableComponent.h"
#include "mc/client/gui/controls/SpriteComponent.h"
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
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/safety/RedactableString.h"

#include "client/Client.h"
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

/// The upstream boss a HUD bar index shows, as last matched (by the bar's raw title).
struct IndexMatch {
    std::string   rawTitle;
    std::uint64_t generation = 0; ///< ClientState::generation when matched
    std::int64_t  boss       = 0; ///< 0: no upstream boss
};

struct ClientState {
    std::mutex                                       mutex; ///< everything below: net handlers vs. HUD / renderers
    std::unordered_map<std::int64_t, BossBar>        bars;
    std::uint64_t                                    generation = 1; ///< bumped when the upstream bars change (titles, set, style)
    std::unordered_map<int, IndexMatch>              matches;        ///< HUD bar index -> upstream boss
    std::set<std::int64_t>                           notchLogged;    ///< bars whose notches were drawn (one log line each)
    std::uint64_t                                    notchVersion = 1; ///< bumped when an index's upstream bar or a bar's overlay changes
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

/// Whether the server still runs the death animation of the upstream boss `uid` (its death time arrived
/// and its bar was not removed yet).
bool holdsDyingBody(std::int64_t uid) {
    auto&            s = C();
    std::scoped_lock lock(s.mutex);
    return s.deathTimes.contains(uid);
}

/// ClientTFBossBar#renderBossBar's tint: the exact RGB of the upstream boss the HUD's bar `index` shows.
/// The bar's title is matched against the upstream bars once per change (a new raw title at the index or
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
        if (match.boss != best) ++s.notchVersion; // the bar at this index is another one now
        match.boss = best;
        if (!best && s.unmatched.insert(title).second) log().info("client boss bar '{}' has no upstream colour", title);
    }
    auto it = s.bars.find(match.boss);
    if (!match.boss || it == s.bars.end()) return std::nullopt;
    if (s.logged.insert(match.boss).second) log().info("client boss bar {} coloured #{:06X}", match.boss, it->second.rgb);
    return mce::Color(static_cast<uint>(it->second.rgb));
}

namespace {

/// The boss bar controls of the HUD (hud_screen.json: boss_health_grid -> one boss_health_panel
/// per bar, collection "boss_bars", with its progress_bar_for_collections of two images:
/// empty_progress_bar and filled_progress_bar_for_collections).
UIControl* findControl(UIControl& root, std::string_view name, int depth = 0) {
    if (root.mName.get() == name) return &root;
    if (depth > 40) return nullptr;
    for (auto const& child : root.mChildren.get())
        if (child)
            if (auto* hit = findControl(*child, name, depth + 1)) return hit;
    return nullptr;
}

/// The sprite of an image control (its one renderable component).
SpriteComponent* spriteOf(UIControl& image) {
    auto const& list = image.mRenderableComponentLookup.get();
    return list.empty() || !list.front() ? nullptr : static_cast<SpriteComponent*>(list.front());
}

/// ClientTFBossBar#renderBossBar's overlay (BossEvent.BossBarOverlay NOTCHED_6 / 10 / 12 / 20:
/// the Naga's, the shielded Lich's): Java draws BossHealthOverlay's notch sprites over the bar's
/// background and over its progress, tinted like the bar. Bedrock's HUD draws no overlay, and draws
/// issued from the render events do not reach its batched layer, so the client mod gives the HUD
/// bar's own two images textures with the dividers in them (the resource pack's
/// textures/ui/entity/boss_bar_notched_<n>_{background,progress}: original pixel art, the HUD tints
/// them with the bar's colour as Java tints its sprites) and puts the vanilla textures back when
/// the bar at that index shows no notched upstream bar. The textures change only when the bars do (the
/// HUD's matches or a "boss.bar" message), not per frame.
struct NotchedImage {
    std::weak_ptr<UIControl> control;
    std::string              vanilla; ///< the texture the HUD gave it
    std::string              applied; ///< the texture set now ("" = the vanilla one)
};
struct NotchState {
    std::weak_ptr<UIControl>  grid;
    std::uint64_t             version = 0; ///< ClientState::notchVersion of the last pass
    std::vector<UIControl*>   items;       ///< the grid's item controls in the last pass (identity only)
    std::vector<NotchedImage> images;      ///< every image the client mod changed
};
NotchState& notchState() {
    static NotchState n; // render thread only
    return n;
}
constexpr int kNotches[] = {0, 6, 10, 12, 20};

/// Sets an image's texture, remembering the HUD's own one.
void setTexture(UIControl& image, std::shared_ptr<UIControl> const& owner, std::string const& texture) {
    auto* sprite = spriteOf(image);
    if (!sprite) return;
    auto& list = notchState().images;
    auto  it   = std::find_if(list.begin(), list.end(), [&](NotchedImage const& n) { return n.control.lock().get() == &image; });
    if (it == list.end()) {
        if (texture.empty()) return; // never changed: nothing to put back
        list.push_back({owner, sprite->mResourceLocation->mPath->value, ""});
        it = std::prev(list.end());
    }
    if (it->applied == texture) return;
    std::string const& path = texture.empty() ? it->vanilla : texture;
    sprite->setResourceLocation(ResourceLocation(Core::PathView(path)));
    it->applied = texture;
}

/// Applies the overlay textures to the HUD's bars when the upstream bars, their HUD indices or the HUD's
/// bar controls changed; otherwise a frame costs a version read and a pointer compare per bar.
void applyNotches(ScreenView& view) {
    auto&         s = C();
    std::uint64_t version;
    {
        std::scoped_lock lock(s.mutex);
        version = s.notchVersion;
    }
    auto& st   = notchState();
    auto  grid = st.grid.lock();
    if (grid) {
        auto const& children = grid->mChildren.get();
        bool        same     = version == st.version && children.size() == st.items.size();
        for (std::size_t i = 0; same && i < children.size(); ++i) same = children[i].get() == st.items[i];
        if (same) return; // nothing changed
    } else {
        // no change since the last pass, and no grid lost since (a rebuilt HUD tree: apply again)
        if (version == st.version && st.items.empty()) return;
        auto* tree = view.mVisualTree.get();
        if (!tree || !tree->mRootControl) return;
        UIControl* found = findControl(*tree->mRootControl, "boss_health_grid");
        if (found)
            if (auto parent = found->mParent.lock())
                for (auto const& child : parent->mChildren.get())
                    if (child.get() == found) grid = child;
        if (!grid) {
            st.items.clear(); // look again at the next change, not on every frame
            st.version = version;
            return;
        }
        st.grid = grid;
        st.images.clear(); // a new HUD tree: its images have their own textures
    }
    // (HUD index, notches, boss) of the notched upstream bars
    std::vector<std::tuple<int, int, std::int64_t>> shown;
    {
        std::scoped_lock lock(s.mutex);
        for (auto const& [index, match] : s.matches) {
            if (!match.boss) continue;
            auto it = s.bars.find(match.boss);
            if (it == s.bars.end() || it->second.overlay <= 0) continue;
            shown.emplace_back(index, kNotches[std::clamp(it->second.overlay, 0, 4)], match.boss);
        }
        version = s.notchVersion;
    }
    st.version = version;
    st.items.clear();
    for (auto const& item : grid->mChildren.get()) {
        st.items.push_back(item.get());
        if (!item) continue;
        int  index   = item->findCollectionIndex("boss_bars");
        auto w       = std::find_if(shown.begin(), shown.end(), [&](auto const& t) { return std::get<0>(t) == index; });
        int  notches = w != shown.end() ? std::get<1>(*w) : 0;
        UIControl* bar = findControl(*item, "progress_bar_for_collections");
        if (!bar) continue;
        for (auto const& child : bar->mChildren.get()) {
            if (!child) continue;
            auto const& name = child->mName.get();
            bool        background = name == "empty_progress_bar";
            if (!background && name != "filled_progress_bar_for_collections") continue;
            std::string texture;
            if (notches) texture = "textures/ui/entity/boss_bar_notched_" + std::to_string(notches) + (background ? "_background" : "_progress");
            setTexture(*child, child, texture);
        }
    }
    std::scoped_lock lock(s.mutex);
    for (auto const& [index, notches, boss] : shown)
        if (s.notchLogged.insert(boss).second)
            log().info("client boss bar {} notched {} (HUD bar {}: textures/ui/entity/boss_bar_notched_{}_*)", boss, notches, index, notches);
}
} // namespace

} // namespace gf::client::entity

// The HUD reads each bar's colour through the screen model: the upstream bosses' bars get their RGB.
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

// A dying upstream boss keeps its body for its whole death animation (the server holds its removal): the
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
        auto             op = m.value("op", std::string("add"));
        // the HUD's title matches are made again when the bars change: added, removed, renamed,
        // restyled; a progress update only when another bar has the same title (the progress
        // then tells them apart)
        if (op == "progress") {
            auto it = s.bars.find(id);
            if (it == s.bars.end()) return;
            it->second.progress = m.value("progress", it->second.progress);
            auto title          = plain(it->second.title);
            for (auto const& [other, b] : s.bars)
                if (other != id && plain(b.title) == title) {
                    ++s.generation;
                    break;
                }
            return;
        }
        ++s.generation;
        ++s.notchVersion;
        if (op == "remove") {
            s.bars.erase(id);
            s.logged.erase(id);
            s.notchLogged.erase(id);
            s.deathTimes.erase(id);
            return;
        }
        if (!s.bars.contains(id) && op == "add") log().info("client boss bar {} received (#{:06X})", id, m.value("rgb", 0u));
        auto& bar = s.bars[id];
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
    // framework's: adding render listeners while the client runs can deadlock the game); it looks
    // at the HUD only, and changes its bar textures only when the upstream bars changed
    void load() {
        auto& bus = ll::event::EventBus::getInstance();
        mListener = bus.emplaceListener<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
            if (ev.screenView().getScreenName() == "hud_screen") applyNotches(ev.screenView());
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

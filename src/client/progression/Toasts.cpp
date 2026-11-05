// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/LockedBiomeToast.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U14, client mod: the toasts of this unit, drawn as Java 1.21.1's ToastComponent draws them
// (F12's toast queue draws TF's layout only: icon at (6, 8), text at (25, 7) / (25, 18)):
// - AdvancementToast (progression.toast): the frame, the icon with its components (variants) at
//   (8, 8); a title that fits 125 pixels: the type's toast name ("Advancement Made!", "Goal
//   Reached!", "Challenge Complete!") at (30, 7) in -256 (yellow), -30465 (pink) for challenges,
//   and the title at (30, 18) in white; a longer title: the toast name at (30, 11) for 1500 ms,
//   then the title's lines centred in the toast, fading in over 300 ms (alpha from 4 to 252 of
//   255); the challenge sound when it first shows (GloamingForest's original fanfare,
//   core::progression::challengeFanfare, for Mojang's ui.toast.challenge_complete); hidden after
//   5000 ms.
// - LockedBiomeToast: the icon at (6, 8), misc.twilightforest.biome_locked at (25, 7) in -256 and
//   misc.twilightforest.biome_locked_2 at (25, 18) in white; hidden after 10000 ms.
// - ToastComponent / ToastInstance: 160 x 32 toasts at the top right, 5 slots (a toast keeps its
//   slot until it slid out; more wait in a queue), 600 ms slides (visibility f², 1 - f² while
//   hiding), the in / out sound when a slide starts (Bedrock's random.toast, as F12), nothing
//   drawn while the GUI is hidden (F1; the queue waits). The frame is the texture F12 draws every
//   toast on (toast::kFrameTexture). The slots F12's own queue holds (other units' toasts) are
//   left free: a new toast takes the first free slot after them.
// They are drawn by a HUD layer, and by the advancement screen over itself while it is open (Java
// draws the toasts over every screen).

#ifdef GF_CLIENT

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/progression/Client.h"
#include "client/ui/Ui.h"
#include "core/progression/Rules.h"
#include "feature/ext/ui/Client.h"
#include "mod/Log.h"

namespace gf::client::progression {

namespace {

namespace cp = core::progression;
using Clock  = std::chrono::steady_clock;

constexpr float kWidth   = 160;
constexpr float kHeight  = 32;
constexpr long  kSlideMs = 600;
constexpr int   kSlots   = 5;
constexpr char  kSound[] = "random.toast";

struct Toast {
    ToastInfo                  info;
    std::shared_ptr<ItemStack> icon;
};

struct Instance {
    Toast toast;
    int   index         = 0;
    long  animationTime = -1;
    long  visibleTime   = 0;
    bool  show          = true;
    bool  soundPlayed   = false; ///< AdvancementToast.playedSound
    long  fanfareStart  = -1;
    long  fanfareAt     = -1;    ///< ms of the fanfare already played
    bool  logged        = false;
    std::vector<std::string> lines; ///< the title split at 125 pixels (measured on the first frame)
    bool  measured      = false;
};

struct State {
    std::mutex           mutex;
    std::vector<Instance> visible;
    std::deque<Toast>    queued;
    bool                 occupied[kSlots]{};
    LocalPlayer const*   player = nullptr; ///< the local player the toasts were drawn for
    long                 screenDrew = -100000;
};
State& state() {
    static State s;
    return s;
}

long nowMs() {
    static auto const start = Clock::now();
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

void play(std::string const& sound, float volume, float pitch) {
    if (auto* c = currentClient()) {
        try {
            c->playUI(sound, volume, pitch);
        } catch (...) {}
    }
}

float visibility(Instance const& s, long now) {
    float f = std::clamp(static_cast<float>(now - s.animationTime) / static_cast<float>(kSlideMs), 0.0f, 1.0f);
    f *= f;
    return s.show ? f : 1 - f;
}

void background(screen::Canvas& c, float x, float y) {
    if (ui::blitNineSlice(c, toast::kFrameTexture, {x, y, kWidth, kHeight}, toast::kFrameSize, toast::kFrameSize, toast::kFrameSlice)) return;
    c.fill({x + 2, y, kWidth - 4, kHeight}, 0xFF000000);
    c.fill({x, y + 2, kWidth, kHeight - 4}, 0xFF000000);
    c.fill({x + 1, y + 1, kWidth - 2, kHeight - 2}, 0xFF555555);
    c.fill({x + 3, y + 3, kWidth - 6, kHeight - 6}, 0xFF212121);
}

/// AdvancementToast#render; returns whether the toast still shows.
bool renderAdvancement(screen::Canvas& c, float x, float y, Instance& s, long t, long now) {
    auto const& info = s.toast.info;
    background(c, x, y);
    if (!s.measured) {
        s.measured = true;
        s.lines    = splitLines(c, info.body, 125);
        if (s.lines.empty()) s.lines.push_back({});
    }
    if (s.lines.size() == 1) {
        c.text(x + 30, y + 7, info.title, info.titleColor, screen::Align::Left, false);
        c.text(x + 30, y + 18, s.lines[0], 0xFFFFFFFF, screen::Align::Left, false);
    } else if (t < 1500) {
        // Java ors the fade-out alpha (clamp((1500 - t) / 300) * 255 << 24 | 0x04000000) into -256 /
        // -30465, whose alpha is already 0xFF: the toast name stays opaque until it is replaced
        c.text(x + 30, y + 11, info.title, info.titleColor, screen::Align::Left, false);
    } else {
        unsigned alpha = static_cast<unsigned>(std::floor(std::clamp(static_cast<float>(t - 1500) / 300.0f, 0.0f, 1.0f) * 252.0f)) << 24 | 0x04000000u;
        int      ly    = static_cast<int>(kHeight) / 2 - static_cast<int>(s.lines.size()) * 9 / 2;
        for (auto const& line : s.lines) {
            c.text(x + 30, y + static_cast<float>(ly), line, 0x00FFFFFFu | alpha, screen::Align::Left, false);
            ly += 9;
        }
    }
    if (!s.soundPlayed && t > 0) {
        s.soundPlayed = true;
        if (info.frame == "challenge") {
            s.fanfareStart = now;
            log().info("client toast: challenge fanfare for {}", info.what);
        }
    }
    if (s.fanfareStart >= 0) {
        long at = now - s.fanfareStart;
        for (auto const& n : cp::fanfareBetween(s.fanfareAt, at)) play(n.sound, n.volume, cp::notePitch(n.note));
        s.fanfareAt = at;
        if (at > cp::challengeFanfare().back().ms) s.fanfareStart = -1;
    }
    if (s.toast.icon) c.item(*s.toast.icon, x + 8, y + 8);
    return t < info.ms;
}

/// LockedBiomeToast#render.
bool renderLocked(screen::Canvas& c, float x, float y, Instance& s, long t) {
    auto const& info = s.toast.info;
    background(c, x, y);
    if (s.toast.icon) c.item(*s.toast.icon, x + 6, y + 8);
    c.text(x + 25, y + 7, info.title, info.titleColor, screen::Align::Left, false);
    c.text(x + 25, y + 18, info.body, 0xFFFFFFFF, screen::Align::Left, false);
    return t < info.ms;
}

/// ToastInstance#render; true when the toast has slid out.
bool renderOne(screen::Canvas& c, float guiWidth, Instance& s, long now) {
    if (s.animationTime == -1) {
        s.animationTime = now;
        play(kSound, 1.0f, 1.0f);
    }
    if (s.show && now - s.animationTime <= kSlideMs) s.visibleTime = now;
    float x    = guiWidth - kWidth * visibility(s, now);
    float y    = static_cast<float>(s.index) * kHeight;
    long  t    = now - s.visibleTime;
    bool  show = s.toast.info.kind == "locked_biome" ? renderLocked(c, x, y, s, t) : renderAdvancement(c, x, y, s, t, now);
    if (!s.logged) {
        s.logged = true;
        log().info("client toast: {} drawn in slot {}", s.toast.info.what, s.index);
    }
    if (show != s.show) {
        s.animationTime = now - static_cast<long>((1.0f - visibility(s, now)) * static_cast<float>(kSlideMs));
        s.show          = show;
        play(kSound, 1.0f, show ? 1.0f : 0.8f);
    }
    return !s.show && now - s.animationTime > kSlideMs;
}

/// A new world (another local player): the toasts of the last one are gone, as F12's queue does on
/// leaving a level. A missing player (between worlds, a transient frame) changes nothing. Lock held.
void adoptPlayer(State& st) {
    auto const* player = currentPlayer();
    if (!player || player == st.player) return;
    if (st.player) {
        st.visible.clear();
        st.queued.clear();
        std::fill(std::begin(st.occupied), std::end(st.occupied), false);
    }
    st.player = player;
}

void renderAll(screen::Canvas& c) {
    auto& st = state();
    std::lock_guard lock(st.mutex);
    adoptPlayer(st);
    if (hud::guiHidden()) return; // ToastComponent#render does nothing while options.hideGui
    float      guiWidth = screen::guiSize().first;
    long const now      = nowMs();
    for (auto it = st.visible.begin(); it != st.visible.end();) {
        if (renderOne(c, guiWidth, *it, now)) {
            st.occupied[it->index] = false;
            it                     = st.visible.erase(it);
        } else {
            ++it;
        }
    }
    // the slots F12's queue fills first (its toasts take the lowest free slots) stay free
    int reserved = static_cast<int>(std::min<std::size_t>(ui::toastCount(), kSlots));
    while (!st.queued.empty()) {
        int free = -1;
        for (int i = reserved; i < kSlots && free < 0; ++i)
            if (!st.occupied[i]) free = i;
        if (free < 0) break;
        Instance s;
        s.toast = std::move(st.queued.front());
        s.index = free;
        st.queued.pop_front();
        st.occupied[free] = true;
        st.visible.push_back(std::move(s));
    }
}

} // namespace

void queueToast(ToastInfo info, nlohmann::json const& iconItem) {
    Toast t;
    t.info = std::move(info);
    if (iconItem.is_object()) t.icon = screen::iconOf(iconItem);
    if (!t.icon && !t.info.icon.empty()) t.icon = screen::iconOf(nlohmann::json{{"id", t.info.icon}, {"count", 1}, {"aux", 0}});
    auto& st = state();
    std::lock_guard lock(st.mutex);
    adoptPlayer(st); // queued in a new world: the old world's toasts go first, not this one
    st.queued.push_back(std::move(t));
}

void drawToasts(screen::Canvas& c, bool fromScreen) {
    long now = nowMs();
    {
        auto& st = state();
        std::lock_guard lock(st.mutex);
        if (fromScreen) st.screenDrew = now;
        else if (now - st.screenDrew < 100) return; // the advancement screen draws them over itself
    }
    float ox = c.originX(), oy = c.originY();
    c.setOrigin(0, 0);
    renderAll(c);
    c.setOrigin(ox, oy);
}

std::optional<ToastInfo> toastOf(std::string const& what) {
    auto& st = state();
    std::lock_guard lock(st.mutex);
    for (auto const& s : st.visible)
        if (s.toast.info.what == what) return s.toast.info;
    for (auto const& q : st.queued)
        if (q.info.what == what) return q.info;
    return std::nullopt;
}

std::size_t cancelToasts(std::string const& what) {
    auto& st = state();
    std::lock_guard lock(st.mutex);
    std::size_t n = std::erase_if(st.queued, [&](Toast const& t) { return t.info.what == what; });
    for (auto it = st.visible.begin(); it != st.visible.end();) {
        if (it->toast.info.what != what) {
            ++it;
            continue;
        }
        st.occupied[it->index] = false;
        it                     = st.visible.erase(it);
        ++n;
    }
    return n;
}

void registerToastLayer() {
    hud::addLayer("progression.toasts", 1000000, [](screen::Canvas& c, float) { drawToasts(c, false); });
}

} // namespace gf::client::progression

#endif

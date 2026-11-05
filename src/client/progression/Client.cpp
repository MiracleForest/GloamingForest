// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/event/LockedBiomeToastHandler.java
//   src/main/java/twilightforest/client/LockedBiomeToast.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U14, client mod:
// - LockedBiomeToastHandler: on every client tick where the local player's tickCount % 5 == 0,
//   progression is enforced, the player is neither creative nor spectator and the toasts are not
//   disabled, the countdown (core::progression::LockedToastCountdown, static as in TF: shared by
//   every world of the process) steps; when it shows, LockedBiomeToast (the restriction's icon,
//   misc.twilightforest.biome_locked in yellow over misc.twilightforest.biome_locked_2, 10 s).
// - The advancement toast of Java's AdvancementToast (progression.toast): "Advancement Made!" /
//   "Goal Reached!" / "Challenge Complete!" in -256 (yellow), -30465 (pink) for challenges, over
//   the advancement's title in white, with its icon (and its variant), for 5 s.
// Both are drawn by this unit's toast component at Java's offsets (Toasts.cpp).
// - The advancements key (L, as Java's key.advancements) asks the server for the screen.

#ifdef GF_CLIENT

#include <mutex>
#include <optional>

#include "mc/deps/core/string/HashedString.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"

#include "client/Client.h"
#include "client/input/Input.h"
#include "client/progression/Client.h"
#include "client/ui/Ui.h"
#include "feature/ext/ui/Client.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/progression_content/Config.h"
#include "feature/progression_content/Content.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::client::progression {

namespace pc = gf::feature::progression_content;
namespace cp = gf::core::progression;

namespace {

std::mutex                      gMutex;
cp::LockedToastCountdown        gCountdown; ///< LockedBiomeToastHandler's static fields
std::optional<ShownToast>       gLastToast;
int                             gLockedToasts = 0;
std::string                     gBiomeOverride;

std::string localBiome(LocalPlayer& p) {
    auto        pos = p.getFeetPos();
    BlockPos    bp{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y + 0.001f)), static_cast<int>(std::floor(pos.z))};
    auto&       region = p.getDimensionBlockSource();
    if (!region.hasChunksAt(bp, 0, false)) return {};
    std::string name = region.getBiome(bp).mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

void showLockedToast(std::string const& icon) {
    ToastInfo info;
    info.what       = "locked_biome";
    info.kind       = "locked_biome";
    info.title      = ctr("misc.twilightforest.biome_locked", "Biome Locked!");
    info.body       = ctr("misc.twilightforest.biome_locked_2", "Check your advancements");
    info.icon       = icon;
    info.titleColor = 0xFFFFFF00; // LockedBiomeToast: -256
    info.ms         = 10000;
    queueToast(info);
    {
        std::lock_guard lock(gMutex);
        gLastToast = ShownToast{"locked_biome", "misc.twilightforest.biome_locked", "misc.twilightforest.biome_locked_2", icon, info.ms};
        ++gLockedToasts;
    }
    log().info("client toast: locked biome ({})", icon);
}

} // namespace

void tickLockedToast(LocalPlayer& player, std::string const& biomeOverride) {
    int tickCount = player.mTickCount;
    if (tickCount % 5 != 0) return;
    if (!gf::progression::client::enforced() || player.isCreative() || player.isSpectator()) return;
    if (config::get<pc::ProgressionConfig>().disableLockedBiomeToasts) return;
    auto        biome = biomeOverride.empty() ? localBiome(player) : biomeOverride;
    std::string icon;
    if (!biome.empty() && gf::progression::client::biomeLocked(biome)) {
        auto const& all = pc::restrictions();
        if (auto it = all.find(biome); it != all.end()) icon = it->second.toastIcon;
    }
    bool show;
    {
        std::lock_guard lock(gMutex);
        show = gCountdown.step(!icon.empty());
    }
    if (show) showLockedToast(icon);
}

std::optional<ShownToast> lastToast() {
    std::lock_guard lock(gMutex);
    return gLastToast;
}

std::pair<int, bool> countdown() {
    std::lock_guard lock(gMutex);
    return {gCountdown.timeUntilToast, gCountdown.shownToast};
}

void resetCountdown() {
    std::lock_guard lock(gMutex);
    gCountdown = {};
}

void setClientBiomeOverride(std::string const& biome) {
    std::lock_guard lock(gMutex);
    gBiomeOverride = biome;
}

int lockedToastCount() {
    std::lock_guard lock(gMutex);
    return gLockedToasts;
}

void showAdvancementToast(nlohmann::json const& msg) {
    auto frame = msg.value("frame", std::string{"task"});
    // AdvancementToast: the type's toast name in -256 (yellow), -30465 (pink) for challenges, the
    // title white
    ToastInfo info;
    info.what  = msg.value("advancement", std::string{});
    info.kind  = "advancement";
    info.frame = frame;
    info.icon  = msg.value("icon", std::string{});
    info.ms    = 5000;
    // the advancement's title component (its key and arguments) in the game's language
    auto titleKey = msg.value("title", std::string{});
    if (auto c = msg.find("titleComponent"); c != msg.end() && !c->is_null()) info.body = clientText(*c);
    if (info.body.empty()) info.body = ctr(titleKey, titleKey);
    info.title = frame == "goal" ? ctr("advancements.toast.goal", "Goal Reached!")
               : frame == "challenge" ? ctr("advancements.toast.challenge", "Challenge Complete!")
                                      : ctr("advancements.toast.task", "Advancement Made!");
    info.titleColor = frame == "challenge" ? 0xFFFF88FFu : 0xFFFFFF00u;
    queueToast(info, msg.value("iconItem", nlohmann::json()));
    {
        std::lock_guard lock(gMutex);
        gLastToast = ShownToast{info.what, info.title, titleKey, info.icon, info.ms};
    }
    log().info("client toast: advancement {} ({})", msg.value("advancement", std::string{}), frame);
}

void registerAdvancementScreen(); // AdvancementScreen.cpp
void watchMouseRelease();         // AdvancementScreen.cpp

namespace {

class ProgressionClientModule {
public:
    // input listeners are added at mod load, before the game's threads run (as the screen framework)
    void load() { watchMouseRelease(); }
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        net::client::on("progression.toast", [](nlohmann::json const& msg) { showAdvancementToast(msg); });
        movement::onLocalTick(0, [](movement::MoveState&) {
            std::string biome;
            {
                std::lock_guard lock(gMutex);
                biome = gBiomeOverride;
            }
            if (auto* p = currentPlayer()) tickLockedToast(*p, biome);
        });
        // Java's key.advancements (L)
        keys::registerKey("progression.advancements", 'L', "key.categories.misc", [] {
            if (net::client::connected()) net::client::send("progression.open", nlohmann::json::object());
        });
        registerAdvancementScreen();
        registerToastLayer();
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::progression

GF_REGISTER_MODULE(gf::client::progression::ProgressionClientModule);

#endif

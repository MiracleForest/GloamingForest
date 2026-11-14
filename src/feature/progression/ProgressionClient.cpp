// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/MissingAdvancementToast.java
//   src/main/java/gloamingforest/util/PlayerHelper.java
//   src/main/java/gloamingforest/util/Restriction.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client view of progression (client build): the local player's completed advancements as the
// server synced them ("combat.progress"), the gloamingEnforcedProgression gamerule ("gamerules.sync")
// and the biome restrictions (<mod>/data/<ns>/gloaming/restrictions/<biome>.json, as upstream's
// Restriction registry). The missing advancement toast ("combat.missing_advancement") is upstream's
// MissingAdvancementToast through F12's toasts (feature/ext/ui/Client.h): UPPER_TEXT
// "Advancement Required:" (0xffffffff) over the advancement's title (0xffffff: white too, Java's
// drawString makes an alpha of 0 opaque) and its icon, in the advancement toast frame, for 10 s.

#ifdef GF_CLIENT

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/ext/ui/Client.h"
#include "feature/gamerules/GameRulesInternal.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/progression/ProgressionInternal.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::progression::client {

namespace {

struct ClientState {
    std::mutex                                                      mutex;
    std::set<std::string>                                           done;
    bool                                                            restrictionsLoaded = false;
    std::unordered_map<std::string, std::vector<std::string>>       restrictions; ///< biome -> advancements
};

ClientState& cstate() {
    static ClientState s;
    return s;
}

std::mutex                       gToastMutex;
std::optional<ShownMissingToast> gLastToast;

void loadRestrictions(ClientState& s) {
    s.restrictionsLoaded = true;
    std::filesystem::path data;
    try {
        data = Mod::getInstance().getSelf().getModDir() / "data";
    } catch (...) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(data, ec)) return;
    for (auto const& ns : std::filesystem::directory_iterator(data, ec)) {
        auto root = ns.path() / "gloaming" / "restrictions";
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (auto const& f : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (f.path().extension() != ".json") continue;
            try {
                std::ifstream in(f.path());
                auto          j  = nlohmann::json::parse(in, nullptr, true, true);
                auto          id = ns.path().filename().string() + ":"
                        + std::filesystem::relative(f.path(), root, ec).replace_extension().generic_string();
                std::vector<std::string> advs;
                for (auto const& a : j.value("advancements", nlohmann::json::array()))
                    if (a.is_string()) advs.push_back(a.get<std::string>());
                s.restrictions[id] = std::move(advs);
            } catch (...) {}
        }
    }
}

} // namespace

std::optional<ShownMissingToast> lastMissingToast() {
    std::lock_guard lock(gToastMutex);
    return gLastToast;
}

bool has(std::string const& advancementId) {
    std::lock_guard lock(cstate().mutex);
    return cstate().done.contains(advancementId);
}

bool enforced() {
    auto v = gamerules::client::value("gloamingEnforcedProgression");
    return v.is_boolean() ? v.get<bool>() : true; // upstream's default
}

bool biomeLocked(std::string const& biomeId) {
    if (!enforced()) return false;
    std::lock_guard lock(cstate().mutex);
    auto&           s = cstate();
    if (!s.restrictionsLoaded) loadRestrictions(s);
    auto it = s.restrictions.find(biomeId);
    if (it == s.restrictions.end() || it->second.empty()) return false;
    // upstream's PlayerHelper#playerHasRequiredAdvancements answers with the first listed advancement
    return !s.done.contains(it->second.front());
}

namespace {

class ProgressionClientModule {
public:
    void install() {
        net::client::on("combat.progress", [](nlohmann::json const& msg) {
            std::lock_guard lock(cstate().mutex);
            cstate().done.clear();
            for (auto const& a : msg.value("done", nlohmann::json::array()))
                if (a.is_string()) cstate().done.insert(a.get<std::string>());
        });
        net::client::on("combat.missing_advancement", [](nlohmann::json const& msg) {
            namespace toast = gf::client::toast;
            toast::ToastSpec spec;
            spec.iconItem = msg.value("icon", std::string{kMissingIcon});
            if (spec.iconItem.empty()) spec.iconItem = kMissingIcon;
            spec.titleKey = kMissingUpperText;
            // the advancement's title: a translation key (translated on this client) or plain text
            spec.bodyKey = msg.value("title", std::string{kMissingNoTitle});
            spec.ms      = kMissingToastMs;
            toast::show(spec, toast::styleOf(toast::Kind::MissingAdvancement));
            auto advancement = msg.value("advancement", std::string{});
            {
                std::lock_guard lock(gToastMutex);
                gLastToast = ShownMissingToast{advancement, spec.titleKey, spec.bodyKey, spec.iconItem, spec.ms};
            }
            log().info("client toast: missing advancement {} ({}, icon {})", advancement, spec.bodyKey, spec.iconItem);
        });
        net::client::onConnected([] {
            std::lock_guard lock(cstate().mutex);
            cstate().done.clear();
            cstate().restrictionsLoaded = false;
            cstate().restrictions.clear();
        });
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::progression::client

GF_REGISTER_MODULE_ORDERED(gf::progression::client::ProgressionClientModule, gf::ModuleOrder::Contracts);

#endif

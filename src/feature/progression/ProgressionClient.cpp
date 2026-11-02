// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/MissingAdvancementToast.java
//   src/main/java/twilightforest/util/PlayerHelper.java
//   src/main/java/twilightforest/util/Restriction.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client view of progression (client build): the local player's completed advancements as the
// server synced them ("combat.progress"), the tfEnforcedProgression gamerule ("gamerules.sync")
// and the biome restrictions (<mod>/data/<ns>/twilight/restrictions/<biome>.json, as TF's
// Restriction registry). The missing advancement toast ("combat.missing_advancement") is TF's
// MissingAdvancementToast: "Advancement Required!" over the advancement's title, for 10 s.

#ifdef GF_CLIENT

#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <unordered_map>

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/gamerules/GameRulesInternal.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
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
        auto root = ns.path() / "twilight" / "restrictions";
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

bool has(std::string const& advancementId) {
    std::lock_guard lock(cstate().mutex);
    return cstate().done.contains(advancementId);
}

bool enforced() {
    auto v = gamerules::client::value("tfEnforcedProgression");
    return v.is_boolean() ? v.get<bool>() : true; // TF's default
}

bool biomeLocked(std::string const& biomeId) {
    if (!enforced()) return false;
    std::lock_guard lock(cstate().mutex);
    auto&           s = cstate();
    if (!s.restrictionsLoaded) loadRestrictions(s);
    auto it = s.restrictions.find(biomeId);
    if (it == s.restrictions.end() || it->second.empty()) return false;
    // TF's PlayerHelper#playerHasRequiredAdvancements answers with the first listed advancement
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
            gf::client::toast::ToastSpec spec;
            spec.iconItem = msg.value("icon", std::string{"twilightforest:twilight_portal_miniature_structure"});
            spec.titleKey = "misc.twilightforest.advancement_required";
            spec.bodyKey  = msg.value("title", std::string{"twilightforest.ui.advancement.no_title"});
            spec.ms       = 10000; // TF: 10 seconds
            gf::client::toast::show(spec);
            log().info("client toast: missing advancement {}", msg.value("advancement", std::string{}));
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

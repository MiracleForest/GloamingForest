// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/util/Restriction.java
//   src/main/java/twilightforest/util/Enforcement.java
//   src/main/java/twilightforest/init/custom/Enforcements.java
//   src/main/java/twilightforest/events/ProgressionEvents.java
//   src/main/java/twilightforest/client/event/LockedBiomeToastHandler.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).

#include "core/progression/Rules.h"

#include <algorithm>
#include <map>

namespace gf::core::progression {

namespace {
std::string ns(std::string id) {
    if (!id.empty() && id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}
} // namespace

Enforcement enforcementOf(std::string const& id) {
    auto n = ns(id);
    if (n == "twilightforest:darkness") return Enforcement::Darkness;
    if (n == "twilightforest:hunger") return Enforcement::Hunger;
    if (n == "twilightforest:fire") return Enforcement::Fire;
    if (n == "twilightforest:frost") return Enforcement::Frost;
    if (n == "twilightforest:acid_rain") return Enforcement::AcidRain;
    return Enforcement::Unknown;
}

char const* enforcementName(Enforcement e) {
    switch (e) {
    case Enforcement::Darkness: return "darkness";
    case Enforcement::Hunger: return "hunger";
    case Enforcement::Fire: return "fire";
    case Enforcement::Frost: return "frost";
    case Enforcement::AcidRain: return "acid_rain";
    default: return "unknown";
    }
}

std::optional<Restriction> Restriction::fromJson(std::string const& biome, nlohmann::json const& j) {
    if (!j.is_object()) return std::nullopt;
    Restriction r;
    r.biome         = ns(biome);
    r.enforcementId = ns(j.value("enforcement", std::string{}));
    r.enforcement   = enforcementOf(r.enforcementId);
    if (r.enforcementId.empty()) return std::nullopt;
    if (auto m = j.find("multiplier"); m != j.end() && m->is_number()) r.multiplier = m->get<float>();
    else return std::nullopt; // fieldOf("multiplier"): required
    if (auto s = j.find("structure_key"); s != j.end() && s->is_string()) r.structureKey = ns(s->get<std::string>());
    if (auto t = j.find("locked_biome_toast"); t != j.end()) {
        if (t->is_string()) r.toastIcon = ns(t->get<std::string>());
        else if (t->is_object()) r.toastIcon = ns(t->value("id", t->value("item", std::string{})));
    }
    for (auto const& a : j.value("advancements", nlohmann::json::array()))
        if (a.is_string()) r.advancements.push_back(ns(a.get<std::string>()));
    if (r.advancements.empty()) return std::nullopt; // ExtraCodecs.nonEmptyList
    return r;
}

bool Restriction::liftedFor(std::function<bool(std::string const&)> const& has) const {
    return std::all_of(advancements.begin(), advancements.end(), [&](std::string const& a) { return has(a); });
}

int innerPeriod(Enforcement e) { return e == Enforcement::AcidRain ? 5 : 60; }

bool innerGate(Enforcement e, int tickCount) { return tickCount % innerPeriod(e) == 0; }

bool enforcementDue(Enforcement e, int tickCount) {
    return e != Enforcement::Unknown && outerGate(tickCount) && innerGate(e, tickCount);
}

int hungerAmplifier(int currentAmplifier, float multiplier) {
    int m = static_cast<int>(multiplier);
    return currentAmplifier >= 0 ? currentAmplifier + m : m;
}

bool LockedToastCountdown::step(bool lockedWithIcon) {
    if (lockedWithIcon) {
        timeUntilToast--;
        if (!shownToast && timeUntilToast <= 0) {
            shownToast = true;
            return true;
        }
        return false;
    }
    if (shownToast) {
        timeUntilToast = 60;
        shownToast     = false;
    }
    return false;
}

void layoutTree(std::vector<TreeNode>& nodes) {
    std::map<std::string, std::size_t>              index;
    std::map<std::string, std::vector<std::size_t>> children;
    for (std::size_t i = 0; i < nodes.size(); ++i) index[nodes[i].id] = i;
    std::vector<std::size_t> roots;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto const& p = nodes[i].parent;
        if (p.empty() || !index.contains(p)) roots.push_back(i);
        else children[p].push_back(i);
    }
    float nextRow = 0;
    std::function<void(std::size_t, int)> place = [&](std::size_t i, int depth) {
        auto& n = nodes[i];
        n.x     = static_cast<float>(depth);
        auto it = children.find(n.id);
        if (it == children.end() || it->second.empty()) {
            n.y = nextRow;
            nextRow += 1;
            return;
        }
        for (auto c : it->second) place(c, depth + 1);
        n.y = (nodes[it->second.front()].y + nodes[it->second.back()].y) / 2.0f;
    };
    for (auto r : roots) place(r, 0);
}

} // namespace gf::core::progression

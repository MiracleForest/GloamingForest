// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFGameRules.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The gamerule store of "rules+fx" (F10). Bedrock's /gamerule only knows vanilla rules, so TF's
// rules are per-world values in the world DB ("gf:gamerules:values") with
// `/twilightforest gamerule [<rule> [<value>]]`. TF's three rules are known from the start with
// TF's defaults (tfEnforcedProgression true, playersTfPortalDefaultDelay 60,
// playersTfPortalCreativeDelay 1); their owners (U14, F4) register them again at install.
// Companions get every value over "gamerules.sync" on join and on each change (TF sends
// EnforceProgressionStatusPacket to every player when tfEnforcedProgression changes).

#include "feature/gamerules/GameRules.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/gamerules/GameRulesInternal.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::gamerules {

namespace {

constexpr char kValuesKey[] = "gf:gamerules:values";

struct Rule {
    bool  isBool = true;
    Value def    = false;
};

struct Store {
    std::recursive_mutex                       mutex;
    std::map<std::string, Rule>                rules;
    std::map<std::string, Value>               values; ///< the current world's explicit values
    bool                                       loaded = false;
    std::vector<std::function<void(std::string const&, Value const&)>> listeners;
};

Store& store() {
    static Store s;
    return s;
}

/// TF's rules (TFGameRules) with TF's defaults, known before their owners register them.
struct Defaults {
    Defaults() {
        auto& s                                  = store();
        s.rules["tfEnforcedProgression"]         = Rule{true, true};
        s.rules["playersTfPortalDefaultDelay"]   = Rule{false, 60};
        s.rules["playersTfPortalCreativeDelay"]  = Rule{false, 1};
    }
} const gDefaults;

void loadValues() {
    auto& s = store();
    s.values.clear();
    s.loaded = true;
    auto doc = game::db::get(kValuesKey);
    if (!doc || !doc->is_object()) return;
    for (auto const& [id, v] : doc->items()) {
        if (v.is_boolean()) s.values[id] = v.get<bool>();
        else if (v.is_number_integer()) s.values[id] = v.get<int>();
    }
}

void saveValues() {
    auto&          s = store();
    nlohmann::json doc = nlohmann::json::object();
    for (auto const& [id, v] : s.values) {
        if (std::holds_alternative<bool>(v)) doc[id] = std::get<bool>(v);
        else doc[id] = std::get<int>(v);
    }
    game::db::put(kValuesKey, doc);
}

void ensureLoaded() {
    if (!store().loaded && game::db::available()) loadValues();
}

nlohmann::json toJson(Value const& v) {
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v);
    return std::get<int>(v);
}

std::string toText(Value const& v) {
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
    return std::to_string(std::get<int>(v));
}

void registerRule(std::string const& id, Rule rule) {
    std::lock_guard lock(store().mutex);
    store().rules[id] = std::move(rule);
}

} // namespace

nlohmann::json snapshot() {
    std::lock_guard lock(store().mutex);
    ensureLoaded();
    nlohmann::json out = nlohmann::json::object();
    for (auto const& [id, rule] : store().rules) out[id] = toJson(get(id));
    return out;
}

void onChanged(std::function<void(std::string const&, Value const&)> fn) {
    std::lock_guard lock(store().mutex);
    store().listeners.push_back(std::move(fn));
}

void registerBool(std::string const& id, bool defaultValue) { registerRule(id, Rule{true, defaultValue}); }
void registerInt(std::string const& id, int defaultValue) { registerRule(id, Rule{false, defaultValue}); }

Value get(std::string const& id) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    ensureLoaded();
    auto r = s.rules.find(id);
    if (r == s.rules.end()) return false;
    auto v = s.values.find(id);
    if (v != s.values.end() && std::holds_alternative<bool>(v->second) == r->second.isBool) return v->second;
    return r->second.def;
}

bool getBool(std::string const& id) {
    auto v = get(id);
    return std::holds_alternative<bool>(v) ? std::get<bool>(v) : std::get<int>(v) != 0;
}

int getInt(std::string const& id) {
    auto v = get(id);
    return std::holds_alternative<int>(v) ? std::get<int>(v) : (std::get<bool>(v) ? 1 : 0);
}

bool set(std::string const& id, Value value) {
    std::vector<std::function<void(std::string const&, Value const&)>> listeners;
    {
        auto&           s = store();
        std::lock_guard lock(s.mutex);
        auto            r = s.rules.find(id);
        if (r == s.rules.end() || std::holds_alternative<bool>(value) != r->second.isBool) return false;
        ensureLoaded();
        s.values[id] = value;
        if (game::db::available()) saveValues();
        listeners = s.listeners;
    }
    log().info("gamerule {} = {}", id, toText(value));
    for (auto const& fn : listeners) {
        try {
            fn(id, value);
        } catch (std::exception const& ex) {
            log().error("gamerules: change listener failed: {}", ex.what());
        }
    }
    // every companion gets the new values (TF: EnforceProgressionStatusPacket to all players)
    if (auto level = ll::service::getLevel()) {
        auto doc = snapshot();
        level->forEachPlayer([&doc](Player& p) {
            if (net::isCompanion(p)) net::send(p, "gamerules.sync", {{"rules", doc}});
            return true;
        });
    }
    return true;
}

void unregisterRule(std::string const& id) {
    auto&           s = store();
    std::lock_guard lock(s.mutex);
    s.rules.erase(id);
    ensureLoaded();
    if (s.values.erase(id) && game::db::available()) saveValues();
}

std::vector<std::string> ids() {
    std::lock_guard          lock(store().mutex);
    std::vector<std::string> out;
    for (auto const& [id, _] : store().rules) out.push_back(id);
    return out;
}

/// Parses a command value for rule `id` ("true"/"false" or an integer); nullopt when invalid.
std::optional<Value> parse(std::string const& id, std::string const& text) {
    std::lock_guard lock(store().mutex);
    auto            r = store().rules.find(id);
    if (r == store().rules.end()) return std::nullopt;
    if (r->second.isBool) {
        if (text == "true") return Value{true};
        if (text == "false") return Value{false};
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        int         n    = std::stoi(text, &used);
        if (used != text.size()) return std::nullopt;
        return Value{n};
    } catch (...) {
        return std::nullopt;
    }
}

#ifdef GF_CLIENT
namespace client {
namespace {
std::mutex&     clientMutex() {
    static std::mutex m;
    return m;
}
nlohmann::json& clientRules() {
    static nlohmann::json j = nlohmann::json::object();
    return j;
}
} // namespace

nlohmann::json value(std::string const& id) {
    std::lock_guard lock(clientMutex());
    auto const&     rules = clientRules();
    return rules.contains(id) ? rules[id] : nlohmann::json{};
}

void apply(nlohmann::json const& rules) {
    std::lock_guard lock(clientMutex());
    if (rules.is_object()) clientRules() = rules;
}

void reset() {
    std::lock_guard lock(clientMutex());
    clientRules() = nlohmann::json::object();
}
} // namespace client
#endif

// ---- /twilightforest gamerule --------------------------------------------------------------------

struct GameruleShowParams {
    std::string rule;
};
struct GameruleSetBoolParams {
    std::string rule;
    bool        value = false;
};
struct GameruleSetIntParams {
    std::string rule;
    int         value = 0;
};

namespace {

void list(CommandOutput& output) {
    std::string text;
    for (auto const& id : ids()) text += (text.empty() ? "" : ", ") + id + " = " + toText(get(id));
    output.success("TF gamerules: " + text);
}

class GameRulesModule {
public:
    void install() {
        game::db::onWorldReady([] {
            std::lock_guard lock(store().mutex);
            loadValues();
        });
        game::db::onWorldClosed([] {
            std::lock_guard lock(store().mutex);
            store().values.clear();
            store().loaded = false;
        });
        net::onCompanionJoined([](Player& p) { net::send(p, "gamerules.sync", {{"rules", snapshot()}}); });
#ifdef GF_CLIENT
        net::client::on("gamerules.sync", [](nlohmann::json const& msg) { client::apply(msg.value("rules", nlohmann::json::object())); });
#endif
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::gamerules

GF_REGISTER_MODULE_ORDERED(gf::gamerules::GameRulesModule, gf::ModuleOrder::Contracts);

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace gf::gamerules;
    cmd.overload().text("gamerule").execute([](CommandOrigin const&, CommandOutput& output) { list(output); });
    cmd.overload<GameruleShowParams>().text("gamerule").required("rule").execute(
        [](CommandOrigin const&, CommandOutput& output, GameruleShowParams const& p) {
            auto all = ids();
            if (std::find(all.begin(), all.end(), p.rule) == all.end()) return output.error("Unknown TF gamerule: " + p.rule);
            output.success(p.rule + " = " + toText(get(p.rule)));
        }
    );
    // Bedrock's parser tells booleans from integers, so one overload each (as /gamerule)
    auto apply = [](CommandOrigin const& origin, CommandOutput& output, std::string const& rule, Value v) {
        if (!gf::commands::isAdmin(origin)) return output.error("You need operator permission.");
        if (!set(rule, v)) return output.error("Unknown TF gamerule or a value of the wrong type: " + rule + " " + toText(v));
        output.success("Gamerule " + rule + " is now set to: " + toText(v));
    };
    cmd.overload<GameruleSetBoolParams>().text("gamerule").required("rule").required("value").execute(
        [apply](CommandOrigin const& origin, CommandOutput& output, GameruleSetBoolParams const& p) { apply(origin, output, p.rule, p.value); }
    );
    cmd.overload<GameruleSetIntParams>().text("gamerule").required("rule").required("value").execute(
        [apply](CommandOrigin const& origin, CommandOutput& output, GameruleSetIntParams const& p) { apply(origin, output, p.rule, p.value); }
    );
});

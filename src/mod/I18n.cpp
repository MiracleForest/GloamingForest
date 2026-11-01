// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/I18n.cpp), LGPL-3.0-or-later.
#include "mod/I18n.h"

#include <cctype>
#include <fstream>
#include <map>
#include <unordered_map>

#include "nlohmann/json.hpp"

#include "mc/world/actor/player/Player.h"

#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/ModConfig.h"

namespace gf {

namespace {
using Table = std::unordered_map<std::string, std::string>;
std::map<std::string, Table, std::less<>> gTables;

/// "en-us" / "en_us" / "EN_US" -> "en_US" (Bedrock's spelling).
std::string normalize(std::string_view locale) {
    std::string s(locale);
    bool        region = false;
    for (auto& c : s) {
        if (c == '-') c = '_';
        if (c == '_') {
            region = true;
            continue;
        }
        c = region ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                   : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

Table const* find(std::string_view locale) {
    if (auto it = gTables.find(locale); it != gTables.end()) return &it->second;
    return nullptr;
}

Table::const_iterator lookup(Table const& t, std::string_view key) { return t.find(std::string(key)); }
} // namespace

void loadTranslations(std::filesystem::path const& dir) {
    gTables.clear();
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        auto          j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object()) {
            log().warn("lang file {} is not a JSON object", entry.path().string());
            continue;
        }
        auto& table = gTables[normalize(entry.path().stem().string())];
        for (auto& [k, v] : j.items()) {
            if (v.is_string()) table[k] = v.get<std::string>();
        }
    }
    log().debug("loaded {} languages", gTables.size());
}

std::string_view translate(std::string_view key, std::string_view fallback, std::string_view locale) {
    std::string const candidates[] = {normalize(locale), normalize(config::get<ModConfig>().language), "en_US"};
    for (auto const& c : candidates) {
        if (c.empty()) continue;
        if (auto* t = find(c)) {
            if (auto it = lookup(*t, key); it != t->end()) return it->second;
        }
    }
    return fallback;
}

bool hasTranslation(std::string_view key, std::string_view locale) {
    for (auto const& c : {normalize(locale), std::string("en_US")}) {
        if (c.empty()) continue;
        if (auto* t = find(c); t && lookup(*t, key) != t->end()) return true;
    }
    return false;
}

std::string localeOf(Player const& player) {
    auto const& cfg = config::get<ModConfig>();
    if (!cfg.usePlayerLocale || player.isSimulated()) return cfg.language;
    auto code = normalize(player.getLocaleCode());
    if (code.empty() || !find(code)) return cfg.language;
    return code;
}

} // namespace gf

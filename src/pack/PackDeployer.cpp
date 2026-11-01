// Adapted from LiteLDev/Levitate v0.1.0 (src/pack/PackDeployer.cpp), LGPL-3.0-or-later.
//
// Makes the mod's packs load everywhere.
//
// - Behavior packs: LeviLamina adds every mod's `behavior_packs/*` to the behavior stack of every
//   world (BDS and client local worlds). Nothing to do.
// - Resource packs, client: LeviLamina adds every mod's `resource_packs/*` to the client's global
//   resource stack. Nothing to do.
// - Resource packs, BDS: LeviLamina registers the packs in the repository but doesn't put them on
//   the level stack that is sent to joining players. At load (before the world opens) we add
//   them to `worlds/<level-name>/world_resource_packs.json`, and (optionally) mark them required,
//   so vanilla clients get TF's textures, models and names too.
// - Names, BDS: the server fills in item and block names itself (command feedback, forms) from
//   its own localization, which has only the vanilla texts. At install we append the RP's merged
//   `texts/<locale>.lang` to the server's current language, so those names resolve instead of
//   showing `item.twilightforest:<id>.name`.
//
// The packs are GloamingForest_RP / GloamingForest_BP and, in development builds, the gf_test
// fixture packs GloamingForest_Test_RP / _BP (assets/test).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/service/Bedrock.h"

#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelData.h"

#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/ModConfig.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace gf::pack {

namespace fs = std::filesystem;

namespace {
/// The mod's resource packs (directories under resource_packs/ with a manifest).
[[maybe_unused]] std::vector<fs::path> resourcePacks() {
    std::vector<fs::path> out;
    std::error_code       ec;
    auto                  dir = Mod::getInstance().getSelf().getModDir() / "resource_packs";
    for (auto const& e : fs::directory_iterator(dir, ec)) {
        if (e.is_directory() && fs::exists(e.path() / "manifest.json")) out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

#ifdef GF_SERVER
namespace {
std::optional<nlohmann::json> readJson(fs::path const& p) {
    std::ifstream in(p);
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in, nullptr, false, true);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

std::string levelName() {
    std::ifstream in("server.properties");
    std::string   line;
    while (std::getline(in, line)) {
        if (line.starts_with("level-name=")) {
            auto v = line.substr(11);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            return v;
        }
    }
    return "Bedrock level";
}

std::optional<std::string> readText(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    return text;
}

/// `key=value` lines of a .lang file (comments and blank lines skipped) into `out`, replacing keys.
void readLang(std::string const& text, std::map<std::string, std::string>& out) {
    std::size_t start = 0;
    while (start < text.size()) {
        auto end  = text.find('\n', start);
        auto line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start     = end == std::string::npos ? text.size() : end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        auto eq = line.find('=');
        if (line.empty() || line.starts_with("#") || eq == std::string::npos || eq == 0) continue;
        auto value = line.substr(eq + 1);
        if (auto tab = value.find("\t#"); tab != std::string::npos) value.erase(tab); // trailing comment
        out[line.substr(0, eq)] = value;
    }
}

/// Appends the RPs' names to the server's current language, once: that language's own file (if
/// we ship one) over en_US, so untranslated keys still get the English name.
void appendNamesToServerLanguage(std::vector<fs::path> const& rpDirs) {
    auto                               current = getI18n().getCurrentLanguage();
    auto                               code    = current->getFullLanguageCode();
    std::map<std::string, std::string> names;
    int                                files = 0;
    for (auto const& rpDir : rpDirs) {
        bool own = false;
        for (auto const& locale : {std::string("en_US"), code}) {
            if (own && locale == "en_US") continue; // code is en_US itself
            if (auto text = readText(rpDir / "texts" / (locale + ".lang"))) {
                readLang(*text, names);
                ++files;
                own = locale == "en_US";
            }
        }
    }
    if (names.empty()) return;
    std::string merged;
    for (auto const& [key, value] : names) merged += key + "=" + value + "\n";
    // The API hands out the shared localization as const. BDS enables mods right after "Server
    // started", before any player can have joined, on the server thread: the same moment the game
    // appends pack languages itself. Appending again (a reload) writes the same values.
    const_cast<Localization&>(*current).appendTranslations(merged, {}, {}, "");
    log().info("added {} resource pack names to the server language {} ({} files)", names.size(), code, files);
}
} // namespace
#endif

class PackDeployer {
public:
    void load() {
#ifdef GF_SERVER
        auto            name  = levelName();
        auto            world = fs::path("worlds") / fs::path(std::u8string(name.begin(), name.end()));
        std::error_code ec;
        fs::create_directories(world, ec);
        auto file    = world / "world_resource_packs.json";
        auto packs   = readJson(file).value_or(nlohmann::json::array());
        bool changed = false;
        if (!packs.is_array()) packs = nlohmann::json::array();
        for (auto const& rpDir : resourcePacks()) {
            auto manifest = readJson(rpDir / "manifest.json");
            if (!manifest) {
                log().warn("resource pack manifest not readable in {}", rpDir.string());
                continue;
            }
            auto const& header  = (*manifest)["header"];
            auto        uuid    = header.value("uuid", std::string{});
            auto        version = header.value("version", nlohmann::json::array({0, 0, 1}));
            bool        found   = false;
            for (auto& p : packs) {
                if (p.value("pack_id", std::string{}) != uuid) continue;
                found = true;
                if (p["version"] != version) {
                    p["version"] = version;
                    changed      = true;
                }
            }
            if (!found) {
                packs.push_back({
                    {"pack_id", uuid   },
                    {"version", version},
                });
                changed = true;
            }
        }
        if (changed) {
            std::ofstream(file) << packs.dump(2);
            log().info("registered the resource packs in {}", file.string());
        }
#endif
    }

    void install() {
#ifdef GF_SERVER
        appendNamesToServerLanguage(resourcePacks());
        if (!config::get<ModConfig>().requireResourcePack) return;
        if (auto level = ll::service::getLevel()) level->getLevelData().mTexturePacksRequired = true;
#endif
    }
    void uninstall() {}
};

} // namespace gf::pack

GF_REGISTER_MODULE_ORDERED(gf::pack::PackDeployer, gf::ModuleOrder::Packs);

// The server resolves our item / block names (BDS: from the RP texts appended at install; client:
// from the RP on the global stack), as command feedback and forms show them.
GF_REGISTER_SELFTEST("core.pack_names", [](gf::selftest::Context& ctx) {
    namespace items = gf::game::items;
    for (auto const& [id, name] : std::initializer_list<std::pair<char const*, char const*>>{
             {"gf_test:fixture_item", "Fixture Item"},
         }) {
        auto stack = items::make(id);
        ctx.require(stack.has_value(), std::string("make ") + id);
        auto got = stack->getName();
        // the English name, or a translation (a server / game in another language), never the key
        bool raw = got.empty() || got.starts_with("item.") || got.starts_with("tile.") || got.ends_with(".name");
        ctx.check(got == name || !raw, std::string(id) + " is named '" + got + "', expected '" + name + "' (or its translation)");
    }
});

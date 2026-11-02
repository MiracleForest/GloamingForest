// The mod's data tree for the F9 runtimes (see DataFiles.h).
#include "feature/item/DataFiles.h"

#include <algorithm>
#include <fstream>

#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::feature::item::data {

namespace {
std::filesystem::path& rootOverride() {
    static std::filesystem::path p;
    return p;
}
std::vector<std::function<void()>>& listeners() {
    static std::vector<std::function<void()>> v;
    return v;
}
} // namespace

std::filesystem::path root() {
    if (!rootOverride().empty()) return rootOverride();
    return Mod::getInstance().getSelf().getModDir() / "data";
}

void setRoot(std::filesystem::path dir) { rootOverride() = std::move(dir); }

std::vector<File> list(std::string_view dir) {
    namespace fs = std::filesystem;
    std::vector<File> out;
    std::error_code   ec;
    auto              base = root();
    for (auto const& nsEntry : fs::directory_iterator(base, ec)) {
        if (!nsEntry.is_directory()) continue;
        auto ns  = nsEntry.path().filename().string();
        auto sub = nsEntry.path() / fs::path(std::string(dir));
        if (!fs::is_directory(sub, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(sub, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file() || it->path().extension() != ".json") continue;
            auto rel = fs::relative(it->path(), sub, ec).replace_extension().generic_string();
            out.push_back({ns, rel, it->path()});
        }
    }
    std::sort(out.begin(), out.end(), [](File const& a, File const& b) { return std::tie(a.ns, a.rel) < std::tie(b.ns, b.rel); });
    return out;
}

nlohmann::json read(std::filesystem::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        log().warn("data: cannot read {}", file.string());
        return nullptr;
    }
    auto j = nlohmann::json::parse(in, nullptr, false, true);
    if (j.is_discarded()) {
        log().warn("data: {} is not valid JSON", file.string());
        return nullptr;
    }
    return j;
}

void onReload(std::function<void()> fn) { listeners().push_back(std::move(fn)); }

void reloadAll() {
    for (auto const& fn : listeners()) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("data: reload failed: {}", e.what());
        }
    }
}

std::string withNamespace(std::string_view id) {
    if (id.find(':') != std::string_view::npos) return std::string(id);
    return "minecraft:" + std::string(id);
}

namespace {
class DataModule {
public:
    void install() {
        reloadAll();
        static bool registered = false;
        if (!registered) {
            registered = true;
            config::onReload([] { reloadAll(); });
        }
    }
    void uninstall() {}
};
} // namespace

} // namespace gf::feature::item::data

// before the other contracts (interactions, worldgen, ... read tags lazily, but their install may already ask)
GF_REGISTER_MODULE_ORDERED(gf::feature::item::data::DataModule, gf::ModuleOrder::Services - 10);

// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Shared by the golden tests of the Java oracle (unit F13): where the repository and tests/golden are,
// and file / JSON reading.
#pragma once

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace gf::tests::golden {

/// An environment variable as a path (empty when unset or empty).
inline std::filesystem::path envPath(char const* name) {
#ifdef _WIN32
    char*       value = nullptr;
    std::size_t len   = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) return {};
    std::filesystem::path out(value);
    std::free(value);
    return out;
#else
    char const* value = std::getenv(name);
    return value ? std::filesystem::path(value) : std::filesystem::path();
#endif
}

/// The repository root: $GF_REPO_DIR, or found walking up from this file's directory / the working
/// directory (xmake runs the tests from the build directory) to tests/golden/manifest.json. Empty: not
/// found.
inline std::filesystem::path repoDir() {
    namespace fs = std::filesystem;
    static fs::path const dir = [] {
        std::error_code ec;
        auto isRoot = [&](fs::path const& p) { return fs::exists(p / "tests/golden/manifest.json", ec); };
        if (auto env = envPath("GF_REPO_DIR"); !env.empty() && isRoot(env)) return env;
        fs::path const here = fs::path(__FILE__).parent_path(); // <repo>/tests/golden, maybe relative
        for (fs::path start : {here, fs::current_path(ec)}) {
            for (fs::path p = start; !p.empty(); p = p.parent_path()) {
                if (isRoot(p)) return p;
                // a relative __FILE__ is relative to the directory the compiler ran in
                if (!here.empty() && !here.is_absolute() && isRoot(p / here / "../..")) return fs::weakly_canonical(p / here / "../..", ec);
                if (p == p.root_path()) break;
            }
        }
        return fs::path();
    }();
    return dir;
}

/// tests/golden: $GF_GOLDEN_DIR, else the repository's (empty when neither is found).
inline std::filesystem::path goldenDir() {
    static std::filesystem::path const dir = [] {
        if (auto env = envPath("GF_GOLDEN_DIR"); !env.empty()) return env;
        auto repo = repoDir();
        return repo.empty() ? std::filesystem::path() : repo / "tests/golden";
    }();
    return dir;
}

/// A file's bytes ("" when it cannot be read).
inline std::string readFile(std::filesystem::path const& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// A JSON file (discarded when missing or malformed: is_discarded()).
inline nlohmann::json readJson(std::filesystem::path const& p) { return nlohmann::json::parse(readFile(p), nullptr, false); }

} // namespace gf::tests::golden

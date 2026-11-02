#pragma once

// Shared by the landmark gtests (F6): the repository root (the goldens of tests/golden, the
// structure records of assets/data/landmarks) and JSON file reading.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"

namespace gf::tests::landmark {

/// An environment variable as a path ("" when unset).
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

/// The repository root: $GF_REPO_DIR, or found walking up from this file / the working directory
/// (xmake runs the tests from the build directory) to tests/golden/manifest.json. Empty: not found.
inline std::filesystem::path repoDir() {
    namespace fs = std::filesystem;
    static fs::path const dir = [] {
        std::error_code ec;
        auto isRoot = [&](fs::path const& p) { return fs::exists(p / "tests/golden/manifest.json", ec); };
        if (auto env = envPath("GF_REPO_DIR"); !env.empty() && isRoot(env)) return env;
        for (fs::path start : {fs::path(__FILE__).parent_path(), fs::current_path(ec)}) {
            for (fs::path p = start; !p.empty(); p = p.parent_path()) {
                if (isRoot(p)) return p;
                if (p == p.root_path()) break;
            }
        }
        return fs::path();
    }();
    return dir;
}

/// A JSON file (a discarded value when it cannot be read or parsed).
inline nlohmann::json readJson(std::filesystem::path const& p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return nlohmann::json::parse(ss.str(), nullptr, false);
}

} // namespace gf::tests::landmark

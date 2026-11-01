// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Config.h), LGPL-3.0-or-later.
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "ll/api/Config.h"

#include "mod/Registry.h"

namespace gf::config {

/// Per-section JSON configs under `<mod dir>/config/<section>.json` (rule R13: one section per
/// unit, named after it; every TF config key is mapped to exactly one section in
/// docs/spec/config.md).
///
/// Declare a reflectable aggregate with an integral `version` member and register it once:
///
///     struct UncraftingConfig { int version = 1; bool disableUncraftingOnly = false; };
///     GF_REGISTER_CONFIG("uncraftmaps", UncraftingConfig);           // in a .cpp file
///
///     bool off = gf::config::get<UncraftingConfig>().disableUncraftingOnly;   // anywhere
///
/// Files are created with defaults on first load, merged with new defaults when `version`
/// changes, edited in game with `/twilightforest config [section]` and reloaded by
/// `/twilightforest reload`. Configs are global (not per world). Keep fields plain reflectable
/// values (bool, numbers, strings, nested structs / maps) so the config screen can edit them.
template <class T>
T& mut() {
    static T value{};
    return value;
}

template <class T>
T const& get() {
    return mut<T>();
}

struct ConfigEntry {
    std::string                                       section;
    std::function<bool(std::filesystem::path const&)> load; ///< path of the .json file
    std::function<bool(std::filesystem::path const&)> save;
    /// The current values / the defaults of the section as JSON (config screen).
    std::function<nlohmann::ordered_json()> values;
    std::function<nlohmann::ordered_json()> defaults;
    /// Writes the section's file with `patch` merged into the current values (RFC 7386 merge
    /// patch; values of the wrong type are refused). Apply it with `loadAll()`.
    std::function<bool(nlohmann::ordered_json const& patch, std::filesystem::path const&)> write;
};
using ConfigRegistry = StaticRegistry<ConfigEntry>;

/// Directory holding the config files (valid after mod load).
[[nodiscard]] std::filesystem::path const& configDir();
/// Loads every registered section (creates missing files). Returns false if any failed.
bool loadAll();
/// Writes every registered section.
bool saveAll();
/// Callbacks run after `loadAll()` (e.g. to apply a changed interval).
void onReload(std::function<void()> fn);
/// The registered section names, in registration order.
[[nodiscard]] std::vector<std::string> sections();

namespace detail {
void setConfigDir(std::filesystem::path dir);

template <ll::config::IsConfig T>
struct ConfigRegistrar {
    explicit ConfigRegistrar(std::string section) {
        ConfigRegistry::add(ConfigEntry{
            std::move(section),
            [](std::filesystem::path const& p) {
                T    fresh{};
                bool ok = ll::config::loadConfig(fresh, p); // false = rewritten (missing / version change)
                if (!ok) ll::config::saveConfig(fresh, p);
                mut<T>() = std::move(fresh);
                return true;
            },
            [](std::filesystem::path const& p) { return ll::config::saveConfig(mut<T>(), p); },
            [] { return ll::reflection::serialize<nlohmann::ordered_json>(mut<T>()).value(); },
            [] { return ll::reflection::serialize<nlohmann::ordered_json>(T{}).value(); },
            [](nlohmann::ordered_json const& patch, std::filesystem::path const& p) {
                auto data = ll::reflection::serialize<nlohmann::ordered_json>(mut<T>()).value();
                data.merge_patch(patch);
                T copy = mut<T>();
                if (!ll::reflection::deserialize(copy, data)) return false;
                return ll::config::saveConfig(copy, p);
            },
        });
    }
};
} // namespace detail

} // namespace gf::config

/// Registers config struct `T` under `<configDir>/<section>.json`. `section` is a string literal
/// (the unit's name, rule R13).
#define GF_REGISTER_CONFIG(section, T)                                                                                 \
    static ::gf::config::detail::ConfigRegistrar<T> GF_UNIQUE(gf_config_registrar_)(section)

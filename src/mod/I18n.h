// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/I18n.h), LGPL-3.0-or-later.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/Format.h"

class Player;

namespace gf {

/// Loads `<dir>/<locale>.json` files (flat {"key": "text"} maps merged at build time from
/// `assets/lang/<locale>/*.json` and the imported upstream lang files, for the 29 Bedrock locales).
void loadTranslations(std::filesystem::path const& dir);

/// Looks up `key` in `locale` (empty = configured default), then en_US, then returns `fallback`.
[[nodiscard]] std::string_view translate(std::string_view key, std::string_view fallback, std::string_view locale = {});
/// Whether `key` exists in `locale` (or en_US).
[[nodiscard]] bool hasTranslation(std::string_view key, std::string_view locale = {});

/// The locale to use for `player` ("en_US" style), honouring `ModConfig::usePlayerLocale`.
[[nodiscard]] std::string localeOf(Player const& player);

/// Translates and formats with `{}` / `{0}` / `%s` / `%1$s` placeholders, in the default locale.
/// A translation key belongs to the unit that owns the feature (rule R9); other units pass the
/// English text as the fallback without defining the key.
///     gf::tr("gloamingforest.book.lich", "The tower is sealed by {}", name)
template <class... Args>
[[nodiscard]] std::string tr(std::string_view key, std::string_view fallback, Args const&... args) {
    return core::formatMessageArgs(translate(key, fallback), args...);
}

/// Like `tr`, in the player's locale.
template <class... Args>
[[nodiscard]] std::string trFor(Player const& player, std::string_view key, std::string_view fallback, Args const&... args) {
    return core::formatMessageArgs(translate(key, fallback, localeOf(player)), args...);
}

} // namespace gf

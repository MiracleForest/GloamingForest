// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/config/GloamingCommonConfig.java
//   src/main/java/gloamingforest/config/GloamingClientConfig.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Config section "artifacts" (docs/spec/config.md): common.casketUUIDLocking (server) and the four
// client keys, which the client mod reads from its own copy of the section (Java's client config
// lives on the client too).
#include <optional>

#include "feature/artifacts/Artifacts.h"
#include "mod/Config.h"

namespace gf::feature::artifacts {

namespace {
std::optional<ArtifactsConfig>& overridden() {
    static std::optional<ArtifactsConfig> o;
    return o;
}
} // namespace

ArtifactsConfig const& config() {
    if (auto const& o = overridden()) return *o;
    return gf::config::get<ArtifactsConfig>();
}

void overrideConfig(ArtifactsConfig const& values) { overridden() = values; }

void restoreConfig() { overridden().reset(); }

} // namespace gf::feature::artifacts

GF_REGISTER_CONFIG("artifacts", gf::feature::artifacts::ArtifactsConfig);

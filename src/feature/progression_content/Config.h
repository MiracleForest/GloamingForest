#pragma once

// U14's config section "progression" (docs/spec/config.md).

namespace gf::feature::progression_content {

struct ProgressionConfig {
    int version = 1;
    /// TFClientConfig "disableLockedBiomeToasts" (config.twilightforest.locked_toasts, default
    /// false): no toast when entering a locked biome.
    bool disableLockedBiomeToasts = false;
};

} // namespace gf::feature::progression_content

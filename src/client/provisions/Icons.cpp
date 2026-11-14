// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/ClientRegistrationEvents.java (Experiment115Item.THINK / FULL)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U10 (provisions), client mod: Experiment 115's item model overrides. upstream gives the item an
// EXPERIMENT_115_VARIANTS component only for advancement icons (TFAdvancementGenerator): "think"
// shows item/think115, "full" the block model experiment115_8_8_regenerating. The component travels
// as item data "gloamingforest:e115_variant" (U14's advancement icons); "think" swaps the slot icon
// through U11's icon layers (item atlas key tf_provisions_think115). "full" keeps the item icon: a
// 3D block model has no item atlas sprite (docs/units/provisions.md).
#ifdef GF_CLIENT

#include <optional>
#include <string>

#include "mc/world/item/ItemStack.h"

#include "feature/ext/tgear/IconHooks.h"
#include "game/Items.h"
#include "mod/Module.h"

namespace gf::client::provisions {

namespace {

namespace icons = ::gf::ext::tgear::client;

std::optional<icons::IconLayers> e115Icon(ItemStack const& stack) {
    auto v = game::items::getData(stack, "gloamingforest:e115_variant");
    if (v.is_string() && v.get<std::string>() == "think") return icons::IconLayers{.base = "tf_provisions_think115"};
    return std::nullopt;
}

} // namespace

class ProvisionsIconsModule {
public:
    void install() {
        static bool const once = [] {
            icons::setIconLayers("gloamingforest:experiment_115", e115Icon);
            return true;
        }();
        (void)once;
    }
    void uninstall() {}
};

} // namespace gf::client::provisions

GF_REGISTER_MODULE(gf::client::provisions::ProvisionsIconsModule);

#endif // GF_CLIENT

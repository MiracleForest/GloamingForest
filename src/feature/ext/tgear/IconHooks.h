#pragma once

// U11 extension (rule R18: new files only; to be folded into F12's client render contract by A8 in W2b).
// Client build only.
//
// Item icon layers in GUI slots (Java's item model overrides and layered item models: upstream's
// TravellersGearItemModel and the red thread's "size" property). Bedrock draws a slot's icon from the
// item atlas through ItemRenderer::renderGuiItemInChunk (the inventory, the hotbar and every
// container screen); this hook asks the provider of the item's id for its layers: the base icon is
// replaced by an item atlas entry (item_texture.json key), and every overlay entry is drawn over it in
// the same chunk batch (same position, scale and light). F12's Gui item renderer
// (InventoryItemRenderer::render) does not fire for these slots on 1.26.51, so the layers go through
// the icon path itself; A8 folds this into client.render (an icon-layer registry).
//
//     gf::ext::tgear::client::setIconLayers("gloamingforest:red_thread", [](ItemStack const& s) {
//         return IconLayers{.base = "gf_tgear_red_thread_bundle_2"};
//     });

#ifdef GF_CLIENT

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class ItemStack;

namespace gf::ext::tgear::client {

struct IconLayers {
    std::string              base;     ///< item atlas key of the base icon (empty: the item's own icon)
    std::vector<std::string> overlays; ///< item atlas keys drawn over it, in order
};
using IconFn = std::function<std::optional<IconLayers>(ItemStack const& stack)>;

/// The icon layers of item `itemId` (nullopt from the provider: the item's own icon only).
void setIconLayers(std::string const& itemId, IconFn fn);

/// How many slot icons were drawn with layers (e2e evidence).
[[nodiscard]] std::uint64_t layeredIcons();

void installIconHooks();
void uninstallIconHooks();

} // namespace gf::ext::tgear::client

#endif // GF_CLIENT

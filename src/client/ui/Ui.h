#pragma once

// Contract "client.ui", client side (header owned by F0, implemented by F12; spike S5).
// Client build only. The server halves (vanilla fallbacks) are in feature/ui/Ui.h.
//
// Native screens on the client mod's screen framework (client/screen/base/Screen.h), HUD layers,
// toasts and tooltips. Every TF screen, overlay and toast is owned by a W1 unit and registered
// here; units never hook the UI renderer (rule R8).
//
//     gf::client::screen::registerScreen("uncraftmaps.uncrafting", [](Json const& data, auto send) { ... });
//     gf::client::hud::addLayer("artifacts.shields", 100, [](auto& canvas, float partial) { ... });
//     gf::client::toast::show({.iconItem = "twilightforest:magic_map", .titleKey = "...", .bodyKey = "..."});

#ifdef GF_CLIENT

#include <functional>
#include <string>
#include <vector>

#include "client/screen/base/Screen.h"

class ItemStack;

namespace gf::client::screen {

/// A container screen in TF's GUI style: the background texture `skin` (a resource pack path
/// such as "textures/gui/guigoblintinkering") drawn 1:1 at the panel, container slots at their
/// Java positions, and the player inventory below.
class ContainerScreen : public Screen {
public:
    ContainerScreen(std::string title, std::string skin, float width, float height);

    struct SlotPos {
        int   index = 0; ///< container slot index
        float x = 0, y = 0; ///< panel pixels (Java's Slot x / y)
    };
    std::string          skin;
    std::vector<SlotPos> slots;

    void renderExtra(Canvas& c, float mx, float my) override;
};

/// Builds the screen for a server-opened type (gf::ui on the server opens it for companions).
using Factory = ServerFactory;
void registerScreen(std::string const& type, Factory factory);

} // namespace gf::client::screen

namespace gf::client::hud {
/// Draws on top of the HUD (GUI pixels); lower `order` draws first.
using DrawFn = std::function<void(screen::Canvas& canvas, float partialTicks)>;
void addLayer(std::string const& id, int order, DrawFn fn);
} // namespace gf::client::hud

namespace gf::client::toast {
struct ToastSpec {
    std::string iconItem;  ///< item id shown as the icon
    std::string titleKey;  ///< translation keys (gf::client::ctr)
    std::string bodyKey;
    int         ms = 5000; ///< display time
};
void show(ToastSpec const& spec);
} // namespace gf::client::toast

namespace gf::client::tooltip {
/// Adds lines under an item's name in every tooltip (inventory, containers).
using TooltipFn = std::function<void(ItemStack const& stack, std::vector<std::string>& lines)>;
void add(std::string const& itemId, TooltipFn fn);
} // namespace gf::client::tooltip

#endif // GF_CLIENT

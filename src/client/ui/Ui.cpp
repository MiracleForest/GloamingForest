// Neutral stub of the client half of "client.ui" (F0). F12 replaces this file (rule R18).
#ifdef GF_CLIENT

#include "client/ui/Ui.h"

#include "core/Stub.h"

namespace gf::client::screen {

ContainerScreen::ContainerScreen(std::string title, std::string skin, float width, float height)
: Screen(std::move(title), width, height),
  skin(std::move(skin)) {}

void ContainerScreen::renderExtra(Canvas&, float, float) { GF_STUB("client.ui.ContainerScreen"); }

void registerScreen(std::string const& type, Factory factory) {
    GF_STUB("client.ui.registerScreen");
    registerServerScreen(type, std::move(factory)); // the base framework already opens it
}

} // namespace gf::client::screen

namespace gf::client::hud {
void addLayer(std::string const&, int, DrawFn) { GF_STUB("client.hud.addLayer"); }
} // namespace gf::client::hud

namespace gf::client::toast {
void show(ToastSpec const&) { GF_STUB("client.toast.show"); }
} // namespace gf::client::toast

namespace gf::client::tooltip {
void add(std::string const&, TooltipFn) { GF_STUB("client.tooltip.add"); }
} // namespace gf::client::tooltip

#endif // GF_CLIENT

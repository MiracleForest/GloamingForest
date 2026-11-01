// Neutral stub of the server half of "client.ui" (F0). F12 replaces this file (rule R18).
#include "feature/ui/Ui.h"

#include "core/Stub.h"

namespace gf::ui {

void form(Player&, std::string const&, std::string const&, std::vector<FormButton>, std::function<void(Player&)>) { GF_STUB("ui.form"); }

bool ddui(Player&, std::string const&, nlohmann::json const&, std::function<void(Player&, nlohmann::json const&)>) {
    GF_STUB("ui.ddui");
    return false;
}

std::string containerTitleSkin(std::string const& titleKey, std::string const&) {
    GF_STUB("ui.containerTitleSkin");
    return titleKey;
}

} // namespace gf::ui

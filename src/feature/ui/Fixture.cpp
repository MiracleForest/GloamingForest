// The F12 fixture (selftests and the e2e scenarios ui.ps1), server side.
//
// - Screen type "ui.fixture": the native version is a ContainerScreen with the fixture GUI skin
//   (client/ui/Fixture.cpp), vanilla players get a form (the form fallback of the contract).
// - Key "ui.fixture" (registered by the client fixture when the gf_test packs are deployed): the
//   server logs the press, which is what the client scenario waits for.
// - Channel "ui.fixture": turns the client fixture on / off (HUD layer, toast, world lines and
//   geometry, sky / fog / cloud providers): fixture::show().

#include "feature/ui/Fixture.h"

#include <atomic>

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/common/Screens.h"
#include "feature/ext/ui/Server.h"
#include "feature/net/Net.h"
#include "feature/ui/Ui.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::ui::fixture {

using Json = nlohmann::json;

namespace {
std::atomic<int> gClicks{0};
std::atomic<int> gKeys{0};
std::atomic<int> gForms{0};

Json screenData() {
    Json items = Json::object();
    if (auto s = game::items::make("minecraft:map")) items["0"] = game::items::toJson(*s);
    if (auto s = game::items::make("minecraft:compass")) items["4"] = game::items::toJson(*s);
    if (auto s = game::items::make("minecraft:oak_sapling", 3)) items["8"] = game::items::toJson(*s);
    return Json{{"clicks", gClicks.load()}, {"items", items}};
}
} // namespace

int clicks() { return gClicks; }
int keyPresses() { return gKeys; }
int formsShown() { return gForms; }

std::vector<Player*> realPlayers() {
    std::vector<Player*> out;
    auto                 level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer([&out](Player& p) {
        if (!p.isSimulated()) out.push_back(&p);
        return true;
    });
    return out;
}

bool show(Player& player, Json const& what) { return net::send(player, "ui.fixture", what); }

void openFormFallback(Player& player) {
    ++gForms;
    ui::form(
        player,
        trFor(player, "gf.ui.fixture.title", "GloamingForest UI fixture"),
        trFor(player, "gf.ui.fixture.form_body", "The form fallback of a TF screen for vanilla clients."),
        {
            {.text = trFor(player, "gf.ui.fixture.button_click", "Click"), .icon = "textures/items/map_filled", .onClick = [](Player&) { ++gClicks; }},
            {.text = trFor(player, "gf.ui.fixture.button_close", "Close"), .icon = "", .onClick = {}},
        }
    );
}

namespace {
class FixtureModule {
public:
    void install() {
        screens::registerType({
            .id   = "ui.fixture",
            .data = [](Player&, Json const&) -> std::optional<Json> { return screenData(); },
            .action =
                [](Player& p, Json const&, Json const& action) {
                    if (action.value("click", -1) >= 0) {
                        ++gClicks;
                        log().info("ui fixture: slot {} clicked by {}", action.value("click", -1), p.getRealName());
                    }
                },
            .form   = [](Player& p, Json const&) { openFormFallback(p); },
            .closed = [](Player& p, Json const&) { log().info("ui fixture: screen closed by {}", p.getRealName()); },
        });
        input::onKey("ui.fixture", [](Player& p, bool down) {
            if (!down) return;
            ++gKeys;
            log().info("ui fixture: key pressed by {}", p.getRealName());
        });
    }
    void uninstall() {}
};
} // namespace

} // namespace gf::ui::fixture

GF_REGISTER_MODULE(gf::ui::fixture::FixtureModule);

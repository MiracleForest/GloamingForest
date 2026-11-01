// The F12 fixture (selftests and the e2e scenarios ui.ps1), server side. Development builds only
// (fixture::enabled: the gf_test packs are deployed next to the mod).
//
// - Screen type "ui.fixture": the native version is a ContainerScreen with the fixture GUI skin
//   (client/ui/Fixture.cpp), vanilla players get a form (the form fallback of the contract).
// - Key "ui.fixture" (registered by the client fixture): the server logs the press, which is what
//   the client scenario waits for.
// - Channel "ui.fixture": turns the client fixture on / off (HUD layer, toast, world lines and
//   geometry, sky / fog / cloud providers, item renderers, map decorations): fixture::show().
// - Blocks for the client scenario: a named chest (the vanilla container skin), an item frame with
//   the fixture item (ItemContext::Fixed).

#include "feature/ui/Fixture.h"

#include <atomic>
#include <filesystem>

#include "ll/api/service/Bedrock.h"

#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ItemFrameBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/common/Screens.h"
#include "feature/ext/ui/Server.h"
#include "feature/net/Net.h"
#include "feature/ui/Ui.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
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

bool enabled() {
    static bool const on = [] {
        std::error_code ec;
        return std::filesystem::exists(gf::Mod::getInstance().getSelf().getModDir() / "behavior_packs" / "GloamingForest_Test_BP", ec);
    }();
    return on;
}

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

namespace {
/// The game's DefaultDataLoadHelper (no state, only its vtable): loads saved NBT as is.
DataLoadHelper& defaultLoadHelper() {
    static void** helper = DefaultDataLoadHelper::$vftable();
    return *reinterpret_cast<DataLoadHelper*>(&helper);
}
struct Placed {
    int         dim;
    BlockPos    pos;
    std::string id; ///< removed only while the block is still this one
};
std::vector<Placed>& placed() {
    static std::vector<Placed> v;
    return v;
}
void remember(BlockSource& region, BlockPos const& pos, std::string id) { placed().push_back({static_cast<int>(region.getDimensionId()), pos, std::move(id)}); }
} // namespace

bool placeNamedChest(BlockSource& region, BlockPos const& pos, std::string const& name) {
    auto const* chest = game::blockWith("minecraft:chest");
    if (!chest || !game::setBlock(region, pos, *chest)) return false;
    auto* actor = region.getBlockEntity(pos);
    if (!actor) return false;
    if (name.empty()) { // a plain chest: the vanilla look must stay
        remember(region, pos, "minecraft:chest");
        log().info("ui fixture: plain chest placed at {} {} {}", pos.x, pos.y, pos.z);
        return true;
    }
    auto        level = ll::service::getLevel();
    auto        ctx   = SaveContextFactory::createCloneSaveContext();
    CompoundTag tag;
    if (!level || !actor->save(tag, *ctx)) return false;
    tag["CustomName"] = name;
    actor->load(*level, tag, defaultLoadHelper());
    actor->refresh(region);
    remember(region, pos, "minecraft:chest");
    log().info("ui fixture: chest '{}' placed at {} {} {}", name, pos.x, pos.y, pos.z);
    return true;
}

std::string chestName(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor) return {};
    auto        ctx = SaveContextFactory::createCloneSaveContext();
    CompoundTag tag;
    if (!actor->save(tag, *ctx) || !tag.contains("CustomName", Tag::Type::String)) return {};
    return std::string(static_cast<std::string const&>(tag["CustomName"]));
}

namespace {
ItemFrameBlockActor* frameAt(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor || (actor->getType() != BlockActorType::ItemFrame && actor->getType() != BlockActorType::GlowItemFrame)) return nullptr;
    return static_cast<ItemFrameBlockActor*>(actor);
}
} // namespace

bool placeFrame(BlockSource& region, BlockPos const& pos, std::string const& itemId) {
    auto stack = game::items::make(itemId);
    return stack && placeFrame(region, pos, *stack);
}

bool placeFrame(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    auto const* support = game::blockWith("minecraft:stone");
    auto const* frame   = game::blockWith("minecraft:frame", {{"facing_direction", 2}}); // faces north: hangs on the block south of it
    if (!support || !frame || stack.isNull()) return false;
    std::string itemId = stack.getTypeName();
    BlockPos behind{pos.x, pos.y, pos.z + 1};
    if (!game::setBlock(region, behind, *support)) return false;
    remember(region, behind, "minecraft:stone");
    if (!game::setBlock(region, pos, *frame)) return false;
    remember(region, pos, "minecraft:frame");
    auto* actor = frameAt(region, pos);
    if (!actor) return false;
    actor->setItem(region, ItemInstance(stack), nullptr);
    actor->refresh(region);
    log().info("ui fixture: item frame with {} placed at {} {} {}", itemId, pos.x, pos.y, pos.z);
    return true;
}

std::string frameItem(BlockSource& region, BlockPos const& pos) {
    auto* actor = frameAt(region, pos);
    if (!actor) return {};
    ItemInstance const& item = actor->mItem;
    return item.isNull() ? std::string{} : item.getTypeName();
}

int clearPlaced() {
    int n = 0;
    // newest first: a frame goes before the block that holds it
    for (auto it = placed().rbegin(); it != placed().rend(); ++it) {
        auto* region = game::regionOf(it->dim);
        if (!region) continue;
        if (game::blockIdAt(*region, it->pos) == it->id && game::clearBlock(*region, it->pos)) ++n;
    }
    placed().clear();
    return n;
}

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

Json dduiDocument() {
    return Json{
        {"title", "GloamingForest UI fixture"},
        {"elements",
         Json::array({
             {{"type", "header"}, {"text", "DDUI fallback"}},
             {{"type", "label"}, {"text", "The DDUI fallback of a TF screen for vanilla clients."}},
             {{"type", "toggle"}, {"id", "toggle"}, {"text", "Toggle"}, {"value", true}},
             {{"type", "slider"}, {"id", "slider"}, {"text", "Slider"}, {"min", 0}, {"max", 10}, {"step", 1}, {"value", 3}},
             {{"type", "button"}, {"id", "ok"}, {"text", "OK"}},
         })},
    };
}

namespace {
class FixtureModule {
public:
    void install() {
        if (!enabled()) return; // production build: no fixture
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
        if (mKey) return;
        mKey = true; // gf::input has no removal: once per process
        input::onKey("ui.fixture", [](Player& p, bool down) {
            if (!down) return;
            ++gKeys;
            log().info("ui fixture: key pressed by {}", p.getRealName());
        });
    }
    void uninstall() {}

private:
    bool mKey = false;
};
} // namespace

} // namespace gf::ui::fixture

GF_REGISTER_MODULE(gf::ui::fixture::FixtureModule);

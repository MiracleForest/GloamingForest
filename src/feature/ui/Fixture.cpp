// The F12 fixture (selftests and the e2e scenarios ui.ps1), server side. Development builds only
// (fixture::enabled: the gf_test packs are deployed next to the mod).
//
// - Screen type "ui.fixture": the native version is a ContainerScreen with the fixture GUI skin
//   (client/ui/Fixture.cpp), vanilla players get a form (the form fallback of the contract).
// - Keys "ui.fixture" (J, registered at mod load) and "ui.fixture_late" (K, registered after the
//   input handler was built) of the client fixture: the server logs the presses, which is what the
//   client scenario waits for.
// - Channel "ui.fixture": turns the client fixture on / off (HUD layer, toast, world lines and
//   geometry, sky / fog / cloud providers, item renderers, map decorations, the native particle
//   factory): fixture::show().
// - Real players take part only when they carry the tag kPlayerTag (the client scenario adds it):
//   the stage in the ui selftest row holds the blocks of the client scenario (a named chest for the
//   vanilla container skin, an item frame with the fixture item for ItemContext::Fixed, a framed map),
//   and ui.fixture_clear puts the stage and the players back (position, held stack, helmet).

#include "feature/ui/Fixture.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Inventory.h"
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
#include "feature/movement/Movement.h"
#include "feature/net/Net.h"
#include "feature/ui/Ui.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::ui::fixture {

using Json      = nlohmann::json;
using ArmorSlot = SharedTypes::Legacy::ArmorSlot;

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

Player* playerById(std::int64_t id) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(ActorUniqueID{id}) : nullptr; // rule R16
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

std::vector<Player*> fixturePlayers() {
    std::vector<Player*> out;
    auto                 level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer([&out](Player& p) {
        if (!p.isSimulated() && p.hasTag(kPlayerTag)) out.push_back(&p);
        return true;
    });
    return out;
}

bool show(Player& player, Json const& what) { return net::send(player, "ui.fixture", what); }

// ---- players --------------------------------------------------------------------------------------

namespace {
struct SavedPlayer {
    int       dim;
    Vec3      feet;
    Vec2      rot;
    int       slot;
    ItemStack selected;
    ItemStack head;
};
std::unordered_map<std::int64_t, SavedPlayer>& savedPlayers() {
    static std::unordered_map<std::int64_t, SavedPlayer> m;
    return m;
}
} // namespace

void rememberPlayer(Player& p) {
    auto id = p.getOrCreateUniqueID().rawID;
    if (savedPlayers().contains(id)) return;
    int slot = p.getSelectedItemSlot();
    savedPlayers().emplace(
        id,
        SavedPlayer{static_cast<int>(p.getDimensionId()), p.getFeetPos(), p.getRotation(), slot, p.getInventory().getItem(slot), p.getArmor(ArmorSlot::Head)}
    );
}

int restorePlayers() {
    int n = 0;
    for (auto const& [id, s] : savedPlayers()) {
        auto* p = playerById(id);
        if (!p) continue;
        p->setArmor(ArmorSlot::Head, s.head);
        p->getInventory().setItem(s.slot, s.selected);
        p->sendInventory(false);
        p->teleport(s.feet, DimensionType(s.dim), s.rot);
        if (!p->isSimulated()) log().info("ui fixture: {} put back (position, held stack, helmet)", p->getRealName());
        ++n;
    }
    savedPlayers().clear();
    return n;
}

// ---- stage ----------------------------------------------------------------------------------------

namespace {
constexpr char kStageKey[] = "gf:ui:fixture_stage";

struct StageBlock {
    BlockPos    pos;
    std::string id;
    Json        states;
};
struct Stage {
    int                     dim = 0;
    std::vector<StageBlock> blocks;
    bool                    active = false;
};
Stage& stage() {
    static Stage s;
    return s;
}

/// A block's states as blockWithStates takes them (statesOf gives text).
Json statesJson(Block const& block) {
    Json out = Json::object();
    for (auto const& [k, v] : game::statesOf(block)) {
        if (v == "true" || v == "false") out[k] = v == "true";
        else if (!v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-') && v.find_first_not_of("-0123456789") == std::string::npos) out[k] = std::stoi(v);
        else out[k] = v;
    }
    return out;
}

Json stageJson(Stage const& s) {
    Json blocks = Json::array();
    for (auto const& b : s.blocks) blocks.push_back({b.pos.x, b.pos.y, b.pos.z, b.id, b.states});
    return {{"dim", s.dim}, {"blocks", blocks}};
}

std::optional<Stage> stageFromJson(Json const& j) {
    try {
        Stage s;
        s.dim = j.at("dim").get<int>();
        for (auto const& b : j.at("blocks")) s.blocks.push_back({{b.at(0).get<int>(), b.at(1).get<int>(), b.at(2).get<int>()}, b.at(3).get<std::string>(), b.at(4)});
        s.active = true;
        return s;
    } catch (...) {
        return std::nullopt;
    }
}

template <class F>
void forStageBox(BlockPos const& o, F&& f) {
    for (int x = o.x - 1; x <= o.x + 1; ++x)
        for (int y = o.y - 1; y <= o.y + 3; ++y)
            for (int z = o.z; z <= o.z + 4; ++z) f(BlockPos{x, y, z});
}
} // namespace

int clearStage() {
    auto& s = stage();
    if (!s.active) {
        // a session that ended before its clear (the client harness restarts the game) left it in the DB
        auto stored = game::db::get(kStageKey);
        if (!stored) return 0;
        auto restored = stageFromJson(*stored);
        if (!restored) {
            game::db::erase(kStageKey);
            return 0;
        }
        s = std::move(*restored);
    }
    int   n      = 0;
    auto* region = game::regionOf(s.dim);
    // the stage is put back whole or not at all: an unloaded part keeps the snapshot (memory and
    // world DB) for a later clear
    bool loaded = region != nullptr;
    for (auto const& b : s.blocks) loaded = loaded && region->hasChunksAt(b.pos, 0, false);
    if (!loaded) {
        log().warn("ui fixture: the stage is not loaded; it is put back by a later ui.fixture_clear");
        return 0;
    }
    for (auto const& b : s.blocks) {
        auto const* want = game::blockWithStates(b.id, b.states);
        if (!want) want = game::blockWith(b.id);
        if (!want || &region->getBlock(b.pos) == want) continue;
        if (game::setBlock(*region, b.pos, *want)) ++n;
    }
    s = Stage{};
    game::db::erase(kStageKey);
    return n;
}

bool prepareStage(BlockSource& region, BlockPos const& origin) {
    auto& s = stage();
    if (!s.active) {
        clearStage(); // a stage a previous session left (world DB)
        Stage fresh;
        fresh.dim    = static_cast<int>(region.getDimensionId());
        fresh.active = true;
        bool loaded  = true;
        forStageBox(origin, [&](BlockPos const& p) {
            if (!region.hasChunksAt(p, 0, false)) {
                loaded = false;
                return;
            }
            auto const& b = region.getBlock(p);
            fresh.blocks.push_back({p, game::blockId(b), statesJson(b)});
        });
        if (!loaded) return false;
        s = std::move(fresh);
        game::db::put(kStageKey, stageJson(s));
    }
    auto const* floor = game::blockWith("minecraft:grass_block");
    auto const* air   = game::blockWith("minecraft:air");
    if (!floor || !air) return false;
    forStageBox(origin, [&](BlockPos const& p) {
        auto const& want = p.y == origin.y - 1 ? *floor : *air;
        if (&region.getBlock(p) != &want) game::setBlock(region, p, want);
    });
    return true;
}

void standOnStage(Player& p, BlockSource const& region, BlockPos const& origin, float targetHeight) {
    rememberPlayer(p);
    // the eye is 1.62 above the feet; the target `targetHeight` above the floor and 2 blocks away
    float pitch = static_cast<float>(std::atan2(1.62 - targetHeight, 2.0) * 180.0 / 3.14159265358979);
    Vec3  feet{static_cast<float>(origin.x) + .5f, static_cast<float>(origin.y), static_cast<float>(origin.z) + .5f};
    p.teleport(feet, region.getDimensionId(), Vec2{pitch, 0.f}); // yaw 0: south
}

// ---- blocks ---------------------------------------------------------------------------------------

namespace {
/// The game's DefaultDataLoadHelper (no state, only its vtable): loads saved NBT as is.
DataLoadHelper& defaultLoadHelper() {
    static void** helper = DefaultDataLoadHelper::$vftable();
    return *reinterpret_cast<DataLoadHelper*>(&helper);
}
} // namespace

bool placeNamedChest(BlockSource& region, BlockPos const& pos, std::string const& name) {
    auto const* chest = game::blockWith("minecraft:chest");
    if (!chest || !game::setBlock(region, pos, *chest)) return false;
    auto* actor = region.getBlockEntity(pos);
    if (!actor) return false;
    if (name.empty()) { // a plain chest: the vanilla look must stay
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
    BlockPos    behind{pos.x, pos.y, pos.z + 1};
    if (!game::setBlock(region, behind, *support)) return false;
    if (!game::setBlock(region, pos, *frame)) return false;
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

// ---- forms ----------------------------------------------------------------------------------------

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

// ---- input edges ----------------------------------------------------------------------------------

namespace {
std::atomic<int> gEdgeLines{0};

char const* edgeName(movement::Edge e) {
    switch (e) {
    case movement::Edge::JumpPressed: return "JumpPressed";
    case movement::Edge::JumpReleased: return "JumpReleased";
    case movement::Edge::SneakPressed: return "SneakPressed";
    case movement::Edge::SneakReleased: return "SneakReleased";
    case movement::Edge::SprintStarted: return "SprintStarted";
    case movement::Edge::SprintStopped: return "SprintStopped";
    case movement::Edge::LeftGround: return "LeftGround";
    case movement::Edge::Landed: return "Landed";
    }
    return "?";
}
} // namespace

void logEdges(int count) {
    static bool registered = false; // gf::movement has no handler removal: once per process
    if (!registered) {
        registered = true;
        movement::onInputEdge([](Player& p, movement::Edge e) {
            if (gEdgeLines.load() <= 0 || p.isSimulated() || !p.hasTag(kPlayerTag)) return;
            --gEdgeLines;
            log().info("ui fixture: edge {} of {}", edgeName(e), p.getRealName());
        });
    }
    gEdgeLines = std::max(0, count);
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
        input::onKey("ui.fixture_late", [](Player& p, bool down) {
            if (down) log().info("ui fixture: late key pressed by {}", p.getRealName());
        });
    }
    void uninstall() {}

private:
    bool mKey = false;
};
} // namespace

} // namespace gf::ui::fixture

GF_REGISTER_MODULE(gf::ui::fixture::FixtureModule);

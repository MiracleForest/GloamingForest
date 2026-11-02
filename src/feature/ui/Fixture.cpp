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
//   and ui.fixture_clear puts the stage (item frames emptied first: nothing drops), the players
//   (position, every inventory slot a step changed, helmet) and the respawn step's gamerules back.
//   All three are kept in the world DB too (gf:ui:fixture_stage, gf:ui:fixture_players,
//   gf:ui:fixture_rules), so a session that ended before its clear is put back by the next one.

#include "feature/ui/Fixture.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <unordered_map>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/ActorType.h"
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
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

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
constexpr char kPlayersKey[] = "gf:ui:fixture_players";

/// What the fixture changed on a player, as it was before (kept in memory and in the world DB, so a
/// session that ends before ui.fixture_clear is put back by the next one).
struct SavedPlayer {
    int                      dim = 0;
    Vec3                     feet{};
    Vec2                     rot{};
    std::map<int, ItemStack> slots; ///< inventory slot -> its stack before the first step that changed it
    std::optional<ItemStack> head;
    bool                     simulated = false; ///< a test player: not kept across sessions
};
struct SavedPlayers {
    std::unordered_map<std::int64_t, SavedPlayer> byId;
    bool                                          loaded = false; ///< the world DB entry was read this world
};
SavedPlayers& saved() {
    static SavedPlayers s;
    return s;
}

Json vecJson(Vec3 const& v) { return Json::array({v.x, v.y, v.z}); }

Json savedJson() {
    Json out = Json::object();
    for (auto const& [id, s] : saved().byId) {
        if (s.simulated) continue; // gone with the session
        Json slots = Json::object();
        for (auto const& [slot, stack] : s.slots) slots[std::to_string(slot)] = game::items::toJson(stack);
        Json j = {{"dim", s.dim}, {"feet", vecJson(s.feet)}, {"rot", Json::array({s.rot.x, s.rot.y})}, {"slots", slots}};
        if (s.head) j["head"] = game::items::toJson(*s.head);
        out[std::to_string(id)] = std::move(j);
    }
    return out;
}

/// Reads the players a previous session left (world DB) once per world; entries in memory win.
void loadSaved() {
    auto& s = saved();
    if (s.loaded) return;
    s.loaded    = true;
    auto stored = game::db::get(kPlayersKey);
    if (!stored || !stored->is_object()) return;
    for (auto const& [key, j] : stored->items()) {
        try {
            auto id = std::stoll(key);
            if (s.byId.contains(id)) continue;
            SavedPlayer p;
            p.dim  = j.at("dim").get<int>();
            p.feet = Vec3{j.at("feet").at(0).get<float>(), j.at("feet").at(1).get<float>(), j.at("feet").at(2).get<float>()};
            p.rot  = Vec2{j.at("rot").at(0).get<float>(), j.at("rot").at(1).get<float>()};
            for (auto const& [slot, item] : j.at("slots").items()) p.slots.emplace(std::stoi(slot), game::items::fromJson(item));
            if (j.contains("head")) p.head = game::items::fromJson(j.at("head"));
            s.byId.emplace(id, std::move(p));
        } catch (...) {
            log().warn("ui fixture: a saved player entry could not be read: {}", key);
        }
    }
}

void storeSaved() {
    auto j = savedJson();
    if (j.empty()) game::db::erase(kPlayersKey);
    else game::db::put(kPlayersKey, j);
}

/// The saved entry of `p` (created with its place now); `fresh` tells whether it was just created.
SavedPlayer& savedFor(Player& p, bool* created = nullptr) {
    loadSaved();
    auto id          = p.getOrCreateUniqueID().rawID;
    auto [it, fresh] = saved().byId.try_emplace(id);
    if (created) *created = fresh;
    if (fresh) {
        it->second.dim       = static_cast<int>(p.getDimensionId());
        it->second.feet      = p.getFeetPos();
        it->second.rot       = p.getRotation();
        it->second.simulated = p.isSimulated();
    }
    return it->second;
}
} // namespace

void rememberPlayer(Player& p) {
    bool fresh = false;
    (void)savedFor(p, &fresh);
    if (fresh) storeSaved();
}

void rememberSlot(Player& p, int slot) {
    bool  fresh = false;
    auto& s     = savedFor(p, &fresh);
    if (s.slots.contains(slot) && !fresh) return;
    s.slots.emplace(slot, p.getInventory().getItem(slot));
    storeSaved();
}

void rememberHead(Player& p) {
    bool  fresh = false;
    auto& s     = savedFor(p, &fresh);
    if (s.head && !fresh) return;
    if (!s.head) s.head = p.getArmor(ArmorSlot::Head);
    storeSaved();
}

void setHeld(Player& p, ItemStack const& stack) {
    int slot = p.getSelectedItemSlot();
    rememberSlot(p, slot);
    p.getInventory().setItem(slot, stack);
    p.sendInventory(true);
}

void setHead(Player& p, ItemStack const& stack) {
    rememberHead(p);
    p.setArmor(ArmorSlot::Head, stack);
}

int restorePlayers() {
    loadSaved();
    int n = 0;
    for (auto it = saved().byId.begin(); it != saved().byId.end();) {
        auto* p = playerById(it->first);
        if (!p) { // not here now: kept (world DB) for a later clear; a gone test player never comes back
            it = it->second.simulated ? saved().byId.erase(it) : std::next(it);
            continue;
        }
        auto const& s = it->second;
        if (s.head) p->setArmor(ArmorSlot::Head, *s.head);
        for (auto const& [slot, stack] : s.slots) p->getInventory().setItem(slot, stack);
        p->sendInventory(false);
        p->teleport(s.feet, DimensionType(s.dim), s.rot);
        if (!p->isSimulated()) log().info("ui fixture: {} put back (position, {} inventory slot(s), helmet)", p->getRealName(), s.slots.size());
        ++n;
        it = saved().byId.erase(it);
    }
    storeSaved();
    return n;
}

std::size_t savedPlayerCount() {
    loadSaved();
    return saved().byId.size();
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

/// The stage a session that ended before its clear left in the world DB (the client harness
/// restarts the game), read into memory when none is active.
void loadStoredStage() {
    auto& s = stage();
    if (s.active) return;
    auto stored = game::db::get(kStageKey);
    if (!stored) return;
    if (auto restored = stageFromJson(*stored)) s = std::move(*restored);
    else game::db::erase(kStageKey);
}

ItemFrameBlockActor* frameAt(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor || (actor->getType() != BlockActorType::ItemFrame && actor->getType() != BlockActorType::GlowItemFrame)) return nullptr;
    return static_cast<ItemFrameBlockActor*>(actor);
}

/// Whether stage `s` is the box of `origin` in dimension `dim`.
bool stageAt(Stage const& s, int dim, BlockPos const& origin) {
    if (s.dim != dim || s.blocks.empty()) return false;
    BlockPos first{origin.x - 1, origin.y - 1, origin.z};
    return s.blocks.front().pos == first && s.blocks.size() == 3u * 5u * 5u;
}
} // namespace

void discardStage() {
    stage() = Stage{};
    game::db::erase(kStageKey);
}

bool emptyFrame(BlockSource& region, BlockPos const& pos) {
    auto* frame = frameAt(region, pos);
    if (!frame) return false;
    ItemInstance const& item = frame->mItem;
    if (item.isNull()) return false;
    // ItemFrameBlockActor::onRemoved drops the framed item: empty the frame before its block goes
    frame->setItem(region, ItemInstance::EMPTY_ITEM(), nullptr);
    frame->refresh(region);
    return true;
}

int clearStage() {
    auto& s = stage();
    loadStoredStage();
    if (!s.active) return 0;
    int   n      = 0;
    auto* region = game::regionOf(s.dim);
    // the stage is put back whole or not at all: an unloaded part keeps the snapshot (memory and
    // world DB) for a later clear
    bool loaded = region != nullptr;
    for (auto const& b : s.blocks) loaded = loaded && region->hasChunksAt(b.pos, 0, false);
    if (!loaded) {
        log().warn("ui fixture: the stage is not loaded; a later ui.fixture_clear restores it");
        return 0;
    }
    for (auto const& b : s.blocks) emptyFrame(*region, b.pos);
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
    auto& s   = stage();
    int   dim = static_cast<int>(region.getDimensionId());
    loadStoredStage();
    if (s.active && !stageAt(s, dim, origin)) {
        clearStage(); // a stage elsewhere (another session's spawn, another dimension): put it back first
        if (s.active) {
            // its snapshot is the only copy of those blocks: keep it rather than start another stage
            log().warn("ui fixture: an earlier stage elsewhere could not be restored; no new stage");
            return false;
        }
    }
    bool fixtureBlocks = s.active; // the box holds the fixture's blocks (its frames), not the world's
    if (!s.active) {
        // the snapshot of the untouched box; a stage that could not be put back (s.active at the same
        // place) keeps its own snapshot: the box now holds the fixture's blocks, not the original ones
        Stage fresh;
        fresh.dim    = dim;
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
    // the fixture's own frames are emptied (nothing drops); a frame of the untouched world is left as
    // the game removes it (its item drops rather than vanishes: the snapshot keeps no block entities)
    if (fixtureBlocks) forStageBox(origin, [&](BlockPos const& p) { emptyFrame(region, p); });
    forStageBox(origin, [&](BlockPos const& p) {
        auto const& want = p.y == origin.y - 1 ? *floor : *air;
        if (&region.getBlock(p) != &want) game::setBlock(region, p, want);
    });
    return true;
}

std::size_t itemEntitiesNear(BlockSource& region, BlockPos const& origin, int radius) {
    AABB box{
        Vec3{static_cast<float>(origin.x - radius), static_cast<float>(origin.y - radius), static_cast<float>(origin.z - radius)},
        Vec3{static_cast<float>(origin.x + radius + 1), static_cast<float>(origin.y + radius + 1), static_cast<float>(origin.z + radius + 1)}
    };
    return region.getEntities(box, 0.f, ActorType::ItemEntity).size();
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

// ---- respawn rules ------------------------------------------------------------------------------

namespace {
constexpr char kRulesKey[] = "gf:ui:fixture_rules";

struct RespawnRule {
    char const*               name;
    GameRules::GameRulesIndex index;
};
constexpr RespawnRule kRespawnRules[] = {
    {"doimmediaterespawn", GameRules::GameRulesIndex::DoImmediateRespawn},
    {"keepinventory", GameRules::GameRulesIndex::KeepInventory},
};

std::optional<bool> ruleValue(GameRules::GameRulesIndex index) {
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    GameRuleId id;
    id.mValue = static_cast<int>(index);
    return level->getGameRules().getBool(id, false);
}

/// Runs a command as the console (the gamerule command also tells the clients).
bool runCommand(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    ServerCommandOrigin origin("gloamingforest-ui-fixture", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    return ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin).mSuccessCount > 0;
}
} // namespace

bool setRespawnRules() {
    if (!game::db::get(kRulesKey)) { // the values before the fixture, once (a later run keeps the first)
        Json before = Json::object();
        for (auto const& r : kRespawnRules) {
            auto v = ruleValue(r.index);
            if (!v) return false;
            before[r.name] = *v;
        }
        game::db::put(kRulesKey, before);
        log().info("ui fixture: gamerules before the respawn step: {}", before.dump());
    }
    bool ok = true;
    for (auto const& r : kRespawnRules) ok = runCommand(std::string("gamerule ") + r.name + " true") && ok;
    return ok;
}

int restoreRespawnRules() {
    auto stored = game::db::get(kRulesKey);
    if (!stored || !stored->is_object()) return 0;
    int  n  = 0;
    bool ok = true;
    for (auto const& r : kRespawnRules) {
        auto it = stored->find(r.name);
        if (it == stored->end() || !it->is_boolean()) continue;
        if (ruleValue(r.index) != it->get<bool>()) ok = runCommand(std::string("gamerule ") + r.name + (it->get<bool>() ? " true" : " false")) && ok;
        ++n;
    }
    if (!ok) {
        log().warn("ui fixture: the gamerules could not be restored ({}); kept for a later ui.fixture_clear", stored->dump());
        return 0;
    }
    game::db::erase(kRulesKey);
    log().info("ui fixture: gamerules put back: {}", stored->dump());
    return n;
}

namespace {
class FixtureModule {
public:
    void install() {
        if (!mWorldHooks) { // the client loads many worlds in one process: per-world state goes with its world
            mWorldHooks = true;
            game::db::onWorldClosed([] {
                stage() = Stage{};
                saved() = SavedPlayers{};
            });
        }
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
    bool mKey        = false;
    bool mWorldHooks = false;
};
} // namespace

} // namespace gf::ui::fixture

GF_REGISTER_MODULE(gf::ui::fixture::FixtureModule);

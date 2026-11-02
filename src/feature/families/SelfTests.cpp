// Block families (F11): selftests "families.*" (testPos dx 6100..6199) on the gf_test fixture
// families (tools/families/fixtures/families.json, generated into the gf_test packs).
//
// Development builds have the gf_test packs: there every test fails when the fixture families are
// missing (the generator did not run), so a run cannot pass without checking anything. Builds
// without test packs skip with an info line.
// Clicks go through the game as the network handler runs a player's: GameMode::useItemOn /
// destroyBlock (-> the interactions hooks -> vanilla placement); a click the interactions dispatch
// never saw fails the test. (SimulatedPlayer's own use / interact calls drop uses inside Bedrock's
// build delay and empty-hand interactions, so they are not used.)

#include <cmath>
#include <fstream>
#include <random>
#include <variant>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/ext/combat/InventoryViewers.h"
#include "feature/ext/families/Growers.h"
#include "feature/families/BlockEvents.h"
#include "feature/families/Families.h"
#include "feature/families/LeafColors.h"
#include "feature/families/SignFont.h"
#include "feature/families/Signs.h"
#include "feature/inventory/Inventory.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace gf::feature::families::detail {
void   randomTickAt(BlockSource& region, BlockPos const& pos);
using TreePlacer = std::function<bool(std::string const& feature, BlockSource& region, BlockPos const& pos)>;
TreePlacer& treePlacer();
void   advanceSaplingWith(BlockSource& region, BlockPos const& pos, gf::blockevents::RandomSource& random);
void   signEditorFor(Player& player, GlobalPos const& pos, bool front);
bool   signEdit(Player& player, GlobalPos const& pos, bool front, std::vector<std::string> const& lines);
Json   signSentText(GlobalPos const& pos);
void   signSendAll(Player& player);
void   signForgetUnloaded(GlobalPos const& pos);
} // namespace gf::feature::families::detail

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace fam = gf::feature::families;
namespace ia  = gf::interactions;
using Facing  = ScriptModuleMinecraft::ScriptFacing;
constexpr char const* kFacing = gf::feature::families::state::Facing;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

bool fixturesReady(Context& ctx) {
    if (fam::entryOf("gf_test:family_slab") && gf::game::blockWith("gf_test:family_slab")) return true;
    if (gf::game::blockWith("gf_test:fixture_block")) {
        // a development build (the gf_test packs are here) without the family fixtures: the
        // generator (tools/families) did not run on this build
        ctx.check(false, "fixture families missing: the gf_test family blocks are not in this development build (tools/families did not run)");
        return false;
    }
    ctx.info("no gf_test packs in this build: the families fixtures are skipped");
    return false;
}

int st(GlobalPos const& p, char const* name) {
    auto const* b = fam::blockAt(p);
    return b ? fam::intState(*b, name).value_or(-1) : -1;
}
std::string sst(GlobalPos const& p, std::string const& name) {
    auto const* b = fam::blockAt(p);
    return b ? fam::strState(*b, name) : std::string();
}
bool flag(GlobalPos const& p, std::string const& name) {
    auto const* b = fam::blockAt(p);
    return b && fam::flagState(*b, name);
}

SimulatedPlayer& simPlayer(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

/// This process (a world reload restarts the server).
std::int64_t sessionId() {
    static std::int64_t const id = static_cast<std::int64_t>(std::random_device{}()) << 20 | static_cast<std::int64_t>(std::random_device{}() & 0xFFFFF);
    return id;
}

std::string where(GlobalPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); }

/// Runs a console command (gamerules).
bool command(std::string const& cmd) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    ServerCommandOrigin origin("gloamingforest-selftest", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    return ll::command::CommandRegistrar::getServerInstance().executeCommand(cmd, origin).mSuccessCount > 0;
}

/// The world's randomTickSpeed (1 when it can't be read).
int randomTickSpeed() {
    auto level = ll::service::getLevel();
    if (!level) return 1;
    auto const& rules = level->getGameRules().mGameRules.get();
    auto        idx   = static_cast<std::size_t>(GameRules::GameRulesIndex::RandomTickSpeed);
    if (idx >= rules.size()) return 1;
    auto const* v = std::get_if<int>(&rules[idx].mValue.get());
    return v ? *v : 1;
}

/// Raises randomTickSpeed for the guard's life and puts the world's own value back afterwards,
/// also when a require() ends the test early (the coroutine frame goes).
class RandomTicksRaised {
public:
    explicit RandomTicksRaised(int speed) : mBefore(randomTickSpeed()) { mOk = command("gamerule randomtickspeed " + std::to_string(speed)); }
    ~RandomTicksRaised() { restore(); }
    RandomTicksRaised(RandomTicksRaised const&)            = delete;
    RandomTicksRaised& operator=(RandomTicksRaised const&) = delete;
    [[nodiscard]] bool ok() const { return mOk; }
    void               restore() {
        if (mRestored) return;
        mRestored = true;
        command("gamerule randomtickspeed " + std::to_string(mBefore));
    }

private:
    int  mBefore;
    bool mOk       = false;
    bool mRestored = false;
};

/// Puts `itemId` (16 of it; "" = an empty hand) in hotbar slot 0 and stands the player two blocks
/// off face `face` of `p` (south of it for the top / bottom), looking at `look` (default: `p`).
void stand(Context& ctx, GlobalPos const& p, int face, std::string const& itemId, std::optional<Vec3> look = std::nullopt) {
    auto& sp = simPlayer(ctx);
    Vec3  pos{p.x + 0.5f, float(p.y), p.z + 0.5f};
    if (face >= 2) {
        pos.x += fam::kDx[face] * 2.0f;
        pos.z += fam::kDz[face] * 2.0f;
    } else {
        pos.z += 2.0f;
        if (face == fam::Up) pos.y += 1.0f; // the top of a ground block: stand on the ground
    }
    sp.teleport(pos, sp.getDimensionId());
    if (look) sp.simulateLookAt(*look, sim::LookDuration::Instant);
    else sp.simulateLookAt(gf::game::blockPos(p), sim::LookDuration::Instant);
    if (itemId.empty()) {
        sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    } else if (auto item = gf::game::items::make(itemId, 16)) {
        sp.simulateSetItem(*item, true, 0);
    }
}

/// Every click waits this long after the player is placed: SimulatedPlayer applies a look on its
/// next tick, and Bedrock drops a player's use of an item on a block a few ticks after the last one.
constexpr int kClickGap = 8;

/// The player's right click on face `face` of `p` at face-local `hit` with the held item (hotbar
/// slot 0, maybe empty), as the network handler runs one (ItemUseInventoryTransaction):
/// GameMode::useItemOn with the absolute click position, a first event.
void useOn(Context& ctx, GlobalPos const& p, Facing face, Vec3 hit) {
    auto&    sp   = simPlayer(ctx);
    BlockPos bp   = gf::game::blockPos(p);
    auto&    held = const_cast<ItemStack&>(sp.getSelectedItem());
    sp.mGameMode->useItemOn(held, bp, static_cast<uchar>(face), Vec3{bp.x + hit.x, bp.y + hit.y, bp.z + hit.z}, HandSlot::Mainhand, nullptr, true);
}

/// The player breaks the block at `p` (hit on face `face`) as the server does at the end of a
/// player's mining: GameMode::destroyBlock.
void destroy(Context& ctx, GlobalPos const& p, Facing face) { simPlayer(ctx).mGameMode->destroyBlock(gf::game::blockPos(p), static_cast<uchar>(face)); }

/// Clicks face `face` of `p` at face-local `hit` through the game. True when the interactions
/// dispatch saw the click (the real path, no shortcut).
ll::coro::CoroTask<bool> click(Context& ctx, GlobalPos p, Facing face, Vec3 hit, std::string itemId) {
    stand(ctx, p, static_cast<int>(face), itemId);
    co_await ctx.ticks(kClickGap); // the look is applied on the player's next tick
    bool seen = false;
    auto id   = ia::onUseOn({}, [&seen, p](ia::UseOnContext const& c) {
        if (c.pos == p) seen = true;
        return ia::Result::Pass;
    }, ia::priority::Highest + 1000);
    useOn(ctx, p, face, hit);
    ia::remove(id);
    co_return seen;
}

/// Places `itemId` against face `face` of `against`; the position the game placed it at.
ll::coro::CoroTask<std::optional<GlobalPos>> place(Context& ctx, GlobalPos against, Facing face, Vec3 hit, std::string itemId, std::optional<Vec3> look = std::nullopt) {
    stand(ctx, against, static_cast<int>(face), itemId, look);
    co_await ctx.ticks(kClickGap);
    std::optional<GlobalPos> placed;
    auto id = ia::onPlace({}, [&placed](ia::PlaceContext const& c) { placed = c.pos; }, ia::priority::Highest + 1000);
    useOn(ctx, against, face, hit);
    ia::remove(id);
    co_return placed;
}

/// The dropped items within 2 blocks of `p`.
std::vector<Actor*> itemsNear(Context& ctx, GlobalPos const& p) {
    std::vector<Actor*> out;
    auto*               region = ctx.region();
    if (!region) return out;
    AABB box{Vec3{p.x - 2.f, p.y - 2.f, p.z - 2.f}, Vec3{p.x + 3.f, p.y + 3.f, p.z + 3.f}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item") out.push_back(a);
    return out;
}

int dustSignal(GlobalPos const& p) {
    auto const* dust = fam::blockAt(p);
    return dust ? fam::intState(*dust, "redstone_signal").value_or(-1) : -1;
}

/// The container declarations tools/families wrote (<mod>/data/gf/containers/<file>), by block id.
fam::Json containerDeclarations(std::string const& file) {
    fam::Json out = fam::Json::object();
    try {
        std::ifstream in(gf::Mod::getInstance().getSelf().getModDir() / "data" / "gf" / "containers" / file);
        auto          doc = fam::Json::parse(in, nullptr, false);
        for (auto const& e : doc.is_array() ? doc : fam::Json::array())
            if (e.is_object() && e.contains("block") && e["block"].is_string()) out[e["block"].get<std::string>()] = e;
    } catch (...) {}
    return out;
}

/// The last "screen.open" of `type` sent to the player (null: none).
fam::Json openedScreen(Context& ctx, std::string const& type) {
    fam::Json opened;
    for (auto const& m : gf::net::takeTestMessages(ctx.player()))
        if (m.channel == "screen.open" && m.payload.value("type", std::string{}) == type) opened = m.payload;
    return opened;
}

/// The player closes its native screen of `type` (the client mod's "screen.closed").
void closeScreen(Context& ctx, std::string const& type) { gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", type}}); }

/// The player counts as a client mod user while the guard lives (also when a require() ends the
/// test early).
struct CompanionGuard {
    Context& ctx;
    explicit CompanionGuard(Context& c) : ctx(c) {
        gf::net::setTestCompanion(ctx.player(), true);
        (void)gf::net::takeTestMessages(ctx.player());
    }
    ~CompanionGuard() { gf::net::setTestCompanion(ctx.player(), false); }
    CompanionGuard(CompanionGuard const&)            = delete;
    CompanionGuard& operator=(CompanionGuard const&) = delete;
};

/// A fixed RandomSource for the grower's choices.
class FixedRandom final : public gf::blockevents::RandomSource {
public:
    explicit FixedRandom(float f) : mFloat(f) {}
    void         setSeed(std::int64_t) override {}
    int          nextInt() override { return 0; }
    int          nextInt(int) override { return 0; }
    std::int64_t nextLong() override { return 0; }
    bool         nextBoolean() override { return false; }
    float        nextFloat() override { return mFloat; }
    double       nextDouble() override { return mFloat; }
    double       nextGaussian() override { return 0; }
    std::unique_ptr<RandomSource> fork() override { return std::make_unique<FixedRandom>(mFloat); }
    std::unique_ptr<gf::core::jrand::PositionalRandomFactory> forkPositional() override { return nullptr; }

private:
    float mFloat;
};

} // namespace

GF_REGISTER_SELFTEST("families.fixtures", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    int missing = 0, count = 0;
    for (auto const* e : fam::entries()) {
        if (!e->id.starts_with("gf_test:family_")) continue;
        ++count;
        if (!gf::game::blockWith(e->id)) {
            ++missing;
            ctx.check(false, e->id + " is registered in the game");
        }
    }
    ctx.check(count >= 30, "every fixture family is loaded (" + std::to_string(count) + ")");
    ctx.check(fam::entryOf("gf_test:family_door")->family == fam::Family::Door, "families are read by name");
    ctx.check(fam::entryOf("gf_test:family_wall_sign")->family == fam::Family::WallSign, "a sign of class WallSignBlock is a wall sign");
    ctx.check(fam::entryOf("gf_test:family_wall_hanging_sign")->family == fam::Family::WallHangingSign, "a hanging sign of class WallHangingSignBlock is a wall hanging sign");
    ctx.check(fam::entryOf("gf_test:family_wall_pillar")->family == fam::Family::WallPillar, "a pillar of class WallPillarBlock is a wall pillar");
    ctx.check(fam::entryOf("gf_test:family_stairs")->has(fam::Waterloggable), "params.waterlogged adds the waterloggable modifier");
    // F1's form: the modifiers in params.traits
    ctx.check(fam::entryOf("gf_test:family_slab")->has(fam::Waterloggable), "params.traits [waterloggable] adds the waterloggable modifier");
    ctx.check(fam::entryOf("gf_test:family_climbable")->has(fam::Climbable), "params.traits [climbable] adds the climbable modifier");
    ctx.check(fam::entryOf("gf_test:family_lightable")->has(fam::Lightable) && fam::entryOf("gf_test:family_lightable")->family == fam::Family::Cube,
              "a shape family and a modifier on one id");
    // TF's registrations fill what data leaves out
    ctx.check(fam::familyOf("pillar", "ThornsBlock") == fam::Family::ConnectablePillar, "TF's thorns are connectable pillars");
    // the state schema of docs/spec/blockstates.md: Java's names and values, no traits
    auto const* door = gf::game::blockWith("gf_test:family_door");
    ctx.check(door && fam::strState(*door, kFacing) == "north" && fam::strState(*door, fam::state::Half) == "lower" && fam::strState(*door, fam::state::Hinge) == "left"
                  && fam::boolState(*door, fam::state::Open) == false,
              "a door has Java's facing / half / hinge / open states");
    auto const* chest   = gf::game::blockWith("gf_test:family_chest");
    auto const* trapped = gf::game::blockWith("gf_test:family_trapped_chest");
    ctx.check(chest && fam::strState(*chest, fam::state::Type) == "single" && fam::intState(*chest, fam::state::Lid) == 0 && !fam::intState(*chest, fam::state::OpenCount),
              "a chest has type and the lid (no signal)");
    ctx.check(trapped && fam::intState(*trapped, fam::state::Lid) == 0 && fam::intState(*trapped, fam::state::OpenCount) == 0, "a trapped chest has the lid and open_count");
    // tools/families declares the chest families as containers of the inventory contract (F10)
    auto declared = containerDeclarations("families_test.json");
    ctx.check(declared.value("gf_test:family_chest", fam::Json{}).value("stat", "") == "minecraft:open_chest"
                  && declared.value("gf_test:family_trapped_chest", fam::Json{}).value("stat", "") == "minecraft:trigger_trapped_chest",
              "the chests are declared containers with Java's opening stats (ChestBlock / TrappedChestBlock)");
    ctx.check(declared.value("gf_test:family_chest", fam::Json{}).value("pairs", false) && declared.value("gf_test:family_chest", fam::Json{}).value("comparator", false),
              "they pair into double chests and give comparators their signal");
    // a sign a lone families.sign_a_write left behind (its reader cleans up after a reload)
    auto sign   = ctx.testPos(6125, 4);
    bool loaded = co_await ctx.ensureLoaded(sign);
    if (loaded && ctx.blockAt(sign) == "gf_test:family_sign") ctx.cleanupBlock(sign);
    ctx.info("families fixtures present: " + std::to_string(count) + " (" + std::to_string(missing) + " missing)");
});

GF_REGISTER_SELFTEST("families.slab_doubling", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6101, 5);
    auto p   = ctx.testPos(6101, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(at(p, 1, 0, 0), "gf_test:family_slab", {{fam::state::Type, "bottom"}}), "bottom slab placed");
    co_await ctx.ticks(1);
    // a second slab clicked onto the top of a bottom slab merges into a double slab
    ctx.check(co_await click(ctx, at(p, 1, 0, 0), Facing::Up, Vec3{0.5f, 0.5f, 0.5f}, "gf_test:family_slab"), "the click went through the game");
    co_await ctx.ticks(2);
    ctx.check(sst(at(p, 1, 0, 0), fam::state::Type) == "double", "the slab is double after a second slab on its top (" + sst(at(p, 1, 0, 0), fam::state::Type) + ")");
    // a top slab takes a slab from below
    ctx.require(ctx.setBlock(at(p, 3, 1, 0), "gf_test:family_slab", {{fam::state::Type, "top"}}), "top slab placed");
    co_await ctx.ticks(1);
    auto& region = *ctx.region();
    ctx.check(fam::mergeSlab(region, gf::game::blockPos(at(p, 3, 1, 0))), "a top slab merges");
    ctx.check(sst(at(p, 3, 1, 0), fam::state::Type) == "double", "the top slab is double");
    ctx.check(!fam::mergeSlab(region, gf::game::blockPos(at(p, 3, 1, 0))), "a double slab takes no more");
    // placing a slab against the upper half of a block's side makes a top slab
    ctx.require(ctx.setBlock(at(p, 0, 0, 2), "minecraft:stone"), "a stone to place against");
    auto placed = co_await place(ctx, at(p, 0, 0, 2), Facing::East, Vec3{1.0f, 0.8f, 0.5f}, "gf_test:family_slab");
    co_await ctx.ticks(2);
    ctx.check(placed && sst(*placed, fam::state::Type) == "top", "a slab placed on the upper half of a side is a top slab (" + (placed ? sst(*placed, fam::state::Type) : "not placed") + ")");
    (void)box;
});

GF_REGISTER_SELFTEST("families.door_pair", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6105, 5);
    auto p   = ctx.testPos(6105, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto door = at(p, 2, 0, 0);
    // a player places the door on the ground: the lower half, then the upper half on top
    ctx.check(co_await click(ctx, at(door, 0, -1, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_door"), "the click went through the game");
    co_await ctx.ticks(3);
    ctx.require(ctx.blockAt(door) == "gf_test:family_door", "the lower half is placed (got " + ctx.blockAt(door) + ")");
    ctx.check(ctx.blockAt(at(door, 0, 1, 0)) == "gf_test:family_door", "the upper half is placed above it");
    ctx.check(sst(door, fam::state::Half) == "lower" && sst(at(door, 0, 1, 0), fam::state::Half) == "upper", "lower and upper halves");
    ctx.check(sst(door, kFacing) == "north" && sst(at(door, 0, 1, 0), kFacing) == "north", "the door faces the way the player looked (north: " + sst(door, kFacing) + ")");
    ctx.cleanupBlock(at(door, 0, 1, 0));
    // using either half opens both
    ctx.check(fam::useDoor(at(door, 0, 1, 0), nullptr), "the upper half can be used");
    co_await ctx.ticks(2);
    ctx.check(flag(door, fam::state::Open) && flag(at(door, 0, 1, 0), fam::state::Open), "both halves are open");
    ctx.check(co_await click(ctx, door, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, ""), "a click on the door");
    co_await ctx.ticks(2);
    ctx.check(!flag(door, fam::state::Open) && !flag(at(door, 0, 1, 0), fam::state::Open), "a player's click closes both halves");
    // redstone opens it (the lower half's neighbour), and it closes when the power goes
    ctx.require(ctx.setBlock(at(door, 1, 0, 0), "minecraft:redstone_block"), "a redstone block beside the door");
    co_await ctx.ticks(8);
    ctx.check(flag(door, fam::state::Open) && flag(door, fam::state::Powered), "redstone opens the door");
    ctx.setBlock(at(door, 1, 0, 0), "minecraft:air");
    co_await ctx.ticks(8);
    ctx.check(!flag(door, fam::state::Open), "the door closes without power");
    // DoorBlock.canSurvive: the ground under the door goes, the door pops off
    ctx.setBlock(at(door, 0, -1, 0), "minecraft:air");
    co_await ctx.ticks(4);
    ctx.check(ctx.blockAt(door) != "gf_test:family_door" && ctx.blockAt(at(door, 0, 1, 0)) != "gf_test:family_door", "the door breaks without support (" + ctx.blockAt(door) + ")");
    // removing the upper half removes the lower one
    ctx.setBlock(at(door, 0, -1, 0), "minecraft:stone");
    ctx.setBlock(door, "gf_test:family_door", {{fam::state::Half, "lower"}});
    ctx.setBlock(at(door, 0, 1, 0), "gf_test:family_door", {{fam::state::Half, "upper"}});
    co_await ctx.ticks(2);
    fam::clear(at(door, 0, 1, 0));
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(door) != "gf_test:family_door", "the lower half goes with the upper half (got " + ctx.blockAt(door) + ")");
    // the upper half broken with its drops (a survival player's break): one door, the lower half's
    // (the upper half has no loot: Java's half=lower loot condition)
    auto upper = at(door, 0, 1, 0);
    ctx.setBlock(door, "gf_test:family_door", {{fam::state::Half, "lower"}});
    ctx.setBlock(upper, "gf_test:family_door", {{fam::state::Half, "upper"}});
    co_await ctx.ticks(2);
    for (auto* item : itemsNear(ctx, door)) item->remove();
    fam::destroyWithDrops(*ctx.region(), gf::game::blockPos(upper));
    co_await ctx.ticks(4);
    auto drops = itemsNear(ctx, door).size();
    ctx.check(ctx.blockAt(door) != "gf_test:family_door", "the lower half goes with the broken upper half");
    ctx.check(drops == 1u, "breaking the upper half drops one door (" + std::to_string(drops) + ")");
    for (auto* item : itemsNear(ctx, door)) item->remove();
    // a creative player breaking the upper half (GameMode::destroyBlock): no drop at all
    // (DoorBlock.playerWillDestroy -> preventDropFromBottomPart)
    ctx.setBlock(door, "gf_test:family_door", {{fam::state::Half, "lower"}});
    ctx.setBlock(upper, "gf_test:family_door", {{fam::state::Half, "upper"}});
    auto name = simPlayer(ctx).getRealName();
    ctx.require(command("gamemode creative \"" + name + "\""), "the player in creative");
    stand(ctx, upper, fam::North, "");
    co_await ctx.ticks(2);
    destroy(ctx, upper, Facing::North);
    co_await ctx.ticks(4);
    command("gamemode survival \"" + name + "\"");
    drops = itemsNear(ctx, door).size();
    ctx.check(ctx.blockAt(door) != "gf_test:family_door" && ctx.blockAt(upper) != "gf_test:family_door", "a creative player's break takes both halves");
    ctx.check(drops == 0u, "and drops nothing (" + std::to_string(drops) + ")");
    for (auto* item : itemsNear(ctx, door)) item->remove();
    (void)box;
});

GF_REGISTER_SELFTEST("families.fence_connections", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6110, 5);
    auto p   = ctx.testPos(6110, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto a = at(p, 1, 0, 0), b = at(p, 2, 0, 0), c = at(p, 2, 0, -1);
    ctx.require(ctx.setBlock(a, "gf_test:family_fence"), "fence a");
    ctx.require(ctx.setBlock(b, "gf_test:family_fence"), "fence b");
    co_await ctx.ticks(3);
    // one-sided: a joins b on its east side only, b joins a on its west side only
    ctx.check(flag(a, "twilightforest:east") && !flag(a, "twilightforest:west"), "fence a connects east to b and not west");
    ctx.check(flag(b, "twilightforest:west") && !flag(b, "twilightforest:east"), "fence b connects west to a and not east");
    ctx.check(!flag(a, "twilightforest:north") && !flag(a, "twilightforest:south"), "no connection to air");
    // a full block connects, leaves don't (Java isExceptionForConnection), a vanilla fence does
    ctx.require(ctx.setBlock(c, "minecraft:stone"), "stone north of b");
    ctx.require(ctx.setBlock(at(b, 0, 0, 1), "gf_test:family_leaves", {{fam::state::Persistent, true}}), "leaves south of b");
    ctx.require(ctx.setBlock(at(a, 0, 0, 1), "minecraft:oak_fence"), "a vanilla fence south of a");
    co_await ctx.ticks(3);
    ctx.check(flag(b, "twilightforest:north"), "a fence connects to a sturdy block");
    ctx.check(!flag(b, "twilightforest:south"), "but not to leaves");
    ctx.check(flag(a, "twilightforest:south"), "and to a vanilla wooden fence");
    // removing a neighbour disconnects
    ctx.setBlock(a, "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(!flag(b, "twilightforest:west"), "the connection goes with the neighbour");
    // walls and panes follow their neighbours too
    auto w = at(p, 1, 0, 2), w2 = at(p, 2, 0, 2);
    ctx.require(ctx.setBlock(w, "gf_test:family_wall") && ctx.setBlock(w2, "gf_test:family_wall"), "two walls");
    auto g = at(p, 3, 0, 1);
    ctx.require(ctx.setBlock(g, "gf_test:family_pane") && ctx.setBlock(at(g, 0, 0, 1), "gf_test:family_pane"), "two panes");
    co_await ctx.ticks(3);
    ctx.check(sst(w, "twilightforest:east") == "low" && sst(w2, "twilightforest:west") == "low", "walls connect low (" + sst(w, "twilightforest:east") + ")");
    ctx.check(sst(w, "twilightforest:west") == "none", "a wall's free side stays none");
    ctx.check(flag(w, fam::state::Up), "the end of a wall keeps its post");
    ctx.check(flag(g, "twilightforest:south") && !flag(g, "twilightforest:north"), "panes connect to each other only");
    // a block on top of a wall line raises its sides to tall
    ctx.require(ctx.setBlock(at(w, 0, 1, 0), "minecraft:stone"), "stone on the wall");
    co_await ctx.ticks(3);
    ctx.check(sst(w, "twilightforest:east") == "tall", "a covered wall side is tall (" + sst(w, "twilightforest:east") + ")");
    (void)box;
});

GF_REGISTER_SELFTEST("families.collision", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java: fences and walls collide 24 px high (the collision hook), a player stands on top
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6155, 5);
    auto p   = ctx.testPos(6155, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto fence = at(p, 2, 0, 0), wall = at(p, 2, 0, 2);
    ctx.require(ctx.setBlock(fence, "gf_test:family_fence") && ctx.setBlock(wall, "gf_test:family_wall"), "a fence and a wall");
    co_await ctx.ticks(3);
    auto& region = *ctx.region();
    for (auto const& [pos, name] : {std::pair{fence, "fence"}, std::pair{wall, "wall"}}) {
        auto const* b = fam::blockAt(pos);
        std::vector<AABB> boxes;
        if (b) b->getBlockType().addCollisionShapes(*b, region, gf::game::blockPos(pos), nullptr, boxes, {});
        float top = 0;
        for (auto const& bx : boxes) top = std::max(top, bx.max.y - pos.y);
        ctx.check(std::fabs(top - 1.5f) < 0.01f, std::string("the ") + name + " collides 24 px high (" + std::to_string(top) + ")");
    }
    // a player dropped onto the fence stands on its 24 px top
    simPlayer(ctx).teleport(Vec3{fence.x + 0.5f, fence.y + 2.2f, fence.z + 0.5f}, simPlayer(ctx).getDimensionId());
    co_await ctx.ticks(20);
    float feet = simPlayer(ctx).getAABB().min.y - fence.y;
    ctx.check(std::fabs(feet - 1.5f) < 0.05f, "a player stands on a fence at 1.5 blocks (" + std::to_string(feet) + ")");
    (void)box;
});

GF_REGISTER_SELFTEST("families.button_redstone", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6115, 5);
    auto p   = ctx.testPos(6115, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto button = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(button, "gf_test:family_button", {{fam::state::Face, "floor"}}), "a button on the ground");
    ctx.require(ctx.setBlock(at(button, 1, 0, 0), "minecraft:redstone_wire"), "redstone dust beside it");
    co_await ctx.ticks(2);
    ctx.check(dustSignal(at(button, 1, 0, 0)) == 0, "no power before the press");
    ctx.check(co_await click(ctx, button, Facing::Up, Vec3{0.5f, 0.1f, 0.5f}, ""), "the click went through the game");
    co_await ctx.ticks(4);
    ctx.check(flag(button, fam::state::Powered), "the button is pressed");
    ctx.check(dustSignal(at(button, 1, 0, 0)) == 15, "the pressed button powers the dust with 15 (got " + std::to_string(dustSignal(at(button, 1, 0, 0))) + ")");
    // TF's wooden buttons stay pressed for 30 ticks
    co_await ctx.ticks(20);
    ctx.check(flag(button, fam::state::Powered), "still pressed after 20 ticks");
    co_await ctx.ticks(14);
    ctx.check(!flag(button, fam::state::Powered), "released after 30 ticks");
    co_await ctx.ticks(4);
    ctx.check(dustSignal(at(button, 1, 0, 0)) == 0, "the dust is off again (got " + std::to_string(dustSignal(at(button, 1, 0, 0))) + ")");
    // a pressure plate is pressed while a player stands on it
    auto plate = at(p, 0, 0, 2);
    ctx.require(ctx.setBlock(plate, "gf_test:family_pressure_plate"), "a pressure plate");
    simPlayer(ctx).teleport(Vec3{plate.x + 0.5f, float(plate.y), plate.z + 0.5f}, simPlayer(ctx).getDimensionId());
    co_await ctx.ticks(4);
    ctx.check(flag(plate, fam::state::Powered), "the plate is pressed under the player");
    simPlayer(ctx).teleport(Vec3{plate.x + 3.5f, float(plate.y), plate.z + 0.5f}, simPlayer(ctx).getDimensionId());
    co_await ctx.ticks(24);
    ctx.check(!flag(plate, fam::state::Powered), "and released after the player left (20 ticks)");
    // BasePressurePlateBlock.canSurvive: a plate stands on a fence's top centre
    auto fence = at(p, 4, 0, 2);
    ctx.require(ctx.setBlock(fence, "minecraft:oak_fence"), "a fence");
    auto placed = co_await place(ctx, fence, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_pressure_plate");
    co_await ctx.ticks(3);
    ctx.check(placed && ctx.blockAt(*placed) == "gf_test:family_pressure_plate", "a plate stays on a fence (canSupportCenter)");
    (void)box;
});

GF_REGISTER_SELFTEST("families.switch_a_save", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a button and a plate saved while pressed (the e2e scenario reloads the world in between)
    if (!fixturesReady(ctx)) co_return;
    auto p = ctx.testPos(6160, 1);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // set without ctx.setBlock (which restores air after the test): they stay for the reader; the
    // flat world's ground carries them
    auto& region = *ctx.region();
    auto const* button = gf::game::blockWithStates("gf_test:family_button", {{fam::state::Face, "floor"}, {fam::state::Powered, true}});
    auto const* plate  = gf::game::blockWithStates("gf_test:family_pressure_plate", {{fam::state::Powered, true}});
    ctx.require(button && plate, "pressed switch states");
    gf::game::setBlock(region, gf::game::blockPos(p), *button);
    gf::game::setBlock(region, gf::game::blockPos(at(p, 2, 0, 0)), *plate);
    co_await ctx.ticks(1);
    ctx.require(flag(p, fam::state::Powered) && flag(at(p, 2, 0, 0), fam::state::Powered), "a pressed button and a pressed plate");
    gf::game::db::put("gf:families:selftest_switches", {{"session", sessionId()}});
    ctx.info("pressed switches written at " + where(p));
    co_return;
});

GF_REGISTER_SELFTEST("families.switch_b_reload", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto p = ctx.testPos(6160, 1);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto marker = gf::game::db::get("gf:families:selftest_switches");
    gf::game::db::erase("gf:families:selftest_switches");
    if (ctx.blockAt(p) != "gf_test:family_button" || !marker || marker->value("session", std::int64_t{0}) == sessionId()) {
        // written in this session (no reload in between): the button pressed by a command has no
        // release scheduled, as in Java; nothing to check
        ctx.info("no switches saved before a reload: nothing to check");
        for (auto const& q : {p, at(p, 2, 0, 0)}) ctx.cleanupBlock(q);
        co_return;
    }
    // the chunk's first tick after the load re-arms the releases (Java keeps them with the chunk)
    for (int i = 0; i < 80 && (flag(p, fam::state::Powered) || flag(at(p, 2, 0, 0), fam::state::Powered)); ++i) co_await ctx.ticks(1);
    ctx.check(!flag(p, fam::state::Powered), "the button saved while pressed is released after the reload");
    ctx.check(!flag(at(p, 2, 0, 0), fam::state::Powered), "the plate saved while pressed is released after the reload");
    ctx.info("switches released after the reload at " + where(p));
    for (auto const& q : {p, at(p, 2, 0, 0)}) ctx.cleanupBlock(q);
});

GF_REGISTER_SELFTEST("families.leaves_decay", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6120, 5);
    auto p   = ctx.testPos(6120, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto log    = at(p, 1, 0, 0);
    auto leaf1  = at(p, 1, 1, 0);
    auto leaf2  = at(p, 2, 1, 0);
    auto keeper = at(p, 3, 1, 0);
    ctx.require(ctx.setBlock(log, "gf_test:family_log", {{fam::state::Axis, "y"}}), "a log");
    ctx.require(ctx.setBlock(leaf1, "gf_test:family_leaves"), "leaves on the log");
    ctx.require(ctx.setBlock(leaf2, "gf_test:family_leaves"), "leaves beside them");
    ctx.require(ctx.setBlock(keeper, "gf_test:family_leaves", {{fam::state::Persistent, true}}), "persistent leaves");
    // Java's distance spreads one scheduled tick at a time
    for (int i = 0; i < 40 && !(st(leaf1, fam::state::Distance) == 1 && st(leaf2, fam::state::Distance) == 2); ++i) co_await ctx.ticks(1);
    ctx.check(st(leaf1, fam::state::Distance) == 1 && st(leaf2, fam::state::Distance) == 2,
              "the distance to the log (" + std::to_string(st(leaf1, fam::state::Distance)) + ", " + std::to_string(st(leaf2, fam::state::Distance)) + ")");
    // vanilla leaves carry a distance too (getOptionalDistanceAt): log - vanilla leaves - ours
    auto vlog = at(p, 0, 0, 2), vleaf = at(p, 1, 0, 2), ours = at(p, 2, 0, 2);
    ctx.require(ctx.setBlock(vlog, "minecraft:oak_log") && ctx.setBlock(vleaf, "minecraft:oak_leaves") && ctx.setBlock(ours, "gf_test:family_leaves"), "vanilla log and leaves");
    for (int i = 0; i < 20 && st(ours, fam::state::Distance) != 2; ++i) co_await ctx.ticks(1);
    ctx.check(st(ours, fam::state::Distance) == 2, "leaves next to vanilla leaves of a log are at distance 2 (" + std::to_string(st(ours, fam::state::Distance)) + ")");
    ctx.setBlock(ours, "minecraft:air");
    ctx.setBlock(vleaf, "minecraft:air");
    ctx.setBlock(vlog, "minecraft:air");
    // the log goes: the distances climb to 7 (the leaves feed each other on the way, as in Java)
    ctx.setBlock(log, "minecraft:air");
    for (int i = 0; i < 80 && !(st(leaf1, fam::state::Distance) == 7 && st(leaf2, fam::state::Distance) == 7); ++i) co_await ctx.ticks(1);
    ctx.check(st(leaf1, fam::state::Distance) == 7 && st(leaf2, fam::state::Distance) == 7, "no log: distance 7");
    // the game's random ticks (LevelChunk::tickImpl at a raised randomTickSpeed) decay them
    RandomTicksRaised fast(1000);
    ctx.require(fast.ok(), "random ticks raised");
    for (int i = 0; i < 60 && (ctx.blockAt(leaf1) == "gf_test:family_leaves" || ctx.blockAt(leaf2) == "gf_test:family_leaves"); ++i) co_await ctx.ticks(1);
    fast.restore();
    ctx.check(ctx.blockAt(leaf1) != "gf_test:family_leaves" && ctx.blockAt(leaf2) != "gf_test:family_leaves", "the leaves decayed on random ticks");
    ctx.check(ctx.blockAt(keeper) == "gf_test:family_leaves", "persistent leaves never decay");
    (void)box;
});

GF_REGISTER_SELFTEST("families.sign_a_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the sign stays for families.sign_b_read (after a reload in the e2e scenario), which clears it;
    // families.fixtures clears one a lone run left behind
    if (!fixturesReady(ctx)) co_return;
    auto p = ctx.testPos(6125, 4);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    gf::game::setBlock(region, gf::game::blockPos(p), *gf::game::blockWith("gf_test:family_sign"));
    co_await ctx.ticks(1);
    ctx.require(ctx.blockAt(p) == "gf_test:family_sign", "a sign is placed");
    ctx.check(gf::signs::text(p).size() == 4 && gf::signs::text(p)[0].empty(), "a new sign has four empty lines");
    ctx.check(gf::signs::setText(p, {"Here lies", "a naga", "", "families"}), "setText on a sign");
    ctx.check(gf::signs::setText(p, {"back side"}, false), "setText on the back");
    ctx.check(!gf::signs::setText(at(p, 0, 1, 0), {"x"}), "setText refuses a block that is no sign");
    auto lines = gf::signs::text(p);
    ctx.check(lines.size() == 4 && lines[0] == "Here lies" && lines[3] == "families", "the text reads back");
    ctx.check(gf::signs::text(p, false)[0] == "back side", "the back reads back");
    ctx.info("sign written at " + where(p));
});

GF_REGISTER_SELFTEST("families.sign_b_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!fixturesReady(ctx)) co_return;
    auto p = ctx.testPos(6125, 4);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    if (ctx.blockAt(p) != "gf_test:family_sign") {
        ctx.info("no sign written: nothing to read");
        co_return;
    }
    auto lines = gf::signs::text(p);
    ctx.check(lines.size() == 4 && lines[0] == "Here lies" && lines[1] == "a naga" && lines[3] == "families", "the sign text survived the reload");
    ctx.check(gf::signs::text(p, false)[0] == "back side", "the back survived too");
    co_await ctx.ticks(2);
    auto* display = fam::detail::signDisplay(p, true);
    ctx.check(display && display->getNameTag().find("Here lies") != std::string::npos, "the text shows on its display entity after the reload");
    ctx.info("sign text verified at " + where(p));
    ctx.setBlock(p, "minecraft:air"); // the sign broken (cleanupBlock only clears after the test)
    co_await ctx.ticks(2);
    ctx.check(!fam::detail::signDisplay(p, true), "the display entity goes with the sign");
});

GF_REGISTER_SELFTEST("families.sign_edit", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the editor: only the player it opened for, once; waxed signs; display entities; companions
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6165, 5);
    auto p   = ctx.testPos(6165, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(at(p, 0, -1, 0), "minecraft:stone"), "ground");
    ctx.require(ctx.setBlock(p, "gf_test:family_sign", {{fam::state::Rotation, 8}}), "a sign");
    co_await ctx.ticks(1);
    auto& other = ctx.spawnPlayer("FamiliesEditor2");
    auto  otherId = other.getOrCreateUniqueID();
    stand(ctx, p, fam::North, "");
    fam::detail::signEditorFor(ctx.player(), p, true);
    ctx.check(!fam::detail::signEdit(other, p, true, {"intruder"}), "another player's edit is refused");
    ctx.check(fam::detail::signEdit(ctx.player(), p, true, {"hello", "world"}), "the editor's edit is taken");
    ctx.check(gf::signs::text(p)[0] == "hello", "the text is set");
    ctx.check(!fam::detail::signEdit(ctx.player(), p, true, {"again"}), "an editor edits once per opening");
    // Java's line width: a long line is cut where the editor stops typing (90 px)
    fam::detail::signEditorFor(ctx.player(), p, true);
    fam::detail::signEdit(ctx.player(), p, true, {std::string(40, 'W')});
    ctx.check(gf::signs::text(p)[0].size() == 15, "a line is cut at 90 px (" + std::to_string(gf::signs::text(p)[0].size()) + " W)");
    co_await ctx.ticks(2);
    auto* display = fam::detail::signDisplay(p, true);
    ctx.check(display && display->getNameTag().find("WWW") != std::string::npos, "the front text shows on its display entity");
    // the name shows without looking at it: CanShowName (set by the kit) and the nameable
    // component's always_show (the NametagAlwaysShow data item)
    ctx.check(display && display->getStatusFlag(ActorFlags::CanShowName) && display->mEntityData->getInt8(static_cast<ushort>(ActorDataIDs::NametagAlwaysShow)) != 0,
              "its name tag is always shown");
    ctx.check(!fam::detail::signDisplay(p, false), "an empty back has none");
    // an empty hand opens the editor, a dye colours the side facing the player, honeycomb waxes it
    ctx.check(co_await click(ctx, p, Facing::North, Vec3{0.5f, 0.5f, 0.5f}, "minecraft:red_dye"), "a dye click");
    co_await ctx.ticks(2);
    ctx.check(gf::blockdata::get(p) && (*gf::blockdata::get(p))["sign"]["front"].value("color", "") == "red", "the dye colours the front");
    // SignBlock.useItemOn: another item (and a dye that changes nothing) goes on to the editor
    ctx.check(co_await click(ctx, p, Facing::North, Vec3{0.5f, 0.5f, 0.5f}, "minecraft:red_dye"), "the same dye again");
    ctx.check(fam::detail::signEdit(ctx.player(), p, true, {"WWW again"}), "a dye that changes nothing opens the editor");
    ctx.check(co_await click(ctx, p, Facing::North, Vec3{0.5f, 0.5f, 0.5f}, "minecraft:stick"), "a stick click");
    ctx.check(fam::detail::signEdit(ctx.player(), p, true, {"WWW stick"}), "any other item opens the editor");
    // ServerGamePacketListenerImpl.updateSignText: ChatFormatting.stripFormatting on every line
    fam::detail::signEditorFor(ctx.player(), p, true);
    fam::detail::signEdit(ctx.player(), p, true, {"\xC2\xA7" "k" "\xC2\xA7" "c" "Hi" "\xC2\xA7", "a\xC2\xA7" "lb"});
    ctx.check(gf::signs::text(p)[0] == "Hi" && gf::signs::text(p)[1] == "ab", "a player's formatting codes are stripped (" + gf::signs::text(p)[0] + ")");
    fam::detail::signEditorFor(ctx.player(), p, true);
    fam::detail::signEdit(ctx.player(), p, true, {"WWW stick"});
    ctx.check(co_await click(ctx, p, Facing::North, Vec3{0.5f, 0.5f, 0.5f}, "minecraft:honeycomb"), "a honeycomb click");
    co_await ctx.ticks(2);
    fam::detail::signEditorFor(ctx.player(), p, true);
    ctx.check(!fam::detail::signEdit(ctx.player(), p, true, {"waxed"}), "a waxed sign takes no edit");
    // SignBlock.useItemOn on a waxed sign: PASS_TO_DEFAULT_BLOCK_INTERACTION, so the main hand goes
    // on to useWithoutItem (the waxed fail: the click is taken) and the off hand's item is used
    // as an item (the sign passes)
    {
        static bool reached = false; // a probe after the sign's handler (it runs only if the sign passed)
        reached  = false;
        auto id  = ia::onUseOn({}, [](ia::UseOnContext const&) { reached = true; return ia::Result::Pass; }, ia::priority::Fallback - 1000);
        stand(ctx, p, fam::North, "minecraft:stick");
        co_await ctx.ticks(kClickGap);
        useOn(ctx, p, Facing::North, Vec3{0.5f, 0.5f, 0.5f});
        bool mainPassed = reached;
        reached         = false;
        co_await ctx.ticks(kClickGap); // another click, not the held button's repeat
        auto& sp        = simPlayer(ctx);
        auto  off       = gf::game::items::make("minecraft:stick", 1);
        auto  bp        = gf::game::blockPos(p);
        sp.mGameMode->useItemOn(*off, bp, static_cast<uchar>(Facing::North), Vec3{bp.x + 0.5f, bp.y + 0.5f, bp.z + 0.5f}, HandSlot::Offhand, nullptr, true);
        bool offPassed = reached;
        ia::remove(id);
        ctx.check(!mainPassed, "a main hand item on a waxed sign is taken (the fail sound)");
        ctx.check(offPassed, "an off hand item on a waxed sign is passed on as an item use");
    }
    gf::signs::setText(p, {"for everyone"});
    // the text can't be taken off the sign: a killed display entity comes back (checked every 20
    // ticks)
    command("kill @e[type=gloamingforest:sign_text,x=" + std::to_string(p.x) + ",y=" + std::to_string(p.y) + ",z=" + std::to_string(p.z) + ",r=2]");
    co_await ctx.ticks(2);
    auto* killed = fam::detail::signDisplay(p, true);
    ctx.info(std::string("after /kill the display entity is ") + (!killed ? "gone" : (killed->isAlive() ? "alive" : "dead")));
    for (int i = 0; i < 50; ++i) {
        auto* d = fam::detail::signDisplay(p, true);
        if (d && d->isAlive() && d->getNameTag().find("for everyone") != std::string::npos) break;
        co_await ctx.ticks(1);
    }
    auto* back = fam::detail::signDisplay(p, true);
    ctx.check(back && back->isAlive() && back->getNameTag().find("for everyone") != std::string::npos, "a killed display entity comes back with the text");
    if (auto* o = ll::service::getLevel()->fetchEntity(otherId, false)) static_cast<SimulatedPlayer*>(o)->simulateDisconnect();
    ctx.cleanupBlock(p);
    (void)box;
});

GF_REGISTER_SELFTEST("families.double_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the chest family is the inventory contract's container (tools/families declared it): its
    // halves pair, a click of a client mod user opens the double chest's 54 slots (the RIGHT half
    // first), a player inside opens both lids (ChestLidController: 10 ticks), a broken half keeps the
    // other half's slots; the trapped chest sends the players inside as its signal
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6130, 5);
    auto p   = ctx.testPos(6130, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  a = at(p, 1, 0, 0), b = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(a, "gf_test:family_chest", {{kFacing, "north"}}), "chest a facing north");
    co_await ctx.ticks(1);
    auto ha = gf::inventory::find(region, gf::game::blockPos(a));
    ctx.require(ha.has_value(), "the chest is a container of the inventory contract (tools/families declared it)");
    ctx.check(ha->slots == 27, "a half has 27 slots (" + std::to_string(ha->slots) + ")");
    ha->set(a, 0, *gf::game::items::make("minecraft:dirt", 5));
    // b east of a, facing north: b is the RIGHT (first) half, a the LEFT
    ctx.require(ctx.setBlock(b, "gf_test:family_chest"), "chest b east of it");
    fam::pairChest(region, gf::game::blockPos(b), fam::North, false, std::nullopt);
    co_await ctx.ticks(3);
    ctx.check(sst(b, fam::state::Type) == "right" && sst(a, fam::state::Type) == "left", "the chests pair (b " + sst(b, fam::state::Type) + ", a " + sst(a, fam::state::Type) + ")");
    ctx.check(fam::chestPartner(region, gf::game::blockPos(a)).has_value(), "each half finds its partner");
    auto hb = gf::inventory::find(region, gf::game::blockPos(b));
    ctx.require(hb.has_value(), "the other half is a container too");
    hb->set(b, 0, *gf::game::items::make("minecraft:stone", 3));
    ctx.check(gf::game::items::idOf(ha->get(a, 0)) == "minecraft:dirt", "each half keeps its own slots");
    // a third chest beside a double chest stays single
    auto c = at(p, 3, 0, 0);
    ctx.require(ctx.setBlock(c, "gf_test:family_chest"), "chest c");
    fam::pairChest(region, gf::game::blockPos(c), fam::North, false, std::nullopt);
    co_await ctx.ticks(2);
    ctx.check(sst(c, fam::state::Type) == "single", "a third chest stays single");
    ctx.setBlock(c, "minecraft:air");
    {
        // a client mod user's empty-hand click on the LEFT half: the double chest's screen
        CompanionGuard companion(ctx);
        ctx.check(co_await click(ctx, a, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, ""), "a click on chest a");
        co_await ctx.ticks(1);
        auto opened = openedScreen(ctx, "inventory.chest");
        ctx.require(!opened.is_null(), "the inventory contract's chest screen opened");
        auto data  = opened.value("data", fam::Json::object());
        auto items = data.value("items", fam::Json::array());
        ctx.check(data.value("rows", 0) == 6 && items.size() == 54u, "a double chest shows 6 rows, 54 slots (" + std::to_string(items.size()) + ")");
        auto slot = [&](std::size_t i) { return i < items.size() && !items[i].is_null() ? gf::game::items::idOf(gf::game::items::fromJson(items[i])) : std::string(); };
        ctx.check(slot(0) == "minecraft:stone" && slot(27) == "minecraft:dirt", "the RIGHT half's slots come first (ChestBlock#combine: " + slot(0) + ", " + slot(27) + ")");
        ctx.check(gf::inventory::viewers(a) == 1 && gf::inventory::viewers(b) == 1, "the player is inside both halves");
        // ChestLidController: the lids open in 10 ticks, eased (22.5 degree steps)
        co_await ctx.ticks(2);
        int early = st(a, fam::state::Lid);
        co_await ctx.ticks(10);
        ctx.check(early > 0 && early < 4, "the lid is on its way after 2 ticks (" + std::to_string(early) + ")");
        ctx.check(st(a, fam::state::Lid) == 4 && st(b, fam::state::Lid) == 4, "both lids are open after 10 ticks (" + std::to_string(st(a, fam::state::Lid)) + ", " + std::to_string(st(b, fam::state::Lid)) + ")");
        closeScreen(ctx, "inventory.chest");
        co_await ctx.ticks(2);
        ctx.check(gf::inventory::viewers(a) == 0, "closing the screen leaves the chest");
        co_await ctx.ticks(10);
        ctx.check(st(a, fam::state::Lid) == 0 && st(b, fam::state::Lid) == 0, "and the lids close again");
        ctx.check(gf::game::items::idOf(ha->get(a, 0)) == "minecraft:dirt" && gf::game::items::idOf(hb->get(b, 0)) == "minecraft:stone" && itemsNear(ctx, a).empty(),
                  "the halves keep their stacks while their states change (pairing, lids)");
    }
    ctx.check(fam::chestLidStep(0) == 0 && fam::chestLidStep(1) == 1 && fam::chestLidStep(3) == 3 && fam::chestLidStep(5) == 4 && fam::chestLidStep(10) == 4,
              "ChestRenderer's eased lid: 1 - (1 - o)^3 in 22.5 degree steps");
    // breaking one half: its own slots drop, the other half keeps its own
    for (auto* item : itemsNear(ctx, b)) item->remove();
    ctx.setBlock(b, "minecraft:air");
    co_await ctx.ticks(3);
    ctx.check(sst(a, fam::state::Type) == "single", "the remaining half is single again");
    ctx.check(gf::game::items::idOf(ha->get(a, 0)) == "minecraft:dirt", "the remaining half keeps its own items");
    int stone = 0;
    for (auto* item : itemsNear(ctx, b)) {
        auto const& s = static_cast<ItemActor*>(item)->item();
        if (gf::game::items::idOf(s) == "minecraft:stone") stone += gf::game::items::countOf(s);
        item->remove();
    }
    ctx.check(stone == 3, "the broken half drops its own stacks (" + std::to_string(stone) + " stone)");
    // a trapped chest sends the players inside as its signal, a comparator reads its slots
    auto t = at(p, 0, 0, 2);
    ctx.require(ctx.setBlock(t, "gf_test:family_trapped_chest", {{kFacing, "north"}}), "a trapped chest");
    ctx.require(ctx.setBlock(at(t, 1, 0, 0), "minecraft:redstone_wire"), "dust beside it");
    co_await ctx.ticks(2);
    ctx.check(dustSignal(at(t, 1, 0, 0)) == 0, "no signal while closed");
    {
        CompanionGuard companion(ctx);
        ctx.check(co_await click(ctx, t, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, ""), "a click on the trapped chest");
        co_await ctx.ticks(3);
        ctx.check(st(t, fam::state::OpenCount) == 1, "one player inside (" + std::to_string(st(t, fam::state::OpenCount)) + ")");
        ctx.check(dustSignal(at(t, 1, 0, 0)) >= 1, "an open trapped chest powers the dust (" + std::to_string(dustSignal(at(t, 1, 0, 0))) + ")");
        closeScreen(ctx, "inventory.chest");
        co_await ctx.ticks(3);
        ctx.check(st(t, fam::state::OpenCount) == 0 && dustSignal(at(t, 1, 0, 0)) == 0, "and stops when closed");
    }
    // ChestBlock.getAnalogOutputSignal (the inventory contract's declared container): one full stack
    auto ht = gf::inventory::find(region, gf::game::blockPos(t));
    ctx.require(ht.has_value(), "the trapped chest is a container");
    ctx.check(gf::redstone::analogOutput(region, gf::game::blockPos(t)).value_or(-1) == 0, "an empty chest gives a comparator 0");
    ht->set(t, 0, *gf::game::items::make("minecraft:stone", 64));
    co_await ctx.ticks(1);
    auto analog = gf::redstone::analogOutput(region, gf::game::blockPos(t));
    ctx.check(analog.value_or(-1) == 1, "one full stack in 27 slots gives 1 (" + std::to_string(analog.value_or(-1)) + ")");
    ht->set(t, 0, ItemStack{});
    (void)box;
});
GF_REGISTER_SELFTEST("families.shapes", [](Context& ctx) -> ll::coro::CoroTask<> {
    // stairs shapes, trapdoor, fence gate, wall pillar and connected texture connections
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6135, 5);
    auto p   = ctx.testPos(6135, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // StairBlock.getStairsShape: stairs facing north with stairs facing west in front: outer_left
    auto s1 = at(p, 0, 0, -1), s0 = at(p, 0, 0, 0);
    ctx.require(ctx.setBlock(s1, "gf_test:family_stairs", {{kFacing, "west"}, {fam::state::Half, "bottom"}}), "stairs facing west");
    ctx.require(ctx.setBlock(s0, "gf_test:family_stairs", {{kFacing, "north"}, {fam::state::Half, "bottom"}}), "stairs facing north behind them");
    co_await ctx.ticks(3);
    ctx.check(sst(s0, fam::state::Shape) == "outer_left", "an outer corner (" + sst(s0, fam::state::Shape) + ")");
    // vanilla stairs count too (StairBlock.isStairs): west-facing oak stairs (weirdo_direction 1) behind
    auto s2 = at(p, 2, 0, 0), v2 = at(p, 2, 0, 1);
    ctx.require(ctx.setBlock(v2, "minecraft:oak_stairs", {{"weirdo_direction", 1}}), "vanilla stairs facing west");
    ctx.require(ctx.setBlock(s2, "gf_test:family_stairs", {{kFacing, "north"}, {fam::state::Half, "bottom"}}), "stairs facing north in front of them");
    co_await ctx.ticks(3);
    ctx.check(sst(s2, fam::state::Shape) == "inner_left", "an inner corner with vanilla stairs (" + sst(s2, fam::state::Shape) + ")");
    // a trapdoor toggles on use
    auto t = at(p, 0, 0, 2);
    ctx.require(ctx.setBlock(t, "gf_test:family_trapdoor", {{kFacing, "north"}}), "a trapdoor");
    co_await ctx.ticks(1);
    ctx.check(fam::useTrapdoor(t, nullptr) && flag(t, fam::state::Open), "the trapdoor opens");
    ctx.check(fam::useTrapdoor(t, nullptr) && !flag(t, fam::state::Open), "and closes");
    // a fence gate opens away from the player and is lowered between walls
    auto g = at(p, 2, 0, 2);
    ctx.require(ctx.setBlock(g, "gf_test:family_fence_gate", {{kFacing, "north"}}), "a fence gate");
    ctx.require(ctx.setBlock(at(g, 1, 0, 0), "gf_test:family_wall") && ctx.setBlock(at(g, -1, 0, 0), "gf_test:family_wall"), "walls on its sides");
    co_await ctx.ticks(3);
    ctx.check(flag(g, fam::state::InWall), "the gate between walls is lowered");
    ctx.check(fam::useFenceGate(g, nullptr) && flag(g, fam::state::Open), "the gate opens");
    // ConnectableRotatedPillarBlock.updateShape: the lower pillar connects up when the upper one
    // is placed; the upper one (placed with its default state) keeps its own
    auto w1 = at(p, 4, 0, 0), w2 = at(p, 4, 1, 0);
    ctx.require(ctx.setBlock(w1, "gf_test:family_wall_pillar", {{fam::state::Axis, "y"}}), "wall pillar");
    co_await ctx.ticks(1);
    ctx.require(ctx.setBlock(w2, "gf_test:family_wall_pillar", {{fam::state::Axis, "y"}}), "wall pillar above");
    co_await ctx.ticks(3);
    ctx.check(flag(w1, "twilightforest:up"), "the lower wall pillar connects up");
    ctx.check(!flag(w1, "twilightforest:north"), "and not to the air");
    // connected textures: the sides, and (one-face blocks) the corner block
    auto c1 = at(p, 0, 1, 4), c2 = at(p, 1, 1, 4), c3 = at(p, 0, 2, 4), c4 = at(p, 1, 2, 4);
    ctx.require(ctx.setBlock(c1, "gf_test:family_ct_flat") && ctx.setBlock(c2, "gf_test:family_ct_flat") && ctx.setBlock(c3, "gf_test:family_ct_flat"), "three connected texture blocks");
    co_await ctx.ticks(3);
    ctx.check(flag(c1, "gloamingforest:east") && flag(c1, "gloamingforest:up") && !flag(c1, "gloamingforest:west"), "connected textures join their own kind");
    ctx.check(!flag(c1, "gloamingforest:corner_east_up"), "no corner block yet");
    ctx.require(ctx.setBlock(c4, "gf_test:family_ct_flat"), "the corner block");
    co_await ctx.ticks(3);
    ctx.check(flag(c1, "gloamingforest:corner_east_up"), "the diagonal block sets the corner (ConnectedTextureModel.isCornerBlockPresent)");
    (void)box;
});

GF_REGISTER_SELFTEST("families.placement", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java getStateForPlacement through the game's placement
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6150, 5);
    auto p   = ctx.testPos(6150, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto ground = at(p, 0, -1, 0);
    // stairs on the ground, the player south of them looking north: facing north, bottom half
    auto s = co_await place(ctx, ground, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_stairs");
    co_await ctx.ticks(2);
    ctx.check(s && sst(*s, kFacing) == "north" && sst(*s, fam::state::Half) == "bottom" && sst(*s, fam::state::Shape) == "straight",
              "stairs face the player's look, bottom half (" + (s ? sst(*s, kFacing) + " " + sst(*s, fam::state::Half) : "not placed") + ")");
    // a log against a block's south side lies along z
    auto stone = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(stone, "minecraft:stone"), "a stone");
    auto l = co_await place(ctx, stone, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "gf_test:family_log");
    co_await ctx.ticks(2);
    ctx.check(l && sst(*l, fam::state::Axis) == "z", "a log on a side takes that axis (" + (l ? sst(*l, fam::state::Axis) : "not placed") + ")");
    if (l) ctx.setBlock(*l, "minecraft:air"); // cleanupBlock only clears after the test
    // a button on a block's side is a wall button facing out; on top a floor button
    auto b1 = co_await place(ctx, stone, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "gf_test:family_button");
    co_await ctx.ticks(2);
    ctx.check(b1 && sst(*b1, fam::state::Face) == "wall" && sst(*b1, kFacing) == "south", "a wall button faces out of the wall (" + (b1 ? sst(*b1, fam::state::Face) + " " + sst(*b1, kFacing) : "not placed") + ")");
    if (b1) ctx.setBlock(*b1, "minecraft:air");
    // a wall sign on the side; a wall hanging sign stands across the wall (WallHangingSignBlock)
    auto ws = co_await place(ctx, stone, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "gf_test:family_sign");
    co_await ctx.ticks(2);
    ctx.check(ws && ctx.blockAt(*ws) == "gf_test:family_wall_sign" && sst(*ws, kFacing) == "south", "a sign on a side becomes the wall sign facing out (" + (ws ? ctx.blockAt(*ws) : "not placed") + ")");
    if (ws) ctx.setBlock(*ws, "minecraft:air");
    auto hs = co_await place(ctx, stone, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "gf_test:family_hanging_sign", Vec3{stone.x + 0.6f, stone.y + 0.5f, stone.z + 1.0f});
    co_await ctx.ticks(2);
    ctx.check(hs && ctx.blockAt(*hs) == "gf_test:family_wall_hanging_sign" && (sst(*hs, kFacing) == "east" || sst(*hs, kFacing) == "west"),
              "a hanging sign on a side hangs across the wall (" + (hs ? ctx.blockAt(*hs) + " " + sst(*hs, kFacing) : "not placed") + ")");
    if (hs) ctx.setBlock(*hs, "minecraft:air");
    // a horizontal block faces the player, a directional one the other way of the look
    auto hz = co_await place(ctx, at(p, 4, -1, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_horizontal");
    co_await ctx.ticks(2);
    ctx.check(hz && sst(*hz, kFacing) == "south", "a horizontal block faces the player (" + (hz ? sst(*hz, kFacing) : "not placed") + ")");
    // EtchedNagastoneBlock: BlockPlaceContext.getNearestLookingDirection is the look alone. The player
    // looks east, 30 degrees down, and clicks the top of a block: the block faces east (the clicked
    // face's opposite, down, does not come first); a DirectionalBlock faces the other way (west)
    auto eye = [&](GlobalPos const& against) { return Vec3{against.x + 0.5f, against.y + 1.0f + 1.62f, against.z + 2.5f}; };
    auto east30 = [&](GlobalPos const& against) {
        auto e = eye(against);
        return Vec3{e.x + 10.0f, e.y - 5.7735f, e.z};
    };
    auto etchedOn = at(p, 1, -1, -2);
    auto et       = co_await place(ctx, etchedOn, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_etched", east30(etchedOn));
    co_await ctx.ticks(2);
    ctx.check(et && sst(*et, kFacing) == "east", "an etched nagastone faces the nearest look direction (" + (et ? sst(*et, kFacing) : "not placed") + ")");
    auto dirOn = at(p, 3, -1, -2);
    auto dr    = co_await place(ctx, dirOn, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_directional", east30(dirOn));
    co_await ctx.ticks(2);
    ctx.check(dr && sst(*dr, kFacing) == "west", "a directional block faces away from it (" + (dr ? sst(*dr, kFacing) : "not placed") + ")");
    // a button with nothing to hang on is not placed (a fence top is no sturdy face)
    auto fence = at(p, 0, 0, 3);
    ctx.require(ctx.setBlock(fence, "minecraft:oak_fence"), "a fence");
    auto none = co_await place(ctx, fence, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "gf_test:family_button");
    co_await ctx.ticks(2);
    ctx.check(!none || ctx.blockAt(*none) != "gf_test:family_button", "a button on a fence top is refused");
    (void)box;
});

GF_REGISTER_SELFTEST("families.interactions", [](Context& ctx) -> ll::coro::CoroTask<> {
    // log stripping, potted plants, lightable, snowloggable, climbable
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6140, 5);
    auto p   = ctx.testPos(6140, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto lg = at(p, 0, 0, 0);
    ctx.require(ctx.setBlock(lg, "gf_test:family_log", {{fam::state::Axis, "x"}}), "a log on its side");
    co_await ctx.ticks(1);
    ctx.check(co_await click(ctx, lg, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:iron_axe"), "an axe click");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(lg) == "gf_test:family_stripped_log", "the axe strips the log (" + ctx.blockAt(lg) + ")");
    ctx.check(sst(lg, fam::state::Axis) == "x", "the stripped log keeps its axis");
    // potted plants
    auto pot = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(pot, "minecraft:flower_pot"), "an empty flower pot");
    co_await ctx.ticks(1);
    ctx.check(co_await click(ctx, pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f}, "gf_test:family_sapling"), "a sapling click on the pot");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(pot) == "gf_test:family_potted", "the sapling goes into the pot (" + ctx.blockAt(pot) + ")");
    ctx.check(co_await click(ctx, pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f}, ""), "an empty hand on the potted sapling");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(pot) == "minecraft:flower_pot", "the plant comes out again");
    // FlowerPotBlock.useItemOn: any plant (a vanilla one too) on a full pot does nothing
    ctx.check(co_await click(ctx, pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f}, "gf_test:family_sapling"), "the sapling again");
    co_await ctx.ticks(2);
    ctx.check(co_await click(ctx, pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f}, "minecraft:cactus"), "a cactus on the full pot");
    co_await ctx.ticks(2);
    ctx.check(ctx.blockAt(pot) == "gf_test:family_potted", "a vanilla plant leaves the full pot as it is (" + ctx.blockAt(pot) + ")");
    // lightable
    auto lamp = at(p, 4, 0, 0);
    ctx.require(ctx.setBlock(lamp, "gf_test:family_lightable"), "a lightable block");
    co_await ctx.ticks(1);
    ctx.check(co_await click(ctx, lamp, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "minecraft:flint_and_steel"), "flint and steel");
    co_await ctx.ticks(2);
    ctx.check(sst(lamp, fam::state::Lighting) == "normal", "flint and steel lights it (" + sst(lamp, fam::state::Lighting) + ")");
    ctx.check(co_await click(ctx, lamp, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, ""), "an empty hand");
    co_await ctx.ticks(2);
    ctx.check(sst(lamp, fam::state::Lighting) == "none", "an empty hand puts it out");
    // snowloggable: snow in, then breaking the snow part or the plant part (SnowLoggable.clip)
    auto bush = at(p, 0, 0, 2);
    ctx.require(ctx.setBlock(bush, "gf_test:family_snowloggable"), "a snowloggable plant");
    co_await ctx.ticks(1);
    for (int i = 0; i < 3; ++i) {
        ctx.check(co_await click(ctx, bush, Facing::Up, Vec3{0.5f, 0.5f, 0.5f}, "minecraft:snow_layer"), "snow on the plant");
        co_await ctx.ticks(6);
    }
    ctx.check(st(bush, fam::state::Layers) == 3, "three snow layers inside the plant (" + std::to_string(st(bush, fam::state::Layers)) + ")");
    stand(ctx, bush, fam::South, "", Vec3{bush.x + 0.5f, bush.y + 0.1f, bush.z + 0.5f});
    co_await ctx.ticks(2); // the look is applied on the player's next tick
    destroy(ctx, bush, Facing::South);
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(bush) == "gf_test:family_snowloggable" && st(bush, fam::state::Layers) == 0, "hitting the snow takes the snow, the plant stays");
    ctx.setBlock(bush, "gf_test:family_snowloggable", {{fam::state::Layers, 3}});
    co_await ctx.ticks(1);
    stand(ctx, bush, fam::South, "", Vec3{bush.x + 0.5f, bush.y + 0.8f, bush.z + 0.5f});
    co_await ctx.ticks(2); // the look is applied on the player's next tick
    destroy(ctx, bush, Facing::South);
    co_await ctx.ticks(3);
    ctx.check(ctx.blockAt(bush) == "minecraft:snow_layer", "hitting the plant takes the plant, its snow stays (" + ctx.blockAt(bush) + ")");
    // climbable blocks are climbable for the game's movement
    auto const* climb = gf::game::blockWith("gf_test:family_climbable");
    ctx.check(climb && (static_cast<std::uint64_t>(climb->getBlockType().mProperties) & static_cast<std::uint64_t>(BlockProperty::Climbable)) != 0,
              "the climbable family block type is climbable");
    (void)box;
});

GF_REGISTER_SELFTEST("families.saplings", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java SaplingBlock stages and TreeGrower: the tree, the 2x2 mega tree, the secondary chance,
    // a unit's own grower
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6170, 6);
    auto p   = ctx.testPos(6170, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    std::vector<std::pair<std::string, BlockPos>> grown;
    fam::detail::treePlacer() = [&grown](std::string const& feature, BlockSource& r, BlockPos const& pos) {
        grown.emplace_back(feature, pos);
        gf::game::setBlock(r, pos, *gf::game::blockWith("gf_test:fixture_block"));
        return true;
    };
    FixedRandom low(0.0f), high(0.99f);
    auto sap = at(p, 0, 0, 0);
    ctx.require(ctx.setBlock(sap, "gf_test:family_sapling"), "a sapling");
    fam::detail::advanceSaplingWith(region, gf::game::blockPos(sap), high);
    ctx.check(st(sap, fam::state::Stage) == 1, "the sapling's first stage");
    fam::detail::advanceSaplingWith(region, gf::game::blockPos(sap), high);
    ctx.check(grown.size() == 1 && grown[0].first == "gf_test:fixture_column" && grown[0].second == gf::game::blockPos(sap), "the second advance grows its tree at the sapling");
    // 2x2: the mega tree from the north-west sapling, the four saplings cleared
    grown.clear();
    auto m0 = at(p, 2, 0, 0);
    for (auto [dx, dz] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}})
        ctx.require(ctx.setBlock(at(m0, dx, 0, dz), "gf_test:family_mega_sapling", {{fam::state::Stage, 1}}), "a mega sapling");
    fam::detail::advanceSaplingWith(region, gf::game::blockPos(at(m0, 1, 0, 1)), high);
    ctx.check(grown.size() == 1 && grown[0].second == gf::game::blockPos(m0), "the 2x2 grows the mega tree from its north-west corner");
    ctx.check(ctx.blockAt(at(m0, 1, 0, 1)) != "gf_test:family_mega_sapling" && ctx.blockAt(at(m0, 0, 0, 1)) != "gf_test:family_mega_sapling", "the four saplings make way");
    // alone, with secondary_chance 1: the secondary tree
    grown.clear();
    auto lone = at(p, 0, 0, 3);
    ctx.require(ctx.setBlock(lone, "gf_test:family_mega_sapling", {{fam::state::Stage, 1}}), "a lone mega sapling");
    fam::detail::advanceSaplingWith(region, gf::game::blockPos(lone), low);
    ctx.check(grown.size() == 1 && grown[0].first == "gf_test:fixture_column_secondary", "a lone sapling grows the secondary tree by chance (" + (grown.empty() ? std::string("none") : grown[0].first) + ")");
    // a unit's own grower replaces the data's
    bool own = false;
    gf::families_ext::registerGrower("gf_test:family_sapling", [&own](BlockSource&, BlockPos const&, Block const&, gf::families_ext::RandomSource&) { return own = true; });
    auto sap2 = at(p, 2, 0, 3);
    ctx.require(ctx.setBlock(sap2, "gf_test:family_sapling", {{fam::state::Stage, 1}}), "another sapling");
    fam::detail::advanceSaplingWith(region, gf::game::blockPos(sap2), high);
    ctx.check(own, "a registered grower grows the sapling");
    gf::families_ext::registerGrower("gf_test:family_sapling", nullptr);
    fam::detail::treePlacer() = nullptr;
    (void)box;
});

GF_REGISTER_SELFTEST("families.blockevents", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the contract on the F0 fixture block: placed, neighbour change, removed, random tick (the
    // game's random ticks: LevelChunk::tickImpl at a raised randomTickSpeed)
    auto p = ctx.testPos(6145, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    static int placed = 0, neighbor = 0, removed = 0, ticked = 0; // handlers stay registered: no captured locals
    static GlobalPos probe;
    placed = neighbor = removed = ticked = 0;
    probe  = p;
    static bool registered = false;
    if (!registered) {
        registered = true;
        auto mine  = [](BlockSource& r, BlockPos const& pos) { return gf::game::globalPos(r, pos) == probe; };
        gf::blockevents::onPlaced("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, Block const&, Actor*) { if (mine(r, pos)) ++placed; });
        gf::blockevents::onNeighborChanged("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, BlockPos const&) { if (mine(r, pos)) ++neighbor; });
        gf::blockevents::onRemoved("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, Block const&) { if (mine(r, pos)) ++removed; });
        gf::blockevents::onRandomTick("gf_test:fixture_block", [mine](BlockSource& r, BlockPos const& pos, Block const&, gf::blockevents::RandomSource&) {
            if (mine(r, pos)) ++ticked;
        });
    }
    ctx.require(ctx.setBlock(p, "gf_test:fixture_block"), "the fixture block");
    co_await ctx.ticks(2);
    ctx.check(placed == 1, "onPlaced ran once (" + std::to_string(placed) + ")");
    ctx.setBlock(at(p, 1, 0, 0), "minecraft:stone");
    co_await ctx.ticks(2);
    ctx.check(neighbor >= 1, "onNeighborChanged ran for a neighbour");
    ctx.setBlock(p, "gf_test:fixture_block", {{"gf_test:level", 2}});
    co_await ctx.ticks(2);
    ctx.check(placed == 1 && removed == 0, "a state change is no placement / removal");
    RandomTicksRaised fast(1000);
    ctx.require(fast.ok(), "random ticks raised");
    for (int i = 0; i < 40 && ticked == 0; ++i) co_await ctx.ticks(1);
    fast.restore();
    ctx.check(ticked >= 1, "the chunk's random ticks reach the block (" + std::to_string(ticked) + ")");
    ctx.setBlock(p, "minecraft:air");
    co_await ctx.ticks(2);
    ctx.check(removed == 1, "onRemoved ran once (" + std::to_string(removed) + ")");
    ctx.setBlock(at(p, 1, 0, 0), "minecraft:air");
    probe = GlobalPos{-1, 0, -100000, 0};
});

GF_REGISTER_SELFTEST("families.waterlogging", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a waterloggable family block (here F1's form: the slab's params.traits) placed into water
    // keeps the water in the block (Bedrock's liquid detection; Java SimpleWaterloggedBlock); a
    // family block without the modifier does not
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6177, 5);
    auto p   = ctx.testPos(6177, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  wet    = [&region](GlobalPos const& q) {
        auto const& extra = region.getExtraBlock(gf::game::blockPos(q)).getTypeName();
        return extra == "minecraft:water" || extra == "minecraft:flowing_water";
    };
    std::pair<GlobalPos, char const*> const cases[]{{at(p, 1, 0, 0), "gf_test:family_slab"}, {at(p, 4, 0, 0), "gf_test:family_fence"}};
    for (auto const& [w, id] : cases) {
        // a basin: stone around the water
        for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}})
            ctx.require(ctx.setBlock(at(w, dx, 0, dz), "minecraft:stone"), "the basin");
        ctx.require(ctx.setBlock(w, "minecraft:water"), "water in the basin");
    }
    co_await ctx.ticks(2);
    for (auto const& [w, id] : cases) {
        auto placed = co_await place(ctx, at(w, 0, -1, 0), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, id);
        co_await ctx.ticks(3);
        ctx.check(placed && *placed == w && ctx.blockAt(w) == id, std::string(id) + " placed into the water (" + ctx.blockAt(w) + ")");
    }
    ctx.check(wet(cases[0].first), "the waterloggable slab keeps the water");
    ctx.check(!wet(cases[1].first), "a family block without the modifier does not");
    (void)box;
});

GF_REGISTER_SELFTEST("families.chest_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the inventory contract's chest rules on the family chests: a loot table set through the
    // handler (RandomizableContainer) rolls when the unopened chest breaks and drops with its
    // stacks (Containers.dropContents); a redstone conductor on top blocks the chest
    // (ChestBlock.isChestBlockedAt)
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6187, 5);
    auto p   = ctx.testPos(6187, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  c      = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(c, "gf_test:family_chest", {{kFacing, "north"}}), "a chest");
    co_await ctx.ticks(1);
    auto h = gf::inventory::find(region, gf::game::blockPos(c));
    ctx.require(h.has_value(), "the chest is a container");
    h->set(c, 5, *gf::game::items::make("minecraft:dirt", 7));
    h->setLootTable(c, "gf_test:chests/fixture_chest", 42);
    for (auto* item : itemsNear(ctx, c)) item->remove();
    ctx.setBlock(c, "minecraft:air"); // broken without being opened
    co_await ctx.ticks(4);
    int dirt = 0, loot = 0;
    for (auto* a : itemsNear(ctx, c)) {
        auto const& stack = static_cast<ItemActor*>(a)->item();
        auto        id    = gf::game::items::idOf(stack);
        int         n     = gf::game::items::countOf(stack);
        if (id == "minecraft:dirt") dirt += n;
        else if (id == "gf_test:fixture_item" || id == "minecraft:stick" || id == "minecraft:diamond") loot += n;
        a->remove();
    }
    ctx.check(dirt == 7, "the broken chest drops the stacks it held (" + std::to_string(dirt) + " dirt)");
    ctx.check(loot >= 1, "and its pending loot table, rolled as it breaks (" + std::to_string(loot) + " items of gf_test:chests/fixture_chest)");
    // a solid block on top: no screen
    auto c2 = at(p, 0, 0, 2);
    ctx.require(ctx.setBlock(c2, "gf_test:family_chest", {{kFacing, "north"}}), "another chest");
    ctx.require(ctx.setBlock(at(c2, 0, 1, 0), "minecraft:stone"), "stone on it");
    co_await ctx.ticks(1);
    {
        CompanionGuard companion(ctx);
        ctx.check(co_await click(ctx, c2, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, ""), "a click on the covered chest");
        co_await ctx.ticks(1);
        ctx.check(openedScreen(ctx, "inventory.chest").is_null(), "a chest under a solid block does not open");
        ctx.setBlock(at(c2, 0, 1, 0), "minecraft:air");
        co_await ctx.ticks(1);
        ctx.check(co_await click(ctx, c2, Facing::North, Vec3{0.5f, 0.5f, 0.0f}, ""), "a click on the free chest");
        co_await ctx.ticks(1);
        ctx.check(!openedScreen(ctx, "inventory.chest").is_null(), "the free chest opens");
        closeScreen(ctx, "inventory.chest");
    }
    co_await ctx.ticks(12);
    (void)box;
});
GF_REGISTER_SELFTEST("families.hanging_chain", [](Context& ctx) -> ll::coro::CoroTask<> {
    // CeilingHangingSignBlock / WallHangingSignBlock.shouldTryToChainAnotherHangingSign: a hanging
    // sign item on the bottom of a ceiling hanging sign, or on a side of a wall hanging sign that is
    // not its text, hangs another sign below (the sign under a hanging sign survives: its support
    // shape); on the text side the sign takes the click
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6192, 5);
    auto p   = ctx.testPos(6192, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto top = at(p, 1, 3, 0), sign = at(p, 1, 2, 0);
    ctx.require(ctx.setBlock(top, "minecraft:stone"), "a ceiling");
    ctx.require(ctx.setBlock(sign, "gf_test:family_hanging_sign", {{fam::state::Rotation, 8}}), "a hanging sign under it");
    co_await ctx.ticks(2);
    auto below = co_await place(ctx, sign, Facing::Down, Vec3{0.5f, 0.0f, 0.5f}, "gf_test:family_hanging_sign");
    co_await ctx.ticks(3);
    ctx.check(below && *below == at(sign, 0, -1, 0) && ctx.blockAt(*below) == "gf_test:family_hanging_sign",
              "a hanging sign item on the bottom of a hanging sign hangs another below it (" + (below ? ctx.blockAt(*below) + " at " + where(*below) : std::string("nothing placed")) + ")");
    if (below) ctx.cleanupBlock(*below);
    auto side = co_await place(ctx, sign, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "gf_test:family_hanging_sign");
    co_await ctx.ticks(2);
    ctx.check(!side || ctx.blockAt(*side) != "gf_test:family_hanging_sign", "on its text side the sign takes the click");
    if (side) ctx.cleanupBlock(*side);
    // a wall hanging sign facing north hangs between two blocks; its bottom is not a text side
    auto w = at(p, 3, 2, 0);
    ctx.require(ctx.setBlock(at(w, -1, 0, 0), "minecraft:stone") && ctx.setBlock(at(w, 1, 0, 0), "minecraft:stone"), "two blocks to hang on");
    ctx.require(ctx.setBlock(w, "gf_test:family_wall_hanging_sign", {{kFacing, "north"}}), "a wall hanging sign between them");
    co_await ctx.ticks(2);
    auto underWall = co_await place(ctx, w, Facing::Down, Vec3{0.5f, 0.0f, 0.5f}, "gf_test:family_hanging_sign");
    co_await ctx.ticks(3);
    ctx.check(underWall && ctx.blockAt(*underWall) == "gf_test:family_hanging_sign", "a wall hanging sign chains a sign below too (" + (underWall ? ctx.blockAt(*underWall) : std::string("nothing placed")) + ")");
    if (underWall) ctx.cleanupBlock(*underWall);
    (void)box;
});

GF_REGISTER_SELFTEST("families.tf_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TF's own blocks through the kit (F1's skeleton + tools/families): a snowy berry bush keeps
    // TF's shape per age and layers (TFBushBlock: a grown bush collides as a full block, snow or
    // not), the chests are containers with Java's stats, the magic leaves' colours by position
    // (ColorHandler, the client mod's tint), Java's sign line width shared with the editor
    auto const* bush = gf::game::blockWith("twilightforest:blackberry_bush");
    if (!fam::entryOf("twilightforest:blackberry_bush") || !bush) {
        ctx.info("no TF family blocks in this build: skipped");
        co_return;
    }
    auto box = co_await ctx.flatArea(6196, 4);
    auto p   = ctx.testPos(6196, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  top    = [&](GlobalPos const& q) {
        auto const*       b = fam::blockAt(q);
        std::vector<AABB> boxes;
        if (b) b->getBlockType().addCollisionShapes(*b, region, gf::game::blockPos(q), nullptr, boxes, {});
        float t = 0;
        for (auto const& bx : boxes) t = std::max(t, bx.max.y - q.y);
        return t;
    };
    auto b1 = at(p, 0, 0, 0);
    ctx.require(ctx.setBlock(b1, "twilightforest:blackberry_bush", {{"twilightforest:age", 3}, {fam::state::Layers, 1}}), "a grown blackberry bush with one snow layer");
    co_await ctx.ticks(2);
    ctx.check(std::fabs(top(b1) - 1.0f) < 0.01f, "it collides as a full block (TFBushBlock, " + std::to_string(top(b1)) + ")");
    ctx.require(ctx.setBlock(b1, "twilightforest:blackberry_bush", {{"twilightforest:age", 0}, {fam::state::Layers, 0}}), "a young bush without snow");
    co_await ctx.ticks(1);
    ctx.check(std::fabs(top(b1) - 0.5f) < 0.01f, "a young bush collides 8 px high (" + std::to_string(top(b1)) + ")");
    // the chests
    auto ch = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(ch, "twilightforest:canopy_trapped_chest", {{kFacing, "north"}}), "a canopy trapped chest");
    co_await ctx.ticks(1);
    ctx.check(gf::inventory::find(region, gf::game::blockPos(ch)).has_value(), "TF's chests are containers of the inventory contract");
    auto declared = containerDeclarations("families.json");
    ctx.check(declared.value("twilightforest:canopy_trapped_chest", fam::Json{}).value("stat", "") == "minecraft:trigger_trapped_chest"
                  && declared.value("twilightforest:canopy_chest", fam::Json{}).value("stat", "") == "minecraft:open_chest",
              "TF's chests are declared with Java's opening stats");
    ctx.check(st(ch, fam::state::Lid) == 0 && st(ch, fam::state::OpenCount) == 0, "a TF trapped chest has the lid and the signal");
    // ColorHandler's position colours
    namespace lc = fam::leafcolors;
    ctx.check(lc::time(0, 0, 0) == 0x6A9C17 && lc::time(16, 0, 0) == 0xFB6C1B, "time leaves fade from (106, 156, 23) to (251, 108, 27)");
    ctx.check(lc::transformation(0, 0, 0) == 0x6CCCEA && lc::mining(0, 0, 0) == 0xFCF144 && lc::sorting(0, 0, 0) == 0x364C03, "the other magic leaves' spring colours");
    ctx.check(lc::rainbow(0, 0, 0) == 0x00FF00 && lc::rainbow(8, 0, 0) == 0xFFFF80, "rainbow oak's colours (" + std::to_string(lc::rainbow(8, 0, 0)) + ")");
    ctx.check(lc::of("twilightforest:time_leaves").has_value() && !lc::of("twilightforest:canopy_leaves").has_value(), "only the position-coloured leaves");
    ctx.check(gf::game::blockWith("twilightforest:time_leaves") != nullptr, "time leaves exist");
    // CANOPY_COLORIZER / MANGROVE_COLORIZER of the biome foliage (here FoliageColor's default)
    ctx.check(lc::canopy(0x48B518) == 0x47A73F && lc::mangrove(0x48B518) == 0x84CD56 && lc::canopy(0xFF48B518) == 0x47A73F,
              "canopy and mangrove leaves blend the foliage colour (" + std::to_string(lc::canopy(0x48B518)) + ", " + std::to_string(lc::mangrove(0x48B518)) + ")");
    auto canopyBlend = lc::blendOf("twilightforest:canopy_leaves");
    auto mangroveBlend = lc::blendOf("twilightforest:mangrove_leaves");
    ctx.check(canopyBlend && mangroveBlend && canopyBlend->fn == &lc::canopy && mangroveBlend->fn == &lc::mangrove && !lc::blendOf("twilightforest:twilight_oak_leaves").has_value(),
              "only canopy and mangrove leaves blend");
    // without a world (or position) ColorHandler gives FoliageColor.getEvergreenColor / getBirchColor,
    // without the colorizer
    ctx.check(canopyBlend && mangroveBlend && canopyBlend->withoutWorld == 0x619961 && mangroveBlend->withoutWorld == 0x80A755,
              "without a world canopy leaves are evergreen and mangrove leaves birch coloured");
    // the sign editor's line width (SignFont.h, the same table on both sides)
    namespace sf = fam::signfont;
    ctx.check(sf::lineWidth("Twilight") == 6 + 6 + 2 + 3 + 2 + 6 + 6 + 4 && sf::fitLine(std::string(30, 'W'), 90, 384).size() == 15u, "Java's sign line width");
    // what SignRenderer draws of a line: the first line of Font.split (word wrap at 90 px)
    ctx.check(sf::firstLine("Here lies a naga", 90) == "Here lies a naga" && sf::firstLine(std::string(20, 'W'), 90) == std::string(15, 'W'),
              "a line that fits is drawn whole, a long word is cut at 90 px");
    ctx.check(sf::firstLine("WWWWWWWWWW WWWWWWWW", 90) == "WWWWWWWWWW" && sf::firstLine("ab\ncd", 90) == "ab" && sf::firstLine("\xC2\xA7" "cWWWWWWWWWWWWWWW", 90) == "\xC2\xA7" "cWWWWWWWWWWWWWWW",
              "a long line breaks at its last space, at a line break, and formatting codes have no width (" + sf::firstLine("WWWWWWWWWW WWWWWWWW", 90) + ")");
    ctx.check(sf::firstLine(std::string(12, 'W'), 60) == std::string(10, 'W'), "a hanging sign's line is 60 px");
    (void)box;
});

GF_REGISTER_SELFTEST("families.leaves_waterlogged", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java LeavesBlock.randomTick: decaying leaves dropResources then level.removeBlock(pos, false),
    // which puts the block's fluid in its place: waterlogged leaves leave their water source (TF's
    // leaves are waterloggable: F1's params.traits)
    if (!fam::entryOf("twilightforest:twilight_oak_leaves") || !gf::game::blockWith("twilightforest:twilight_oak_leaves")) {
        ctx.info("no TF leaves in this build: skipped");
        co_return;
    }
    auto box = co_await ctx.flatArea(6182, 5);
    auto p   = ctx.testPos(6182, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = *ctx.region();
    auto  wet    = [&region](GlobalPos const& q) {
        auto const& extra = region.getExtraBlock(gf::game::blockPos(q)).getTypeName();
        return extra == "minecraft:water" || extra == "minecraft:flowing_water";
    };
    auto const* water = gf::game::blockWith("minecraft:water");
    ctx.require(water != nullptr, "water");
    auto w = at(p, 2, 0, 0), dry = at(p, 2, 0, 2);
    // a basin of stone around the waterlogged leaves, so the water can't flow away
    for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) ctx.require(ctx.setBlock(at(w, dx, 0, dz), "minecraft:stone"), "the basin");
    ctx.require(ctx.setBlock(w, "twilightforest:twilight_oak_leaves", {{fam::state::Distance, 7}, {fam::state::Persistent, false}}), "leaves far from any log");
    ctx.require(ctx.setBlock(dry, "twilightforest:twilight_oak_leaves", {{fam::state::Distance, 7}, {fam::state::Persistent, false}}), "dry leaves far from any log");
    region.setExtraBlock(gf::game::blockPos(w), *water, 3);
    co_await ctx.ticks(3);
    ctx.require(ctx.blockAt(w) == "twilightforest:twilight_oak_leaves" && wet(w), "the leaves are waterlogged (water in their liquid layer)");
    fam::detail::randomTickAt(region, gf::game::blockPos(w));
    fam::detail::randomTickAt(region, gf::game::blockPos(dry));
    co_await ctx.ticks(2);
    auto const* left = fam::blockAt(w);
    ctx.check(left && left->getTypeName() == "minecraft:water", "the waterlogged leaves decayed into their water (" + ctx.blockAt(w) + ")");
    ctx.check(left && fam::intState(*left, "liquid_depth").value_or(-1) == 0, "a water source, as Java's removeBlock leaves it");
    ctx.check(!wet(w), "no water left in the liquid layer");
    ctx.check(ctx.blockAt(dry) == "minecraft:air", "dry leaves decay into air (" + ctx.blockAt(dry) + ")");
    for (auto* item : itemsNear(ctx, w)) item->remove();
    for (auto* item : itemsNear(ctx, dry)) item->remove();
    (void)box;
});

GF_REGISTER_SELFTEST("families.sign_sync", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the client mod draws the sign text itself (SignRenderer): the server sends companions the
    // text of a sign when it changes (the changes of a tick in one message at its end), "gone" when
    // it goes, everything when they join; vanilla players get nothing (they read the display
    // entity's name tag)
    if (!fixturesReady(ctx)) co_return;
    auto box = co_await ctx.flatArea(6182, 5);
    auto p   = ctx.testPos(6184, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(ctx.setBlock(at(p, 0, -1, 0), "minecraft:stone"), "ground");
    ctx.require(ctx.setBlock(p, "gf_test:family_sign", {{fam::state::Rotation, 8}}), "a sign");
    auto q = at(p, 2, 0, 0);
    ctx.require(ctx.setBlock(at(q, 0, -1, 0), "minecraft:stone"), "ground 2");
    ctx.require(ctx.setBlock(q, "gf_test:family_sign", {{fam::state::Rotation, 8}}), "a second sign");
    co_await ctx.ticks(1);
    {
        // a vanilla player (no client mod) reads the display entity's name tag; that it is sent no
        // sign text rests on net::send's refusal (this check only sees what a regression that marked
        // it a companion would record), and the client scenario checks it on a real host
        gf::net::setTestCompanion(ctx.player(), false);
        (void)gf::net::takeTestMessages(ctx.player());
        gf::signs::setText(q, {"vanilla"});
        co_await ctx.ticks(1);
        bool none = true;
        for (auto const& m : gf::net::takeTestMessages(ctx.player())) none = none && m.channel != "families.sign_text";
        ctx.check(!gf::net::isCompanion(ctx.player()) && none, "a vanilla player is no companion and records no sign text");
        auto* display = fam::detail::signDisplay(q, true);
        ctx.check(display && display->getNameTag() == "vanilla\n\n\n", "it reads the text on the display entity's name tag");
    }
    CompanionGuard guard(ctx);
    auto texts = [&ctx] {
        std::vector<fam::Json> out;
        for (auto const& m : gf::net::takeTestMessages(ctx.player()))
            if (m.channel == "families.sign_text") out.push_back(m.payload);
        return out;
    };
    auto namesAt = [](fam::Json const& msg, char const* list, GlobalPos const& where) {
        for (auto const& e : msg.value(list, fam::Json::array())) {
            auto const& pos = e.is_array() ? e : e.value("pos", fam::Json());
            if (pos == fam::Json::array({where.dim, where.x, where.y, where.z})) return true;
        }
        return false;
    };
    auto names = [&](fam::Json const& msg, char const* list) { return namesAt(msg, list, p); };
    gf::signs::setText(p, {"sync", "me"});
    ctx.check(texts().empty(), "nothing is sent before the end of the tick");
    co_await ctx.ticks(1);
    auto sent = texts();
    ctx.check(sent.size() == 1 && names(sent[0], "signs"), "a companion is sent the new text (" + std::to_string(sent.size()) + " messages)");
    if (!sent.empty() && !sent[0].value("signs", fam::Json::array()).empty()) {
        auto const& e = sent[0]["signs"][0];
        ctx.check(e["front"]["lines"][0] == "sync" && e["front"]["color"] == "black" && e["front"]["glowing"] == false && e["back"]["lines"][0] == "",
                  "with both sides' lines, colour and glow");
        // the client mod hides the name tags of exactly the sign's display entities: named by id
        auto*        display = fam::detail::signDisplay(p, true);
        std::int64_t id      = display ? display->getOrCreateUniqueID().rawID : 0;
        ctx.check(display && e.value("display", fam::Json::array()) == fam::Json::array({id}),
                  "and the unique id of its display entity (" + e.value("display", fam::Json::array()).dump() + ", " + std::to_string(id) + ")");
    }
    gf::signs::setText(p, {"sync", "me"});
    co_await ctx.ticks(1);
    ctx.check(texts().empty(), "the same text is not sent again");
    gf::signs::setText(p, {"back"}, false);
    co_await ctx.ticks(1);
    sent = texts();
    ctx.check(sent.size() == 1 && names(sent[0], "signs") && sent[0]["signs"][0]["back"]["lines"][0] == "back", "a change of the back is sent");
    ctx.check(fam::detail::signSentText(p).value("front", fam::Json()).value("lines", fam::Json::array()) == fam::Json::array({"sync", "me", "", ""}), "the server remembers what it sent");
    fam::detail::signSendAll(ctx.player());
    sent = texts();
    ctx.check(!sent.empty() && names(sent[0], "signs"), "a companion that joins gets the text of the signs");
    // two signs written in one tick: one message with both
    gf::signs::setText(p, {"one", "tick"});
    gf::signs::setText(q, {"same", "tick"});
    co_await ctx.ticks(1);
    sent = texts();
    ctx.check(sent.size() == 1 && names(sent[0], "signs") && namesAt(sent[0], "signs", q), "the changes of one tick go in one message (" + std::to_string(sent.size()) + " messages)");
    // a sign whose chunk unloads is withdrawn from the companions (its load sends it again), so the
    // client mod's list keeps only the signs of loaded chunks
    fam::detail::signForgetUnloaded(q);
    co_await ctx.ticks(1);
    sent = texts();
    ctx.check(sent.size() == 1 && namesAt(sent[0], "gone", q) && !namesAt(sent[0], "gone", p) && fam::detail::signSentText(q).is_null(),
              "a sign whose chunk unloads is gone for the companions (" + std::to_string(sent.size()) + " messages)");
    ctx.setBlock(q, "minecraft:air");
    ctx.setBlock(at(q, 0, -1, 0), "minecraft:air");
    ctx.setBlock(p, "minecraft:air");
    co_await ctx.ticks(2);
    sent = texts();
    bool gone = false;
    for (auto const& m : sent) gone = gone || names(m, "gone");
    ctx.check(gone, "a broken sign's text is gone for the companions");
    ctx.check(fam::detail::signSentText(p).is_null(), "and the server forgets it");
    (void)box;
});

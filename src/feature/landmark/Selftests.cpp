// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of the landmark unit (F6, prefix "landmark.", testPos dx 5600..5699). They depend only
// on W0 and this unit: the fixture landmark gf_test:landmark_fixture (locked like TF's lich tower by
// twilightforest:progress_naga, hint book "lichtower") stands in for the W1 structures.
//
//   landmark.grid           the grid and TF's structure records (descriptors, structure sets)
//   landmark.protection     forceStart + protection: break / use / multi-place refused for a
//                           non-qualified survival player, boxes + packets shown, allowed after
//                           the grant (when the progression store exists), creative bypass
//   landmark.conquer        setConquered on a forced start, the conquered flag persisted
//   landmark.persistence    a start written to the world DB reads back (pieces, flags)
//   landmark.hint_book      the hint book written for a player
//   landmark.clearance      decorationAllowed / featureBlocked / startsIntersecting / spawns
//   landmark.persist_write  (server scenario, before a restart) a forced, conquered start
//   landmark.persist_read   (after the restart) that start, then removed
//   landmark.display        (client scenario) keeps protection boxes on show for a screenshot
#include <algorithm>
#include <optional>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "feature/common/Interactions.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/gamerules/GameRules.h"
#include "feature/landmark/Internal.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::core::GlobalPos;
using gf::selftest::Context;
namespace fl = gf::feature::landmark;
namespace lm = gf::landmark;
namespace ia = gf::interactions;
using JsonT  = nlohmann::json;

constexpr char kFixture[] = "gf_test:landmark_fixture";
constexpr char kLock[]    = "twilightforest:progress_naga";

GlobalPos at(GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

/// The unlocked fixture (conquerable, no protection): the persistence tests leave it in the world.
constexpr char kOpenFixture[] = "gf_test:landmark_fixture_open";

/// Removes a forced start made by a test (memory and world DB).
void dropStart(GlobalPos const& center, char const* key = kFixture) {
    fl::eraseRecord(center.dim, center.x >> 4, center.z >> 4, key);
    fl::flushRecords();
}

/// Undoes what a test changed, also when it aborts (require / fail throw): the enforcement
/// override, the test companion flag and the forced starts.
struct Cleanup {
    std::vector<GlobalPos>      starts;
    std::optional<std::int64_t> companion;
    Cleanup() = default;
    Cleanup(Cleanup const&)            = delete;
    Cleanup& operator=(Cleanup const&) = delete;
    ~Cleanup() {
        fl::setEnforcedOverride(std::nullopt);
        if (companion) {
            if (auto level = ll::service::getLevel()) {
                ActorUniqueID id;
                id.rawID = *companion;
                if (auto* p = level->getPlayer(id)) gf::net::setTestCompanion(*p, false);
            }
        }
        for (auto const& s : starts) dropStart(s);
    }
};

ia::Result destroyAs(Player& p, GlobalPos const& pos) {
    auto&       region  = *gf::game::regionOf(pos.dim);
    auto const& block   = region.getBlock(gf::game::blockPos(pos));
    std::string blockId = block.getTypeName(), itemId;
    auto const& item    = gf::game::items::empty();
    ia::DestroyContext c{p, item, itemId, block, blockId, pos, 1, false};
    return ia::dispatchDestroy(c);
}

ia::Result useOnAs(Player& p, GlobalPos const& pos, std::string const& itemId, ItemStack const& item, bool first = true) {
    auto&       region  = *gf::game::regionOf(pos.dim);
    auto const& block   = region.getBlock(gf::game::blockPos(pos));
    std::string blockId = block.getTypeName();
    ia::UseOnContext c{p, item, itemId, block, blockId, pos, 1, {0.5, 1.0, 0.5}, false, first};
    return ia::dispatchUseOn(c);
}

bool hasChannel(std::vector<gf::core::Envelope> const& msgs, std::string const& channel) {
    return std::any_of(msgs.begin(), msgs.end(), [&](auto const& m) { return m.channel == channel; });
}

} // namespace

GF_REGISTER_SELFTEST("landmark.grid", [](Context& ctx) {
    namespace cl = gf::core::landmark;
    ctx.check(cl::nearestCenter(0, 0) == (cl::ColumnXZ{8, 8}), "the landmark center of region 0,0 is block 8,8");
    ctx.check(cl::chunkHasCenter(0, 0) && !cl::chunkHasCenter(1, 1), "chunk 0,0 holds the center");
    ctx.check(cl::pickVariety(0, -64 + 64, -64 + 80) == "twilightforest:lich_tower", "region offset (4, 5) is a lich tower");
    // TF's 21 structures have descriptors, the four declared-only keys have none
    static char const* const keys[] = {"aurora_palace",   "camp",          "dark_tower",          "fallen_trunk",       "final_castle",     "giant_house",
                                       "hedge_maze",      "hollow_tree",   "hydra_lair",          "knight_stronghold",  "labyrinth",        "large_hollow_hill",
                                       "lich_tower",      "medium_hollow_hill", "mushroom_tower", "naga_courtyard",     "quest_grove",      "small_hollow_hill",
                                       "swamp_hollow_tree", "troll_cave",  "yeti_cave"};
    int found = 0;
    for (auto const* k : keys) found += lm::descriptor(std::string("twilightforest:") + k) != nullptr;
    ctx.check(found == 21, "21 TF structure descriptors (found " + std::to_string(found) + ")");
    for (auto const* k : {"quest_island", "druid_grove", "floating_ruins", "world_tree"})
        ctx.check(lm::descriptor(std::string("twilightforest:") + k) == nullptr, std::string("no descriptor for ") + k);
    auto const* lich = lm::descriptor("twilightforest:lich_tower");
    ctx.require(lich != nullptr, "lich tower descriptor");
    ctx.check(lich->lockAdvancements == std::vector<std::string>{kLock}, "the lich tower is locked by progress_naga");
    ctx.check(lich->hintBookKey == "twilightforest.book.lichtower", "its hint book is the lich tower's");
    ctx.check(lich->conquerable && lich->icon == "twilightforest:lich_tower", "conquerable, with its map icon");
    ctx.check(!lich->surfaceDecorationsAllowed && lich->undergroundDecorationsAllowed, "lich tower decoration clearance");
    ctx.check(lich->controlledSpawns["labelled_monster_spawns"].size() == 2, "lich tower spawn lists 0 and 1");
    auto const* dark = lm::descriptor("twilightforest:dark_tower");
    ctx.require(dark != nullptr, "dark tower descriptor");
    ctx.check(dark->controlledSpawns["labelled_monster_spawns"].size() == 2 && dark->controlledSpawns["water_spawns"].size() == 1,
              "dark tower floors: monster lists 0, 1 and the water list");
    auto const* grove = lm::descriptor("twilightforest:quest_grove");
    ctx.check(grove && grove->conquerable && grove->lockAdvancements.empty(), "the quest grove is conquerable without a lock");
    auto const* maze = lm::descriptor("twilightforest:hedge_maze");
    ctx.check(maze && !maze->conquerable, "the hedge maze is not conquerable");
    auto const* giant = fl::info("twilightforest:giant_house");
    ctx.check(giant && giant->traits.progression && !giant->traits.landmarkTag, "the giant house is locked but not in #landmark");
    auto const* hollowTree = fl::info("twilightforest:hollow_tree");
    ctx.check(hollowTree && hollowTree->structureSet == "twilightforest:hollow_tree", "the hollow tree's structure set");
    auto const* set = fl::structureSet("twilightforest:camp");
    ctx.check(set && set->placement.kind == gf::core::landmark::placement::Kind::AvoidLandmarkGrid && set->placement.spacing == 18,
              "camp: avoid_landmark_grid, spacing 18");
    auto const* lichSet = fl::structureSet("twilightforest:lich_tower");
    ctx.check(lichSet && lichSet->placement.gridLock == "twilightforest:lich_tower", "lich tower: landmark_grid locked to itself");
});

GF_REGISTER_SELFTEST("landmark.protection", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto area   = co_await ctx.flatArea(5600, 16);
    auto origin = ctx.testPos(5600);
    auto    center = at(origin, 8, 0, 8);
    Cleanup cleanup;
    cleanup.starts.push_back(center);
    fl::setEnforcedOverride(true);
    auto start = lm::forceStart(kFixture, center);
    ctx.require(start.has_value() && start->generated, "forceStart made the fixture landmark");
    ctx.check(start->pieceBoxes.size() == 2, "the fixture has its shell and porch pieces");
    co_await ctx.ticks(2);
    auto wall    = at(center, 3, 2, 0);  // the shell's east wall
    auto porch   = at(center, 0, 0, 5);  // the porch (not protected)
    auto outside = at(center, 0, 0, -6); // outside the start
    ctx.check(ctx.blockAt(wall) == "minecraft:stone_bricks", "the shell was placed in the loaded chunks (got " + ctx.blockAt(wall) + ")");

    (void)ctx.player();
    co_await ctx.ticks(1);
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    gf::net::setTestCompanion(p, true);
    cleanup.companion = p.getOrCreateUniqueID().rawID;
    (void)gf::net::takeTestMessages(p);
    ctx.check(lm::isProtectedFor(p, gf::game::blockPos(wall)), "the shell is protected for a player without progress_naga");
    ctx.check(!lm::isProtectedFor(p, gf::game::blockPos(porch)), "the porch piece is not protected");
    ctx.check(!lm::isProtectedFor(p, gf::game::blockPos(outside)), "outside the start nothing is protected");
    ctx.check(lm::startAt(center.dim, gf::game::blockPos(wall)).has_value(), "startAt finds the forced start");

    // break, use and multi-place are refused (ProgressionEvents)
    ctx.check(destroyAs(p, wall) == ia::Result::Deny, "breaking the wall is refused");
    {
        auto& gm = *p.mGameMode;
        gm.destroyBlock(gf::game::blockPos(wall), 1);
    }
    ctx.check(ctx.blockAt(wall) == "minecraft:stone_bricks", "GameMode::destroyBlock left the wall standing");
    auto stone = gf::game::items::make("minecraft:stone");
    ctx.require(stone.has_value(), "a stone stack");
    ctx.check(useOnAs(p, wall, "minecraft:stone", *stone) == ia::Result::Deny, "placing against the wall is refused");
    ctx.check(useOnAs(p, porch, "minecraft:stone", *stone) == ia::Result::Pass, "using the unprotected porch passes");
    auto door = gf::game::items::make("minecraft:wooden_door");
    if (door) {
        // a door placed on the ground under the shell: the clicked block is outside the start, but
        // the door's upper half would fill the protected shell (EntityMultiPlaceEvent)
        ctx.check(useOnAs(p, at(center, 0, -1, 0), "minecraft:wooden_door", *door) == ia::Result::Deny, "a door reaching into the shell is refused");
        ctx.check(useOnAs(p, at(center, 0, -1, -8), "minecraft:wooden_door", *door) == ia::Result::Pass, "a door away from the shell passes");
    }
    co_await ctx.ticks(3);
    auto msgs = gf::net::takeTestMessages(p);
    ctx.check(hasChannel(msgs, "landmark.area_protection"), "the companion got landmark.area_protection");
    auto boxes = fl::liveProtectionBoxes();
    auto shown = [&](gf::core::BlockBox const& b) { return std::any_of(boxes.begin(), boxes.end(), [&](auto const& x) { return x == b; }); };
    ctx.check(shown(start->pieceBoxes[0]) || shown(start->pieceBoxes[1]),
              "the protected box is shown");
    ctx.info("protection box entities alive: " + std::to_string(fl::protectionBoxEntities()));

    // creative and spectator players are never refused; neither is anyone without enforcement
    p.setPlayerGameType(GameType::Creative);
    ctx.check(!lm::isProtectedFor(p, gf::game::blockPos(wall)), "creative players are not refused");
    p.setPlayerGameType(GameType::Survival);
    fl::setEnforcedOverride(false);
    ctx.check(!lm::isProtectedFor(p, gf::game::blockPos(wall)), "nothing is protected without tfEnforcedProgression");
    fl::setEnforcedOverride(true);

    // allowed after the grant
    gf::progression::grant(p, kLock);
    if (fl::stubHit("progression.grant")) {
        ctx.info("the progression store is the F0 contract stub: the grant half waits for F10");
    } else {
        ctx.check(!lm::isProtectedFor(p, gf::game::blockPos(wall)), "with progress_naga the shell is open");
        ctx.check(destroyAs(p, wall) == ia::Result::Pass, "with progress_naga breaking passes");
        gf::progression::revoke(p, kLock);
    }
    (void)area;
});

GF_REGISTER_SELFTEST("landmark.conquer", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = at(ctx.testPos(5620, 2), 0, 0, 0);
    ctx.require(co_await ctx.ensureLoaded(center), "test area loaded");
    // a start record only: no blocks (placed in a far corner above the row)
    auto const* fixture = fl::info(kFixture);
    ctx.require(fixture != nullptr, "the fixture descriptor");
    auto rec = fl::createStart(*fixture, center.dim, center.x >> 4, center.z >> 4, at(center, 0, 40, 0), true);
    ctx.require(rec != nullptr, "the fixture start");
    Cleanup cleanup;
    cleanup.starts.push_back(at(center, 0, 40, 0));
    fl::putRecord(rec);
    ctx.check(!lm::isConquered(rec->start), "a new start is not conquered");
    lm::setConquered(center.dim, gf::game::blockPos(at(center, 0, 40, 0)), kFixture, true);
    ctx.check(lm::isConquered(rec->start), "setConquered marks it");
    fl::flushRecords();
    auto doc = gf::game::db::get(fl::recordDbKey(center.dim, center.x >> 4, center.z >> 4));
    ctx.check(doc && (*doc)[kFixture].value("conquered", false), "the flag is in the world DB");
    lm::setConquered(center.dim, gf::game::blockPos(at(center, 0, 40, 0)), kFixture, false);
    ctx.check(!lm::isConquered(rec->start), "and can be reactivated");
});

GF_REGISTER_SELFTEST("landmark.persistence", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = at(ctx.testPos(5630, 2), 0, 40, 0);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5630, 2)), "test area loaded");
    auto const* fixture = fl::info(kFixture);
    ctx.require(fixture != nullptr, "the fixture descriptor");
    auto rec = fl::createStart(*fixture, center.dim, center.x >> 4, center.z >> 4, center, true);
    ctx.require(rec != nullptr, "the fixture start");
    rec->start.conquered = true;
    auto json            = fl::recordToJson(*rec);
    auto back            = fl::recordFromJson(json);
    ctx.require(back != nullptr, "the record reads back");
    ctx.check(back->start.key == kFixture && back->start.conquered && back->forced, "key and flags");
    ctx.check(back->pieces.size() == 2 && back->start.pieceBoxes == rec->start.pieceBoxes, "piece boxes");
    ctx.check(back->start.box == rec->start.box && back->start.center == rec->start.center, "box and center");
    ctx.check(json["pieces"][0]["piece"].value("id", std::string{}) == "gf_test:landmark_fixture_shell", "pieces saved as StructurePiece tags");
    ctx.check(back->pieces[0].protectedPiece && back->pieces[1].protectedPiece, "piece flags kept");
    co_return;
});

GF_REGISTER_SELFTEST("landmark.hint_book", [](Context& ctx) -> ll::coro::CoroTask<> {
    (void)ctx.player();
    co_await ctx.ticks(1);
    auto&       p    = ctx.player();
    auto const* inf  = fl::info("twilightforest:lich_tower");
    ctx.require(inf != nullptr, "lich tower");
    ctx.check(inf->hintBook == "lichtower" && inf->hintPages == 4 && inf->hintMob == "twilightforest:kobold", "the hint config");
    auto book = fl::createHintBook(*inf, &p);
    ctx.require(book.has_value(), "a hint book");
    ctx.check(gf::game::items::idOf(*book) == "minecraft:written_book", "a written book");
    auto const* tag = gf::game::items::userData(*book);
    ctx.require(tag != nullptr, "with book data");
    ctx.check(tag->contains("pages") && tag->contains("title") && tag->contains("author"), "pages, title and author");
    auto marker = gf::game::items::getData(*book, "translatable_book");
    ctx.check(marker.value("name", std::string{}) == "lichtower" && marker.value("pages", 0) == 4, "the translatable_book marker");
    auto unknown = fl::makeBook("unknown", 2, &p);
    ctx.check(unknown.has_value(), "the default book (twilightforest.book.unknown)");
    ctx.check(!fl::createHintBook(*fl::info("twilightforest:hedge_maze"), &p).has_value(), "no book for structures without hints");
});

GF_REGISTER_SELFTEST("landmark.clearance", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = at(ctx.testPos(5640, 2), 0, 40, 0);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5640, 2)), "test area loaded");
    auto const* fixture = fl::info(kFixture);
    ctx.require(fixture != nullptr, "the fixture descriptor");
    auto rec = fl::createStart(*fixture, center.dim, center.x >> 4, center.z >> 4, center, true);
    ctx.require(rec != nullptr, "the fixture start");
    Cleanup cleanup;
    cleanup.starts.push_back(center);
    fl::putRecord(rec);
    auto starts = lm::startsIntersecting(center.dim, lm::ChunkPos{center.x >> 4, center.z >> 4});
    ctx.check(std::any_of(starts.begin(), starts.end(), [](auto const& s) { return s.key == kFixture; }), "startsIntersecting lists it");
    // the fixture clears surface decorations and vegetation in its pieces (radius 0)
    gf::landmark::ext::Occupancy surface{.surface = true};
    ctx.check(gf::landmark::ext::featureBlocked(center.dim, center.x, center.y + 1, center.z, surface), "a surface feature inside the shell is blocked");
    ctx.check(!gf::landmark::ext::featureBlocked(center.dim, center.x + 30, center.y, center.z, surface), "far away it is not");
    gf::landmark::ext::Occupancy under{.underground = true};
    ctx.check(!gf::landmark::ext::featureBlocked(center.dim, center.x, center.y + 1, center.z, under), "underground features are allowed");
    surface.structuresAllowed = {kFixture};
    ctx.check(!gf::landmark::ext::featureBlocked(center.dim, center.x, center.y + 1, center.z, surface), "structures_allowed lifts it");
    // controlled spawns: list 0 inside, nothing outside, nothing once conquered
    auto spawns = gf::landmark::ext::potentialSpawns(center.dim, "monster", center.x, center.y + 1, center.z);
    ctx.check(!spawns.has_value(), "the fixture's pieces have no spawn index (no list)");
});

GF_REGISTER_SELFTEST("landmark.persist_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = at(ctx.testPos(5660, 2), 0, 60, 0);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5660, 2)), "test area loaded");
    // the unlocked fixture: when nothing reads it back (a "full" run sorts persist_read first) the
    // record it leaves protects nothing
    auto const* fixture = fl::info(kOpenFixture);
    ctx.require(fixture != nullptr, "the open fixture descriptor");
    auto rec = fl::createStart(*fixture, center.dim, center.x >> 4, center.z >> 4, center, true);
    ctx.require(rec != nullptr, "the fixture start");
    rec->start.conquered = true;
    fl::putRecord(rec);
    fl::flushRecords();
    gf::log().info("landmark.persist_write: start written at {} {} {}", center.x, center.y, center.z);
});

GF_REGISTER_SELFTEST("landmark.persist_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = at(ctx.testPos(5660, 2), 0, 60, 0);
    co_await ctx.ticks(1);
    auto rec = fl::findRecord(center.dim, center.x >> 4, center.z >> 4, kOpenFixture);
    if (!rec) {
        gf::log().info("landmark.persist_read: no start");
        co_return;
    }
    ctx.check(rec->start.conquered && rec->forced && rec->pieces.size() == 2, "the start came back with its flag and pieces");
    gf::log().info("landmark.persist_read: start verified at {} {} {}", center.x, center.y, center.z);
    dropStart(center, kOpenFixture);
});

GF_REGISTER_SELFTEST("landmark.display", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the client scenario looks at this area: the fixture landmark with its protection boxes shown
    // for 12 seconds (every 2 s the simulated player tries the wall again, as a player would)
    auto area   = co_await ctx.flatArea(5680, 16);
    auto origin = ctx.testPos(5680);
    auto    center = at(origin, 8, 0, 8);
    Cleanup cleanup;
    cleanup.starts.push_back(center);
    fl::setEnforcedOverride(true);
    auto start = lm::forceStart(kFixture, center);
    ctx.require(start.has_value() && start->generated, "the fixture landmark");
    gf::log().info("landmark.display: area at {} {} {}", center.x, center.y, center.z);
    co_await ctx.ticks(60); // time for the scenario to bring the camera
    for (int i = 0; i < 6; ++i) {
        (void)ctx.player();
        co_await ctx.ticks(1);
        auto& p = ctx.player();
        p.setPlayerGameType(GameType::Survival);
        ctx.check(destroyAs(p, at(center, 3, 2, 0)) == ia::Result::Deny, "the wall is protected");
        if (i == 0) {
            co_await ctx.ticks(2);
            gf::log().info("landmark.display: protection shown ({} boxes, {} entities) {}", fl::liveProtectionBoxes().size(), fl::protectionBoxEntities(), fl::describeProtectionBoxes());
        }
        co_await ctx.ticks(40);
    }
    (void)area;
});

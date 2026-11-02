// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of the landmark unit (F6, prefix "landmark.", testPos dx 5600..5699). They depend only
// on W0 and this unit: the fixture landmark gf_test:landmark_fixture (locked like TF's lich tower by
// twilightforest:progress_naga, hint book "lichtower") stands in for the W1 structures.
//
//   landmark.grid             the grid and TF's structure records (descriptors with every TF field,
//                             structure sets, the decoration step indexes, stable over a reload),
//                             entity names, the layouts' context random
//   landmark.decoration       Java's decoration slots and seeds of the 21 TF structures (registry,
//                             worldgen context, loaded placement), the core's structure table
//                             against the worldgen's registry entry by entry, the decoration step
//                             mask, the fixtures' slots past the end of their step, and the
//                             landmark.pieces structure step placing a start at its step only with
//                             its structure's random
//   landmark.biomes           every landmark's biome check against TF's biome tags (tags contract)
//   landmark.commands         TF's literals, argument shapes and texts of center / conquer
//                             [reactivate] / genbook [<structure>] (+ the generatebook alias),
//                             locate's keys and structure tags (invalid, Minecraft's structures, a
//                             tag's result), and commands.generic.unknown below a literal's level
//   landmark.protection       forceStart + protection: break / use / multi-place refused for a
//                             non-qualified survival player, boxes + packets shown, allowed after
//                             progression::grant and locked again after revoke (the progression
//                             store), creative bypass, /place'd structures never protected
//   landmark.conquer          setConquered on a forced start, the conquered flag persisted; an
//                             off-grid natural start untouched; structure_cleared's player boxes
//   landmark.persistence      a start written to the world DB reads back (pieces, flags)
//   landmark.hint_book        the hint book written for a player, rewritten for another language
//   landmark.hint_drop        the hint mob carries the book and always drops it (drop chance 2.0)
//   landmark.hint_timer       the hint timer's game time advances every tick (300 ticks sampled)
//   landmark.clearance        decorationAllowed / featureBlocked / startsIntersecting / spawns, and
//                             the terraformers over every chunk a start references
//   landmark.starts           the start evaluation never blocks the server thread (a generation
//                             thread waits), locate's probe stores nothing and agrees with the
//                             Starts stage, nearestLandmark's ready check and valid starts only
//   landmark.persist_1_write  (server scenario, before a restart) a forced, conquered start
//   landmark.persist_2_read   (after the restart) that start, then removed; in a 'full' run the
//                             two run back to back and leave nothing behind
//   landmark.display          (client scenario) keeps protection boxes on show for a screenshot
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"

#include "core/landmark/Decoration.h"
#include "core/landmark/Grid.h"
#include "core/landmark/Landmark.h"
#include "feature/common/Interactions.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/worldgen/Generation.h"
#include "feature/gamerules/GameRules.h"
#include "feature/landmark/Internal.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
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
/// override, the test companion flag, the lock advancement (the progression store keeps it per
/// player in the world DB: put back as the test found it) and the forced starts.
struct Cleanup {
    std::vector<GlobalPos>      starts;
    std::optional<std::int64_t> companion;
    std::optional<std::int64_t> lockPlayer; ///< whose kLock to restore
    bool                        lockHad = false;
    Cleanup() = default;
    Cleanup(Cleanup const&)            = delete;
    Cleanup& operator=(Cleanup const&) = delete;
    /// Takes kLock from `p` for the test (restored afterwards).
    void withoutLock(Player& p) {
        lockPlayer = p.getOrCreateUniqueID().rawID;
        lockHad    = gf::progression::has(p, kLock);
        if (lockHad) gf::progression::revoke(p, kLock);
    }
    ~Cleanup() {
        fl::setEnforcedOverride(std::nullopt);
        auto level = ll::service::getLevel();
        auto find  = [&](std::int64_t raw) -> Player* {
            if (!level) return nullptr;
            ActorUniqueID id;
            id.rawID = raw;
            return level->getPlayer(id);
        };
        if (companion)
            if (auto* p = find(*companion)) gf::net::setTestCompanion(*p, false);
        if (lockPlayer)
            if (auto* p = find(*lockPlayer)) {
                if (lockHad) gf::progression::grant(*p, kLock);
                else if (gf::progression::has(*p, kLock)) gf::progression::revoke(*p, kLock);
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

/// Lifts a peaceful world to easy while a test needs a hostile mob (TF's hint kobold is a Monster:
/// peaceful discards it, as Java does), and puts the difficulty back afterwards, also on abort.
struct HostileMobsAllowed {
    using Difficulty = SharedTypes::Legacy::Difficulty;
    std::optional<Difficulty> restore;
    HostileMobsAllowed() {
        auto level = ll::service::getLevel();
        if (level && level->getDifficulty() == Difficulty::Peaceful) {
            restore = Difficulty::Peaceful;
            level->setDifficulty(Difficulty::Easy);
        }
    }
    HostileMobsAllowed(HostileMobsAllowed const&)            = delete;
    HostileMobsAllowed& operator=(HostileMobsAllowed const&) = delete;
    ~HostileMobsAllowed() {
        if (auto level = ll::service::getLevel(); level && restore) level->setDifficulty(*restore);
    }
};

bool hasChannel(std::vector<gf::core::Envelope> const& msgs, std::string const& channel) {
    return std::any_of(msgs.begin(), msgs.end(), [&](auto const& m) { return m.channel == channel; });
}

/// The start chunks a test evaluates with TF's structure sets placed in the test's own dimension
/// (setStructureDimOverride, while the Twilight Forest is the dimension contract's stub). Afterwards,
/// also on abort: the override is lifted, the chunks are released and forgotten, and every record
/// the test added there is erased (memory and world DB).
struct StartsScope {
    int                                   dim;
    std::vector<std::pair<int, int>>      chunks;
    std::vector<std::vector<std::string>> before; ///< the keys started in each chunk before the test
    explicit StartsScope(int d) : dim(d) { fl::setStructureDimOverride(d); }
    StartsScope(StartsScope const&)            = delete;
    StartsScope& operator=(StartsScope const&) = delete;
    void touch(int cx, int cz) {
        if (std::any_of(chunks.begin(), chunks.end(), [&](auto const& c) { return c.first == cx && c.second == cz; })) return;
        chunks.emplace_back(cx, cz);
        std::vector<std::string> keys;
        for (auto const& r : fl::recordsStartedIn(dim, cx, cz)) keys.push_back(r->start.key);
        before.push_back(std::move(keys));
    }
    void lift() { fl::setStructureDimOverride(std::nullopt); }
    ~StartsScope() {
        lift();
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            auto [cx, cz] = chunks[i];
            fl::setEvaluationBusy(dim, cx, cz, false);
            for (auto const& r : fl::recordsStartedIn(dim, cx, cz))
                if (std::find(before[i].begin(), before[i].end(), r->start.key) == before[i].end()) fl::eraseRecord(dim, cx, cz, r->start.key);
            fl::forgetEvaluation(dim, cx, cz);
        }
        fl::flushRecords();
    }
};

/// A thread of a test, released and joined when the test ends (also on abort).
struct TestThread {
    std::thread           thread;
    std::function<void()> release;
    TestThread() = default;
    TestThread(TestThread const&)            = delete;
    TestThread& operator=(TestThread const&) = delete;
    ~TestThread() {
        if (release) release();
        if (thread.joinable()) thread.join();
    }
};

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
    auto const* lichInfoBefore = fl::info("twilightforest:lich_tower");
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
    // Descriptor::extra: the rest of the TF record, verbatim, and what GF derives under "gf"
    auto const* hill = lm::descriptor("twilightforest:small_hollow_hill");
    ctx.require(hill != nullptr, "small hollow hill descriptor");
    ctx.check(hill->extra.value("hill_size", 0) == 1 && hill->extra.contains("speleothem_config"), "hollow hill: hill_size and speleothem_config");
    ctx.check(!hill->extra.contains("controlled_spawns") && hill->extra["gf"].value("type", std::string{}) == "twilightforest:hollow_hill",
              "parsed fields left out, derived ones under extra.gf");
    for (auto const* k : {"hydra_lair", "troll_cave", "yeti_cave", "medium_hollow_hill", "large_hollow_hill"}) {
        auto const* d = lm::descriptor(std::string("twilightforest:") + k);
        ctx.check(d && d->extra.contains("speleothem_config"), std::string(k) + ": speleothem_config");
    }
    auto const* trunk = lm::descriptor("twilightforest:fallen_trunk");
    ctx.check(trunk && trunk->extra.contains("log") && trunk->extra.contains("length") && trunk->extra.contains("big_trunk_length")
                  && trunk->extra.value("chest_loot_table", std::string{}) == "twilightforest:fallen_trunk_loot",
              "fallen trunk: log, length, big_trunk_length, chest_loot_table");
    for (auto const* k : {"hollow_tree", "swamp_hollow_tree"}) {
        auto const* d  = lm::descriptor(std::string("twilightforest:") + k);
        bool        ok = d != nullptr;
        for (auto const* f : {"height", "radius", "log", "wood", "leaves", "root", "vine", "bug", "dungeon_air", "dungeon_wood", "dungeon_monster",
                              "dungeon_loot_block", "dungeon_loot_table"})
            ok = ok && d->extra.contains(f);
        ctx.check(ok, std::string(k) + ": the hollow tree parameters");
    }
    auto const* swampTree = lm::descriptor("twilightforest:swamp_hollow_tree");
    ctx.check(swampTree && swampTree->extra.contains("allow_in_water"), "swamp hollow tree: allow_in_water");
    // CampStructure / FallenTrunkStructure hard-code their clearance in Java: it applies, but it is no
    // field of TF's record (extra keeps the record as it is; the fixed one sits under extra.gf)
    for (auto const* k : {"camp", "fallen_trunk"}) {
        auto const* d = lm::descriptor(std::string("twilightforest:") + k);
        ctx.check(d && !d->extra.contains("decoration_clearance") && d->extra["gf"].contains("fixed_decoration_clearance") && !d->surfaceDecorationsAllowed
                      && d->undergroundDecorationsAllowed,
                  std::string(k) + ": the Java clearance applies, outside the TF record");
    }
    // the descriptors load once: a second install keeps every pointer handed out
    fl::loadDescriptors();
    ctx.check(lm::descriptor("twilightforest:lich_tower") == lich && fl::info("twilightforest:lich_tower") == lichInfoBefore,
              "loading the descriptors again keeps their addresses");
    // EntityType#getDescription for /tf info's spawn list: names, not ids
    ctx.check(fl::entityName("minecraft:zombie", nullptr) == "Zombie", "minecraft:zombie is \"" + fl::entityName("minecraft:zombie", nullptr) + "\"");
    ctx.check(fl::entityName("minecraft:wither_skeleton", nullptr) == "Wither Skeleton",
              "minecraft:wither_skeleton is \"" + fl::entityName("minecraft:wither_skeleton", nullptr) + "\"");
    ctx.check(fl::entityName("twilightforest:death_tome", nullptr) == "Death Tome",
              "twilightforest:death_tome is \"" + fl::entityName("twilightforest:death_tome", nullptr) + "\"");
    // Structure.GenerationContext#random() for layouts: one deterministic source per start chunk
    {
        lm::Start s;
        s.key    = "twilightforest:lich_tower";
        s.seed   = 1234;
        s.center = {0, 100, 0, 200};
        s.data   = JsonT{{"chunk", JsonT::array({6, 12})}};
        auto a = gf::landmark::ext::contextRandom(s), b = gf::landmark::ext::contextRandom(s);
        bool same = a && b;
        for (int i = 0; same && i < 4; ++i) same = a->nextLong() == b->nextLong();
        ctx.check(same, "contextRandom: the same start gives the same sequence");
    }
    // ChunkGenerator#applyBiomeDecoration's index: every structure of the step before it in registry
    // order (path, then namespace), vanilla's included: lich_tower follows 5 vanilla and 9 TF
    // surface structures, labyrinth buried_treasure and knight_stronghold
    auto const* lichInfo = fl::info("twilightforest:lich_tower");
    auto const* labInfo  = fl::info("twilightforest:labyrinth");
    ctx.check(lichInfo && lichInfo->inRegistry && lichInfo->slot == (gf::core::landmark::decoration::Slot{4, 14}),
              "lich_tower is decoration index 14 of surface_structures");
    ctx.check(labInfo && labInfo->inRegistry && labInfo->slot == (gf::core::landmark::decoration::Slot{3, 2}),
              "labyrinth is decoration index 2 of underground_structures");
});

namespace {

/// Java's applyBiomeDecoration slot of every TF structure (GenerationStep.Decoration, index among
/// the step's structures of Minecraft 1.21.1's and TF 4.8's Structure registry in registry order;
/// tests/landmark/DecorationTest.cpp derives the same numbers).
struct JavaSlot {
    char const* key;
    int         step, index;
};
constexpr JavaSlot kJavaSlots[] = {
    {"twilightforest:knight_stronghold", 3, 1}, {"twilightforest:labyrinth", 3, 2},          {"twilightforest:troll_cave", 3, 7},
    {"twilightforest:aurora_palace", 4, 0},     {"twilightforest:camp", 4, 2},               {"twilightforest:dark_tower", 4, 3},
    {"twilightforest:fallen_trunk", 4, 6},      {"twilightforest:final_castle", 4, 7},       {"twilightforest:hedge_maze", 4, 8},
    {"twilightforest:hollow_tree", 4, 9},       {"twilightforest:hydra_lair", 4, 10},        {"twilightforest:large_hollow_hill", 4, 13},
    {"twilightforest:lich_tower", 4, 14},       {"twilightforest:medium_hollow_hill", 4, 16}, {"twilightforest:mushroom_tower", 4, 18},
    {"twilightforest:naga_courtyard", 4, 19},   {"twilightforest:quest_grove", 4, 23},       {"twilightforest:small_hollow_hill", 4, 33},
    {"twilightforest:swamp_hollow_tree", 4, 35}, {"twilightforest:yeti_cave", 4, 42},        {"twilightforest:giant_house", 10, 0},
};

/// A worldgen::Region that only counts what is placed in it (the structure step outside a chunk
/// generation).
class CountingRegion final : public gf::worldgen::Region {
public:
    CountingRegion(int dim, int chunkX, int chunkZ, std::int64_t seed) : mDim(dim), mChunkX(chunkX), mChunkZ(chunkZ), mSeed(seed) {}
    [[nodiscard]] int          centerChunkX() const override { return mChunkX; }
    [[nodiscard]] int          centerChunkZ() const override { return mChunkZ; }
    [[nodiscard]] int          minY() const override { return -64; }
    [[nodiscard]] int          maxY() const override { return 320; }
    [[nodiscard]] bool         isInside(int x, int z) const override { return std::abs((x >> 4) - mChunkX) <= 1 && std::abs((z >> 4) - mChunkZ) <= 1; }
    [[nodiscard]] Block const& getBlock(BlockPos const&) const override { return *gf::game::blockWith("minecraft:air"); }
    bool                       setBlock(BlockPos const& pos, Block const&, int) override {
        if (!isInside(pos.x, pos.z)) return false;
        ++placed;
        return true;
    }
    [[nodiscard]] int          height(gf::worldgen::Heightmap, int, int) const override { return 0; }
    [[nodiscard]] std::string  biome(BlockPos const&) const override { return {}; }
    [[nodiscard]] std::int64_t seed() const override { return mSeed; }
    [[nodiscard]] int          dimension() const override { return mDim; }
    int                        placed = 0;

private:
    int          mDim, mChunkX, mChunkZ;
    std::int64_t mSeed;
};

/// Runs a command as the server console in dimension `dim` at permission level `level`: (success,
/// the output messages: ids and parameters). The selftest framework's runServerCommand
/// (src/selftest/SelfTest.cpp) is file-local and has no permission level; F0 is asked to expose
/// one in SelfTest.h, which replaces this copy.
std::pair<bool, std::string> runCommand(std::string const& command, int dim, CommandPermissionLevel permission = CommandPermissionLevel::Owner) {
    auto level = ll::service::getLevel();
    if (!level) return {false, "no level"};
    ServerCommandOrigin origin("gloamingforest-landmark-selftest", level->asServer(), permission, DimensionType(dim));
    auto        out = ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
    std::string messages;
    for (auto const& m : out.mMessages) {
        if (!messages.empty()) messages += "; ";
        messages += m.mMessageId;
        for (auto const& p : m.mParams) messages += " " + p;
    }
    return {out.mSuccessCount > 0, messages};
}

} // namespace

// ChunkGenerator#applyBiomeDecoration's structure part: each landmark's pieces are placed at its
// GenerationStep with setFeatureSeed(decorationSeed, index, step) on a Xoroshiro WorldgenRandom, the
// index counting every structure of the step in Java's registry (vanilla's included)
GF_REGISTER_SELFTEST("landmark.decoration", [](Context& ctx) {
    namespace dec = gf::core::landmark::decoration;
    namespace wgx = gf::worldgen::ext;
    // the slots: Java's, the worldgen registry's (what the structure step's context seeds with) and
    // the descriptors' agree for all 21 TF structures
    for (auto const& w : kJavaSlots) {
        auto const* inf  = fl::info(w.key);
        auto        slot = fl::decorationSlot(w.key);
        auto        reg  = wgx::structureSlot(w.key);
        ctx.check(inf && inf->inRegistry && inf->slot == (dec::Slot{w.step, w.index}) && slot == inf->slot && reg && reg->step == w.step && reg->index == w.index
                      && dec::stepIndex(inf->step) == w.step,
                  std::string(w.key) + " decorates at step " + std::to_string(w.step) + " index " + std::to_string(w.index) + " (got " + std::to_string(slot.step) + "/"
                      + std::to_string(slot.index) + ", registry " + (reg ? std::to_string(reg->step) + "/" + std::to_string(reg->index) : "none") + ")");
    }
    // the core's own Structure registry (Minecraft 1.21.1's 34 structures, the gtests' table) with
    // TF's descriptors at their steps, in registry order, is F5's structuresByStep() entry by entry:
    // the two cannot drift apart unnoticed (a data pack structure F5 reads would show here too)
    {
        auto registry = dec::vanillaStructures();
        for (auto const& k : fl::descriptorKeys())
            if (auto const* inf = fl::info(k); inf && !fl::isFixtureKey(k)) registry.push_back({k, fl::stepOf(*inf)});
        std::stable_sort(registry.begin(), registry.end(), [](dec::Structure const& a, dec::Structure const& b) { return dec::registryBefore(a.id, b.id); });
        std::vector<std::vector<std::string>> core(dec::kSteps);
        for (auto const& e : registry) core[static_cast<std::size_t>(e.step)].push_back(e.id);
        auto        worldgen = wgx::structuresByStep();
        std::string firstDiff;
        int         entries = 0;
        for (int step = 0; step < dec::kSteps && firstDiff.empty(); ++step) {
            auto const& a = core[static_cast<std::size_t>(step)];
            auto const  b = step < static_cast<int>(worldgen.size()) ? worldgen[static_cast<std::size_t>(step)] : std::vector<std::string>{};
            for (std::size_t i = 0; i < std::max(a.size(), b.size()) && firstDiff.empty(); ++i) {
                auto ea = i < a.size() ? a[i] : std::string("(none)");
                auto eb = i < b.size() ? b[i] : std::string("(none)");
                if (ea != eb) firstDiff = std::string(dec::stepName(step)) + " #" + std::to_string(i) + ": core " + ea + ", worldgen " + eb;
                else ++entries;
            }
        }
        ctx.check(firstDiff.empty() && entries == 34 + 21,
                  "the core's structure registry is the worldgen's, entry by entry (" + (firstDiff.empty() ? std::to_string(entries) + " entries" : firstDiff) + ")");
    }
    // the seeds: the decoration context the worldgen hands the structure step and the random of a
    // placement in loaded chunks are Java's setDecorationSeed + setFeatureSeed
    auto first = [](std::unique_ptr<gf::core::jrand::WorldgenRandom> const& r) { return r ? r->nextLong() : 0; };
    std::int64_t const seed = fl::levelSeed();
    int                mismatches = 0;
    for (auto [cx, cz] : {std::pair{0, 0}, std::pair{-37, 512}, std::pair{1000, -1000}}) {
        auto ds = dec::decorationSeed(seed, cx, cz);
        for (auto const& w : kJavaSlots) {
            auto                    java = first(dec::structureRandom(ds, {w.step, w.index}));
            wgx::DecorationContext d{cx, cz, w.step, ds};
            auto                    viaContext = d.structureRandomOf(w.key);
            if (!viaContext || first(viaContext) != java || first(fl::decorationRandom(seed, cx, cz, w.key)) != java) ++mismatches;
            wgx::DecorationContext other{cx, cz, w.step == 4 ? 3 : 4, ds};
            if (other.structureRandomOf(w.key)) ++mismatches; // no random at another step
        }
    }
    ctx.check(mismatches == 0, "every TF structure's decoration random is Java's (" + std::to_string(mismatches) + " mismatches)");

    // the steps some landmark decorates at: TF's underground_structures, surface_structures and
    // top_layer_modification (the fixtures' surface_structures); the structure step returns at once
    // at the other 8
    ctx.check(fl::decorationStepMask() == ((1u << 3) | (1u << 4) | (1u << 10)),
              "landmarks decorate at steps 3, 4 and 10 only (mask " + std::to_string(fl::decorationStepMask()) + ")");

    // the structure step itself: a start is placed at its structure's step only, every piece with
    // the structure's one random (the fixture landmarks: no registry lists them, so their
    // descriptor's surface_structures, past the end of it in registration order: no registered
    // structure shares their seed)
    auto const* fixture = fl::info(kFixture);
    auto const* open    = fl::info(kOpenFixture);
    ctx.require(fixture != nullptr && open != nullptr, "the fixture descriptors");
    auto surface     = static_cast<int>(wgx::structuresByStep()[4].size());
    auto fixtureSlot = fl::decorationSlot(kFixture);
    ctx.check(!fixture->inRegistry && fixtureSlot == (dec::Slot{4, surface}) && open->slot == (dec::Slot{4, surface + 1}),
              "the fixtures decorate past the end of surface_structures: " + std::to_string(surface) + " and " + std::to_string(surface + 1) + " (got "
                  + std::to_string(fixtureSlot.step) + "/" + std::to_string(fixtureSlot.index) + ", " + std::to_string(open->slot.step) + "/"
                  + std::to_string(open->slot.index) + ")");
    auto base = ctx.testPos(5670, 2);
    int  dim = base.dim, cx = base.x >> 4, cz = (base.z >> 4) + 20000; // far from every other start
    auto rec  = fl::createStart(*fixture, dim, cx, cz, GlobalPos{dim, cx * 16 + 8, 64, cz * 16 + 8}, true);
    ctx.require(rec != nullptr && rec->livePieces.size() == 2, "a fixture start with its two pieces");
    Cleanup cleanup;
    cleanup.starts.push_back(rec->start.center);
    fl::putRecord(rec);
    std::vector<std::int64_t> probes;
    // only the pieces of this start (a generation thread may place another fixture meanwhile)
    fl::setFixtureProbe([&probes, cx, cz](gf::worldgen::RandomSource& r, gf::core::BlockBox const& box) {
        if ((box.minX >> 4) == cx && (box.minZ >> 4) == cz) probes.push_back(r.nextLong());
    });
    struct ProbeOff {
        ~ProbeOff() { fl::setFixtureProbe(nullptr); }
    } probeOff;
    CountingRegion region(dim, cx, cz, seed);
    auto           ds     = dec::decorationSeed(seed, cx, cz);
    std::string    placed;
    for (int step = 0; step < dec::kSteps; ++step) {
        wgx::DecorationContext d{cx, cz, step, ds};
        placed += std::to_string(fl::decorateStep(region, cx, cz, d));
    }
    fl::setFixtureProbe(nullptr);
    ctx.check(placed == "00001000000", "placed at surface_structures only (starts per step: " + placed + ")");
    auto java = dec::structureRandom(ds, fixtureSlot);
    auto a = java->nextLong(), b = java->nextLong();
    ctx.check(probes.size() == 2 && probes[0] == a && probes[1] == b,
              "the shell and the porch drew from one random, setFeatureSeed(decorationSeed, " + std::to_string(fixtureSlot.index) + ", 4)");
    ctx.check(region.placed > 0, "the pieces wrote through the region (" + std::to_string(region.placed) + " blocks)");
});

// The landmarks' biome checks (Structure#biomes(): LandmarkStructure#findValidGenerationPoint,
// Structure#isValidBiome, WorldUtil#findNearestMapLandmark) against TF 4.8's biome tags
// (src/generated/resources/data/twilightforest/tags/worldgen/biome) through the tags contract
GF_REGISTER_SELFTEST("landmark.biomes", [](Context& ctx) {
    static char const* const tfBiomes[] = {"clearing",         "dark_forest", "dark_forest_center", "dense_forest", "dense_mushroom_forest", "enchanted_forest",
                                           "final_plateau",    "fire_swamp",  "firefly_forest",     "forest",       "glacier",               "highlands",
                                           "highlands_underground", "lake",   "mushroom_forest",    "oak_savannah", "snowy_forest",          "spooky_forest",
                                           "stream",           "swamp",       "thornlands",         "underground"};
    std::vector<std::string> const common{"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"};
    std::vector<std::string> const hollowTree{"dense_forest", "fire_swamp", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "enchanted_forest"};
    struct Want {
        char const*              key;
        char const*              biomes; ///< the record's "biomes"
        std::vector<std::string> members;
    };
    std::vector<Want> const want = {
        {"aurora_palace", "#twilightforest:valid_aurora_palace_biomes", {"glacier"}},
        {"camp", "#twilightforest:valid_camp_biomes", {"oak_savannah", "clearing", "mushroom_forest", "forest", "firefly_forest"}},
        {"dark_tower", "#twilightforest:valid_dark_tower_biomes", {"dark_forest_center"}},
        {"fallen_trunk", "#twilightforest:valid_hollow_tree_biomes", hollowTree},
        {"final_castle", "#twilightforest:valid_final_castle_biomes", {"final_plateau"}},
        {"giant_house", "#twilightforest:valid_giant_house_biomes", {"highlands"}},
        {"hedge_maze", "#twilightforest:valid_hedge_maze_biomes", common},
        {"hollow_tree", "#twilightforest:valid_hollow_tree_biomes", hollowTree},
        {"hydra_lair", "#twilightforest:valid_hydra_lair_biomes", {"fire_swamp"}},
        {"knight_stronghold", "#twilightforest:valid_knight_stronghold_biomes", {"dark_forest"}},
        {"labyrinth", "#twilightforest:valid_labyrinth_biomes", {"swamp"}},
        {"large_hollow_hill", "#twilightforest:valid_hollow_hill_biomes", common},
        {"lich_tower", "#twilightforest:valid_lich_tower_biomes", common},
        {"medium_hollow_hill", "#twilightforest:valid_hollow_hill_biomes", common},
        {"mushroom_tower", "#twilightforest:valid_mushroom_tower_biomes", {"dense_mushroom_forest"}},
        {"naga_courtyard", "#twilightforest:valid_naga_courtyard_biomes", common},
        {"quest_grove", "#twilightforest:valid_quest_grove_biomes", {"enchanted_forest"}},
        {"small_hollow_hill", "#twilightforest:valid_hollow_hill_biomes", common},
        {"swamp_hollow_tree", "twilightforest:swamp", {"swamp"}},
        {"troll_cave", "#twilightforest:valid_troll_cave_biomes", {"highlands"}},
        {"yeti_cave", "#twilightforest:valid_yeti_cave_biomes", {"snowy_forest"}},
    };
    for (auto const& w : want) {
        auto        key = std::string("twilightforest:") + w.key;
        auto const* inf = fl::info(key);
        ctx.check(inf != nullptr, key + " has a descriptor");
        if (!inf) continue;
        ctx.check(inf->biomes == w.biomes, key + "'s biomes are " + w.biomes + " (got " + inf->biomes + ")");
        std::set<std::string> expected, got;
        for (auto const& m : w.members) expected.insert("twilightforest:" + m);
        for (auto const* b : tfBiomes)
            if (fl::biomeMatches(inf->biomes, std::string("twilightforest:") + b)) got.insert(std::string("twilightforest:") + b);
        std::string list;
        for (auto const& g : got) list += (list.empty() ? "" : " ") + g;
        ctx.check(got == expected, key + " starts in TF's biomes of " + w.biomes + " (matched: " + list + ")");
        ctx.check(!fl::biomeMatches(inf->biomes, "minecraft:plains") && !fl::biomeMatches(inf->biomes, ""), key + " never in a vanilla or unknown biome");
    }
});

// TF's subcommands as TF registers them (TFCommand): literals, argument shapes and texts, from the
// console (no player: genbook's not_player, as Java's CommandSourceStack without an entity)
GF_REGISTER_SELFTEST("landmark.commands", [](Context& ctx) {
    // ResourceLocation#read of genbook's ResourceKeyArgument
    ctx.check(fl::parseResourceLocation("lich_tower") == std::optional<std::string>("minecraft:lich_tower"), "no namespace: minecraft");
    ctx.check(fl::parseResourceLocation("twilightforest:lich_tower") == std::optional<std::string>("twilightforest:lich_tower"), "a full id");
    ctx.check(fl::parseResourceLocation(":camp") == std::optional<std::string>("minecraft:camp"), "an empty namespace: minecraft");
    ctx.check(!fl::parseResourceLocation("Twilightforest:Lich_Tower") && !fl::parseResourceLocation("a:b:c") && !fl::parseResourceLocation("a b"),
              "upper case, a second ':' and spaces are invalid");
    ctx.check(fl::structureRegistered("twilightforest:lich_tower") && fl::structureRegistered("twilightforest:hollow_tree")
                  && fl::structureRegistered("minecraft:village_plains"),
              "TF's and Minecraft's structures are registered");
    ctx.check(!fl::structureRegistered("twilightforest:quest_island") && !fl::structureRegistered("minecraft:lich_tower"),
              "TF's declared-only keys and unknown ids are not");

    int  dim      = ctx.testPos(5670).dim;
    auto expectIn = [&](std::string const& command, bool ok, std::vector<std::string> const& texts,
                        CommandPermissionLevel permission = CommandPermissionLevel::Owner) {
        auto [success, messages] = runCommand(command, dim, permission);
        bool        all          = true;
        std::string want;
        for (auto const& t : texts) {
            all = all && messages.find(t) != std::string::npos;
            want += (want.empty() ? "'" : ", '") + t + "'";
        }
        ctx.check(success == ok && all, "/" + command + " -> " + (ok ? "success" : "failure") + " with " + want + " (got " + (success ? "success" : "failure") + ": "
                                            + messages + ")");
    };
    // genbook [<structure>]: the argument is resolved before generateBook checks for a player
    expectIn("twilightforest genbook minecraft:no_such_structure", false, {"There is no structure with type \"minecraft:no_such_structure\""});
    expectIn("twilightforest genbook lich_tower", false, {"There is no structure with type \"minecraft:lich_tower\""});
    expectIn("twilightforest genbook twilightforest:quest_island", false, {"There is no structure with type \"twilightforest:quest_island\""});
    // ResourceLocation#read stops at the first character no resource location holds: "Bad:Id" reads
    // as "" (minecraft:) followed by trailing data; "a:b:c" reads whole and is no valid location
    expectIn("twilightforest genbook Bad:Id", false, {"Expected whitespace to end one argument, but found trailing data"});
    expectIn("twilightforest genbook a:b:c", false, {"Invalid ID"});
    // the GloamingForest additions locate / place read their argument the same way (Java's /locate
    // structure and /place structure)
    expectIn("twilightforest locate lich_tower", false, {"There is no structure with type \"minecraft:lich_tower\""});
    expectIn("twilightforest place lich_tower", false, {"There is no structure with type \"minecraft:lich_tower\""});
    // locate's tag: LocateCommand#getHolders reads Registry#getTag (the tags contract's structure
    // tags); an unknown one is ERROR_STRUCTURE_INVALID with "#" and the tag
    ctx.check(gf::tags::members(gf::tags::Registry::Structure, "twilightforest:landmark").size() == 16, "#twilightforest:landmark has TF's 16 structures");
    expectIn("twilightforest locate #twilightforest:no_such_tag", false, {"There is no structure with type \"#twilightforest:no_such_tag\""});
    expectIn("twilightforest locate #Bad", false, {"Expected whitespace to end one argument, but found trailing data"});
    // Minecraft's own structures: outside the Twilight Forest Java finds and places them, which only
    // Bedrock's own commands can here (the console stands in the overworld)
    expectIn("twilightforest locate minecraft:village_plains", false, {"minecraft:village_plains is not one of them"});
    expectIn("twilightforest place minecraft:village_plains", false, {"minecraft:village_plains is not one of them"});
    // a tag's result names the structure found (LocateCommand#showLocateResult): a forced fixture
    // start, in #twilightforest:landmark, far above this unit's row
    {
        auto const* fixture = fl::info(kFixture);
        ctx.require(fixture != nullptr, "the fixture descriptor");
        auto    center = at(ctx.testPos(5675, 3), 0, 60, 0);
        Cleanup cleanup;
        auto    rec = fl::createStart(*fixture, center.dim, center.x >> 4, center.z >> 4, center, true);
        ctx.require(rec != nullptr, "a forced fixture start");
        cleanup.starts.push_back(center);
        fl::putRecord(rec);
        expectIn("twilightforest locate #twilightforest:landmark", true, {"The nearest #twilightforest:landmark (gf_test:landmark_fixture", ", ~, "});
        expectIn("twilightforest locate gf_test:landmark_fixture", true, {"The nearest gf_test:landmark_fixture is at [", " blocks away)"});
    }
    expectIn("twilightforest genbook twilightforest:lich_tower", false, {"This command must be run by a valid player!"});
    expectIn("tf genbook minecraft:village_plains", false, {"This command must be run by a valid player!"});
    expectIn("tffeature genbook", false, {"This command must be run by a valid player!"});
    expectIn("twilightforest generatebook", false, {"This command must be run by a valid player!"});
    // conquer [reactivate]: the console stands outside every landmark here
    expectIn("twilightforest conquer", false, {"You are not in a structure"});
    expectIn("twilightforest conquer reactivate", false, {"You are not in a structure"});
    // center: TF's three lines, from one run
    expectIn("twilightforest center", true, {"The nearest feature is: ", "Center of feature: ", "Located in feature chunk: "});
    // a source below the literal's level: Java's requires(n) hides the literal; Bedrock answers a
    // command the source may not use with commands.generic.unknown (genbook needs Admin, the
    // others GameDirectors)
    for (auto const* literal : {"center", "info", "conquer", "conquer reactivate", "locate twilightforest:lich_tower", "place twilightforest:lich_tower"})
        expectIn(std::string("twilightforest ") + literal, false, {"commands.generic.unknown " + std::string(literal).substr(0, std::string(literal).find(' '))},
                 CommandPermissionLevel::Any);
    expectIn("twilightforest genbook", false, {"commands.generic.unknown genbook"}, CommandPermissionLevel::GameDirectors);
    expectIn("twilightforest genbook", false, {"This command must be run by a valid player!"}, CommandPermissionLevel::Admin);
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
    cleanup.withoutLock(p); // another test may have left progress_naga on the simulated player
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

    // creative and spectator players are never refused; neither is anyone without enforcement
    p.setPlayerGameType(GameType::Creative);
    ctx.check(!lm::isProtectedFor(p, gf::game::blockPos(wall)), "creative players are not refused");
    p.setPlayerGameType(GameType::Survival);
    fl::setEnforcedOverride(false);
    ctx.check(!lm::isProtectedFor(p, gf::game::blockPos(wall)), "nothing is protected without tfEnforcedProgression");
    fl::setEnforcedOverride(true);

    // allowed after the grant: PlayerHelper#playerHasRequiredAdvancements answers from the
    // progression store (F10), granted and revoked the way U14's criteria and /advancement do
    {
        auto& g = ctx.player();
        gf::progression::grant(g, kLock);
        ctx.require(gf::progression::has(g, kLock), "progression::grant stored progress_naga");
        ctx.check(!lm::isProtectedFor(g, gf::game::blockPos(wall)), "with progress_naga the shell is open");
        ctx.check(destroyAs(g, wall) == ia::Result::Pass, "with progress_naga breaking passes");
        ctx.check(useOnAs(g, wall, "minecraft:stone", *stone) == ia::Result::Pass, "with progress_naga placing passes");
        ctx.check(!fl::isAreaProtectedQuery(g, wall.dim, wall.x, wall.y, wall.z), "with progress_naga isAreaProtected is false");
        gf::progression::revoke(g, kLock);
        ctx.require(!gf::progression::has(g, kLock), "progression::revoke took progress_naga back");
        ctx.check(lm::isProtectedFor(g, gf::game::blockPos(wall)), "without it the shell is locked again");
        ctx.check(destroyAs(g, wall) == ia::Result::Deny, "without it breaking is refused again");
    }

    // Java's /place structure stores no start: a /twilightforest place landmark protects nothing
    {
        auto const* fixture = fl::info(kFixture);
        ctx.require(fixture != nullptr, "the fixture descriptor");
        auto placedAt = at(center, 16, 40, 0); // another chunk: records are kept per chunk and key
        auto placed   = fl::createStart(*fixture, placedAt.dim, placedAt.x >> 4, placedAt.z >> 4, placedAt, true);
        ctx.require(placed != nullptr, "a placed fixture start");
        placed->placed = true;
        fl::putRecord(placed);
        cleanup.starts.push_back(placedAt);
        auto& q = ctx.player();
        ctx.check(!lm::isProtectedFor(q, gf::game::blockPos(at(placedAt, 3, 2, 0))), "a /place'd landmark is not protected");
    }
    if (gf::config::get<fl::LandmarkConfig>().protectionBoxes) {
        ctx.check(fl::protectionBoxEntities() > 0, "a protection box entity is alive");
        // TF's ProtectionBox is never saved: an untracked box (one loaded back with its chunk) goes,
        // the live ones stay
        auto* stray = fl::spawnEntity(*gf::game::regionOf(center.dim), "twilightforest:protection_box", static_cast<float>(center.x) + 0.5f,
                                      static_cast<float>(center.y) + 12.0f, static_cast<float>(center.z) + 0.5f);
        ctx.check(stray != nullptr, "a protection box entity can be spawned");
        if (stray) ctx.check(fl::sweepStrayProtectionBoxes() >= 1, "an untracked protection box is removed");
        co_await ctx.ticks(2);
        ctx.check(fl::protectionBoxEntities() > 0, "the tracked boxes are kept");
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
    // LandmarkUtil#locateNearestLandmarkStart reads the grid center chunk only: a start off the grid
    // that is no selftest stand-in (not forced) is never conquered from its own position
    {
        auto natural = fl::createStart(*fixture, center.dim, center.x >> 4, center.z >> 4, at(center, 0, 40, 0), false);
        ctx.require(natural != nullptr, "a natural fixture start");
        fl::putRecord(natural);
        lm::setConquered(center.dim, gf::game::blockPos(at(center, 0, 40, 0)), kFixture, true);
        ctx.check(!lm::isConquered(natural->start), "an off-grid natural start is not conquered");
    }
    // structure_cleared goes to the players whose bounding box meets AABB(pos).inflate(32)
    auto box = [](float x, float y, float z) { return AABB{Vec3{x - 0.3f, y, z - 0.3f}, Vec3{x + 0.3f, y + 1.8f, z + 0.3f}}; };
    ctx.check(fl::playerBoxReaches(box(0.5f, -33.5f, 0.5f), 0, 0, 0, 32.0), "a player whose head reaches into the area counts (feet 33.5 below)");
    ctx.check(fl::playerBoxReaches(box(33.2f, 0.0f, 0.5f), 0, 0, 0, 32.0), "a player 0.3 beyond the side counts");
    ctx.check(!fl::playerBoxReaches(box(0.5f, -34.0f, 0.5f), 0, 0, 0, 32.0), "a player entirely below does not (head at -32.2)");
    ctx.check(!fl::playerBoxReaches(box(33.4f, 0.0f, 0.5f), 0, 0, 0, 32.0), "nor one beyond the side");
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
    ctx.check(marker.value("locale", std::string{"?"}) == gf::localeOf(p), "written in the reader's language");
    // another reader (another language): the book is written again from its keys
    ctx.check(!fl::retranslateBook(*book, p), "the same reader gets it unchanged");
    auto foreign = marker;
    foreign["locale"] = "xx_XX";
    gf::game::items::setData(*book, "translatable_book", foreign);
    ctx.check(fl::retranslateBook(*book, p), "a book written for another language is rewritten");
    ctx.check(gf::game::items::getData(*book, "translatable_book").value("locale", std::string{}) == gf::localeOf(p)
                  && gf::game::items::getData(*book, "translatable_book").value("pages", 0) == 4,
              "in the reader's language, with its pages");
    ctx.check(!fl::createHintBook(*fl::info("twilightforest:hedge_maze"), &p).has_value(), "no book for structures without hints");
});

GF_REGISTER_SELFTEST("landmark.hint_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    // StructureHints#didSpawnHintMonster: the hint mob holds the book in its main hand with drop
    // chance PRESERVE_ITEM_DROP_CHANCE_THRESHOLD (2.0), so killing it always yields the hint. The
    // kobold is U17's entity: a vanilla zombie carries the lich tower's book here.
    auto area = co_await ctx.flatArea(5650, 8);
    auto pos  = ctx.testPos(5652, 2);
    auto level = ll::service::getLevel();
    ctx.require(static_cast<bool>(level), "a level");
    // Level#getGameTime: level.dat's currentTick, the tick the level counts
    auto gt = fl::gameTime(), ct = level->getCurrentTick().tickID;
    ctx.check((gt > ct ? gt - ct : ct - gt) <= 1, "the hint timer reads level.dat's currentTick (" + std::to_string(gt) + " vs " + std::to_string(ct) + ")");
    auto const* lich = fl::info("twilightforest:lich_tower");
    ctx.require(lich != nullptr, "lich tower");
    fl::Info carrier = *lich;
    carrier.hintMob  = "minecraft:zombie";
    auto* region     = ctx.region();
    ctx.require(region != nullptr, "the test region");
    HostileMobsAllowed hostile; // the e2e worlds are peaceful
    (void)ctx.player();
    co_await ctx.ticks(1);
    region   = ctx.region();
    auto* mob = fl::spawnHintCarrier(carrier, *region, static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + 0.5f, &ctx.player());
    ctx.require(mob != nullptr, "the hint mob spawned");
    ctx.check(gf::game::items::idOf(mob->getCarriedItem()) == "minecraft:written_book", "it holds the hint book");
    auto chance = fl::mainHandDropChance(*mob);
    ctx.check(chance.has_value() && *chance >= 2.0f, "its main hand always drops (chance " + std::to_string(chance.value_or(-1.0f)) + ")");
    auto mobId = mob->getOrCreateUniqueID();
    mob->kill();
    co_await ctx.ticks(30);
    AABB box{Vec3{static_cast<float>(area.minX), static_cast<float>(area.minY) - 2.0f, static_cast<float>(area.minZ)},
             Vec3{static_cast<float>(area.maxX + 1), static_cast<float>(area.minY) + 8.0f, static_cast<float>(area.maxZ + 1)}};
    bool dropped = false;
    if (auto* r = ctx.region()) {
        for (auto* a : r->getEntities(box)) {
            if (!a || a->getTypeName() != "minecraft:item") continue;
            if (gf::game::items::idOf(static_cast<ItemActor*>(a)->item()) == "minecraft:written_book") dropped = true;
            a->remove(); // the drops of the test
        }
    }
    ctx.check(dropped, "killing it dropped the hint book");
    if (auto* left = level->fetchEntity(mobId, false)) left->remove();
});

GF_REGISTER_SELFTEST("landmark.hint_timer", [](Context& ctx) -> ll::coro::CoroTask<> {
    // ProgressionStructure's hint timer (a hint monster at most every 1200 ticks per structure) reads
    // Level#getGameTime, fl::gameTime(): level.dat's currentTick. It has to advance with every tick,
    // not only when the level data is written: sampled against the level's live tick for 300 ticks
    auto level = ll::service::getLevel();
    ctx.require(static_cast<bool>(level), "a level");
    bool sameStorage = &level->getCurrentTick() == &level->getLevelData().mCurrentTick.get();
    ctx.info(std::string("Level::getCurrentTick ") + (sameStorage ? "is" : "is not") + " the level data's currentTick");
    auto const startGame = fl::gameTime(), startLive = level->getCurrentTick().tickID;
    std::uint64_t game = startGame, live = startLive;
    for (int sample = 0; sample < 6; ++sample) {
        co_await ctx.ticks(50);
        level = ll::service::getLevel();
        ctx.require(static_cast<bool>(level), "the level is still there");
        game = fl::gameTime();
        live = level->getCurrentTick().tickID;
        ctx.check((game > live ? game - live : live - game) <= 1,
                  "after " + std::to_string((sample + 1) * 50) + " ticks the game time " + std::to_string(game) + " follows the live tick " + std::to_string(live));
    }
    ctx.check(game - startGame >= 290 && game - startGame <= live - startLive + 1,
              "the game time advanced every tick (" + std::to_string(game - startGame) + " in " + std::to_string(live - startLive) + " live ticks)");
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
    // terraforming (WorldgenHooks#gatherCustomTerrain): a start's terraformer counts everywhere in the
    // chunks it references, outside its box too, and nowhere else
    static std::atomic<int> terraformCalls{0};
    static std::atomic<bool> terraformActive{false};
    static bool              terraformRegistered = false;
    if (!terraformRegistered) {
        terraformRegistered = true;
        lm::addTerraformer(kFixture, [](lm::Start const&, int, int, int) {
            if (!terraformActive.load()) return 0.0;
            ++terraformCalls;
            return 1.0;
        });
    }
    terraformActive = true;
    struct Off {
        ~Off() { terraformActive = false; }
    } off;
    auto const& box   = rec->start.box;
    int         cx    = center.x >> 4, cz = center.z >> 4;
    int         edgeX = (cx << 4) < box.minX ? (cx << 4) : (cx << 4) + 15; // a column of the chunk outside the box
    ctx.require(edgeX < box.minX || edgeX > box.maxX, "a column of the start chunk lies outside the box");
    ctx.check(gf::landmark::ext::hasTerraformers(center.dim, cx, cz), "the start chunk has a terraformer");
    ctx.check(gf::landmark::ext::terraformDensity(center.dim, center.x, center.y, center.z) == 1.0, "inside the box it applies");
    ctx.check(gf::landmark::ext::terraformDensity(center.dim, edgeX, center.y, center.z) == 1.0, "outside the box, in a referenced chunk, it applies too");
    auto chunkFn = gf::landmark::ext::chunkTerraformer(center.dim, cx, cz);
    ctx.check(chunkFn && chunkFn(edgeX, center.y - 30, (cz << 4) + 15) == 1.0, "chunkTerraformer: the whole chunk, any height");
    int farCx = ((box.maxX + 1) >> 4) + 2; // two chunks past the box: no reference
    ctx.check(!gf::landmark::ext::hasTerraformers(center.dim, farCx, cz) && gf::landmark::ext::terraformDensity(center.dim, farCx << 4, center.y, center.z) == 0.0,
              "a chunk the box does not reach has none");
    ctx.check(terraformCalls.load() >= 3, "the terraformer was evaluated");
});

GF_REGISTER_SELFTEST("landmark.starts", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace cl  = gf::core::landmark;
    namespace plc = gf::core::landmark::placement;
    using Clock   = std::chrono::steady_clock;
    auto base     = ctx.testPos(5690, 2);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    int const   dim = base.dim, cx = base.x >> 4, cz = base.z >> 4;
    StartsScope scope(dim);

    // the server thread never waits for another thread evaluating a start chunk: it evaluates the
    // chunk itself (the same deterministic starts)
    scope.touch(cx, cz);
    fl::forgetEvaluation(dim, cx, cz);
    fl::setEvaluationBusy(dim, cx, cz, true);
    auto t0    = Clock::now();
    bool known = fl::ensureStartsIn(dim, cx, cz);
    auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    fl::setEvaluationBusy(dim, cx, cz, false);
    ctx.check(known && fl::startsEvaluated(dim, cx, cz), "the server thread evaluated a chunk another thread holds itself");
    ctx.check(ms < 1000, "without waiting for that thread (" + std::to_string(ms) + " ms)");

    // a generation thread (no evaluation of its own under way) waits for the thread evaluating the
    // chunk, then goes on
    {
        int const wx = cx - 1;
        scope.touch(wx, cz);
        fl::forgetEvaluation(dim, wx, cz);
        fl::setEvaluationBusy(dim, wx, cz, true);
        auto       state = std::make_shared<std::atomic<int>>(0); // 0 waiting, 1 known, 2 not known
        TestThread worker;
        worker.release = [dim, wx, cz] { fl::setEvaluationBusy(dim, wx, cz, false); };
        worker.thread  = std::thread([state, dim, wx, cz] { state->store(fl::ensureStartsIn(dim, wx, cz) ? 1 : 2); });
        co_await ctx.ticks(4);
        ctx.check(state->load() == 0, "a generation thread waits while another thread evaluates the chunk");
        fl::setEvaluationBusy(dim, wx, cz, false);
        for (int i = 0; i < 40 && state->load() == 0; ++i) co_await ctx.ticks(1);
        ctx.check(state->load() == 1 && fl::startsEvaluated(dim, wx, cz), "and evaluates it once released (state " + std::to_string(state->load()) + ")");
    }

    // /twilightforest locate's probe (CHUNK_LOAD_NEEDED): one structure set, nothing stored, and the
    // same answer the Starts stage then gives the chunk
    {
        auto const* camp = fl::structureSet("twilightforest:camp");
        ctx.require(camp != nullptr, "the camp structure set");
        auto pot = plc::potentialStructureChunk(camp->placement, fl::levelSeed(), cx, cz);
        scope.touch(pot.x, pot.z);
        fl::forgetEvaluation(dim, pot.x, pot.z);
        auto before = fl::recordsStartedIn(dim, pot.x, pot.z).size();
        auto probed = fl::probeStart(dim, *camp, pot.x, pot.z);
        ctx.check(fl::recordsStartedIn(dim, pot.x, pot.z).size() == before && !fl::startsEvaluated(dim, pot.x, pot.z),
                  "locate's probe of chunk " + std::to_string(pot.x) + " " + std::to_string(pot.z) + " stores no start (" + (probed.empty() ? "none" : probed) + ")");
        ctx.check(!gf::game::db::get(fl::recordDbKey(dim, pot.x, pot.z)).has_value() || before > 0, "nor writes one to the world DB");
        (void)fl::ensureStartsIn(dim, pot.x, pot.z);
        std::string made;
        for (auto const& rec : fl::recordsStartedIn(dim, pot.x, pot.z))
            for (auto const& [key, weight] : camp->structures)
                if (rec->start.key == key && !rec->placed) made = key;
        ctx.check(made == probed, "the Starts stage makes what the probe answered (\"" + made + "\" vs \"" + probed + "\")");
    }
    scope.lift();

    // LandmarkUtil#locateNearestMatchingLandmark(checkReady): the grid center chunk must be loaded,
    // and only valid starts count (a reserved site has no pieces); markStructureConquered's lookup has
    // no ready check
    auto const* fixture = fl::info(kFixture);
    ctx.require(fixture != nullptr, "the fixture descriptor");
    auto nearCenter = cl::nearestCenter(cx, cz);
    ctx.require(co_await ctx.ensureLoaded(GlobalPos{dim, nearCenter.x, base.y, nearCenter.z}), "the nearest landmark center chunk loaded");
    scope.touch(nearCenter.x >> 4, nearCenter.z >> 4);
    auto natural = fl::createStart(*fixture, dim, nearCenter.x >> 4, nearCenter.z >> 4, GlobalPos{dim, nearCenter.x, base.y + 40, nearCenter.z}, false);
    ctx.require(natural != nullptr && !natural->pieces.empty(), "a fixture start at the center");
    fl::putRecord(natural);
    ctx.check(fl::nearestLandmark(dim, cx, cz) == natural, "a valid start in the loaded center chunk is the nearest landmark");
    ctx.check(fl::nearestLandmarkOf(dim, kFixture, nearCenter.x, nearCenter.z) == natural, "and the one markStructureConquered finds");
    auto reserved = fl::createStart(*fixture, dim, nearCenter.x >> 4, nearCenter.z >> 4, GlobalPos{dim, nearCenter.x, base.y + 40, nearCenter.z}, false);
    ctx.require(reserved != nullptr, "a second fixture start");
    reserved->pieces.clear();
    reserved->livePieces.clear();
    reserved->start.pieceBoxes.clear();
    reserved->start.generated = false;
    fl::putRecord(reserved); // in place of the valid one
    ctx.check(fl::nearestLandmark(dim, cx, cz) == nullptr, "a reserved site (no pieces) is no landmark start");
    ctx.check(fl::nearestLandmarkOf(dim, kFixture, nearCenter.x, nearCenter.z) == nullptr, "nor one to conquer");
    // a center chunk far from every player: not loaded
    auto farCenter = cl::nearestCenter(cx + 128, cz);
    ctx.require(!fl::chunkLoaded(dim, farCenter.x >> 4, farCenter.z >> 4), "a landmark center 2048 blocks away is not loaded");
    scope.touch(farCenter.x >> 4, farCenter.z >> 4);
    auto distant = fl::createStart(*fixture, dim, farCenter.x >> 4, farCenter.z >> 4, GlobalPos{dim, farCenter.x, base.y + 40, farCenter.z}, false);
    ctx.require(distant != nullptr, "a fixture start at the far center");
    fl::putRecord(distant);
    ctx.check(fl::nearestLandmark(dim, cx + 128, cz) == nullptr, "an unloaded center chunk gives no landmark (checkReady)");
    // markStructureConquered has no ready check on the center chunk, but reads a start only where its
    // own start chunk is loaded (LandmarkUtil#locateNearestLandmarkStart's level.hasChunk)
    ctx.check(fl::nearestLandmarkOf(dim, kFixture, farCenter.x, farCenter.z) == nullptr, "nor a start to conquer while its start chunk is not loaded");
});

GF_REGISTER_SELFTEST("landmark.persist_1_write", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = at(ctx.testPos(5660, 2), 0, 60, 0);
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(5660, 2)), "test area loaded");
    // the unlocked fixture (protects nothing). A 'full' run reads it back right away (persist_2_read
    // sorts after this test) and removes it; the server scenario restarts the server in between
    auto const* fixture = fl::info(kOpenFixture);
    ctx.require(fixture != nullptr, "the open fixture descriptor");
    auto rec = fl::createStart(*fixture, center.dim, center.x >> 4, center.z >> 4, center, true);
    ctx.require(rec != nullptr, "the fixture start");
    rec->start.conquered = true;
    // the hint timer's game time now: Level#getGameTime is persisted with the world, so after the
    // restart it continues from here
    rec->start.data["gf_test_game_time"] = fl::gameTime();
    fl::putRecord(rec);
    fl::flushRecords();
    gf::log().info("landmark.persist_1_write: start written at {} {} {} (game time {})", center.x, center.y, center.z, fl::gameTime());
});

GF_REGISTER_SELFTEST("landmark.persist_2_read", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto center = at(ctx.testPos(5660, 2), 0, 60, 0);
    co_await ctx.ticks(1);
    auto rec = fl::findRecord(center.dim, center.x >> 4, center.z >> 4, kOpenFixture);
    if (!rec) {
        gf::log().info("landmark.persist_2_read: no start");
        co_return;
    }
    ctx.check(rec->start.conquered && rec->forced && rec->pieces.size() == 2, "the start came back with its flag and pieces");
    auto written = rec->start.data.value("gf_test_game_time", std::uint64_t{0});
    ctx.check(fl::gameTime() >= written, "the game time went on from " + std::to_string(written) + " (now " + std::to_string(fl::gameTime()) + ")");
    gf::log().info("landmark.persist_2_read: start verified at {} {} {} (game time {})", center.x, center.y, center.z, fl::gameTime());
    dropStart(center, kOpenFixture);
});

GF_REGISTER_SELFTEST("landmark.display", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the client scenario looks at this area: the fixture landmark with its protection boxes shown
    // for 24 seconds (every 2 s the simulated player tries the wall again, as a player would; the
    // scenario needs two teleports and a platform before its two screenshots, slower on vanilla clients)
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
    for (int i = 0; i < 12; ++i) {
        (void)ctx.player();
        co_await ctx.ticks(1);
        auto& p = ctx.player();
        p.setPlayerGameType(GameType::Survival);
        if (i == 0) cleanup.withoutLock(p);
        ctx.check(destroyAs(p, at(center, 3, 2, 0)) == ia::Result::Deny, "the wall is protected");
        if (i == 0) {
            co_await ctx.ticks(2);
            gf::log().info("landmark.display: protection shown ({} boxes, {} entities) {}", fl::liveProtectionBoxes().size(), fl::protectionBoxEntities(), fl::describeProtectionBoxes());
        }
        co_await ctx.ticks(40);
    }
    (void)area;
});

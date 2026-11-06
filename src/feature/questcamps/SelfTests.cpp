// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U19 (questcamps) selftests, dx 1900..1999 (rule R12): the Questing Ram's quest (every colour once,
// dropped wool, the reward with the trigger and the conquered grove), the quest data and loot tables,
// the grove piece, a seeded camp's jigsaw layout, and every template feature (druid hut with its
// basement, wells, graveyard, grove ruins, stone circle) with golden volume hashes of the original
// templates (rule R19).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"

#include "core/VolumeHash.h"
#include "core/jrand/JRand.h"
#include "core/questcamps/Questcamps.h"
#include "feature/entity/Entity.h"
#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/ext/questcamps/W0Internals.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/landmark/Landmark.h"
#include "feature/loot/Loot.h"
#include "feature/progression/Progression.h"
#include "feature/questcamps/Questcamps.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::questcamps::tests {

using gf::selftest::Context;
using core::BlockBox;
namespace cq = gf::core::questcamps;
namespace fe = gf::feature::entity;
namespace st = gf::structure;

namespace {

// ---- helpers ---------------------------------------------------------------------------------------------------

Actor* spawnRam(Context& ctx, core::GlobalPos const& at) {
    auto* region = ctx.region();
    if (!region) return nullptr;
    return gf::spawn::spawnMob(*region, kRam, Vec3{at.x + 0.5F, static_cast<float>(at.y), at.z + 0.5F}, gf::spawn::SpawnReason::Command);
}

void removeActor(std::int64_t uid) {
    if (auto* a = fe::actorById(uid)) a->remove();
}

/// Removes the actors of `type` inside `box` (wraiths and rams the features spawned).
void removeIn(Context& ctx, BlockBox const& box, std::string const& type) {
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a || a->getTypeName() != type || static_cast<int>(a->getDimensionId()) != ctx.dimension()) continue;
        auto p = a->getFeetPos();
        if (p.x >= box.minX - 2 && p.x <= box.maxX + 3 && p.z >= box.minZ - 2 && p.z <= box.maxZ + 3) a->remove();
    }
}

ItemStack wool(int dye) { return game::items::make("minecraft:" + std::string(cq::kDyeNames[static_cast<std::size_t>(dye)]) + "_wool").value_or(ItemStack{}); }

/// The blocks of a box below a flat area (basements and well shafts dig there): restored when the
/// guard goes, so no block is left behind (rule R12).
struct BelowGuard {
    int                       dim = 0;
    BlockBox                  box;
    std::vector<Block const*> blocks;
    BelowGuard(Context& ctx, BlockBox b) : dim(ctx.dimension()), box(b) {
        auto* region = ctx.region();
        if (!region) return;
        for (int y = box.minY; y <= box.maxY; ++y)
            for (int z = box.minZ; z <= box.maxZ; ++z)
                for (int x = box.minX; x <= box.maxX; ++x) blocks.push_back(&region->getBlock(BlockPos{x, y, z}));
    }
    ~BelowGuard() {
        auto* region = game::regionOf(dim);
        if (!region || blocks.empty()) return;
        std::size_t i = 0;
        for (int y = box.minY; y <= box.maxY; ++y)
            for (int z = box.minZ; z <= box.maxZ; ++z)
                for (int x = box.minX; x <= box.maxX; ++x) {
                    BlockPos p{x, y, z};
                    Block const* want = blocks[i++];
                    if (want && &region->getBlock(p) != want) game::setBlock(*region, p, *want);
                }
    }
};

/// The golden volume hashes of the original templates placed raw (no processors), reviewed once
/// with the client screenshots (rule R19). A template whose design changes gets its new hash here.
std::map<std::string, std::string> const& goldens() {
    static std::map<std::string, std::string> const m = {
#include "feature/questcamps/Goldens.inc"
    };
    return m;
}

/// Places template `id` raw at `origin` and returns its volume hash.
std::string rawHash(Context& ctx, std::string const& id, BlockPos const& origin) {
    auto const* t = st::templates::get(id);
    if (!t) return "missing";
    st::BlockSourceRegion region(*ctx.region(), origin);
    // clear the template's box first so earlier placements do not show through structure voids
    for (int y = 0; y < t->sizeY; ++y)
        for (int z = 0; z < t->sizeZ; ++z)
            for (int x = 0; x < t->sizeX; ++x) region.setBlock(BlockPos{origin.x + x, origin.y + y, origin.z + z}, st::blocks::air(), 2);
    st::templates::place(id, region, origin, st::TemplateSettings{});
    return ctx.snapshotVolume(BlockBox{origin.x, origin.y, origin.z, origin.x + t->sizeX - 1, origin.y + t->sizeY - 1, origin.z + t->sizeZ - 1});
}

/// The ids of every U19 template.
std::vector<std::string> allTemplates() {
    std::vector<std::string> ids = {"twilightforest:quest_grove"};
    for (char const* s : {"campfire_east", "campfire_south", "campfire_west"}) ids.push_back(std::string("gloamingforest:camp/") + s);
    for (char const* s : {"solo_tent", "duo_tent", "open_tent", "luxury_tent"}) ids.push_back(std::string("gloamingforest:camp/tent/") + s);
    for (char const* s : {"intersection_left", "intersection_right", "intersection_short", "j_path", "l_path", "path_2x4", "path_3x4", "path_2x6", "path_2x7"})
        ids.push_back(std::string("gloamingforest:camp/path/") + s);
    for (char const* s : {"compost_large", "compost_small", "garden_1x3", "garden_2x4", "garden_straight", "garden_u", "pen", "repair_station", "berries_staked",
                          "berries_support", "berries_trellis", "blackberry_wall", "blueberry_wall", "raspberry_wall", "double_drying_rack", "long_drying_rack",
                          "lumber1", "lumber2", "lumber3"})
        ids.push_back(std::string("gloamingforest:camp/deco/") + s);
    for (char const* s : {"druid_hut/druid_hut", "druid_hut/druid_sideways", "druid_hut/druid_doubledeck", "druid_hut/basement_study", "druid_hut/basement_study_trap",
                          "druid_hut/basement_shelves", "druid_hut/basement_shelves_trap", "druid_hut/basement_gallery", "druid_hut/basement_gallery_trap",
                          "well/simple_well_top", "well/simple_well_bottom", "well/fancy_well_top", "well/fancy_well_bottom", "graveyard/graveyard",
                          "graveyard/grave_full", "graveyard/grave_upper", "graveyard/grave_lower", "graveyard/grave_trap", "ruins/grove_pillar",
                          "ruins/grove_arch", "ruins/stone_circle"})
        ids.push_back(std::string("gloamingforest:feature/") + s);
    return ids;
}

Json featureConfig(std::string const& id) {
    for (auto const& f : gf::feature::item::data::list("worldgen/configured_feature")) {
        if (f.id() != id) continue;
        auto doc = gf::feature::item::data::read(f.path);
        return doc.value("config", Json::object());
    }
    return Json::object();
}

/// A chunk whose 16 x 16 columns lie inside the flat area (TemplateFeature snaps into it).
BlockPos chunkOrigin(BlockBox const& box) {
    int cx = ((box.minX + 20) >> 4) << 4, cz = ((box.minZ + 20) >> 4) << 4;
    return BlockPos{cx + 3, box.minY, cz + 5};
}

} // namespace

// ---- data --------------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("questcamps.quest_data", [](Context& ctx) {
    auto q = quest();
    ctx.check(q.fromData, "the quest comes from twilight/quests/questing_ram.json");
    for (int i = 0; i < 16; ++i) {
        auto colours = questColorsOf("minecraft:" + std::string(cq::kDyeNames[static_cast<std::size_t>(i)]) + "_wool");
        ctx.check(colours.size() == 1 && colours.front() == i, std::string("the ") + std::string(cq::kDyeNames[static_cast<std::size_t>(i)]) + " wool is a quest item");
    }
    ctx.check(questColorsOf("minecraft:stone").empty(), "stone is no quest item");
    ctx.check(q.reward == "twilightforest:entities/questing_ram_rewards", "the reward table");
    gf::loot::LootContext lc;
    lc.region = ctx.region();
    std::map<std::string, int> counts;
    auto rewards = gf::loot::roll(q.reward, lc);
    for (auto const& s : rewards) counts[game::items::idOf(s)] += game::items::countOf(s);
    ctx.check(counts["twilightforest:crumble_horn"] == 1, "the rewards hold a crumble horn");
    ctx.check(counts["minecraft:bundle"] == 1, "the rewards hold a bundle (trophy and the seven blocks)");
    auto dropper = gf::loot::roll("twilightforest:quest_grove_dropper", lc);
    int  wools   = 0;
    for (auto const& s : dropper) wools += game::items::idOf(s).ends_with("_wool") ? game::items::countOf(s) : 0;
    ctx.check(wools == 4, "quest_grove_dropper gives 4 wool blocks (got " + std::to_string(wools) + ")");
    for (char const* table : {"twilightforest:well", "twilightforest:fancy_well", "twilightforest:graveyard", "twilightforest:chests/hut_junk",
                              "twilightforest:camp_tent", "twilightforest:camp_pot", "twilightforest:camp_armor_rack"})
        ctx.check(!gf::loot::roll(table, lc).empty(), std::string(table) + " rolls items");
});

GF_REGISTER_SELFTEST("questcamps.templates", [](Context& ctx) {
    auto ids = allTemplates();
    ctx.check(ids.size() == 57, "57 templates");
    for (auto const& id : ids) ctx.check(st::templates::get(id) != nullptr, "template " + id);
    // TemplateFeature's snap: nextInt(16 - size) for any rotation, so at most 15 x 15
    for (auto const& id : ids) {
        if (!id.starts_with("gloamingforest:feature/") || id.find("graveyard/") != std::string::npos) continue;
        if (id.find("basement") != std::string::npos || id.find("_bottom") != std::string::npos) continue;
        auto const* t = st::templates::get(id);
        if (t) ctx.check(t->sizeX <= 15 && t->sizeZ <= 15, id + " fits 15 x 15");
    }
    // the graves share one footprint (GraveyardFeature uses grave_full's size for all three)
    auto const* full = st::templates::get("gloamingforest:feature/graveyard/grave_full");
    for (char const* g : {"grave_upper", "grave_lower"}) {
        auto const* t = st::templates::get(std::string("gloamingforest:feature/graveyard/") + g);
        ctx.check(full && t && t->sizeX == full->sizeX && t->sizeZ == full->sizeZ, std::string(g) + " has grave_full's footprint");
    }
    auto const* grove = st::templates::get("twilightforest:quest_grove");
    ctx.check(grove && grove->sizeX == 27 && grove->sizeZ == 27, "the quest grove is 27 x 27");
});

// ---- the Questing Ram ----------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("questcamps.ram_accepts_wool", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1900, 12);
    auto* a  = spawnRam(ctx, core::GlobalPos{ctx.dimension(), box.minX + 5, box.minY, box.minZ + 5});
    ctx.require(a != nullptr, "a Questing Ram spawned");
    auto uid = fe::uidOf(*a);
    co_await ctx.ticks(2);
    a = fe::actorById(uid);
    ctx.require(a != nullptr, "the ram is still there");
    ctx.check(colorFlags(*a) == 0 && !rewarded(*a), "a new ram has no colours");
    ctx.check(!tryAccept(*a, game::items::make("minecraft:stone").value_or(ItemStack{})), "stone is refused");
    for (int i = 0; i < 16; ++i) {
        auto w = wool(i);
        ctx.check(isItemTempting(*a, w), std::string(cq::kDyeNames[static_cast<std::size_t>(i)]) + " wool tempts the ram");
        ctx.check(tryAccept(*a, w), std::string(cq::kDyeNames[static_cast<std::size_t>(i)]) + " wool is accepted");
        ctx.check(!tryAccept(*a, w), std::string(cq::kDyeNames[static_cast<std::size_t>(i)]) + " wool is accepted once only");
        ctx.check(!isItemTempting(*a, w), "a collected colour no longer tempts");
    }
    ctx.check(colorFlags(*a) == 0xFFFF, "all 16 colours are set");
    auto& d = gf::entity::data(*a, kRamData);
    ctx.check(d.value("ColorFlags", 0) == 0xFFFF, "ColorFlags is kept in the ram's data");
    removeActor(uid);
});

GF_REGISTER_SELFTEST("questcamps.ram_eats_dropped_wool", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1915, 12);
    auto* a  = spawnRam(ctx, core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY, box.minZ + 2});
    ctx.require(a != nullptr, "a Questing Ram spawned");
    auto uid = fe::uidOf(*a);
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "the level");
    auto  red  = wool(14);
    auto* item = level->getSpawner().spawnItem(*ctx.region(), red, nullptr, Vec3{box.minX + 7.5F, static_cast<float>(box.minY) + 0.2F, box.minZ + 2.5F}, 0);
    ctx.require(item != nullptr, "a red wool item dropped");
    bool eaten = false;
    for (int t = 0; t < 400 && !eaten; t += 5) {
        co_await ctx.ticks(5);
        auto* r = fe::actorById(uid);
        ctx.require(r != nullptr, "the ram is still there");
        eaten = cq::colorPresent(colorFlags(*r), 14);
    }
    ctx.check(eaten, "the ram walked to the dropped red wool and ate it within 20 s");
    removeIn(ctx, box, "minecraft:item");
    removeActor(uid);
});

GF_REGISTER_SELFTEST("questcamps.ram_reward", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1930, 40);
    core::GlobalPos center{ctx.dimension(), box.minX + 20, box.minY, box.minZ + 20};
    // a grove start around the ram (forced; its stored box starts 2 above the generation point)
    gf::landmark::ext::ScopedStart forced(kGrove, core::GlobalPos{center.dim, center.x, center.y - 2, center.z});
    ctx.require(forced && forced->generated, "a quest grove start");
    static int triggers = 0;
    static bool listening = false;
    if (!std::exchange(listening, true)) gf::progression::onTrigger(kTrigger, [](Player&, Json const&) { ++triggers; });
    int before = triggers;
    auto& sp = ctx.spawnPlayer("questcamps_ram");
    sp.teleport(Vec3{center.x + 4.5F, static_cast<float>(center.y), center.z + 4.5F}, DimensionType(center.dim));
    auto* a = spawnRam(ctx, center);
    ctx.require(a != nullptr, "a Questing Ram spawned");
    auto uid = fe::uidOf(*a);
    setColorFlags(*a, 0xFFFF & ~(1 << 3));
    ctx.check(tryAccept(*a, wool(3)), "the last colour (light blue) is accepted");
    bool done = false;
    for (int t = 0; t < 200 && !done; t += 5) {
        co_await ctx.ticks(5);
        auto* r = fe::actorById(uid);
        ctx.require(r != nullptr, "the ram is still there");
        done = rewarded(*r);
    }
    ctx.check(done, "the ram rewards the completed quest within 10 s");
    co_await ctx.ticks(2);
    std::map<std::string, int> drops;
    if (auto level = ll::service::getLevel())
        for (auto* e : level->getRuntimeActorList())
            if (e && e->isType(ActorType::ItemEntity) && static_cast<int>(e->getDimensionId()) == ctx.dimension()) {
                auto f = e->getFeetPos();
                if (std::abs(f.x - center.x) < 6 && std::abs(f.z - center.z) < 6) drops[game::items::idOf(static_cast<ItemActor*>(e)->item())]++;
            }
    ctx.check(drops["twilightforest:crumble_horn"] == 1 && drops["minecraft:bundle"] == 1, "the crumble horn and the reward bundle dropped");
    ctx.check(triggers > before, "twilightforest:complete_quest_ram fired for the player nearby");
    auto start = gf::landmark::startAt(center.dim, BlockPos{center.x, center.y, center.z});
    ctx.check(start && start->key == kGrove && start->conquered, "the quest grove is conquered");
    removeIn(ctx, box, "minecraft:item");
    removeIn(ctx, box, kRam);
    removeActor(uid);
});

// ---- the grove ---------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("questcamps.grove_layout", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1945, 40);
    // the generation point is 13 blocks in from the template's corner; the template goes 2 below
    // the stored piece (placePieceAdjusted(-2))
    int  gx = box.minX + 14, gz = box.minZ + 14, gy = box.minY - 1;
    auto piece = makeGrove(gx, gy, gz);
    ctx.check(piece->typeId == kGrovePiece, "the piece type TFQuest1");
    ctx.check(piece->bbox.minX == gx - 12 && piece->bbox.minZ == gz - 12 && piece->bbox.minY == gy + 2, "the stored box starts at (x - 12, y + 2, z - 12)");
    ctx.check(piece->bbox.maxX - piece->bbox.minX == 26 && piece->bbox.maxZ - piece->bbox.minZ == 26, "the box is 27 x 27");
    auto homesBefore = pendingHomes();
    {
        st::BlockSourceRegion region(*ctx.region(), BlockPos{gx, gy, gz});
        core::jrand::LegacyRandom random(1900);
        BlockBox chunkBox{box.minX, box.minY - 4, box.minZ, box.maxX, box.maxY, box.maxZ};
        piece->postProcess(region, random, chunkBox, BlockPos{gx, gy, gz});
    }
    co_await ctx.ticks(2);
    int droppers = 0;
    for (int x = gx - 12; x <= gx + 14; ++x)
        for (int z = gz - 12; z <= gz + 14; ++z) droppers += ctx.blockAt(core::GlobalPos{ctx.dimension(), x, gy + 3, z}) == "minecraft:dropper";
    ctx.check(droppers == 4, "four offering droppers (got " + std::to_string(droppers) + ")");
    bool ram = false;
    if (auto level = ll::service::getLevel())
        for (auto* e : level->getRuntimeActorList())
            if (e && e->getTypeName() == kRam && std::abs(e->getFeetPos().x - (gx + 1.5F)) < 2 && std::abs(e->getFeetPos().z - (gz + 1.5F)) < 2) ram = true;
    ctx.check(ram || pendingHomes() > homesBefore, "the quest_ram marker placed a Questing Ram on the dais");
    co_await ctx.ticks(5);
    bool homed = false;
    if (auto level = ll::service::getLevel())
        for (auto* e : level->getRuntimeActorList())
            if (e && e->getTypeName() == kRam)
                if (auto* m = fe::asMob(*e); m && gf::entity::home(*m)) homed = gf::entity::home(*m)->radius == 13 || homed;
    ctx.check(homed, "the grove's ram has its home point (radius 13)");
    removeIn(ctx, box, kRam);
    BlockBox grove{gx - 12, gy, gz - 12, gx + 14, gy + 9, gz + 14};
    removeIn(ctx, grove, "minecraft:item");
});

// ---- camps -------------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("questcamps.camp_assembly", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1960, 64);
    BlockPos origin{box.minX + 32, box.minY, box.minZ + 32};
    st::BlockSourceRegion     region(*ctx.region(), origin);
    core::jrand::LegacyRandom random(424242);
    auto gen    = st::ext::contextOf(region, random, 1919);
    auto pieces = st::ext::assemble("gloamingforest:camp/structure_start", origin, -1, gen);
    ctx.require(!pieces.empty(), "the camp assembled");
    std::map<std::string, int> kinds;
    std::string                layout;
    for (auto const& p : pieces) {
        auto tag = p->save();
        auto id  = tag.value("Template", std::string{});
        if (id.find("/tent/") != std::string::npos) kinds["tent"]++;
        else if (id.find("/path/") != std::string::npos) kinds["path"]++;
        else if (id.find("/deco/") != std::string::npos) kinds["deco"]++;
        else if (id.find("campfire") != std::string::npos) kinds["start"]++;
        layout += id + "@" + std::to_string(p->bbox.minX - origin.x) + "," + std::to_string(p->bbox.minY - origin.y) + "," + std::to_string(p->bbox.minZ - origin.z) +
                  "/" + std::to_string(p->bbox.maxX - p->bbox.minX) + "x" + std::to_string(p->bbox.maxZ - p->bbox.minZ) + ";";
    }
    ctx.info("camp pieces " + std::to_string(pieces.size()) + ": " + layout);
    ctx.check(kinds["start"] == 1 && pieces.front()->save().value("Template", std::string{}).find("campfire") != std::string::npos, "one campfire start, first");
    ctx.check(kinds["tent"] >= 1, "at least one tent");
    ctx.check(kinds["path"] >= 1, "at least one path");
    ctx.check(kinds["deco"] >= 1, "at least one decoration");
    // no two pieces overlap (the jigsaw's collision rule)
    bool overlap = false;
    for (std::size_t i = 0; i < pieces.size(); ++i)
        for (std::size_t j = i + 1; j < pieces.size(); ++j) overlap = overlap || pieces[i]->bbox.intersects(pieces[j]->bbox);
    ctx.check(!overlap, "no two camp pieces overlap");
    // the seeded layout's golden hash (template ids and boxes relative to the origin)
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(core::fnv1a64(layout)));
    std::string hash = buf;
    ctx.info("camp layout hash " + hash);
    auto it = goldens().find("camp_layout");
    ctx.check(it != goldens().end() && it->second == hash, "the golden camp layout hash (got " + hash + ")");
    // the pieces placed: wool tents, a campfire, path blocks
    BlockBox all = pieces.front()->bbox;
    for (auto const& p : pieces) {
        all.minX = std::min(all.minX, p->bbox.minX), all.minY = std::min(all.minY, p->bbox.minY), all.minZ = std::min(all.minZ, p->bbox.minZ);
        all.maxX = std::max(all.maxX, p->bbox.maxX), all.maxY = std::max(all.maxY, p->bbox.maxY), all.maxZ = std::max(all.maxZ, p->bbox.maxZ);
    }
    ctx.require(all.minX >= box.minX && all.maxX <= box.maxX && all.minZ >= box.minZ && all.maxZ <= box.maxZ, "the camp fits the test area");
    BelowGuard guard(ctx, BlockBox{all.minX, box.minY - 4, all.minZ, all.maxX, box.minY - 1, all.maxZ});
    core::jrand::LegacyRandom post(7);
    for (auto& p : pieces) p->postProcess(region, post, BlockBox{all.minX, all.minY - 4, all.minZ, all.maxX, all.maxY + 4, all.maxZ}, origin);
    co_await ctx.ticks(2);
    int fire = 0, paths = 0;
    for (int x = all.minX; x <= all.maxX; ++x)
        for (int z = all.minZ; z <= all.maxZ; ++z)
            for (int y = box.minY - 2; y <= box.minY + 1; ++y) {
                auto id = ctx.blockAt(core::GlobalPos{ctx.dimension(), x, y, z});
                fire += id == "minecraft:campfire";
                paths += id == "minecraft:grass_path" || id == "minecraft:dirt_path";
            }
    ctx.check(fire == 1, "the campfire burns at the start");
    ctx.check(paths > 4, "the paths are laid on the ground (" + std::to_string(paths) + " path blocks)");
    removeIn(ctx, box, "minecraft:painting");
});

// ---- template features -----------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("questcamps.template_goldens", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1980, 20);
    BlockPos origin{box.minX + 1, box.minY + 1, box.minZ + 1};
    std::string report;
    for (auto const& id : allTemplates()) {
        auto h = rawHash(ctx, id, origin);
        report += id + "=" + h + " ";
        auto it = goldens().find(id);
        ctx.check(it != goldens().end() && it->second == h, "the golden hash of " + id + " (got " + h + ")");
    }
    ctx.info("template hashes " + report);
});

GF_REGISTER_SELFTEST("questcamps.druid_hut_markers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(1920, 48);
    auto origin = chunkOrigin(box);
    BelowGuard guard(ctx, BlockBox{box.minX, box.minY - 16, box.minZ, box.maxX, box.minY - 1, box.maxZ});
    auto       config = featureConfig("twilightforest:druid_hut");
    ctx.check(config.contains("palette_choices") && config.contains("target_palettes"), "the druid hut's SwizzleConfig");
    st::BlockSourceRegion region(*ctx.region(), origin);
    bool  basement = false, placed = false;
    std::set<std::string> huts;
    int   chests = 0;
    for (std::int64_t seed = 1; seed <= 12 && (!basement || huts.size() < 2); ++seed) {
        core::jrand::LegacyRandom random(seed);
        if (!placeDruidHut(region, origin, random, config)) continue;
        placed = true;
        auto const& r = lastFeatureReport();
        huts.insert(r.templateId);
        chests += r.chests;
        if (!r.basement.empty()) basement = true;
        ctx.info("hut " + r.templateId + " basement " + (r.basement.empty() ? "-" : r.basement) + " markers " + std::to_string(r.markers.size()));
    }
    ctx.check(placed, "a druid hut was placed on the flat ground");
    ctx.check(chests > 0, "the loot / barrel markers became containers");
    ctx.check(basement, "a seed with a basement (nextBoolean) placed one 12 below");
    removeIn(ctx, box, "minecraft:painting");
});

GF_REGISTER_SELFTEST("questcamps.wells", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(1925, 48);
    auto origin = chunkOrigin(box);
    BelowGuard guard(ctx, BlockBox{box.minX, box.minY - 16, box.minZ, box.maxX, box.minY - 1, box.maxZ});
    st::BlockSourceRegion region(*ctx.region(), origin);
    auto simpleConfig = featureConfig("twilightforest:simple_well");
    auto fancyConfig  = featureConfig("twilightforest:fancy_well");
    core::jrand::LegacyRandom r1(5);
    ctx.check(placeSimpleWell(region, origin, r1, simpleConfig), "a simple well was placed");
    auto simple = lastFeatureReport();
    ctx.check(simple.placementPos.y == box.minY, "the simple well's top sits one above the averaged ground");
    ctx.check(std::count(simple.markers.begin(), simple.markers.end(), "lootS") + std::count(simple.markers.begin(), simple.markers.end(), "lootW") == 2,
              "the bottom's two loot markers were handled");
    core::jrand::LegacyRandom r2(6);
    ctx.check(placeFancyWell(region, origin, r2, fancyConfig), "a fancy well was placed");
    auto fancy = lastFeatureReport();
    ctx.check(fancy.chests == 1, "the fancy well's barrel holds fancy_well loot");
    ctx.check(fancy.placementPos.y == box.minY - 1, "the fancy well's top sits on the averaged ground");
});

GF_REGISTER_SELFTEST("questcamps.graveyard", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1935, 48);
    BelowGuard guard(ctx, BlockBox{box.minX, box.minY - 4, box.minZ, box.maxX, box.minY - 1, box.maxZ});
    st::BlockSourceRegion region(*ctx.region(), BlockPos{box.minX + 24, box.minY, box.minZ + 24});
    BlockPos origin{box.minX + 20, box.minY, box.minZ + 20};
    int      wraiths = 0, graves = 0;
    for (std::int64_t seed = 1; seed <= 6 && wraiths == 0; ++seed) {
        core::jrand::LegacyRandom random(seed);
        if (!placeGraveyard(region, origin, random)) continue;
        auto const& r = lastFeatureReport();
        graves        = r.graves;
        wraiths += r.wraiths;
        ctx.info("graveyard rotation " + std::to_string(st::rotationIndex(r.rotation)) + " mirror " + std::to_string(static_cast<int>(r.mirror)) + " graves " +
                 std::to_string(r.graves) + " wraiths " + std::to_string(r.wraiths) + " chests " + std::to_string(r.chests));
    }
    ctx.check(graves == 8, "eight graves in the grid (got " + std::to_string(graves) + ")");
    ctx.check(wraiths > 0, "a full grave got its wraith");
    co_await ctx.ticks(2);
    removeIn(ctx, box, "twilightforest:wraith");
});

GF_REGISTER_SELFTEST("questcamps.ruins", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(1950, 48);
    auto origin = chunkOrigin(box);
    st::BlockSourceRegion region(*ctx.region(), origin);
    std::set<std::string> seen;
    for (std::int64_t seed = 1; seed <= 8; ++seed) {
        core::jrand::LegacyRandom random(seed);
        if (placeGroveRuins(region, origin, random)) seen.insert(lastFeatureReport().templateId);
    }
    ctx.check(seen.size() == 2, "both grove ruins (pillar and arch) were placed");
    core::jrand::LegacyRandom random(3);
    ctx.check(placeStoneCircle(region, origin, random), "a stone circle was placed");
    ctx.check(lastFeatureReport().templateId == "gloamingforest:feature/ruins/stone_circle", "the stone circle template");
});

// ---- the client showcase (runs only on request of the client scenario) -----------------------------------------

namespace {
bool displayRequested(int dim) {
    auto r = gf::selftest::runServerCommand("scoreboard objectives remove gf_questcamps_display", dim);
    return r.ok && r.successCount > 0;
}
} // namespace

// Builds what the client scenario screenshots (tools/e2e/client/scenarios/questcamps.ps1): a Questing
// Ram with five colours (its wool segments and the crosshair indicator), the quest grove, a seeded camp
// and the template features side by side; each stage is held 10 s, then everything is restored.
GF_REGISTER_SELFTEST("questcamps.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!displayRequested(ctx.dimension())) {
        ctx.info("not requested by the client scenario (scoreboard objective gf_questcamps_display): the showcase is skipped");
        co_return;
    }
    auto box = co_await ctx.flatArea(1900, 120);
    BelowGuard guard(ctx, BlockBox{box.minX, box.minY - 16, box.minZ, box.maxX, box.minY - 1, box.maxZ});
    ctx.info("area at " + std::to_string(box.minX) + " " + std::to_string(box.minY) + " " + std::to_string(box.minZ));
    // the ram at (+6, +6) with white, orange, lime, blue and black
    auto* a = spawnRam(ctx, core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY, box.minZ + 6});
    ctx.require(a != nullptr, "a Questing Ram spawned");
    auto uid = fe::uidOf(*a);
    setColorFlags(*a, 1 << 0 | 1 << 1 | 1 << 5 | 1 << 11 | 1 << 15);
    if (auto* m = fe::asMob(*a)) gf::entity::setHome(*m, core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY, box.minZ + 6}, 2);
    ctx.info("ram ready");
    co_await ctx.ticks(200);
    // the grove at (+30, +16)
    {
        auto piece = makeGrove(box.minX + 30, box.minY - 1, box.minZ + 16);
        st::BlockSourceRegion     region(*ctx.region(), BlockPos{box.minX + 30, box.minY, box.minZ + 16});
        core::jrand::LegacyRandom random(1900);
        piece->postProcess(region, random, BlockBox{box.minX, box.minY - 4, box.minZ, box.maxX, box.maxY, box.maxZ}, BlockPos{box.minX + 30, box.minY - 1, box.minZ + 16});
    }
    ctx.info("grove ready");
    co_await ctx.ticks(200);
    // a seeded camp at (+30, +80)
    {
        BlockPos                  origin{box.minX + 30, box.minY, box.minZ + 80};
        st::BlockSourceRegion     region(*ctx.region(), origin);
        core::jrand::LegacyRandom random(424242);
        auto gen    = st::ext::contextOf(region, random, 1919);
        auto pieces = st::ext::assemble("gloamingforest:camp/structure_start", origin, -1, gen);
        core::jrand::LegacyRandom post(7);
        for (auto& p : pieces) p->postProcess(region, post, BlockBox{box.minX, box.minY - 4, box.minZ, box.maxX, box.maxY, box.maxZ}, origin);
    }
    ctx.info("camp ready");
    co_await ctx.ticks(200);
    // the template features in a row at z +100..+116 (each in its own chunk)
    {
        int  cz = ((box.minZ + 100) >> 4) << 4;
        auto at = [&](int i) { return BlockPos{(((box.minX + 4) >> 4) << 4) + 16 * i + 16, box.minY, cz + 4}; };
        st::BlockSourceRegion region(*ctx.region(), at(2));
        core::jrand::LegacyRandom r(11);
        (void)placeDruidHut(region, at(0), r, featureConfig("twilightforest:druid_hut"));
        (void)placeSimpleWell(region, at(1), r, featureConfig("twilightforest:simple_well"));
        (void)placeFancyWell(region, at(2), r, featureConfig("twilightforest:fancy_well"));
        (void)placeGroveRuins(region, at(3), r);
        (void)placeStoneCircle(region, at(4), r);
        (void)placeGraveyard(region, BlockPos{box.minX + 90, box.minY, box.minZ + 92}, r); // inside the restored area
        ctx.info("features at " + std::to_string(at(0).x) + " " + std::to_string(at(0).z));
    }
    ctx.info("features ready");
    co_await ctx.ticks(300);
    removeIn(ctx, box, kRam);
    removeIn(ctx, box, "twilightforest:wraith");
    removeIn(ctx, box, "minecraft:painting");
    removeActor(uid);
});

} // namespace gf::feature::questcamps::tests

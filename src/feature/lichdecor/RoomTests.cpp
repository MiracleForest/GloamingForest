// GloamingForest - U22 (lichdecor): golden volume hashes of the lich tower room templates (rule
// R19): every template of assets/structures/lich/rooms/<size> is placed alone (no rotation, no
// processors) in a flat area, hashed with snapshotVolume and cleared again. A template whose
// golden is in kGolden must match it; the others log "golden <id> <hash>" so the table can be filled
// from a run (an empty table passes, as U21's lich.tower_layout does before its goldens exist).

#include <map>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "core/GlobalPos.h"
#include "feature/structure/Region.h"
#include "feature/structure/Structure.h"
#include "game/Blocks.h"
#include "selftest/SelfTest.h"

namespace gf::feature::lichdecor {

namespace {

namespace gs = gf::structure;

struct SizeSet {
    char const*              size;
    std::vector<char const*> names;
};

std::vector<SizeSet> const& roomSets() {
    static std::vector<SizeSet> const k{
        {"3x3",
         {"curved_roof", "double", "fence_roof", "flat_roof", "flat_side_roof", "lava_well", "lookout", "pointed_roof", "pyramid_roof", "sapling",
          "short_lookout", "slabs_roof", "taller_double", "tree", "water_fountain", "water_well", "wither_rose"}},
        {"5x5",
         {"altar", "beard_checkered", "beard_chiseled", "beard_chunks", "beard_flat", "beard_staggered", "beard_trim", "curved_roof", "desk",
          "elbow_junction", "fence_roof", "flat_roof", "flat_side_roof", "full_junction", "full_junction_2", "full_junction_3", "ladder", "lectern",
          "library", "lone_chest", "pointed_roof", "potion", "pyramid_roof", "slabs_roof", "spawner_1", "spawner_2", "spawner_3", "spawner_4",
          "spawner_5", "straight_junction", "t_junction", "trinity", "webbed_spawner", "zombie_trap"}},
        {"7x7",
         {"altar",          "altars",          "barbed_webs",       "beard_checkered", "beard_chiseled", "beard_chunks",     "beard_flat",
          "beard_staggered", "beard_trim",     "book_staircases",   "cactus",          "corner_caging",  "curved_roof",      "desk",
          "elbow_chest",    "elbow_junction",  "fence_roof",        "flat_roof",       "flat_side_roof", "full_junction",    "full_junction_1",
          "full_junction_2", "garden_lab",     "grave",             "guarded_chest",   "jar_study",      "library_hall",     "nursery",
          "pedestal_junction", "pedestal_library", "pointed_roof",  "potion",          "potion_lab",     "pyramid_roof",     "ritual",
          "shelved_jars",   "slabs_roof",      "straight_junction", "t_junction",      "tiered_library", "tiered_study",     "walled_library"}},
        {"9x9",
         {"altar",        "archives",       "beard_checkered", "beard_chiseled", "beard_chunks",   "beard_flat",        "beard_staggered", "beard_trim",
          "cauldron_keep", "center_decor",  "centerpiece",     "classic_library", "curved_roof",   "elbow_junction",    "enchanting_prison", "fence_roof",
          "flat_roof",    "flat_side_roof", "full_junction",   "holding",        "keepsake_casket", "large_study",      "lectern_hall",    "library_junction",
          "lockup",       "mossy_junction", "pointed_roof",    "pyramid_roof",   "seven",          "slabs_roof",        "straight_junction", "study",
          "t_junction",   "tiered_library", "tiered_spawner",  "tiered_study",   "winding_ways"}},
    };
    return k;
}

/// Committed goldens (template id -> volume hash), filled from the logged lines of a run.
std::map<std::string, std::string> const& goldens() {
    static std::map<std::string, std::string> const k{};
    return k;
}

ll::coro::CoroTask<> placeAll(selftest::Context& ctx, char const* size) {
    auto box = co_await ctx.flatArea(2280, 16, 32);
    auto* source = ctx.region();
    ctx.require(source != nullptr, "the test dimension has a block source");
    SizeSet const* set = nullptr;
    for (auto const& s : roomSets())
        if (std::string(s.size) == size) set = &s;
    ctx.require(set != nullptr, "known room size");
    BlockPos origin{box.minX, box.minY, box.minZ};
    auto const* air = game::blockWith("minecraft:air");
    int         checked = 0, logged = 0;
    for (auto const* name : set->names) {
        std::string id = std::string("twilightforest:lich_tower/") + size + "/" + name;
        auto const* t  = gs::templates::get(id);
        if (!t) {
            ctx.check(false, "template " + id + " is loaded");
            continue;
        }
        {
            gs::BlockSourceRegion region(*ctx.region(), origin);
            gs::TemplateSettings  settings;
            settings.seed = 22;
            ctx.check(gs::templates::place(id, region, origin, settings), "template " + id + " places");
        }
        core::BlockBox vol{origin.x, origin.y, origin.z, origin.x + t->sizeX - 1, origin.y + t->sizeY - 1, origin.z + t->sizeZ - 1};
        auto           hash = ctx.snapshotVolume(vol);
        auto           it   = goldens().find(id);
        if (it != goldens().end()) {
            ctx.check(hash == it->second, id + " matches its golden (" + hash + " vs " + it->second + ")");
            ++checked;
        } else {
            ctx.info("golden " + id + " " + hash);
            ++logged;
        }
        // clear for the next one
        if (air && ctx.region())
            for (int x = vol.minX; x <= vol.maxX; ++x)
                for (int y = vol.minY; y <= vol.maxY; ++y)
                    for (int z = vol.minZ; z <= vol.maxZ; ++z) game::setBlock(*ctx.region(), BlockPos{x, y, z}, *air);
        co_await ctx.ticks(1);
    }
    ctx.info(std::string(size) + ": " + std::to_string(set->names.size()) + " templates, " + std::to_string(checked) + " golden, " + std::to_string(logged) +
             " logged");
}

} // namespace

} // namespace gf::feature::lichdecor

GF_REGISTER_SELFTEST("lichdecor.rooms_3x3", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await gf::feature::lichdecor::placeAll(ctx, "3x3"); });
GF_REGISTER_SELFTEST("lichdecor.rooms_5x5", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await gf::feature::lichdecor::placeAll(ctx, "5x5"); });
GF_REGISTER_SELFTEST("lichdecor.rooms_7x7", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await gf::feature::lichdecor::placeAll(ctx, "7x7"); });
GF_REGISTER_SELFTEST("lichdecor.rooms_9x9", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await gf::feature::lichdecor::placeAll(ctx, "9x9"); });

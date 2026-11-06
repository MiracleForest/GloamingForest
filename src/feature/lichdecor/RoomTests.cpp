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
    static std::map<std::string, std::string> const k{
        {"twilightforest:lich_tower/3x3/curved_roof", "6300f357e7e716b7"},
        {"twilightforest:lich_tower/3x3/double", "ce2708a1848783ca"},
        {"twilightforest:lich_tower/3x3/fence_roof", "db092899e1a3579a"},
        {"twilightforest:lich_tower/3x3/flat_roof", "06fcce24dcb9f9c7"},
        {"twilightforest:lich_tower/3x3/flat_side_roof", "08215ddb1c5d1f6e"},
        {"twilightforest:lich_tower/3x3/lava_well", "0ea86f56c7895d0b"},
        {"twilightforest:lich_tower/3x3/lookout", "b616a1995ea51d9a"},
        {"twilightforest:lich_tower/3x3/pointed_roof", "0e0102b68ce81ba0"},
        {"twilightforest:lich_tower/3x3/pyramid_roof", "5e66798a95226684"},
        {"twilightforest:lich_tower/3x3/sapling", "cf029f9884455654"},
        {"twilightforest:lich_tower/3x3/short_lookout", "f387003feec02476"},
        {"twilightforest:lich_tower/3x3/slabs_roof", "1ac2c317032ab677"},
        {"twilightforest:lich_tower/3x3/taller_double", "c795be9c4bd2cc0c"},
        {"twilightforest:lich_tower/3x3/tree", "5831c0c9fda879b4"},
        {"twilightforest:lich_tower/3x3/water_fountain", "fde4cffc22857493"},
        {"twilightforest:lich_tower/3x3/water_well", "268027907bea055d"},
        {"twilightforest:lich_tower/3x3/wither_rose", "b14f8b46f38a3564"},
        {"twilightforest:lich_tower/5x5/altar", "9f6c5bb00f223a8e"},
        {"twilightforest:lich_tower/5x5/beard_checkered", "3b49c5435bcc0f90"},
        {"twilightforest:lich_tower/5x5/beard_chiseled", "8e62a96dc451ce08"},
        {"twilightforest:lich_tower/5x5/beard_chunks", "2d13553cd535b23c"},
        {"twilightforest:lich_tower/5x5/beard_flat", "e7331a8a1b4c0efc"},
        {"twilightforest:lich_tower/5x5/beard_staggered", "ab1cc565e796ccd5"},
        {"twilightforest:lich_tower/5x5/beard_trim", "180aa25bd04c6a82"},
        {"twilightforest:lich_tower/5x5/curved_roof", "4e12bf22aad2f66a"},
        {"twilightforest:lich_tower/5x5/desk", "8f171fdf26897022"},
        {"twilightforest:lich_tower/5x5/elbow_junction", "93ee718704f5a109"},
        {"twilightforest:lich_tower/5x5/fence_roof", "a12b2d35f1d44a3d"},
        {"twilightforest:lich_tower/5x5/flat_roof", "dfa0561ca59940cf"},
        {"twilightforest:lich_tower/5x5/flat_side_roof", "de5975fbe1bc2bae"},
        {"twilightforest:lich_tower/5x5/full_junction", "967493d9d682b3bb"},
        {"twilightforest:lich_tower/5x5/full_junction_2", "211db7e27894039b"},
        {"twilightforest:lich_tower/5x5/full_junction_3", "584916b060058fff"},
        {"twilightforest:lich_tower/5x5/ladder", "93ea2b1768407dbf"},
        {"twilightforest:lich_tower/5x5/lectern", "cd67b7b6f71a5d9a"},
        {"twilightforest:lich_tower/5x5/library", "025b3b9751c15408"},
        {"twilightforest:lich_tower/5x5/lone_chest", "c511d8ba6f31370f"},
        {"twilightforest:lich_tower/5x5/pointed_roof", "3cb9e56546625a99"},
        {"twilightforest:lich_tower/5x5/potion", "a7d43cf9aa709cdc"},
        {"twilightforest:lich_tower/5x5/pyramid_roof", "e962d600b58a8637"},
        {"twilightforest:lich_tower/5x5/slabs_roof", "3319c6c9254896a8"},
        {"twilightforest:lich_tower/5x5/spawner_1", "cd67b7b6f71a5d9a"},
        {"twilightforest:lich_tower/5x5/spawner_2", "0ab72f720cda04ca"},
        {"twilightforest:lich_tower/5x5/spawner_3", "025b3b9751c15408"},
        {"twilightforest:lich_tower/5x5/spawner_4", "56a365d4b72035ec"},
        {"twilightforest:lich_tower/5x5/spawner_5", "cd67b7b6f71a5d9a"},
        {"twilightforest:lich_tower/5x5/straight_junction", "b710a80bbb1929ea"},
        {"twilightforest:lich_tower/5x5/t_junction", "4a356764d490c392"},
        {"twilightforest:lich_tower/5x5/trinity", "eda3c0dc69b58b12"},
        {"twilightforest:lich_tower/5x5/webbed_spawner", "cd67b7b6f71a5d9a"},
        {"twilightforest:lich_tower/5x5/zombie_trap", "025b3b9751c15408"},
        {"twilightforest:lich_tower/7x7/altar", "6ab69438071454cb"},
        {"twilightforest:lich_tower/7x7/altars", "da76af597e0aa389"},
        {"twilightforest:lich_tower/7x7/barbed_webs", "0bd99a77b487b62b"},
        {"twilightforest:lich_tower/7x7/beard_checkered", "f05dcc075bee6a10"},
        {"twilightforest:lich_tower/7x7/beard_chiseled", "d1e14915c7a86efa"},
        {"twilightforest:lich_tower/7x7/beard_chunks", "94ef3df2a5b3e46f"},
        {"twilightforest:lich_tower/7x7/beard_flat", "73239dd0248f0280"},
        {"twilightforest:lich_tower/7x7/beard_staggered", "0daa1451cdca78ef"},
        {"twilightforest:lich_tower/7x7/beard_trim", "b74e4bae6f03e692"},
        {"twilightforest:lich_tower/7x7/book_staircases", "37f4a01f4ef53205"},
        {"twilightforest:lich_tower/7x7/cactus", "0d9e1c7ac15c6063"},
        {"twilightforest:lich_tower/7x7/corner_caging", "db5d1e46c8ffdacf"},
        {"twilightforest:lich_tower/7x7/curved_roof", "81a769e5c015502e"},
        {"twilightforest:lich_tower/7x7/desk", "33e666b6c7ace7ef"},
        {"twilightforest:lich_tower/7x7/elbow_chest", "b94866ab169a4727"},
        {"twilightforest:lich_tower/7x7/elbow_junction", "a386d6daabb161e7"},
        {"twilightforest:lich_tower/7x7/fence_roof", "8d2a3f333ae97829"},
        {"twilightforest:lich_tower/7x7/flat_roof", "84c925ceb8df252f"},
        {"twilightforest:lich_tower/7x7/flat_side_roof", "3d003c1a49f4de56"},
        {"twilightforest:lich_tower/7x7/full_junction", "c809525e6d9ff741"},
        {"twilightforest:lich_tower/7x7/full_junction_1", "33e2145699752ed1"},
        {"twilightforest:lich_tower/7x7/full_junction_2", "c9870d8558578aca"},
        {"twilightforest:lich_tower/7x7/garden_lab", "38dfd502b95bbab9"},
        {"twilightforest:lich_tower/7x7/grave", "11f638d364923a9d"},
        {"twilightforest:lich_tower/7x7/guarded_chest", "eea708f87f34ec32"},
        {"twilightforest:lich_tower/7x7/jar_study", "d6c3faeac69bb08f"},
        {"twilightforest:lich_tower/7x7/library_hall", "faef5ca1be183ec9"},
        {"twilightforest:lich_tower/7x7/nursery", "7407f851efc201a7"},
        {"twilightforest:lich_tower/7x7/pedestal_junction", "94273e5e76fc47e1"},
        {"twilightforest:lich_tower/7x7/pedestal_library", "f4f402461cb2c147"},
        {"twilightforest:lich_tower/7x7/pointed_roof", "9f2167be721f77b7"},
        {"twilightforest:lich_tower/7x7/potion", "759ca1857dac4b17"},
        {"twilightforest:lich_tower/7x7/potion_lab", "3549cb177f0a23d7"},
        {"twilightforest:lich_tower/7x7/pyramid_roof", "6db5e2926b8034c9"},
        {"twilightforest:lich_tower/7x7/ritual", "65727eb10587c2f4"},
        {"twilightforest:lich_tower/7x7/shelved_jars", "24ce33583a89f6a3"},
        {"twilightforest:lich_tower/7x7/slabs_roof", "9b155726f1516a30"},
        {"twilightforest:lich_tower/7x7/straight_junction", "66427b56c6379349"},
        {"twilightforest:lich_tower/7x7/t_junction", "95ba8281152882bb"},
        {"twilightforest:lich_tower/7x7/tiered_library", "fb33b36da84b28c5"},
        {"twilightforest:lich_tower/7x7/tiered_study", "99a5c7d0e44c6645"},
        {"twilightforest:lich_tower/7x7/walled_library", "faef5ca1be183ec9"},
        {"twilightforest:lich_tower/9x9/altar", "3dfae12b47f77d69"},
        {"twilightforest:lich_tower/9x9/archives", "d5b7f4706731c36d"},
        {"twilightforest:lich_tower/9x9/beard_checkered", "c4641b90d787300b"},
        {"twilightforest:lich_tower/9x9/beard_chiseled", "3183994105b539a1"},
        {"twilightforest:lich_tower/9x9/beard_chunks", "037fa3dc51b069ed"},
        {"twilightforest:lich_tower/9x9/beard_flat", "3541867a946a6234"},
        {"twilightforest:lich_tower/9x9/beard_staggered", "6342703d3f236b85"},
        {"twilightforest:lich_tower/9x9/beard_trim", "824b31e07bfd2534"},
        {"twilightforest:lich_tower/9x9/cauldron_keep", "363e3989ea834035"},
        {"twilightforest:lich_tower/9x9/center_decor", "13c935ef6c9bdc3d"},
        {"twilightforest:lich_tower/9x9/centerpiece", "9670d52a48a6ce3b"},
        {"twilightforest:lich_tower/9x9/classic_library", "4e6be106b3daff53"},
        {"twilightforest:lich_tower/9x9/curved_roof", "688c2c17fddba37b"},
        {"twilightforest:lich_tower/9x9/elbow_junction", "076eebcda02dca1b"},
        {"twilightforest:lich_tower/9x9/enchanting_prison", "44995f90545bb846"},
        {"twilightforest:lich_tower/9x9/fence_roof", "981550a7305867cd"},
        {"twilightforest:lich_tower/9x9/flat_roof", "3dc9dbf52d20a267"},
        {"twilightforest:lich_tower/9x9/flat_side_roof", "f7bbdf1c6550bc9e"},
        {"twilightforest:lich_tower/9x9/full_junction", "8b5398e5f7bca051"},
        {"twilightforest:lich_tower/9x9/holding", "ca34710fe786282d"},
        {"twilightforest:lich_tower/9x9/keepsake_casket", "cba83188048adda5"},
        {"twilightforest:lich_tower/9x9/large_study", "a4f2020da02b9193"},
        {"twilightforest:lich_tower/9x9/lectern_hall", "d5b7f4706731c36d"},
        {"twilightforest:lich_tower/9x9/library_junction", "fd41437a7697dce1"},
        {"twilightforest:lich_tower/9x9/lockup", "ee0a0aa628c62a59"},
        {"twilightforest:lich_tower/9x9/mossy_junction", "9aad9c701a9a3b27"},
        {"twilightforest:lich_tower/9x9/pointed_roof", "0c69d00d896d45a0"},
        {"twilightforest:lich_tower/9x9/pyramid_roof", "93580fec737e1a7a"},
        {"twilightforest:lich_tower/9x9/seven", "43ce6fcb704f2371"},
        {"twilightforest:lich_tower/9x9/slabs_roof", "dc9c37e47d5eec83"},
        {"twilightforest:lich_tower/9x9/straight_junction", "79d11f17b274925d"},
        {"twilightforest:lich_tower/9x9/study", "55e47eaf6cc985cb"},
        {"twilightforest:lich_tower/9x9/t_junction", "5b7d6a727955f649"},
        {"twilightforest:lich_tower/9x9/tiered_library", "4ebf84a52f7d5c9e"},
        {"twilightforest:lich_tower/9x9/tiered_spawner", "9bb8b9dda96b22d2"},
        {"twilightforest:lich_tower/9x9/tiered_study", "c83015e1e2b76de4"},
        {"twilightforest:lich_tower/9x9/winding_ways", "9c641d42172f763b"},
    };
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

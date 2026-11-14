// U1 `biomes` selftests (dx 100-199, rule R12).
//
//   biomes.layer_golden        the biome source reproduces F13's oracle samples (3 seeds, main and
//                              wide grids, noise biomes) and is the one installed in worldgen
//   biomes.key_biome_quadrants every 8x8 key-layer region holds the four key biomes
//   biomes.companions          key biomes are ringed by their companions
//   biomes.thornlands_border   the final plateau only touches thornlands
//   biomes.streams             streams divide differing biomes, never next to the excluded ones
//   biomes.vertical_layers     underground below the columns (highlands: highlands_underground)
//   biomes.tags                upstream's biome tags (17 valid_* + in_gloaming_forest)
//   biomes.engine_biomes       the 22 behavior pack biomes are registered with the game, with upstream's
//                              water colours
//   biomes.spawn_tables        BiomeHelper's tables equal upstream's biome JSON (spawners, costs, probability)
//   biomes.spawn_costs         the spawn costs leave a type out where its charge exceeds its budget
//   biomes.generated           a freshly generated Gloaming Forest chunk stores the source's biomes
//   biomes.terrain_blend       sampleTerrain through the extension equals the provider's
//   biomes.survey              (not a check) a place inside each surface biome, for the client scenario
//   biomes.vanilla_spawns      the vanilla mobs of upstream's tables spawn naturally in the Gloaming Forest
//   biomes.water_controls[_restore] (client scenario only, a tagged player) biomes painted into
//                              another dimension for the water colour controls

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>

#include "ll/api/service/Bedrock.h"

#include <tuple>
#include <vector>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkLocalHeight.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/biomes/BiomeLayers.h"
#include "core/biomes/Colors.h"
#include "feature/biomes/Biomes.h"
#include "feature/ext/biomes/Biomes.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace gf::feature::biomes {

namespace {

#include "feature/biomes/Golden.inc"

using core::biomes::BiomeProvider;
using selftest::Context;

std::unique_ptr<BiomeProvider> make(Context& ctx, std::int64_t seed) {
    auto data = sourceData();
    ctx.require(data != nullptr, "the biome layer stacks and terrain grid load from the mod's data folder");
    return std::make_unique<BiomeProvider>(data->grid, data->stacks, seed);
}

std::string idOf(Biome const& b) {
    auto const& name = b.mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

} // namespace

GF_REGISTER_SELFTEST("biomes.layer_golden", [](Context& ctx) {
    std::map<std::int64_t, std::unique_ptr<BiomeProvider>> providers;
    auto                                                   of = [&](std::int64_t seed) -> BiomeProvider& {
        auto& p = providers[seed];
        if (!p) p = make(ctx, seed);
        return *p;
    };
    int bad = 0;
    for (auto const& g : kGoldenMain) {
        auto const& got = of(g.seed).mainBiome(g.qx, g.qz);
        if (got != g.biome && ++bad <= 5) ctx.check(false, std::format("seed {} main ({}, {}): {} != {}", g.seed, g.qx, g.qz, got, g.biome));
    }
    for (auto const& g : kGoldenNoise) {
        auto const& got = of(g.seed).noiseBiome(g.qx, g.qy, g.qz);
        if (got != g.biome && ++bad <= 10) ctx.check(false, std::format("seed {} noise ({}, {}, {}): {} != {}", g.seed, g.qx, g.qy, g.qz, got, g.biome));
    }
    ctx.check(bad == 0, std::format("{} of {} oracle samples differ", bad, std::size(kGoldenMain) + std::size(kGoldenNoise)));
    // the installed source is this one, for the world's seed
    auto seed = worldgen::seed();
    auto& p   = of(seed);
    int   off = 0;
    for (int i = 0; i < 64; ++i) {
        int bx = (i * 977) % 4000 - 2000, bz = (i * 613) % 4000 - 2000;
        off += worldgen::mainBiomeAt(bx, bz) != p.mainBiome(bx >> 2, bz >> 2);
    }
    ctx.check(off == 0, std::format("worldgen::mainBiomeAt follows the upstream biome source ({} of 64 columns differ)", off));
    ctx.info(std::format("{} oracle samples, world seed {}", std::size(kGoldenMain) + std::size(kGoldenNoise), seed));
});

GF_REGISTER_SELFTEST("biomes.key_biome_quadrants", [](Context& ctx) {
    auto p = make(ctx, 0);
    // the key layer runs before 6 zooms (64 quarts per pixel): an 8x8 key region spans 512 quarts;
    // sample 4 regions, each must hold all four key biomes
    char const* keys[] = {"gloamingforest:glacier", "gloamingforest:fire_swamp", "gloamingforest:dark_forest_center", "gloamingforest:final_plateau"};
    for (int rx = -1; rx <= 0; ++rx) {
        for (int rz = -1; rz <= 0; ++rz) {
            std::set<std::string> seen;
            for (int qz = 0; qz < 512; qz += 4)
                for (int qx = 0; qx < 512; qx += 4) seen.insert(p->mainBiome(rx * 512 + qx, rz * 512 + qz));
            for (char const* k : keys) ctx.check(seen.contains(k), std::format("region ({}, {}) holds {}", rx, rz, k));
        }
    }
});

GF_REGISTER_SELFTEST("biomes.companions", [](Context& ctx) {
    auto p = make(ctx, 123456789);
    std::map<std::string, std::string> companion = {
        {"gloamingforest:fire_swamp", "gloamingforest:swamp"},
        {"gloamingforest:glacier", "gloamingforest:snowy_forest"},
        {"gloamingforest:dark_forest_center", "gloamingforest:dark_forest"},
        {"gloamingforest:final_plateau", "gloamingforest:highlands"},
    };
    // walk outwards from each key biome cell found: within 48 quarts its companion appears
    std::set<std::string> verified;
    for (int qz = -512; qz < 512 && verified.size() < companion.size(); qz += 16) {
        for (int qx = -512; qx < 512; qx += 16) {
            auto const& b  = p->mainBiome(qx, qz);
            auto        it = companion.find(b);
            if (it == companion.end() || verified.contains(b)) continue;
            bool found = false;
            for (int r = 1; r <= 48 && !found; ++r)
                for (int d = -r; d <= r && !found; ++d)
                    found = p->mainBiome(qx + d, qz + r) == it->second || p->mainBiome(qx + d, qz - r) == it->second
                         || p->mainBiome(qx + r, qz + d) == it->second || p->mainBiome(qx - r, qz + d) == it->second;
            ctx.check(found, std::format("{} at ({}, {}) has {} around it", b, qx, qz, it->second));
            verified.insert(b);
        }
    }
    ctx.check(verified.size() == companion.size(), std::format("{} of 4 key biomes found", verified.size()));
});

GF_REGISTER_SELFTEST("biomes.thornlands_border", [](Context& ctx) {
    auto p       = make(ctx, 0);
    int  plateau = 0, touching = 0, wrong = 0;
    for (int qz = -768; qz < 768; qz += 2) {
        for (int qx = -768; qx < 768; qx += 2) {
            if (p->mainBiome(qx, qz) != "gloamingforest:final_plateau") continue;
            ++plateau;
            for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                auto const& n = p->mainBiome(qx + dx, qz + dz);
                if (n == "gloamingforest:final_plateau") continue;
                ++touching;
                wrong += n != "gloamingforest:thornlands";
            }
        }
    }
    ctx.check(plateau > 0, "a final plateau within 768 quarts");
    ctx.check(touching > 0 && wrong == 0, std::format("the final plateau touches only thornlands ({} of {} neighbours are not)", wrong, touching));
});

GF_REGISTER_SELFTEST("biomes.streams", [](Context& ctx) {
    std::int64_t const seed    = -8346138912345678901LL;
    auto               p       = make(ctx, seed);
    int                streams = 0;
    for (int qz = -512; qz < 512; qz += 3) {
        for (int qx = -512; qx < 512; qx += 3) {
            if (p->mainBiome(qx, qz) != "gloamingforest:stream") continue;
            ++streams;
        }
    }
    ctx.check(streams > 50, std::format("streams run between the biomes ({} sampled cells)", streams));
    // the stream's terrain column is the river's: low and flat
    auto const* col = p->terrainColumn("gloamingforest:stream");
    ctx.require(col != nullptr, "the stream has a terrain column");
    ctx.check(col->depth == -0.1 && col->scale == 0.001 && col->weight == 1.35, "the stream's depth / scale / weight are upstream's");

    // The seam rules (biomes_along_streams' seam layer over random_forest_biomes, upstream's
    // SeamLayer#apply): a pixel becomes the stream exactly when one of its four neighbours differs
    // from it and neither of the two is an excluded neighbour biome nor the two an excluded pair;
    // checked against the stack's own layers with the rules read from upstream's JSON.
    auto data = sourceData();
    auto it   = data->stacks.entries.find("gloamingforest:biomes_along_streams");
    ctx.require(it != data->stacks.entries.end(), "the biomes_along_streams stack");
    auto const& seamJson = it->second.at("filtered_layer").at("parent");
    ctx.require(seamJson.value("layer_type", "") == "gloamingforest:seam", "the stream input is a seam layer");
    core::biomes::BiomeNames names;
    auto                     seam   = core::biomes::buildLayer(seamJson, data->stacks, names, seed);
    auto                     forest = core::biomes::buildLayer(seamJson.at("parent"), data->stacks, names, seed);
    std::set<std::string>    excluded;
    for (auto const& e : seamJson.at("excluded_neighbor_biomes")) excluded.insert(e.get<std::string>());
    std::set<std::pair<std::string, std::string>> pairs;
    for (auto const& e : seamJson.at("excluded_biome_intersections")) {
        pairs.emplace(e.at(0).get<std::string>(), e.at(1).get<std::string>());
        pairs.emplace(e.at(1).get<std::string>(), e.at(0).get<std::string>());
    }
    auto partition = [&](std::string const& a, std::string const& b) {
        return a != b && !excluded.contains(a) && !excluded.contains(b) && !pairs.contains({a, b});
    };
    int seams = 0, wrong = 0, besideExcluded = 0, acrossPair = 0;
    for (int z = -96; z < 96; ++z) {
        for (int x = -96; x < 96; ++x) {
            auto const&        mid = names.name(forest->get(x, z));
            bool               cut = false;
            for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                auto const& n = names.name(forest->get(x + dx, z + dz));
                cut          = cut || partition(mid, n);
                if (mid != n && (excluded.contains(mid) || excluded.contains(n))) ++besideExcluded;
                if (mid != n && pairs.contains({mid, n})) ++acrossPair;
            }
            auto const& got = names.name(seam->get(x, z));
            bool        ok  = cut ? got == "gloamingforest:stream" : got == mid;
            if (!ok && ++wrong <= 5) ctx.check(false, std::format("seam pixel ({}, {}): {} (input {}, stream expected: {})", x, z, got, mid, cut));
            seams += got == "gloamingforest:stream";
        }
    }
    ctx.check(wrong == 0, std::format("{} of {} seam pixels break upstream's stream rules", wrong, 192 * 192));
    ctx.check(seams > 0, std::format("{} stream pixels in the seam layer", seams));
    // the excluded cases occur in the sample (so the rules above were exercised)
    ctx.check(besideExcluded > 0, std::format("{} borders with an excluded neighbour biome (no stream)", besideExcluded));
    ctx.info(std::format("{} borders between an excluded pair (no stream)", acrossPair));
});

GF_REGISTER_SELFTEST("biomes.vertical_layers", [](Context& ctx) {
    auto p = make(ctx, 0);
    struct Row {
        char const* key;
        int         qy;
        char const* expected;
    } const rows[] = {
        {"gloamingforest:clearing", -1, "gloamingforest:clearing"},
        {"gloamingforest:clearing", -2, "gloamingforest:underground"},
        {"gloamingforest:clearing", -10, "gloamingforest:underground"},
        {"gloamingforest:clearing", 40, "gloamingforest:clearing"},
        {"gloamingforest:highlands", -3, "gloamingforest:highlands_underground"},
        {"gloamingforest:highlands", 10, "gloamingforest:highlands"},
        {"gloamingforest:lake", -4, "gloamingforest:lake"},
        {"gloamingforest:lake", -5, "gloamingforest:underground"},
        {"gloamingforest:final_plateau", -8, "gloamingforest:final_plateau"},
        {"gloamingforest:thornlands", -8, "gloamingforest:thornlands"},
    };
    for (auto const& r : rows) {
        auto const* col = p->terrainColumn(r.key);
        ctx.require(col != nullptr, std::string(r.key) + " has a terrain column");
        ctx.check(col->biomeAt(r.qy) == r.expected, std::format("{} at quart y {}: {} (expected {})", r.key, r.qy, col->biomeAt(r.qy), r.expected));
    }
});

GF_REGISTER_SELFTEST("biomes.tags", [](Context& ctx) {
    using tags::Registry;
    struct Row {
        char const*                     tag;
        std::vector<char const*>        members;
    } const rows[] = {
        {"gloamingforest:valid_aurora_palace_biomes", {"glacier"}},
        {"gloamingforest:valid_camp_biomes", {"oak_savannah", "clearing", "mushroom_forest", "forest", "firefly_forest"}},
        {"gloamingforest:valid_dark_tower_biomes", {"dark_forest_center"}},
        {"gloamingforest:valid_final_castle_biomes", {"final_plateau"}},
        {"gloamingforest:valid_giant_house_biomes", {"highlands"}},
        {"gloamingforest:valid_hedge_maze_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"gloamingforest:valid_hollow_hill_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"gloamingforest:valid_hollow_tree_biomes", {"dense_forest", "fire_swamp", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "enchanted_forest"}},
        {"gloamingforest:valid_hydra_lair_biomes", {"fire_swamp"}},
        {"gloamingforest:valid_knight_stronghold_biomes", {"dark_forest"}},
        {"gloamingforest:valid_labyrinth_biomes", {"swamp"}},
        {"gloamingforest:valid_lich_tower_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"gloamingforest:valid_mushroom_tower_biomes", {"dense_mushroom_forest"}},
        {"gloamingforest:valid_naga_courtyard_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"gloamingforest:valid_quest_grove_biomes", {"enchanted_forest"}},
        {"gloamingforest:valid_troll_cave_biomes", {"highlands"}},
        {"gloamingforest:valid_yeti_cave_biomes", {"snowy_forest"}},
    };
    for (auto const& r : rows) {
        auto members = tags::members(Registry::Biome, r.tag);
        ctx.check(members.size() == r.members.size(), std::format("#{} has {} members ({} expected)", r.tag, members.size(), r.members.size()));
        for (char const* m : r.members) {
            std::string id = std::string("gloamingforest:") + m;
            ctx.check(tags::has(Registry::Biome, r.tag, id), std::format("{} is in #{}", id, r.tag));
        }
    }
    int in = 0;
    for (auto const& id : biomeIds()) in += tags::has(Registry::Biome, "gloamingforest:in_gloaming_forest", id);
    // every upstream biome but highlands_underground (upstream's tag lists 22 entries, dense mushroom forest twice)
    ctx.check(in == 21, std::format("{} of the 22 biomes are #gloamingforest:in_gloaming_forest (21 expected)", in));
});

GF_REGISTER_SELFTEST("biomes.engine_biomes", [](Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    auto const& reg     = level->getBiomeRegistry();
    int         missing = 0;
    for (auto const& id : biomeIds()) {
        if (!reg.lookupByName(id)) {
            ++missing;
            ctx.check(false, id + " is registered (behavior pack biome)");
        }
    }
    ctx.check(missing == 0, std::format("{} of 22 biomes registered", 22 - missing));
    // upstream's water colours on the server's biomes (the biome definitions sent to clients carry them)
    int wrong = 0;
    for (auto const& id : biomeIds()) {
        auto const* b = reg.lookupByName(id);
        if (!b) continue;
        unsigned got  = static_cast<unsigned>(static_cast<int>(b->mMapWaterColor)) & 0xFFFFFF;
        unsigned want = static_cast<unsigned>(*core::biomes::colors::water(id));
        if (got != want && ++wrong <= 3) ctx.check(false, std::format("{} water #{:06X} (upstream #{:06X})", id, got, want));
    }
    ctx.check(wrong == 0, "upstream's water colours are set on the 22 biomes");
    if (auto const* swamp = reg.lookupByName("minecraft:swampland"))
        ctx.info(std::format("vanilla swampland map water colour #{:08X}", static_cast<unsigned>(static_cast<int>(swamp->mMapWaterColor))));
});

GF_REGISTER_SELFTEST("biomes.spawn_tables", [](Context& ctx) {
    namespace fs = std::filesystem;
    auto dir     = Mod::getInstance().getSelf().getModDir() / "data" / "gloamingforest" / "worldgen" / "biome";
    for (auto const& id : biomeIds()) {
        auto           file = dir / (id.substr(15) + ".json");
        std::ifstream  in(file);
        ctx.require(static_cast<bool>(in), "upstream's biome JSON " + file.generic_string());
        auto           j    = core::biomes::Json::parse(in);
        auto           mine = spawnsOf(id);
        // spawners
        std::size_t total = 0;
        for (auto const& [category, list] : j.at("spawners").items()) {
            auto const& got = mine.table.byCategory[category];
            total          += list.size();
            ctx.check(got.size() == list.size(), std::format("{} {}: {} spawners ({} in upstream)", id, category, got.size(), list.size()));
            for (std::size_t i = 0; i < std::min(got.size(), list.size()); ++i) {
                auto const& e = list[i];
                std::string type = e.at("type");
                ctx.check(
                    got[i].entityId == type && got[i].weight == e.at("weight").get<int>() && got[i].minCount == e.at("minCount").get<int>()
                        && got[i].maxCount == e.at("maxCount").get<int>(),
                    std::format("{} {} #{}: {}", id, category, i, type)
                );
            }
        }
        std::size_t mineTotal = 0;
        for (auto const& [c, l] : mine.table.byCategory) mineTotal += l.size();
        ctx.check(mineTotal == total, std::format("{}: {} spawners ({} in upstream)", id, mineTotal, total));
        float prob = j.value("creature_spawn_probability", 0.1f);
        ctx.check(std::abs(mine.table.creatureProbability - prob) < 1e-6, std::format("{}: creature probability {}", id, prob));
        auto const& costs = j.at("spawn_costs");
        ctx.check(costs.size() == mine.costs.size(), std::format("{}: {} spawn costs ({} in upstream)", id, mine.costs.size(), costs.size()));
        for (auto const& c : mine.costs) {
            bool ok = costs.contains(c.entityId) && std::abs(costs[c.entityId].at("charge").get<double>() - c.charge) < 1e-9
                   && std::abs(costs[c.entityId].at("energy_budget").get<double>() - c.energyBudget) < 1e-9;
            ctx.check(ok, std::format("{}: spawn cost of {}", id, c.entityId));
        }
    }
});

GF_REGISTER_SELFTEST("biomes.spawn_costs", [](Context& ctx) {
    // the swamp: zombies charge 0.7 with an energy budget of 0.15 (BiomeHelper#swampSpawning)
    auto const& swamp = spawnsOf("gloamingforest:swamp");
    auto        cost  = std::find_if(swamp.costs.begin(), swamp.costs.end(), [](auto const& c) { return c.entityId == "minecraft:zombie"; });
    ctx.require(cost != swamp.costs.end(), "the swamp has a spawn cost for zombies");
    auto has = [](std::vector<spawn::SpawnEntry> const& l, char const* id) {
        return std::any_of(l.begin(), l.end(), [&](auto const& e) { return e.entityId == id; });
    };
    core::biomes::PotentialCalculator none;
    auto                              all = costFiltered("gloamingforest:swamp", "monster", none, BlockPos{0, 64, 0});
    ctx.check(all.size() == swamp.table.byCategory.at("monster").size(), "no mob around: the whole monster list");
    // one zombie: charge 0.7 * 0.7 / d <= 0.15 from d = 3.27 on
    core::biomes::PotentialCalculator one;
    one.addCharge(0, 64, 0, cost->charge);
    auto near = costFiltered("gloamingforest:swamp", "monster", one, BlockPos{3, 64, 0});
    auto far  = costFiltered("gloamingforest:swamp", "monster", one, BlockPos{4, 64, 0});
    ctx.check(!has(near, "minecraft:zombie"), "no zombie 3 blocks from a zombie in the swamp (over its energy budget)");
    ctx.check(has(far, "minecraft:zombie"), "a zombie 4 blocks from a zombie in the swamp");
    // every other type is left out exactly when its own charge exceeds its budget there (the
    // potential does not depend on the types around)
    for (auto const& e : all) {
        auto   c        = std::find_if(swamp.costs.begin(), swamp.costs.end(), [&](auto const& k) { return k.entityId == e.entityId; });
        bool   expected = c == swamp.costs.end() || c->charge * (cost->charge / 3.0) <= c->energyBudget;
        ctx.check(has(near, e.entityId.c_str()) == expected, std::format("{} 3 blocks from a zombie: {}", e.entityId, expected ? "kept" : "left out"));
    }
    // a biome without costs keeps its list whatever the potential
    auto forest = costFiltered("gloamingforest:forest", "monster", one, BlockPos{0, 64, 0});
    ctx.check(forest.size() == spawnsOf("gloamingforest:forest").table.byCategory["monster"].size(), "the forest has no spawn costs");
});

GF_REGISTER_SELFTEST("biomes.terrain_blend", [](Context& ctx) {
    auto p = provider();
    ctx.require(p != nullptr, "the world's biome provider");
    for (int i = 0; i < 32; ++i) {
        int  x = i * 311 - 4000, z = 2500 - i * 173;
        auto e = gf::biomes::ext::sampleTerrain(x, z);
        auto d = p->sampleTerrain(x, z);
        ctx.check(e && e->depth == d.depth && e->scale == d.scale, std::format("sampleTerrain({}, {})", x, z));
        ctx.check(gf::biomes::ext::keyBiome(x >> 2, z >> 2) == p->keyBiome(x >> 2, z >> 2), "keyBiome");
    }
});

GF_REGISTER_SELFTEST("biomes.generated", [](Context& ctx) -> ll::coro::CoroTask<> {
    ctx.require(co_await ctx.inGloaming(), "in the Gloaming Forest");
    auto origin = ctx.testPos(100);
    // a chunk generated now, far from the test rows (U1's own column of the grid); chunk aligned, so
    // every sample below lies in the one chunk ensureLoaded loads (a sample in an unloaded
    // neighbour reads the default biome)
    core::GlobalPos far{origin.dim, (origin.x + 4096 + 100) & ~15, 0, (origin.z + 4096) & ~15};
    ctx.require(co_await ctx.ensureLoaded(far), "a far Gloaming Forest chunk loads");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the Gloaming Forest block source");
    ctx.require(region->hasChunksAt(BlockPos{far.x + 8, 0, far.z + 8}, 7, false), "the sampled chunk is loaded");
    int checked = 0, same = 0;
    std::set<std::string> seen;
    for (int dx = 0; dx < 16; dx += 3) {
        for (int dz = 0; dz < 16; dz += 3) {
            for (int y : {-24, 0, 40}) {
                BlockPos    pos{far.x + dx, y, far.z + dz};
                std::string stored = idOf(region->getBiome(pos));
                std::string source = worldgen::biomeAt(pos);
                ++checked;
                same += stored == source;
                seen.insert(stored);
            }
        }
    }
    std::string list;
    for (auto const& s : seen) list += (list.empty() ? "" : ", ") + s;
    ctx.info("stored biomes: " + list);
    ctx.check(same == checked, std::format("the chunk stores the biome source's zoomed biomes ({} of {})", same, checked));
    for (auto const& s : seen) ctx.check(s.starts_with("gloamingforest:"), s + " is a Gloaming Forest biome");
});

// Not a check: finds a place inside each surface biome near the origin for the world's seed (a
// 7x7-quart square of the biome; 3x3 for the narrow streams) and logs it for the client scenario's
// screenshots ("survey <biome> at <x> <z>").
GF_REGISTER_SELFTEST("biomes.survey", [](Context& ctx) {
    auto p = provider();
    ctx.require(p != nullptr, "the world's biome provider");
    std::set<std::string> found;
    std::set<std::string> wanted;
    for (auto const& id : biomeIds())
        if (id != "gloamingforest:underground" && id != "gloamingforest:highlands_underground") wanted.insert(id);
    auto uniform = [&](int qx, int qz, std::string const& b, int r) {
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
                if (p->mainBiome(qx + dx, qz + dz) != b) return false;
        return true;
    };
    for (int ring = 0; ring <= 1024 && found.size() < wanted.size(); ring += 4) {
        for (int i = -ring; i <= ring && found.size() < wanted.size(); i += 4) {
            for (auto [qx, qz] : {std::pair{i, -ring}, {i, ring}, {-ring, i}, {ring, i}}) {
                auto const& b = p->mainBiome(qx, qz);
                if (!wanted.contains(b) || found.contains(b)) continue;
                if (!uniform(qx, qz, b, b == "gloamingforest:stream" ? 1 : 3)) continue;
                found.insert(b);
                ctx.info(std::format("survey {} at {} {}", b, qx * 4 + 2, qz * 4 + 2));
            }
        }
    }
    for (auto const& b : wanted)
        if (!found.contains(b)) ctx.info("survey " + b + " not found within 4096 blocks");
});

// The vanilla mobs of upstream's tables spawn naturally in the Gloaming Forest (F5's spawn::spawnMob with
// SpawnReason::Natural, what its natural spawner calls), the creature and the monster category; the
// same spawns without U1's pass through Bedrock's own spawn rules are logged for comparison.
GF_REGISTER_SELFTEST("biomes.vanilla_spawns", [](Context& ctx) -> ll::coro::CoroTask<> {
    // monsters spawn only off Peaceful (Java's spawnEnemies, Bedrock's spawner alike), and the
    // e2e worlds are Peaceful: Normal for the test, the world's difficulty afterwards
    struct DifficultyScope {
        SharedTypes::Legacy::Difficulty previous = SharedTypes::Legacy::Difficulty::Peaceful;
        DifficultyScope() {
            if (auto level = ll::service::getLevel()) {
                previous = level->getDifficulty();
                level->setDifficulty(SharedTypes::Legacy::Difficulty::Normal);
            }
        }
        ~DifficultyScope() {
            if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
        }
    } normal;
    ctx.require(co_await ctx.inGloaming(), "in the Gloaming Forest");
    auto  box    = co_await ctx.flatArea(150, 5);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the Gloaming Forest block source");
    Vec3 at{static_cast<float>(box.minX) + 2.5f, static_cast<float>(box.minY), static_cast<float>(box.minZ) + 2.5f};
    auto tryOnce = [&](char const* id) {
        Actor* a  = spawn::spawnMob(*region, id, at, spawn::SpawnReason::Natural);
        bool   ok = a != nullptr;
        if (a) a->remove();
        return ok;
    };
    for (char const* id : {"minecraft:chicken", "minecraft:zombie", "minecraft:spider", "minecraft:bat"}) {
        setVanillaSpawnsForTest(false);
        bool without = tryOnce(id);
        setVanillaSpawnsForTest(true);
        bool with = tryOnce(id);
        ctx.info(std::format("{}: natural spawn {} with Bedrock's own spawn rules, {} with U1's pass", id, without ? "spawns" : "refused", with ? "spawns" : "refused"));
        ctx.check(with, std::string(id) + " spawns naturally in the Gloaming Forest");
    }
    co_await ctx.ticks(1);
});

// Water colour controls for the client scenario (not checks; no-ops unless a player is tagged
// gf_biomes_water_controls): 3 x 3 chunks 3000 blocks east of the tagged player are painted with
// another dimension's biome, chunks the client has never received, and kept loaded by a ticking
// area until biomes.water_controls_restore. In the Gloaming Forest the control is the vanilla
// minecraft:swampland (its client biome file's green water), in any other dimension upstream's fire swamp
// (its client biome file's red water). The scenario builds a pool there and photographs it: which
// water a biome gets in which dimension tells the dimension's part from the biome files' part.
namespace {
struct PaintedControl {
    int                                                 dim = 0;
    std::vector<std::tuple<int, int, Biome const*>>     columns; ///< x, z, original biome
    bool                                                tickingArea = false;
};
PaintedControl& paintedControl() {
    static PaintedControl p;
    return p;
}
Player* taggedPlayer(char const* tag) {
    Player* found = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer([&](Player& p) {
            if (p.hasTag(tag)) found = &p;
            return found == nullptr;
        });
    return found;
}
ChunkBlockPos columnPos(int x, int z, int minY) {
    return ChunkBlockPos{static_cast<uchar>(x & 15), ChunkLocalHeight{static_cast<short>(0)}, static_cast<uchar>(z & 15)};
}
} // namespace

GF_REGISTER_SELFTEST("biomes.water_controls", [](Context& ctx) -> ll::coro::CoroTask<> {
    Player* shown = taggedPlayer("gf_biomes_water_controls");
    if (!shown) {
        ctx.info("no player tagged gf_biomes_water_controls: nothing painted");
        co_return;
    }
    int         dim  = static_cast<int>(shown->getDimensionId());
    auto        feet = shown->getFeetPos();
    int         x = static_cast<int>(std::floor(feet.x)) + 3000, y = static_cast<int>(std::floor(feet.y)), z = static_cast<int>(std::floor(feet.z));
    bool        gloaming = dim != 0;
    std::string biomeId  = gloaming ? "minecraft:swampland" : "gloamingforest:fire_swamp";
    ctx.require(co_await ctx.ensureLoaded({dim, x, y, z}), "the chunks 3000 blocks east load on the server");
    auto* region = gf::game::regionOf(dim);
    ctx.require(region != nullptr, "the tagged player's region");
    Biome const* biome = region->getDimension().getBiomeRegistry().lookupByName(biomeId);
    ctx.require(biome != nullptr, biomeId + " is registered");
    auto& out = paintedControl();
    out       = {dim, {}, false};
    int n     = 0;
    for (int cx = (x >> 4) - 1; cx <= (x >> 4) + 1; ++cx)
        for (int cz = (z >> 4) - 1; cz <= (z >> 4) + 1; ++cz) {
            auto* chunk = region->getChunk(cx, cz);
            if (!chunk) continue;
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz) {
                    int bx = cx * 16 + lx, bz = cz * 16 + lz;
                    out.columns.emplace_back(bx, bz, &region->getBiome(BlockPos{bx, y, bz}));
                    chunk->_setBiome(*biome, columnPos(bx, bz, region->getMinHeight()), true);
                    ++n;
                }
        }
    auto r = selftest::runServerCommand(std::format("tickingarea add circle {} {} {} 2 gf_biomes_water_control", x, y, z), dim);
    out.tickingArea = static_cast<bool>(r);
    std::string now = idOf(region->getBiome(BlockPos{x, y, z}));
    ctx.check(now == biomeId, std::format("the server holds {} 3000 blocks east ({})", biomeId, now));
    gf::log().info("biomes water control: painted {} columns with {} at {} {} in dimension {}", n, biomeId, x, z, dim);
});

GF_REGISTER_SELFTEST("biomes.water_controls_restore", [](Context& ctx) {
    auto& p = paintedControl();
    if (p.tickingArea) {
        selftest::runServerCommand("tickingarea remove gf_biomes_water_control", p.dim);
        p.tickingArea = false;
    }
    if (p.columns.empty()) {
        ctx.info("nothing painted");
        return;
    }
    auto* region = gf::game::regionOf(p.dim);
    ctx.require(region != nullptr, "the painted region");
    int restored = 0;
    for (auto const& [x, z, biome] : p.columns) {
        auto* chunk = region->getChunk(x >> 4, z >> 4);
        if (!chunk || !biome) continue;
        chunk->_setBiome(*biome, columnPos(x, z, region->getMinHeight()), true);
        ++restored;
    }
    gf::log().info("biomes water control: restored {} of {} columns", restored, p.columns.size());
    p.columns.clear();
});

} // namespace gf::feature::biomes

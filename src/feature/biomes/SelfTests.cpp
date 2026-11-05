// U1 `biomes` selftests (dx 100-199, rule R12).
//
//   biomes.layer_golden        the biome source reproduces F13's oracle samples (3 seeds, main and
//                              wide grids, noise biomes) and is the one installed in worldgen
//   biomes.key_biome_quadrants every 8x8 key-layer region holds the four key biomes
//   biomes.companions          key biomes are ringed by their companions
//   biomes.thornlands_border   the final plateau only touches thornlands
//   biomes.streams             streams divide differing biomes, never next to the excluded ones
//   biomes.vertical_layers     underground below the columns (highlands: highlands_underground)
//   biomes.tags                TF's biome tags (17 valid_* + in_twilight_forest)
//   biomes.engine_biomes       the 22 behavior pack biomes are registered with the game, with TF's
//                              water colours
//   biomes.spawn_tables        BiomeHelper's tables equal TF's biome JSON (spawners, costs, probability)
//   biomes.spawn_costs         the spawn costs leave a type out where its charge exceeds its budget
//   biomes.generated           a freshly generated Twilight Forest chunk stores the source's biomes
//   biomes.terrain_blend       sampleTerrain through the extension equals the provider's

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/registry/BiomeRegistry.h"

#include "core/biomes/BiomeLayers.h"
#include "core/biomes/Colors.h"
#include "feature/biomes/Biomes.h"
#include "feature/ext/biomes/Biomes.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
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
    ctx.check(off == 0, std::format("worldgen::mainBiomeAt follows the TF biome source ({} of 64 columns differ)", off));
    ctx.info(std::format("{} oracle samples, world seed {}", std::size(kGoldenMain) + std::size(kGoldenNoise), seed));
});

GF_REGISTER_SELFTEST("biomes.key_biome_quadrants", [](Context& ctx) {
    auto p = make(ctx, 0);
    // the key layer runs before 6 zooms (64 quarts per pixel): an 8x8 key region spans 512 quarts;
    // sample 4 regions, each must hold all four key biomes
    char const* keys[] = {"twilightforest:glacier", "twilightforest:fire_swamp", "twilightforest:dark_forest_center", "twilightforest:final_plateau"};
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
        {"twilightforest:fire_swamp", "twilightforest:swamp"},
        {"twilightforest:glacier", "twilightforest:snowy_forest"},
        {"twilightforest:dark_forest_center", "twilightforest:dark_forest"},
        {"twilightforest:final_plateau", "twilightforest:highlands"},
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
            if (p->mainBiome(qx, qz) != "twilightforest:final_plateau") continue;
            ++plateau;
            for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                auto const& n = p->mainBiome(qx + dx, qz + dz);
                if (n == "twilightforest:final_plateau") continue;
                ++touching;
                wrong += n != "twilightforest:thornlands";
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
            if (p->mainBiome(qx, qz) != "twilightforest:stream") continue;
            ++streams;
        }
    }
    ctx.check(streams > 50, std::format("streams run between the biomes ({} sampled cells)", streams));
    // the stream's terrain column is the river's: low and flat
    auto const* col = p->terrainColumn("twilightforest:stream");
    ctx.require(col != nullptr, "the stream has a terrain column");
    ctx.check(col->depth == -0.1 && col->scale == 0.001 && col->weight == 1.35, "the stream's depth / scale / weight are TF's");

    // The seam rules (biomes_along_streams' seam layer over random_forest_biomes, TF's
    // SeamLayer#apply): a pixel becomes the stream exactly when one of its four neighbours differs
    // from it and neither of the two is an excluded neighbour biome nor the two an excluded pair;
    // checked against the stack's own layers with the rules read from TF's JSON.
    auto data = sourceData();
    auto it   = data->stacks.entries.find("twilightforest:biomes_along_streams");
    ctx.require(it != data->stacks.entries.end(), "the biomes_along_streams stack");
    auto const& seamJson = it->second.at("filtered_layer").at("parent");
    ctx.require(seamJson.value("layer_type", "") == "twilightforest:seam", "the stream input is a seam layer");
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
            bool        ok  = cut ? got == "twilightforest:stream" : got == mid;
            if (!ok && ++wrong <= 5) ctx.check(false, std::format("seam pixel ({}, {}): {} (input {}, stream expected: {})", x, z, got, mid, cut));
            seams += got == "twilightforest:stream";
        }
    }
    ctx.check(wrong == 0, std::format("{} of {} seam pixels break TF's stream rules", wrong, 192 * 192));
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
        {"twilightforest:clearing", -1, "twilightforest:clearing"},
        {"twilightforest:clearing", -2, "twilightforest:underground"},
        {"twilightforest:clearing", -10, "twilightforest:underground"},
        {"twilightforest:clearing", 40, "twilightforest:clearing"},
        {"twilightforest:highlands", -3, "twilightforest:highlands_underground"},
        {"twilightforest:highlands", 10, "twilightforest:highlands"},
        {"twilightforest:lake", -4, "twilightforest:lake"},
        {"twilightforest:lake", -5, "twilightforest:underground"},
        {"twilightforest:final_plateau", -8, "twilightforest:final_plateau"},
        {"twilightforest:thornlands", -8, "twilightforest:thornlands"},
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
        {"twilightforest:valid_aurora_palace_biomes", {"glacier"}},
        {"twilightforest:valid_camp_biomes", {"oak_savannah", "clearing", "mushroom_forest", "forest", "firefly_forest"}},
        {"twilightforest:valid_dark_tower_biomes", {"dark_forest_center"}},
        {"twilightforest:valid_final_castle_biomes", {"final_plateau"}},
        {"twilightforest:valid_giant_house_biomes", {"highlands"}},
        {"twilightforest:valid_hedge_maze_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"twilightforest:valid_hollow_hill_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"twilightforest:valid_hollow_tree_biomes", {"dense_forest", "fire_swamp", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "enchanted_forest"}},
        {"twilightforest:valid_hydra_lair_biomes", {"fire_swamp"}},
        {"twilightforest:valid_knight_stronghold_biomes", {"dark_forest"}},
        {"twilightforest:valid_labyrinth_biomes", {"swamp"}},
        {"twilightforest:valid_lich_tower_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"twilightforest:valid_mushroom_tower_biomes", {"dense_mushroom_forest"}},
        {"twilightforest:valid_naga_courtyard_biomes", {"clearing", "dense_forest", "dense_mushroom_forest", "firefly_forest", "forest", "mushroom_forest", "oak_savannah", "spooky_forest"}},
        {"twilightforest:valid_quest_grove_biomes", {"enchanted_forest"}},
        {"twilightforest:valid_troll_cave_biomes", {"highlands"}},
        {"twilightforest:valid_yeti_cave_biomes", {"snowy_forest"}},
    };
    for (auto const& r : rows) {
        auto members = tags::members(Registry::Biome, r.tag);
        ctx.check(members.size() == r.members.size(), std::format("#{} has {} members ({} expected)", r.tag, members.size(), r.members.size()));
        for (char const* m : r.members) {
            std::string id = std::string("twilightforest:") + m;
            ctx.check(tags::has(Registry::Biome, r.tag, id), std::format("{} is in #{}", id, r.tag));
        }
    }
    int in = 0;
    for (auto const& id : biomeIds()) in += tags::has(Registry::Biome, "twilightforest:in_twilight_forest", id);
    // every TF biome but highlands_underground (TF's tag lists 22 entries, dense mushroom forest twice)
    ctx.check(in == 21, std::format("{} of the 22 biomes are #twilightforest:in_twilight_forest (21 expected)", in));
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
    // TF's water colours on the server's biomes (the biome definitions sent to clients carry them)
    int wrong = 0;
    for (auto const& id : biomeIds()) {
        auto const* b = reg.lookupByName(id);
        if (!b) continue;
        unsigned got  = static_cast<unsigned>(static_cast<int>(b->mMapWaterColor)) & 0xFFFFFF;
        unsigned want = static_cast<unsigned>(*core::biomes::colors::water(id));
        if (got != want && ++wrong <= 3) ctx.check(false, std::format("{} water #{:06X} (TF #{:06X})", id, got, want));
    }
    ctx.check(wrong == 0, "TF's water colours are set on the 22 biomes");
    if (auto const* swamp = reg.lookupByName("minecraft:swampland"))
        ctx.info(std::format("vanilla swampland map water colour #{:08X}", static_cast<unsigned>(static_cast<int>(swamp->mMapWaterColor))));
});

GF_REGISTER_SELFTEST("biomes.spawn_tables", [](Context& ctx) {
    namespace fs = std::filesystem;
    auto dir     = Mod::getInstance().getSelf().getModDir() / "data" / "twilightforest" / "worldgen" / "biome";
    for (auto const& id : biomeIds()) {
        auto           file = dir / (id.substr(15) + ".json");
        std::ifstream  in(file);
        ctx.require(static_cast<bool>(in), "TF's biome JSON " + file.generic_string());
        auto           j    = core::biomes::Json::parse(in);
        auto           mine = spawnsOf(id);
        // spawners
        std::size_t total = 0;
        for (auto const& [category, list] : j.at("spawners").items()) {
            auto const& got = mine.table.byCategory[category];
            total          += list.size();
            ctx.check(got.size() == list.size(), std::format("{} {}: {} spawners ({} in TF)", id, category, got.size(), list.size()));
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
        ctx.check(mineTotal == total, std::format("{}: {} spawners ({} in TF)", id, mineTotal, total));
        float prob = j.value("creature_spawn_probability", 0.1f);
        ctx.check(std::abs(mine.table.creatureProbability - prob) < 1e-6, std::format("{}: creature probability {}", id, prob));
        auto const& costs = j.at("spawn_costs");
        ctx.check(costs.size() == mine.costs.size(), std::format("{}: {} spawn costs ({} in TF)", id, mine.costs.size(), costs.size()));
        for (auto const& c : mine.costs) {
            bool ok = costs.contains(c.entityId) && std::abs(costs[c.entityId].at("charge").get<double>() - c.charge) < 1e-9
                   && std::abs(costs[c.entityId].at("energy_budget").get<double>() - c.energyBudget) < 1e-9;
            ctx.check(ok, std::format("{}: spawn cost of {}", id, c.entityId));
        }
    }
});

GF_REGISTER_SELFTEST("biomes.spawn_costs", [](Context& ctx) {
    // the swamp: zombies charge 0.7 with an energy budget of 0.15 (BiomeHelper#swampSpawning)
    auto const& swamp = spawnsOf("twilightforest:swamp");
    auto        cost  = std::find_if(swamp.costs.begin(), swamp.costs.end(), [](auto const& c) { return c.entityId == "minecraft:zombie"; });
    ctx.require(cost != swamp.costs.end(), "the swamp has a spawn cost for zombies");
    auto has = [](std::vector<spawn::SpawnEntry> const& l, char const* id) {
        return std::any_of(l.begin(), l.end(), [&](auto const& e) { return e.entityId == id; });
    };
    core::biomes::PotentialCalculator none;
    auto                              all = costFiltered("twilightforest:swamp", "monster", none, BlockPos{0, 64, 0});
    ctx.check(all.size() == swamp.table.byCategory.at("monster").size(), "no mob around: the whole monster list");
    // one zombie: charge 0.7 * 0.7 / d <= 0.15 from d = 3.27 on
    core::biomes::PotentialCalculator one;
    one.addCharge(0, 64, 0, cost->charge);
    auto near = costFiltered("twilightforest:swamp", "monster", one, BlockPos{3, 64, 0});
    auto far  = costFiltered("twilightforest:swamp", "monster", one, BlockPos{4, 64, 0});
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
    auto forest = costFiltered("twilightforest:forest", "monster", one, BlockPos{0, 64, 0});
    ctx.check(forest.size() == spawnsOf("twilightforest:forest").table.byCategory["monster"].size(), "the forest has no spawn costs");
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
    ctx.require(co_await ctx.inTwilight(), "in the Twilight Forest");
    auto origin = ctx.testPos(100);
    // a chunk generated now, far from the test rows (U1's own column of the grid); chunk aligned, so
    // every sample below lies in the one chunk ensureLoaded loads (a sample in an unloaded
    // neighbour reads the default biome)
    core::GlobalPos far{origin.dim, (origin.x + 4096 + 100) & ~15, 0, (origin.z + 4096) & ~15};
    ctx.require(co_await ctx.ensureLoaded(far), "a far Twilight Forest chunk loads");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the Twilight Forest block source");
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
    for (auto const& s : seen) ctx.check(s.starts_with("twilightforest:"), s + " is a Twilight Forest biome");
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
        if (id != "twilightforest:underground" && id != "twilightforest:highlands_underground") wanted.insert(id);
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
                if (!uniform(qx, qz, b, b == "twilightforest:stream" ? 1 : 3)) continue;
                found.insert(b);
                ctx.info(std::format("survey {} at {} {}", b, qx * 4 + 2, qz * 4 + 2));
            }
        }
    }
    for (auto const& b : wanted)
        if (!found.contains(b)) ctx.info("survey " + b + " not found within 4096 blocks");
});

} // namespace gf::feature::biomes

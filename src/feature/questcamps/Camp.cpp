// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/type/CampStructure.java
//   src/main/java/twilightforest/world/components/structures/camp/CampPieces.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Camps (docs/spec/questcamps.md 3): a random point of the start chunk at the terrain height
// (adjustForTerrain 12, 3), a campfire start from pool camp/structure_start turned to a random
// direction, then the jigsaw expansion of TwilightJigsawPiece (tents, main paths, paths, decos,
// racks). F6 places the sites (avoid_landmark_grid, spacing 18, separation 14); the pieces with the
// pool entry's BEARD_BOX beard the terrain to their boxes.

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string_view>

#include "feature/ext/landmark/LandmarkExt.h"
#include "feature/ext/questcamps/W0Internals.h"
#include "feature/ext/structure/TemplateDefinitions.h"
#include "feature/landmark/Landmark.h"
#include "feature/questcamps/Questcamps.h"
#include "feature/worldgen/Worldgen.h"

namespace gf::feature::questcamps {

namespace st = gf::structure;

namespace {

constexpr char kStartPool[] = "gloamingforest:camp/structure_start";

/// "twilightforest:camp/<path>" as "gloamingforest:camp/<path>" (other strings unchanged).
std::string aliased(std::string const& id) {
    static constexpr std::string_view tf = "twilightforest:camp/";
    return id.starts_with(tf) ? "gloamingforest:camp/" + id.substr(tf.size()) : id;
}

/// The camp's templates, pools and connector names are in the gloamingforest namespace (assemble's
/// dangling-reference check knows no template pool ids): every TF template definition of the camp is
/// registered again under its alias, with the same pool entries (weights, height adjustment,
/// processors, marker handlers, terrain adaptation) and its pool aliases renamed alike.
void registerAliasDefinitions() {
    for (auto const& [templateId, def] : st::data::loadDir("twilight/template_definition")) {
        if (!templateId.starts_with("twilightforest:camp/") || !def.is_object()) continue;
        Json aliasedDef = Json::object();
        for (auto const& [pool, instance] : def.items()) {
            Json inst = instance;
            if (inst.is_object() && inst.contains("pool_aliases") && inst["pool_aliases"].is_object()) {
                Json renamed = Json::object();
                for (auto const& [from, to] : inst["pool_aliases"].items()) renamed[aliased(from)] = to.is_string() ? Json(aliased(to.get<std::string>())) : to;
                inst["pool_aliases"] = renamed;
            }
            aliasedDef[aliased(pool)] = inst;
        }
        st::pools::addDefinition(aliased(templateId), aliasedDef);
    }
}

/// CampPieces' pools (twilightforest:camp/structure_start, tent, rack_path, main_path, path, deco,
/// rack) are registered again as gloamingforest:camp/... (registerAliasDefinitions).

std::pair<int, int> startChunk(gf::landmark::Start const& start) {
    if (start.data.contains("chunk") && start.data["chunk"].is_array() && start.data["chunk"].size() == 2)
        return {start.data["chunk"][0].get<int>(), start.data["chunk"][1].get<int>()};
    return {start.center.x >> 4, start.center.z >> 4};
}

bool layout(gf::landmark::LayoutContext& ctx) {
    auto  context = gf::landmark::ext::contextRandom(ctx.start);
    auto& random  = *context;
    auto [cx, cz] = startChunk(ctx.start);
    int dim       = ctx.start.dimension;
    // CampStructure#findGenerationPoint
    int x = st::ext::lerpDiscrete(random.nextFloat(), cx << 4, (cx << 4) + 15);
    int z = st::ext::lerpDiscrete(random.nextFloat(), cz << 4, (cz << 4) + 15);
    int y = gf::landmark::ext::adjustForTerrain(dim, x, z, 12, 3).value_or(gf::landmark::ext::seaLevel(dim) + 1);
    ctx.start.center.x = x;
    ctx.start.center.y = y;
    ctx.start.center.z = z;
    st::ext::GenerationContext gen;
    gen.random              = &random;
    gen.seed                = gf::worldgen::seed();
    gen.firstOccupiedHeight = [dim](gf::worldgen::Heightmap type, int hx, int hz) {
        return gf::landmark::ext::firstOccupiedHeight(dim, hx, hz, type).value_or(gf::landmark::ext::seaLevel(dim));
    };
    auto pieces = st::ext::assemble(kStartPool, BlockPos{x, y, z}, -1, gen);
    for (auto& p : pieces) ctx.pieces.push_back(std::move(p));
    return !ctx.pieces.empty();
}

/// The BEARD_BOX boxes of a start (pool entries with terrain_adaptation beard_box), with their
/// ground level deltas, gathered once per chunk and thread.
struct BeardCache {
    std::int64_t                               key = 0;
    int                                        chunkX = 0, chunkZ = 0;
    std::vector<std::pair<int, std::vector<st::BlockBox>>> byDelta; ///< ground level delta -> boxes
};

double terraform(gf::landmark::Start const& start, int x, int y, int z) {
    thread_local BeardCache cache;
    std::int64_t key = start.seed ^ (static_cast<std::int64_t>(start.center.x) << 32) ^ static_cast<std::int64_t>(start.center.z) * 31 ^
                       static_cast<std::int64_t>(start.pieceBoxes.size()) << 20;
    int cx = x >> 4, cz = z >> 4;
    if (cache.key != key || cache.chunkX != cx || cache.chunkZ != cz) {
        cache.key    = key;
        cache.chunkX = cx;
        cache.chunkZ = cz;
        cache.byDelta.clear();
        st::BlockBox around{(cx << 4) - 24, -4096, (cz << 4) - 24, (cx << 4) + 39, 4096, (cz << 4) + 39};
        for (auto const& p : gf::landmark::ext::startPieces(start)) {
            if (!p.box.intersects(around)) continue;
            auto adaptation = p.tag.value("terrain_adaptation", std::string{});
            std::transform(adaptation.begin(), adaptation.end(), adaptation.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (adaptation != "beard_box") continue;
            int  delta = p.tag.value("ground_offset", 0);
            auto it    = std::find_if(cache.byDelta.begin(), cache.byDelta.end(), [&](auto const& e) { return e.first == delta; });
            if (it == cache.byDelta.end()) cache.byDelta.emplace_back(delta, std::vector<st::BlockBox>{p.box});
            else it->second.push_back(p.box);
        }
    }
    double d = 0;
    for (auto const& [delta, boxes] : cache.byDelta) d += gf::landmark::ext::boxDensity(boxes, delta, 0, "beard_box", x, y, z);
    return d;
}

} // namespace

void installCamp() {
    registerAliasDefinitions();
    gf::landmark::registerGenerator(kCamp, gf::landmark::Generator{.layout = layout, .terraform = terraform});
}

} // namespace gf::feature::questcamps

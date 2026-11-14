// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/util/StructureTemplateDefinition.java
//   src/main/java/gloamingforest/world/components/structures/util/StructureTemplateDefinitions.java
//   src/main/java/gloamingforest/world/components/structures/util/TemplatePoolInstance.java
//   src/main/java/gloamingforest/world/components/structures/GloamingJigsawPiece.java (defaultForTemplate)
//   src/main/java/gloamingforest/world/components/structures/type/CampStructure.java (jigsaw start)
//   src/main/java/gloamingforest/init/GloamingStructurePieceTypes.java (GloamingJigsawTemplate, GloamingUtilityPiece)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Template pools from upstream's gloaming/template_definition data (a template lists the pools it
// belongs to with a weight or a TemplatePoolInstance), and upstream's jigsaw growth over them.
#include <algorithm>
#include <climits>
#include <optional>
#include <mutex>
#include <shared_mutex>

#include "feature/structure/bases/BasesInternal.h"
#include "feature/structure/bases/Component.h"

namespace gf::structure {

namespace pools {

namespace {

std::shared_mutex                         gMutex;
std::map<std::string, Json>               gDefinitions; ///< template id -> definition (files + in-memory)
std::map<std::string, Json>               gExtra;       ///< in-memory definitions
std::map<std::string, std::vector<Entry>> gPools;

/// ResourceLocation#compareTo: path first, then namespace.
bool idLess(std::string const& a, std::string const& b) {
    auto split = [](std::string const& s) {
        auto c = s.find(':');
        return c == std::string::npos ? std::pair<std::string, std::string>{"minecraft", s} : std::pair{s.substr(0, c), s.substr(c + 1)};
    };
    auto [na, pa] = split(a);
    auto [nb, pb] = split(b);
    if (pa != pb) return pa < pb;
    return na < nb;
}

void rebuildLocked() {
    gPools.clear();
    std::map<std::string, Json> all = gDefinitions;
    for (auto const& [k, v] : gExtra) all[k] = v;
    std::map<std::string, std::vector<Entry>> raw;
    for (auto const& [templateId, def] : all) {
        if (!def.is_object()) continue;
        for (auto const& [poolId, instance] : def.items()) {
            Entry e;
            e.templateId = templateId;
            e.instance   = instance;
            e.weight     = instance.is_number_integer() ? instance.get<int>() : instance.value("weight", 1);
            raw[poolId].push_back(std::move(e));
        }
    }
    for (auto& [poolId, entries] : raw) {
        std::sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b) { return idLess(a.templateId, b.templateId); });
        gPools[poolId] = std::move(entries);
    }
}

} // namespace

std::vector<Entry> const* get(std::string const& poolId) {
    std::shared_lock lock(gMutex);
    auto             it = gPools.find(poolId);
    return it == gPools.end() ? nullptr : &it->second;
}

void addDefinition(std::string const& templateId, Json const& definition) {
    std::unique_lock lock(gMutex);
    gExtra[templateId] = definition;
    rebuildLocked();
}

void rebuild(std::map<std::string, Json> const& definitions) {
    std::unique_lock lock(gMutex);
    gDefinitions = definitions;
    rebuildLocked();
}

std::size_t definitionCount() {
    std::shared_lock lock(gMutex);
    return gDefinitions.size() + gExtra.size();
}

Entry const* random(std::vector<Entry> const& pool, RandomSource& rnd) {
    int total = 0;
    for (auto const& e : pool) total += e.weight;
    if (total <= 0) return nullptr;
    int i = rnd.nextInt(total);
    for (auto const& e : pool) {
        i -= e.weight;
        if (i < 0) return &e;
    }
    return nullptr;
}

} // namespace pools

namespace bases::detail {

namespace {

/// A GloamingJigsawPiece built from a pool entry (GloamingJigsawPiece.defaultForTemplate).
class PoolJigsawPiece final : public JigsawPiece {
public:
    PoolJigsawPiece(int depth, std::string const& templateId, JigsawPlaceContext const& context, Json const& instance, std::vector<std::string> chosen)
    : JigsawPiece("gloamingforest:GloamingJigsawTemplate", depth, templateId, context) {
        if (instance.is_object()) {
            terrainAdjustment = parseTerrainAdjustment(instance.value("terrain_adaptation", std::string("none")));
            if (instance.contains("processors")) {
                auto const& p = instance["processors"];
                if (p.is_string()) processors = p.get<std::string>();
                else {
                    // an inline processor list: registered under a synthetic id so the piece can save it
                    std::string id = "gf:inline/" + std::to_string(std::hash<std::string>{}(p.dump()));
                    processors::addList(id, p);
                    processors = id;
                }
            }
            rigidProjection = instance.value("projection", std::string("rigid")) != "terrain_matching";
            if (instance.contains("height_adjustment")) beardifierGroundDelta = instance["height_adjustment"].value("y_offset", 0);
            // ignore_world_waterlog: placeSettings.setLiquidSettings(LiquidSettings.IGNORE_WATERLOGGING)
            if (instance.value("ignore_world_waterlog", false)) placeSettings.processors.push_back(liquids::kIgnoreWaterlogging);
            if (instance.contains("marker_handlers")) {
                auto const& m = instance["marker_handlers"];
                if (m.is_string()) markerHandlers = m.get<std::string>();
                else {
                    std::string id = "gf:inline/" + std::to_string(std::hash<std::string>{}(m.dump()));
                    markers::addList(id, m);
                    markerHandlers = id;
                }
            }
            Json aliases = instance.value("pool_aliases", Json::object());
            for (auto const& [k, v] : aliases.items()) poolAliases[k] = v.get<std::string>();
        }
        if (processors) placeSettings.processors.push_back(*processors);
        for (auto& c : chosen) placeSettings.processors.push_back(std::move(c));
    }
};

} // namespace

std::unique_ptr<JigsawPiece> defaultForTemplate(int genDepth, std::string const& templateId, JigsawPlaceContext const& context, Json const& instance,
                                                RandomSource& random) {
    // TemplatePoolInstance#chooseRandomProcessors: one weighted pick per list
    std::vector<std::string> chosen;
    if (instance.is_object()) {
        for (auto const& list : instance.value("randomized_processors", Json::array())) {
            int total = 0;
            for (auto const& e : list) total += e.value("weight", 1);
            if (total <= 0) continue;
            int i = random.nextInt(total);
            for (auto const& e : list) {
                i -= e.value("weight", 1);
                if (i < 0) {
                    chosen.push_back(e["data"].dump());
                    break;
                }
            }
        }
    }
    return std::make_unique<PoolJigsawPiece>(genDepth, templateId, context, instance, std::move(chosen));
}

ext::GenerationContext const*& generationContext() {
    thread_local ext::GenerationContext const* context = nullptr;
    return context;
}

namespace {

worldgen::Heightmap parseHeightmap(std::string const& name) {
    if (name == "WORLD_SURFACE") return worldgen::Heightmap::WorldSurface;
    if (name == "OCEAN_FLOOR_WG") return worldgen::Heightmap::OceanFloorWg;
    if (name == "OCEAN_FLOOR") return worldgen::Heightmap::OceanFloor;
    if (name == "MOTION_BLOCKING") return worldgen::Heightmap::MotionBlocking;
    if (name == "MOTION_BLOCKING_NO_LEAVES") return worldgen::Heightmap::MotionBlockingNoLeaves;
    return worldgen::Heightmap::WorldSurfaceWg;
}

} // namespace

JigsawPlaceContext adjustContextForTerrain(JigsawPlaceContext context, Json const& h, ext::GenerationContext const& gen, bool parentProjectsTerrain) {
    if (!h.is_object()) return context;
    auto               type  = parseHeightmap(h.value("heightmap", std::string("WORLD_SURFACE_WG")));
    int                delta = h.value("y_offset", 0);
    std::optional<int> limit;
    if (h.contains("ground_junction_diff_clamp") && h["ground_junction_diff_clamp"].is_number_integer()) limit = h["ground_junction_diff_clamp"].get<int>();
    int templateY = context.templatePos.y;
    // clampYLevelPos / clampYLevelBox
    auto clamp = [&](auto terrain) {
        if (!limit) return delta + terrain();
        if (*limit == 0) return templateY;
        return std::clamp(delta + terrain(), templateY - *limit, templateY + *limit);
    };
    int y;
    if (parentProjectsTerrain) {
        BlockPos at{context.templatePos.x + context.seedJigsaw.pos.x, context.templatePos.y + context.seedJigsaw.pos.y,
                    context.templatePos.z + context.seedJigsaw.pos.z};
        y = clamp([&] { return gen.firstOccupiedHeight ? gen.firstOccupiedHeight(type, at.x, at.z) : templateY; });
    } else {
        BlockBox box = context.makeBoundingBox();
        y = clamp([&] { return ext::adjustForTerrain(gen, box.minX, box.minZ, box.maxX, box.maxZ, 2, type); });
    }
    context.templatePos.y = y;
    return context;
}

std::unique_ptr<JigsawPiece> initializeTemplateFromPool(std::string const& pool, BlockPos const& parentJunctionPos, JigsawRecord const& parentOrientation,
                                                        std::string const& selectName, RandomSource& random, int genDepth,
                                                        ext::GenerationContext const* gen, bool parentProjectsTerrain) {
    auto const* entries = pools::get(pool);
    if (!entries) return nullptr;
    auto const* entry = pools::random(*entries, random);
    if (!entry) return nullptr;
    auto context = JigsawPlaceContext::pickPlaceableJunction(parentJunctionPos, BlockPos{0, 0, 0}, parentOrientation, entry->templateId, selectName, random);
    if (!context) return nullptr;
    // TemplatePoolInstance#adjustContextForTerrain: with a generation context, the entry's
    // height_adjustment (absent: the junction height stays)
    if (gen && entry->instance.is_object() && entry->instance.contains("height_adjustment"))
        context = adjustContextForTerrain(*context, entry->instance["height_adjustment"], *gen, parentProjectsTerrain);
    return defaultForTemplate(genDepth, entry->templateId, *context, entry->instance, random);
}

bool projectsTerrain(JigsawPiece const& piece) {
    // the protected Projection of a piece, read through a member pointer of a derived class
    struct Peek : JigsawPiece {
        static bool rigid(JigsawPiece const& p) { return p.*(&Peek::rigidProjection); }
    };
    return !Peek::rigid(piece);
}

void registerPieceTypes() {
    registerPieceType("gloamingforest:GloamingUtilityPiece", [](Json const& saved) -> std::unique_ptr<Piece> {
        auto p = std::make_unique<UtilityPiece>(0, BlockBox{});
        p->load(saved);
        return p;
    });
    registerPieceType("gloamingforest:GloamingJigsawTemplate", [](Json const& saved) -> std::unique_ptr<Piece> {
        auto p = std::make_unique<JigsawPiece>("gloamingforest:GloamingJigsawTemplate", 0, saved.value("Template", std::string{}), JigsawPlaceContext{});
        p->load(saved);
        return p;
    });
}

} // namespace bases::detail

namespace jigsaw {

std::vector<std::unique_ptr<Piece>> assemble(std::string const& pool, BlockPos const& origin, int depth, RandomSource& random) {
    std::vector<std::unique_ptr<Piece>> pieces;
    // CampStructure#findGenerationPoint: FrontAndTop(UP, Rotation.getRandom(random).rotate(SOUTH))
    bases::JigsawRecord oriented;
    oriented.frontVertical = true;
    oriented.frontUp       = true;
    oriented.top           = rotateDir(Orientation::South, rotationFromIndex(random.nextInt(4)));
    auto start             = bases::detail::initializeTemplateFromPool(pool, origin, oriented, pool, random, 0);
    if (!start) return pieces;
    int& maxDepth = bases::detail::maxJigsawDepth();
    int  previous = maxDepth;
    maxDepth      = depth < 0 ? INT_MAX : depth;
    auto* raw     = start.get();
    pieces.push_back(std::move(start));
    raw->addJigsaws(*raw, pieces, random);
    maxDepth = previous;
    return pieces;
}

} // namespace jigsaw
} // namespace gf::structure

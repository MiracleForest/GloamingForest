// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/GloamingStructureComponentOld.java
//   src/main/java/gloamingforest/world/components/structures/GloamingJigsawPiece.java
//   src/main/java/gloamingforest/world/components/structures/HedgeMazeComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/lichtower/TowerWingComponent.java
//   src/main/java/gloamingforest/world/components/structures/darktower/DarkTowerEntranceComponent.java
//   src/main/java/gloamingforest/world/components/structures/stronghold/KnightStrongholdComponent.java
//   src/main/java/gloamingforest/world/components/structures/trollcave/TrollCaveMainComponent.java
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleDungeonRoom31Component.java
//   src/main/java/gloamingforest/world/components/structures/courtyard/CourtyardTerrace.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/*.java
//   src/main/java/gloamingforest/init/GloamingStructurePieceTypes.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/terrain/Beard.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace gf::core::terrain::beard {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// How an upstream piece type answers the beardifier.
enum class Rule {
    Plain,        ///< a plain StructurePiece / TemplateStructurePiece: the structure's adaptation
    OldNone,      ///< GloamingStructureComponentOld's default: NONE
    BeardBox0,    ///< BEARD_BOX, ground delta 0 (HedgeMazeComponent)
    BeardBox1,    ///< BEARD_BOX, ground delta 1 (TowerWingComponent's delta: TowerMain, DarkTowerEntrance; lich tower base, trim, foyer, grave)
    BeardBox2,    ///< BEARD_BOX, ground delta 2 (LichPerimeterFence)
    BeardBox3,    ///< BEARD_BOX, ground delta 3 (CourtyardTerrace and its hedge)
    Bury,         ///< BURY, ground delta 0 (protected stronghold pieces, troll cave, final castle dungeon rooms)
    ModifierNone, ///< a PieceBeardifierModifier answering NONE (the other lich tower pieces)
    Jigsaw,       ///< GloamingJigsawPiece: "terrain_adaptation" / "ground_offset" of its tag
    WingBeard,    ///< LichTowerWingBeard: "gen_ground" ? BEARD_BOX : NONE, ground delta 4
    YardBox,      ///< LichYardBox: "dirt_mix" ? BEARD_BOX : NONE, ground delta 0
};

std::unordered_map<std::string, Rule> const& rules() {
    static std::unordered_map<std::string, Rule> const r = [] {
        std::unordered_map<std::string, Rule> m;
        auto add = [&](Rule rule, std::initializer_list<char const*> ids) {
            for (auto const* id : ids) m.emplace(id, rule);
        };
        // plain pieces (no PieceBeardifierModifier, no PoolElementStructurePiece)
        add(Rule::Plain, {"gloamingquest1", "gloamingfallentrunk", "gloamingutilitypiece", "gloaminghtlb", "gloaminghtmb", "gloaminghtsb", "gloaminghttr", "gloaminghtro", "gloaminghtld", "gloamingncmn", "gloamingnccp", "gloamingnccpp",
                          "gloamingnccr", "gloamingncln", "gloamingnct", "gloamingncis", "gloamingncpd", "gloamingncpa", "gloamingncwl", "gloamingncwp", "gloamingncwc", "gloamingncwa", "gloamingcloud"});
        // GloamingStructureComponentOld subclasses without an override: NONE
        add(Rule::OldNone,
            {"gloaminghill",     "gloaminghydra",    "gloamingyeti",     "gloamingmtmai",   "gloamingmtwin",    "gloamingmtbri",   "gloamingmtmb",    "gloamingmtroofmush", "gloamingltbea",   "gloamingltba",    "gloamingltbri",
             "gloamingltout",    "gloamingltroo",    "gloamingltras",    "gloamingltrf",    "gloamingltrgf",    "gloamingltrp",    "gloamingltrpo",   "gloamingltrs",       "gloamingltrsf",   "gloamingltrst",   "gloamingltrsto",
             "gloamingltwin",    "gloamingmmc",      "gloamingmmcif",    "gloamingmmcr",    "gloamingmmcs",     "gloamingmmde",    "gloamingmmdec",   "gloamingmmdef",      "gloamingmmdefl",  "gloamingmmdep",   "gloamingmmder",
             "gloamingmmdes",    "gloamingmmdet",    "gloamingmmdetrc",  "gloamingmmdetc",  "gloamingmmes",     "gloamingmmmound", "gloamingmmmr",    "gloamingmmr",        "gloamingmmrb",    "gloamingmmrc",    "gloamingmmre",
             "gloamingmmrf",     "gloamingmmrsc",    "gloamingmmrv",     "gloamingmmruins", "gloamingmmue",     "gloamingmmaze",   "gloamingdtbal",   "gloamingdtbea",      "gloamingdtbb",    "gloamingdtbt",    "gloamingdtbri",
             "gloamingdteb",     "gloamingdtmai",    "gloamingdtmb",     "gloamingdtroos",  "gloamingdtra",     "gloamingdtrc",    "gloamingdtrfp",   "gloamingdtrr",       "gloamingdtwin",   "gloamingitmai",   "gloamingitwin",
             "gloamingitroof",   "gloamingitbea",    "gloamingitboss",   "gloamingitent",   "gloamingitbri",    "gloamingitst",    "gloamingtcloud",  "gloamingclca",       "gloamingcltr",    "gloamingtcva",    "gloamingfcmain",
             "gloamingfcstto",   "gloamingfclato",   "gloamingfcmur",    "gloamingfctof48", "gloamingfcro48cr", "gloamingfcbogaz", "gloamingfcsito",  "gloamingfcdunst",    "gloamingfcdunbor", "gloamingfcro9cr", "gloamingfcro13cr",
             "gloamingfcro13con", "gloamingfcro13pk", "gloamingfcento",  "gloamingfcensito", "gloamingfcenboto", "gloamingfcenst", "gloamingfcbelto", "gloamingfcbri",      "gloamingfctof13", "gloamingfcbef21", "gloamingfcfth21",
             "gloamingfcdamt",   "gloamingfcwrt",
             // KnightStrongholdComponent#getTerrainAdjustment: isComponentProtected() ? BURY : NONE;
             // these six answer isComponentProtected() = false
             "gloamingsac", "gloamingsua", "gloamingsult", "gloamingsurt", "gloamingsuco", "gloamingsuti"});
        add(Rule::BeardBox0, {"gloaminghedge"});
        add(Rule::BeardBox1, {"gloamingltmai", "gloamingdtent", "gloaminglttfoy", "gloamingltctbase", "gloamingltcttrim", "gloamingltgrave"});
        add(Rule::BeardBox2, {"gloamingltfence"});
        add(Rule::BeardBox3, {"gloamingncte", "gloamingnche"});
        add(Rule::Bury, {"gloamingssh", "gloamingslt", "gloamingscr", "gloamingsrt", "gloamingsde", "gloamingsbalr", "gloamingstr", "gloamingsss", "gloamingstc", "gloamingsat", "gloamingsfo", "gloamingtrear", "gloamingsbr", "gloamingsenter", "gloamingsshield",
                         "gloamingtcmai", "gloamingtccon", "gloamingtcgard", "gloamingfcdunr31", "gloamingfcdunen", "gloamingfcdunex"});
        add(Rule::ModifierNone, {"gloamingltctseg", "gloamingltmobbridge", "gloamingltbridge", "gloaminglttroof", "gloaminglttroom", "gloaminglttdecor", "gloaminglttgallery", "gloaminglttfoyd", "gloaminglttboss",
                                 "gloaminglttbossroof", "gloamingltlight"});
        add(Rule::Jigsaw, {"gloamingjigsawtemplate"});
        add(Rule::WingBeard, {"gloaminglttbeard"});
        add(Rule::YardBox, {"gloamingltpath"});
        return m;
    }();
    return r;
}

bool tagBool(nlohmann::json const& tag, char const* key) {
    if (!tag.is_object()) return false;
    auto it = tag.find(key);
    if (it == tag.end()) return false;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number()) return it->get<double>() != 0.0; // NBT booleans are bytes
    return false;
}

} // namespace

double rigidDensity(Rigid const& r, int x, int y, int z) {
    BlockBox const& b  = r.box;
    int             dx = std::max(0, std::max(b.minX - x, x - b.maxX));
    int             dz = std::max(0, std::max(b.minZ - z, z - b.maxZ));
    int             groundY        = b.minY + r.groundLevelDelta;
    int             distAboveGround = y - groundY;
    int             dy             = 0;
    switch (r.adjustment) {
    case TerrainAdjustment::None:
        dy = 0;
        break;
    case TerrainAdjustment::Bury:
    case TerrainAdjustment::BeardThin:
        dy = distAboveGround;
        break;
    case TerrainAdjustment::BeardBox:
        dy = std::max(0, std::max(groundY - y, y - b.maxY));
        break;
    case TerrainAdjustment::Encapsulate:
        dy = std::max(0, std::max(b.minY - y, y - b.maxY));
        break;
    }
    switch (r.adjustment) {
    case TerrainAdjustment::None:
        return 0.0;
    case TerrainAdjustment::Bury:
        return core::landmark::buryContribution(static_cast<double>(dx), static_cast<double>(dy) / 2.0, static_cast<double>(dz));
    case TerrainAdjustment::BeardThin:
    case TerrainAdjustment::BeardBox:
        return core::landmark::beardContribution(dx, dy, dz, distAboveGround) * 0.8;
    case TerrainAdjustment::Encapsulate:
        return core::landmark::buryContribution(static_cast<double>(dx) / 2.0, static_cast<double>(dy) / 2.0, static_cast<double>(dz) / 2.0) * 0.8;
    }
    return 0.0;
}

double beardifierDensity(std::vector<Rigid> const& rigids, int x, int y, int z) {
    double d = 0.0;
    for (auto const& r : rigids) d += rigidDensity(r, x, y, z);
    return d;
}

bool isCloseToChunk(BlockBox const& b, int chunkX, int chunkZ, int distance) {
    int minX = chunkX * 16, minZ = chunkZ * 16;
    // BoundingBox#intersects(minX, minZ, maxX, maxZ)
    return b.maxX >= minX - distance && b.minX <= minX + 15 + distance && b.maxZ >= minZ - distance && b.minZ <= minZ + 15 + distance;
}

TerrainAdjustment adjustmentOf(std::string const& name) { return core::landmark::terrainAdjustmentOf(lower(name)); }

std::optional<PieceBeard> gloamingPieceBeard(std::string const& typeId, BlockBox const& box, nlohmann::json const& tag) {
    auto        colon = typeId.find(':');
    std::string ns    = colon == std::string::npos ? std::string("minecraft") : lower(typeId.substr(0, colon));
    if (ns != "gloamingforest") return std::nullopt;
    auto it = rules().find(lower(typeId.substr(colon + 1)));
    if (it == rules().end()) return std::nullopt;
    auto modifier = [&](TerrainAdjustment a, int delta) { return PieceBeard{true, a, box, delta}; };
    switch (it->second) {
    case Rule::Plain:
        return PieceBeard{false, TerrainAdjustment::None, box, 0};
    case Rule::OldNone:
    case Rule::ModifierNone:
        return modifier(TerrainAdjustment::None, 0);
    case Rule::BeardBox0:
        return modifier(TerrainAdjustment::BeardBox, 0);
    case Rule::BeardBox1:
        return modifier(TerrainAdjustment::BeardBox, 1);
    case Rule::BeardBox2:
        return modifier(TerrainAdjustment::BeardBox, 2);
    case Rule::BeardBox3:
        return modifier(TerrainAdjustment::BeardBox, 3);
    case Rule::Bury:
        return modifier(TerrainAdjustment::Bury, 0);
    case Rule::Jigsaw: {
        // TerrainAdjustment.valueOf(tag "terrain_adaptation") (absent: NONE), tag.getInt("ground_offset")
        auto adj   = TerrainAdjustment::None;
        int  delta = 0;
        if (tag.is_object()) {
            if (auto a = tag.find("terrain_adaptation"); a != tag.end() && a->is_string()) adj = adjustmentOf(a->get<std::string>());
            if (auto g = tag.find("ground_offset"); g != tag.end() && g->is_number()) delta = g->get<int>();
        }
        return modifier(adj, delta);
    }
    case Rule::WingBeard:
        return modifier(tagBool(tag, "gen_ground") ? TerrainAdjustment::BeardBox : TerrainAdjustment::None, 4);
    case Rule::YardBox:
        return modifier(tagBool(tag, "dirt_mix") ? TerrainAdjustment::BeardBox : TerrainAdjustment::None, 0);
    }
    return std::nullopt;
}

std::optional<Rigid> rigidOf(PieceBeard const& piece, BlockBox const& pieceBox, TerrainAdjustment structureAdaptation) {
    if (piece.modifier) {
        if (piece.adjustment == TerrainAdjustment::None) return std::nullopt;
        return Rigid{piece.box, piece.adjustment, piece.groundLevelDelta};
    }
    if (structureAdaptation == TerrainAdjustment::None) return std::nullopt;
    return Rigid{pieceBox, structureAdaptation, 0};
}

} // namespace gf::core::terrain::beard

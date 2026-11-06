// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFStructureComponentOld.java
//   src/main/java/twilightforest/world/components/structures/TwilightJigsawPiece.java
//   src/main/java/twilightforest/world/components/structures/HedgeMazeComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerMainComponent.java
//   src/main/java/twilightforest/world/components/structures/lichtower/TowerWingComponent.java
//   src/main/java/twilightforest/world/components/structures/darktower/DarkTowerEntranceComponent.java
//   src/main/java/twilightforest/world/components/structures/stronghold/KnightStrongholdComponent.java
//   src/main/java/twilightforest/world/components/structures/trollcave/TrollCaveMainComponent.java
//   src/main/java/twilightforest/world/components/structures/finalcastle/FinalCastleDungeonRoom31Component.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardTerrace.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/*.java
//   src/main/java/twilightforest/init/TFStructurePieceTypes.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
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

/// How a TF piece type answers the beardifier.
enum class Rule {
    Plain,        ///< a plain StructurePiece / TemplateStructurePiece: the structure's adaptation
    OldNone,      ///< TFStructureComponentOld's default: NONE
    BeardBox0,    ///< BEARD_BOX, ground delta 0 (HedgeMazeComponent)
    BeardBox1,    ///< BEARD_BOX, ground delta 1 (TowerWingComponent's delta: TowerMain, DarkTowerEntrance; lich tower base, trim, foyer, grave)
    BeardBox2,    ///< BEARD_BOX, ground delta 2 (LichPerimeterFence)
    BeardBox3,    ///< BEARD_BOX, ground delta 3 (CourtyardTerrace and its hedge)
    Bury,         ///< BURY, ground delta 0 (protected stronghold pieces, troll cave, final castle dungeon rooms)
    ModifierNone, ///< a PieceBeardifierModifier answering NONE (the other lich tower pieces)
    Jigsaw,       ///< TwilightJigsawPiece: "terrain_adaptation" / "ground_offset" of its tag
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
        add(Rule::Plain, {"tfquest1", "tffallentrunk", "tfutilitypiece", "tfhtlb", "tfhtmb", "tfhtsb", "tfhttr", "tfhtro", "tfhtld", "tfncmn", "tfnccp", "tfnccpp",
                          "tfnccr", "tfncln", "tfnct", "tfncis", "tfncpd", "tfncpa", "tfncwl", "tfncwp", "tfncwc", "tfncwa", "tfcloud"});
        // TFStructureComponentOld subclasses without an override: NONE
        add(Rule::OldNone,
            {"tfhill",     "tfhydra",    "tfyeti",     "tfmtmai",   "tfmtwin",    "tfmtbri",   "tfmtmb",    "tfmtroofmush", "tfltbea",   "tfltba",    "tfltbri",
             "tfltout",    "tfltroo",    "tfltras",    "tfltrf",    "tfltrgf",    "tfltrp",    "tfltrpo",   "tfltrs",       "tfltrsf",   "tfltrst",   "tfltrsto",
             "tfltwin",    "tfmmc",      "tfmmcif",    "tfmmcr",    "tfmmcs",     "tfmmde",    "tfmmdec",   "tfmmdef",      "tfmmdefl",  "tfmmdep",   "tfmmder",
             "tfmmdes",    "tfmmdet",    "tfmmdetrc",  "tfmmdetc",  "tfmmes",     "tfmmmound", "tfmmmr",    "tfmmr",        "tfmmrb",    "tfmmrc",    "tfmmre",
             "tfmmrf",     "tfmmrsc",    "tfmmrv",     "tfmmruins", "tfmmue",     "tfmmaze",   "tfdtbal",   "tfdtbea",      "tfdtbb",    "tfdtbt",    "tfdtbri",
             "tfdteb",     "tfdtmai",    "tfdtmb",     "tfdtroos",  "tfdtra",     "tfdtrc",    "tfdtrfp",   "tfdtrr",       "tfdtwin",   "tfitmai",   "tfitwin",
             "tfitroof",   "tfitbea",    "tfitboss",   "tfitent",   "tfitbri",    "tfitst",    "tftcloud",  "tfclca",       "tfcltr",    "tftcva",    "tffcmain",
             "tffcstto",   "tffclato",   "tffcmur",    "tffctof48", "tffcro48cr", "tffcbogaz", "tffcsito",  "tffcdunst",    "tffcdunbor", "tffcro9cr", "tffcro13cr",
             "tffcro13con", "tffcro13pk", "tffcento",  "tffcensito", "tffcenboto", "tffcenst", "tffcbelto", "tffcbri",      "tffctof13", "tffcbef21", "tffcfth21",
             "tffcdamt",   "tffcwrt",
             // KnightStrongholdComponent#getTerrainAdjustment: isComponentProtected() ? BURY : NONE;
             // these six answer isComponentProtected() = false
             "tfsac", "tfsua", "tfsult", "tfsurt", "tfsuco", "tfsuti"});
        add(Rule::BeardBox0, {"tfhedge"});
        add(Rule::BeardBox1, {"tfltmai", "tfdtent", "tflttfoy", "tfltctbase", "tfltcttrim", "tfltgrave"});
        add(Rule::BeardBox2, {"tfltfence"});
        add(Rule::BeardBox3, {"tfncte", "tfnche"});
        add(Rule::Bury, {"tfssh", "tfslt", "tfscr", "tfsrt", "tfsde", "tfsbalr", "tfstr", "tfsss", "tfstc", "tfsat", "tfsfo", "tftrear", "tfsbr", "tfsenter", "tfsshield",
                         "tftcmai", "tftccon", "tftcgard", "tffcdunr31", "tffcdunen", "tffcdunex"});
        add(Rule::ModifierNone, {"tfltctseg", "tfltmobbridge", "tfltbridge", "tflttroof", "tflttroom", "tflttdecor", "tflttgallery", "tflttfoyd", "tflttboss",
                                 "tflttbossroof", "tfltlight"});
        add(Rule::Jigsaw, {"tfjigsawtemplate"});
        add(Rule::WingBeard, {"tflttbeard"});
        add(Rule::YardBox, {"tfltpath"});
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

std::optional<PieceBeard> tfPieceBeard(std::string const& typeId, BlockBox const& box, nlohmann::json const& tag) {
    auto        colon = typeId.find(':');
    std::string ns    = colon == std::string::npos ? std::string("minecraft") : lower(typeId.substr(0, colon));
    if (ns != "twilightforest") return std::nullopt;
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

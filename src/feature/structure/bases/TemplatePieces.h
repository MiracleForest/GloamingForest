// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/TFStructureComponentTemplate.java
//   src/main/java/gloamingforest/world/components/structures/GloamingTemplateStructurePiece.java
//   src/main/java/gloamingforest/world/components/structures/GloamingDoubleTemplateStructurePiece.java
//   src/main/java/gloamingforest/world/components/structures/GloamingJigsawPiece.java
//   src/main/java/gloamingforest/util/jigsaw/JigsawRecord.java
//   src/main/java/gloamingforest/util/jigsaw/JigsawPlaceContext.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#pragma once

// Contract "structure", shared bases (header owned by F0, implemented by F7; rule R18). The
// template-backed piece bases. Rule R19: upstream's .nbt templates are All Rights Reserved, so every
// template here is an original GloamingForest template (.gft.json, structure::templates) with the
// id of the upstream template it stands in for ("gloamingforest:lich_tower/root/foyer"); data markers are
// the template's markers (upstream's STRUCTURE_BLOCK DATA mode "metadata" strings).

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "feature/structure/bases/ComponentOld.h"

namespace gf::structure::bases {

/// StructurePlaceSettings of a template piece (rotation, mirror, pivot; structure blocks ignored).
struct PlaceSettings {
    Rotation                 rotation = Rotation::None;
    Mirror                   mirror   = Mirror::None;
    BlockPos                 pivot{0, 0, 0};
    std::vector<std::string> processors; ///< processor list / processor ids applied on placement
};
/// makeSettings(rotation[, mirror]) / randomRotation(random) / readSettings(saved).
[[nodiscard]] PlaceSettings makeSettings(Rotation rotation, Mirror mirror = Mirror::None);
[[nodiscard]] PlaceSettings randomRotation(RandomSource& random);
[[nodiscard]] PlaceSettings readSettings(Json const& saved);
[[nodiscard]] Json          writeSettings(PlaceSettings const& settings);

/// TFStructureComponentTemplate: a TFStructureComponent placing one template at templatePosition.
class ComponentTemplate : public Component {
public:
    ComponentTemplate(std::string typeId, int genDepth, int x, int y, int z, BlockBox const& box);
    ComponentTemplate(std::string typeId, int genDepth, int x, int y, int z, Rotation rotation);

    /// setup: loadTemplates, then the rotated position and the box from the template's size.
    void setup();
    /// "TPX", "TPY", "TPZ" plus Component's fields.
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

protected:
    /// Picks `templateId` (and any other templates the piece needs).
    virtual void loadTemplates() = 0;
    void         setModifiedTemplatePositionFromRotation();
    void         setBoundingBoxFromTemplate(BlockPos const& pos);
    void         setTemplatePositionFromRotation();

    PlaceSettings placeSettings;
    BlockPos      templatePosition{0, 0, 0};
    BlockPos      rotatedPosition{0, 0, 0};
    std::string   templateId; ///< Java's TEMPLATE
};

/// GloamingTemplateStructurePiece: TemplateStructurePiece without vanilla's jigsaw handling; data
/// markers go to handleDataMarker with the region and the rotation.
class TemplatePiece : public Piece {
public:
    TemplatePiece(std::string typeId, int genDepth, std::string templateId, PlaceSettings settings, BlockPos const& startPosition);

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    [[nodiscard]] std::string const& getTemplateName() const { return templateName; }
    [[nodiscard]] Json               save() const override;
    void                             load(Json const& data) override;

protected:
    /// The template's data markers ("metadata" label at `pos`), after the template was placed.
    virtual void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBox,
                                  Rotation rotation);
    /// placePieceAdjusted: places the template `dY` blocks higher, then restores position and box.
    void placePieceAdjusted(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pos, int dY);

    std::string   templateName;
    PlaceSettings placeSettings;
    BlockPos      templatePosition{0, 0, 0};
    int           placeFlag = 2; ///< Block.UPDATE_CLIENTS
};

/// GloamingDoubleTemplateStructurePiece: a template plus an overlay template on the same spot.
class DoubleTemplatePiece : public TemplatePiece {
public:
    DoubleTemplatePiece(std::string typeId, int genDepth, std::string templateId, PlaceSettings settings, std::string overlayTemplateId,
                        PlaceSettings overlaySettings, BlockPos const& startPosition);

    void postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;
    [[nodiscard]] Json save() const override;
    void               load(Json const& data) override;

protected:
    std::string   templateOverlayLocation;
    PlaceSettings placeSettingsOverlay;
};

/// JigsawRecord: a jigsaw connector of a template (`pos` is relative to the template origin).
struct JigsawRecord {
    int         priority = 0;     ///< "selection_priority": higher generates first
    Orientation front    = Orientation::North;
    bool        frontVertical = false; ///< FrontAndTop with an UP / DOWN front
    bool        frontUp       = false;
    Orientation top      = Orientation::North;
    BlockPos    pos{0, 0, 0};
    std::string pool = "minecraft:empty";
    std::string name;
    std::string target;
};

/// JigsawPlaceContext: where and how a template attaches to a parent's jigsaw.
struct JigsawPlaceContext {
    BlockPos                  templatePos{0, 0, 0};
    PlaceSettings             placementSettings;
    JigsawRecord              seedJigsaw;
    std::vector<JigsawRecord> spareJigsaws;
    std::string               templateLocation;

    /// pickPlaceableJunction: a template of `templateId` whose jigsaw named `jigsawNameLabel` meets
    /// the parent's jigsaw at `sourceJigsawPos` (nullopt: none fits).
    [[nodiscard]] static std::optional<JigsawPlaceContext> pickPlaceableJunction(BlockPos const& parentTemplatePos, BlockPos const& sourceJigsawPos,
                                                                                 JigsawRecord const& sourceJigsaw, std::string const& templateId,
                                                                                 std::string const& jigsawNameLabel, RandomSource& random);
    [[nodiscard]] JigsawRecord const* findFirst(std::string const& name) const;
    [[nodiscard]] BlockBox            makeBoundingBox() const;
    [[nodiscard]] bool                isWithoutCollision(PieceList const& pieces) const;
};

/// GloamingJigsawPiece: a template piece that grows children from its spare jigsaws.
class JigsawPiece : public TemplatePiece {
public:
    JigsawPiece(std::string typeId, int genDepth, std::string templateId, JigsawPlaceContext const& context);

    /// addJigsaws: fills each spare jigsaw (processJigsaw), adding the children to `pieces`.
    virtual void addJigsaws(JigsawPiece& parent, PieceList& pieces, RandomSource& random);
    void         postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) override;

    [[nodiscard]] JigsawRecord const&              getSourceJigsaw() const { return sourceJigsaw; }
    [[nodiscard]] BlockPos                         getSourcePosition() const;
    [[nodiscard]] std::vector<JigsawRecord> const& getSpareJigsaws() const { return spareJigsaws; }
    [[nodiscard]] std::vector<JigsawRecord>        matchSpareJigsaws(std::function<bool(JigsawRecord const&)> const& filter) const;
    [[nodiscard]] int                              firstMatchIndex(std::function<bool(JigsawRecord const&)> const& filter) const;

    // PieceBeardifierModifier
    [[nodiscard]] virtual BlockBox          getBeardifierBox() const;
    [[nodiscard]] virtual TerrainAdjustment getTerrainAdjustment() const { return terrainAdjustment; }
    [[nodiscard]] virtual int               getGroundLevelDelta() const { return beardifierGroundDelta; }
    [[nodiscard]] Json              save() const override;
    void                            load(Json const& data) override;

protected:
    virtual void processJigsaw(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& connection, int jigsawIndex);
    void handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBox,
                          Rotation rotation) override;

    JigsawRecord                       sourceJigsaw;
    std::vector<JigsawRecord>          spareJigsaws;
    TerrainAdjustment                  terrainAdjustment = TerrainAdjustment::None;
    std::optional<std::string>         processors;     ///< processor list id ("template_processors")
    std::optional<std::string>         markerHandlers; ///< template_marker_handler_list id ("marker_handlers")
    bool                               rigidProjection = true; ///< StructureTemplatePool.Projection ("place_projection")
    int                                beardifierGroundDelta = 0; ///< "ground_offset"
    std::map<std::string, std::string> poolAliases;
};

} // namespace gf::structure::bases

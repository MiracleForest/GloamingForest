// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFStructureComponentTemplate.java
//   src/main/java/twilightforest/world/components/structures/TwilightTemplateStructurePiece.java
//   src/main/java/twilightforest/world/components/structures/TwilightDoubleTemplateStructurePiece.java
//   src/main/java/twilightforest/world/components/structures/TwilightJigsawPiece.java
//   src/main/java/twilightforest/util/jigsaw/JigsawRecord.java
//   src/main/java/twilightforest/util/jigsaw/JigsawPlaceContext.java
//   src/main/java/twilightforest/util/jigsaw/JigsawUtil.java
//   src/main/java/twilightforest/util/RotationUtil.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "feature/structure/bases/TemplatePieces.h"

#include <algorithm>
#include <climits>

#include "feature/structure/bases/BasesInternal.h"
#include "feature/worldgen/Worldgen.h"

namespace gf::structure::bases {

namespace detail {

templates::Placement placement(PlaceSettings const& settings, BlockPos const& templatePosition, BlockPos const& pivotPos, std::optional<BlockBox> clip) {
    templates::Placement p;
    p.offset              = templatePosition;
    p.pivotPos            = pivotPos;
    p.rotationPivot       = settings.pivot;
    p.settings.rotation   = settings.rotation;
    p.settings.mirror     = settings.mirror;
    p.settings.processors = settings.processors;
    p.settings.clip       = clip;
    return p;
}

BlockPos templateSize(std::string const& id, Rotation rotation) {
    auto const* t = templates::parsed(id);
    if (!t) return BlockPos{0, 0, 0};
    if (rotation == Rotation::Clockwise90 || rotation == Rotation::Counterclockwise90) return BlockPos{t->sz, t->sy, t->sx};
    return BlockPos{t->sx, t->sy, t->sz};
}

namespace {

// FrontAndTop: front direction (6) and top (horizontal for vertical fronts, UP otherwise)
enum Dir6 { Down, Up, North, South, West, East };

Dir6 parse6(std::string const& s) {
    if (s == "down") return Down;
    if (s == "up") return Up;
    if (s == "south") return South;
    if (s == "west") return West;
    if (s == "east") return East;
    return North;
}

Orientation horizontalOf(Dir6 d) {
    switch (d) {
    case South:
        return Orientation::South;
    case West:
        return Orientation::West;
    case East:
        return Orientation::East;
    default:
        return Orientation::North;
    }
}

} // namespace

JigsawRecord recordFrom(templates::Connector const& c) {
    JigsawRecord r;
    Dir6         front = parse6(c.front);
    r.frontVertical    = front == Up || front == Down;
    r.frontUp          = front == Up;
    r.front            = r.frontVertical ? Orientation::North : horizontalOf(front);
    r.top              = r.frontVertical ? horizontalOf(parse6(c.top)) : Orientation::North;
    r.pos              = c.pos;
    r.pool             = c.pool;
    r.name             = c.name;
    r.target           = c.target;
    r.priority         = c.priority;
    return r;
}

JigsawRecord rotated(JigsawRecord r, Rotation rotation) {
    if (r.frontVertical) r.top = rotateDir(r.top, rotation);
    else r.front = rotateDir(r.front, rotation);
    return r;
}

// FrontAndTop.values(): DOWN_EAST, DOWN_NORTH, DOWN_SOUTH, DOWN_WEST, UP_EAST, UP_NORTH, UP_SOUTH,
// UP_WEST, WEST_UP, EAST_UP, NORTH_UP, SOUTH_UP
int frontAndTopOrdinal(JigsawRecord const& r) {
    auto hz = [](Orientation o) {
        switch (o) {
        case Orientation::East:
            return 0;
        case Orientation::North:
            return 1;
        case Orientation::South:
            return 2;
        default:
            return 3;
        }
    };
    if (r.frontVertical) return (r.frontUp ? 4 : 0) + hz(r.top);
    switch (r.front) {
    case Orientation::West:
        return 8;
    case Orientation::East:
        return 9;
    case Orientation::North:
        return 10;
    default:
        return 11;
    }
}

void setFrontAndTop(JigsawRecord& r, int ordinal) {
    static Orientation const hz[] = {Orientation::East, Orientation::North, Orientation::South, Orientation::West};
    if (ordinal >= 0 && ordinal < 8) {
        r.frontVertical = true;
        r.frontUp       = ordinal >= 4;
        r.top           = hz[ordinal & 3];
        r.front         = Orientation::North;
        return;
    }
    r.frontVertical = false;
    r.frontUp       = false;
    r.top           = Orientation::North;
    switch (ordinal) {
    case 8:
        r.front = Orientation::West;
        break;
    case 9:
        r.front = Orientation::East;
        break;
    case 10:
        r.front = Orientation::North;
        break;
    default:
        r.front = Orientation::South;
    }
}

Json recordToJson(JigsawRecord const& r) {
    return Json{{"priority", r.priority}, {"facing", frontAndTopOrdinal(r)}, {"x", r.pos.x}, {"y", r.pos.y}, {"z", r.pos.z},
                {"pool", r.pool},         {"name", r.name},                  {"target", r.target}};
}

JigsawRecord recordFromJson(Json const& j) {
    JigsawRecord r;
    r.priority = j.value("priority", 0);
    setFrontAndTop(r, j.value("facing", 10));
    r.pos    = BlockPos{j.value("x", 0), j.value("y", 0), j.value("z", 0)};
    r.pool   = j.value("pool", std::string("minecraft:empty"));
    r.name   = j.value("name", std::string{});
    r.target = j.value("target", std::string{});
    return r;
}

TerrainAdjustment parseTerrainAdjustment(std::string const& name) {
    std::string n;
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "bury") return TerrainAdjustment::Bury;
    if (n == "beard_thin") return TerrainAdjustment::BeardThin;
    if (n == "beard_box") return TerrainAdjustment::BeardBox;
    if (n == "encapsulate") return TerrainAdjustment::Encapsulate;
    return TerrainAdjustment::None;
}

char const* terrainAdjustmentName(TerrainAdjustment t) {
    switch (t) {
    case TerrainAdjustment::Bury:
        return "BURY";
    case TerrainAdjustment::BeardThin:
        return "BEARD_THIN";
    case TerrainAdjustment::BeardBox:
        return "BEARD_BOX";
    case TerrainAdjustment::Encapsulate:
        return "ENCAPSULATE";
    default:
        return "NONE";
    }
}

int& maxJigsawDepth() {
    thread_local int depth = INT_MAX;
    return depth;
}

} // namespace detail

// ---- settings ----------------------------------------------------------------------------------------------

PlaceSettings makeSettings(Rotation rotation, Mirror mirror) { return PlaceSettings{rotation, mirror, BlockPos{0, 0, 0}, {}}; }

// Rotation.getRandom: Util.getRandom(values(), random)
PlaceSettings randomRotation(RandomSource& random) { return makeSettings(rotationFromIndex(random.nextInt(4))); }

PlaceSettings readSettings(Json const& saved) {
    PlaceSettings s;
    s.rotation = rotationFromIndex(saved.value("rotation", 0));
    s.mirror   = static_cast<Mirror>(((saved.value("mirror", 0) % 3) + 3) % 3);
    s.pivot    = BlockPos{saved.value("pivot_x", 0), saved.value("pivot_y", 0), saved.value("pivot_z", 0)};
    for (auto const& p : saved.value("gf_processors", Json::array())) s.processors.push_back(p.get<std::string>());
    return s;
}

Json writeSettings(PlaceSettings const& s) {
    Json j{{"rotation", rotationIndex(s.rotation)}, {"mirror", static_cast<int>(s.mirror)}, {"pivot_x", s.pivot.x}, {"pivot_y", s.pivot.y}, {"pivot_z", s.pivot.z}};
    if (!s.processors.empty()) j["gf_processors"] = s.processors;
    return j;
}

// ---- TFStructureComponentTemplate ----------------------------------------------------------------------------

ComponentTemplate::ComponentTemplate(std::string typeId, int depth, int x, int y, int z, BlockBox const& box)
: Component(std::move(typeId), depth, box),
  templatePosition(x, y, z) {
    mirror = Mirror::None;
}

ComponentTemplate::ComponentTemplate(std::string typeId, int depth, int x, int y, int z, Rotation rot)
: Component(std::move(typeId), depth, x, y, z),
  templatePosition(x, y, z) {
    rotation               = rot;
    mirror                 = Mirror::None;
    placeSettings.rotation = rot;
}

void ComponentTemplate::setup() {
    loadTemplates();
    setModifiedTemplatePositionFromRotation();
    setBoundingBoxFromTemplate(rotatedPosition);
}

Json ComponentTemplate::save() const {
    Json j   = Component::save();
    j["TPX"] = templatePosition.x;
    j["TPY"] = templatePosition.y;
    j["TPZ"] = templatePosition.z;
    return j;
}

void ComponentTemplate::load(Json const& data) {
    Component::load(data);
    templatePosition       = BlockPos{data.value("TPX", 0), data.value("TPY", 0), data.value("TPZ", 0)};
    placeSettings.rotation = rotation;
    // the LAZY_TEMPLATE_LOADER: subclasses call setup() when they first need the template
}

void ComponentTemplate::setModifiedTemplatePositionFromRotation() {
    Rotation r    = placeSettings.rotation;
    BlockPos size = detail::templateSize(templateId, r);
    rotatedPosition = templatePosition;
    if (r == Rotation::Clockwise90 || r == Rotation::Clockwise180) rotatedPosition.x += size.z - 1;
    if (r == Rotation::Clockwise180 || r == Rotation::Counterclockwise90) rotatedPosition.z += size.x - 1;
}

void ComponentTemplate::setBoundingBoxFromTemplate(BlockPos const& pos) {
    Rotation r    = placeSettings.rotation;
    BlockPos size = detail::templateSize(templateId, r);
    Mirror   m    = placeSettings.mirror;
    bbox          = BlockBox{0, 0, 0, size.x, size.y - 1, size.z};
    switch (r) {
    case Rotation::Clockwise90:
        bbox = moved(bbox, -size.x, 0, 0);
        break;
    case Rotation::Counterclockwise90:
        bbox = moved(bbox, 0, 0, -size.z);
        break;
    case Rotation::Clockwise180:
        bbox = moved(bbox, -size.x, 0, -size.z);
        break;
    default:
        break;
    }
    if (m == Mirror::FrontBack) {
        int dx = 0, dz = 0;
        if (r != Rotation::Clockwise90 && r != Rotation::Counterclockwise90) {
            dx = r == Rotation::Clockwise180 ? size.x : -size.x;
        } else {
            Orientation d = rotateDir(Orientation::West, r);
            dx            = stepX(d) * size.z;
            dz            = stepZ(d) * size.z;
        }
        bbox = moved(bbox, dx, 0, dz);
    } else if (m == Mirror::LeftRight) {
        int dx = 0, dz = 0;
        if (r != Rotation::Clockwise90 && r != Rotation::Counterclockwise90) {
            dz = r == Rotation::Clockwise180 ? size.z : -size.z;
        } else {
            Orientation d = rotateDir(Orientation::North, r);
            dx            = stepX(d) * size.x;
            dz            = stepZ(d) * size.x;
        }
        bbox = moved(bbox, dx, 0, dz);
    }
    bbox = moved(bbox, pos.x, pos.y, pos.z);
}

void ComponentTemplate::setTemplatePositionFromRotation() {
    Rotation r    = placeSettings.rotation;
    BlockPos size = detail::templateSize(templateId, r);
    if (r == Rotation::Clockwise90 || r == Rotation::Clockwise180) templatePosition.x += size.z - 1;
    if (r == Rotation::Clockwise180 || r == Rotation::Counterclockwise90) templatePosition.z += size.x - 1;
}

// ---- TwilightTemplateStructurePiece -----------------------------------------------------------------------------

TemplatePiece::TemplatePiece(std::string typeId, int depth, std::string templateId, PlaceSettings settings, BlockPos const& startPosition)
: Piece(std::move(typeId)),
  templateName(std::move(templateId)),
  placeSettings(std::move(settings)),
  templatePosition(startPosition) {
    genDepth    = depth;
    orientation = kNoOrientation;
    rotation    = placeSettings.rotation;
    mirror      = placeSettings.mirror;
    if (auto const* t = templates::parsed(templateName))
        bbox = templateBox(templatePosition, placeSettings.rotation, placeSettings.pivot, placeSettings.mirror, t->sx, t->sy, t->sz);
    else bbox = BlockBox{startPosition.x, startPosition.y, startPosition.z, startPosition.x, startPosition.y, startPosition.z};
}

void TemplatePiece::postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) {
    auto const* t = templates::parsed(templateName);
    if (!t) return;
    bbox = templateBox(templatePosition, placeSettings.rotation, placeSettings.pivot, placeSettings.mirror, t->sx, t->sy, t->sz);
    auto p  = detail::placement(placeSettings, templatePosition, pivot, chunkBox);
    p.flags = placeFlag;
    std::vector<std::pair<BlockPos, std::string>> data;
    if (templates::placeInWorld(*t, region, p, random, &data)) {
        Rotation r = placeSettings.rotation;
        for (auto const& [pos, label] : data) handleDataMarker(label, pos, region, random, chunkBox, r);
    }
}

Json TemplatePiece::save() const {
    Json j        = Piece::save();
    j["Template"] = templateName;
    j["TPX"]      = templatePosition.x;
    j["TPY"]      = templatePosition.y;
    j["TPZ"]      = templatePosition.z;
    j.update(writeSettings(placeSettings));
    return j;
}

void TemplatePiece::load(Json const& data) {
    Piece::load(data);
    templateName     = data.value("Template", templateName);
    templatePosition = BlockPos{data.value("TPX", 0), data.value("TPY", 0), data.value("TPZ", 0)};
    placeSettings    = readSettings(data);
    rotation         = placeSettings.rotation;
    mirror           = placeSettings.mirror;
}

void TemplatePiece::handleDataMarker(std::string const&, BlockPos const&, Region&, RandomSource&, BlockBox const&, Rotation) {}

void TemplatePiece::placePieceAdjusted(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pos, int dY) {
    BlockPos originalPlacement = templatePosition;
    BlockBox originalBox       = bbox;
    templatePosition.y += dY;
    postProcess(region, random, chunkBox, BlockPos{pos.x, pos.y + dY, pos.z});
    templatePosition = originalPlacement;
    bbox             = originalBox;
}

// ---- TwilightDoubleTemplateStructurePiece -----------------------------------------------------------------------

DoubleTemplatePiece::DoubleTemplatePiece(std::string typeId, int depth, std::string templateId, PlaceSettings settings, std::string overlayTemplateId,
                                         PlaceSettings overlaySettings, BlockPos const& start)
: TemplatePiece(std::move(typeId), depth, std::move(templateId), std::move(settings), start),
  templateOverlayLocation(std::move(overlayTemplateId)),
  placeSettingsOverlay(std::move(overlaySettings)) {}

void DoubleTemplatePiece::postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) {
    TemplatePiece::postProcess(region, random, chunkBox, pivot);
    auto const* overlay = templates::parsed(templateOverlayLocation);
    if (!overlay) return;
    // [VanillaCopy] TemplateStructurePiece.postProcess with the overlay. TF passes the overlay's data
    // markers to the deprecated five-argument handleDataMarker, which does nothing.
    auto p = detail::placement(placeSettingsOverlay, templatePosition, pivot, chunkBox);
    (void)templates::placeInWorld(*overlay, region, p, random, nullptr);
}

Json DoubleTemplatePiece::save() const {
    Json j               = TemplatePiece::save();
    j["TemplateOverlay"] = templateOverlayLocation;
    j["overlay"]         = writeSettings(placeSettingsOverlay);
    return j;
}

void DoubleTemplatePiece::load(Json const& data) {
    TemplatePiece::load(data);
    templateOverlayLocation = data.value("TemplateOverlay", templateOverlayLocation);
    if (data.contains("overlay")) placeSettingsOverlay = readSettings(data["overlay"]);
}

// ---- JigsawPlaceContext -------------------------------------------------------------------------------------------

namespace {

/// JigsawUtil.canRearrangeForConnection.
bool canRearrangeForConnection(JigsawRecord const& source, JigsawRecord const& other) {
    if (source.frontVertical) return other.frontVertical && other.frontUp != source.frontUp;
    return !other.frontVertical;
}

/// Util.shuffle(list, random).
template <class T>
void shuffle(std::vector<T>& list, RandomSource& random) {
    for (int i = static_cast<int>(list.size()); i > 1; i--) std::swap(list[static_cast<std::size_t>(i - 1)], list[static_cast<std::size_t>(random.nextInt(i))]);
}

/// JigsawUtil.readConnectableJigsaws: the template's jigsaws in block order (y, x, z), shuffled
/// and stably sorted by selection priority (highest first).
std::vector<JigsawRecord> readConnectableJigsaws(std::string const& templateId, RandomSource* random) {
    std::vector<JigsawRecord> out;
    auto const*               t = templates::parsed(templateId);
    if (!t || t->sx == 0) return out;
    for (auto const& c : t->connectors) out.push_back(detail::recordFrom(c));
    std::stable_sort(out.begin(), out.end(), [](JigsawRecord const& a, JigsawRecord const& b) {
        if (a.pos.y != b.pos.y) return a.pos.y < b.pos.y;
        if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
        return a.pos.z < b.pos.z;
    });
    if (random) {
        shuffle(out, *random);
        std::stable_sort(out.begin(), out.end(), [](JigsawRecord const& a, JigsawRecord const& b) { return a.priority > b.priority; });
    }
    return out;
}

/// JigsawRecord.fromUnconfiguredJigsaw.
JigsawRecord configured(JigsawRecord const& raw, PlaceSettings const& settings) {
    JigsawRecord r = detail::rotated(raw, settings.rotation);
    r.pos          = transformPos(raw.pos, settings.mirror, settings.rotation, settings.pivot);
    return r;
}

} // namespace

std::optional<JigsawPlaceContext> JigsawPlaceContext::pickPlaceableJunction(BlockPos const& parentTemplatePos, BlockPos const& sourceJigsawPos,
                                                                            JigsawRecord const& sourceJigsaw, std::string const& templateId,
                                                                            std::string const& jigsawNameLabel, RandomSource& random) {
    if (templateId.empty()) return std::nullopt;
    auto connectables = readConnectableJigsaws(templateId, &random);
    BlockPos sourceTemplatePos{parentTemplatePos.x + sourceJigsawPos.x, parentTemplatePos.y + sourceJigsawPos.y, parentTemplatePos.z + sourceJigsawPos.z};

    std::optional<JigsawRecord> connectable;
    for (std::size_t i = 0; i < connectables.size(); i++) {
        if (connectables[i].name == jigsawNameLabel && canRearrangeForConnection(sourceJigsaw, connectables[i])) {
            connectable = connectables[i];
            connectables.erase(connectables.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    if (!connectable) return std::nullopt;

    // generateAtJunction
    Rotation relative = sourceJigsaw.frontVertical ? relativeRotation(connectable->top, sourceJigsaw.top)
                                                   : relativeRotation(opposite(connectable->front), sourceJigsaw.front);
    // getPlacement: centerPos.relative(sourceFront).subtract(otherOffset)
    BlockPos center = sourceTemplatePos;
    if (sourceJigsaw.frontVertical) center.y += sourceJigsaw.frontUp ? 1 : -1;
    else {
        center.x += stepX(sourceJigsaw.front);
        center.z += stepZ(sourceJigsaw.front);
    }
    BlockPos otherOffset = connectable->pos;

    JigsawPlaceContext ctx;
    ctx.templatePos       = BlockPos{center.x - otherOffset.x, center.y - otherOffset.y, center.z - otherOffset.z};
    ctx.placementSettings = makeSettings(relative, Mirror::None);
    ctx.placementSettings.pivot = otherOffset;
    // JigsawRecord.fromUnprocessedInfos: configure, shuffle, stable sort by priority (highest first)
    for (auto const& raw : connectables) ctx.spareJigsaws.push_back(configured(raw, ctx.placementSettings));
    shuffle(ctx.spareJigsaws, random);
    std::stable_sort(ctx.spareJigsaws.begin(), ctx.spareJigsaws.end(), [](JigsawRecord const& a, JigsawRecord const& b) { return a.priority > b.priority; });
    ctx.seedJigsaw       = configured(*connectable, ctx.placementSettings);
    ctx.templateLocation = templateId;
    return ctx;
}

JigsawRecord const* JigsawPlaceContext::findFirst(std::string const& name) const {
    for (auto const& r : spareJigsaws)
        if (r.name == name) return &r;
    return nullptr;
}

BlockBox JigsawPlaceContext::makeBoundingBox() const {
    auto const* t = templates::parsed(templateLocation);
    if (!t) return BlockBox{templatePos.x, templatePos.y, templatePos.z, templatePos.x, templatePos.y, templatePos.z};
    return templateBox(templatePos, placementSettings.rotation, placementSettings.pivot, placementSettings.mirror, t->sx, t->sy, t->sz);
}

bool JigsawPlaceContext::isWithoutCollision(PieceList const& pieces) const { return findCollisionPiece(pieces, makeBoundingBox()) == nullptr; }

// ---- TwilightJigsawPiece -----------------------------------------------------------------------------------------

JigsawPiece::JigsawPiece(std::string typeId, int depth, std::string templateId, JigsawPlaceContext const& context)
: TemplatePiece(std::move(typeId), depth, std::move(templateId), context.placementSettings, context.templatePos),
  sourceJigsaw(context.seedJigsaw),
  spareJigsaws(context.spareJigsaws) {}

void JigsawPiece::addJigsaws(JigsawPiece& parent, PieceList& pieces, RandomSource& random) {
    std::int64_t posLong = pure::blockPosAsLong(templatePosition.x, templatePosition.y, templatePosition.z);
    auto         mixed   = static_cast<std::uint64_t>(worldgen::seed()) * static_cast<std::uint64_t>(posLong);
    random.setSeed(random.nextLong() ^ static_cast<std::int64_t>(mixed));
    addChildren(parent, pieces, random);
    auto jigsaws = spareJigsaws;
    for (std::size_t i = 0; i < jigsaws.size(); i++) processJigsaw(parent, pieces, random, jigsaws[i], static_cast<int>(i));
}

void JigsawPiece::processJigsaw(JigsawPiece& parent, PieceList& pieces, RandomSource& random, JigsawRecord const& connection, int) {
    if (genDepth + 1 > detail::maxJigsawDepth()) return;
    auto        alias = poolAliases.find(connection.pool);
    std::string pool  = alias != poolAliases.end() ? alias->second : connection.pool;
    BlockPos    parentJunctionPos{templatePosition.x + connection.pos.x, templatePosition.y + connection.pos.y, templatePosition.z + connection.pos.z};
    auto        piece = detail::initializeTemplateFromPool(pool, parentJunctionPos, connection, connection.target, random, genDepth + 1);
    (void)parent; // parent.projection == TERRAIN_MATCHING only steers terrain adjustment (none without a generation context)
    if (!piece) return;
    if (findCollisionPiece(pieces, piece->bbox)) return;
    auto* raw = piece.get();
    pieces.push_back(std::move(piece));
    raw->addJigsaws(*this, pieces, random);
}

void JigsawPiece::postProcess(Region& region, RandomSource& random, BlockBox const& chunkBox, BlockPos const& pivot) {
    TemplatePiece::postProcess(region, random, chunkBox, pivot);
    // markPosForPostprocessing of the template's jigsaw blocks: .gft connectors are not blocks
}

BlockPos JigsawPiece::getSourcePosition() const {
    return BlockPos{templatePosition.x + sourceJigsaw.pos.x, templatePosition.y + sourceJigsaw.pos.y, templatePosition.z + sourceJigsaw.pos.z};
}

std::vector<JigsawRecord> JigsawPiece::matchSpareJigsaws(std::function<bool(JigsawRecord const&)> const& filter) const {
    std::vector<JigsawRecord> out;
    for (auto const& r : spareJigsaws)
        if (filter(r)) out.push_back(r);
    return out;
}

int JigsawPiece::firstMatchIndex(std::function<bool(JigsawRecord const&)> const& filter) const {
    for (std::size_t i = 0; i < spareJigsaws.size(); i++)
        if (filter(spareJigsaws[i])) return static_cast<int>(i);
    return -1;
}

BlockBox JigsawPiece::getBeardifierBox() const { return bbox; }

void JigsawPiece::handleDataMarker(std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBox,
                                   Rotation rot) {
    TemplatePiece::handleDataMarker(label, pos, region, random, chunkBox, rot);
    if (!markerHandlers) return;
    auto const* list = markers::list(*markerHandlers);
    if (!list || !list->is_array()) return;
    for (auto const& handler : *list)
        if (markers::handle(handler, label, pos, region, random, chunkBox, rot)) break;
}

Json JigsawPiece::save() const {
    Json j      = TemplatePiece::save();
    j["source"] = detail::recordToJson(sourceJigsaw);
    Json connections = Json::array();
    for (auto const& r : spareJigsaws) connections.push_back(detail::recordToJson(r));
    j["connections"] = connections;
    if (terrainAdjustment != TerrainAdjustment::None) j["terrain_adaptation"] = detail::terrainAdjustmentName(terrainAdjustment);
    if (processors) j["template_processors"] = *processors;
    j["place_projection"] = rigidProjection ? "RIGID" : "TERRAIN_MATCHING";
    if (markerHandlers) j["marker_handlers"] = *markerHandlers;
    j["ground_offset"] = beardifierGroundDelta;
    if (!poolAliases.empty()) j["pool_aliases"] = poolAliases;
    return j;
}

void JigsawPiece::load(Json const& data) {
    TemplatePiece::load(data);
    sourceJigsaw = detail::recordFromJson(data.value("source", Json::object()));
    spareJigsaws.clear();
    for (auto const& c : data.value("connections", Json::array())) spareJigsaws.push_back(detail::recordFromJson(c));
    terrainAdjustment = detail::parseTerrainAdjustment(data.value("terrain_adaptation", std::string("NONE")));
    processors.reset();
    if (data.contains("template_processors")) processors = data["template_processors"].get<std::string>();
    rigidProjection = data.value("place_projection", std::string("RIGID")) != "TERRAIN_MATCHING";
    markerHandlers.reset();
    if (data.contains("marker_handlers")) markerHandlers = data["marker_handlers"].get<std::string>();
    beardifierGroundDelta = data.value("ground_offset", 0);
    poolAliases.clear();
    for (auto const& [k, v] : data.value("pool_aliases", Json::object()).items()) poolAliases[k] = v.get<std::string>();
}

} // namespace gf::structure::bases

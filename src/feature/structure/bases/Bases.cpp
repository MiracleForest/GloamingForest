// Neutral stub of the shared structure bases (contract "structure", F0). F7 replaces this file with
// the translations of the TF classes named in the headers (rule R18). Every entry point reports
// itself once (GF_STUB) and returns a neutral value; constructors and plain accessors only store
// their arguments, so W1 pieces can be written and linked against the headers before F7 lands.
#include "feature/structure/bases/Component.h"
#include "feature/structure/bases/ComponentOld.h"
#include "feature/structure/bases/HollowHill.h"
#include "feature/structure/bases/Maze.h"
#include "feature/structure/bases/TemplatePieces.h"
#include "feature/structure/bases/Tower.h"

#include <algorithm>

#include "core/Stub.h"
#include "game/Blocks.h"

namespace gf::structure::bases {

namespace {
constexpr char kStub[] = "structure.bases";

struct AirSelector final : BlockSelector {
    Block const& next(RandomSource&, int, int, int, bool) override { return ComponentOld::air(); }
};
} // namespace

// ---- Component.h ---------------------------------------------------------------------------------

Piece* findCollisionPiece(PieceList const& pieces, BlockBox const& box) {
    for (auto const& p : pieces) {
        if (p && p->bbox.intersects(box)) return p.get();
    }
    return nullptr;
}

std::unique_ptr<Decorator> defaultDecorator() { return std::make_unique<Decorator>(); }
std::unique_ptr<Decorator> decoratorFor(std::string const&) {
    GF_STUB(kStub);
    return defaultDecorator();
}
void registerDecorator(std::string const&, std::function<std::unique_ptr<Decorator>()>) { GF_STUB(kStub); }

Component::Component(std::string typeId, int depth, BlockBox const& box) : Piece(std::move(typeId)) {
    bbox     = box;
    genDepth = depth;
}
Component::Component(std::string typeId, int depth, int x, int y, int z) : Component(std::move(typeId), depth, BlockBox{x, y, z, x, y, z}) {}
Json Component::save() const {
    GF_STUB(kStub);
    return Piece::save();
}
void Component::load(Json const& data) {
    GF_STUB(kStub);
    Piece::load(data);
}
void Component::placeBlockScheduled(Region&, Block const&, int, int, int, BlockBox const&) { GF_STUB(kStub); }
void Component::setInvisibleTextEntity(Region&, int, int, int, BlockBox const&, std::string const&, bool, float) {}

UtilityPiece::UtilityPiece(int depth, BlockBox const& box, bool allow) : Piece("twilightforest:TFUtilityPiece"), allowFeatures(allow) {
    bbox     = box;
    genDepth = depth;
}
void UtilityPiece::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) {}
Json UtilityPiece::save() const {
    GF_STUB(kStub);
    return Piece::save();
}
void UtilityPiece::load(Json const& data) {
    GF_STUB(kStub);
    Piece::load(data);
}

// ---- ComponentOld.h ------------------------------------------------------------------------------

ComponentOld::ComponentOld(std::string typeId, int depth, int x, int y, int z) : Component(std::move(typeId), depth, x, y, z) {}
ComponentOld::ComponentOld(std::string typeId, int depth, BlockBox const& box) : Component(std::move(typeId), depth, box) {}

void ComponentOld::setOrientation(std::optional<Orientation> facing) {
    GF_STUB(kStub);
    orientation = facing.value_or(Orientation::North);
}
std::optional<Orientation> ComponentOld::getOrientation() const { return orientation; }

BlockBox ComponentOld::getComponentToAddBoundingBox2(int x, int y, int z, int, int, int, int, int, int, Orientation) {
    GF_STUB(kStub);
    return BlockBox{x, y, z, x, y, z};
}
Piece* ComponentOld::findIntersectingExcluding(PieceList const&, BlockBox const&, Piece const*) {
    GF_STUB(kStub);
    return nullptr;
}
BlockPos     ComponentOld::getBlockPosWithOffset(int x, int y, int z) const { return getWorldPos(x, y, z); }
Block const& ComponentOld::getBlockStateFromPosRotated(Region&, int, int, int, BlockBox const&, Rotation) const {
    GF_STUB(kStub);
    return air();
}
void ComponentOld::replaceAirAndLiquidDownwardsRotated(Region&, Block const&, int, int, int, Rotation, BlockBox const&) { GF_STUB(kStub); }

BlockBox          ComponentOld::getBeardifierBox() const { return bbox; }
TerrainAdjustment ComponentOld::getTerrainAdjustment() const { return TerrainAdjustment::None; }
int               ComponentOld::getGroundLevelDelta() const { return 0; }

void ComponentOld::setSpawner(Region&, int, int, int, BlockBox const&, std::string const&, std::function<void(Json&)> const&) { GF_STUB(kStub); }
void ComponentOld::setSpawnerInWorld(Region&, BlockBox const&, std::string const&, std::function<void(Json&)> const&, BlockPos const&) {
    GF_STUB(kStub);
}
void ComponentOld::setSpawnerRotated(Region&, int, int, int, Rotation, std::string const&, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::surroundBlockCardinal(Region&, Block const&, int, int, int, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::surroundBlockCardinalRotated(Region&, Block const&, int, int, int, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::surroundBlockCorners(Region&, Block const&, int, int, int, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::placeTreasureAtCurrentPosition(Region&, int, int, int, std::string const&, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::placeTreasureAtCurrentPosition(Region&, int, int, int, std::string const&, bool, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::placeTreasureAtWorldPosition(Region&, std::string const&, bool, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
void ComponentOld::placeTreasureRotated(Region&, int, int, int, Orientation, Rotation, std::string const&, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::placeTreasureRotated(Region&, int, int, int, Orientation, Rotation, std::string const&, bool, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::manualTreaurePlacement(Region&, int, int, int, Orientation, std::string const&, bool, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::setDoubleLootChest(Region&, int, int, int, int, int, int, std::optional<Orientation>, std::string const&, BlockBox const&, bool) {
    GF_STUB(kStub);
}
void ComponentOld::setDoubleLootChest(
    Region&, int, int, int, int, int, int, std::optional<Orientation>, std::string const&, std::string const&, BlockBox const&, bool
) {
    GF_STUB(kStub);
}
void ComponentOld::placeTripwire(Region&, int, int, int, int, Orientation, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::placeSignAtCurrentPosition(Region&, int, int, int, std::string const&, std::string const&, BlockBox const&) { GF_STUB(kStub); }
std::array<int, 3> ComponentOld::offsetTowerCoords(int x, int y, int z, int, Orientation) const {
    GF_STUB(kStub);
    return {x, y, z};
}
BlockPos ComponentOld::offsetTowerCCoords(int x, int y, int z, int, Orientation) const {
    GF_STUB(kStub);
    return BlockPos{x, y, z};
}
int ComponentOld::worldX(int x, int z) const { return Piece::worldX(x, z); }
int ComponentOld::worldZ(int x, int z) const { return Piece::worldZ(x, z); }
int ComponentOld::getXWithOffsetRotated(int x, int z, Rotation) const {
    GF_STUB(kStub);
    return worldX(x, z);
}
int ComponentOld::getZWithOffsetRotated(int x, int z, Rotation) const {
    GF_STUB(kStub);
    return worldZ(x, z);
}
Orientation ComponentOld::getStructureRelativeRotation(Rotation) const {
    GF_STUB(kStub);
    return orientation;
}
void ComponentOld::setBlockStateRotated(Region&, Block const&, int, int, int, Rotation, BlockBox const&) { GF_STUB(kStub); }
void ComponentOld::fillBlocksRotated(Region&, BlockBox const&, int, int, int, int, int, int, Block const&, Rotation) { GF_STUB(kStub); }
void ComponentOld::randomlyFillBlocksRotated(
    Region&, BlockBox const&, RandomSource&, float, int, int, int, int, int, int, Block const&, Block const&, Rotation
) {
    GF_STUB(kStub);
}
void ComponentOld::fillAirRotated(Region&, BlockBox const&, int, int, int, int, int, int, Rotation) { GF_STUB(kStub); }
void ComponentOld::fillWithAir(Region&, BlockBox const&, int, int, int, int, int, int, std::function<bool(Block const&)> const&) { GF_STUB(kStub); }
void ComponentOld::fillWithBlocks(Region&, BlockBox const&, int, int, int, int, int, int, Block const&, std::function<bool(Block const&)> const&) {
    GF_STUB(kStub);
}
void ComponentOld::fillWithBlocks(
    Region&, BlockBox const&, int, int, int, int, int, int, Block const&, Block const&, std::function<bool(Block const&)> const&
) {
    GF_STUB(kStub);
}
BlockSelector& ComponentOld::getStrongholdStones() {
    GF_STUB(kStub);
    static AirSelector selector;
    return selector;
}
int ComponentOld::getAverageGroundLevel(Region&, BlockBox const&) const {
    GF_STUB(kStub);
    return -1;
}
int ComponentOld::findGroundLevel(Region&, BlockBox const&, int start, std::function<bool(Block const&)> const&) const {
    GF_STUB(kStub);
    return start;
}
bool ComponentOld::isBoundingBoxOutsideBiomes(Region&, std::function<bool(std::string const&)> const&, BlockPos const&) const {
    GF_STUB(kStub);
    return false;
}
Block const& ComponentOld::getStairState(Block const& stair, Orientation, bool) {
    GF_STUB(kStub);
    return stair;
}
Block const& ComponentOld::getSlabState(Block const& slab, SlabType) {
    GF_STUB(kStub);
    return slab;
}
Block const& ComponentOld::air() { return *game::blockWith("minecraft:air"); }

// ---- TemplatePieces.h ----------------------------------------------------------------------------

PlaceSettings makeSettings(Rotation rotation, Mirror mirror) { return PlaceSettings{rotation, mirror, BlockPos{0, 0, 0}, {}}; }
PlaceSettings randomRotation(RandomSource&) {
    GF_STUB(kStub);
    return makeSettings(Rotation::None);
}
PlaceSettings readSettings(Json const&) {
    GF_STUB(kStub);
    return makeSettings(Rotation::None);
}
Json writeSettings(PlaceSettings const&) {
    GF_STUB(kStub);
    return Json::object();
}

ComponentTemplate::ComponentTemplate(std::string typeId, int depth, int x, int y, int z, BlockBox const& box)
: Component(std::move(typeId), depth, box),
  templatePosition(x, y, z) {}
ComponentTemplate::ComponentTemplate(std::string typeId, int depth, int x, int y, int z, Rotation rot)
: Component(std::move(typeId), depth, x, y, z),
  templatePosition(x, y, z) {
    rotation               = rot;
    placeSettings.rotation = rot;
}
void ComponentTemplate::setup() { GF_STUB(kStub); }
Json ComponentTemplate::save() const { return Component::save(); }
void ComponentTemplate::load(Json const& data) { Component::load(data); }
void ComponentTemplate::setModifiedTemplatePositionFromRotation() { GF_STUB(kStub); }
void ComponentTemplate::setBoundingBoxFromTemplate(BlockPos const&) { GF_STUB(kStub); }
void ComponentTemplate::setTemplatePositionFromRotation() { GF_STUB(kStub); }

TemplatePiece::TemplatePiece(std::string typeId, int depth, std::string templateId, PlaceSettings settings, BlockPos const& start)
: Piece(std::move(typeId)),
  templateName(std::move(templateId)),
  placeSettings(std::move(settings)),
  templatePosition(start) {
    genDepth = depth;
}
void TemplatePiece::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
Json TemplatePiece::save() const {
    GF_STUB(kStub);
    return Piece::save();
}
void TemplatePiece::load(Json const& data) {
    GF_STUB(kStub);
    Piece::load(data);
}
void TemplatePiece::handleDataMarker(std::string const&, BlockPos const&, Region&, RandomSource&, BlockBox const&, Rotation) {}
void TemplatePiece::placePieceAdjusted(Region&, RandomSource&, BlockBox const&, BlockPos const&, int) { GF_STUB(kStub); }

DoubleTemplatePiece::DoubleTemplatePiece(
    std::string   typeId,
    int           depth,
    std::string   templateId,
    PlaceSettings settings,
    std::string   overlayTemplateId,
    PlaceSettings overlaySettings,
    BlockPos const& start
)
: TemplatePiece(std::move(typeId), depth, std::move(templateId), std::move(settings), start),
  templateOverlayLocation(std::move(overlayTemplateId)),
  placeSettingsOverlay(std::move(overlaySettings)) {}
void DoubleTemplatePiece::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
Json DoubleTemplatePiece::save() const { return TemplatePiece::save(); }
void DoubleTemplatePiece::load(Json const& data) { TemplatePiece::load(data); }

std::optional<JigsawPlaceContext> JigsawPlaceContext::pickPlaceableJunction(
    BlockPos const&, BlockPos const&, JigsawRecord const&, std::string const&, std::string const&, RandomSource&
) {
    GF_STUB(kStub);
    return std::nullopt;
}
JigsawRecord const* JigsawPlaceContext::findFirst(std::string const& name) const {
    for (auto const& r : spareJigsaws) {
        if (r.name == name) return &r;
    }
    return nullptr;
}
BlockBox JigsawPlaceContext::makeBoundingBox() const {
    GF_STUB(kStub);
    return BlockBox{templatePos.x, templatePos.y, templatePos.z, templatePos.x, templatePos.y, templatePos.z};
}
bool JigsawPlaceContext::isWithoutCollision(PieceList const&) const {
    GF_STUB(kStub);
    return false;
}

JigsawPiece::JigsawPiece(std::string typeId, int depth, std::string templateId, JigsawPlaceContext const& context)
: TemplatePiece(std::move(typeId), depth, std::move(templateId), context.placementSettings, context.templatePos),
  sourceJigsaw(context.seedJigsaw),
  spareJigsaws(context.spareJigsaws) {}
void     JigsawPiece::addJigsaws(JigsawPiece&, PieceList&, RandomSource&) { GF_STUB(kStub); }
void     JigsawPiece::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
BlockPos JigsawPiece::getSourcePosition() const {
    return BlockPos{templatePosition.x + sourceJigsaw.pos.x, templatePosition.y + sourceJigsaw.pos.y, templatePosition.z + sourceJigsaw.pos.z};
}
std::vector<JigsawRecord> JigsawPiece::matchSpareJigsaws(std::function<bool(JigsawRecord const&)> const& filter) const {
    std::vector<JigsawRecord> out;
    for (auto const& r : spareJigsaws) {
        if (filter(r)) out.push_back(r);
    }
    return out;
}
int JigsawPiece::firstMatchIndex(std::function<bool(JigsawRecord const&)> const& filter) const {
    for (std::size_t i = 0; i < spareJigsaws.size(); ++i) {
        if (filter(spareJigsaws[i])) return static_cast<int>(i);
    }
    return -1;
}
BlockBox JigsawPiece::getBeardifierBox() const { return bbox; }
Json     JigsawPiece::save() const { return TemplatePiece::save(); }
void     JigsawPiece::load(Json const& data) { TemplatePiece::load(data); }
void     JigsawPiece::processJigsaw(JigsawPiece&, PieceList&, RandomSource&, JigsawRecord const&, int) { GF_STUB(kStub); }
void     JigsawPiece::handleDataMarker(std::string const&, BlockPos const&, Region&, RandomSource&, BlockBox const&, Rotation) { GF_STUB(kStub); }

// ---- Maze.h --------------------------------------------------------------------------------------

Maze::Maze(int cellsWidth, int cellsDepth, RandomSource&)
: width(cellsWidth),
  depth(cellsDepth),
  rawWidth(cellsWidth * 2 + 1),
  rawDepth(cellsDepth * 2 + 1),
  storage(static_cast<std::size_t>(rawWidth) * static_cast<std::size_t>(rawDepth), 0) {}
int Maze::getCell(int, int) const {
    GF_STUB(kStub);
    return OUT_OF_BOUNDS;
}
void Maze::putWall(int, int, int, int, int) { GF_STUB(kStub); }
bool Maze::isWall(int, int, int, int) const {
    GF_STUB(kStub);
    return true;
}
void Maze::putRaw(int, int, int) { GF_STUB(kStub); }
bool Maze::allCellsNonZero() const {
    GF_STUB(kStub);
    return false;
}
void Maze::resetCells() { std::fill(storage.begin(), storage.end(), 0); }
void Maze::copyToStructure(Region&, int, int, int, ComponentOld&, BlockBox const&, RandomSource&) { GF_STUB(kStub); }
bool Maze::shouldTorch(int, int, RandomSource&) const {
    GF_STUB(kStub);
    return false;
}
bool Maze::shouldPillar(int, int) const {
    GF_STUB(kStub);
    return false;
}
bool Maze::shouldTree(int, int, RandomSource&) const {
    GF_STUB(kStub);
    return false;
}
void Maze::carveRoom1(int, int) { GF_STUB(kStub); }
void Maze::add4Exits() { GF_STUB(kStub); }
void Maze::generateRecursiveBacktracker(int, int, RandomSource&) { GF_STUB(kStub); }
void Maze::rbGen(int, int, RandomSource&) { GF_STUB(kStub); }

// ---- HollowHill.h --------------------------------------------------------------------------------

HollowHill::HollowHill(std::string typeId, int depth, int size, int x, int y, int z, std::string configId)
: ComponentOld(std::move(typeId), depth, x, y, z),
  hillSize(size),
  radius((size * 2 + 1) * 8 - 6),
  hdiam((size * 2 + 1) * 16),
  speleothemConfigId(std::move(configId)) {}
void HollowHill::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
Json HollowHill::save() const { return ComponentOld::save(); }
void HollowHill::load(Json const& data) { ComponentOld::load(data); }
void HollowHill::drainWater(BlockBox const&, BlockBox const&, Region&, int, Block const&, int, int, double, Block const&) { GF_STUB(kStub); }
float HollowHill::getDistSqFromCenter(BlockPos const& center, BlockPos const& to) {
    float x = static_cast<float>(to.x - center.x) - 0.5f;
    float z = static_cast<float>(to.z - center.z) - 0.5f;
    return x * x + z * z;
}
std::string HollowHill::getLevel1Mob(RandomSource&) const {
    GF_STUB(kStub);
    return "minecraft:spider";
}
std::string HollowHill::getLevel2Mob(RandomSource&) const {
    GF_STUB(kStub);
    return "minecraft:spider";
}
std::string HollowHill::getLevel3Mob(RandomSource&) const {
    GF_STUB(kStub);
    return "minecraft:spider";
}
void        HollowHill::generateSpeleothem(Region&, BlockPos const&, BlockBox const&, bool) { GF_STUB(kStub); }
std::string HollowHill::getMobID(RandomSource& random) const { return getMobID(random, hillSize); }
std::string HollowHill::getMobID(RandomSource&, int) const {
    GF_STUB(kStub);
    return "minecraft:spider";
}

// ---- Tower.h -------------------------------------------------------------------------------------

TowerWing::TowerWing(std::string typeId, int depth, int x, int y, int z) : ComponentOld(std::move(typeId), depth, x, y, z) {}
TowerWing::TowerWing(std::string typeId, int depth, int x, int y, int z, int pSize, int pHeight, Orientation)
: ComponentOld(std::move(typeId), depth, x, y, z),
  size(pSize),
  height(pHeight) {}
void TowerWing::addChildren(Piece&, PieceList&, RandomSource&) { GF_STUB(kStub); }
void TowerWing::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
Json TowerWing::save() const { return ComponentOld::save(); }
void TowerWing::load(Json const& data) { ComponentOld::load(data); }
bool TowerWing::makeTowerWing(PieceList&, RandomSource&, int, int, int, int, int, int, Rotation) {
    GF_STUB(kStub);
    return false;
}
void TowerWing::addOpening(int, int, int, Rotation) { GF_STUB(kStub); }
void TowerWing::addOpening(int, int, int, Orientation) { GF_STUB(kStub); }
void TowerWing::makeABeard(Piece&, PieceList&, RandomSource&) { GF_STUB(kStub); }
void TowerWing::makeARoof(Piece&, PieceList&, RandomSource&) { GF_STUB(kStub); }
bool TowerWing::isDeadEnd() const {
    GF_STUB(kStub);
    return true;
}
std::array<int, 3> TowerWing::getValidOpening(RandomSource&, Rotation) {
    GF_STUB(kStub);
    return {0, 0, 0};
}
int  TowerWing::getGroundLevelDelta() const { return ComponentOld::getGroundLevelDelta(); }
bool TowerWing::makeBridge(PieceList&, RandomSource&, int, int, int, int, int, int, Rotation) {
    GF_STUB(kStub);
    return false;
}
void TowerWing::makeAttachedRoof(PieceList&, RandomSource&) { GF_STUB(kStub); }
void TowerWing::tryToFitRoof(PieceList&, RandomSource&, std::unique_ptr<TowerRoof>) { GF_STUB(kStub); }
void TowerWing::makeFreestandingRoof(PieceList&, RandomSource&) { GF_STUB(kStub); }
void TowerWing::makeOpeningMarkers(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateThisTower(Region&, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateDeadEnd(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateFloor(Region&, RandomSource&, int, int, int, std::optional<Rotation>, std::optional<Rotation>, BlockBox const&) {
    GF_STUB(kStub);
}
void TowerWing::decorateWell(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateSkeletonRoom(Region&, RandomSource&, int, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateZombieRoom(Region&, RandomSource&, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateCactusRoom(Region&, RandomSource&, int, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateTreasureChest(Region&, int, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateSpiderWebs(Region&, RandomSource&, int, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateFurniture(Region&, RandomSource&, int, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateSolidRock(Region&, RandomSource&, int, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateLibrary(Region&, RandomSource&, int, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateLibraryTreasure(Region&, RandomSource&, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateFullLibrary(Region&, RandomSource&, int, int, Rotation, Rotation, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateTrap(Region&, int, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateStairTower(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateStairFloor(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateChandelier(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateHangingChains(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
bool TowerWing::chainCollides(BlockPos const&, std::vector<BlockPos> const&) const {
    GF_STUB(kStub);
    return true;
}
void TowerWing::decorateOneChain(Region&, RandomSource&, int, int, int, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateFloatingBooks(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateFloatingVines(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decoratePlanter(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::decorateStairWell(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
bool TowerWing::isWindowPos(int, int) const {
    GF_STUB(kStub);
    return false;
}
bool TowerWing::isWindowPos(int, int, int) const {
    GF_STUB(kStub);
    return false;
}
bool TowerWing::isOpeningPos(int, int, int) const {
    GF_STUB(kStub);
    return false;
}
bool TowerWing::isLadderPos(int, int, Rotation, Rotation) const {
    GF_STUB(kStub);
    return false;
}
int TowerWing::getLadderX(Rotation) const {
    GF_STUB(kStub);
    return 0;
}
int TowerWing::getLadderZ(Rotation) const {
    GF_STUB(kStub);
    return 0;
}
void TowerWing::makeOpenings(Region&, BlockBox const&) { GF_STUB(kStub); }
void TowerWing::makeDoorOpening(Region&, int, int, int, BlockBox const&) { GF_STUB(kStub); }
int  TowerWing::getYByStairs(int, RandomSource&, Rotation) {
    GF_STUB(kStub);
    return 0;
}
void TowerWing::makeWindows(Region&, BlockBox const&, bool) { GF_STUB(kStub); }
void TowerWing::makeWindowBlock(Region&, int, int, int, Rotation, BlockBox const&, bool) { GF_STUB(kStub); }
void TowerWing::makeWindowBase(Region&, int, int, int, Rotation, BlockBox const&) { GF_STUB(kStub); }
bool TowerWing::makeStairs(Region&, RandomSource&, BlockBox const&) {
    GF_STUB(kStub);
    return false;
}
bool TowerWing::makeStairs5(Region&, RandomSource&, BlockBox const&) {
    GF_STUB(kStub);
    return false;
}
void TowerWing::makeStairs5flight(Region&, BlockBox const&, int, Rotation, bool) { GF_STUB(kStub); }
bool TowerWing::makeStairs7(Region&, RandomSource&, BlockBox const&) {
    GF_STUB(kStub);
    return false;
}
void TowerWing::makeStairs7flight(Region&, BlockBox const&, int, Rotation, bool) { GF_STUB(kStub); }
bool TowerWing::makeStairs9(Region&, RandomSource&, BlockBox const&) {
    GF_STUB(kStub);
    return false;
}
void TowerWing::makeStairs9flight(Region&, BlockBox const&, int, Rotation, bool) { GF_STUB(kStub); }
bool TowerWing::makeStairs15(Region&, RandomSource&, BlockBox const&) {
    GF_STUB(kStub);
    return false;
}
void TowerWing::makeStairs15flight(Region&, RandomSource&, BlockBox const&, int, Rotation, bool) { GF_STUB(kStub); }
void TowerWing::generatePaintingsOnWall(Region&, RandomSource&, int, int, Orientation, int, BlockBox const&) { GF_STUB(kStub); }
std::optional<BlockPos> TowerWing::getRandomWallSpot(RandomSource&, int, Orientation, BlockBox const&) {
    GF_STUB(kStub);
    return std::nullopt;
}
void TowerWing::makeGlyphBranches(Region&, RandomSource&, Block const&, BlockBox const&) { GF_STUB(kStub); }

TowerMain::TowerMain(RandomSource&, int depth, int x, int y, int z) : TowerWing("twilightforest:TFLTMai", depth, x, y, z) {}
void               TowerMain::addChildren(Piece&, PieceList&, RandomSource&) { GF_STUB(kStub); }
void               TowerMain::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
std::array<int, 3> TowerMain::getOutbuildingOpening(RandomSource&, Rotation) {
    GF_STUB(kStub);
    return {0, 0, 0};
}
bool TowerMain::makeTowerOutbuilding(PieceList&, RandomSource&, int, int, int, int, int, int, Rotation) {
    GF_STUB(kStub);
    return false;
}
TerrainAdjustment TowerMain::getTerrainAdjustment() const { return ComponentOld::getTerrainAdjustment(); }
void              TowerMain::makeStairwayCrossings(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
void              TowerMain::makeStairCrossing(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
void              TowerMain::makeLichRoom(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
void              TowerMain::makeTowerPaintings(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }
void              TowerMain::makeLichFloor(Region&, int, Rotation, BlockBox const&) { GF_STUB(kStub); }
void              TowerMain::decorateLichChandelier(Region&, int, BlockBox const&) { GF_STUB(kStub); }
void              TowerMain::decoratePaintings(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }
void              TowerMain::decorateTorches(Region&, RandomSource&, int, BlockBox const&) { GF_STUB(kStub); }

TowerBridge::TowerBridge(int depth, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing("twilightforest:TFLTBri", depth, x, y, z, 3, 3, direction),
  dSize(pSize),
  dHeight(pHeight) {}
void     TowerBridge::addChildren(Piece&, PieceList&, RandomSource&) { GF_STUB(kStub); }
void     TowerBridge::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
BlockBox TowerBridge::getWingBB() const {
    GF_STUB(kStub);
    return bbox;
}

TowerOutbuilding::TowerOutbuilding(int depth, int x, int y, int z, int pSize, int pHeight, Orientation direction)
: TowerWing("twilightforest:TFLTOut", depth, x, y, z, pSize, pHeight, direction) {}
void TowerOutbuilding::makeABeard(Piece&, PieceList&, RandomSource&) { GF_STUB(kStub); }
bool TowerOutbuilding::makeTowerWing(PieceList&, RandomSource&, int, int, int, int, int, int, Rotation) {
    GF_STUB(kStub);
    return false;
}
void TowerOutbuilding::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }

TowerBeard::TowerBeard(std::string typeId, int depth, TowerWing const& wing, int x, int y, int z)
: ComponentOld(std::move(typeId), depth, x, y, z),
  size(wing.size - 2),
  height((wing.size - 2) / 2) {}
void TowerBeard::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
Json TowerBeard::save() const { return ComponentOld::save(); }
void TowerBeard::load(Json const& data) { ComponentOld::load(data); }
void TowerBeard::makePyramidBeard(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }

TowerBeardAttached::TowerBeardAttached(int depth, TowerWing const& wing, int x, int y, int z)
: TowerBeard("twilightforest:TFLTBA", depth, wing, x, y, z) {}
void TowerBeardAttached::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
void TowerBeardAttached::makeAttachedBeard(Region&, RandomSource&, BlockBox const&) { GF_STUB(kStub); }

TowerRoof::TowerRoof(std::string typeId, int depth, int x, int y, int z) : ComponentOld(std::move(typeId), depth, x, y, z) { spawnListIndex = -1; }
void TowerRoof::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) {}
bool TowerRoof::fits(TowerWing const&, PieceList const&) const {
    GF_STUB(kStub);
    return false;
}
Json TowerRoof::save() const { return ComponentOld::save(); }
void TowerRoof::load(Json const& data) { ComponentOld::load(data); }
void TowerRoof::makeAttachedOverhangBB(TowerWing const&) { GF_STUB(kStub); }
void TowerRoof::makeCapBB(TowerWing const&) { GF_STUB(kStub); }
void TowerRoof::makeOverhangBB(TowerWing const&) { GF_STUB(kStub); }

TowerRoofFence::TowerRoofFence(int depth, TowerWing const&, int x, int y, int z) : TowerRoof("twilightforest:TFLTRF", depth, x, y, z) {}
void TowerRoofFence::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }

TowerRoofGableForwards::TowerRoofGableForwards(int depth, TowerWing const&, int x, int y, int z)
: TowerRoof("twilightforest:TFLTRGF", depth, x, y, z) {}
void TowerRoofGableForwards::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
int  TowerRoofGableForwards::slopeChangeForSize() const {
    GF_STUB(kStub);
    return 0;
}

TowerRoofPointy::TowerRoofPointy(std::string typeId, int depth, TowerWing const&, int x, int y, int z) : TowerRoof(std::move(typeId), depth, x, y, z) {}
void TowerRoofPointy::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
int  TowerRoofPointy::slopeChangeForSize() const {
    GF_STUB(kStub);
    return 0;
}

TowerRoofPointyOverhang::TowerRoofPointyOverhang(int depth, TowerWing const& wing, int x, int y, int z)
: TowerRoofPointy("twilightforest:TFLTRPO", depth, wing, x, y, z) {}

TowerRoofSlab::TowerRoofSlab(std::string typeId, int depth, TowerWing const&, int x, int y, int z) : TowerRoof(std::move(typeId), depth, x, y, z) {}
void TowerRoofSlab::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }
void TowerRoofSlab::makePyramidCap(Region&, Block const&, Block const&, BlockBox const&) { GF_STUB(kStub); }
void TowerRoofSlab::makeConnectedCap(Region&, Block const&, Block const&, BlockBox const&) { GF_STUB(kStub); }

TowerRoofAttachedSlab::TowerRoofAttachedSlab(int depth, TowerWing const& wing, int x, int y, int z)
: TowerRoofSlab("twilightforest:TFLTRAS", depth, wing, x, y, z) {}
void TowerRoofAttachedSlab::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }

TowerRoofSlabForwards::TowerRoofSlabForwards(int depth, TowerWing const& wing, int x, int y, int z)
: TowerRoofSlab("twilightforest:TFLTRSF", depth, wing, x, y, z) {}
void TowerRoofSlabForwards::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }

TowerRoofStairs::TowerRoofStairs(int depth, TowerWing const&, int x, int y, int z) : TowerRoof("twilightforest:TFLTRSt", depth, x, y, z) {}
void TowerRoofStairs::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }

TowerRoofStairsOverhang::TowerRoofStairsOverhang(int depth, TowerWing const&, int x, int y, int z)
: TowerRoof("twilightforest:TFLTRStO", depth, x, y, z) {}
void TowerRoofStairsOverhang::postProcess(Region&, RandomSource&, BlockBox const&, BlockPos const&) { GF_STUB(kStub); }

} // namespace gf::structure::bases

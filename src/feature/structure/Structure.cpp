// Neutral stub of the "structure" contract (F0). F7 replaces this file (rule R18).
#include "feature/structure/Structure.h"

#include "core/Stub.h"
#include "game/Blocks.h"

namespace gf::structure {

namespace {
Block const& air() { return *game::blockWith("minecraft:air"); }
} // namespace

void Piece::addChildren(Piece&, std::vector<std::unique_ptr<Piece>>&, RandomSource&) {}
Json Piece::save() const { return Json::object(); }
void Piece::load(Json const&) {}

int Piece::worldX(int x, int) const {
    GF_STUB("structure.Piece");
    return bbox.minX + x;
}
int          Piece::worldY(int y) const { return bbox.minY + y; }
int          Piece::worldZ(int, int z) const { return bbox.minZ + z; }
Block const& Piece::oriented(Block const& block) const { return block; }
void         Piece::placeBlock(Region&, Block const&, int, int, int, BlockBox const&) { GF_STUB("structure.Piece"); }
void         Piece::placeBlock(Region&, std::string const&, Json const&, int, int, int, BlockBox const&) { GF_STUB("structure.Piece"); }
Block const& Piece::getBlock(Region&, int, int, int, BlockBox const&) const {
    GF_STUB("structure.Piece");
    return air();
}
void Piece::fill(Region&, BlockBox const&, int, int, int, int, int, int, Block const&, Block const&, bool) { GF_STUB("structure.Piece"); }
void Piece::fillAir(Region&, BlockBox const&, int, int, int, int, int, int) { GF_STUB("structure.Piece"); }
void Piece::randomFill(Region&, BlockBox const&, int, int, int, int, int, int, bool, RandomSource&, BlockSelector&) {
    GF_STUB("structure.Piece");
}
bool Piece::placeChest(Region&, BlockBox const&, RandomSource&, int, int, int, std::string const&, std::string const&) {
    GF_STUB("structure.Piece");
    return false;
}
bool Piece::placeSpawner(Region&, BlockBox const&, int, int, int, std::string const&) {
    GF_STUB("structure.Piece");
    return false;
}
bool Piece::placeTemplate(Region&, BlockBox const&, std::string const&, BlockPos const&, TemplateSettings const&) {
    GF_STUB("structure.Piece");
    return false;
}

void                   registerPieceType(std::string const&, Factory) { GF_STUB("structure.registerPieceType"); }
std::unique_ptr<Piece> loadPiece(Json const&) {
    GF_STUB("structure.loadPiece");
    return nullptr;
}
void registerProcessor(std::string const&, ProcessorFn) { GF_STUB("structure.registerProcessor"); }
void registerMarkerHandler(std::string const&, MarkerFn) { GF_STUB("structure.registerMarkerHandler"); }

namespace templates {
Template const* get(std::string const&) {
    GF_STUB("structure.templates");
    return nullptr;
}
bool place(std::string const&, Region&, BlockPos const&, TemplateSettings const&) {
    GF_STUB("structure.templates");
    return false;
}
} // namespace templates

namespace palettes {
Block const& apply(std::string const&, Block const& block, RandomSource&) {
    GF_STUB("structure.palettes");
    return block;
}
} // namespace palettes

namespace selectors {
BlockSelector* get(std::string const&) {
    GF_STUB("structure.selectors");
    return nullptr;
}
} // namespace selectors

namespace jigsaw {
std::vector<std::unique_ptr<Piece>> assemble(std::string const&, BlockPos const&, int, RandomSource&) {
    GF_STUB("structure.jigsaw");
    return {};
}
} // namespace jigsaw

namespace speleothems {
bool place(Region&, BlockPos const&, std::string const&, RandomSource&, bool) {
    GF_STUB("structure.speleothems");
    return false;
}
} // namespace speleothems

} // namespace gf::structure

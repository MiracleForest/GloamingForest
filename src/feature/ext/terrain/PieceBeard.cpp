// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U2 (terrain): the PieceBeardifierModifier extension (feature/ext/terrain/PieceBeard.h).
#include "feature/ext/terrain/PieceBeard.h"

#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "core/terrain/Beard.h"
#include "feature/structure/Structure.h"
#include "feature/structure/bases/ComponentOld.h"
#include "feature/structure/bases/TemplatePieces.h"

namespace gf::feature::terrain {
/// Bumped by every registerPieceBeard (the beardifier's per-start cache keys on it).
std::uint64_t pieceBeardVersion();
} // namespace gf::feature::terrain

namespace gf::terrain::ext {

namespace {

namespace beard = core::terrain::beard;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Registry {
    std::shared_mutex                             mutex;
    std::unordered_map<std::string, PieceBeardFn> fns;
};
Registry& registry() {
    static Registry r;
    return r;
}
std::atomic<std::uint64_t>& version() {
    static std::atomic<std::uint64_t> v{1};
    return v;
}

char const* nameOf(structure::bases::TerrainAdjustment a) {
    switch (a) {
    case structure::bases::TerrainAdjustment::Bury:
        return "bury";
    case structure::bases::TerrainAdjustment::BeardThin:
        return "beard_thin";
    case structure::bases::TerrainAdjustment::BeardBox:
        return "beard_box";
    case structure::bases::TerrainAdjustment::Encapsulate:
        return "encapsulate";
    default:
        return "none";
    }
}
char const* nameOf(beard::TerrainAdjustment a) {
    switch (a) {
    case beard::TerrainAdjustment::Bury:
        return "bury";
    case beard::TerrainAdjustment::BeardThin:
        return "beard_thin";
    case beard::TerrainAdjustment::BeardBox:
        return "beard_box";
    case beard::TerrainAdjustment::Encapsulate:
        return "encapsulate";
    default:
        return "none";
    }
}

} // namespace

void registerPieceBeard(std::string const& typeId, PieceBeardFn fn) {
    auto&            r = registry();
    std::unique_lock lock(r.mutex);
    if (fn) r.fns[lower(typeId)] = std::move(fn);
    else r.fns.erase(lower(typeId));
    ++version();
}

PieceBeard pieceBeard(landmark::ext::StartPiece const& piece) {
    // 1. a registered answer
    if (!piece.type.empty()) {
        PieceBeardFn fn;
        {
            auto&            r = registry();
            std::shared_lock lock(r.mutex);
            if (auto it = r.fns.find(lower(piece.type)); it != r.fns.end()) fn = it->second;
        }
        if (fn) return fn(piece);
    }
    // 2. TF 4.8's piece types
    if (auto tf = beard::tfPieceBeard(piece.type, piece.box, piece.tag)) return {tf->modifier, nameOf(tf->adjustment), tf->box, tf->groundLevelDelta};
    // 3. F7's bases that implement PieceBeardifierModifier
    if (!piece.type.empty() && piece.tag.is_object() && piece.tag.contains("id")) {
        if (auto loaded = structure::loadPiece(piece.tag)) {
            if (auto const* old = dynamic_cast<structure::bases::ComponentOld const*>(loaded.get()))
                return {true, nameOf(old->getTerrainAdjustment()), old->getBeardifierBox(), old->getGroundLevelDelta()};
            if (auto const* jig = dynamic_cast<structure::bases::JigsawPiece const*>(loaded.get()))
                return {true, nameOf(jig->getTerrainAdjustment()), jig->getBeardifierBox(), jig->getGroundLevelDelta()};
        }
    }
    // 4. a plain piece
    return {false, "none", piece.box, 0};
}

} // namespace gf::terrain::ext

namespace gf::feature::terrain {
std::uint64_t pieceBeardVersion() { return gf::terrain::ext::version().load(); }
} // namespace gf::feature::terrain

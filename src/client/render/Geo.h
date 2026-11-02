#pragma once

// Bedrock geometry files (*.geo.json, format 1.12+) as textured quads, for drawGeo (F12).
// Client build only (no game types: the parser is plain C++ and is tested by the client
// selftest "ui.client_geo").
//
// Conventions (the game's entity renderer): model units are pixels (1/16 block); the model's +X is
// mirrored (Bedrock's renderer draws -x), cube / bone rotations are degrees applied Z, then Y,
// then X around their pivots, box UVs follow Bedrock's layout (up, down, west, north, east,
// south). Quads come out in blocks, around the model origin, with UVs normalised to the
// geometry's texture size.

#ifdef GF_CLIENT

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace gf::client::render::geo {

struct Quad {
    std::array<std::array<float, 3>, 4> pos; ///< blocks, model space (x already mirrored)
    std::array<std::array<float, 2>, 4> uv;  ///< 0..1
};

struct Model {
    std::string       id;
    float             textureWidth = 64, textureHeight = 64;
    std::vector<Quad> quads;
};

/// Every geometry of a parsed geo file, by identifier ("geometry.twilightforest.naga_trophy").
[[nodiscard]] std::map<std::string, Model> parse(nlohmann::json const& file, std::string* error = nullptr);

/// The geometry `id` from the resource packs of the mod (`<mod>/resource_packs/*/models/**.geo.json`),
/// loaded once (nullptr: unknown id).
[[nodiscard]] Model const* find(std::string const& id);
/// Directory scanned by `find` (the mod directory); set at load.
void setRoot(std::filesystem::path const& modDir);
/// Adds geometries that are not in a resource pack (the development fixture's, built into the mod);
/// an id already added keeps its first geometry (pointers from `find` stay valid). Returns how many
/// were added.
std::size_t add(std::map<std::string, Model> models);

} // namespace gf::client::render::geo

#endif // GF_CLIENT

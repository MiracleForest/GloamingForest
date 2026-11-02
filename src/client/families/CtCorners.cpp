// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/client/model/block/connected/ConnectedTextureModel.java
//   src/main/java/twilightforest/client/model/block/connected/ConnectionLogic.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Block families (F11), client mod: the corners of connected texture blocks the packs have no
// room for. A connected texture face is four quadrants; a quadrant whose two sides connect draws
// ConnectionLogic's "cornerless" piece when the block in their corner joins too, else the "corner"
// piece (ConnectedTextureModel.isCornerBlockPresent). Blocks with every face connected (TF's castle
// doors, ...) would need six side and twelve corner states, past Bedrock's 65536 permutations per
// block, so their packs draw cornerless wherever two sides connect (tools/families). Here the
// client mod draws the corner piece over such a quadrant when its corner block is missing: the
// patches tools/families generated (geometry.gf_families.ct_corner_<face>_<quadrant>, 0.02 px over
// the face) with the block's connected sheet, through the client.render contract's block renderers
// (F12). The list of those blocks: <mod>/data/gf/families_render/*.json {"ct_corners": {id:
// {texture, faces}}}.

#ifdef GF_CLIENT

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "nlohmann/json.hpp"

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "client/Client.h"
#include "client/render/Render.h"
#include "feature/families/Families.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"

namespace gf::client::families {

namespace {

namespace fam = gf::feature::families;

struct CtCorners {
    std::string         texture; ///< the connected sheet (resource pack path)
    std::array<bool, 6> faces{}; ///< the connected faces (FacingID)
};

std::map<std::string, CtCorners>& ctBlocks() {
    static std::map<std::string, CtCorners> m;
    return m;
}

void loadList() {
    std::error_code ec;
    auto            dir = Mod::getInstance().getSelf().getModDir() / "data" / "gf" / "families_render";
    if (!std::filesystem::is_directory(dir, ec)) return;
    for (auto const& f : std::filesystem::directory_iterator(dir, ec)) {
        if (f.path().extension() != ".json") continue;
        std::ifstream in(f.path());
        auto          doc = nlohmann::json::parse(in, nullptr, false);
        if (!doc.is_object() || !doc.contains("ct_corners") || !doc["ct_corners"].is_object()) continue;
        for (auto const& [id, v] : doc["ct_corners"].items()) {
            if (!v.is_object() || !v.contains("texture") || !v["texture"].is_string()) continue;
            CtCorners c;
            c.texture = v["texture"].get<std::string>();
            for (auto const& face : v.value("faces", nlohmann::json::array()))
                if (face.is_string())
                    if (auto d = fam::dirOf(face.get<std::string>())) c.faces[static_cast<std::size_t>(*d)] = true;
            ctBlocks()[id] = std::move(c);
        }
    }
}

/// The corner pieces of the block at `pos` (ConnectedTextureModel: ConnectionLogic.of(h, v,
/// corner) is "corner" where both sides connect and the corner block does not).
void drawCorners(render::RenderContext& ctx, BlockPos const& pos, Block const& block, float) {
    auto* player = currentPlayer();
    auto const* e = fam::entryOf(block);
    if (!player || !e) return;
    auto it = ctBlocks().find(e->id);
    if (it == ctBlocks().end()) return;
    auto& region = player->getDimensionBlockSource();
    for (int f = 0; f < 6; ++f) {
        if (!it->second.faces[static_cast<std::size_t>(f)]) continue;
        // a face against a block it joins, or against a full block, is not seen
        BlockPos front = fam::offset(pos, f);
        if (fam::ctConnects(*e, fam::blockAt(region, front)) || fam::solidBlock(region, front)) continue;
        auto plane = fam::ctPlane(fam::axisOf(f));
        for (int i = 0; i < 4; ++i) {
            int a = plane[static_cast<std::size_t>(i)], b = plane[static_cast<std::size_t>((i + 1) % 4)];
            if (!fam::flagState(block, fam::state::ctSide(a)) || !fam::flagState(block, fam::state::ctSide(b))) continue;
            if (fam::ctConnects(*e, fam::blockAt(region, fam::offset(fam::offset(pos, a), b)))) continue;
            render::Transform at;
            at.translation = Vec3{static_cast<float>(pos.x) + .5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + .5f};
            render::drawGeo(ctx, "geometry.gf_families.ct_corner_" + std::string(fam::dirName(f)) + "_" + std::to_string(i + 1), it->second.texture, at);
        }
    }
}

class CtCornersModule {
public:
    void install() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        loadList();
        for (auto const& [id, _] : ctBlocks()) render::registerBlockRenderer(id, drawCorners);
        if (!ctBlocks().empty()) log().info("families: {} connected texture blocks get their corners drawn", ctBlocks().size());
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::families

GF_REGISTER_MODULE(gf::client::families::CtCornersModule);

#endif // GF_CLIENT

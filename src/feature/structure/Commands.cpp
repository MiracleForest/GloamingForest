// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/command/TFCommand.java (structure_util branch)
//   src/main/java/twilightforest/command/DisplayPiecesCommand.java
//   src/main/java/twilightforest/command/ClearDisplayCommand.java
//   src/main/java/twilightforest/command/CountLootCommand.java
//   src/main/java/twilightforest/command/CountTemplateCommand.java
//   src/main/java/twilightforest/command/StructureDistanceCommand.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// /twilightforest structure_util (operators):
//   display_pieces <structure>       outlines the start and its pieces around you (TF: block
//                                    displays; Bedrock: particle outlines for a minute, piece list in chat)
//   clear_display                    stops the outlines
//   count_loot <structure> [common]  counts the items in the start's containers
//   count_template <structure>       counts the templates of the start's pieces
//   generator_radius <structure>     outlines the chunks 0..8 around the start's chunk
// GloamingForest development additions:
//   place <template> [rotation] [mirror]  places a template at your feet
//   capture <id> <x1> <y1> <z1> <x2> <y2> <z2>  saves a box as a .gft.json template (original builds)
//   jigsaw <pool> [depth]            assembles a template pool at your feet
//   templates                        lists the known templates, pools and data counts
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/Keys.h"
#include "feature/inventory/Inventory.h"
#include "feature/landmark/Landmark.h"
#include "feature/structure/Region.h"
#include "feature/structure/bases/BasesInternal.h"
#include "game/Blocks.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::structure {

// Command parameter types need external linkage.
struct StructureUtilKeyParams {
    std::string filter_structure;
};
struct StructureUtilLootParams {
    std::string filter_structure;
    bool        show_common = true;
};
struct StructureUtilPlaceParams {
    std::string template_id;
    int         rotation = 0;
    int         mirror   = 0;
};
struct StructureUtilJigsawParams {
    std::string pool;
    int         depth = 7;
};
struct StructureUtilCaptureParams {
    std::string id;
    int         x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
};

namespace {

/// Outlines drawn with particles until cleared or expired (TF's block displays).
struct Outline {
    int         dim;
    BlockBox    box;
    std::string particle;
    int         ticksLeft;
};
std::vector<Outline>    gOutlines;
ll::event::ListenerPtr  gOutlineTick;
int                     gTickCounter = 0;

void drawOutlines() {
    if (gOutlines.empty() || (++gTickCounter % 10) != 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto& o : gOutlines) {
        o.ticksLeft -= 10;
        auto dim = level->getDimension(o.dim).lock();
        if (!dim) continue;
        auto const& b = o.box;
        // the twelve edges, one particle every two blocks
        auto edge = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
            float len   = std::max({std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0)});
            int   steps = std::max(1, static_cast<int>(len / 2));
            for (int i = 0; i <= steps; i++) {
                float t = static_cast<float>(i) / static_cast<float>(steps);
                level->spawnParticleEffect(o.particle, Vec3{x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z0 + (z1 - z0) * t}, dim.get());
            }
        };
        auto  x0 = static_cast<float>(b.minX), y0 = static_cast<float>(b.minY), z0 = static_cast<float>(b.minZ);
        auto  x1 = static_cast<float>(b.maxX + 1), y1 = static_cast<float>(b.maxY + 1), z1 = static_cast<float>(b.maxZ + 1);
        edge(x0, y0, z0, x1, y0, z0);
        edge(x0, y0, z1, x1, y0, z1);
        edge(x0, y1, z0, x1, y1, z0);
        edge(x0, y1, z1, x1, y1, z1);
        edge(x0, y0, z0, x0, y1, z0);
        edge(x1, y0, z0, x1, y1, z0);
        edge(x0, y0, z1, x0, y1, z1);
        edge(x1, y0, z1, x1, y1, z1);
        edge(x0, y0, z0, x0, y0, z1);
        edge(x1, y0, z0, x1, y0, z1);
        edge(x0, y1, z0, x0, y1, z1);
        edge(x1, y1, z0, x1, y1, z1);
    }
    std::erase_if(gOutlines, [](Outline const& o) { return o.ticksLeft <= 0; });
    if (gOutlines.empty() && gOutlineTick) {
        ll::event::EventBus::getInstance().removeListener(gOutlineTick);
        gOutlineTick.reset();
    }
}

void addOutline(int dim, BlockBox const& box, std::string particle) {
    gOutlines.push_back(Outline{dim, box, std::move(particle), 60 * 20});
    if (!gOutlineTick)
        gOutlineTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { drawOutlines(); });
}

struct Where {
    Player*      player = nullptr;
    BlockSource* source = nullptr;
    BlockPos     pos{0, 0, 0};
    int          dim = 0;
};

std::optional<Where> where(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) {
        output.error("You need operator permission.");
        return std::nullopt;
    }
    auto* player = commands::playerOf(origin);
    if (!player) {
        output.error(tr("gf.structure.need_player", "Run this as a player."));
        return std::nullopt;
    }
    Where w;
    w.player = player;
    w.source = &player->getDimensionBlockSource();
    w.pos    = game::blockPosOf(player->getFeetPos());
    w.pos.y  = std::max<int>(w.pos.y, w.source->getMinHeight());
    w.dim    = static_cast<int>(w.source->getDimensionId());
    return w;
}

/// The landmark start `key` at the player (StructureManager#getStructureAt).
std::optional<landmark::Start> startAt(Where const& w, std::string const& key, CommandOutput& output) {
    auto start = landmark::startAt(w.dim, w.pos);
    if (!start || (start->key != key && !key.empty())) {
        output.error(tr("commands.tffeature.structure.outside", "You are not in a structure."));
        return std::nullopt;
    }
    return start;
}

/// The saved pieces of a start ({"pieces": [Piece#save]} in the start's data), or its boxes.
std::vector<Json> piecesOf(landmark::Start const& start) {
    std::vector<Json> out;
    if (start.data.is_object() && start.data.contains("pieces") && start.data["pieces"].is_array())
        for (auto const& p : start.data["pieces"]) out.push_back(p);
    if (out.empty())
        for (auto const& b : start.pieceBoxes) out.push_back(Json{{"BB", boxToJson(b)}});
    return out;
}

void displayPieces(CommandOrigin const& origin, CommandOutput& output, std::string const& key) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    int successes = 1;
    addOutline(w->dim, start->box, "minecraft:redstone_ore_dust_particle");
    auto pieces = piecesOf(*start);
    for (auto const& p : pieces) {
        BlockBox box = boxFromJson(p.value("BB", Json::array()));
        std::string id = p.value("id", std::string{});
        bool protectedPiece = !id.empty() && start->generated; // ProgressionPiece#isComponentProtected defaults to true
        addOutline(w->dim, box, protectedPiece ? "minecraft:villager_happy" : "minecraft:water_wake_particle");
        w->player->sendMessage((id.empty() ? tr("commands.tffeature.display_pieces.missing_key", "Missing Piece Key") : id) + " " + boxToJson(box).dump());
        successes++;
    }
    log().info("structure_util display_pieces {}: {} outlines", start->key, successes);
    output.success(std::to_string(successes) + " outlines");
}

void clearDisplay(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    gOutlines.clear();
    output.success("cleared");
}

void countLoot(CommandOrigin const& origin, CommandOutput& output, std::string const& key, bool showCommon) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    std::map<std::string, int> counts;
    std::vector<BlockBox>      boxes;
    for (auto const& p : piecesOf(*start)) boxes.push_back(boxFromJson(p.value("BB", Json::array())));
    for (auto const& box : boxes) {
        for (int x = box.minX; x <= box.maxX; x++)
            for (int z = box.minZ; z <= box.maxZ; z++) {
                if (!w->source->hasChunksAt(BlockPos{x, box.minY, z}, 0, false)) continue;
                for (int y = box.minY; y <= box.maxY; y++) {
                    BlockPos pos{x, y, z};
                    auto     handler = inventory::find(*w->source, pos);
                    if (!handler || !handler->get) continue;
                    auto gpos = game::globalPos(*w->source, pos);
                    for (int slot = 0; slot < handler->slots; slot++) {
                        ItemStack stack = handler->get(gpos, slot);
                        if (stack.isNull()) continue;
                        counts[stack.getTypeName()] += stack.mCount;
                    }
                }
            }
    }
    (void)showCommon; // item rarity is the item contract's; every item is listed
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    int total = 0;
    for (auto const& [item, n] : sorted) {
        w->player->sendMessage(item + ": " + std::to_string(n));
        total += n;
    }
    log().info("structure_util count_loot {}: {} items", start->key, total);
    output.success(std::to_string(total));
}

void countTemplate(CommandOrigin const& origin, CommandOutput& output, std::string const& key) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    std::map<std::string, int> counts;
    for (auto const& p : piecesOf(*start))
        if (p.contains("Template")) counts[p["Template"].get<std::string>()]++;
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    for (auto const& [t, n] : sorted) w->player->sendMessage(t + "    " + std::to_string(n));
    log().info("structure_util count_template {}: {} templates", start->key, counts.size());
    output.success(std::to_string(counts.size()));
}

void generatorRadius(CommandOrigin const& origin, CommandOutput& output, std::string const& key) {
    auto w = where(origin, output);
    if (!w) return;
    auto start = startAt(*w, key, output);
    if (!start) return;
    int sectionY = core::chunkOf(w->pos.y - 16) << 4;
    int cx = core::chunkOf(start->center.x), cz = core::chunkOf(start->center.z);
    static char const* particles[] = {"minecraft:basic_flame_particle", "minecraft:redstone_ore_dust_particle", "minecraft:lava_particle",
                                      "minecraft:villager_happy",       "minecraft:villager_happy",              "minecraft:water_wake_particle",
                                      "minecraft:portal_directional",   "minecraft:heart_particle",               "minecraft:heart_particle"};
    for (int dx = -8; dx <= 8; dx++)
        for (int dz = -8; dz <= 8; dz++) {
            int      r = std::max(std::abs(dx), std::abs(dz));
            BlockBox box{(cx + dx) << 4, sectionY, (cz + dz) << 4, ((cx + dx) << 4) + 15, sectionY + 15, ((cz + dz) << 4) + 15};
            if (r == 0 || r == 8 || dx == 0 || dz == 0) addOutline(w->dim, box, particles[r]);
        }
    w->player->sendMessage(tr("commands.tffeature.generator_radius.center_chunk", "Center Chunk") + ": " + std::to_string(cx) + ", " + std::to_string(cz));
    output.success("ok");
}

void placeTemplate(CommandOrigin const& origin, CommandOutput& output, StructureUtilPlaceParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    if (!templates::parsed(p.template_id)) return output.error("unknown template " + p.template_id);
    BlockSourceRegion region(*w->source, w->pos);
    TemplateSettings  settings;
    settings.rotation = rotationFromIndex(p.rotation);
    settings.mirror   = static_cast<Mirror>(std::clamp(p.mirror, 0, 2));
    bool ok           = templates::place(p.template_id, region, w->pos, settings);
    log().info("structure_util place {} at {} {} {}: {}", p.template_id, w->pos.x, w->pos.y, w->pos.z, ok ? "placed" : "failed");
    if (ok) output.success("placed " + p.template_id);
    else output.error("could not place " + p.template_id);
}

void capture(CommandOrigin const& origin, CommandOutput& output, StructureUtilCaptureParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    BlockBox box = BlockBox::of(p.x1, p.y1, p.z1, p.x2, p.y2, p.z2);
    if (box.volume() > 4'000'000) return output.error("box too large");
    // palette of Java states: Bedrock blocks are written by their Bedrock id and states (an
    // original build captured in game; tools/structures reads both)
    std::map<std::string, int> paletteIndex;
    Json                       palette = Json::array();
    std::vector<int>           cells;
    for (int y = box.minY; y <= box.maxY; y++)
        for (int z = box.minZ; z <= box.maxZ; z++)
            for (int x = box.minX; x <= box.maxX; x++) {
                auto const& b     = w->source->getBlock(BlockPos{x, y, z});
                Json        entry = Json{{"Name", b.getTypeName()}};
                auto        st    = game::statesOf(b);
                if (!st.empty()) entry["Properties"] = st;
                auto key = entry.dump();
                auto it  = paletteIndex.find(key);
                if (it == paletteIndex.end()) {
                    it = paletteIndex.emplace(key, static_cast<int>(palette.size())).first;
                    palette.push_back(entry);
                }
                cells.push_back(it->second);
            }
    Json blocksRle = Json::array();
    for (std::size_t i = 0; i < cells.size();) {
        std::size_t j = i;
        while (j < cells.size() && cells[j] == cells[i]) j++;
        blocksRle.push_back(Json::array({cells[i], static_cast<int>(j - i)}));
        i = j;
    }
    Json doc{{"format", "gloamingforest:template"},
             {"format_version", 1},
             {"id", p.id},
             {"size", Json::array({box.maxX - box.minX + 1, box.maxY - box.minY + 1, box.maxZ - box.minZ + 1})},
             {"palette", palette},
             {"blocks", blocksRle}};
    std::string error;
    if (!templates::add(doc, &error)) return output.error("invalid capture: " + error);
    auto colon = p.id.find(':');
    auto file  = Mod::getInstance().getSelf().getModDir() / "structures" / "captured" /
                (p.id.substr(0, colon) + "_" + p.id.substr(colon + 1) + ".gft.json");
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream(file) << doc.dump(1);
    log().info("structure_util capture {} -> {}", p.id, file.string());
    output.success("captured " + p.id);
}

void jigsawCmd(CommandOrigin const& origin, CommandOutput& output, StructureUtilJigsawParams const& p) {
    auto w = where(origin, output);
    if (!w) return;
    JavaRandom random(pure::mthGetSeed(w->pos.x, w->pos.y, w->pos.z));
    auto       pieces = jigsaw::assemble(p.pool, w->pos, p.depth, random);
    if (pieces.empty()) return output.error("pool " + p.pool + " produced no pieces");
    BlockSourceRegion region(*w->source, w->pos);
    for (auto& piece : pieces) {
        JavaRandom pr(pure::mthGetSeed(piece->bbox.minX, piece->bbox.minY, piece->bbox.minZ));
        piece->postProcess(region, pr, piece->bbox, w->pos);
    }
    log().info("structure_util jigsaw {}: {} pieces", p.pool, pieces.size());
    output.success(std::to_string(pieces.size()) + " pieces");
}

void listTemplates(CommandOrigin const& origin, CommandOutput& output) {
    if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
    auto ids = templates::ids();
    log().info("structure kit: {} templates, {} template definitions, {} wood palettes, {} speleothem configs, {} processors, {} markers, {} piece types",
               ids.size(), pools::definitionCount(), palettes::ids().size(), speleothems::ids().size(), processorTypes().size(), markerTypes().size(),
               pieceTypes().size());
    output.success(std::to_string(ids.size()) + " templates");
}

} // namespace

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("display_pieces").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { displayPieces(o, out, p.filter_structure); }
    );
    cmd.overload().text("structure_util").text("clear_display").execute([](CommandOrigin const& o, CommandOutput& out) { clearDisplay(o, out); });
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("count_loot").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { countLoot(o, out, p.filter_structure, true); }
    );
    cmd.overload<StructureUtilLootParams>().text("structure_util").text("count_loot").required("filter_structure").required("show_common").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilLootParams const& p) { countLoot(o, out, p.filter_structure, p.show_common); }
    );
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("count_template").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { countTemplate(o, out, p.filter_structure); }
    );
    cmd.overload<StructureUtilKeyParams>().text("structure_util").text("generator_radius").required("filter_structure").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilKeyParams const& p) { generatorRadius(o, out, p.filter_structure); }
    );
    cmd.overload<StructureUtilPlaceParams>().text("structure_util").text("place").required("template_id").optional("rotation").optional("mirror").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilPlaceParams const& p) { placeTemplate(o, out, p); }
    );
    cmd.overload<StructureUtilCaptureParams>()
        .text("structure_util")
        .text("capture")
        .required("id")
        .required("x1")
        .required("y1")
        .required("z1")
        .required("x2")
        .required("y2")
        .required("z2")
        .execute([](CommandOrigin const& o, CommandOutput& out, StructureUtilCaptureParams const& p) { capture(o, out, p); });
    cmd.overload<StructureUtilJigsawParams>().text("structure_util").text("jigsaw").required("pool").optional("depth").execute(
        [](CommandOrigin const& o, CommandOutput& out, StructureUtilJigsawParams const& p) { jigsawCmd(o, out, p); }
    );
    cmd.overload().text("structure_util").text("templates").execute([](CommandOrigin const& o, CommandOutput& out) { listTemplates(o, out); });
});

} // namespace gf::structure

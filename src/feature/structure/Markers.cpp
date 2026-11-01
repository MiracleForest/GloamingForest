// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/markerhandler/TemplateMarkerHandler.java
//   src/main/java/twilightforest/world/components/structures/markerhandler/TemplateMarkerHandlerType.java
//   src/main/java/twilightforest/world/components/structures/markerhandler/BlockPlaceMarkerHandler.java
//   src/main/java/twilightforest/world/components/structures/markerhandler/DryingRackMarkerHandler.java
//   src/main/java/twilightforest/world/components/structures/markerhandler/LootMarkerHandler.java
//   src/main/java/twilightforest/world/components/structures/markerhandler/PaintingMarkerHandler.java
//   src/main/java/twilightforest/world/components/structures/markerhandler/RotationMarkerHandler.java
//   src/main/java/twilightforest/world/components/structures/markerhandler/SwitchMarkerHandler.java
//   src/main/java/twilightforest/world/components/structures/util/TemplateMarkerHandlerList.java
//   src/main/java/twilightforest/init/custom/TemplateMarkerHandlers.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichBossRoom.java (placePainting)
//   src/main/java/twilightforest/util/EntityUtil.java (getPaintingsOfSizeOrSmaller)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Two kinds of markers:
//  - GloamingForest block entity markers of a .gft template ("loot", "spawner", "sign",
//    "block_entity"), registered through the contract's registerMarkerHandler and applied by
//    every template placement;
//  - TF's TemplateMarkerHandler types (block_placement, drying_rack, loot, painting, rotation,
//    handler_switch) configured by twilight/template_marker_handler_list JSON and run on a
//    template's TF data markers by TwilightJigsawPiece#handleDataMarker.
#include "feature/structure/Internal.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>

#include "mc/world/level/block/Block.h"

#include "mod/Log.h"

namespace gf::structure::markers {

namespace {

std::shared_mutex           gMutex;
std::map<std::string, Json> gLists;

std::vector<std::string> split(std::string const& s, char delim, int limit = -1) {
    // Java String#split(regex, limit) for a literal one-character pattern
    std::vector<std::string> out;
    std::size_t              start = 0;
    while (limit < 0 || static_cast<int>(out.size()) < limit - 1) {
        auto pos = s.find(delim, start);
        if (pos == std::string::npos) break;
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    out.push_back(s.substr(start));
    if (limit < 0)
        while (out.size() > 1 && out.back().empty()) out.pop_back(); // Java drops trailing empties
    return out;
}

bool isNumeric(std::string const& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }); }

/// minecraft:placeable painting variants of 1.21.1 (id, width, height).
struct Variant {
    char const* id;
    int         w, h;
};
std::vector<Variant> const& placeablePaintings() {
    static std::vector<Variant> const v{
        {"kebab", 1, 1},          {"aztec", 1, 1},       {"alban", 1, 1},       {"aztec2", 1, 1},     {"bomb", 1, 1},       {"plant", 1, 1},
        {"wasteland", 1, 1},      {"meditative", 1, 1},  {"pool", 2, 1},        {"courbet", 2, 1},    {"sea", 2, 1},        {"sunset", 2, 1},
        {"creebet", 2, 1},        {"wanderer", 1, 2},    {"graham", 1, 2},      {"prairie_ride", 1, 2}, {"match", 2, 2},    {"bust", 2, 2},
        {"stage", 2, 2},          {"void", 2, 2},        {"skull_and_roses", 2, 2}, {"wither", 2, 2}, {"baroque", 2, 2},    {"humble", 2, 2},
        {"bouquet", 3, 3},        {"cavebird", 3, 3},    {"cotan", 3, 3},       {"endboss", 3, 3},    {"fern", 3, 3},       {"owlemons", 3, 3},
        {"sunflowers", 3, 3},     {"tides", 3, 3},       {"backyard", 3, 4},    {"pond", 3, 4},       {"fighters", 4, 2},   {"changing", 4, 2},
        {"finding", 4, 2},        {"lowmist", 4, 2},     {"passage", 4, 2},     {"skeleton", 4, 3},   {"donkey_kong", 4, 3}, {"pointer", 4, 4},
        {"pigscene", 4, 4},       {"burning_skull", 4, 4}, {"orb", 4, 4},       {"unpacked", 4, 4},
    };
    return v;
}

} // namespace

int parseRange(std::string const& label, RandomSource& random, int defaultMin, int defaultMax) {
    auto params = split(label, '-');
    if (params.size() == 1 && isNumeric(params[0])) return std::stoi(params[0]);
    if (params.size() == 2 && isNumeric(params[0]) && isNumeric(params[1])) return random.nextIntBetweenInclusive(std::stoi(params[0]), std::stoi(params[1]));
    return random.nextIntBetweenInclusive(defaultMin, defaultMax);
}

Block const* provide(Json const& provider, RandomSource& random, BlockPos const& pos) {
    auto type = provider.value("type", std::string("minecraft:simple_state_provider"));
    if (type == "minecraft:simple_state_provider") return blocks::fromJavaState(provider["state"]);
    if (type == "minecraft:weighted_state_provider") {
        // SimpleWeightedRandomList#getRandomValue: nextInt(total weight)
        auto const& entries = provider["entries"];
        int         total   = 0;
        for (auto const& e : entries) total += e.value("weight", 1);
        if (total <= 0) return nullptr;
        int pick = random.nextInt(total);
        for (auto const& e : entries) {
            pick -= e.value("weight", 1);
            if (pick < 0) return blocks::fromJavaState(e["data"]);
        }
        return nullptr;
    }
    if (type == "minecraft:rotated_block_provider") {
        auto const* b = blocks::fromJavaState(provider["state"]);
        if (!b) return nullptr;
        // RotatedBlockProvider: random axis
        static char const* axes[] = {"x", "y", "z"};
        return &blocks::withJava(*b, Json{{"axis", axes[random.nextInt(3)]}});
    }
    warnOnce("provider:" + type, "structure: unsupported block state provider " + type);
    (void)pos;
    return nullptr;
}

bool handle(Json const& handler, std::string const& label, BlockPos const& pos, Region& region, RandomSource& random, BlockBox const& chunkBounds,
            Rotation rotation) {
    auto type = handler.value("type", std::string{});
    if (type == "twilightforest:block_placement") {
        auto const* b = provide(handler["block_state"], random, pos);
        if (!b) return false;
        return region.setBlock(pos, blocks::rotate(*b, rotation), 3);
    }
    if (type == "twilightforest:drying_rack") {
        auto const* b = provide(handler["drying_rack_block"], random, pos);
        if (!b || !blocks::id(*b).ends_with("_drying_rack")) return false;
        if (!region.setBlock(pos, blocks::rotate(*b, rotation), 3)) return false;
        // DryingRackBlockEntity#fillFromLootTable: the rack's storage is gf::inventory's
        blockentities::lootTable(region, pos, handler.value("loot_table", std::string{}), random.nextLong());
        return true;
    }
    if (type == "twilightforest:loot") {
        auto const* b = provide(handler["loot_block"], random, pos);
        if (!b) return false;
        if (!region.setBlock(pos, blocks::rotate(*b, rotation), 3)) return false;
        // RandomizableContainer.setBlockEntityLootTable(level, random, pos, table): seed random.nextLong()
        blockentities::lootTable(region, pos, handler.value("loot_table", std::string{}), random.nextLong());
        return true;
    }
    if (type == "twilightforest:painting") {
        // LichBossRoom.placePainting(pos, random, 1, paintings, level, 1, 1, rotation.rotate(NORTH))
        Orientation dir = rotateDir(Orientation::North, rotation);
        int         width = 1, height = 1, maxArea = width * height, limitTries = 1;
        std::vector<Variant> fitting;
        for (auto const& v : placeablePaintings())
            if (v.w <= width && v.h <= height) fitting.push_back(v);
        // Util.shuffle (Collections-style: for i = size; i > 1; i--: swap(i - 1, nextInt(i)))
        for (int i = static_cast<int>(fitting.size()); i > 1; i--) std::swap(fitting[static_cast<std::size_t>(i - 1)], fitting[static_cast<std::size_t>(random.nextInt(i))]);
        for (auto const& v : fitting) {
            int area = v.w * v.h;
            if (random.nextInt(maxArea) <= area) {
                Json data{{"motive", v.id}, {"direction", blocks::directionName(dir)}};
                // EntityUtil.tryHangPainting(...) || limitTries-- <= 0
                if (blockentities::entity(region, pos.x + 0.5, pos.y + 0.5, pos.z + 0.5, "minecraft:painting", data) || limitTries-- <= 0) return true;
            }
        }
        return false;
    }
    if (type == "twilightforest:rotation") {
        std::string delimiter = handler.value("delimiter", std::string("@"));
        auto        parts     = split(label, delimiter.empty() ? '@' : delimiter[0], 2);
        Rotation    dataRotation =
            parts.size() == 1 ? Rotation::Clockwise180
                              : relativeRotation(Orientation::North, blocks::parseDirection(parts[1]).value_or(Orientation::South));
        auto const& rotated = handler["rotated"];
        if (rotated.is_string()) {
            // a registry reference to a template_marker_handler; not shipped by TF
            warnOnce("marker-ref:" + rotated.get<std::string>(), "structure: unknown template marker handler " + rotated.get<std::string>());
            return false;
        }
        return handle(rotated, parts[0], pos, region, random, chunkBounds, addRotation(rotation, dataRotation));
    }
    if (type == "twilightforest:handler_switch") {
        std::string g      = handler.value("group_character_delimiter", std::string("|"));
        std::string e      = handler.value("entry_character_delimiter", std::string(":"));
        auto        groups = split(label, g.empty() ? '|' : g[0]);
        if (groups.empty()) return false;
        for (std::size_t i = 0; i < groups.size(); i++) {
            int newIndex = random.nextInt(static_cast<int>(groups.size()));
            groups[i]    = groups[static_cast<std::size_t>(newIndex)];
        }
        auto const& handlers = handler.value("element_processor", Json::object());
        for (auto const& group : groups) {
            if (!handlers.contains(group)) continue;
            auto        entrySplit = split(group, e.empty() ? ':' : e[0], 2);
            std::string substring  = entrySplit.size() < 2 ? "" : entrySplit[1];
            if (handle(handlers[group], substring, pos, region, random, chunkBounds, rotation)) return true;
        }
        return false;
    }
    warnOnce("markertype:" + type, "structure: unknown template marker handler type " + type);
    return false;
}

Json const* list(std::string const& id) {
    {
        std::shared_lock lock(gMutex);
        if (auto it = gLists.find(id); it != gLists.end()) return &it->second;
    }
    auto doc = data::load("twilight/template_marker_handler_list", id);
    if (!doc) return nullptr;
    std::unique_lock lock(gMutex);
    return &(gLists[id] = *doc);
}

void addList(std::string const& id, Json const& l) {
    std::unique_lock lock(gMutex);
    gLists[id] = l;
}

void registerBuiltins() {
    // ---- block entity markers of .gft templates --------------------------------------------------
    registerMarkerHandler("loot", [](Region& region, BlockPos const& pos, std::string const&, Json const& data, RandomSource& random) {
        auto seed = data.contains("seed") ? data["seed"].get<std::int64_t>() : random.nextLong();
        blockentities::lootTable(region, pos, data.value("table", std::string{}), seed);
    });
    registerMarkerHandler("spawner", [](Region& region, BlockPos const& pos, std::string const&, Json const& data, RandomSource&) {
        blockentities::spawner(region, pos, data.value("entity", std::string{}), data);
    });
    registerMarkerHandler("sign", [](Region& region, BlockPos const& pos, std::string const&, Json const& data, RandomSource&) {
        std::vector<std::string> lines;
        for (auto const& l : data.value("lines", Json::array())) lines.push_back(l.is_string() ? l.get<std::string>() : l.dump());
        blockentities::sign(region, pos, lines);
    });
    registerMarkerHandler("block_entity", [](Region& region, BlockPos const& pos, std::string const&, Json const& data, RandomSource& random) {
        // generic: a loot table or spawner entity in vanilla block entity field names
        if (data.contains("LootTable")) blockentities::lootTable(region, pos, data["LootTable"].get<std::string>(), data.value("LootTableSeed", random.nextLong()));
        if (data.contains("SpawnData") || data.contains("entity")) {
            std::string id = data.value("entity", std::string{});
            if (id.empty() && data["SpawnData"].is_object()) id = data["SpawnData"].value("entity", Json::object()).value("id", std::string{});
            blockentities::spawner(region, pos, id, data);
        }
    });
}

} // namespace gf::structure::markers

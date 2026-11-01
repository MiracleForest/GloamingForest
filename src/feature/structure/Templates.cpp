// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Contract "structure" / "template": GloamingForest templates (rule R19). TF's .nbt templates are
// All Rights Reserved and never read; every template is an original .gft.json with the id of the
// TF template it stands in for. Placement follows vanilla StructureTemplate#placeInWorld:
// processors see every block (original and current info), the result is mirrored and rotated
// about the pivot, clipped to the settings' box, then block entity markers and entities follow.
//
// Format (format "gloamingforest:template", format_version 1):
//   id        "twilightforest:lich_tower/root/foyer"
//   size      [x, y, z]
//   palette   [{"Name": java id, "Properties": {java property: value}}, ...]
//             minecraft:structure_void entries are not placed (vanilla templates omit them)
//   blocks    run-length [[palette index, count], ...] over y, then z, then x (x fastest)
//   markers   [{"pos": [x, y, z], "marker": name, "data": {...}}]
//             "data": a TF data marker, label in data.metadata (TwilightTemplateStructurePiece
//             #handleDataMarker); any other name is block entity data of the block at pos, run
//             after placement by the handler registered under that name ("loot" {table},
//             "spawner" {entity, Delay, ...}, "sign" {lines}, "block_entity" {...}).
//   jigsaw    [{"pos": [x, y, z], "front": dir, "top": dir (or "facing": dir), "pool", "name",
//               "target", "priority"}] (JigsawBlock orientation + nbt; FrontAndTop)
//   entities  [{"pos": [x, y, z] (doubles), "id": entity id, "data": {...}}]
#include "feature/structure/Internal.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <tuple>

#include "mc/world/level/block/Block.h"

#include "mod/Log.h"
#include "mod/Main.h"

namespace gf::structure {

namespace templates {

namespace {

namespace fs = std::filesystem;

struct Store {
    std::shared_mutex                                    mutex;
    std::map<std::string, fs::path>                      files;     ///< id -> file (not parsed yet)
    std::map<std::string, std::unique_ptr<Template>>     raw;
    std::map<std::string, std::unique_ptr<Parsed>>       parsedById;
    std::set<std::string>                                invalid;
    /// replaced parsed templates stay alive: generation threads may still be placing them
    std::vector<std::unique_ptr<Parsed>>                 retired;
    std::vector<std::unique_ptr<Template>>               retiredRaw;
    bool                                                 scanned = false;
};
Store& store() {
    static Store s;
    return s;
}

std::optional<BlockPos> pos3(Json const& j) {
    if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) return std::nullopt;
    return BlockPos{j[0].get<int>(), j[1].get<int>(), j[2].get<int>()};
}

bool isDirection(std::string const& s) {
    return s == "north" || s == "south" || s == "east" || s == "west" || s == "up" || s == "down";
}

std::unique_ptr<Parsed> parse(Template const& t) {
    auto        p   = std::make_unique<Parsed>();
    Json const& doc = t.data;
    p->base         = &t;
    p->sx           = t.sizeX;
    p->sy           = t.sizeY;
    p->sz           = t.sizeZ;
    for (auto const& entry : doc["palette"]) {
        p->paletteJson.push_back(entry);
        std::string name = entry.value("Name", std::string{});
        if (name == "minecraft:structure_void") p->palette.push_back(nullptr);
        else p->palette.push_back(blocks::fromJavaState(entry));
        // Java's waterlogged property (Bedrock: water in the liquid layer) and potted plants
        // (Bedrock: one flower pot block whose block actor holds the plant)
        auto const& props = entry.value("Properties", Json::object());
        std::int8_t wl    = -1;
        if (props.is_object() && props.contains("waterlogged")) {
            auto const& v = props["waterlogged"];
            wl            = (v.is_boolean() ? v.get<bool>() : v.is_string() && v.get<std::string>() == "true") ? 1 : 0;
        }
        p->paletteWaterlogged.push_back(wl);
        p->pottedPlant.push_back(blocks::pottedPlant(name));
    }
    p->cells.reserve(static_cast<std::size_t>(p->sx) * p->sy * p->sz);
    for (auto const& run : doc["blocks"]) {
        auto idx   = static_cast<std::uint16_t>(run[0].get<int>());
        int  count = run[1].get<int>();
        p->cells.insert(p->cells.end(), static_cast<std::size_t>(count), idx);
    }
    // structure_void cells become 0xffff (skipped)
    for (auto& c : p->cells)
        if (c < p->paletteJson.size() && p->paletteJson[c].value("Name", std::string{}) == "minecraft:structure_void") c = 0xffff;
    for (auto const& m : doc.value("markers", Json::array())) {
        Marker mk;
        mk.pos    = *pos3(m["pos"]);
        mk.marker = m.value("marker", std::string{});
        mk.data   = m.value("data", Json::object());
        p->markers.push_back(std::move(mk));
    }
    // a template's spawners always carry block entity data (vanilla saves it with the block), so
    // spawner processors find it even when the template gives no "spawner" marker
    {
        std::set<std::tuple<int, int, int>> withData;
        for (auto const& m : p->markers)
            if (!m.isData()) withData.insert({m.pos.x, m.pos.y, m.pos.z});
        for (int y = 0; y < p->sy; y++)
            for (int z = 0; z < p->sz; z++)
                for (int x = 0; x < p->sx; x++) {
                    auto idx = p->cells[static_cast<std::size_t>(p->index(x, y, z))];
                    if (idx >= p->paletteJson.size()) continue;
                    auto name = p->paletteJson[idx].value("Name", std::string{});
                    if ((name == "minecraft:spawner" || name == "twilightforest:sinister_spawner") && !withData.contains({x, y, z}))
                        p->markers.push_back(Marker{BlockPos{x, y, z}, "spawner", Json::object()});
                }
    }
    for (auto const& j : doc.value("jigsaw", Json::array())) {
        Connector c;
        c.pos      = *pos3(j["pos"]);
        c.front    = j.value("front", j.value("facing", std::string("north")));
        c.top      = j.value("top", std::string(c.front == "up" || c.front == "down" ? "north" : "up"));
        c.pool     = j.value("pool", std::string("minecraft:empty"));
        c.name     = j.value("name", std::string{});
        c.target   = j.value("target", std::string{});
        c.priority = j.value("priority", j.value("selection_priority", 0));
        p->connectors.push_back(std::move(c));
    }
    for (auto const& e : doc.value("entities", Json::array())) {
        EntityEntry en;
        auto const& pos = e["pos"];
        en.x            = pos[0].get<double>();
        en.y            = pos[1].get<double>();
        en.z            = pos[2].get<double>();
        en.id           = e.value("id", std::string{});
        en.data         = e.value("data", Json::object());
        p->entities.push_back(std::move(en));
    }
    return p;
}

std::unique_ptr<Template> makeTemplate(Json doc) {
    auto t   = std::make_unique<Template>();
    t->id    = doc.value("id", std::string{});
    t->sizeX = doc["size"][0].get<int>();
    t->sizeY = doc["size"][1].get<int>();
    t->sizeZ = doc["size"][2].get<int>();
    t->data  = std::move(doc);
    return t;
}

void scanLocked(Store& s) {
    s.files.clear();
    auto modDir = Mod::getInstance().getSelf().getModDir();
    for (auto const& root : {modDir / "structures", modDir / "data" / "gf" / "structure" / "templates"}) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file()) continue;
            auto name = it->path().filename().string();
            if (!name.ends_with(".gft.json")) continue;
            try {
                std::ifstream in(it->path());
                Json          head = Json::parse(in);
                auto          id   = head.value("id", std::string{});
                if (id.empty()) {
                    warnOnce("template-noid:" + it->path().string(), "structure: template without id: " + it->path().string());
                    continue;
                }
                s.files[id] = it->path();
            } catch (std::exception const& e) {
                warnOnce("template-bad:" + it->path().string(), "structure: unreadable template " + it->path().string() + ": " + e.what());
            }
        }
    }
    s.scanned = true;
}

} // namespace

std::string Marker::label() const {
    if (data.is_object() && data.contains("metadata") && data["metadata"].is_string()) return data["metadata"].get<std::string>();
    return marker;
}

std::vector<std::string> validate(Json const& doc) {
    std::vector<std::string> errors;
    auto                     err = [&](std::string e) { errors.push_back(std::move(e)); };
    if (!doc.is_object()) {
        err("not a JSON object");
        return errors;
    }
    if (doc.value("format", std::string{}) != "gloamingforest:template") err("format must be \"gloamingforest:template\"");
    if (doc.value("format_version", 0) != 1) err("format_version must be 1");
    auto id = doc.value("id", std::string{});
    if (id.find(':') == std::string::npos) err("id must be a namespaced id");
    auto const& size = doc.contains("size") ? doc["size"] : Json();
    auto        sz   = pos3(size);
    if (!sz || sz->x <= 0 || sz->y <= 0 || sz->z <= 0) {
        err("size must be three positive integers");
        return errors;
    }
    if (!doc.contains("palette") || !doc["palette"].is_array() || doc["palette"].empty()) err("palette must be a non-empty array");
    else
        for (auto const& p : doc["palette"])
            if (!p.is_object() || !p.contains("Name") || !p["Name"].is_string()) err("palette entries need a Name");
    long long volume = static_cast<long long>(sz->x) * sz->y * sz->z;
    long long total  = 0;
    auto      pcount = doc.contains("palette") && doc["palette"].is_array() ? doc["palette"].size() : 0;
    if (!doc.contains("blocks") || !doc["blocks"].is_array()) err("blocks must be an array of [palette index, count] runs");
    else
        for (auto const& run : doc["blocks"]) {
            if (!run.is_array() || run.size() != 2 || !run[0].is_number_integer() || !run[1].is_number_integer() || run[1].get<long long>() <= 0) {
                err("bad block run " + run.dump());
                continue;
            }
            if (run[0].get<long long>() < 0 || static_cast<std::size_t>(run[0].get<long long>()) >= pcount) err("palette index out of range in " + run.dump());
            total += run[1].get<long long>();
        }
    if (total != volume) err("blocks cover " + std::to_string(total) + " cells, size needs " + std::to_string(volume));
    auto inside = [&](BlockPos const& p) { return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < sz->x && p.y < sz->y && p.z < sz->z; };
    for (auto const& m : doc.value("markers", Json::array())) {
        auto p = m.contains("pos") ? pos3(m["pos"]) : std::nullopt;
        if (!p || !inside(*p)) err("marker outside the template: " + m.dump());
        if (!m.contains("marker") || !m["marker"].is_string()) err("marker without a name: " + m.dump());
        if (m.value("marker", std::string{}) == "data" && !(m.contains("data") && m["data"].contains("metadata"))) err("data marker without data.metadata");
    }
    for (auto const& j : doc.value("jigsaw", Json::array())) {
        auto p = j.contains("pos") ? pos3(j["pos"]) : std::nullopt;
        if (!p || !inside(*p)) err("jigsaw outside the template: " + j.dump());
        auto front = j.value("front", j.value("facing", std::string{}));
        if (!isDirection(front)) err("jigsaw without a front direction: " + j.dump());
        if (j.contains("top") && !isDirection(j["top"].get<std::string>())) err("jigsaw top must be a direction: " + j.dump());
    }
    for (auto const& e : doc.value("entities", Json::array())) {
        auto const& p = e.contains("pos") ? e["pos"] : Json();
        if (!p.is_array() || p.size() != 3) err("entity without pos: " + e.dump());
        else if (p[0].get<double>() < 0 || p[1].get<double>() < 0 || p[2].get<double>() < 0 || p[0].get<double>() > sz->x ||
                 p[1].get<double>() > sz->y || p[2].get<double>() > sz->z)
            err("entity outside the template: " + e.dump());
        if (!e.contains("id") || !e["id"].is_string()) err("entity without id: " + e.dump());
    }
    return errors;
}

bool add(Json const& doc, std::string* error) {
    auto errors = validate(doc);
    if (!errors.empty()) {
        if (error) *error = errors.front();
        return false;
    }
    auto  t = makeTemplate(doc);
    auto& s = store();
    std::unique_lock lock(s.mutex);
    auto             id = t->id;
    if (auto it = s.parsedById.find(id); it != s.parsedById.end()) {
        s.retired.push_back(std::move(it->second));
        s.parsedById.erase(it);
    }
    if (auto it = s.raw.find(id); it != s.raw.end()) s.retiredRaw.push_back(std::move(it->second));
    s.invalid.erase(id);
    s.files.erase(id);
    s.raw[id] = std::move(t);
    return true;
}

namespace {

Template const* getLocked(Store& s, std::string const& id) {
    if (auto it = s.raw.find(id); it != s.raw.end()) return it->second.get();
    if (s.invalid.contains(id)) return nullptr;
    if (!s.scanned) scanLocked(s);
    auto f = s.files.find(id);
    if (f == s.files.end()) return nullptr;
    try {
        std::ifstream in(f->second);
        Json          doc    = Json::parse(in);
        auto          errors = validate(doc);
        if (!errors.empty()) {
            s.invalid.insert(id);
            warnOnce("template-invalid:" + id, "structure: template " + id + " is invalid: " + errors.front());
            return nullptr;
        }
        auto t   = makeTemplate(std::move(doc));
        auto ptr = t.get();
        s.raw[id] = std::move(t);
        return ptr;
    } catch (std::exception const& e) {
        s.invalid.insert(id);
        warnOnce("template-invalid:" + id, "structure: template " + id + " is unreadable: " + e.what());
        return nullptr;
    }
}

} // namespace

Parsed const* parsed(std::string const& id) {
    auto& s = store();
    {
        std::shared_lock lock(s.mutex);
        if (auto it = s.parsedById.find(id); it != s.parsedById.end()) return it->second.get();
    }
    std::unique_lock lock(s.mutex);
    if (auto it = s.parsedById.find(id); it != s.parsedById.end()) return it->second.get();
    auto const* t = getLocked(s, id);
    if (!t) {
        warnOnce("template-missing:" + id, "structure: missing template " + id);
        return nullptr;
    }
    auto p   = parse(*t);
    auto ptr = p.get();
    s.parsedById[id] = std::move(p);
    return ptr;
}

std::vector<std::string> ids() {
    auto&            s = store();
    std::unique_lock lock(s.mutex);
    if (!s.scanned) scanLocked(s);
    std::set<std::string> out;
    for (auto const& [id, _] : s.files) out.insert(id);
    for (auto const& [id, _] : s.raw) out.insert(id);
    return {out.begin(), out.end()};
}

void scan() {
    auto&            s = store();
    std::unique_lock lock(s.mutex);
    // keep in-memory templates (registered with add), drop file-backed ones
    std::map<std::string, std::unique_ptr<Template>> keep;
    for (auto& [id, t] : s.raw)
        if (!s.files.contains(id)) keep[id] = std::move(t);
    for (auto& [id, t] : s.raw)
        if (t) s.retiredRaw.push_back(std::move(t));
    s.raw = std::move(keep);
    for (auto& [id, p] : s.parsedById) s.retired.push_back(std::move(p));
    s.parsedById.clear();
    s.invalid.clear();
    scanLocked(s);
    for (auto const& [id, _] : s.raw) s.files.erase(id);
}

BlockPos relative(Placement const& placement, BlockPos const& local) {
    return transformPos(local, placement.settings.mirror, placement.settings.rotation, placement.rotationPivot);
}

/// The template's shared random while a placement with a fixed seed runs (else processors get a
/// position-seeded one): vanilla StructurePlaceSettings#getRandom(pos).
thread_local JavaRandom* tSharedRandom = nullptr;

bool placeInWorld(Parsed const& t, Region& region, Placement const& placement, RandomSource& random,
                  std::vector<std::pair<BlockPos, std::string>>* dataMarkers, std::vector<Placed>* placedOut) {
    if (t.cells.empty()) return false;
    auto const& settings = placement.settings;
    auto        procs    = processors::expand(settings.processors);

    std::optional<JavaRandom> shared;
    if (settings.seed != 0) shared.emplace(settings.seed);
    JavaRandom* previousShared = tSharedRandom;
    tSharedRandom              = shared ? &*shared : nullptr;
    BlockPos previousOffset    = processors::currentOffset();
    processors::setCurrentOffset(placement.offset);

    // markers that carry block entity data, by local position
    std::map<std::tuple<int, int, int>, Json> blockData;
    for (auto const& m : t.markers)
        if (!m.isData()) {
            Json d      = m.data;
            d["marker"] = m.marker;
            blockData[{m.pos.x, m.pos.y, m.pos.z}] = std::move(d);
        }

    auto const& clip = settings.clip;
    std::vector<Placed> placed;
    for (int y = 0; y < t.sy; y++)
        for (int z = 0; z < t.sz; z++)
            for (int x = 0; x < t.sx; x++) {
                auto idx = t.cells[static_cast<std::size_t>(t.index(x, y, z))];
                if (idx == 0xffff || idx >= t.palette.size() || !t.palette[idx]) continue;
                BlockPos local{x, y, z};
                BlockPos rel   = relative(placement, local);
                BlockPos world{rel.x + placement.offset.x, rel.y + placement.offset.y, rel.z + placement.offset.z};
                // processors keep x / z (gravity moves y only): skip columns outside the clip early
                if (clip && (world.x < clip->minX || world.x > clip->maxX || world.z < clip->minZ || world.z > clip->maxZ)) continue;
                BlockInfo original{local, t.palette[idx], Json()};
                if (auto it = blockData.find({x, y, z}); it != blockData.end()) original.data = it->second;
                BlockInfo current{world, t.palette[idx], original.data};
                bool      dropped = false;
                for (auto const& p : procs) {
                    auto next = processors::run(p, region, original, current, settings, placement.pivotPos);
                    if (!next) {
                        dropped = true;
                        break;
                    }
                    current = std::move(*next);
                }
                if (dropped || !current.block) continue;
                if (clip && !clip->contains(current.pos.x, current.pos.y, current.pos.z)) continue;
                Placed out{current.pos, current.block, std::move(current.data)};
                // the Java state's waterlogged survives the processors while the block can hold
                // water (FeaturePlacers.transferAllStateKeys copies it); a pot keeps its plant
                bool sameBlock = blocks::id(*current.block) == blocks::id(*t.palette[idx]);
                int  wl        = t.paletteWaterlogged[idx];
                if (sameBlock) out.waterlogged = wl;
                else if (liquids::waterloggable(*current.block)) out.waterlogged = wl > 0 ? 1 : 0;
                if (!t.pottedPlant[idx].empty() && blocks::id(*current.block) == "minecraft:flower_pot") out.pottedPlant = t.pottedPlant[idx];
                placed.push_back(std::move(out));
            }

    // StructureTemplate#placeInWorld with LiquidSettings.APPLY_WATERLOGGING (unless the settings
    // ignore it): a waterloggable block put where the world holds a water source is waterlogged;
    // afterwards, still dry ones next to a water source (above or beside) are waterlogged too,
    // repeated while that changes anything
    bool keepLiquids = !liquids::ignoresWaterlogging(settings.processors);
    auto key         = [](BlockPos const& p) { return std::tuple<int, int, int>{p.x, p.y, p.z}; };
    std::set<std::tuple<int, int, int>> sourcesPlaced; ///< list2: placed blocks whose fluid is a source
    std::set<std::tuple<int, int, int>> wet;           ///< waterlogged by this placement
    std::vector<BlockPos>               dry;           ///< list1: dry liquid containers
    for (auto& p : placed) {
        Block const* b = p.block;
        if (settings.mirror != Mirror::None) b = &blocks::mirror(*b, settings.mirror);
        if (settings.rotation != Rotation::None) b = &blocks::rotate(*b, settings.rotation);
        p.block = b;
        if (p.pos.y < region.minY() || p.pos.y >= region.maxY()) continue;
        bool container  = p.waterlogged >= 0 || liquids::waterloggable(*b);
        bool worldWater = keepLiquids && container && p.waterlogged != 1 && liquids::waterSourceAt(region, p.pos);
        // any other source fluid (lava) there: placeLiquid refuses it and the block is not retried
        bool worldSource = keepLiquids && container && !worldWater && liquids::isLavaSource(region.getBlock(p.pos));
        bool water       = p.waterlogged == 1 || worldWater;
        if (!liquids::setBlock(region, p.pos, *b, placement.flags, water)) continue;
        if (!keepLiquids) continue;
        if (p.waterlogged == 1 || liquids::alwaysWater(*b) || liquids::isWaterSource(*b)) sourcesPlaced.insert(key(p.pos));
        else if (container) {
            if (water) wet.insert(key(p.pos));
            else if (!worldSource) dry.push_back(p.pos);
        }
    }
    if (keepLiquids) {
        auto waterAt = [&](BlockPos const& p) { return wet.contains(key(p)) || liquids::waterSourceAt(region, p); };
        static constexpr int kDirs[5][3] = {{0, 1, 0}, {0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}}; // UP, NORTH, EAST, SOUTH, WEST
        bool changed = true;
        while (changed && !dry.empty()) {
            changed = false;
            for (auto it = dry.begin(); it != dry.end();) {
                bool source = waterAt(*it);
                for (int d = 0; d < 5 && !source; d++) {
                    BlockPos n{it->x + kDirs[d][0], it->y + kDirs[d][1], it->z + kDirs[d][2]};
                    if (waterAt(n) && !sourcesPlaced.contains(key(n))) source = true;
                }
                if (source) {
                    liquids::setWaterlogged(region, *it, true);
                    wet.insert(key(*it));
                    changed = true;
                    it = dry.erase(it);
                } else ++it;
            }
        }
    }
    // flower pot contents (the pot block stands now)
    for (auto const& p : placed)
        if (!p.pottedPlant.empty() && p.pos.y >= region.minY() && p.pos.y < region.maxY()) blockentities::flowerPot(region, p.pos, p.pottedPlant);
    // block entity data after every block stands (containers need their block)
    for (auto const& p : placed) {
        if (!p.data.is_object() || !p.data.contains("marker")) continue;
        auto name = p.data["marker"].get<std::string>();
        if (auto const* fn = markerFor(name)) (*fn)(region, p.pos, name, p.data, random);
        else warnOnce("marker:" + name, "structure: no handler for template marker '" + name + "'");
    }
    if (dataMarkers) {
        for (auto const& m : t.markers) {
            if (!m.isData()) continue;
            BlockPos rel = relative(placement, m.pos);
            BlockPos world{rel.x + placement.offset.x, rel.y + placement.offset.y, rel.z + placement.offset.z};
            if (clip && !clip->contains(world.x, world.y, world.z)) continue;
            dataMarkers->emplace_back(world, m.label());
        }
    }
    if (!settings.ignoreEntities) {
        for (auto const& e : t.entities) {
            // StructureTemplate.transform(Vec3, mirror, rotation, pivot) + offset
            double x = e.x, y = e.y, z = e.z;
            if (settings.mirror == Mirror::LeftRight) z = 1.0 - z;
            else if (settings.mirror == Mirror::FrontBack) x = 1.0 - x;
            double px = placement.rotationPivot.x, pz = placement.rotationPivot.z;
            double rx = x, rz = z;
            switch (settings.rotation) {
            case Rotation::Counterclockwise90:
                rx = px - pz + z;
                rz = px + pz + 1.0 - x;
                break;
            case Rotation::Clockwise90:
                rx = px + pz + 1.0 - z;
                rz = pz - px + x;
                break;
            case Rotation::Clockwise180:
                rx = px + px + 1.0 - x;
                rz = pz + pz + 1.0 - z;
                break;
            default:
                break;
            }
            double wx = rx + placement.offset.x, wy = y + placement.offset.y, wz = rz + placement.offset.z;
            BlockPos bp{pure::mthFloor(wx), pure::mthFloor(wy), pure::mthFloor(wz)};
            if (clip && !clip->contains(bp.x, bp.y, bp.z)) continue;
            if (e.id == "minecraft:painting") {
                // a hanging painting: its block and facing ("facing" as Direction#get2DDataValue or a
                // name) turned with the placement (HangingEntity#rotate / #mirror), its variant
                // hung through TF's spot check
                Orientation dir = Orientation::South;
                if (e.data.contains("facing") && e.data["facing"].is_number_integer()) dir = from2D(e.data["facing"].get<int>());
                else dir = blocks::parseDirection(e.data.value("facing", e.data.value("direction", std::string("south")))).value_or(Orientation::South);
                if (settings.mirror == Mirror::LeftRight && (dir == Orientation::North || dir == Orientation::South)) dir = opposite(dir);
                if (settings.mirror == Mirror::FrontBack && (dir == Orientation::East || dir == Orientation::West)) dir = opposite(dir);
                dir = rotateDir(dir, settings.rotation);
                auto variant = e.data.value("variant", e.data.value("motive", std::string("minecraft:kebab")));
                (void)paintings::tryHang(region, bp, dir, paintings::byId(variant));
                continue;
            }
            blockentities::entity(region, wx, wy, wz, e.id, e.data);
        }
    }
    if (placedOut) *placedOut = std::move(placed);
    tSharedRandom = previousShared;
    processors::setCurrentOffset(previousOffset);
    return true;
}

// ---- contract entry points ---------------------------------------------------------------------------

Template const* get(std::string const& id) {
    auto const* p = parsed(id);
    return p ? p->base : nullptr;
}

bool place(std::string const& id, Region& region, BlockPos const& origin, TemplateSettings const& settings) {
    auto const* t = parsed(id);
    if (!t) return false;
    Placement pl;
    pl.offset   = origin;
    pl.pivotPos = origin;
    pl.settings = settings;
    JavaRandom random(settings.seed != 0 ? settings.seed : pure::mthGetSeed(origin.x, origin.y, origin.z));
    std::vector<std::pair<BlockPos, std::string>> data;
    if (!placeInWorld(*t, region, pl, random, &data)) return false;
    // TF data markers: a handler registered under the label gets them (features handle their own)
    for (auto const& [pos, label] : data)
        if (auto const* fn = markerFor(label)) (*fn)(region, pos, label, Json::object(), random);
    return true;
}

} // namespace templates

namespace processors {

RandomSource& randomAt(BlockPos const& pos) {
    if (templates::tSharedRandom) return *templates::tSharedRandom;
    thread_local JavaRandom r;
    r.setSeed(pure::mthGetSeed(pos.x, pos.y, pos.z));
    return r;
}

} // namespace processors

} // namespace gf::structure

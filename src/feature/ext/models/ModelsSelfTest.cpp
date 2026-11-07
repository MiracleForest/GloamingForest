// Selftests of the model converter's output (unit F2, contract "geo", prefix "models."). No world
// edits, so no testPos range is used. They read what the mod ships:
//   data/gf/models/index.json   every geometry, base animation and attachable tools/models/convert.ps1
//                               generates (assets/gen/geo/models/data/...)
//   resource_packs/GloamingForest_RP/{models/entity,animations,attachables,render_controllers}/...
// and check that every generated file is in the pack and is well-formed Bedrock content that
// matches the index (identifiers, texture sizes, unique bones, parents, UVs inside the texture,
// animation bones, attachable geometries). Each test logs one "present/expected" INFO line, which
// the e2e scenario 'models' checks. Every JSON access is checked: a malformed file (a W1 unit's
// manual override, say) fails a check, never the server.
//
// Lives in src/feature/ext/models/ (rule R18): F2 owns no source directory; this adds no contract API.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using Json = nlohmann::json;
namespace fs = std::filesystem;

fs::path modDir() { return gf::Mod::getInstance().getSelf().getModDir(); }
fs::path rpDir() { return modDir() / "resource_packs" / "GloamingForest_RP"; }

std::optional<Json> readJson(fs::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    auto doc = Json::parse(in, nullptr, false, true);
    if (doc.is_discarded()) return std::nullopt;
    return doc;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// A member by reference, or null (json::value() returns a copy, which must not outlive a loop over it;
// operator[] on a const json is undefined behaviour for a missing key).
Json const& member(Json const& j, char const* key) {
    static Json const empty;
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it != j.end() ? *it : empty;
}

// An array element by reference, or null.
Json const& at(Json const& j, std::size_t i) {
    static Json const empty;
    return j.is_array() && i < j.size() ? j[i] : empty;
}

std::string str(Json const& j, char const* key, std::string const& def = "") {
    auto const& v = member(j, key);
    return v.is_string() ? v.get<std::string>() : def;
}

double num(Json const& j, char const* key, double def) {
    auto const& v = member(j, key);
    return v.is_number() ? v.get<double>() : def;
}

// Two numbers [a, b] (UV, UV size); nothing when the value is not such an array.
std::optional<std::pair<double, double>> pair2(Json const& v) {
    if (!at(v, 0).is_number() || !at(v, 1).is_number()) return std::nullopt;
    return std::make_pair(v[0].get<double>(), v[1].get<double>());
}

// The index (read for every test: the deployed mod may change between runs), or a failed test.
Json index(Context& ctx) {
    auto doc = readJson(modDir() / "data" / "gf" / "models" / "index.json");
    ctx.require(doc.has_value() && doc->is_object(), "data/gf/models/index.json is deployed and parses");
    ctx.require(num(*doc, "format", 0) == 1, "index format 1");
    return std::move(*doc);
}

std::string missingOrBroken(fs::path const& file) {
    return fs::exists(file) ? " does not parse" : " is missing from the pack";
}

// Checks one geometry file against its index entry; returns the bone names (lower case) or nothing.
std::optional<std::set<std::string>> checkGeometry(Context& ctx, Json const& entry, Json const& doc) {
    std::string const id   = str(entry, "identifier");
    std::string const file = str(entry, "file");
    auto fail = [&](std::string const& why) {
        ctx.check(false, file + ": " + why);
        return std::optional<std::set<std::string>>{};
    };
    if (str(doc, "format_version") != "1.12.0") return fail("format_version is not 1.12.0");
    auto const& geos = member(doc, "minecraft:geometry");
    if (!geos.is_array() || geos.size() != 1 || !geos[0].is_object()) return fail("expected one minecraft:geometry");
    auto const& g    = geos[0];
    auto const& desc = member(g, "description");
    if (str(desc, "identifier") != id) return fail("identifier " + str(desc, "identifier", "?") + " != " + id);
    double const w = num(desc, "texture_width", 0), h = num(desc, "texture_height", 0);
    if (w <= 0 || h <= 0) return fail("no texture size");
    if (w != num(entry, "texture_width", -1) || h != num(entry, "texture_height", -1)) return fail("texture size differs from the index");
    auto const& bones = member(g, "bones");
    if (!bones.is_array()) return fail("no bones");
    std::set<std::string> names;
    for (auto const& b : bones) {
        auto n = lower(str(b, "name"));
        if (n.empty() || !names.insert(n).second) return fail("empty or duplicate bone name '" + n + "'");
    }
    std::set<std::string> external;
    for (auto const& p : member(entry, "external_parents"))
        if (p.is_string()) external.insert(lower(p.get<std::string>()));
    int cubes = 0;
    for (auto const& b : bones) {
        std::string const bone = str(b, "name");
        auto const&       parent = member(b, "parent");
        if (!parent.is_null()) {
            if (!parent.is_string()) return fail("bone " + bone + " has a parent that is not a name");
            auto p = lower(parent.get<std::string>());
            if (!names.contains(p) && !external.contains(p)) return fail("bone " + bone + " has an unknown parent " + p);
        }
        auto const& list = member(b, "cubes");
        if (!list.is_null() && !list.is_array()) return fail("cubes of " + bone + " is not a list");
        for (auto const& c : list) {
            ++cubes;
            auto const& size = member(c, "size");
            if (!size.is_array() || size.size() != 3) return fail("cube without a size in " + bone);
            for (auto const& s : size)
                if (!s.is_number() || s.get<double>() < 0) return fail("negative or missing cube size in " + bone);
            auto const& uv     = member(c, "uv");
            auto        inside = [&](double u, double v) { return u >= -1e-3 && v >= -1e-3 && u <= w + 1e-3 && v <= h + 1e-3; };
            if (uv.is_array()) {
                auto p = pair2(uv);
                if (!p) return fail("box uv is not [u, v] in " + bone);
                if (!inside(p->first, p->second)) return fail("box uv outside the texture in " + bone);
            } else if (uv.is_object() && !uv.empty()) {
                for (auto const& [face, f] : uv.items()) {
                    auto p0 = pair2(member(f, "uv"));
                    auto ps = pair2(member(f, "uv_size"));
                    if (!p0 || !ps) return fail(face + " uv without uv / uv_size in " + bone);
                    if (!inside(p0->first, p0->second) || !inside(p0->first + ps->first, p0->second + ps->second))
                        return fail(face + " uv outside the texture in " + bone);
                }
            } else {
                return fail("cube without uv in " + bone);
            }
        }
    }
    if (static_cast<double>(bones.size()) != num(entry, "bones", -1)) return fail("bone count differs from the index");
    if (static_cast<double>(cubes) != num(entry, "cubes", -1)) return fail("cube count differs from the index");
    return names;
}

// The names a resource-pack folder defines under `key` ("animations", "render_controllers", ...), every file
// of it (a W1 unit's manual files sit next to F2's generated ones).
std::set<std::string> definedIn(char const* folder, char const* key) {
    std::set<std::string> names;
    std::error_code       ec;
    auto                  dir = rpDir() / folder;
    if (!fs::exists(dir, ec)) return names;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
        auto doc = readJson(it->path());
        if (!doc) continue;
        if (std::string(key) == "minecraft:geometry") {
            for (auto const& g : member(*doc, "minecraft:geometry")) names.insert(str(member(g, "description"), "identifier"));
            if (doc->is_object()) // the legacy format: "geometry.x" or "geometry.x:parent" keys
                for (auto const& [name, _] : doc->items())
                    if (name.starts_with("geometry.")) names.insert(name.substr(0, name.find(':')));
        } else {
            for (auto const& [name, _] : member(*doc, key).items()) names.insert(name);
        }
    }
    return names;
}

} // namespace

GF_REGISTER_SELFTEST("models.index", [](Context& ctx) {
    auto const idx = index(ctx);
    int        geometries = 0, vanilla = 0;
    for (auto const& g : member(idx, "geometries")) (member(g, "vanilla").is_boolean() && member(g, "vanilla").get<bool>() ? vanilla : geometries)++;
    auto animations  = member(idx, "animations").size();
    auto attachables = member(idx, "attachables").size();
    ctx.check(geometries >= 100, "the converter wrote the entity, armor and trophy geometries");
    ctx.check(vanilla >= 1, "vanilla-model layers are listed");
    ctx.check(animations >= 1 && attachables >= 1, "base animations and attachables are listed");
    ctx.info("index: " + std::to_string(geometries) + " geometries, " + std::to_string(vanilla) + " vanilla layers, "
             + std::to_string(animations) + " base animations, " + std::to_string(attachables) + " attachables (TF "
             + str(idx, "tf", "?") + ")");
});

GF_REGISTER_SELFTEST("models.geometry", [](Context& ctx) {
    auto const idx     = index(ctx);
    int        present = 0, expected = 0;
    for (auto const& e : member(idx, "geometries")) {
        if (member(e, "vanilla").is_boolean() && member(e, "vanilla").get<bool>()) continue;
        ++expected;
        auto const file = str(e, "file");
        if (file.empty()) {
            ctx.check(false, "index geometry " + str(e, "identifier", "?") + " has no file");
            continue;
        }
        auto doc = readJson(rpDir() / file);
        if (!doc) {
            ctx.check(false, file + missingOrBroken(rpDir() / file));
            continue;
        }
        if (checkGeometry(ctx, e, *doc)) ++present;
    }
    ctx.info("geometries present " + std::to_string(present) + "/" + std::to_string(expected));
});

GF_REGISTER_SELFTEST("models.animations", [](Context& ctx) {
    auto const                         idx = index(ctx);
    std::map<std::string, Json const*> geoById;
    for (auto const& e : member(idx, "geometries")) geoById[str(e, "identifier")] = &e;
    int present = 0, expected = 0;
    for (auto const& a : member(idx, "animations")) {
        ++expected;
        std::string const id   = str(a, "identifier");
        std::string const file = str(a, "file");
        if (id.empty() || file.empty()) {
            ctx.check(false, "index animation without an identifier or a file");
            continue;
        }
        auto doc = readJson(rpDir() / file);
        if (!doc) {
            ctx.check(false, file + missingOrBroken(rpDir() / file));
            continue;
        }
        auto const& anim = member(member(*doc, "animations"), id.c_str());
        if (!anim.is_object()) {
            ctx.check(false, file + ": no " + id);
            continue;
        }
        auto const& bonesOfAnim = member(anim, "bones");
        if (!bonesOfAnim.is_object() || bonesOfAnim.empty()) {
            ctx.check(false, file + ": " + id + " has no bones (Bedrock rejects an empty bones object)");
            continue;
        }
        // the bones it moves exist in its geometry (when that geometry is in the pack)
        std::string const prefix = "animation.", suffix = ".base";
        if (id.starts_with(prefix) && id.ends_with(suffix) && id.size() > prefix.size() + suffix.size()) {
            std::string geoId = "geometry." + id.substr(prefix.size(), id.size() - prefix.size() - suffix.size());
            auto        it    = geoById.find(geoId);
            if (it != geoById.end()) {
                if (auto geo = readJson(rpDir() / str(*it->second, "file"))) {
                    std::set<std::string> bones;
                    for (auto const& b : member(at(member(*geo, "minecraft:geometry"), 0), "bones")) bones.insert(lower(str(b, "name")));
                    for (auto const& [bone, _] : bonesOfAnim.items())
                        ctx.check(bones.contains(lower(bone)), id + " moves " + bone + ", which " + geoId + " does not have");
                }
            }
        }
        ++present;
    }
    ctx.info("base animations present " + std::to_string(present) + "/" + std::to_string(expected));
});

GF_REGISTER_SELFTEST("models.attachables", [](Context& ctx) {
    auto const idx = index(ctx);
    // what the pack defines, F2's generated files and the units' manual ones alike: assemble keeps one attachable
    // per identifier and a unit's manual attachable replaces F2's (its own file name, geometry, animations and render
    // controllers), so every name is looked up across the pack, not in the index only
    // (the models folder holds every block and entity geometry: read only when an attachable names one F2 did not)
    std::set<std::string> geometries;
    for (auto const& e : member(idx, "geometries")) geometries.insert(str(e, "identifier"));
    bool scanned  = false;
    auto geometry = [&](std::string const& id) {
        if (!geometries.contains(id) && !scanned) {
            scanned = true;
            geometries.merge(definedIn("models", "minecraft:geometry"));
        }
        return geometries.contains(id);
    };
    std::set<std::string> animations = definedIn("animations", "animations");
    for (auto const& a : member(idx, "animations")) animations.insert(str(a, "identifier"));
    std::set<std::string> controllers = definedIn("render_controllers", "render_controllers");
    controllers.insert({"controller.render.armor", "controller.render.item_default"}); // vanilla
    // the shared render controllers (F2's partition) must be there
    auto rc = readJson(rpDir() / "render_controllers" / "twilightforest.models.render_controllers.json");
    ctx.require(rc.has_value(), "render_controllers/twilightforest.models.render_controllers.json is in the pack");
    // every attachable of the pack by identifier
    std::map<std::string, std::string> byItem; // identifier -> pack-relative file
    {
        std::error_code ec;
        auto            dir = rpDir() / "attachables";
        if (fs::exists(dir, ec))
            for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
                auto doc = readJson(it->path());
                if (!doc) continue;
                auto id = str(member(member(*doc, "minecraft:attachable"), "description"), "identifier");
                if (id.empty()) continue;
                auto rel = fs::relative(it->path(), rpDir(), ec).generic_string();
                auto [at, fresh] = byItem.emplace(id, rel);
                ctx.check(fresh, id + " has two attachables: " + (fresh ? "" : at->second + " and " + rel));
            }
    }
    int present = 0, expected = 0, overridden = 0;
    for (auto const& a : member(idx, "attachables")) {
        ++expected;
        std::string const item = str(a, "item");
        std::string       file = str(a, "file");
        if (auto it = byItem.find(item); it != byItem.end() && it->second != file) {
            file = it->second; // a unit's manual override (assemble dropped the generated file)
            ++overridden;
        }
        std::optional<Json> doc;
        if (!file.empty()) doc = readJson(rpDir() / file);
        if (!doc) {
            ctx.check(false, (file.empty() ? "index attachable without a file" : file + missingOrBroken(rpDir() / file)) + " (" + item + ")");
            continue;
        }
        auto const& desc = member(member(*doc, "minecraft:attachable"), "description");
        ctx.check(str(desc, "identifier") == str(a, "item", "?"), file + ": identifier is the item");
        auto const& geometry = member(desc, "geometry");
        ctx.check(geometry.is_object() && !geometry.empty(), file + ": names a geometry");
        for (auto const& [key, geo] : geometry.items()) {
            auto id = geo.is_string() ? geo.get<std::string>() : std::string();
            ctx.check(id.starts_with("geometry.humanoid.armor.") || geometry(id), file + ": unknown geometry " + id);
            ctx.check(member(desc, "textures").contains(key) && member(desc, "materials").contains(key), file + ": " + key + " has a texture and a material");
        }
        for (auto const& [key, anim] : member(desc, "animations").items()) {
            auto id = anim.is_string() ? anim.get<std::string>() : std::string();
            ctx.check(animations.contains(id), file + ": unknown animation " + key + " = " + id);
        }
        auto const& list = member(desc, "render_controllers");
        ctx.check(list.is_array() && !list.empty(), file + ": has render controllers");
        for (auto const& r : list) {
            std::string name;
            if (r.is_string()) name = r.get<std::string>();
            else if (r.is_object() && r.size() == 1 && r.begin().value().is_string()) name = r.begin().key();
            ctx.check(!name.empty() && controllers.contains(name), file + ": unknown or malformed render controller " + (name.empty() ? r.dump() : name));
        }
        ++present;
    }
    ctx.info("attachables present " + std::to_string(present) + "/" + std::to_string(expected)
             + (overridden ? " (" + std::to_string(overridden) + " replaced by their owners' manual attachables)" : ""));
});

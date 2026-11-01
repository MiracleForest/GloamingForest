// Selftests of the model converter's output (unit F2, contract "geo", prefix "models."). No world
// edits, so no testPos range is used. They read what the mod ships:
//   data/gf/models/index.json   every geometry, base animation and attachable tools/models/convert.ps1
//                               generates (assets/gen/geo/models/data/...)
//   resource_packs/GloamingForest_RP/{models/entity,animations,attachables,render_controllers}/...
// and check that every generated file present in the pack is well-formed Bedrock content that
// matches the index (identifiers, texture sizes, unique bones, parents, UVs inside the texture,
// animation bones, attachable geometries). A unit partition that its W1 unit has not regenerated
// yet is reported, not failed; F2's own partitions must be complete. Each test logs one
// "present/expected" INFO line, which the e2e scenario (tools/models/e2e) checks after it put every
// partition into the deployed mod.
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

// The index (read for every test: the deployed mod may change between runs), or a failed test.
Json index(Context& ctx) {
    auto doc = readJson(modDir() / "data" / "gf" / "models" / "index.json");
    ctx.require(doc.has_value(), "data/gf/models/index.json is deployed and parses");
    ctx.require(doc->value("format", 0) == 1, "index format 1");
    return std::move(*doc);
}

// A member by reference (json::value() returns a copy, which must not outlive a loop over it).
Json const& member(Json const& j, char const* key) {
    static Json const empty;
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it != j.end() ? *it : empty;
}

// The partitions F2 commits itself: they must always be complete.
bool ownedByF2(std::string const& unit) { return unit == "models" || unit == "landmark"; }

// Checks one geometry file against its index entry; returns the bone names (lower case) or nothing.
std::optional<std::set<std::string>> checkGeometry(Context& ctx, Json const& entry, Json const& doc) {
    std::string const id   = entry.value("identifier", "");
    std::string const file = entry.value("file", "");
    auto fail = [&](std::string const& why) {
        ctx.check(false, file + ": " + why);
        return std::optional<std::set<std::string>>{};
    };
    if (doc.value("format_version", "") != "1.12.0") return fail("format_version is not 1.12.0");
    auto const& geos = doc.value("minecraft:geometry", Json::array());
    if (!geos.is_array() || geos.size() != 1) return fail("expected one minecraft:geometry");
    auto const& g    = geos[0];
    auto const& desc = g.value("description", Json::object());
    if (desc.value("identifier", "") != id) return fail("identifier " + desc.value("identifier", "?") + " != " + id);
    double const w = desc.value("texture_width", 0.0), h = desc.value("texture_height", 0.0);
    if (w != entry.value("texture_width", -1.0) || h != entry.value("texture_height", -1.0)) return fail("texture size differs from the index");
    std::set<std::string> names;
    auto const& bones = g.value("bones", Json::array());
    for (auto const& b : bones) {
        auto n = lower(b.value("name", ""));
        if (n.empty() || !names.insert(n).second) return fail("empty or duplicate bone name '" + n + "'");
    }
    std::set<std::string> external;
    for (auto const& p : entry.value("external_parents", Json::array())) external.insert(lower(p.get<std::string>()));
    int cubes = 0;
    for (auto const& b : bones) {
        if (b.contains("parent")) {
            auto p = lower(b["parent"].get<std::string>());
            if (!names.contains(p) && !external.contains(p)) return fail("bone " + b.value("name", "") + " has an unknown parent " + p);
        }
        for (auto const& c : b.value("cubes", Json::array())) {
            ++cubes;
            for (auto const& s : c.value("size", Json::array()))
                if (s.get<double>() < 0) return fail("negative cube size in " + b.value("name", ""));
            auto const& uv = c["uv"];
            auto inside = [&](double u, double v) { return u >= -1e-3 && v >= -1e-3 && u <= w + 1e-3 && v <= h + 1e-3; };
            if (uv.is_array()) {
                if (!inside(uv[0].get<double>(), uv[1].get<double>())) return fail("box uv outside the texture in " + b.value("name", ""));
            } else if (uv.is_object()) {
                for (auto const& [face, f] : uv.items()) {
                    double u0 = f["uv"][0].get<double>(), v0 = f["uv"][1].get<double>();
                    double u1 = u0 + f["uv_size"][0].get<double>(), v1 = v0 + f["uv_size"][1].get<double>();
                    if (!inside(u0, v0) || !inside(u1, v1)) return fail(face + " uv outside the texture in " + b.value("name", ""));
                }
            } else {
                return fail("cube without uv in " + b.value("name", ""));
            }
        }
    }
    if (static_cast<int>(bones.size()) != entry.value("bones", -1)) return fail("bone count differs from the index");
    if (cubes != entry.value("cubes", -1)) return fail("cube count differs from the index");
    return names;
}

} // namespace

GF_REGISTER_SELFTEST("models.index", [](Context& ctx) {
    auto const idx = index(ctx);
    int geometries = 0, vanilla = 0;
    for (auto const& g : member(idx, "geometries")) (g.value("vanilla", false) ? vanilla : geometries)++;
    auto animations  = member(idx, "animations").size();
    auto attachables = member(idx, "attachables").size();
    ctx.check(geometries >= 100, "the converter wrote the entity, armor and trophy geometries");
    ctx.check(vanilla >= 1, "vanilla-model layers are listed");
    ctx.check(animations >= 1 && attachables >= 1, "base animations and attachables are listed");
    ctx.info("index: " + std::to_string(geometries) + " geometries, " + std::to_string(vanilla) + " vanilla layers, "
             + std::to_string(animations) + " base animations, " + std::to_string(attachables) + " attachables (TF "
             + idx.value("tf", "?") + ")");
});

GF_REGISTER_SELFTEST("models.geometry", [](Context& ctx) {
    auto const idx = index(ctx);
    int present = 0, expected = 0;
    std::map<std::string, int> missingByUnit;
    for (auto const& e : member(idx, "geometries")) {
        if (e.value("vanilla", false)) continue;
        ++expected;
        std::string const unit = e.value("unit", "");
        auto doc = readJson(rpDir() / e.value("file", ""));
        if (!doc) {
            if (fs::exists(rpDir() / e.value("file", ""))) ctx.check(false, e.value("file", "") + " does not parse");
            else if (ownedByF2(unit)) ctx.check(false, e.value("file", "") + " (F2's partition) is missing from the pack");
            else missingByUnit[unit]++;
            continue;
        }
        if (checkGeometry(ctx, e, *doc)) ++present;
    }
    std::string missing;
    for (auto const& [unit, n] : missingByUnit) missing += " " + unit + ":" + std::to_string(n);
    ctx.info("geometries present " + std::to_string(present) + "/" + std::to_string(expected)
             + (missing.empty() ? "" : "; not regenerated yet by their units:" + missing));
});

GF_REGISTER_SELFTEST("models.animations", [](Context& ctx) {
    auto const idx = index(ctx);
    std::map<std::string, Json const*> geoById;
    for (auto const& e : member(idx, "geometries")) geoById[e.value("identifier", "")] = &e;
    int present = 0, expected = 0;
    for (auto const& a : member(idx, "animations")) {
        ++expected;
        std::string const id = a.value("identifier", "");
        auto doc = readJson(rpDir() / a.value("file", ""));
        if (!doc) {
            ctx.check(!fs::exists(rpDir() / a.value("file", "")), a.value("file", "") + " does not parse");
            continue;
        }
        auto const& anim = member(member(*doc, "animations"), id.c_str());
        if (!anim.is_object()) {
            ctx.check(false, a.value("file", "") + ": no " + id);
            continue;
        }
        // the bones it moves exist in its geometry (when that geometry is in the pack)
        std::string geoId = "geometry." + id.substr(std::string("animation.").size());
        geoId             = geoId.substr(0, geoId.size() - std::string(".base").size());
        auto it           = geoById.find(geoId);
        if (it != geoById.end()) {
            if (auto geo = readJson(rpDir() / it->second->value("file", ""))) {
                std::set<std::string> bones;
                auto const& geos = member(*geo, "minecraft:geometry");
                if (geos.is_array() && !geos.empty())
                    for (auto const& b : member(geos[0], "bones")) bones.insert(b.value("name", ""));
                for (auto const& [bone, _] : member(anim, "bones").items())
                    ctx.check(bones.contains(bone), id + " moves " + bone + ", which " + geoId + " does not have");
            }
        }
        ++present;
    }
    ctx.info("base animations present " + std::to_string(present) + "/" + std::to_string(expected));
});

GF_REGISTER_SELFTEST("models.attachables", [](Context& ctx) {
    auto const idx = index(ctx);
    std::set<std::string> geometries;
    for (auto const& e : member(idx, "geometries")) geometries.insert(e.value("identifier", ""));
    // the shared render controllers (F2's partition) must be there
    std::set<std::string> controllers{"controller.render.armor", "controller.render.item_default"};
    auto rc = readJson(rpDir() / "render_controllers" / "twilightforest.models.render_controllers.json");
    ctx.require(rc.has_value(), "render_controllers/twilightforest.models.render_controllers.json is in the pack");
    for (auto const& [name, _] : member(*rc, "render_controllers").items()) controllers.insert(name);
    int present = 0, expected = 0;
    for (auto const& a : member(idx, "attachables")) {
        ++expected;
        auto doc = readJson(rpDir() / a.value("file", ""));
        if (!doc) {
            ctx.check(!fs::exists(rpDir() / a.value("file", "")), a.value("file", "") + " does not parse");
            continue;
        }
        auto const& desc = member(member(*doc, "minecraft:attachable"), "description");
        ctx.check(desc.value("identifier", "") == a.value("item", "?"), a.value("file", "") + ": identifier is the item");
        for (auto const& [key, geo] : member(desc, "geometry").items()) {
            auto id = geo.get<std::string>();
            ctx.check(geometries.contains(id) || id.starts_with("geometry.humanoid.armor."), a.value("file", "") + ": unknown geometry " + id);
            ctx.check(member(desc, "textures").contains(key) && member(desc, "materials").contains(key), a.value("file", "") + ": " + key + " has a texture and a material");
        }
        for (auto const& r : member(desc, "render_controllers")) {
            std::string name = r.is_string() ? r.get<std::string>() : r.items().begin().key();
            ctx.check(controllers.contains(name), a.value("file", "") + ": unknown render controller " + name);
        }
        ++present;
    }
    ctx.info("attachables present " + std::to_string(present) + "/" + std::to_string(expected));
});

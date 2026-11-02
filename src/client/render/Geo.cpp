// Adapted from LiteLDev/Levitate v0.1.0 (src/core/kinetics/RotorModel.cpp), LGPL-3.0-or-later.
//
// Bedrock geometry files as quads (F12, see Geo.h). Client build only. Rotation order and box UV
// layout as Levitate's rotor model, extended with bones (parents, pivots, rotations), inflate and
// mirror.

#ifdef GF_CLIENT

#include "client/render/Geo.h"

#include <cmath>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "mod/Log.h"

namespace gf::client::render::geo {

namespace {

using V3   = std::array<float, 3>;
using Json = nlohmann::json;

V3 vec3(Json const& j, V3 fallback = {0, 0, 0}) {
    if (!j.is_array() || j.size() != 3) return fallback;
    try {
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    } catch (...) {
        return fallback;
    }
}

/// Rotates `p` around `pivot` by `degrees` (Z, then Y, then X; json space).
V3 rotate(V3 p, V3 const& pivot, V3 const& degrees) {
    constexpr float kRad = 3.14159265358979f / 180.f;
    if (degrees[0] == 0 && degrees[1] == 0 && degrees[2] == 0) return p;
    for (int i = 0; i < 3; ++i) p[i] -= pivot[i];
    if (degrees[2] != 0) {
        float c = std::cos(degrees[2] * kRad), s = std::sin(degrees[2] * kRad);
        p = {p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2]};
    }
    if (degrees[1] != 0) {
        float c = std::cos(degrees[1] * kRad), s = std::sin(degrees[1] * kRad);
        p = {p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c};
    }
    if (degrees[0] != 0) {
        float c = std::cos(degrees[0] * kRad), s = std::sin(degrees[0] * kRad);
        p = {p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c};
    }
    for (int i = 0; i < 3; ++i) p[i] += pivot[i];
    return p;
}

struct FaceUv {
    float u = 0, v = 0, w = 0, h = 0;
};

struct Bone {
    std::string name, parent;
    V3          pivot{}, rotation{};
    bool        mirror  = false;
    float       inflate = 0;
    Json const* json    = nullptr;
};

/// A rotation step applied to points: the cube's own, then each bone up to the root.
struct Rot {
    V3 pivot, degrees;
};

} // namespace

std::map<std::string, Model> parse(Json const& file, std::string* error) {
    std::map<std::string, Model> out;
    auto fail = [&](std::string const& why) {
        if (error) *error = why;
        return std::map<std::string, Model>{};
    };
    if (!file.is_object()) return fail("not an object");
    auto list = file.find("minecraft:geometry");
    if (list == file.end() || !list->is_array()) return fail("no minecraft:geometry");
    for (auto const& geo : *list) {
        if (!geo.is_object()) continue;
        auto const desc = geo.value("description", Json::object());
        auto       id   = desc.value("identifier", std::string());
        if (id.empty()) continue;
        Model model;
        model.id            = id;
        model.textureWidth  = desc.value("texture_width", 64.f);
        model.textureHeight = desc.value("texture_height", 64.f);
        float tw = model.textureWidth > 0 ? model.textureWidth : 64.f, th = model.textureHeight > 0 ? model.textureHeight : 64.f;

        std::unordered_map<std::string, Bone> bones;
        std::vector<std::string>              order;
        auto const                            boneList = geo.value("bones", Json::array()); // bones point into it
        for (auto const& b : boneList) {
            if (!b.is_object()) continue;
            Bone bone;
            bone.name     = b.value("name", std::string());
            bone.parent   = b.value("parent", std::string());
            bone.pivot    = vec3(b.value("pivot", Json()));
            bone.rotation = vec3(b.value("rotation", Json()));
            bone.mirror   = b.value("mirror", false);
            bone.inflate  = b.value("inflate", 0.f);
            bone.json     = &b;
            order.push_back(bone.name);
            bones[bone.name] = std::move(bone);
        }
        for (auto const& name : order) {
            auto const& bone = bones[name];
            // the rotations from the bone up to the root (each once; a cycle stops the walk)
            std::vector<Rot> chain;
            std::string      at    = name;
            int              guard = 0;
            while (!at.empty() && guard++ < 64) {
                auto it = bones.find(at);
                if (it == bones.end()) break;
                chain.push_back({it->second.pivot, it->second.rotation});
                at = it->second.parent;
            }
            for (auto const& cube : bone.json->value("cubes", Json::array())) {
                if (!cube.is_object()) continue;
                V3    o       = vec3(cube.value("origin", Json()));
                V3    s       = vec3(cube.value("size", Json()));
                float inflate = cube.value("inflate", bone.inflate);
                bool  mirror  = cube.value("mirror", bone.mirror);
                V3    pivot   = vec3(cube.value("pivot", Json()), {o[0] + s[0] / 2, o[1] + s[1] / 2, o[2] + s[2] / 2});
                V3    rot     = vec3(cube.value("rotation", Json()));
                float x0 = o[0] - inflate, y0 = o[1] - inflate, z0 = o[2] - inflate;
                float x1 = o[0] + s[0] + inflate, y1 = o[1] + s[1] + inflate, z1 = o[2] + s[2] + inflate;

                std::map<std::string, FaceUv> uvs;
                auto                          uvj = cube.value("uv", Json());
                if (uvj.is_object()) {
                    for (auto const& [face, f] : uvj.items()) {
                        if (!f.is_object()) continue;
                        auto uv = f.value("uv", Json::array({0, 0}));
                        auto sz = f.value("uv_size", Json::array({0, 0}));
                        try {
                            uvs[face] = FaceUv{uv[0].get<float>(), uv[1].get<float>(), sz[0].get<float>(), sz[1].get<float>()};
                        } catch (...) {}
                    }
                } else if (uvj.is_array() && uvj.size() == 2) { // box UV (sizes floored as the game does)
                    float u = uvj[0].get<float>(), v = uvj[1].get<float>();
                    float sx = std::floor(s[0]), sy = std::floor(s[1]), sz = std::floor(s[2]);
                    uvs["up"]    = {u + sz, v, sx, sz};
                    uvs["down"]  = {u + sz + sx, v, sx, sz};
                    uvs["west"]  = {u, v + sz, sz, sy};
                    uvs["north"] = {u + sz, v + sz, sx, sy};
                    uvs["east"]  = {u + sz + sx, v + sz, sz, sy};
                    uvs["south"] = {u + 2 * sz + sx, v + sz, sx, sy};
                    if (mirror) { // the box is mirrored: east and west swap, every face flips horizontally
                        std::swap(uvs["east"], uvs["west"]);
                        for (auto& [_, f] : uvs) {
                            f.u += f.w;
                            f.w = -f.w;
                        }
                    }
                }
                // corners per face, outside view: TL, TR, BR, BL (json space)
                std::map<std::string, std::array<V3, 4>> faces = {
                    {"north", {V3{x1, y1, z0}, V3{x0, y1, z0}, V3{x0, y0, z0}, V3{x1, y0, z0}}},
                    {"south", {V3{x0, y1, z1}, V3{x1, y1, z1}, V3{x1, y0, z1}, V3{x0, y0, z1}}},
                    {"east", {V3{x1, y1, z1}, V3{x1, y1, z0}, V3{x1, y0, z0}, V3{x1, y0, z1}}},
                    {"west", {V3{x0, y1, z0}, V3{x0, y1, z1}, V3{x0, y0, z1}, V3{x0, y0, z0}}},
                    {"up", {V3{x0, y1, z0}, V3{x1, y1, z0}, V3{x1, y1, z1}, V3{x0, y1, z1}}},
                    {"down", {V3{x0, y0, z1}, V3{x1, y0, z1}, V3{x1, y0, z0}, V3{x0, y0, z0}}},
                };
                for (auto const& [name2, corners] : faces) {
                    auto it = uvs.find(name2);
                    if (it == uvs.end()) continue;
                    FaceUv const& f = it->second;
                    if (f.w == 0 && f.h == 0) continue;
                    Quad q;
                    q.uv = {{
                        {f.u / tw, f.v / th},
                        {(f.u + f.w) / tw, f.v / th},
                        {(f.u + f.w) / tw, (f.v + f.h) / th},
                        {f.u / tw, (f.v + f.h) / th},
                    }};
                    for (int i = 0; i < 4; ++i) {
                        V3 p = rotate(corners[i], pivot, rot);
                        for (auto const& r : chain) p = rotate(p, r.pivot, r.degrees);
                        q.pos[i] = {-p[0] / 16.f, p[1] / 16.f, p[2] / 16.f};
                    }
                    model.quads.push_back(q);
                }
            }
        }
        out[id] = std::move(model);
    }
    if (out.empty()) return fail("no geometry with an identifier");
    return out;
}

namespace {
struct Library {
    std::mutex                   mutex;
    std::filesystem::path        root;
    bool                         loaded = false;
    std::map<std::string, Model> models;
    std::map<std::string, Model> added; ///< add(): kept across setRoot
};
Library& library() {
    static Library l;
    return l;
}

void loadAll(Library& l) {
    l.loaded = true;
    std::error_code ec;
    auto            packs = l.root / "resource_packs";
    if (!std::filesystem::exists(packs, ec)) return;
    int files = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(packs, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        auto const& p    = it->path();
        auto        name = p.filename().string();
        if (!it->is_regular_file(ec) || !name.ends_with(".json")) continue;
        if (p.string().find("models") == std::string::npos) continue;
        std::ifstream in(p);
        auto          j = Json::parse(in, nullptr, false, true);
        if (j.is_discarded()) continue;
        auto models = parse(j);
        if (models.empty()) continue;
        ++files;
        l.models.merge(models);
    }
    log().info("render: {} geometries from {} model files", l.models.size(), files);
}
} // namespace

void setRoot(std::filesystem::path const& modDir) {
    auto&           l = library();
    std::lock_guard lock(l.mutex);
    l.root   = modDir;
    l.loaded = false;
    l.models.clear();
}

Model const* find(std::string const& id) {
    auto&           l = library();
    std::lock_guard lock(l.mutex);
    if (!l.loaded) loadAll(l);
    if (auto it = l.models.find(id); it != l.models.end()) return &it->second;
    auto it = l.added.find(id);
    return it == l.added.end() ? nullptr : &it->second;
}

std::size_t add(std::map<std::string, Model> models) {
    auto&           l = library();
    std::lock_guard lock(l.mutex);
    std::size_t     n = 0;
    for (auto& [id, m] : models) n += l.added.try_emplace(id, std::move(m)).second ? 1 : 0;
    return n;
}

} // namespace gf::client::render::geo

#endif // GF_CLIENT

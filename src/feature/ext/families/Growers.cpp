// Contract extension of "families+blockevents" (F11): sapling growers by string id (Growers.h).

#include "feature/ext/families/Growers.h"

#include <map>

namespace gf::families_ext {

namespace {
std::map<std::string, GrowerFn>& growers() {
    static std::map<std::string, GrowerFn> m;
    return m;
}
std::map<std::string, StructureGrowerFn>& structureGrowers() {
    static std::map<std::string, StructureGrowerFn> m;
    return m;
}
} // namespace

void registerGrower(std::string const& saplingId, GrowerFn fn) { growers()[saplingId] = std::move(fn); }

void registerStructureGrower(std::string const& structureId, StructureGrowerFn fn) { structureGrowers()[structureId] = std::move(fn); }

GrowerFn const* growerOf(std::string const& saplingId) {
    auto it = growers().find(saplingId);
    return it == growers().end() || !it->second ? nullptr : &it->second;
}

StructureGrowerFn const* structureGrowerOf(std::string const& structureId) {
    auto it = structureGrowers().find(structureId);
    return it == structureGrowers().end() || !it->second ? nullptr : &it->second;
}

} // namespace gf::families_ext

// The molang variables gf::fx gives particle effects (FxInternal.h): `variable.velocity`, for the
// server's SpawnParticleEffectPackets (vanilla clients) and the client's resource pack effects
// (companions whose particle has no native factory).

#include <cstddef>
#include <memory>
#include <new>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangMemberArray.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangStruct_XYZ.h"
#include "mc/util/MolangVariableMap.h"

#include "feature/fx/FxInternal.h"

namespace gf::fx {

// The map is two std::vectors and a flag. Its members are constructed here with this build's STL,
// so the vectors must have the game's layout: the release STL (no iterator debugging, no proxy).
static_assert(_ITERATOR_DEBUG_LEVEL == 0, "MolangVariableMap's vectors need the release STL layout (_ITERATOR_DEBUG_LEVEL 0)");
static_assert(sizeof(std::vector<short>) == 3 * sizeof(void*), "a std::vector is three pointers");
static_assert(sizeof(MolangVariableMap) == 56 && alignof(MolangVariableMap) == 8, "MolangVariableMap: 56 bytes");
static_assert(offsetof(MolangVariableMap, mMapFromVariableIndexToVariableArrayOffset) == 0, "MolangVariableMap layout");
static_assert(offsetof(MolangVariableMap, mVariables) == 24, "MolangVariableMap layout");
static_assert(offsetof(MolangVariableMap, mHasPublicVariables) == 48, "MolangVariableMap layout");
static_assert(sizeof(MolangMap) == sizeof(MolangVariableMap), "MolangMap holds exactly one MolangVariableMap");

MolangMap::MolangMap() {
    auto* map = reinterpret_cast<MolangVariableMap*>(mStorage);
    // ll::TypedStorage members construct their value: two empty vectors; the flag is a plain bool
    std::construct_at(std::addressof(map->mMapFromVariableIndexToVariableArrayOffset));
    std::construct_at(std::addressof(map->mVariables));
    map->mHasPublicVariables = false;
}

MolangMap::~MolangMap() { std::destroy_at(std::addressof(get())); } // the game's destructor

MolangVariableMap& MolangMap::get() { return *std::launder(reinterpret_cast<MolangVariableMap*>(mStorage)); }

MolangVariableMap const& MolangMap::get() const { return *std::launder(reinterpret_cast<MolangVariableMap const*>(mStorage)); }

void MolangMap::setVelocity(Vec3 const& velocity) {
    // a struct {x, y, z}, as the script API's MolangVariableMap#setVector3 makes it
    MolangScriptArg const value{MolangMemberArray(MolangStruct_XYZ::FromVec3, velocity)};
    get().setMolangVariable(HashedString("variable.velocity"), value);
}

void fillVariables(MolangMap& map, Vec3 const& velocity) {
    if (velocity.x != 0.f || velocity.y != 0.f || velocity.z != 0.f) map.setVelocity(velocity);
}

} // namespace gf::fx

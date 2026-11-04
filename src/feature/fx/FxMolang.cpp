// The molang variables gf::fx gives particle effects (FxInternal.h): `variable.velocity`,
// `variable.direction`, `variable.color` and `variable.note_color`, for the server's
// SpawnParticleEffectPackets (vanilla clients) and the client's resource pack effects (companions
// whose particle has no native factory).
//
// noteColor translates NoteParticle (net.minecraft.client.particle, Java Edition) as TF 4.8 calls it.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <new>
#include <vector>

#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangMemberArray.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangStruct_RGB.h"
#include "mc/util/MolangStruct_RGBA.h"
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

void MolangMap::setVec3(char const* name, Vec3 const& value) {
    MolangScriptArg const arg{MolangMemberArray(MolangStruct_XYZ::FromVec3, value)};
    get().setMolangVariable(HashedString(name), arg);
}

void MolangMap::setColor(char const* name, Rgba const& color, bool alpha) {
    mce::Color const c(color.r, color.g, color.b, color.a);
    MolangScriptArg const arg = alpha ? MolangScriptArg{MolangMemberArray(MolangStruct_RGBA::FromColor, c)}
                                      : MolangScriptArg{MolangMemberArray(MolangStruct_RGB::FromColor, c)};
    get().setMolangVariable(HashedString(name), arg);
}

std::size_t MolangMap::size() const { return get().mVariables->size(); }

void fillVariables(MolangMap& map, Vec3 const& velocity) {
    if (velocity.x != 0.f || velocity.y != 0.f || velocity.z != 0.f) map.setVelocity(velocity);
}

void fillEffectVariables(MolangMap& map, std::uint32_t vars, Vec3 const& velocity, Vec3 const& direction, Rgba const& color) {
    bool const moving = velocity.x != 0.f || velocity.y != 0.f || velocity.z != 0.f;
    if (moving || (vars & kVarVelocity)) map.setVelocity(velocity);
    if (vars & kVarDirection) map.setVec3("variable.direction", direction);
    if (vars & kVarColorRgba) map.setColor("variable.color", color, true);
    else if (vars & kVarColorRgb) map.setColor("variable.color", color, false);
    if (vars & kVarNoteColor) map.setColor("variable.note_color", noteColor(velocity.x), false);
}

Rgba noteColor(float xSpeed) {
    // NoteParticle: each channel max(0, sin((x + k / 3) * 2pi) * 0.65 + 0.35), k = 0, 1, 2
    constexpr float twoPi = 6.2831855f;
    auto channel = [&](float k) { return std::max(0.f, std::sin((xSpeed + k) * twoPi) * 0.65f + 0.35f); };
    return Rgba{channel(0.f), channel(0.33333334f), channel(0.6666667f), 1.f};
}

Rgba colorOf(nlohmann::json const& params, std::optional<Rgba> const& fallback) {
    if (params.is_object()) {
        auto c = params.find("color");
        if (c != params.end()) {
            if (c->is_number_integer()) {
                auto v = c->get<std::int64_t>();
                return Rgba{((v >> 16) & 0xff) / 255.f, ((v >> 8) & 0xff) / 255.f, (v & 0xff) / 255.f, 1.f};
            }
            if (c->is_array() && (c->size() == 3 || c->size() == 4) && std::all_of(c->begin(), c->end(), [](auto const& x) { return x.is_number(); })) {
                return Rgba{(*c)[0].get<float>(), (*c)[1].get<float>(), (*c)[2].get<float>(), c->size() == 4 ? (*c)[3].get<float>() : 1.f};
            }
        }
    }
    return fallback.value_or(Rgba{});
}

} // namespace gf::fx

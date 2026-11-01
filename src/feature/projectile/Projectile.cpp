// Neutral stub of the "projectile" contract (F0). F8 replaces this file (rule R18).
#include "feature/projectile/Projectile.h"

#include "core/Stub.h"

namespace gf::projectile {

void   registerType(std::string const&, Spec) { GF_STUB("projectile.registerType"); }
Actor* shoot(Actor&, std::string const&, Vec3 const&, float, float, nlohmann::json const&) {
    GF_STUB("projectile.shoot");
    return nullptr;
}
void addSteering(SteeringFn) { GF_STUB("projectile.addSteering"); }

} // namespace gf::projectile

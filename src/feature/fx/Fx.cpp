// Neutral stub of the effect part of "rules+fx" (F0). F10 replaces this file (rule R18).
#include "feature/fx/Fx.h"

#include "core/Stub.h"

namespace gf::fx {

void particle(Dimension&, std::string const&, Vec3 const&, Vec3 const&, int, nlohmann::json const&) { GF_STUB("fx.particle"); }
void sound(Dimension&, std::string const&, Vec3 const&, float, float) { GF_STUB("fx.sound"); }
void soundTo(Player&, std::string const&, float, float) { GF_STUB("fx.soundTo"); }

} // namespace gf::fx

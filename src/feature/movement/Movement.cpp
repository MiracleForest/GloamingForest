// Neutral stub of the server half of "client.input+movement" (F0). F12 replaces this file (rule R18).
#include "feature/movement/Movement.h"

#include "core/Stub.h"

namespace gf::movement {

void onInputEdge(std::function<void(Player&, Edge)>) { GF_STUB("movement.onInputEdge"); }
void applyMotion(Player&, Vec3 const&) { GF_STUB("movement.applyMotion"); }
void validate(std::string const&, ValidateFn) { GF_STUB("movement.validate"); }

} // namespace gf::movement

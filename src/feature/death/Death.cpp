// Neutral stub of the "death" contract (F0). F10 replaces this file (rule R18).
#include "feature/death/Death.h"

#include "core/Stub.h"

namespace gf::death {

void onLethal(int, LethalFn) { GF_STUB("death.onLethal"); }
void registerKeeper(int, KeeperFn) { GF_STUB("death.registerKeeper"); }
void onDropsRemaining(DropsFn) { GF_STUB("death.onDropsRemaining"); }
void onRespawn(RespawnFn) { GF_STUB("death.onRespawn"); }

} // namespace gf::death

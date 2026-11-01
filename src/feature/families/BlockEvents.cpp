// Neutral stub of the block event part of "families+blockevents" (F0). F11 replaces this file (rule R18).
#include "feature/families/BlockEvents.h"

#include "core/Stub.h"

namespace gf::blockevents {

void onRandomTick(std::string const&, RandomTickFn) { GF_STUB("blockevents.onRandomTick"); }
void onNeighborChanged(std::string const&, NeighborFn) { GF_STUB("blockevents.onNeighborChanged"); }
void onEntityInside(std::string const&, EntityFn) { GF_STUB("blockevents.onEntityInside"); }
void onSteppedOn(std::string const&, EntityFn) { GF_STUB("blockevents.onSteppedOn"); }
void onProjectileHit(std::string const&, EntityFn) { GF_STUB("blockevents.onProjectileHit"); }
void onPlaced(std::string const&, PlacedFn) { GF_STUB("blockevents.onPlaced"); }
void onRemoved(std::string const&, RemovedFn) { GF_STUB("blockevents.onRemoved"); }

} // namespace gf::blockevents

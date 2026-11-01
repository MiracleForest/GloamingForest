// Neutral stub of the "world" contract (F0). F4 replaces this file (rule R18: the header stays).
#include "feature/dimension/World.h"

#include "core/Stub.h"

namespace gf::world {

std::optional<DimensionType> twilight() {
    GF_STUB("world.twilight");
    return std::nullopt;
}

bool isTwilight(Dimension const&) {
    GF_STUB("world.isTwilight");
    return false;
}

bool isTwilight(DimensionType) {
    GF_STUB("world.isTwilight");
    return false;
}

bool teleport(Player&, DimensionType, Vec3 const&, float) {
    GF_STUB("world.teleport");
    return false;
}

double coordinateScale() { return 0.125; }

std::optional<long long> fixedTime() { return 13000; }

} // namespace gf::world

namespace gf::portal {

bool tryActivate(BlockSource&, BlockPos const&) {
    GF_STUB("portal.tryActivate");
    return false;
}

void onTeleported(TeleportedFn) { GF_STUB("portal.onTeleported"); }

std::optional<core::GlobalPos> destinationFor(Player const&) {
    GF_STUB("portal.destinationFor");
    return std::nullopt;
}

} // namespace gf::portal

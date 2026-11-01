// Neutral stub of the "landmark" contract (F0). F6 replaces this file (rule R18).
#include "feature/landmark/Landmark.h"

#include "core/Stub.h"

namespace gf::landmark {

Descriptor const* descriptor(std::string const&) {
    GF_STUB("landmark.descriptor");
    return nullptr;
}
void                 registerGenerator(std::string const&, Generator) { GF_STUB("landmark.registerGenerator"); }
std::optional<Start> startAt(int, BlockPos const&) {
    GF_STUB("landmark.startAt");
    return std::nullopt;
}
std::vector<Start> startsIntersecting(int, ChunkPos) {
    GF_STUB("landmark.startsIntersecting");
    return {};
}
bool isConquered(Start const& start) { return start.conquered; }
void setConquered(int, BlockPos const&, std::string const&, bool) { GF_STUB("landmark.setConquered"); }
bool isProtectedFor(Player const&, BlockPos const&) {
    GF_STUB("landmark.isProtectedFor");
    return false;
}
bool trySpawnHint(Player&, Start const&, BlockPos const&) {
    GF_STUB("landmark.trySpawnHint");
    return false;
}
bool generateAt(std::string const&, BlockSource&, BlockPos const&) {
    GF_STUB("landmark.generateAt");
    return false;
}
std::optional<Start> forceStart(std::string const&, GlobalPos const&) {
    GF_STUB("landmark.forceStart");
    return std::nullopt;
}
bool enforcedProgression() {
    GF_STUB("landmark.enforcedProgression");
    return false;
}
void addTerraformer(std::string const&, std::function<double(Start const&, int, int, int)>) { GF_STUB("landmark.addTerraformer"); }
bool decorationAllowed(ChunkPos, DecoKind) { return true; }

} // namespace gf::landmark

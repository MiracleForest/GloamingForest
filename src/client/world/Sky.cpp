// Neutral stub of "client.world" (F0). F12 replaces this file (rule R18).
#ifdef GF_CLIENT

#include "client/world/Sky.h"

#include "core/Stub.h"

namespace gf::client::sky {
void setProvider(int, SkyProvider) { GF_STUB("client.sky.setProvider"); }
} // namespace gf::client::sky

namespace gf::client::music {
void setProvider(MusicFn) { GF_STUB("client.music.setProvider"); }
} // namespace gf::client::music

namespace gf::client::particles {
void registerNative(std::string const&, Factory) { GF_STUB("client.particles.registerNative"); }
} // namespace gf::client::particles

namespace gf::client::maps {
std::optional<MapView> view(std::int64_t) {
    GF_STUB("client.maps.view");
    return std::nullopt;
}
void setIcon(std::string const&, std::string const&) { GF_STUB("client.maps.setIcon"); }
} // namespace gf::client::maps

namespace gf::client::camera {
void addFovModifier(FovFn) { GF_STUB("client.camera.addFovModifier"); }
void shake(float, int) { GF_STUB("client.camera.shake"); }
} // namespace gf::client::camera

#endif // GF_CLIENT

// Neutral stub of the client half of "client.input+movement" (F0). F12 replaces this file (rule R18).
#ifdef GF_CLIENT

#include "client/input/Input.h"

#include "core/Stub.h"

namespace gf::client::keys {
void registerKey(std::string const&, int, std::string const&, std::function<void()>, std::function<void()>) {
    GF_STUB("client.keys.registerKey");
}
bool down(std::string const&) {
    GF_STUB("client.keys.down");
    return false;
}
} // namespace gf::client::keys

namespace gf::client::movement {
void onLocalTick(int, std::function<void(MoveState&)>) { GF_STUB("client.movement.onLocalTick"); }
} // namespace gf::client::movement

#endif // GF_CLIENT

// Neutral stub of the "combat" contract (F0). F10 replaces this file (rule R18).
#include "feature/combat/Combat.h"

#include "core/Stub.h"

namespace gf::combat {

void onIncoming(int, IncomingFn) { GF_STUB("combat.onIncoming"); }
void onDealt(int, DealtFn) { GF_STUB("combat.onDealt"); }
bool hurt(Actor&, float, std::string const&, Actor*, Actor*) {
    GF_STUB("combat.hurt");
    return false;
}

} // namespace gf::combat

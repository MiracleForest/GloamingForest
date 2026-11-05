// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// The mining speed extension (Mining.h): one hook on Player::getDestroyProgress (outside the W0
// contracts; disclosed for A8).
#include "feature/ext/tgmove/Mining.h"

#include <exception>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/block/Block.h"

#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::ext::mining {

namespace {

std::vector<ModifierFn>& modifiers() {
    static std::vector<ModifierFn> v;
    return v;
}

LL_TYPE_INSTANCE_HOOK(MiningProgressHook, ll::memory::HookPriority::Normal, Player, &Player::getDestroyProgress, float, ::Block const& block) {
    float progress = origin(block);
    if (progress <= 0) return progress; // unbreakable / no progress
    for (auto const& fn : modifiers()) {
        try {
            progress *= fn(*this, block);
        } catch (std::exception const& e) {
            log().error("mining: a modifier failed: {}", e.what());
        }
    }
    return progress;
}

class MiningModule {
public:
    void install() { MiningProgressHook::hook(); }
    void uninstall() { MiningProgressHook::unhook(); }
};

} // namespace

void addModifier(ModifierFn fn) { modifiers().push_back(std::move(fn)); }

} // namespace gf::ext::mining

GF_REGISTER_MODULE(gf::ext::mining::MiningModule);

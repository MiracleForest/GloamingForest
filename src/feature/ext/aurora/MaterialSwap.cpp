// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// U28's contract extension (R18): see MaterialSwap.h.
#include "feature/ext/aurora/MaterialSwap.h"

#include <atomic>
#include <unordered_map>

namespace gf::feature::ext::aurora::materials {
namespace {
/// baked texture -> untinted texture (filled at install, read-only afterwards).
std::unordered_map<std::string, std::string>& swaps() {
    static std::unordered_map<std::string, std::string> m;
    return m;
}
std::atomic<int>& made() {
    static std::atomic<int> n{0};
    return n;
}
} // namespace

void swapTexture(std::string const& baked, std::string const& untinted) { swaps()[baked] = untinted; }
int  swapsMade() { return made().load(); }

} // namespace gf::feature::ext::aurora::materials

#ifdef GF_CLIENT

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/block/tessellation_pipeline/MaterialRepository.h"
#include "mc/world/level/block/TintMethod.h"
#include "mc/world/level/block/components/BlockMaterialInstance.h"
#include "mc/world/level/block/components/BlockMaterialInstancesComponent.h"

#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::ext::aurora::materials {

LL_TYPE_INSTANCE_HOOK(
    AuroraMaterialSwapHook,
    ll::memory::HookPriority::Normal,
    ClientBlockPipeline::MaterialRepository,
    &ClientBlockPipeline::MaterialRepository::initializeMaterials,
    void,
    ::BlockMaterialInstancesComponent& materialInstance,
    ::AtlasItemManager const&          atlasItemManager,
    ::TextureShiftManager const&       textureShiftManager,
    ::Experiments const&               experiments,
    bool                               fromBlockItemVisualComponent
) {
    // before the textures resolve: the block's own materials only (an item visual keeps its texture)
    if (!fromBlockItemVisualComponent && !swaps().empty()) {
        int n = 0;
        for (auto& [name, instance] : materialInstance.mMaterials.get()) {
            auto& texture = instance.mTextureName.get().mValue;
            auto  it      = swaps().find(texture);
            if (it == swaps().end()) continue;
            texture              = it->second;
            instance.mTintMethod = ::TintMethod::Water;
            n++;
        }
        if (n > 0 && made().fetch_add(n) == 0) gf::log().info("aurora: client materials swapped to the per-position tint");
    }
    origin(materialInstance, atlasItemManager, textureShiftManager, experiments, fromBlockItemVisualComponent);
}

namespace {
struct MaterialSwapModule {
    void install() { AuroraMaterialSwapHook::hook(); }
    void uninstall() { AuroraMaterialSwapHook::unhook(); }
};
} // namespace

} // namespace gf::feature::ext::aurora::materials

GF_REGISTER_MODULE(gf::feature::ext::aurora::materials::MaterialSwapModule);

#endif // GF_CLIENT

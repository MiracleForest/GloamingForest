#pragma once

// F10-internal: the registered loot conditions / functions and the explosion radius scope of the
// interpreter (LootEval.cpp) and the module (Loot.cpp).

#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/loot/Loot.h"

namespace gf::loot {

/// The condition / function registered for `id` (nullopt: none; the interpreter's own then).
[[nodiscard]] std::optional<ConditionFn> customCondition(std::string const& id);
[[nodiscard]] std::optional<FunctionFn>  customFunction(std::string const& id);

/// Whether any global loot modifier is registered.
[[nodiscard]] bool hasGlobalModifiers();
/// Runs the global modifiers over `loot` (Bedrock's own drops of vanilla blocks).
[[nodiscard]] std::vector<ItemStack> applyGlobalModifiers(std::vector<ItemStack> loot, LootContext const& ctx, std::string const& tableId);

/// The block data document erased at `pos` this tick (the block's drops roll after it went),
/// nullptr when none.
[[nodiscard]] Json const* recentlyErased(core::GlobalPos const& pos);

/// Java's EXPLOSION_RADIUS loot parameter while block drops of an explosion roll.
[[nodiscard]] std::optional<float> currentExplosionRadius();
struct ExplosionScope {
    explicit ExplosionScope(std::optional<float> radius);
    ~ExplosionScope();
    ExplosionScope(ExplosionScope const&)            = delete;
    ExplosionScope& operator=(ExplosionScope const&) = delete;

private:
    std::optional<float> mPrevious;
};

} // namespace gf::loot

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/init/TFDimension.java
//   src/main/java/twilightforest/events/EntityEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Twilight Forest dimension on Bedrock (spike S1, docs/spikes/S1.md): Bedrock 1.26.50+ has
// native custom dimensions. The behavior pack defines `twilightforest:twilight_forest`
// (assets/pack/bp/dimensions/dimension/twilight_forest.json: void generator, min_y -32, height 288);
// the engine registers it at world start, gives it an id from the world's dimension name table and
// sends its height to every client (DimensionDataPacket), so vanilla clients render it natively.
// This file adds TF's dimension_type values the definition can't express:
//   - fixed_time 13000: Dimension::getTimeOfDay of the dimension always reads time 13000 (server and
//     client mod); vanilla clients get a per-player clock (Clock.cpp);
//   - has_raids false: villages in the dimension never start a raid;
//   - EntityEvents#stopEndermenFromGrabbingBlocksInTF: the take_block goal never starts there.
// The other values need no code: coordinate_scale 0.125 (World.cpp, the teleporter), bed_works
// (custom dimensions allow beds), respawn_anchor_works false and piglin_safe false (Bedrock anchors
// only work and piglins only stay unconverted in the Nether), infiniburn #infiniburn_overworld
// (netherrack and magma burn forever in every Bedrock dimension), ultrawarm false, has_ceiling
// false, has_skylight true, natural true (the definition's defaults), min_y / height /
// logical_height (the definition). ambient_light, monster spawn light and effects belong to the
// sky / spawning units (U15, F5).

#include <atomic>
#include <string>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/scripting/modules/minecraft/dimension/ScriptCustomDimensionRegistry.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/TakeBlockGoal.h"
#include "mc/world/actor/ai/village/Village.h"
#include "mc/world/level/DimensionManager.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionDefinitionGroup.h"

#include "feature/dimension/DimensionConfig.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/World.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("dimension", gf::feature::dimension::DimensionConfig);

namespace gf::feature::dimension {

namespace {

std::atomic<int> gTwilightId{-1};

/// TF's fixed_time (dimension_type twilight_forest_type).
constexpr int kFixedTime = 13000;

std::string_view nameOf(Dimension const& d) { return *d.mName; }

} // namespace

// ---- identification ------------------------------------------------------------------------------------

int twilightId() {
    int id = gTwilightId.load(std::memory_order_relaxed);
    if (id >= 0) return id;
    auto level = ll::service::getLevel();
    if (!level) return -1;
    auto type = level->getDimensionManager().tryGetDimensionType(world::kDimensionId);
    if (!type) return -1;
    gTwilightId.store(static_cast<int>(*type), std::memory_order_relaxed);
    return static_cast<int>(*type);
}

void resetTwilightId() { gTwilightId.store(-1, std::memory_order_relaxed); }

bool isTwilightDimension(Dimension const& dimension) { return nameOf(dimension) == world::kDimensionId; }

std::optional<int> dimensionByName(std::string_view identifier) {
    if (identifier == "minecraft:overworld" || identifier == "overworld") return 0;
    if (identifier == "minecraft:the_nether" || identifier == "minecraft:nether" || identifier == "nether") return 1;
    if (identifier == "minecraft:the_end" || identifier == "the_end") return 2;
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto type = level->getDimensionManager().tryGetDimensionType(identifier);
    if (!type) return std::nullopt;
    return static_cast<int>(*type);
}

std::string dimensionName(int dim) {
    switch (dim) {
    case 0:
        return "minecraft:overworld";
    case 1:
        return "minecraft:the_nether";
    case 2:
        return "minecraft:the_end";
    default:
        break;
    }
    auto level = ll::service::getLevel();
    if (!level) return {};
    return level->getDimensionManager().getDimensionIdentifier(DimensionType(dim));
}

double coordinateScaleOf(int dim) {
    if (dim == 1) return 8.0;
    if (dim >= 0 && dim == twilightId()) return world::coordinateScale();
    return 1.0;
}

std::optional<Height> heightOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    auto d = level->getDimension(DimensionType(dim)).lock();
    if (!d) return std::nullopt;
    return Height{d->mHeightRange->mMin, d->mHeightRange->mMax};
}

namespace {

// ---- registration diagnostics (spike S1) ---------------------------------------------------------------

/// Logs the dimension definitions the engine knows and how the Twilight Forest came out.
void logRegistration(char const* when) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& dm   = level->getDimensionManager();
    auto  type = dm.tryGetDimensionType(world::kDimensionId);
    dm.mDimensionDefinitionGroup->forEachDimensionDefinition(
        [](std::string const& name, DimensionDefinitionGroup::DimensionDefinition const& def) {
            log().info(
                "dimension: definition {} (min y {}, height {}, generator {}, id {}, biome '{}')",
                name,
                def.mMinY,
                def.mHeightRange,
                static_cast<int>(def.mGeneratorType),
                def.mDimensionType->mValue,
                *def.mDefaultBiome
            );
        }
    );
    if (!type) {
        log().error("dimension: {} is not registered ({}); the Twilight Forest is unavailable", world::kDimensionId, when);
        return;
    }
    gTwilightId.store(static_cast<int>(*type), std::memory_order_relaxed);
    auto d = level->getOrCreateDimension(*type).lock();
    if (!d) {
        log().error("dimension: {} has id {} but could not be created ({})", world::kDimensionId, static_cast<int>(*type), when);
        return;
    }
    log().info(
        "dimension: {} registered as dimension {} (name '{}', y {}..{}, natural {}, bed respawn {}, sky {}) ({})",
        world::kDimensionId,
        static_cast<int>(*type),
        nameOf(*d),
        d->mHeightRange->mMin,
        d->mHeightRange->mMax,
        d->isNaturalDimension(),
        d->mayRespawnViaBed(),
        d->showSky(),
        when
    );
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    TwilightCustomDimensionReadyHook,
    ll::memory::HookPriority::Normal,
    ScriptModuleMinecraft::ScriptCustomDimensionRegistry,
    &ScriptModuleMinecraft::ScriptCustomDimensionRegistry::_onReadyForCustomDimensionRegistration,
    void,
    DimensionManager& manager
) {
    origin(manager);
    auto type = manager.tryGetDimensionType(world::kDimensionId);
    log().info(
        "dimension: ready for custom dimension registration; {} {}",
        world::kDimensionId,
        type ? "has id " + std::to_string(static_cast<int>(*type)) : std::string("is not known yet")
    );
}

LL_TYPE_INSTANCE_HOOK(
    TwilightRegisterWithFactoryHook,
    ll::memory::HookPriority::Normal,
    DimensionManager,
    &DimensionManager::_registerCustomDimensionWithFactory,
    void,
    std::string_view name,
    DimensionType    type
) {
    origin(name, type);
    if (name == world::kDimensionId) log().info("dimension: {} registered with the dimension factory as {}", name, static_cast<int>(type));
}

// ---- fixed time ----------------------------------------------------------------------------------------------

/// TF's fixed_time: the dimension's time of day never moves (server lighting / spawning and the
/// client mod's sky). Runs on any thread: no level access, only the dimension's own identifier.
/// Vanilla clients get the same through their own clock (Clock.cpp).
LL_TYPE_INSTANCE_HOOK(
    TwilightTimeOfDayHook,
    ll::memory::HookPriority::Normal,
    Dimension,
    &Dimension::$getTimeOfDay,
    float,
    int   time,
    float a
) {
    if (isTwilightDimension(*this)) return origin(kFixedTime, 0.0f);
    return origin(time, a);
}

// ---- endermen ----------------------------------------------------------------------------------------------------

/// EntityEvents#stopEndermenFromGrabbingBlocksInTF (ExtendedEndermanTakeBlockGoal#canUse).
LL_TYPE_INSTANCE_HOOK(TwilightTakeBlockGoalHook, ll::memory::HookPriority::Normal, TakeBlockGoal, &TakeBlockGoal::$canUse, bool) {
    if (!origin()) return false;
    Mob& mob = mMob;
    if (mob.getTypeName() != "minecraft:enderman") return true;
    int tf = gTwilightId.load(std::memory_order_relaxed);
    return tf < 0 || static_cast<int>(mob.getDimensionId()) != tf;
}

// ---- raids --------------------------------------------------------------------------------------------------------

/// dimension_type has_raids false (Raids#createOrExtendRaid returns null when the dimension type has
/// no raids): a village in the Twilight Forest never starts a raid.
LL_TYPE_INSTANCE_HOOK(TwilightRaidHook, ll::memory::HookPriority::Normal, Village, &Village::triggerRaid, void) {
    Dimension& dimension = mDimension;
    if (isTwilightDimension(dimension)) {
        static std::atomic<bool> told{false}; // the engine may ask again every tick
        if (!told.exchange(true)) log().info("dimension: no raid in the Twilight Forest (has_raids false)");
        return;
    }
    origin();
}

// ---- module --------------------------------------------------------------------------------------------------

class DimensionModule {
public:
    /// Registration and time hooks go in before the level exists (BDS creates it before mods are
    /// enabled; the client creates one per local world).
    void load() {
        TwilightCustomDimensionReadyHook::hook();
        TwilightRegisterWithFactoryHook::hook();
        TwilightTimeOfDayHook::hook();
        // before the world DB module installs: on BDS it reports the (already loaded) world as
        // ready during its own install, ahead of this module's
        game::db::onWorldReady([] {
            resetTwilightId();
            logRegistration("world ready");
        });
        game::db::onWorldClosed([] { resetTwilightId(); });
    }

    void install() {
        TwilightTakeBlockGoalHook::hook();
        TwilightRaidHook::hook();
    }

    void uninstall() {
        TwilightRaidHook::unhook();
        TwilightTakeBlockGoalHook::unhook();
        // the load() hooks stay for the life of the process (registration happens per world)
    }
};

} // namespace gf::feature::dimension

GF_REGISTER_MODULE_ORDERED(gf::feature::dimension::DimensionModule, gf::ModuleOrder::Contracts);

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
//     client mod), and players inside it get the time 13000 with the daylight cycle stopped through
//     their own SetTimePacket / GameRulesChangedPacket (vanilla clients: per-player time);
//   - EntityEvents#stopEndermenFromGrabbingBlocksInTF: the take_block goal never starts there.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/GameRulesChangedPacket.h"
#include "mc/network/packet/SetTimePacket.h"
#include "mc/scripting/modules/minecraft/dimension/ScriptCustomDimensionRegistry.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/TakeBlockGoal.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/DimensionManager.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionDefinitionGroup.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

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

// ---- per-player time (vanilla clients) ------------------------------------------------------------------------

namespace {

thread_local bool gSendingTime = false;

/// The player's clock as it should read: the level time with the time of day 13000 in the Twilight
/// Forest (the day count is kept for the "days played" display), the level time elsewhere.
int clockFor(Player const& player, int levelTime) {
    if (!world::isTwilight(player.getDimensionId())) return levelTime;
    return levelTime - (levelTime % 24000) + kFixedTime;
}

void sendTime(Player& player, int time) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTime);
    if (!packet) return;
    static_cast<SetTimePacket&>(*packet).mTime = time;
    gSendingTime = true;
    packet->sendTo(player);
    gSendingTime = false;
}

/// Tells the player's client whether its clock runs (doDaylightCycle), without touching the world.
void sendDaylightCycle(Player& player, bool cycle) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& rules = *level->getGameRules().mGameRules;
    auto  index = static_cast<std::size_t>(GameRules::GameRulesIndex::DoDaylightCycle);
    if (index >= rules.size()) return;
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::GameRulesChanged);
    if (!packet) return;
    GameRule rule(rules[index]);
    rule.mValue = GameRule::Value(cycle);
    static_cast<GameRulesChangedPacket&>(*packet).mRuleData->mRules->push_back(rule);
    gSendingTime = true;
    packet->sendTo(player);
    gSendingTime = false;
}

/// A rules packet that sets doDaylightCycle (it would restart the clock of a Twilight player).
bool setsDaylightCycle(Packet const& packet) {
    if (packet.getId() != MinecraftPacketIds::GameRulesChanged) return false;
    for (auto const& rule : *static_cast<GameRulesChangedPacket const&>(packet).mRuleData->mRules) {
        std::string name = *rule.mName;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "dodaylightcycle") return true;
    }
    return false;
}

std::mutex&                       timeMutex() {
    static std::mutex m;
    return m;
}
/// Players whose client currently runs the Twilight clock (unique ids, rule R16).
std::unordered_set<std::int64_t>& inTwilightClock() {
    static std::unordered_set<std::int64_t> s;
    return s;
}

/// After the server sent its time to some players, corrects it for the ones in the Twilight Forest.
void correctTimeAfterSend(Packet const& packet, Player* only) {
    if (gSendingTime) return;
    bool time  = packet.getId() == MinecraftPacketIds::SetTime;
    bool rules = !time && setsDaylightCycle(packet);
    if (!time && !rules) return;
    int tf = gTwilightId.load(std::memory_order_relaxed);
    if (tf < 0) return;
    int  levelTime = time ? static_cast<SetTimePacket const&>(packet).mTime : 0;
    auto fix       = [&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != tf) return;
        if (time) sendTime(p, clockFor(p, levelTime));
        else sendDaylightCycle(p, false);
    };
    if (only) {
        fix(*only);
        return;
    }
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([&](Player& p) {
        fix(p);
        return true;
    });
}

Player* playerOfUser(UserEntityIdentifierComponent const* user) {
    if (!user) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Player* found = nullptr;
    level->forEachPlayer([&](Player& p) {
        if (&p.getUserEntityIdentifier() == user) {
            found = &p;
            return false;
        }
        return true;
    });
    return found;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    TwilightTimeBroadcastHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(Packet const&)>(&LoopbackPacketSender::$sendBroadcast),
    void,
    Packet const& packet
) {
    origin(packet);
    correctTimeAfterSend(packet, nullptr);
}

LL_TYPE_INSTANCE_HOOK(
    TwilightTimeToClientHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(UserEntityIdentifierComponent const*, Packet const&)>(&LoopbackPacketSender::$sendToClient),
    void,
    UserEntityIdentifierComponent const* user,
    Packet const&                        packet
) {
    origin(user, packet);
    if (!gSendingTime && (packet.getId() == MinecraftPacketIds::SetTime || packet.getId() == MinecraftPacketIds::GameRulesChanged)) {
        if (auto* p = playerOfUser(user)) correctTimeAfterSend(packet, p);
    }
}

namespace {

/// Every tick: players who entered the Twilight Forest get its clock, players who left get the
/// world's clock back.
void syncClocks(Level& level) {
    int tf = gTwilightId.load(std::memory_order_relaxed);
    std::vector<std::pair<std::int64_t, bool>> changes;
    level.forEachPlayer([&](Player& p) {
        auto id   = idOf(p);
        bool inTf = tf >= 0 && static_cast<int>(p.getDimensionId()) == tf;
        bool was;
        {
            std::lock_guard lock(timeMutex());
            was = inTwilightClock().contains(id);
        }
        if (inTf != was) changes.emplace_back(id, inTf);
        return true;
    });
    for (auto [id, inTf] : changes) {
        auto* p = playerById(id);
        if (!p) continue;
        {
            std::lock_guard lock(timeMutex());
            if (inTf) inTwilightClock().insert(id);
            else inTwilightClock().erase(id);
        }
        GameRuleId daylight;
        daylight.mValue = static_cast<int>(GameRules::GameRulesIndex::DoDaylightCycle);
        bool cycle      = level.getGameRules().getBool(daylight, true);
        sendDaylightCycle(*p, inTf ? false : cycle);
        sendTime(*p, clockFor(*p, level.getTime()));
    }
}

} // namespace

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
            {
                std::lock_guard lock(timeMutex());
                inTwilightClock().clear();
            }
            logRegistration("world ready");
        });
        game::db::onWorldClosed([] {
            resetTwilightId();
            std::lock_guard lock(timeMutex());
            inTwilightClock().clear();
        });
    }

    void install() {
        TwilightTakeBlockGoalHook::hook();
        TwilightTimeBroadcastHook::hook();
        TwilightTimeToClientHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [](ll::event::ServerLevelTickEvent& ev) { syncClocks(ev.level()); }
        );
    }

    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        TwilightTimeToClientHook::unhook();
        TwilightTimeBroadcastHook::unhook();
        TwilightTakeBlockGoalHook::unhook();
        // the load() hooks stay for the life of the process (registration happens per world)
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace gf::feature::dimension

GF_REGISTER_MODULE_ORDERED(gf::feature::dimension::DimensionModule, gf::ModuleOrder::Contracts);

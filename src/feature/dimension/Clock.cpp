// The per-player clock of unit F4 (spike S1, docs/spikes/S1.md).
//
// upstream's dimension_type gloaming_forest_type has fixed_time 13000. The server and the client mod read
// it through the Dimension::getTimeOfDay hook (Dimension.cpp); a vanilla Bedrock client renders its
// sky from its own clock, which the server keeps in step through three packets:
//   - SetTimePacket (the level time),
//   - SyncWorldClocksPacket (1.26: the world clock registry; the "overworld" clock is the one the
//     sky follows; InitializeRegistryData at join, SyncStateData when a clock changes or is synced),
//   - GameRulesChangedPacket (doDaylightCycle: whether the client runs its clock on its own).
// A player in the Gloaming Forest gets all three with the time of day 13000 and the clock stopped:
//   - packets addressed to one player (ServerPlayer::sendNetworkPacket) are corrected in place before
//     they are sent and restored afterwards (the same packet object may go to other players next);
//   - packets the LoopbackPacketSender sends to several players (broadcasts, lists) are followed by a
//     corrected packet to each Gloaming player, in the same tick and network batch;
//   - every tick the players who entered / left the dimension get the Gloaming / the world's clock,
//     and every 5 s the Gloaming clock is sent again.
// What actually leaves for each client (NetworkSystem::_sendInternal) is recorded per connection, so
// selftests and the e2e harness can check the clock a client ends each tick with
// (dimension.clock_watch).

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/NetworkIdentifierWithSubId.h"
#include "mc/network/NetworkSystem.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/DimensionDataPacket.h"
#include "mc/network/packet/GameRulesChangedPacket.h"
#include "mc/network/packet/SetTimePacket.h"
#include "mc/network/packet/SyncWorldClocksPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/clock/VanillaWorldClocks.h"
#include "mc/world/level/clock/WorldClockData.h"
#include "mc/world/level/clock/WorldClockRegistry.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "feature/dimension/Clock.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/World.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::dimension {

namespace {

/// upstream's fixed_time (dimension_type gloaming_forest_type).
constexpr int kFixedTime = 13000;

thread_local bool          gSendingTime = false; ///< our own corrections are being sent
std::atomic<std::uint64_t> gOverworldClock{0};

using SyncState = SyncWorldClocksPacketPayload::SyncStateData;
using InitData  = SyncWorldClocksPacketPayload::InitializeRegistryData;

std::string const& overworldClockName() { return VanillaWorldClocks::OVERWORLD_CLOCK_NAME().getString(); }

/// The overworld clock's id as far as known, without looking it up (any thread).
std::uint64_t knownOverworldClockId() { return gOverworldClock.load(std::memory_order_relaxed); }

/// The overworld clock's time and paused flag in a SyncWorldClocks packet (nullopt: not in it).
/// `resolve`: look the clock's id up when it is not known yet (server thread only).
std::optional<std::pair<int, bool>> overworldEntry(SyncWorldClocksPacket const& packet, bool resolve = true) {
    auto        id   = resolve ? overworldClockId() : knownOverworldClockId();
    auto const& data = packet.mData.get();
    if (auto const* s = std::get_if<SyncState>(&data)) {
        for (auto const& e : s->mClockData.get())
            if (id != 0 && e.mClockId == id) return std::pair{static_cast<int>(e.mTime), static_cast<bool>(e.mIsPaused)};
    } else if (auto const* init = std::get_if<InitData>(&data)) {
        for (auto const& c : init->mClockData.get())
            if ((id != 0 && c.mId == id) || c.mName.get() == overworldClockName()) {
                return std::pair{static_cast<int>(c.mTime), static_cast<bool>(c.mIsPaused)};
            }
    }
    return std::nullopt;
}

/// A rules packet that sets doDaylightCycle; its value.
std::optional<bool> daylightCycleIn(Packet const& packet) {
    if (packet.getId() != MinecraftPacketIds::GameRulesChanged) return std::nullopt;
    for (auto const& rule : *static_cast<GameRulesChangedPacket const&>(packet).mRuleData->mRules) {
        std::string name = *rule.mName;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "dodaylightcycle") continue;
        if (auto const* b = std::get_if<bool>(&rule.mValue.get())) return *b;
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

// ---- values ----------------------------------------------------------------------------------------------

int timeOfDay(int time) {
    int r = time % 24000;
    return r < 0 ? r + 24000 : r;
}

int gloamingClock(int time) { return time - timeOfDay(time) + kFixedTime; }

std::uint64_t overworldClockId() {
    if (auto id = gOverworldClock.load(std::memory_order_relaxed)) return id;
    auto level = ll::service::getLevel();
    if (!level) return 0;
    for (auto const& c : level->getWorldClockRegistry()->toData()) {
        if (c.mName.get() == overworldClockName()) {
            gOverworldClock.store(c.mId, std::memory_order_relaxed);
            return c.mId;
        }
    }
    return 0;
}

std::optional<std::pair<int, bool>> overworldClockState() {
    auto level = ll::service::getLevel();
    if (!level) return std::nullopt;
    for (auto const& c : level->getWorldClockRegistry()->toData())
        if (c.mName.get() == overworldClockName()) return std::pair{static_cast<int>(c.mTime), static_cast<bool>(c.mIsPaused)};
    return std::nullopt;
}

namespace {
TimePacket kindOf(Packet const& packet, bool resolve) {
    switch (packet.getId()) {
    case MinecraftPacketIds::SetTime:
        return TimePacket::SetTime;
    case MinecraftPacketIds::SyncWorldClocks:
        return overworldEntry(static_cast<SyncWorldClocksPacket const&>(packet), resolve) ? TimePacket::WorldClocks : TimePacket::None;
    case MinecraftPacketIds::GameRulesChanged:
        return daylightCycleIn(packet) ? TimePacket::DaylightRule : TimePacket::None;
    default:
        return TimePacket::None;
    }
}
} // namespace

TimePacket timePacketKind(Packet const& packet) { return kindOf(packet, true); }

char const* timePacketName(TimePacket kind) {
    switch (kind) {
    case TimePacket::SetTime:
        return "SetTime";
    case TimePacket::WorldClocks:
        return "SyncWorldClocks";
    case TimePacket::DaylightRule:
        return "GameRulesChanged(doDaylightCycle)";
    default:
        return "none";
    }
}

std::function<void()> fixForGloaming(Packet& packet) {
    switch (timePacketKind(packet)) {
    case TimePacket::SetTime: {
        auto& p   = static_cast<SetTimePacket&>(packet);
        int   old = p.mTime;
        int   now = gloamingClock(old);
        if (now == old) return {};
        p.mTime = now;
        return [&p, old] { p.mTime = old; };
    }
    case TimePacket::WorldClocks: {
        auto& p    = static_cast<SyncWorldClocksPacket&>(packet);
        auto  id   = overworldClockId();
        auto& data = p.mData.get();
        if (auto* s = std::get_if<SyncState>(&data)) {
            std::vector<std::tuple<std::size_t, int, bool>> undo;
            auto& list = s->mClockData.get();
            for (std::size_t i = 0; i < list.size(); ++i) {
                auto& e = list[i];
                if (id == 0 || e.mClockId != id) continue;
                if (e.mTime == gloamingClock(e.mTime) && e.mIsPaused) continue;
                undo.emplace_back(i, e.mTime, e.mIsPaused);
                e.mTime     = gloamingClock(e.mTime);
                e.mIsPaused = true;
            }
            if (undo.empty()) return {};
            return [s, undo = std::move(undo)] {
                auto& l = s->mClockData.get();
                for (auto const& [i, t, paused] : undo)
                    if (i < l.size()) l[i].mTime = t, l[i].mIsPaused = paused;
            };
        }
        if (auto* init = std::get_if<InitData>(&data)) {
            std::vector<std::tuple<std::size_t, int, bool>> undo;
            auto& list = init->mClockData.get();
            for (std::size_t i = 0; i < list.size(); ++i) {
                auto& c = list[i];
                if (!((id != 0 && c.mId == id) || c.mName.get() == overworldClockName())) continue;
                if (c.mTime == gloamingClock(c.mTime) && c.mIsPaused) continue;
                undo.emplace_back(i, c.mTime, c.mIsPaused);
                c.mTime     = gloamingClock(c.mTime);
                c.mIsPaused = true;
            }
            if (undo.empty()) return {};
            return [init, undo = std::move(undo)] {
                auto& l = init->mClockData.get();
                for (auto const& [i, t, paused] : undo)
                    if (i < l.size()) l[i].mTime = t, l[i].mIsPaused = paused;
            };
        }
        return {};
    }
    case TimePacket::DaylightRule: {
        std::vector<GameRule::Value*> changed;
        for (auto& rule : *static_cast<GameRulesChangedPacket&>(packet).mRuleData->mRules) {
            std::string name = *rule.mName;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name != "dodaylightcycle") continue;
            auto& v = rule.mValue.get();
            if (auto* b = std::get_if<bool>(&v); b && *b) {
                *b = false;
                changed.push_back(&v);
            }
        }
        if (changed.empty()) return {};
        return [changed] {
            for (auto* v : changed) *v = GameRule::Value(true);
        };
    }
    default:
        return {};
    }
}

namespace {

// ---- sending ----------------------------------------------------------------------------------------------

void sendTime(Player& player, int time) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTime);
    if (!packet) return;
    static_cast<SetTimePacket&>(*packet).mTime = time;
    gSendingTime = true;
    packet->sendTo(player);
    gSendingTime = false;
}

/// The overworld world clock of one client (the other clocks are left alone).
void sendClock(Player& player, int time, bool paused) {
    auto id = overworldClockId();
    if (id == 0) return;
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SyncWorldClocks);
    if (!packet) return;
    SyncWorldClockStateData entry{};
    entry.mClockId  = id;
    entry.mTime     = time;
    entry.mIsPaused = paused;
    auto& state = static_cast<SyncWorldClocksPacket&>(*packet).mData.get().emplace<SyncState>();
    state.mClockData->push_back(entry);
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

/// Sends the Gloaming clock (inGloaming) or the world's clock to one player.
void sendClockOf(Player& player, Level& level, bool inGloaming) {
    GameRuleId daylight;
    daylight.mValue = static_cast<int>(GameRules::GameRulesIndex::DoDaylightCycle);
    bool cycle      = level.getGameRules().getBool(daylight, true);
    sendDaylightCycle(player, inGloaming ? false : cycle);
    int levelTime = level.getTime();
    sendTime(player, inGloaming ? gloamingClock(levelTime) : levelTime);
    if (auto clock = overworldClockState()) {
        sendClock(player, inGloaming ? gloamingClock(clock->first) : clock->first, inGloaming ? true : clock->second);
    }
}

// ---- which players ----------------------------------------------------------------------------------------

bool inGloaming(Player const& p) {
    int upsrc = gloamingId();
    return upsrc >= 0 && static_cast<int>(p.getDimensionId()) == upsrc;
}

Player* playerOfNetwork(NetworkIdentifier const& id, SubClientId sub) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    Player* found = nullptr;
    level->forEachPlayer([&](Player& p) {
        if (p.getNetworkIdentifier() == id && p.getClientSubId() == sub) {
            found = &p;
            return false;
        }
        return true;
    });
    return found;
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

// ---- evidence ----------------------------------------------------------------------------------------------

std::mutex& noteMutex() {
    static std::mutex m;
    return m;
}
std::set<std::string>& notes() {
    static std::set<std::string> s;
    return s;
}

/// One line per packet kind and path per world: which time packets reached Gloaming players.
void noteCorrection(TimePacket kind, char const* path, std::string const& player) {
    std::string key = std::string(timePacketName(kind)) + "|" + path;
    {
        std::lock_guard lock(noteMutex());
        if (!notes().insert(key).second) return;
    }
    log().info("dimension: corrected a {} packet for {} in the Gloaming Forest (via {})", timePacketName(kind), player, path);
}

/// After the server sent a time packet to some players: corrects it for the ones in the Gloaming
/// Forest (`only`: that player; else every player but `except`).
void correctAfterSend(Packet const& packet, char const* path, Player* only, Player const* except = nullptr) {
    if (gSendingTime) return;
    auto kind = timePacketKind(packet);
    if (kind == TimePacket::None || gloamingId() < 0) return;
    auto fix = [&](Player& p) {
        if (!inGloaming(p)) return;
        noteCorrection(kind, path, p.getRealName());
        switch (kind) {
        case TimePacket::SetTime:
            sendTime(p, gloamingClock(static_cast<SetTimePacket const&>(packet).mTime));
            break;
        case TimePacket::WorldClocks:
            if (auto e = overworldEntry(static_cast<SyncWorldClocksPacket const&>(packet))) sendClock(p, gloamingClock(e->first), true);
            break;
        case TimePacket::DaylightRule:
            sendDaylightCycle(p, false);
            break;
        default:
            break;
        }
    };
    if (only) {
        fix(*only);
        return;
    }
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<Player*> targets;
    level->forEachPlayer([&](Player& p) {
        if (&p != except) targets.push_back(&p);
        return true;
    });
    for (auto* p : targets) fix(*p);
}

// ---- what each client was sent ------------------------------------------------------------------------------

std::mutex& wireMutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<NetworkIdentifier, ClientClock>& wire() {
    static std::unordered_map<NetworkIdentifier, ClientClock> m;
    return m;
}

/// The dimension definitions a client is sent when it joins (DimensionDataPacket): the Gloaming
/// Forest's height range as the client will build the dimension (the e2e checks it for vanilla
/// clients, which log nothing themselves).
void noteDimensionData(DimensionDataPacket const& packet) {
    auto const& defs = packet.mDimensionDefinitionGroup->mDimensionDefinitions.get();
    auto        it   = defs.find(world::kDimensionId);
    if (it == defs.end()) {
        log().warn("dimension: the dimension definitions sent to a client lack {} ({} definitions)", world::kDimensionId, defs.size());
        return;
    }
    auto const& d = it->second;
    log().info(
        "dimension: the dimension definitions sent to a client give {} y {}..{} (height {})",
        world::kDimensionId,
        d.mMinY,
        d.mMinY + d.mHeightRange,
        d.mHeightRange
    );
}

void recordWire(NetworkIdentifier const& id, Packet const& packet) {
    // (no level access here: the network may send from another thread)
    if (packet.getId() == MinecraftPacketIds::DimensionDataPacket) {
        noteDimensionData(static_cast<DimensionDataPacket const&>(packet));
        return;
    }
    auto kind = kindOf(packet, false);
    if (kind == TimePacket::None) return;
    std::lock_guard lock(wireMutex());
    auto& c = wire()[id];
    ++c.packets;
    switch (kind) {
    case TimePacket::SetTime:
        c.timeOfDay  = timeOfDay(static_cast<SetTimePacket const&>(packet).mTime);
        c.lastPacket = "SetTime";
        break;
    case TimePacket::WorldClocks:
        if (auto e = overworldEntry(static_cast<SyncWorldClocksPacket const&>(packet), false)) {
            c.timeOfDay   = timeOfDay(e->first);
            c.clockPaused = e->second;
            c.lastPacket  = "SyncWorldClocks";
        }
        break;
    case TimePacket::DaylightRule:
        if (auto v = daylightCycleIn(packet)) c.daylightCycle = *v;
        break;
    default:
        break;
    }
}

std::mutex& timeMutex() {
    static std::mutex m;
    return m;
}
/// Players whose client currently runs the Gloaming clock (unique ids, rule R16).
std::unordered_set<std::int64_t>& inGloamingClock() {
    static std::unordered_set<std::int64_t> s;
    return s;
}
std::unordered_set<std::int64_t>& warned() {
    static std::unordered_set<std::int64_t> s;
    return s;
}

/// Every tick: players who entered the Gloaming Forest get its clock, players who left get the
/// world's clock back; every 5 s the clock of the players inside is sent again. Then the clock each
/// Gloaming player's client ended the last tick with is checked.
void syncClocks(Level& level) {
    static int ticks = 0;
    bool       renew = ++ticks % 100 == 0;
    (void)overworldClockId(); // known from the first tick on (the network side only reads it)
    std::vector<std::pair<std::int64_t, bool>> changes;
    std::vector<std::int64_t>                  staying;
    level.forEachPlayer([&](Player& p) {
        auto id   = idOf(p);
        bool inside = inGloaming(p);
        bool was;
        {
            std::lock_guard lock(timeMutex());
            was = inGloamingClock().contains(id);
        }
        if (inside && was) staying.push_back(id);
        if (inside != was || (inside && renew)) changes.emplace_back(id, inside);
        return true;
    });
    // the clock the clients of players who stayed inside ended the last tick with
    for (auto id : staying) {
        auto* p = playerById(id);
        if (!p) continue;
        std::lock_guard lock(wireMutex());
        auto            it = wire().find(p->getNetworkIdentifier());
        if (it == wire().end() || it->second.timeOfDay < 0) continue;
        auto& c = it->second;
        if (c.timeOfDay == kFixedTime) continue;
        if (c.wrongTicks++ == 0) c.wrongPacket = c.lastPacket;
        if (warned().insert(id).second) {
            log().warn(
                "dimension: the client clock of {} reads time of day {} in the Gloaming Forest (last: {})",
                p->getRealName(),
                c.timeOfDay,
                c.lastPacket
            );
        }
    }
    for (auto [id, inside] : changes) {
        auto* p = playerById(id);
        if (!p) continue;
        {
            std::lock_guard lock(timeMutex());
            if (inside) inGloamingClock().insert(id);
            else inGloamingClock().erase(id);
        }
        sendClockOf(*p, level, inside);
    }
}

void resetState() {
    gOverworldClock.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(timeMutex());
        inGloamingClock().clear();
        warned().clear();
    }
    {
        std::lock_guard lock(wireMutex());
        wire().clear();
    }
    std::lock_guard lock(noteMutex());
    notes().clear();
}

} // namespace

std::optional<ClientClock> clientClock(Player const& player) {
    std::lock_guard lock(wireMutex());
    auto            it = wire().find(player.getNetworkIdentifier());
    if (it == wire().end()) return std::nullopt;
    return it->second;
}

void resetClientClockChecks(Player const& player) {
    std::lock_guard lock(wireMutex());
    auto            it = wire().find(player.getNetworkIdentifier());
    if (it == wire().end()) return;
    it->second.packets     = 0;
    it->second.wrongTicks  = 0;
    it->second.wrongPacket = {};
}

// ---- hooks ---------------------------------------------------------------------------------------------------

/// Packets for one player: corrected before they leave, restored afterwards.
LL_TYPE_INSTANCE_HOOK(
    GloamingTimeToPlayerHook,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$sendNetworkPacket,
    void,
    Packet& packet
) {
    if (gSendingTime || !inGloaming(*this) || timePacketKind(packet) == TimePacket::None) return origin(packet);
    auto kind = timePacketKind(packet);
    auto undo = fixForGloaming(packet);
    if (undo) noteCorrection(kind, "sendNetworkPacket", getRealName());
    origin(packet);
    if (undo) undo();
}

LL_TYPE_INSTANCE_HOOK(
    GloamingTimeBroadcastHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(Packet const&)>(&LoopbackPacketSender::$sendBroadcast),
    void,
    Packet const& packet
) {
    origin(packet);
    correctAfterSend(packet, "sendBroadcast", nullptr);
}

LL_TYPE_INSTANCE_HOOK(
    GloamingTimeToClientHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(UserEntityIdentifierComponent const*, Packet const&)>(&LoopbackPacketSender::$sendToClient),
    void,
    UserEntityIdentifierComponent const* user,
    Packet const&                        packet
) {
    origin(user, packet);
    if (!gSendingTime && timePacketKind(packet) != TimePacket::None) {
        if (auto* p = playerOfUser(user)) correctAfterSend(packet, "sendToClient(user)", p);
    }
}

LL_TYPE_INSTANCE_HOOK(
    GloamingTimeToNetworkHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(NetworkIdentifier const&, Packet const&, SubClientId)>(&LoopbackPacketSender::$sendToClient),
    void,
    NetworkIdentifier const& id,
    Packet const&            packet,
    SubClientId              sub
) {
    origin(id, packet, sub);
    if (!gSendingTime && timePacketKind(packet) != TimePacket::None) {
        if (auto* p = playerOfNetwork(id, sub)) correctAfterSend(packet, "sendToClient(id)", p);
    }
}

LL_TYPE_INSTANCE_HOOK(
    GloamingTimeToClientsHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$sendToClients,
    void,
    std::vector<NetworkIdentifierWithSubId> const& ids,
    Packet const&                                  packet
) {
    origin(ids, packet);
    if (!gSendingTime && timePacketKind(packet) != TimePacket::None) {
        for (auto const& target : ids)
            if (auto* p = playerOfNetwork(target.id, target.subClientId)) correctAfterSend(packet, "sendToClients", p);
    }
}

LL_TYPE_INSTANCE_HOOK(
    GloamingTimeBroadcastExceptHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(NetworkIdentifier const&, SubClientId, Packet const&)>(&LoopbackPacketSender::$sendBroadcast),
    void,
    NetworkIdentifier const& exceptId,
    SubClientId              exceptSub,
    Packet const&            packet
) {
    origin(exceptId, exceptSub, packet);
    if (!gSendingTime && timePacketKind(packet) != TimePacket::None) {
        correctAfterSend(packet, "sendBroadcast(except)", nullptr, playerOfNetwork(exceptId, exceptSub));
    }
}

/// What leaves for each connection (observation only).
LL_TYPE_INSTANCE_HOOK(
    GloamingTimeWireHook,
    ll::memory::HookPriority::Normal,
    NetworkSystem,
    &NetworkSystem::_sendInternal,
    void,
    NetworkIdentifier const& id,
    Packet const&            packet,
    std::string const&       data
) {
    origin(id, packet, data);
    recordWire(id, packet);
}

class ClockModule {
public:
    void load() {
        game::db::onWorldReady([] { resetState(); });
        game::db::onWorldClosed([] { resetState(); });
    }

    void install() {
        GloamingTimeToPlayerHook::hook();
        GloamingTimeBroadcastHook::hook();
        GloamingTimeToClientHook::hook();
        GloamingTimeToNetworkHook::hook();
        GloamingTimeToClientsHook::hook();
        GloamingTimeBroadcastExceptHook::hook();
        GloamingTimeWireHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent& ev) { syncClocks(ev.level()); });
        // a player who comes back still in the Gloaming Forest gets its clock again (StartGame
        // carries the level time)
        mLeave = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            {
                std::lock_guard lock(timeMutex());
                inGloamingClock().erase(idOf(ev.self()));
                warned().erase(idOf(ev.self()));
            }
            std::lock_guard lock(wireMutex());
            wire().erase(ev.self().getNetworkIdentifier());
        });
    }

    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        ll::event::EventBus::getInstance().removeListener(mLeave);
        GloamingTimeWireHook::unhook();
        GloamingTimeBroadcastExceptHook::unhook();
        GloamingTimeToClientsHook::unhook();
        GloamingTimeToNetworkHook::unhook();
        GloamingTimeToClientHook::unhook();
        GloamingTimeBroadcastHook::unhook();
        GloamingTimeToPlayerHook::unhook();
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mLeave;
};

} // namespace gf::feature::dimension

GF_REGISTER_MODULE_ORDERED(gf::feature::dimension::ClockModule, gf::ModuleOrder::Contracts);

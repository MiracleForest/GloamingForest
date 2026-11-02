// Server half of "client.input+movement" (F12).
//
// Bedrock movement is client-authoritative for the player's own input: the server sees one
// PlayerAuthInputPacket per client tick. ServerNetworkHandler::handle only queues the packet
// (ServerPlayerMovementComponent::mQueuedUpdates); the player's tick applies it later. This module
// turns those packets into input edges for the vanilla-client fallbacks (jump / sneak / sprint
// pressed and released, left the ground, landed), pushes motion to a player (SetActorMotionPacket,
// the server's own motion too) and runs the movement validators of the modifiers (anti-desync for
// companions whose client applies modifiers natively) on the position the client claims, before
// the packet is queued: a rejected move puts the player back where its last accepted move ended
// (feature/ext/ui/Server.h has the exact rules). Validation starts afresh when the server moves
// the player itself (Player::teleportTo, a respawn, a dimension change). The ground state of the
// edges comes from the packet (its vertical collision and the claimed y), since the server applies
// the queued packet only later.

#include "feature/movement/Movement.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/network/packet/PlayerAuthInputPacketPayload.h"
#include "mc/network/packet/PlayerInputTick.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/ext/ui/Server.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::movement {

namespace {

/// Packets a rejected player may send before it acknowledges the correction's teleport.
constexpr int kAckPackets = 40;
/// Packets a player moved by the server may send before it arrives (respawn, dimension loading).
constexpr int kArrivePackets = 100;
/// A claimed position this close to the server's means the client arrived where the server put it.
constexpr float kArriveDistance = 2.0f;
/// Server ticks between a rejection and its correction (after the queued input was applied).
constexpr int kCorrectionDelay = 2;
/// A claimed y change larger than this in one packet is a jump of the position, not a move.
constexpr float kPositionJump = 8.0f;

struct Validator {
    std::string id;
    ValidateFn  fn;
};

/// Why the moves of a player are skipped for now.
enum class Settle {
    None,
    Rejected, ///< a correction is coming: until the client acknowledges its teleport
    Arriving, ///< the server moved the player: until the client claims the server's position
};

/// Per real player: the last accepted move and whether moves are skipped.
struct Track {
    Vec3   last{};
    int    dim           = -1;
    bool   valid         = false;
    Settle settle        = Settle::None;
    int    settlePackets = 0;
    /// Arriving: where the server put the player (a teleport's destination); nullopt: the server's
    /// position at each packet (respawn, dimension change: the server places the player later).
    std::optional<Vec3> target;
    /// Arriving: where the client was when the server moved it (its last claim); a claim there is in
    /// flight from before the move, even when the move was shorter than kArriveDistance.
    Vec3 origin{};
};

struct Correction {
    std::int64_t player;
    Vec3         to;
    int          dim;
    std::string  validator;
    int          ticks;
};

struct State {
    std::vector<std::function<void(Player&, Edge)>> edgeHandlers;
    std::vector<Validator>                          validators;
    std::unordered_map<std::int64_t, InputSample>   last;   ///< per player unique id
    std::unordered_map<std::int64_t, float>         claimY; ///< the y of the player's previous packet
    std::unordered_map<std::int64_t, Track>         tracks;
    std::vector<Correction>                         pending;
    std::set<std::string>                           loggedReject;     ///< validator ids
    std::set<std::pair<std::string, std::int64_t>> loggedCorrection; ///< (validator, player)
    std::string                                     lastRejecter;     ///< set by validateMove
    std::atomic<std::size_t>                        corrections{0};
};
State& st() {
    static State s;
    return s;
}

void fire(Player& p, Edge e) {
    // copy: a handler may register another one
    auto handlers = st().edgeHandlers;
    for (auto& h : handlers) {
        try {
            h(p, e);
        } catch (std::exception const& ex) {
            log().error("movement: input edge handler failed: {}", ex.what());
        }
    }
}

} // namespace

void onInputEdge(std::function<void(Player&, Edge)> fn) {
    if (fn) st().edgeHandlers.push_back(std::move(fn));
}

void applyMotion(Player& player, Vec3 const& velocity) {
    player.lerpMotion(velocity); // the server's own copy (and SimulatedPlayers)
    if (player.isSimulated()) return;
    auto  packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
    auto& motion = static_cast<SetActorMotionPacket&>(*packet);
    motion.mRuntimeId = player.getRuntimeID();
    motion.mMotion    = velocity;
    motion.mTick      = PlayerInputTick(player.getEntityContext());
    packet->sendTo(player);
}

void validate(std::string const& id, ValidateFn fn) {
    if (!fn) return;
    auto& v = st().validators;
    for (auto& e : v) {
        if (e.id == id) {
            e.fn = std::move(fn);
            return;
        }
    }
    v.push_back({id, std::move(fn)});
}

bool removeValidator(std::string const& id) {
    auto& s = st();
    auto  n = std::erase_if(s.validators, [&id](Validator const& v) { return v.id == id; });
    if (n == 0) return false;
    s.loggedReject.erase(id);
    std::erase_if(s.loggedCorrection, [&id](auto const& k) { return k.first == id; });
    std::erase_if(s.pending, [&id](Correction const& c) { return c.validator == id; }); // no correction for a rule that is gone
    return true;
}

std::size_t validatorCount() { return st().validators.size(); }

std::size_t corrections() { return st().corrections.load(); }

bool validateMove(Player& player, Vec3 const& from, Vec3 const& to) {
    auto& s = st();
    // by index, the function copied per call: a validator may register or remove one (every packet
    // of every real player comes here, so the list itself is not copied)
    for (std::size_t i = 0; i < s.validators.size(); ++i) {
        auto       fn = s.validators[i].fn;
        std::string id = s.validators[i].id;
        bool       ok = true;
        try {
            ok = fn(player, from, to);
        } catch (std::exception const& ex) {
            log().error("movement: validator {} failed: {}", id, ex.what());
        }
        if (!ok) {
            s.lastRejecter = id;
            if (s.loggedReject.insert(id).second) log().info("movement: {} rejected a move of {}", id, player.getRealName());
            return false;
        }
    }
    return true;
}

std::optional<bool> groundFromInput(bool verticalCollision, float claimedDy, bool teleport) {
    if (teleport || std::abs(claimedDy) > kPositionJump) return std::nullopt;
    // a collision while the claimed (eye) y rose: a ceiling, a step up or the eye rising when the
    // player stops sneaking; the next packet tells
    if (verticalCollision && claimedDy > 0.001f) return std::nullopt;
    return verticalCollision;
}

void resetTrack(Player& player, ResetReason reason) {
    auto& s  = st();
    auto  id = player.getOrCreateUniqueID().rawID;
    std::erase_if(s.pending, [id](Correction const& c) { return c.player == id; });
    s.claimY.erase(id);
    auto& t      = s.tracks[id];
    bool  before = t.valid;
    t.origin     = t.last;
    t.valid      = true;
    t.dim        = static_cast<int>(player.getDimensionId());
    t.last       = player.getFeetPos();
    if (!before) t.origin = t.last;
    if (reason == ResetReason::Teleport && t.settle == Settle::Rejected) {
        // the correction's own teleport: its acknowledgement ends the wait (the server applies the
        // moves the client sent before it, so the server's position says nothing yet)
        t.settlePackets = 0;
        return;
    }
    t.settle        = Settle::Arriving;
    t.settlePackets = 0;
    t.target        = reason == ResetReason::Teleport ? std::optional<Vec3>(t.last) : std::nullopt;
}

bool settling(Player const& player) {
    auto& s  = st();
    auto  it = s.tracks.find(const_cast<Player&>(player).getOrCreateUniqueID().rawID);
    return it != s.tracks.end() && it->second.settle != Settle::None;
}

std::size_t pendingCorrections(std::int64_t playerId) {
    auto& p = st().pending;
    return static_cast<std::size_t>(std::count_if(p.begin(), p.end(), [playerId](Correction const& c) { return c.player == playerId; }));
}

void forgetTrack(Player& player) {
    auto& s  = st();
    auto  id = player.getOrCreateUniqueID().rawID;
    s.tracks.erase(id);
    s.last.erase(id);
    s.claimY.erase(id);
    std::erase_if(s.pending, [id](Correction const& c) { return c.player == id; });
}

void feedInput(Player& player, InputSample const& now) {
    auto  id            = player.getOrCreateUniqueID().rawID;
    auto& last          = st().last;
    auto [it, fresh]    = last.try_emplace(id, now);
    InputSample before  = fresh ? InputSample{false, false, false, now.onGround} : it->second;
    it->second          = now;
    if (now.jump != before.jump) fire(player, now.jump ? Edge::JumpPressed : Edge::JumpReleased);
    if (now.sneak != before.sneak) fire(player, now.sneak ? Edge::SneakPressed : Edge::SneakReleased);
    if (now.sprint != before.sprint) fire(player, now.sprint ? Edge::SprintStarted : Edge::SprintStopped);
    if (now.onGround != before.onGround) fire(player, now.onGround ? Edge::Landed : Edge::LeftGround);
}

namespace {

using Input = PlayerAuthInputPacketPayload::InputData;

/// Validates the move a packet claims (before it is queued); the correction target when rejected.
std::optional<Vec3> checkMove(Player& player, PlayerAuthInputPacket const& packet) {
    auto& s = st();
    if (player.isSimulated()) return std::nullopt;
    // the track is kept without validators too (one lookup per packet), so a rule registered later
    // starts from the last move the client claimed, not from a server position it has not reached
    // the packet carries the client's eye position: the validators and the correction use feet
    float eye    = player.getPosition().y - player.getFeetPos().y;
    Vec3  claim  = packet.mPos;
    Vec3  to{claim.x, claim.y - eye, claim.z};
    auto  id     = player.getOrCreateUniqueID().rawID;
    auto& t      = s.tracks[id];
    int   dim    = static_cast<int>(player.getDimensionId());
    bool  ack    = packet.mInputData->contains(Input::HandledTeleport);
    Vec3  feet   = player.getFeetPos();
    if (!t.valid) { // the first packet of this player: the server's position
        t       = Track{};
        t.valid = true;
        t.dim   = dim;
        t.last  = feet;
    }
    if (t.dim != dim || !player.isAlive()) {
        // another dimension (packets from the old one may still be in flight) or dead (the death
        // screen, then the respawn): wait until the client claims where the server put it
        if (t.dim != dim) std::erase_if(s.pending, [id](Correction const& c) { return c.player == id; });
        if (t.settle != Settle::Arriving) t.origin = t.last; // the dead / old-dimension place
        t.dim           = dim;
        t.last          = feet;
        t.settle        = Settle::Arriving;
        t.settlePackets = 0;
        t.target.reset();
        if (!player.isAlive()) return std::nullopt;
    }
    if (t.settle != Settle::None) {
        ++t.settlePackets;
        bool arrived = ack;
        if (t.settle == Settle::Arriving) {
            Vec3  target = t.target.value_or(feet);
            float near  = to.distanceTo(target);
            // near the server's place, and nearer to it than to where the client was before the move
            arrived = arrived || (near <= kArriveDistance && near <= to.distanceTo(t.origin) && target.distanceTo(t.origin) > 1e-3f)
                   || t.settlePackets > kArrivePackets;
        }
        else arrived = arrived || t.settlePackets > kAckPackets; // moves sent before the correction arrived
        if (arrived) {
            t.settle = Settle::None;
            t.last   = to;
        }
        return std::nullopt;
    }
    if (ack || s.validators.empty()) { // an acknowledged teleport of the server: the new start
        t.last = to;
        return std::nullopt;
    }
    Vec3 from = t.last;
    if (validateMove(player, from, to)) {
        t.last = to;
        return std::nullopt;
    }
    t.settle        = Settle::Rejected;
    t.settlePackets = 0;
    return from;
}

/// Server tick: the corrections whose delay ran out.
void runCorrections() {
    auto& s = st();
    if (s.pending.empty()) return;
    auto level = ll::service::getLevel();
    std::vector<Correction> due;
    for (auto it = s.pending.begin(); it != s.pending.end();) {
        if (--it->ticks <= 0) {
            due.push_back(std::move(*it));
            it = s.pending.erase(it);
        } else ++it;
    }
    for (auto const& c : due) {
        Player* p = level ? level->getPlayer(ActorUniqueID{c.player}) : nullptr; // rule R16
        if (!p || static_cast<int>(p->getDimensionId()) != c.dim || !p->isAlive()) continue;
        try {
            p->teleport(c.to, p->getDimensionId(), p->getRotation()); // LeviLamina's teleport takes feet positions, as /tp
            ++s.corrections;
            if (s.loggedCorrection.insert({c.validator, c.player}).second)
                log().info("movement: corrected {} ({}) to {:.2f} {:.2f} {:.2f}", p->getRealName(), c.validator, c.to.x, c.to.y, c.to.z);
        } catch (std::exception const& e) {
            log().error("movement: correction of {} failed: {}", p->getRealName(), e.what());
        }
    }
}

} // namespace

// One hook for the auth input (rule R8: units use onInputEdge / validate instead).
LL_TYPE_INSTANCE_HOOK(
    MovementAuthInputHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&     source,
    PlayerAuthInputPacket const& packet
) {
    Player*             player = nullptr;
    std::optional<Vec3> correctTo;
    std::string         rejecter;
    try {
        player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId);
        if (player) {
            correctTo = checkMove(*player, packet);
            if (correctTo) rejecter = st().lastRejecter;
        }
    } catch (std::exception const& e) {
        log().error("movement: move validation failed: {}", e.what());
        player = nullptr;
    } catch (...) {
        player = nullptr;
    }
    origin(source, packet);
    if (!player) return;
    try {
        auto const& in = *packet.mInputData;
        InputSample sample;
        sample.jump     = in.contains(Input::JumpDown);
        sample.sneak    = in.contains(Input::SneakDown);
        sample.sprint   = in.contains(Input::SprintDown);
        // the ground state of this packet (the server has only queued it): its vertical collision
        // and the y it claims against the previous packet's
        auto  id     = player->getOrCreateUniqueID().rawID;
        auto& s      = st();
        Vec3  claim  = packet.mPos;
        std::optional<bool> ground;
        if (auto y = s.claimY.find(id); y != s.claimY.end())
            ground = groundFromInput(in.contains(Input::VerticalCollision), claim.y - y->second, in.contains(Input::HandledTeleport));
        s.claimY[id] = claim.y;
        if (!ground) {
            auto before = s.last.find(id);
            ground      = before != s.last.end() ? before->second.onGround : player->isOnGround();
        }
        sample.onGround = *ground;
        feedInput(*player, sample);
        if (correctTo) s.pending.push_back({id, *correctTo, static_cast<int>(player->getDimensionId()), rejecter, kCorrectionDelay});
    } catch (std::exception const& e) {
        log().error("movement: auth input handling failed: {}", e.what());
    }
}

// Every teleport the server makes of a player (/tp, a correction, LeviLamina's Actor::teleport, unit
// code): validation starts afresh from the destination.
LL_TYPE_INSTANCE_HOOK(
    MovementTeleportHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$teleportTo,
    void,
    Vec3 const& pos,
    bool        shouldStopRiding,
    int         cause,
    int         sourceEntityType,
    bool        keepVelocity
) {
    origin(pos, shouldStopRiding, cause, sourceEntityType, keepVelocity);
    try {
        if (!isClientSide() && !isSimulated()) resetTrack(*this, ResetReason::Teleport);
    } catch (...) {}
}

namespace {
class MovementModule {
public:
    void install() {
        MovementAuthInputHook::hook();
        MovementTeleportHook::hook();
        auto& bus   = ll::event::EventBus::getInstance();
        mDisconnect = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) { forgetTrack(ev.self()); });
        mRespawn    = bus.emplaceListener<ll::event::PlayerRespawnEvent>([](ll::event::PlayerRespawnEvent& ev) {
            if (!ev.self().isSimulated()) resetTrack(ev.self(), ResetReason::Respawn);
        });
        mTick = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) { runCorrections(); });
    }
    void uninstall() {
        MovementTeleportHook::unhook();
        MovementAuthInputHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mDisconnect);
        bus.removeListener(mRespawn);
        bus.removeListener(mTick);
        st().last.clear();
        st().claimY.clear();
        st().tracks.clear();
        st().pending.clear();
    }

private:
    ll::event::ListenerPtr mDisconnect;
    ll::event::ListenerPtr mRespawn;
    ll::event::ListenerPtr mTick;
};

} // namespace
} // namespace gf::movement

GF_REGISTER_MODULE_ORDERED(gf::movement::MovementModule, gf::ModuleOrder::Contracts);

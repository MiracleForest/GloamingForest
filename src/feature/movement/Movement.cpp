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
// (feature/ext/ui/Server.h has the exact rules).

#include "feature/movement/Movement.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
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
/// Server ticks between a rejection and its correction (after the queued input was applied).
constexpr int kCorrectionDelay = 2;

struct Validator {
    std::string id;
    ValidateFn  fn;
};

/// Per real player: the last accepted move and a pending correction.
struct Track {
    Vec3 last{};
    int  dim         = -1;
    bool valid       = false;
    bool awaitingAck = false;
    int  ackPackets  = 0;
};

struct Correction {
    std::int64_t player;
    Vec3         to;
    std::string  validator;
    int          ticks;
};

struct State {
    std::vector<std::function<void(Player&, Edge)>> edgeHandlers;
    std::vector<Validator>                          validators;
    std::unordered_map<std::int64_t, InputSample>   last; ///< per player unique id
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
    if (s.validators.empty()) s.tracks.clear(); // validation starts afresh with the next rule
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
    if (s.validators.empty() || player.isSimulated()) return std::nullopt;
    // the packet carries the client's eye position: the validators and the correction use feet
    float eye    = player.getPosition().y - player.getFeetPos().y;
    Vec3  claim  = packet.mPos;
    Vec3  to{claim.x, claim.y - eye, claim.z};
    auto& t      = s.tracks[player.getOrCreateUniqueID().rawID];
    int   dim    = static_cast<int>(player.getDimensionId());
    // a teleport of the server (a correction, /tp, a portal, a respawn) or another dimension: the
    // position the client reports with it is the new start
    if (packet.mInputData->contains(Input::HandledTeleport) || !t.valid || t.dim != dim) {
        bool fresh    = !t.valid || t.dim != dim;
        t.awaitingAck = false;
        t.valid       = true;
        t.dim         = dim;
        t.last        = fresh ? player.getFeetPos() : to;
        if (!fresh) return std::nullopt;
    } else if (t.awaitingAck) {
        // moves sent before the correction's teleport arrived are not checked again
        if (++t.ackPackets > kAckPackets) {
            t.awaitingAck = false;
            t.last        = to;
        }
        return std::nullopt;
    }
    Vec3 from = t.last;
    if (validateMove(player, from, to)) {
        t.last = to;
        return std::nullopt;
    }
    t.awaitingAck = true;
    t.ackPackets  = 0;
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
        if (!p) continue;
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
        sample.onGround = player->isOnGround();
        feedInput(*player, sample);
        if (correctTo) st().pending.push_back({player->getOrCreateUniqueID().rawID, *correctTo, rejecter, kCorrectionDelay});
    } catch (std::exception const& e) {
        log().error("movement: auth input handling failed: {}", e.what());
    }
}

namespace {
class MovementModule {
public:
    void install() {
        MovementAuthInputHook::hook();
        auto& bus   = ll::event::EventBus::getInstance();
        mDisconnect = bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            auto id = ev.self().getOrCreateUniqueID().rawID;
            st().last.erase(id);
            st().tracks.erase(id);
            std::erase_if(st().pending, [id](Correction const& c) { return c.player == id; });
        });
        mTick = bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent&) { runCorrections(); });
    }
    void uninstall() {
        MovementAuthInputHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mDisconnect);
        bus.removeListener(mTick);
        st().last.clear();
        st().tracks.clear();
        st().pending.clear();
    }

private:
    ll::event::ListenerPtr mDisconnect;
    ll::event::ListenerPtr mTick;
};

} // namespace
} // namespace gf::movement

GF_REGISTER_MODULE_ORDERED(gf::movement::MovementModule, gf::ModuleOrder::Contracts);

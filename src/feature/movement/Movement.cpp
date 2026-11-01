// Server half of "client.input+movement" (F12).
//
// Bedrock movement is client-authoritative for the player's own input: the server sees one
// PlayerAuthInputPacket per client tick. This module turns those packets into input edges for the
// vanilla-client fallbacks (jump / sneak / sprint pressed and released, left the ground, landed),
// pushes motion to a player (SetActorMotionPacket, the server's own motion too) and runs the
// movement validators of the modifiers (anti-desync for companions whose client applies modifiers
// natively): a rejected move puts the player back where the server last saw it.

#include "feature/movement/Movement.h"

#include <mutex>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/memory/Hook.h"

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

#include "feature/ext/ui/Server.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::movement {

namespace {

struct Validator {
    std::string id;
    ValidateFn  fn;
};

struct State {
    std::vector<std::function<void(Player&, Edge)>>   edgeHandlers;
    std::vector<Validator>                             validators;
    std::unordered_map<std::int64_t, InputSample>      last; ///< per player unique id
    std::unordered_map<std::string, bool>              loggedReject;
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

std::size_t validatorCount() { return st().validators.size(); }

bool validateMove(Player& player, Vec3 const& from, Vec3 const& to) {
    for (auto const& v : st().validators) {
        bool ok = true;
        try {
            ok = v.fn(player, from, to);
        } catch (std::exception const& ex) {
            log().error("movement: validator {} failed: {}", v.id, ex.what());
        }
        if (!ok) {
            auto& logged = st().loggedReject[v.id];
            if (!logged) {
                logged = true;
                log().info("movement: {} rejected a move of {}", v.id, player.getRealName());
            }
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

} // namespace gf::movement

namespace gf::movement {

using Input = PlayerAuthInputPacketPayload::InputData;

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
    Player* player = nullptr;
    Vec3    from{};
    bool    checkMove = false;
    try {
        player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId);
        if (player && !st().validators.empty()) {
            from      = player->getFeetPos(); // validators and the correction work with feet positions
            checkMove = true;
        }
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
        if (checkMove) {
            Vec3 to = player->getFeetPos();
            // the correction: back to where the server last saw the feet (LeviLamina's teleport
            // takes feet positions, as /tp)
            if (!validateMove(*player, from, to)) player->teleport(from, player->getDimensionId(), player->getRotation());
        }
    } catch (std::exception const& e) {
        log().error("movement: auth input handling failed: {}", e.what());
    }
}

namespace {
class MovementModule {
public:
    void install() {
        MovementAuthInputHook::hook();
        mDisconnect = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            st().last.erase(ev.self().getOrCreateUniqueID().rawID);
        });
    }
    void uninstall() {
        MovementAuthInputHook::unhook();
        ll::event::EventBus::getInstance().removeListener(mDisconnect);
        st().last.clear();
    }

private:
    ll::event::ListenerPtr mDisconnect;
};

} // namespace
} // namespace gf::movement

GF_REGISTER_MODULE_ORDERED(gf::movement::MovementModule, gf::ModuleOrder::Contracts);

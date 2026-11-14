// Block families (F11): selftests of the family blocks' sounds (testPos dx 6140..6144): the chests'
// open and close sounds and the place sound of a waterloggable block placed into water.
//
// Java: ChestBlockEntity's ContainerOpenersCounter plays SoundEvents.CHEST_OPEN when the first
// player opens a chest (onOpen) and CHEST_CLOSE when the last one closes it (onClose), through
// ChestBlockEntity#playSound: nothing for a LEFT half, a single chest at its centre, a RIGHT half
// between the two halves, volume 0.5F, pitch random.nextFloat() * 0.1F + 0.9F. TrappedChestBlock
// uses the same block entity logic. upstream's chests of every wood are those blocks (GloamingBlocks:
// ChestBlock / TrappedChestBlock with BlockEntityType.CHEST).
//
// Here the inventory contract (F10) counts the openers and plays the sound through fx; these tests
// record the PlaySoundPacket the players get (the game's packet sender) and check the sound name
// (the game's own random.chestopen / random.chestclosed, which vanilla's chests play), position,
// volume, pitch and timing, and that the game adds no sound event of its own for the chest (the
// client's "No sound found for block type 'wood' and event type 'chest.open'" comes from vanilla
// chests: block_sounds "wood" has no chest events, so the game falls back to its own definition).
// Each send of a packet is one record with whether it reached the test player (its network
// identifier and sub-client), so "one sound" counts the sounds the player is sent, also when the
// game reuses a freed packet's memory for the next one.
//
// Rule R8: no contract records the sounds players are sent with their name, position, volume and
// pitch (gf::fx only counts its packets, FxInternal.h counters(); the selftest framework records
// boss bar packets only, src/selftest/SelfTest.h is F0's and frozen), and the game's own
// LevelSoundEvent packets are not fx's. So these tests hook the packet sender themselves,
// TEST-ONLY: SoundCapture installs the hooks when the first capture starts and removes them when
// the last one ends, so no game event is hooked outside a running families sound test (LeviLamina
// chains them with the other hooks of these functions). A generic packet capture in the selftest
// framework (which hooks the same functions for captureBossEvent) would replace them.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/NetworkIdentifierWithSubId.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/Packet.h"
#include "mc/common/SubClientId.h"
#include "mc/entity/components/UserEntityIdentifierComponent.h"
#include "mc/network/packet/LevelSoundEventPacket.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/sound/SoundEventIdentifier.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/families/Families.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::core::GlobalPos;
using gf::selftest::Context;
namespace inv = gf::inventory;
namespace fam = gf::feature::families;

// ---- the sounds the players are sent ---------------------------------------------------------------

/// Who one send of a packet reached: the receiver the capture watches (its network identifier and
/// sub-client), checked when the packet is sent.
struct Receiver {
    std::uint64_t netHash = 0;
    SubClientId   sub     = SubClientId::PrimaryClient;
    bool          set     = false;
    [[nodiscard]] bool is(NetworkIdentifier const& id, SubClientId s) const { return set && s == sub && id.getHash() == netHash; }
};

/// One send of a sound packet while a capture was alive (one record per call of the packet sender:
/// a packet sent to three players separately is three records, a broadcast is one).
struct HeardSound {
    std::string   name;   ///< PlaySoundPacket: the sound name; LevelSoundEventPacket: "event:<id>"
    Vec3          pos;    ///< where it plays
    float         volume = 0;
    float         pitch  = 0;
    bool          toWatched = false; ///< the send reached the watched receiver (the test player)
};

struct SoundLog {
    std::mutex              mutex;
    std::vector<HeardSound> heard;
    Receiver                watched;
    std::atomic<int>        captures{0};
};
SoundLog& soundLog() {
    static SoundLog l;
    return l;
}

/// The packet a hooked packet sender call on this thread is sending: when one overload forwards the
/// same packet to another (say sendToClients to sendToClient per receiver), only the outermost call
/// records, so a logical send is one record; another packet sent from inside a send still records.
thread_local Packet const* tSending = nullptr;
struct SendScope {
    Packet const* previous;
    bool          outermost;
    explicit SendScope(Packet const& packet) : previous(tSending), outermost(tSending != &packet) { tSending = &packet; }
    ~SendScope() { tSending = previous; }
    SendScope(SendScope const&)            = delete;
    SendScope& operator=(SendScope const&) = delete;
};
/// Records one send of `packet` when it is a sound; `reaches` tells whether the send reaches the
/// watched receiver. Only the outermost hooked call of a send records (`scope`).
template <class Reaches>
void inspect(SendScope const& scope, Packet const& packet, Reaches&& reaches) {
    auto& log = soundLog();
    if (!scope.outermost || log.captures.load() == 0) return;
    HeardSound h;
    switch (packet.getId()) {
    case MinecraftPacketIds::PlaySound: {
        auto const& p   = static_cast<PlaySoundPacket const&>(packet);
        BlockPos const& at = p.mPos.get(); // (1/8 block units)
        h.name   = p.mName.get();
        h.pos    = Vec3{at.x / 8.f, at.y / 8.f, at.z / 8.f};
        h.volume = p.mVolume;
        h.pitch  = p.mPitch;
        break;
    }
    case MinecraftPacketIds::LevelSoundEvent: {
        auto const& p     = static_cast<LevelSoundEventPacket const&>(packet);
        auto const& sound = p.mSoundEvent.get().mSound.get();
        if (auto const* e = std::get_if<SharedTypes::Legacy::LevelSoundEvent>(&sound)) h.name = "event:" + std::to_string(static_cast<int>(*e));
        else if (auto const* s = std::get_if<std::string>(&sound)) h.name = "event:" + *s;
        else h.name = "event:?";
        h.pos = p.mPos.get();
        break;
    }
    default:
        return;
    }
    std::lock_guard lock(log.mutex);
    h.toWatched = reaches(log.watched);
    log.heard.push_back(std::move(h));
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    FamiliesSoundToUserHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(UserEntityIdentifierComponent const*, Packet const&)>(&LoopbackPacketSender::$sendToClient),
    void,
    UserEntityIdentifierComponent const* user,
    Packet const&                        packet
) {
    SendScope scope(packet);
    inspect(scope, packet, [user](Receiver const& w) { return user && w.is(user->mNetworkId.get(), user->mClientSubId); });
    origin(user, packet);
}

// Packet::sendTo(Player) (gf::fx::sound) goes through this overload
LL_TYPE_INSTANCE_HOOK(
    FamiliesSoundToNetIdHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(NetworkIdentifier const&, Packet const&, SubClientId)>(&LoopbackPacketSender::$sendToClient),
    void,
    NetworkIdentifier const& id,
    Packet const&            packet,
    SubClientId              sub
) {
    SendScope scope(packet);
    inspect(scope, packet, [&id, sub](Receiver const& w) { return w.is(id, sub); });
    origin(id, packet, sub);
}

LL_TYPE_INSTANCE_HOOK(
    FamiliesSoundToClientsHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$sendToClients,
    void,
    std::vector<NetworkIdentifierWithSubId> const& ids,
    Packet const&                                  packet
) {
    SendScope scope(packet);
    inspect(scope, packet, [&ids](Receiver const& w) {
        return std::any_of(ids.begin(), ids.end(), [&w](NetworkIdentifierWithSubId const& r) { return w.is(r.id, r.subClientId); });
    });
    origin(ids, packet);
}

LL_TYPE_INSTANCE_HOOK(
    FamiliesSoundBroadcastHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(Packet const&)>(&LoopbackPacketSender::$sendBroadcast),
    void,
    Packet const& packet
) {
    SendScope scope(packet);
    inspect(scope, packet, [](Receiver const& w) { return w.set; });
    origin(packet);
}

// the broadcast to every client but one (the game's sounds a player makes itself: the others hear them)
LL_TYPE_INSTANCE_HOOK(
    FamiliesSoundBroadcastExceptHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    static_cast<void (LoopbackPacketSender::*)(NetworkIdentifier const&, SubClientId, Packet const&)>(&LoopbackPacketSender::$sendBroadcast),
    void,
    NetworkIdentifier const& exceptId,
    SubClientId              exceptSub,
    Packet const&            packet
) {
    SendScope scope(packet);
    inspect(scope, packet, [&exceptId, exceptSub](Receiver const& w) { return w.set && !w.is(exceptId, exceptSub); });
    origin(exceptId, exceptSub, packet);
}

namespace {

/// Records the sound packets sent while it lives (the hooks are installed only then), each send
/// once, with whether it reached the watched player.
class SoundCapture {
public:
    /// Watches `player`: the sends that reach it are told apart from the others (`toPlayer`).
    explicit SoundCapture(Player& player) {
        {
            std::lock_guard lock(soundLog().mutex);
            soundLog().watched = Receiver{player.getNetworkIdentifier().getHash(), player.getClientSubId(), true};
        }
        // the receivers are told apart by network identifier and sub-client: no other player of
        // the level may share the watched one's, or its sends would count twice
        auto const& w = soundLog().watched;
        auto const  me = player.getOrCreateUniqueID();
        player.getLevel().forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (p.getOrCreateUniqueID().rawID != me.rawID && w.is(p.getNetworkIdentifier(), p.getClientSubId())) ++mSharing;
            return true;
        }));
        if (soundLog().captures.fetch_add(1) == 0) {
            FamiliesSoundToUserHook::hook();
            FamiliesSoundToNetIdHook::hook();
            FamiliesSoundToClientsHook::hook();
            FamiliesSoundBroadcastHook::hook();
            FamiliesSoundBroadcastExceptHook::hook();
        }
        clear();
    }
    ~SoundCapture() {
        if (soundLog().captures.fetch_sub(1) == 1) {
            FamiliesSoundToUserHook::unhook();
            FamiliesSoundToNetIdHook::unhook();
            FamiliesSoundToClientsHook::unhook();
            FamiliesSoundBroadcastHook::unhook();
            FamiliesSoundBroadcastExceptHook::unhook();
            std::lock_guard lock(soundLog().mutex);
            soundLog().watched = Receiver{};
        }
    }
    SoundCapture(SoundCapture const&)            = delete;
    SoundCapture& operator=(SoundCapture const&) = delete;

    /// The number of other players whose receiver equals the watched player's (0: its sends are
    /// counted exactly).
    [[nodiscard]] int sharing() const { return mSharing; }

    void clear() {
        std::lock_guard lock(soundLog().mutex);
        soundLog().heard.clear();
    }
    /// The sends that reached the watched player within `range` blocks of `centre`, in order (one
    /// per sound the player hears: a sound played twice is two, also when the game reused the
    /// packet's memory).
    [[nodiscard]] std::vector<HeardSound> toPlayer(Vec3 const& centre, float range) const {
        return select(centre, range, [](HeardSound const& h) { return h.toWatched; });
    }
    /// Every send within `range` blocks of `centre`, whoever it reached (a broadcast is one send).
    [[nodiscard]] std::vector<HeardSound> sent(Vec3 const& centre, float range) const {
        return select(centre, range, [](HeardSound const&) { return true; });
    }

private:
    int mSharing = 0;

    template <class Pred>
    [[nodiscard]] std::vector<HeardSound> select(Vec3 const& centre, float range, Pred&& pred) const {
        std::lock_guard         lock(soundLog().mutex);
        std::vector<HeardSound> out;
        for (auto const& h : soundLog().heard) {
            float dx = h.pos.x - centre.x, dy = h.pos.y - centre.y, dz = h.pos.z - centre.z;
            if (dx * dx + dy * dy + dz * dz <= range * range && pred(h)) out.push_back(h);
        }
        return out;
    }
};

std::string describe(std::vector<HeardSound> const& heard) {
    std::string s;
    for (auto const& h : heard) {
        if (!s.empty()) s += ", ";
        s += h.name + " @" + std::to_string(h.pos.x) + "," + std::to_string(h.pos.y) + "," + std::to_string(h.pos.z) + " v" + std::to_string(h.volume) + " p" + std::to_string(h.pitch);
    }
    return s.empty() ? "none" : s;
}

/// The chest sounds among `heard` (PlaySoundPacket random.chestopen / random.chestclosed) and the
/// game's own chest sound events (LevelSoundEvent ChestOpen / ChestClosed).
std::vector<HeardSound> named(std::vector<HeardSound> const& heard, std::string const& name) {
    std::vector<HeardSound> out;
    std::copy_if(heard.begin(), heard.end(), std::back_inserter(out), [&](HeardSound const& h) { return h.name == name; });
    return out;
}
std::string const kOpenName   = "random.chestopen";
std::string const kCloseName  = "random.chestclosed";
std::string const kOpenEvent  = "event:" + std::to_string(static_cast<int>(SharedTypes::Legacy::LevelSoundEvent::ChestOpen));
std::string const kCloseEvent = "event:" + std::to_string(static_cast<int>(SharedTypes::Legacy::LevelSoundEvent::ChestClosed));

/// ChestBlockEntity#playSound's values: volume 0.5F, pitch in [0.9, 1.0), at `where` (the packet
/// keeps 1/8 block).
bool javaValues(HeardSound const& h, Vec3 const& where) {
    auto close = [](float a, float b) { return std::fabs(a - b) <= 0.126f; };
    return std::fabs(h.volume - 0.5f) < 1e-4f && h.pitch >= 0.9f - 1e-4f && h.pitch < 1.0f + 1e-4f && close(h.pos.x, where.x) && close(h.pos.y, where.y)
        && close(h.pos.z, where.z);
}

constexpr char kFacing[] = "gloamingforest:facing";
constexpr char kType[]   = "gloamingforest:type";

/// upstream's chest woods (GloamingBlocks: GLOAMING_OAK_CHEST ... SORTING_TRAPPED_CHEST).
constexpr char const* kWoods[] = {"gloaming_oak", "canopy", "mangrove", "dark", "time", "transformation", "mining", "sorting"};

SimulatedPlayer* simPlayer(Context& ctx, std::int64_t uid) {
    auto* region = ctx.region();
    if (!region) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return static_cast<SimulatedPlayer*>(region->getLevel().getPlayer(id));
}

/// The test player stands south of `p` looking at it with an empty hand and right-clicks its top
/// (GameMode::useItemOn, a first event); returns after the tick of the click.
ll::coro::CoroTask<> clickChest(Context& ctx, GlobalPos p) {
    auto uid = ctx.player().getOrCreateUniqueID().rawID;
    if (auto* sp = simPlayer(ctx, uid)) {
        sp->teleport(Vec3{p.x + .5f, static_cast<float>(p.y), p.z + 2.5f}, sp->getDimensionId());
        sp->simulateLookAt(gf::game::blockPos(p), sim::LookDuration::Instant);
        sp->simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    }
    co_await ctx.ticks(8); // the look applies on the player's next tick; Bedrock drops a use-on right after another
    if (auto* sp = simPlayer(ctx, uid)) {
        auto  b    = gf::game::blockPos(p);
        auto& held = const_cast<ItemStack&>(sp->getSelectedItem());
        sp->mGameMode->useItemOn(held, b, 1, Vec3{b.x + .5f, b.y + 1.f, b.z + .5f}, HandSlot::Mainhand, nullptr, true);
    }
}

/// The player counts as a client mod user while it lives (the inventory contract's chest screen).
struct Companion {
    Context& ctx;
    explicit Companion(Context& c) : ctx(c) {
        gf::net::setTestCompanion(ctx.player(), true);
        (void)gf::net::takeTestMessages(ctx.player());
    }
    ~Companion() {
        gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}});
        (void)gf::net::takeTestMessages(ctx.player());
        gf::net::setTestCompanion(ctx.player(), false);
    }
    Companion(Companion const&)            = delete;
    Companion& operator=(Companion const&) = delete;
};

void closeScreen(Context& ctx) { gf::net::injectFromClient(ctx.player(), "screen.closed", {{"type", "inventory.chest"}}); }

} // namespace

GF_REGISTER_SELFTEST("families.chest_sounds", [](Context& ctx) -> ll::coro::CoroTask<> {
    // every upstream chest and trapped chest: opened by a click, the open sound plays once in the same
    // tick, at the chest's centre, volume 0.5, pitch 0.9..1.0; closed, the close sound the same way;
    // nothing else of the game's own (no LevelSoundEvent ChestOpen / ChestClosed: one sound each)
    std::vector<std::string> ids;
    for (auto const* wood : kWoods)
        for (auto const* kind : {"_chest", "_trapped_chest"}) ids.push_back(std::string("gloamingforest:") + wood + kind);
    std::erase_if(ids, [](std::string const& id) { return !gf::game::blockWith(id) || !fam::entryOf(id); });
    if (ids.empty()) {
        ctx.info("no upstream chests in this build: skipped");
        co_return;
    }
    ctx.check(ids.size() == 16, std::to_string(ids.size()) + " of upstream's 16 chests and trapped chests are in this build");
    auto box = co_await ctx.flatArea(6140, 5);
    auto p   = ctx.testPos(6140, 1);
    p.x += 2;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    Vec3 const   centre{p.x + .5f, p.y + .5f, p.z + .5f};
    SoundCapture capture(ctx.player());
    ctx.require(capture.sharing() == 0, "the test player is the only receiver of its network identifier and sub-client (" + std::to_string(capture.sharing()) + " other)");
    Companion    companion(ctx);
    for (auto const& id : ids) {
        ctx.require(ctx.setBlock(p, id, {{kFacing, "south"}}), id + " placed");
        co_await ctx.ticks(2);
        auto decl = inv::declared(id);
        ctx.check(decl && decl->openSound == "minecraft:block.chest.open" && decl->closeSound == "minecraft:block.chest.close",
                  id + ": declared with SoundEvents.CHEST_OPEN / CHEST_CLOSE");
        capture.clear();
        co_await clickChest(ctx, p);
        // Java: the menu opens and ContainerOpenersCounter#incrementOpeners plays the sound while the
        // server handles the click
        bool inClickTick = !named(capture.toPlayer(centre, 2.f), kOpenName).empty();
        co_await ctx.ticks(1);
        auto opened = capture.toPlayer(centre, 2.f);
        ctx.check(inv::viewerCount(p) == 1, id + ": the click opened it (" + std::to_string(inv::viewerCount(p)) + " opener)");
        auto open = named(opened, kOpenName);
        ctx.check(open.size() == 1 && javaValues(open[0], centre), id + ": one random.chestopen at its centre, volume 0.5, pitch 0.9..1.0 (heard: " + describe(opened) + ")");
        ctx.check(inClickTick, id + ": the open sound plays in the click's tick");
        // (whoever a send reached: a broadcast past the clicking player counts too)
        auto anyOpen = capture.sent(centre, 2.f);
        ctx.check(named(anyOpen, kOpenEvent).empty() && named(anyOpen, kCloseEvent).empty(), id + ": no chest sound event of the game's own (sent: " + describe(anyOpen) + ")");
        capture.clear();
        closeScreen(ctx);
        auto closed = capture.toPlayer(centre, 2.f); // (ContainerOpenersCounter#decrementOpeners: at once)
        auto close  = named(closed, kCloseName);
        ctx.check(close.size() == 1 && javaValues(close[0], centre), id + ": one random.chestclosed as the last opener leaves, same values (heard: " + describe(closed) + ")");
        co_await ctx.ticks(1);
        auto anyClose = capture.sent(centre, 2.f);
        ctx.check(named(anyClose, kOpenEvent).empty() && named(anyClose, kCloseEvent).empty(), id + ": no chest sound event of the game's own on close (sent: " + describe(anyClose) + ")");
        co_await ctx.ticks(12); // the lid closes
        ctx.setBlock(p, "minecraft:air");
        co_await ctx.ticks(1);
    }
    (void)box;
});

GF_REGISTER_SELFTEST("families.sound_events", [](Context& ctx) {
    // the sound types of the family blocks define the events the game plays with them
    // (tools/families: add_family_sound_events): place_in_water (a waterloggable block placed into
    // water: Java's BlockItem#place plays the type's place sound) as the type's place event, and the
    // chest types chest.open / chest.closed as vanilla's chests (random.chestopen /
    // random.chestclosed, volume 0.5, pitch 0.9..1.0)
    namespace fs = std::filesystem;
    using JsonT  = nlohmann::json;
    auto read    = [](fs::path const& p) {
        std::ifstream in(p);
        return in ? JsonT::parse(in, nullptr, false) : JsonT();
    };
    auto       dir   = gf::Mod::getInstance().getSelf().getModDir();
    auto const packs = {std::string("GloamingForest_RP"), std::string("GloamingForest_Test_RP")};
    // the merged view: blocks.json of both packs, block_sounds events of both packs
    std::map<std::string, std::string> soundOf;
    std::map<std::string, JsonT>       events;
    for (auto const& pack : packs) {
        auto blocks = read(dir / "resource_packs" / pack / "blocks.json");
        if (blocks.is_object())
            for (auto const& [id, b] : blocks.items())
                if (b.is_object() && b.contains("sound") && b["sound"].is_string()) soundOf[id] = b["sound"].get<std::string>();
        auto sounds = read(dir / "resource_packs" / pack / "sounds.json");
        if (sounds.is_object() && sounds.contains("block_sounds") && sounds["block_sounds"].is_object())
            for (auto const& [t, def] : sounds["block_sounds"].items())
                if (def.is_object() && def.contains("events") && def["events"].is_object())
                    for (auto const& [e, v] : def["events"].items()) events[t][e] = v;
    }
    // the family blocks whose sound type the hand-made RP layer set (tools/families:
    // data/gf/families_sounds): the manual layer wins per id, also over the hanging signs' type
    std::map<std::string, std::string> manualSound;
    std::error_code                    ec;
    for (fs::directory_iterator it(dir / "data" / "gf" / "families_sounds", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".json") continue;
        auto doc = read(it->path());
        if (doc.is_object() && doc.contains("manual") && doc["manual"].is_object())
            for (auto const& [id, s] : doc["manual"].items())
                if (s.is_string()) manualSound[id] = s.get<std::string>();
    }
    auto event = [&](std::string const& t, char const* e) -> JsonT const* {
        auto it = events.find(t);
        return it != events.end() && it->second.contains(e) ? &it->second[e] : nullptr;
    };
    int checked = 0, chests = 0, hanging = 0;
    std::set<std::string> missing;
    for (auto const* e : fam::entries()) {
        auto it = soundOf.find(e->id);
        if ((e->family == fam::Family::HangingSign || e->family == fam::Family::WallHangingSign) && !e->params.contains("sound")) {
            // CeilingHangingSignBlock / WallHangingSignBlock: properties.sound(woodType.hangingSignSoundType()),
            // SoundType.HANGING_SIGN for upstream's wood types
            ++hanging;
            auto byHand = manualSound.find(e->id);
            if (it != soundOf.end() && byHand != manualSound.end() && byHand->second == it->second) {
                ctx.info(e->id + " keeps the manual layer's sound type " + it->second);
            } else if (it == soundOf.end() || it->second != "hanging_sign")
                missing.insert("hanging_sign sound type (" + e->id + ": " + (it == soundOf.end() ? std::string("none") : it->second) + ")");
        }
        bool const chest = e->family == fam::Family::Chest || e->family == fam::Family::TrappedChest;
        if (it == soundOf.end()) {
            // (tools/families fails such a block at assemble: the events need a type to live on)
            if ((e->has(fam::Waterloggable) || chest) && gf::game::blockWith(e->id)) missing.insert("a sound type (" + e->id + ")");
            continue;
        }
        if (e->has(fam::Waterloggable)) {
            ++checked;
            if (!event(it->second, "place_in_water")) missing.insert(it->second + " place_in_water (" + e->id + ")");
        }
        if (chest) {
            ++chests;
            for (auto const* name : {"chest.open", "chest.closed"}) {
                auto const* v = event(it->second, name);
                bool ok = v && v->is_object() && v->value("sound", std::string{}) == (std::string(name) == "chest.open" ? kOpenName : kCloseName) && std::fabs(v->value("volume", 0.0) - 0.5) < 1e-6;
                if (!ok) missing.insert(it->second + " " + name + " (" + e->id + ")");
            }
        }
    }
    std::string list;
    for (auto const& m : missing) list += (list.empty() ? "" : ", ") + m;
    ctx.check(checked > 0 && chests > 0, std::to_string(checked) + " waterloggable family blocks and " + std::to_string(chests) + " chests with a sound type (" + std::to_string(hanging) + " hanging signs)");
    ctx.check(missing.empty(), "every one's sound type defines the event the game plays (missing: " + (list.empty() ? std::string("none") : list) + ")");
});

GF_REGISTER_SELFTEST("families.chest_sounds_double", [](Context& ctx) -> ll::coro::CoroTask<> {
    // a double chest: one sound between the halves (the RIGHT half's; the LEFT half plays none),
    // whichever half is clicked; the close sound the same way once the last opener leaves
    constexpr char kChest[] = "gloamingforest:canopy_trapped_chest";
    if (!gf::game::blockWith(kChest) || !fam::entryOf(kChest)) {
        ctx.info("no upstream chests in this build: skipped");
        co_return;
    }
    auto box = co_await ctx.flatArea(6140, 5);
    auto l   = ctx.testPos(6140, 1);
    l.x += 1;
    auto r = l;
    r.x += 1; // facing north, the left half's partner is east of it (ChestBlock#getConnectedDirection: clockwise)
    ctx.require(co_await ctx.ensureLoaded(l), "test area loaded");
    ctx.require(ctx.setBlock(l, kChest, {{kFacing, "north"}, {kType, "left"}}), "the left half");
    ctx.require(ctx.setBlock(r, kChest, {{kFacing, "north"}, {kType, "right"}}), "the right half");
    co_await ctx.ticks(2);
    ctx.require(fam::chestPartner(*ctx.region(), gf::game::blockPos(l)).has_value(), "the halves pair");
    Vec3 const   between{l.x + 1.f, l.y + .5f, l.z + .5f};
    SoundCapture capture(ctx.player());
    ctx.require(capture.sharing() == 0, "the test player is the only receiver of its network identifier and sub-client (" + std::to_string(capture.sharing()) + " other)");
    Companion    companion(ctx);
    for (auto const& half : {l, r}) {
        capture.clear();
        co_await clickChest(ctx, half);
        auto heard = capture.toPlayer(between, 3.f);
        co_await ctx.ticks(1);
        auto open = named(heard, kOpenName);
        ctx.check(inv::viewerCount(l) == 1 && inv::viewerCount(r) == 1, "a click on the " + std::string(half.x == l.x ? "left" : "right") + " half opens both");
        ctx.check(open.size() == 1 && javaValues(open[0], between),
                  "one random.chestopen between the halves, volume 0.5, pitch 0.9..1.0 (clicked the " + std::string(half.x == l.x ? "left" : "right") + " half; heard: " + describe(heard) + ")");
        capture.clear();
        closeScreen(ctx);
        auto after  = capture.toPlayer(between, 3.f);
        auto closed = named(after, kCloseName);
        ctx.check(closed.size() == 1 && javaValues(closed[0], between), "one random.chestclosed between the halves (heard: " + describe(after) + ")");
        co_await ctx.ticks(12);
    }
    for (auto const& q : {l, r}) ctx.setBlock(q, "minecraft:air");
    (void)box;
});

GF_REGISTER_SELFTEST("families.place_in_water_sound", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Java: BlockItem#place plays the block's SoundType#getPlaceSound once at the block, also when a
    // SimpleWaterloggedBlock takes the water it is placed into. Bedrock's placing client plays the
    // place sound of a block placed into water as LevelSoundEvent PlaceInWater (the type's
    // place_in_water, which tools/families defines as its place event): a player placing an upstream waterloggable slab or stairs
    // into a water source makes exactly one place sound (PlaceInWater, Place and ItemUseOn are all
    // counted, so a second sound of another kind fails the test).
    // These are the server's sends: the game broadcasts LevelSoundEvent Place (the type's place
    // event, Java's place sound) to the other players, here a listener, past the placing player,
    // whose own client plays the sound itself as PlaceInWater (the type's place_in_water). That
    // client-side sound is not visible on the server: the client e2e
    // (tools/e2e/client/scenarios/families.ps1) places a slab into water and fails on a content-log
    // "No sound found ... 'place_in_water'" line.
    std::string const kPlace = "event:" + std::to_string(static_cast<int>(SharedTypes::Legacy::LevelSoundEvent::Place));
    constexpr char const* kCandidates[] = {"gloamingforest:gloaming_oak_slab", "gloamingforest:castle_brick_stairs"};
    std::vector<std::string> ids;
    for (auto const* id : kCandidates) {
        auto const* e = fam::entryOf(id);
        if (gf::game::blockWith(id) && e) {
            ctx.check(e->has(fam::Waterloggable), std::string(id) + " is a waterloggable family block");
            ids.emplace_back(id);
        }
    }
    if (ids.empty()) {
        ctx.info("no upstream waterloggable slabs or stairs in this build: skipped");
        co_return;
    }
    auto box = co_await ctx.flatArea(6143, 8); // (both basins, x - 1..x + 1 of p.x + 1 and p.x + 5, and their floor)
    auto p   = ctx.testPos(6143, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto wet = [&ctx](GlobalPos const& q) { // (the block source looked up again: R16)
        auto* region = ctx.region();
        if (!region) return false;
        auto const& extra = region->getExtraBlock(gf::game::blockPos(q)).getTypeName();
        return extra == "minecraft:water" || extra == "minecraft:flowing_water";
    };
    // a second player in hearing range: the game sends the placing player's own sounds to the others
    auto& listener   = ctx.spawnPlayer("gf_families_listener");
    listener.teleport(Vec3{p.x + 3.5f, static_cast<float>(p.y), p.z - 1.5f}, listener.getDimensionId()); // (removed with the test)
    auto const uid = ctx.player().getOrCreateUniqueID().rawID;
    SoundCapture capture(ctx.player());
    auto placeSound = [](HeardSound const& h) {
        using E = SharedTypes::Legacy::LevelSoundEvent;
        for (auto e : {E::Place, E::PlaceInWater, E::ItemUseOn})
            if (h.name == "event:" + std::to_string(static_cast<int>(e))) return true;
        return false;
    };
    for (std::size_t i = 0; i < ids.size(); ++i) {
        auto const& id = ids[i];
        auto        w  = p;
        w.x += static_cast<int>(i) * 4 + 1;
        // a basin of stone around a water source, on the area's ground
        for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            auto q = w;
            q.x += dx;
            q.z += dz;
            ctx.require(ctx.setBlock(q, "minecraft:stone"), "the basin");
        }
        ctx.require(ctx.setBlock(w, "minecraft:water"), "water in the basin");
        auto below = w;
        below.y -= 1;
        if (auto* sp = simPlayer(ctx, uid)) {
            sp->teleport(Vec3{w.x + .5f, static_cast<float>(w.y), w.z + 2.5f}, sp->getDimensionId());
            sp->simulateLookAt(gf::game::blockPos(below), sim::LookDuration::Instant);
            if (auto item = gf::game::items::make(id, 16)) sp->simulateSetItem(*item, true, 0);
        }
        co_await ctx.ticks(8); // the look applies on the player's next tick; Bedrock drops a use-on right after another
        Vec3 const centre{w.x + .5f, w.y + .5f, w.z + .5f};
        capture.clear();
        if (auto* sp = simPlayer(ctx, uid)) {
            auto  b    = gf::game::blockPos(below);
            auto& held = const_cast<ItemStack&>(sp->getSelectedItem());
            sp->mGameMode->useItemOn(held, b, 1, Vec3{b.x + .5f, b.y + 1.f, b.z + .5f}, HandSlot::Mainhand, nullptr, true);
        }
        co_await ctx.ticks(3);
        ctx.check(ctx.blockAt(w) == id && wet(w), id + " placed into the water keeps it (" + ctx.blockAt(w) + ")");
        auto sent  = capture.sent(centre, 2.f);
        auto place = sent;
        std::erase_if(place, [&](HeardSound const& h) { return !placeSound(h); });
        ctx.check(place.size() == 1, id + ": one place sound for the block placed into water (sent: " + describe(sent) + ")");
        // the others hear the type's place event, as for a block placed in the air
        if (place.size() == 1) ctx.check(place[0].name == kPlace, id + ": the others hear LevelSoundEvent Place (sent: " + describe(sent) + ")");
        ctx.setBlock(w, "minecraft:air");
    }
    (void)box;
});

GF_REGISTER_SELFTEST("families.water_slab_probe", [](Context& ctx) {
    // the client e2e's check (tools/e2e/client/scenarios/families.ps1) that the slab its player placed
    // into water from the water's edge is there and holds the water, on the server: an upstream waterloggable
    // slab with water in its extra layer within a few blocks of a player that is no simulated player.
    // Without such a player (the server scenarios, full) there is nothing to look at.
    constexpr char kSlab[] = "gloamingforest:gloaming_oak_slab";
    auto level = ll::service::getLevel();
    if (!level) {
        ctx.info("no level");
        return;
    }
    int  players = 0;
    bool found   = false;
    auto xyz     = [](BlockPos const& q) { return std::to_string(q.x) + " " + std::to_string(q.y) + " " + std::to_string(q.z); };
    level->forEachPlayer([&](Player& player) {
        if (player.isSimulatedPlayer()) return true;
        ++players;
        auto&      region = player.getDimensionBlockSource();
        auto const feet   = player.getFeetBlockPos();
        // every slab in reach is named (another one nearby must not hide the one the client placed)
        for (int dx = -4; dx <= 4; ++dx)
            for (int dy = -3; dy <= 1; ++dy)
                for (int dz = -4; dz <= 4; ++dz) {
                    BlockPos const q{feet.x + dx, feet.y + dy, feet.z + dz};
                    if (region.getBlock(q).getTypeName() != kSlab) continue;
                    auto const& extra = region.getExtraBlock(q).getTypeName();
                    bool const  wet   = extra == "minecraft:water" || extra == "minecraft:flowing_water";
                    found             = found || wet;
                    ctx.info(std::string(kSlab) + (wet ? " in water at " : " without water at ") + xyz(q) + (wet ? std::string() : " (" + extra + ")"));
                }
        return true;
    });
    if (players == 0) ctx.info("no client player: nothing to probe");
    else if (!found) ctx.info("no " + std::string(kSlab) + " in water near the client players");
});

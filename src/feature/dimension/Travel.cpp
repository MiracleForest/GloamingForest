// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/TFPortalBlock.java (entityInside, getPortalTransitionTime,
//     getPortalDestination, animateTick)
//   src/main/java/twilightforest/components/entity/TFPortalAttachment.java
//   src/main/java/twilightforest/world/TFTeleporter.java (createTransition, createPosition)
//   src/main/java/twilightforest/world/NoReturnTeleporter.java
//   src/main/java/twilightforest/events/CapabilityEvents.java (spawnInTFIfNecessary, playerLogsIn)
//   src/main/java/twilightforest/events/ProgressionEvents.java (performProtectionAndPortalChecks)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Portal travel: the portal timer of players (TFPortalAttachment), vanilla's portal process
// (Entity#setAsInsidePortal / handlePortal / PortalProcessor with TF's transition time: the
// gamerule delay for players, 0 for every other entity) for players and for every other entity
// that can use portals (items, mobs, vehicles with their riders), and the trip itself, plus the
// new-player spawn in the Twilight Forest. Java loads the destination synchronously; here the
// traveller stays where it is while a transient ticking area loads the destination (AreaLoader.h;
// for a player it starts loading 2 s before the trip, while it waits in the portal), then the
// teleporter runs and the traveller moves straight to its arrival spot. gf::world::teleport uses
// the same machinery (destination first, riders along). Vanilla clients
// see the portal overlay as a camera fade; the client mod draws TF's PortalOverlay
// (src/client/dimension) and tells the server so on "dimension.client".

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerRespawnEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/minecraft_camera/CameraInstruction.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/nbt/FloatTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/CameraInstructionPacket.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/Stub.h"
#include "feature/common/Interactions.h"
#include "feature/dimension/AreaLoader.h"
#include "feature/dimension/DimensionConfig.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/Teleporter.h"
#include "feature/dimension/World.h"
#include "feature/fx/Fx.h"
#include "feature/gamerules/GameRules.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::dimension {

namespace tp = teleporter;

namespace {

constexpr int  kMaxTicks           = 60;  // TFPortalAttachment.MAX_TICKS
constexpr int  kPlayerChangeDelay  = 10;  // Player#getDimensionChangingDelay
constexpr int  kEntityChangeDelay  = 300; // Entity#getDimensionChangingDelay
constexpr int  kPortalTicketTicks  = 300; // TicketType.PORTAL timeout
constexpr char kBanishedKey[]      = "gf:dimension:banished";
constexpr char kClientChannel[]    = "dimension.client";
constexpr int  kLoadRadius         = 18; // findPortalCoords scans 16 blocks around, sites reach 2 more

/// Per-entity portal state (rule R16: keyed by unique id, never an Actor*).
struct State {
    bool player = true;
    // TFPortalAttachment (players)
    bool attachInside = false;
    int  portalTimer  = 0;
    // vanilla PortalProcessor + Entity#portalCooldown
    bool     hasProcess     = false;
    bool     insideThisTick = false;
    int      portalTime     = 0;
    BlockPos entry{};
    int      portalCooldown = 0;
    // the destination, loaded while the player waits in the portal (Java loads it at once)
    AreaHandle preload;
    bool       preloading = false;
    int        preloadDim = -1; ///< where the preload is (the trip loads the same area)
    BlockPos   preloadAt{};
    // trip / presentation
    bool travelling    = false;
    bool fading        = false; ///< a camera fade is showing (vanilla overlay)
    int  fadeTick      = 0;     ///< tickCount when the fade is to be renewed
    int  fadeStart     = 0;     ///< tickCount when the fade started to grow
    int  fadeInTicks   = 1;     ///< how long it grows
    bool nativeOverlay = false; ///< the client mod draws TF's PortalOverlay
    int  tickCount     = 0;
};

std::unordered_map<std::int64_t, State>& states() {
    static std::unordered_map<std::int64_t, State> s;
    return s;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }
int   nextInt(int bound) { return std::uniform_int_distribution<int>(0, bound - 1)(rng()); }

int gTick = 0; ///< server ticks since the world was ready

Actor* actorById(std::int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->fetchEntity(ActorUniqueID{id}, false);
}

// ---- banished (TFDataAttachments.BANISHED_TO_TWILIGHT_FOREST, copyOnDeath) ---------------------------

std::unordered_set<std::string>& banished() {
    static std::unordered_set<std::string> s;
    return s;
}
void saveBanished() {
    nlohmann::json list = nlohmann::json::array();
    for (auto const& u : banished()) list.push_back(u);
    game::db::put(kBanishedKey, list);
}
void loadBanished() {
    banished().clear();
    if (auto doc = game::db::get(kBanishedKey); doc && doc->is_array())
        for (auto const& u : *doc)
            if (u.is_string()) banished().insert(u.get<std::string>());
}
void setBanished(Player const& player) {
    if (!banished().insert(player.getUuid().asString()).second) return;
    saveBanished();
}
bool isBanished(Player const& player) { return banished().contains(player.getUuid().asString()); }

// ---- portal index: the portal blocks of the chunks near players ------------------------------------------

struct ChunkPortals {
    std::vector<BlockPos> blocks;
    int                   scannedAt = 0;
    bool                  dirty     = true;
};
using ChunkKey = std::int64_t;
ChunkKey chunkKey(int cx, int cz) { return (static_cast<std::int64_t>(cx) << 32) ^ static_cast<std::uint32_t>(cz); }
int      keyX(ChunkKey k) { return static_cast<int>(k >> 32); }
int      keyZ(ChunkKey k) { return static_cast<int>(static_cast<std::int32_t>(k & 0xffffffff)); }

std::unordered_map<int, std::unordered_map<ChunkKey, ChunkPortals>>& portalIndex() {
    static std::unordered_map<int, std::unordered_map<ChunkKey, ChunkPortals>> idx;
    return idx;
}

constexpr int kIndexRadius = 2;   ///< chunks around each player (the display ticks reach 32 blocks)
constexpr int kRescanTicks = 100; ///< a chunk's portal blocks are read again after this long

bool chunkLoaded(BlockSource& region, int cx, int cz) {
    return region.hasChunksAt(BlockPos{cx * 16, static_cast<int>(region.getMinHeight()), cz * 16}, 0, false);
}

/// Every second: the chunks near players join the index; stale or touched chunks are read again
/// (sub-chunk palettes without a portal block are skipped); chunks that unloaded or have no portal
/// blocks and no player near leave it. Touched chunks are read every tick.
void refreshIndex(Level& level) {
    bool full = gTick % 20 == 0;
    std::unordered_map<int, std::set<ChunkKey>> near;
    if (full) {
        level.forEachPlayer([&](Player& p) {
            BlockPos at  = game::blockPosOf(p.getFeetPos());
            auto&    set = near[static_cast<int>(p.getDimensionId())];
            for (int dx = -kIndexRadius; dx <= kIndexRadius; ++dx)
                for (int dz = -kIndexRadius; dz <= kIndexRadius; ++dz) set.insert(chunkKey((at.x >> 4) + dx, (at.z >> 4) + dz));
            return true;
        });
        for (auto const& [dim, keys] : near) {
            auto& chunks = portalIndex()[dim];
            for (auto k : keys) chunks.try_emplace(k);
        }
    }
    for (auto dit = portalIndex().begin(); dit != portalIndex().end();) {
        auto* region = game::regionOf(dit->first);
        auto& chunks = dit->second;
        auto  nearIt = near.find(dit->first);
        for (auto it = chunks.begin(); it != chunks.end();) {
            auto& c       = it->second;
            int   cx      = keyX(it->first), cz = keyZ(it->first);
            bool  isNear  = nearIt != near.end() && nearIt->second.contains(it->first);
            bool  present = region && chunkLoaded(*region, cx, cz);
            if (full && !isNear && (!present || (c.blocks.empty() && !c.dirty))) {
                it = chunks.erase(it);
                continue;
            }
            if (present && (c.dirty || (full && gTick - c.scannedAt >= kRescanTicks))) {
                c.blocks    = tp::portalBlocksInChunk(*region, cx, cz);
                c.scannedAt = gTick;
                c.dirty     = false;
            }
            ++it;
        }
        if (chunks.empty()) dit = portalIndex().erase(dit);
        else ++dit;
    }
}

// ---- presentation ---------------------------------------------------------------------------------------------

/// The vanilla stand-in for PortalOverlay: a camera fade in the portal's colour.
void sendFade(Player& player, bool clearFirst, float in, float hold, float out) {
    if (clearFirst) {
        auto clear = MinecraftPackets::createPacket(MinecraftPacketIds::CameraInstruction);
        if (clear) {
            static_cast<CameraInstructionPacket&>(*clear).mCameraInstruction->mClear = true;
            clear->sendTo(player);
        }
    }
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::CameraInstruction);
    if (!packet) return;
    CameraInstructionOptions::FadeInstruction::TimeOption time;
    time.mFadeInTime  = in;
    time.mHoldTime    = hold;
    time.mFadeOutTime = out;
    CameraInstructionOptions::FadeInstruction::ColorOption color;
    // the nether portal sprite TF draws, averaged
    color.mRed   = 0.33f;
    color.mGreen = 0.04f;
    color.mBlue  = 0.73f;
    CameraInstructionOptions::FadeInstruction fade;
    fade.mTime  = time;
    fade.mColor = color;
    static_cast<CameraInstructionPacket&>(*packet).mCameraInstruction->mFade = fade;
    packet->sendTo(player);
}

/// TF's overlay is the portal sprite drawn with alpha timer / 60: the sprite's own alpha (about
/// 3/4) times a ramp over the 60 ticks before the trip; after the trip it runs out over 30 ticks.
/// Bedrock's fade has no partial alpha to hold, it only grows to opaque: it grows at TF's rate
/// (0.75 x timer / 60) so that it reaches TF's 0.75 at the trip, and becomes opaque only after it,
/// while the client shows the dimension change. A short hold covers that (the client drops camera
/// instructions while it loads a dimension, so the fade times itself), then it runs out over 1.5 s.
/// It is renewed while the player stays inside (a portal on cooldown, a longer gamerule delay) or
/// waits for the destination.
constexpr float kSpriteAlpha = 0.75f;
constexpr float kFadeHold    = 1.0f;
constexpr float kFadeOut     = 1.5f;

/// How far the client's fade has grown (0..1).
float fadeAlpha(State const& st) {
    return std::clamp(static_cast<float>(st.tickCount - st.fadeStart) / static_cast<float>(std::max(st.fadeInTicks, 1)), 0.0f, 1.0f);
}

/// Takes a camera fade down when the player steps out: TF's overlay runs out over the timer's
/// remaining ticks (2 a tick). A fade that is (nearly) opaque runs out that long; one that is still
/// translucent goes at once (a new fade would start from opaque).
void endFade(Player& player, State& st) {
    if (fadeAlpha(st) >= 0.85f) {
        sendFade(player, true, 0.05f, 0.0f, std::max(static_cast<float>(st.portalTimer) / 40.0f, 0.05f));
    } else if (auto clear = MinecraftPackets::createPacket(MinecraftPacketIds::CameraInstruction)) {
        static_cast<CameraInstructionPacket&>(*clear).mCameraInstruction->mClear = true;
        clear->sendTo(player);
    }
    st.fading = false;
}

/// Called every tick after the timer moved: starts / renews / ends the fade of players without the
/// client mod's overlay.
void updateFade(Player& player, State& st, bool wasInside) {
    if (st.nativeOverlay) return;
    // with a short transition time (creative) TF's overlay hardly shows before the trip: no fade
    if (st.attachInside && !st.fading && !st.travelling && portalDelay(player) >= 20) {
        float in = std::max(static_cast<float>(kMaxTicks - st.portalTimer) / 20.0f / kSpriteAlpha, 0.05f);
        sendFade(player, false, in, kFadeHold, kFadeOut);
        st.fading      = true;
        st.fadeStart   = st.tickCount;
        st.fadeInTicks = static_cast<int>(in * 20.0f);
        st.fadeTick    = st.tickCount + static_cast<int>((in + kFadeHold) * 20.0f); // when it starts to go
        log().info("dimension: portal fade shown to {} (in {:.2f} s)", player.getRealName(), in);
    } else if (st.attachInside && st.fading && st.tickCount >= st.fadeTick) {
        // still inside, or waiting for the destination to load: keep it up
        sendFade(player, false, 0.05f, kFadeHold, kFadeOut);
        st.fadeStart   = st.tickCount;
        st.fadeInTicks = 1;
        st.fadeTick    = st.tickCount + static_cast<int>(kFadeHold * 20.0f);
    } else if (!st.attachInside && wasInside && st.fading && !st.travelling) {
        endFade(player, st);
    }
}

// ---- the portal block under an entity ------------------------------------------------------------------------------

/// An open portal block the entity's bounding box touches (Entity#checkInsideBlocks).
std::optional<BlockPos> touchedPortal(Actor& actor, BlockSource& region) {
    AABB box = actor.getAABB();
    int  x0 = static_cast<int>(std::floor(box.min.x + 1e-7f)), x1 = static_cast<int>(std::floor(box.max.x - 1e-7f));
    int  y0 = static_cast<int>(std::floor(box.min.y + 1e-7f)), y1 = static_cast<int>(std::floor(box.max.y - 1e-7f));
    int  z0 = static_cast<int>(std::floor(box.min.z + 1e-7f)), z1 = static_cast<int>(std::floor(box.max.z - 1e-7f));
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                BlockPos p{x, y, z};
                if (y < region.getMinHeight() || y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) continue;
                if (isOpenPortal(region.getBlock(p))) return p;
            }
    return std::nullopt;
}

bool portalAt(BlockSource& region, BlockPos const& p) {
    return p.y >= region.getMinHeight() && p.y < region.getMaxHeight() && isPortal(region.getBlock(p));
}

/// Entity#isInWall: a solid block around the eyes (a square 0.8 times the entity's width).
bool isInWall(Actor& actor, BlockSource& region) {
    AABB const& bb   = actor.getAABB();
    float       half = (bb.max.x - bb.min.x) * 0.8f / 2.0f;
    Vec3        eye  = actor.getEyePos();
    int         y    = static_cast<int>(std::floor(eye.y));
    if (y < region.getMinHeight() || y >= region.getMaxHeight()) return false;
    for (int x = static_cast<int>(std::floor(eye.x - half)); x <= static_cast<int>(std::floor(eye.x + half)); ++x)
        for (int z = static_cast<int>(std::floor(eye.z - half)); z <= static_cast<int>(std::floor(eye.z + half)); ++z) {
            BlockPos p{x, y, z};
            if (!region.hasChunksAt(p, 0, false)) continue;
            auto const& b = region.getBlock(p);
            if (!b.isAir() && b._isSolid()) return true;
        }
    return false;
}

/// Entity#canUsePortal(false): alive and not a passenger. For other entities also of a kind that
/// goes through portals: Java entities that never move (lightning, effect clouds, hanging entities,
/// eyes of ender, end crystals, evoker fangs) never touch blocks, fishing hooks, the dragon and the
/// wither refuse portals, and the engine's own exclusions (Actor::canChangeDimensionsUsingPortal)
/// apply except its rule against vehicles with riders (Java 1.21 takes the riders along).
bool canUsePortal(Actor& actor) {
    if (!actor.isAlive() || actor.getVehicle()) return false;
    if (actor.isPlayer()) return true;
    switch (actor.getEntityTypeId()) {
    case ActorType::LightningBolt:
    case ActorType::AreaEffectCloud:
    case ActorType::Painting:
    case ActorType::LeashKnot:
    case ActorType::EyeOfEnder:
    case ActorType::EnderCrystal:
    case ActorType::EvocationFang:
    case ActorType::FishingHook:
    case ActorType::Dragon:
    case ActorType::WitherBoss:
        return false;
    default:
        break;
    }
    return actor.hasPassenger() || actor.canChangeDimensionsUsingPortal();
}

/// Entity#setAsInsidePortal.
void setAsInsidePortal(State& st, BlockPos const& pos, int cooldownDelay) {
    if (st.portalCooldown > 0) {
        st.portalCooldown = cooldownDelay;
        return;
    }
    if (!st.hasProcess) {
        st.hasProcess = true;
        st.portalTime = 0;
    }
    st.entry          = pos;
    st.insideThisTick = true;
}

/// Entity#getDimensionChangingDelay: 10 for players and for vehicles a player rides first, else 300.
int changeDelayOf(Actor& actor) {
    if (actor.isPlayer()) return kPlayerChangeDelay;
    auto* first = actor.getFirstPassenger();
    return first && first->isPlayer() ? kPlayerChangeDelay : kEntityChangeDelay;
}

// ---- trips -------------------------------------------------------------------------------------------------------------

/// The riders of a vehicle, off it (they travel along and get on again: Entity#changeDimension).
std::vector<std::int64_t> takePassengers(Actor& vehicle) {
    std::vector<std::int64_t> out;
    for (int i = 0; i < 8; ++i) {
        auto* rider = vehicle.getFirstPassenger();
        if (!rider) break;
        out.push_back(idOf(*rider));
        rider->stopRiding(true, false, false, true);
        if (vehicle.getFirstPassenger() == rider) break; // would not get off
    }
    return out;
}

/// Moves one entity to `at` in `dest`: a player through the engine's dimension change (it arrives
/// a few ticks later, the client loads the dimension), any other entity as a copy made there and
/// the original removed, as Java's Entity#changeDimension does for every entity but players (the
/// copy has a new unique id on Bedrock). Returns the entity's id in `dest`, nullopt on failure.
std::optional<std::int64_t> transferEntity(Actor& actor, int dest, Vec3 const& at, Vec2 const& rotation) {
    if (actor.isPlayer()) {
        actor.teleport(at, DimensionType(dest), rotation);
        return idOf(actor);
    }
    auto* region = game::regionOf(dest);
    if (!region) return std::nullopt;
    CompoundTag tag;
    if (!actor.save(tag)) return std::nullopt;
    tag.remove("UniqueID");
    tag["Pos"]      = ListTag{CompoundTagVariant(FloatTag(at.x)), CompoundTagVariant(FloatTag(at.y)), CompoundTagVariant(FloatTag(at.z))};
    tag["Rotation"] = ListTag{CompoundTagVariant(FloatTag(rotation.y)), CompoundTagVariant(FloatTag(rotation.x))};
    auto copy       = region->spawnActor(tag);
    if (!copy) return std::nullopt;
    auto from = idOf(actor);
    auto to   = idOf(*copy);
    moveItemOwner(from, to);
    actor.remove();
    return to;
}

/// Moves the traveller (and its riders) to `at` in `dest` and waits until it is there. The
/// traveller's id becomes the one it has in `dest`. A player's riders are its shoulder parrots on
/// Bedrock (Java keeps them in the player's data, so they travel along): they come along too.
ll::coro::CoroTask<bool> moveTraveller(tp::Traveller& t, int dest, Vec3 at) {
    auto* a = actorById(t.id);
    if (!a) co_return false;
    std::vector<std::int64_t> riders = takePassengers(*a);
    auto                      moved  = transferEntity(*a, dest, at, Vec2{t.pitch, t.yaw});
    if (!moved) {
        log().warn("dimension: {} could not be moved to {}", t.name, dimensionName(dest));
        for (auto r : riders) // back on, at home
            if (auto* rider = actorById(r)) rider->startRiding(*a, true);
        co_return false;
    }
    if (*moved != t.id) {
        // the portal state (cooldown) goes with the copy
        if (auto it = states().find(t.id); it != states().end()) {
            auto st = it->second;
            states().erase(it);
            states()[*moved] = st;
        }
        t.id = *moved;
    }
    // the riders of a copy (made in the destination at once) follow at once; a player's (its
    // shoulder parrots) only once the player is there, so a player that does not arrive keeps them
    std::vector<std::int64_t> arrivedRiders;
    auto                      sendRiders = [&] {
        for (auto r : riders) {
            auto* rider = actorById(r);
            if (!rider) continue;
            if (auto id = transferEntity(*rider, dest, at, rider->getRotation())) arrivedRiders.push_back(*id);
        }
    };
    if (!t.isPlayer) sendRiders();
    // players change dimension over a few ticks; copies are there at once
    bool there = false;
    for (int i = 0; i < 200 && !there; ++i) {
        a = actorById(t.id);
        if (a) there = static_cast<int>(a->getDimensionId()) == dest;
        if (!there) co_await ll::chrono::ticks(1);
        if (!a && i >= 40) break;
    }
    if (!there) {
        log().warn("dimension: {} did not arrive in {} at {:.1f} {:.1f} {:.1f}", t.name, dimensionName(dest), at.x, at.y, at.z);
        if (t.isPlayer) {
            if (auto* p = actorById(t.id))
                for (auto r : riders) // back on, where the player is
                    if (auto* rider = actorById(r); rider && rider->getDimensionId() == p->getDimensionId()) rider->startRiding(*p, true);
        }
        co_return false;
    }
    if (t.isPlayer) sendRiders();
    // the riders get on again once the traveller is placed (finishTrip: keepRidersOn)
    t.passengers = arrivedRiders;
    if (!riders.empty() && arrivedRiders.size() < riders.size())
        log().warn("dimension: {} took only {} of its {} riders along to {}", t.name, arrivedRiders.size(), riders.size(), dimensionName(dest));
    co_return true;
}

/// The riders that came along get on their vehicle again. The engine finishes a player's
/// dimension change over the next ticks and may drop its passengers meanwhile: they are put back
/// on until they have stayed on for 10 ticks (for at most 3 s).
void keepRidersOn(std::int64_t vehicleId, std::vector<std::int64_t> riders, std::string name, int dest) {
    if (riders.empty()) return;
    ll::coro::keepThis([vehicleId, riders = std::move(riders), name = std::move(name), dest]() -> ll::coro::CoroTask<> {
        int on = 0, steady = 0;
        for (int i = 0; i < 60 && steady < 10; ++i) {
            auto* vehicle = actorById(vehicleId);
            if (!vehicle) break;
            on       = 0;
            bool all = true;
            for (auto r : riders) {
                auto* rider = actorById(r);
                if (!rider || rider->getDimensionId() != vehicle->getDimensionId()) {
                    all = false;
                    continue;
                }
                if (rider->getVehicle() != vehicle) {
                    all = false;
                    rider->startRiding(*vehicle, true);
                }
                if (rider->getVehicle() == vehicle) ++on;
            }
            steady = all ? steady + 1 : 0;
            co_await ll::chrono::ticks(1);
        }
        log().info(
            "dimension: {} took {} riders along to {}, {} back on{}",
            name,
            riders.size(),
            dimensionName(dest),
            on,
            steady >= 10 ? "" : " (they did not stay on: the engine has not finished the dimension change)"
        );
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

/// Moves a player within its dimension with its riders (a teleport drops them: they are taken
/// off, moved and put on again).
void teleportWithRiders(Player& player, int dim, Vec3 const& at, Vec2 const& rotation) {
    auto riders = takePassengers(player);
    player.teleport(at, DimensionType(dim), rotation);
    for (auto r : riders) {
        auto* rider = actorById(r);
        if (!rider) continue;
        rider->teleport(at, DimensionType(dim), rider->getRotation());
        rider->startRiding(player, true);
    }
}

void finishTrip(tp::Traveller const& t, int to, Vec3 const& at, char const* verb = "travelled") {
    auto* a = actorById(t.id);
    if (!a) return;
    if (t.isPlayer) {
        auto& p = static_cast<Player&>(*a);
        // place it exactly (the dimension change may settle it a little off)
        teleportWithRiders(p, to, at, Vec2{t.pitch, t.yaw});
        p.setFallDistance(0.0f);
    }
    keepRidersOn(t.id, t.passengers, t.name, to);
    auto& st      = states()[t.id];
    st.travelling = false;
    st.fading     = false; // the fade runs out by itself after the trip, as TF's overlay does
    log().info(
        "dimension: {} {} from {} to {} at {:.1f} {:.1f} {:.1f}",
        t.name,
        verb,
        dimensionName(t.srcDim),
        dimensionName(to),
        at.x,
        at.y,
        at.z
    );
    if (t.isPlayer) notifyTeleported(static_cast<Player&>(*a), t.srcDim, to);
}

/// Clears the travelling flag when a trip ends, however it ends.
struct TripGuard {
    std::int64_t id;
    TripGuard(std::int64_t i) : id(i) {}
    TripGuard(TripGuard const&)            = delete;
    TripGuard& operator=(TripGuard const&) = delete;
    ~TripGuard() {
        auto it = states().find(id);
        if (it == states().end()) return;
        it->second.travelling = false;
        if (it->second.player && it->second.fading && !it->second.attachInside) {
            // an aborted trip: take the camera fade down (finishTrip does it for a completed one)
            if (auto* p = playerById(id)) endFade(*p, it->second);
        }
    }
};

/// Where the destination's portal is, as far as the cache and the destination's loaded chunks tell
/// now (nullopt: none known yet): the area a trip loads first.
std::optional<BlockPos> knownPortal(int dest, BlockPos const& srcPos, BlockPos const& pos) {
    if (auto cached = tp::cacheGet(dimensionName(dest), srcPos.x, srcPos.z)) return cached->pos;
    if (auto* region = game::regionOf(dest)) return tp::getPortalPosition(*region, pos);
    return std::nullopt;
}

/// TFTeleporter#createTransition (+ createPosition): the destination is loaded first, the traveller
/// stays where it is until the teleporter found or built its portal, then moves there. When the
/// destination does not load in time the trip is called off and the traveller stays at home.
ll::coro::CoroTask<bool> runTransition(tp::Traveller t, int dest, BlockPos pos, bool forcedEntry) {
    TripGuard guard{t.id};
    auto      destName = dimensionName(dest);
    // the area the player's wait preloaded (its portal search done then), else the search now
    std::optional<BlockPos> center;
    if (auto it = states().find(t.id); it != states().end() && it->second.preload && it->second.preloadDim == dest) center = it->second.preloadAt;
    if (!center) center = knownPortal(dest, t.srcPos, pos).value_or(pos);
    auto hold = co_await loadArea(dest, *center, kLoadRadius);
    // (the trip's own hold shares the preloaded area now)
    if (auto it = states().find(t.id); it != states().end()) it->second.preload.reset();
    if (!hold) {
        log().warn("dimension: the trip of {} to {} was called off (the destination did not load)", t.name, destName);
        co_return false;
    }
    auto* region = game::regionOf(dest);
    if (!region || !actorById(t.id)) co_return false;
    AreaHandle          second;
    std::optional<Vec3> result = tp::placeInExistingPortal(*region, t, pos);
    if (!result) {
        // createPosition: moveToSafeCoords, makePortal, placeInExistingPortal
        BlockPos safe = tp::safeColumn(*region, t, pos);
        if (!tp::areaLoaded(*region, safe, kLoadRadius)) {
            second = co_await loadArea(dest, safe, kLoadRadius);
            region = game::regionOf(dest);
            if (!second || !region || !actorById(t.id)) {
                log().warn("dimension: the trip of {} to {} was called off (the safe spot did not load)", t.name, destName);
                co_return false;
            }
        }
        Vec3     safePos   = tp::safePosInColumn(*region, t, Vec3{safe.x + 0.5f, safe.y + 0.5f, safe.z + 0.5f});
        BlockPos portalPos = tp::makePortal(*region, t, safePos, forcedEntry);
        result             = tp::placeInExistingPortal(*region, t, portalPos);
    }
    if (!result) {
        int h  = tp::heightmap(*region, pos.x, pos.z);
        result = Vec3{pos.x + 0.5f, h + 0.5f, pos.z + 0.5f};
    }
    bool moved = co_await moveTraveller(t, dest, *result);
    guard.id   = t.id; // a copy made in the destination has its own id
    // TicketType.PORTAL keeps the destination loaded a while (players load their own area)
    holdFor(std::move(hold), t.isPlayer ? 20 : kPortalTicketTicks);
    holdFor(std::move(second), t.isPlayer ? 20 : kPortalTicketTicks);
    if (!moved) co_return false;
    finishTrip(t, dest, *result);
    co_return true;
}

/// TFPortalBlock#getPortalDestination: the destination dimension and the scaled entry position.
struct Destination {
    int      dim = 0;
    BlockPos pos;
};
std::optional<Destination> portalDestination(int src, BlockPos const& entry, bool quiet = false) {
    auto const& cfg = gf::config::get<DimensionConfig>();
    int         tf  = twilightId();
    if (tf < 0) return std::nullopt;
    std::optional<int> dest = src != tf ? std::optional<int>(tf) : dimensionByName(cfg.originDimension);
    if (!dest) {
        if (!quiet) log().warn("dimension: the portal's origin dimension {} does not exist", cfg.originDimension);
        return std::nullopt;
    }
    double scale = coordinateScaleOf(src) / coordinateScaleOf(*dest);
    // WorldBorder#clampToBounds (Bedrock: the +-29999984 limits)
    auto clamp = [](double v) { return static_cast<int>(std::floor(std::clamp(v, -29999984.0, 29999983.0))); };
    return Destination{*dest, BlockPos{clamp(entry.x * scale), entry.y, clamp(entry.z * scale)}};
}

/// TFPortalBlock#getPortalDestination, then the trip.
void startTrip(Actor& actor, BlockPos const& entry) {
    int  src  = static_cast<int>(actor.getDimensionId());
    auto dest = portalDestination(src, entry);
    if (!dest) return;
    auto t                    = tp::capture(actor);
    states()[t.id].travelling = true;
    log().info(
        "dimension: {} enters the portal at {} {} {} in {} -> {}",
        t.name,
        entry.x,
        entry.y,
        entry.z,
        dimensionName(src),
        dimensionName(dest->dim)
    );
    ll::coro::keepThis([t = std::move(t), dest = dest->dim, newPos = dest->pos]() -> ll::coro::CoroTask<> {
        co_await runTransition(t, dest, newPos, false);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

/// Java loads the destination within the tick of the trip. Here it loads while the player waits
/// in the portal, from kPreloadTicks before the trip on, so the trip finds it ready and leaves on
/// time (the trip's own hold shares the area).
constexpr int kPreloadTicks = 40;

void preloadDestination(Player& player, State& st) {
    int  src  = static_cast<int>(player.getDimensionId());
    auto dest = portalDestination(src, st.entry, true);
    if (!dest) return;
    BlockPos at    = knownPortal(dest->dim, game::blockPosOf(player.getFeetPos()), dest->pos).value_or(dest->pos);
    auto     id    = idOf(player);
    st.preloading  = true;
    ll::coro::keepThis([id, dim = dest->dim, at]() -> ll::coro::CoroTask<> {
        auto hold = co_await loadArea(dim, at, kLoadRadius);
        auto it   = states().find(id);
        if (it == states().end()) co_return;
        it->second.preloading = false;
        // kept while the player is still on its way in (the process runs, no trip yet)
        if (hold && it->second.hasProcess && !it->second.travelling) {
            it->second.preload    = std::move(hold);
            it->second.preloadDim = dim;
            it->second.preloadAt  = at;
        }
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

} // namespace

// ---- CapabilityEvents#newSpawnInTwilightForest ----------------------------------------------------------------------------

void newSpawnInTwilightForest(Player& player) {
    auto const& cfg = gf::config::get<DimensionConfig>();
    if (!cfg.newPlayersSpawnInTF) return;
    int tf = twilightId();
    if (tf < 0) return;
    auto t = tp::capture(player);
    if (states()[t.id].travelling) return; // already on its way
    states()[t.id].travelling = true;
    ll::coro::keepThis([t = std::move(t), tf, portal = cfg.portalForNewPlayer]() -> ll::coro::CoroTask<> {
        TripGuard guard{t.id};
        // level.getHeightmapPos(MOTION_BLOCKING_NO_LEAVES, player.blockPosition()) in the Twilight
        // Forest: its column is loaded first (the player stays where it is meanwhile)
        BlockPos column{t.srcPos.x, 0, t.srcPos.z};
        auto     hold = co_await loadArea(tf, column, kLoadRadius);
        auto*    region = game::regionOf(tf);
        if (!hold || !region || !playerById(t.id)) {
            log().warn("dimension: the new spawn of {} in the Twilight Forest was called off (it did not load)", t.name);
            co_return;
        }
        BlockPos newDefaultSpawn{column.x, tp::heightmap(*region, column.x, column.z), column.z};
        // TF sets the respawn point and the banished mark right after changeDimension, whatever
        // became of the trip: here as soon as the trip starts (a player who leaves or a trip that
        // fails later stays banished, with the Twilight Forest as its spawn)
        if (auto* p = playerById(t.id)) {
            p->setRespawnPosition(newDefaultSpawn, DimensionType(tf));
            setBanished(*p);
            log().info("dimension: {} now lives in the Twilight Forest (spawn {} {} {})", t.name, newDefaultSpawn.x, newDefaultSpawn.y, newDefaultSpawn.z);
        }
        bool arrived = false;
        if (portal) {
            arrived = co_await runTransition(t, tf, newDefaultSpawn, true);
        } else {
            // NoReturnTeleporter#createNoPortalTransition
            AreaHandle second;
            BlockPos   safe = tp::safeColumn(*region, t, newDefaultSpawn);
            if (!tp::areaLoaded(*region, safe, kLoadRadius)) {
                second = co_await loadArea(tf, safe, kLoadRadius);
                region = game::regionOf(tf);
                if (!second || !region || !playerById(t.id)) co_return;
            }
            Vec3 safePos = tp::safePosInColumn(*region, t, Vec3{safe.x + 0.5f, safe.y + 0.5f, safe.z + 0.5f});
            Vec3 at        = tp::noReturnPlacePosition(*region, t, safePos);
            auto traveller = t;
            arrived        = co_await moveTraveller(traveller, tf, at);
            if (arrived) finishTrip(traveller, tf, at);
            holdFor(std::move(second), 20);
        }
        holdFor(std::move(hold), 20);
        if (!arrived) log().warn("dimension: the new spawn trip of {} did not complete; its spawn stays in the Twilight Forest", t.name);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

// ---- gf::world::teleport ----------------------------------------------------------------------------------------------------

bool teleportPlayer(Player& player, int dest, Vec3 const& at, float yaw, float pitch) {
    tp::Traveller t;
    t.id       = idOf(player);
    t.isPlayer = true;
    t.name     = player.getRealName();
    t.srcDim   = static_cast<int>(player.getDimensionId());
    t.srcExact = player.getFeetPos();
    t.srcPos   = game::blockPosOf(t.srcExact);
    t.yaw      = yaw;
    t.pitch    = pitch;
    auto& st   = states()[t.id];
    if (st.travelling) {
        log().warn("dimension: {} is already on its way somewhere: no teleport to {}", t.name, dimensionName(dest));
        return false;
    }
    // ServerPlayer#teleportTo: off its vehicle first
    if (player.getVehicle()) player.stopRiding(true, false, false, true);
    if (dest == t.srcDim) {
        // within the dimension at once; the riders (shoulder parrots) come along
        teleportWithRiders(player, dest, at, Vec2{pitch, yaw});
        notifyTeleported(player, t.srcDim, dest);
        return true;
    }
    // TFTeleporter#placeEntity semantics: the destination is loaded first (the player waits where
    // it is), then the player and its riders move there
    st.travelling = true;
    ll::coro::keepThis([t, dest, at]() mutable -> ll::coro::CoroTask<> {
        TripGuard guard{t.id};
        auto      hold   = co_await loadArea(dest, game::blockPosOf(at), kLoadRadius);
        auto      failed = [&](char const* why) {
            log().warn("dimension: the teleport of {} to {} did not complete ({})", t.name, dimensionName(dest), why);
            if (auto* p = playerById(t.id)) {
                p->sendMessage(trFor(*p, "gf.dimension.teleport_failed", "The teleport to {} did not complete.", dimensionName(dest)));
            }
        };
        if (!hold) {
            failed("the destination did not load");
            co_return;
        }
        if (!playerById(t.id)) co_return;
        bool moved = co_await moveTraveller(t, dest, at);
        holdFor(std::move(hold), 20);
        if (!moved) {
            failed("the player did not arrive");
            co_return;
        }
        finishTrip(t, dest, at, "teleported");
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
    return true;
}

bool hasRespawnPosition(Player const& player) {
    // Bedrock always keeps a respawn point: without a bed / anchor / forced spawn it is the world
    // spawn itself (spawn block == position == the shared spawn column, in the overworld); a bed
    // sets the bed block and the stand position, a forced spawn the position only (block MIN)
    auto const& rp    = *player.mPlayerRespawnPoint;
    BlockPos    block = *rp.mSpawnBlockPos;
    BlockPos    at    = *rp.mPlayerPosition;
    auto        level = ll::service::getLevel();
    if (!level) return true;
    BlockPos shared = level->getSharedSpawnPos();
    bool     world  = rp.mDimension->mValue == 0 && block == at && block.x == shared.x && block.z == shared.z;
    return !world;
}

void unbanish(std::string const& uuid) {
    if (banished().erase(uuid) > 0) saveBanished();
}

void portalIndexTouch(int dim, BlockPos const& pos) {
    for (int dx = -1; dx <= 1; ++dx) // a 2x2 portal may reach into the next chunk
        for (int dz = -1; dz <= 1; ++dz) portalIndex()[dim][chunkKey((pos.x + dx) >> 4, (pos.z + dz) >> 4)].dirty = true;
}

std::vector<BlockPos> indexedPortals(int dim) {
    std::vector<BlockPos> out;
    auto                  it = portalIndex().find(dim);
    if (it == portalIndex().end()) return out;
    for (auto const& [k, c] : it->second) out.insert(out.end(), c.blocks.begin(), c.blocks.end());
    return out;
}

namespace {

// ---- animateTick (server-side stand-in for the client's random display ticks) --------------------------------------------

/// Probability that Java's ClientLevel#animateTick (667 samples at radius 16 and 667 at radius 32,
/// each axis offset rand(r) - rand(r)) picks a block at this offset in one tick.
double displayTickChance(int dx, int dy, int dz) {
    auto axis = [](int d, int r) { return std::max(0, r - std::abs(d)) / static_cast<double>(r * r); };
    return 667.0 * axis(dx, 16) * axis(dy, 16) * axis(dz, 16) + 667.0 * axis(dx, 32) * axis(dy, 32) * axis(dz, 32);
}

/// TFPortalBlock#animateTick ([VanillaCopy] of NetherPortalBlock#animateTick).
void animateTick(Dimension& dim, Block const& state, BlockPos const& pos) {
    int random = nextInt(100);
    if (game::intState(state, kOneWayState).value_or(0) != 0 && random < 80) return;
    if (random == 0) {
        fx::sound(dim, kWhooshSound, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 0.5f, nextFloat() * 0.4f + 0.8f);
    }
    for (int i = 0; i < 4; ++i) {
        Vec3 at{pos.x + nextFloat(), pos.y + 1.0f, pos.z + nextFloat()};
        Vec3 speed{(nextFloat() - 0.5f) * 0.5f, nextFloat(), (nextFloat() - 0.5f) * 0.5f};
        fx::particle(dim, "minecraft:portal", at, speed);
    }
}

/// Whether gf::fx is still F0's stub (nothing to show then; checked once a second).
bool fxIsStub() {
    static int  checkedAt = -1000;
    static bool stub      = false;
    if (gTick - checkedAt >= 20 || gTick < checkedAt) {
        checkedAt  = gTick;
        auto calls = core::stubCalls();
        stub       = std::find(calls.begin(), calls.end(), "fx.particle") != calls.end();
    }
    return stub;
}

/// Each indexed portal block near a player plays its display tick with the chance the nearest
/// player's client would pick it (Java runs animateTick per client; one roll per block here, the
/// particles are visible to everyone near).
void displayTicks(Level& level) {
    if (portalIndex().empty() || fxIsStub()) return;
    std::unordered_map<int, std::vector<BlockPos>> players;
    level.forEachPlayer([&](Player& p) {
        players[static_cast<int>(p.getDimensionId())].push_back(game::blockPosOf(p.getFeetPos()));
        return true;
    });
    for (auto const& [dim, chunks] : portalIndex()) {
        auto pit = players.find(dim);
        if (pit == players.end()) continue;
        auto* region = game::regionOf(dim);
        if (!region) continue;
        for (auto const& [k, c] : chunks) {
            for (auto const& b : c.blocks) {
                double chance = 0.0;
                for (auto const& at : pit->second) chance = std::max(chance, displayTickChance(b.x - at.x, b.y - (at.y + 1), b.z - at.z));
                if (chance <= 0.0 || nextFloat() >= chance) continue;
                if (!portalAt(*region, b)) continue;
                animateTick(region->getDimension(), region->getBlock(b), b);
            }
        }
    }
}

// ---- per tick -------------------------------------------------------------------------------------------------------------

/// Entity#handlePortal (PortalProcessor#processPortalTeleportation) for one entity. Java's trip
/// happens within the tick; here the traveller waits for its destination, and its cooldown and
/// portal process stand still meanwhile (they run from the arrival on, as in Java).
void handlePortal(Actor& actor, State& st, int transitionTime) {
    if (st.travelling) return;
    if (st.portalCooldown > 0) --st.portalCooldown; // processPortalCooldown
    if (!st.hasProcess) return;
    bool teleport = false;
    if (!st.insideThisTick) {
        st.portalTime = std::max(st.portalTime - 4, 0); // decayTick
    } else {
        st.insideThisTick = false;
        teleport          = canUsePortal(actor) && st.portalTime++ >= transitionTime;
    }
    if (teleport) {
        // the process stays (portalTime transition + 1) and decays from the arrival on: a player
        // who steps into a portal again before it ran out goes on from where it is
        st.portalCooldown = changeDelayOf(actor); // setPortalCooldown
        startTrip(actor, st.entry);
    } else if (st.portalTime <= 0) {
        st.hasProcess = false; // hasExpired: portalProcess = null
        st.preload.reset();
    }
}

void tickPlayer(Player& player) {
    auto  id     = idOf(player);
    auto& st     = states()[id];
    auto* region = game::regionOf(static_cast<int>(player.getDimensionId()));
    if (!region) return;
    st.player = true;
    ++st.tickCount;
    bool wasInside = st.attachInside;

    handlePortal(player, st, portalDelay(player));

    // TFPortalBlock#entityInside
    if (!st.travelling) {
        if (auto pos = touchedPortal(player, *region)) {
            if (!unworthy(player) && canUsePortal(player)) {
                setAsInsidePortal(st, *pos, kPlayerChangeDelay);
                st.attachInside = true;
            }
        }
    }
    // inside a portal (not on cooldown) with the trip near: its destination loads meanwhile
    if (st.hasProcess && st.insideThisTick && !st.travelling && !st.preload && !st.preloading) {
        int delay = portalDelay(player);
        if (delay > 1 && st.portalTime >= std::max(0, delay - kPreloadTicks)) preloadDestination(player, st);
    }

    // TFPortalAttachment#tick
    if (st.attachInside) {
        st.portalTimer = std::min(st.portalTimer + 1, kMaxTicks);
        if (!isInWall(player, *region)) {
            BlockPos pos = game::blockPosOf(player.getFeetPos());
            if (!portalAt(*region, pos) && !portalAt(*region, BlockPos{pos.x, pos.y - 1, pos.z})) st.attachInside = false;
        }
    } else if (st.portalTimer > 0) {
        st.portalTimer -= 2;
        if (st.portalTimer < 0) st.portalTimer = 0;
    }
    updateFade(player, st, wasInside);

    // ProgressionEvents#performProtectionAndPortalChecks: portal creation
    auto const& cfg = gf::config::get<DimensionConfig>();
    if (!cfg.disablePortalCreation && st.tickCount % (!cfg.checkPortalPlacement ? 100 : 20) == 0) {
        // Java op levels (0..4) on Bedrock's command permission levels (0 any, 1 operators, 2 admin, 3 host, 4 owner)
        static constexpr int kBedrockLevel[5] = {0, 1, 1, 2, 4};
        int need = kBedrockLevel[std::clamp(cfg.portalCreationPermission, 0, 4)];
        if (static_cast<int>(player.getCommandPermissionLevel()) >= need) {
            checkForPortalCreation(player, *region, cfg.portalCreationPermission >= 3 ? 4.0f : 32.0f);
        }
    }
}

/// Every other entity: its portal process, then the indexed portal blocks it touches.
void tickEntities() {
    // handlePortal for the entities with portal state
    for (auto it = states().begin(); it != states().end();) {
        auto& st = it->second;
        if (st.player) {
            ++it;
            continue;
        }
        auto* actor = actorById(it->first);
        if (actor && !actor->isPlayer()) handlePortal(*actor, st, 0);              // getPortalTransitionTime: 0
        else if (st.portalCooldown > 0 && !st.travelling) --st.portalCooldown; // (a copy on its way keeps it)
        if (!st.hasProcess && st.portalCooldown <= 0 && !st.travelling) it = states().erase(it);
        else ++it;
    }
    // TFPortalBlock#entityInside for the entities in the indexed portal blocks, and what the
    // portal's water does to every entity in it (players too)
    std::vector<std::pair<std::int64_t, BlockPos>> inside;
    std::vector<std::pair<std::int64_t, int>>      wet;
    for (auto const& [dim, chunks] : portalIndex()) {
        auto* region = game::regionOf(dim);
        if (!region) continue;
        for (auto const& [k, c] : chunks) {
            if (c.blocks.empty()) continue;
            BlockPos lo = c.blocks.front(), hi = c.blocks.front();
            for (auto const& b : c.blocks) {
                lo = BlockPos{std::min(lo.x, b.x), std::min(lo.y, b.y), std::min(lo.z, b.z)};
                hi = BlockPos{std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
            }
            AABB box{Vec3(lo.x, lo.y, lo.z), Vec3(hi.x + 1, hi.y + 1, hi.z + 1)};
            for (auto* actor : region->getEntities(box, 0.0f)) {
                if (!actor) continue;
                wet.emplace_back(idOf(*actor), dim);
                if (actor->isPlayer() || !canUsePortal(*actor)) continue;
                if (auto pos = touchedPortal(*actor, *region)) inside.emplace_back(idOf(*actor), *pos);
            }
        }
    }
    for (auto const& [id, dim] : wet) {
        auto* actor  = actorById(id);
        auto* region = game::regionOf(dim);
        if (actor && region && static_cast<int>(actor->getDimensionId()) == dim) portalWaterTick(*actor, *region);
    }
    portalWaterEndTick();
    for (auto const& [id, pos] : inside) {
        auto* actor = actorById(id);
        if (!actor) continue;
        auto& st  = states()[id];
        st.player = false;
        if (st.travelling) continue;
        setAsInsidePortal(st, pos, changeDelayOf(*actor));
    }
}

} // namespace

int portalDelay(Player const& player) {
    bool creative = player.isCreative() || player.isSpectator(); // abilities.invulnerable
    auto value    = gamerules::get(creative ? kCreativeDelayRule : kDefaultDelayRule);
    if (auto const* v = std::get_if<int>(&value)) return *v;
    return creative ? kCreativeDelay : kDefaultDelay; // the gamerule store has no value yet
}

int portalTimer(Player const& player) {
    auto it = states().find(idOf(player));
    return it == states().end() ? 0 : it->second.portalTimer;
}

void setPortalCooldown(Actor& actor, int ticks) {
    auto& st          = states()[idOf(actor)];
    st.player         = actor.isPlayer();
    st.portalCooldown = ticks;
}

PortalProcessView portalProcessOf(Actor const& actor) {
    auto it = states().find(idOf(actor));
    if (it == states().end()) return {};
    auto const& st = it->second;
    return PortalProcessView{st.hasProcess, st.portalTime, st.portalCooldown, st.travelling, static_cast<bool>(st.preload)};
}

class TravelModule {
public:
    void load() {
        game::db::onWorldReady([] {
            states().clear();
            portalIndex().clear();
            gTick = 0;
            resetNotified();
            refreshPortalTypes();
            portalWaterClear();
            loadBanished();
            tp::cacheLoad();
        });
        game::db::onWorldSave([] { tp::cacheSave(); });
        game::db::onWorldClosed([] {
            states().clear();
            portalIndex().clear();
            banished().clear();
            tp::cacheClear();
            portalWaterClear();
            clearItemOwners(); // unique ids of another world mean other items
        });
    }

    void install() {
        gamerules::registerInt(kDefaultDelayRule, kDefaultDelay);
        gamerules::registerInt(kCreativeDelayRule, kCreativeDelay);
        registerPortalBlockEvents();
        installItemOwners();
        installPortalFluid();
        net::on(kClientChannel, [](Player& player, nlohmann::json const& msg) {
            bool native                          = msg.value("overlay", false);
            states()[idOf(player)].nativeOverlay = native;
            log().info("dimension: {} draws the portal overlay {}", player.getRealName(), native ? "natively" : "through the camera fade");
        });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent& ev) {
            ++gTick;
            refreshIndex(ev.level());
            std::vector<std::int64_t> ids;
            ev.level().forEachPlayer([&](Player& p) {
                ids.push_back(idOf(p));
                return true;
            });
            for (auto id : ids)
                if (auto* p = playerById(id)) tickPlayer(*p);
            tickEntities();
            displayTicks(ev.level());
        }));
        // CapabilityEvents#playerLogsIn
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
            auto& p  = ev.self();
            auto  id = idOf(p);
            if (!isBanished(p)) {
                interactions::nextTick([id] {
                    if (auto* pl = playerById(id)) newSpawnInTwilightForest(*pl);
                });
            }
        }));
        // CapabilityEvents#spawnInTFIfNecessary
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerRespawnEvent>([](ll::event::PlayerRespawnEvent& ev) {
            auto& p = ev.self();
            if (p.isClientSide() || hasRespawnPosition(p)) return;
            auto id = idOf(p);
            interactions::nextTick([id] {
                if (auto* pl = playerById(id)) newSpawnInTwilightForest(*pl);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            states().erase(idOf(ev.self()));
        }));
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        net::off(kClientChannel);
        uninstallPortalFluid();
        uninstallItemOwners();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace gf::feature::dimension

namespace gf::portal {

std::optional<core::GlobalPos> destinationFor(Player const& player) {
    namespace dim = feature::dimension;
    auto const& cfg = gf::config::get<dim::DimensionConfig>();
    int         src = static_cast<int>(player.getDimensionId());
    int         tf  = dim::twilightId();
    if (tf < 0) return std::nullopt;
    std::optional<int> dest = src != tf ? std::optional<int>(tf) : dim::dimensionByName(cfg.originDimension);
    if (!dest) return std::nullopt;
    double   scale = dim::coordinateScaleOf(src) / dim::coordinateScaleOf(*dest);
    BlockPos at    = game::blockPosOf(player.getFeetPos());
    BlockPos newPos{static_cast<int>(std::floor(at.x * scale)), at.y, static_cast<int>(std::floor(at.z * scale))};
    if (auto cached = dim::teleporter::cacheGet(dim::dimensionName(*dest), at.x, at.z)) {
        return core::GlobalPos{*dest, cached->pos.x, cached->pos.y, cached->pos.z};
    }
    if (auto* region = game::regionOf(*dest)) {
        if (auto found = dim::teleporter::getPortalPosition(*region, newPos)) return core::GlobalPos{*dest, found->x, found->y, found->z};
    }
    return core::GlobalPos{*dest, newPos.x, newPos.y, newPos.z};
}

} // namespace gf::portal

GF_REGISTER_MODULE(gf::feature::dimension::TravelModule);

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
// Portal travel for players: the portal timer (TFPortalAttachment), vanilla's portal process
// (PortalProcessor with TF's transition time) and the trip itself, plus the new-player spawn in
// the Twilight Forest. Vanilla clients see the portal overlay as a camera fade; the client mod
// draws TF's PortalOverlay (src/client/dimension) and tells the server so on "dimension.client".

#include <algorithm>
#include <cmath>
#include <random>
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
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/CameraInstructionPacket.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/dimension/DimensionConfig.h"
#include "feature/dimension/Internal.h"
#include "feature/dimension/Teleporter.h"
#include "feature/common/Interactions.h"
#include "feature/dimension/World.h"
#include "feature/fx/Fx.h"
#include "feature/gamerules/GameRules.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::dimension {

namespace tp = teleporter;

namespace {

constexpr int  kMaxTicks          = 60; // TFPortalAttachment.MAX_TICKS
constexpr int  kDimensionDelay    = 10; // Player#getDimensionChangingDelay
constexpr char kBanishedKey[]     = "gf:dimension:banished";
constexpr char kClientChannel[]   = "dimension.client";
constexpr int  kLoadRadius        = 18; // findPortalCoords scans 16 blocks around, sites reach 2 more

/// Per-player portal state (rule R16: keyed by unique id, never a Player*).
struct State {
    // TFPortalAttachment
    bool attachInside = false;
    int  portalTimer  = 0;
    // vanilla PortalProcessor + Entity#portalCooldown
    bool     hasProcess     = false;
    bool     insideThisTick = false;
    int      portalTime     = 0;
    BlockPos entry{};
    int      portalCooldown = 0;
    // trip / presentation
    bool travelling    = false;
    bool fading        = false; ///< a camera fade is showing (vanilla overlay)
    bool nativeOverlay = false; ///< the client mod draws TF's PortalOverlay
    int  tickCount     = 0;
    std::vector<BlockPos> nearbyPortals; ///< portal blocks within 32 blocks (animateTick)
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

// ---- banished (TFDataAttachments.BANISHED_TO_TWILIGHT_FOREST, copyOnDeath) ---------------------------

std::unordered_set<std::string>& banished() {
    static std::unordered_set<std::string> s;
    return s;
}
void loadBanished() {
    banished().clear();
    if (auto doc = game::db::get(kBanishedKey); doc && doc->is_array())
        for (auto const& u : *doc)
            if (u.is_string()) banished().insert(u.get<std::string>());
}
void setBanished(Player const& player) {
    if (!banished().insert(player.getUuid().asString()).second) return;
    nlohmann::json list = nlohmann::json::array();
    for (auto const& u : banished()) list.push_back(u);
    game::db::put(kBanishedKey, list);
}
bool isBanished(Player const& player) { return banished().contains(player.getUuid().asString()); }

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

/// Called every tick after the timer moved: starts / ends the fade of players without the client
/// mod's overlay.
void updateFade(Player& player, State& st, bool wasInside) {
    if (st.nativeOverlay) return;
    if (st.attachInside && !wasInside && !st.fading) {
        float in = static_cast<float>(kMaxTicks - st.portalTimer) / 20.0f;
        sendFade(player, false, std::max(in, 0.05f), 600.0f, 1.5f);
        st.fading = true;
        log().info("dimension: portal fade shown to {}", player.getRealName());
    } else if (!st.attachInside && wasInside && st.fading && !st.travelling) {
        sendFade(player, true, 0.0f, 0.0f, std::max(static_cast<float>(st.portalTimer) / 40.0f, 0.05f));
        st.fading = false;
    }
}

// ---- the portal block under a player ------------------------------------------------------------------------------

/// An open portal block the player's bounding box touches (Entity#checkInsideBlocks).
std::optional<BlockPos> touchedPortal(Player& player, BlockSource& region) {
    AABB box = player.getAABB();
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

// ---- trips -------------------------------------------------------------------------------------------------------------

float topOf(int dim) {
    auto h = heightOf(dim);
    return h ? static_cast<float>(h->max - 2) : 200.0f;
}

/// Moves the player to `pos` in `dim` (any dimension) and waits until it is there and the area
/// within `radius` blocks of `area` is loaded. False when the player left or it took over 10 s.
ll::coro::CoroTask<bool> arriveAt(std::int64_t id, int dim, Vec3 pos, float yaw, float pitch, BlockPos area, int radius) {
    auto* p = playerById(id);
    if (!p) co_return false;
    p->teleport(pos, DimensionType(dim), Vec2{pitch, yaw});
    for (int i = 0; i < 200; ++i) {
        co_await ll::chrono::ticks(1);
        p = playerById(id);
        if (!p) co_return false;
        if (static_cast<int>(p->getDimensionId()) != dim) continue;
        auto* region = game::regionOf(dim);
        if (region && tp::areaLoaded(*region, area, radius)) co_return true;
    }
    log().warn("dimension: the area at {} {} {} of dimension {} did not load in time", area.x, area.y, area.z, dim);
    co_return false;
}

void finishTrip(std::int64_t id, int from, int to, Vec3 const& at, float yaw, float pitch) {
    auto* p = playerById(id);
    if (!p) return;
    p->teleport(at, DimensionType(to), Vec2{pitch, yaw});
    auto& st      = states()[id];
    st.travelling = false;
    if (st.fading) {
        // the timer runs down by 2 a tick from where it was: fade out over its last part
        sendFade(*p, true, 0.0f, 0.0f, std::max(static_cast<float>(st.portalTimer) / 40.0f, 0.05f));
        st.fading = false;
    }
    log().info(
        "dimension: {} travelled from {} to {} at {:.1f} {:.1f} {:.1f}",
        p->getRealName(),
        dimensionName(from),
        dimensionName(to),
        at.x,
        at.y,
        at.z
    );
    notifyTeleported(*p, from, to);
}

/// TFTeleporter#createTransition (+ createPosition) run across the destination's chunk loading.
/// Clears the travelling flag when a trip ends, however it ends.
struct TripGuard {
    std::int64_t id;
    ~TripGuard() {
        if (auto it = states().find(id); it != states().end()) it->second.travelling = false;
    }
};

ll::coro::CoroTask<> runTransition(tp::Traveller t, int dest, BlockPos pos, bool forcedEntry) {
    TripGuard guard{t.id};
    auto      destName = dimensionName(dest);
    auto  cached   = tp::cacheGet(destName, t.srcPos.x, t.srcPos.z);
    // where the existing portal is, as far as the destination's loaded chunks tell now
    std::optional<BlockPos> known = cached ? std::optional<BlockPos>(cached->pos) : std::nullopt;
    if (!known) {
        if (auto* region = game::regionOf(dest)) known = tp::getPortalPosition(*region, pos);
    }
    BlockPos target = known.value_or(pos);
    Vec3     first{target.x + 0.5f, known ? target.y + 1.0f : topOf(dest), target.z + 0.5f};
    if (!co_await arriveAt(t.id, dest, first, t.yaw, t.pitch, target, kLoadRadius)) co_return;
    auto* region = game::regionOf(dest);
    if (!region) co_return;
    std::optional<Vec3> result = tp::placeInExistingPortal(*region, t, pos);
    if (!result) {
        // createPosition: moveToSafeCoords, makePortal, placeInExistingPortal
        BlockPos safe = tp::safeColumn(*region, t, pos);
        if (!tp::areaLoaded(*region, safe, kLoadRadius)) {
            if (!co_await arriveAt(t.id, dest, Vec3{safe.x + 0.5f, topOf(dest), safe.z + 0.5f}, t.yaw, t.pitch, safe, kLoadRadius)) co_return;
            region = game::regionOf(dest);
            if (!region) co_return;
        }
        Vec3     safePos   = tp::safePosInColumn(*region, t, Vec3{safe.x + 0.5f, safe.y + 0.5f, safe.z + 0.5f});
        BlockPos portalPos = tp::makePortal(*region, t, safePos, forcedEntry);
        result             = tp::placeInExistingPortal(*region, t, portalPos);
    }
    if (!result) {
        int h  = tp::heightmap(*region, pos.x, pos.z);
        result = Vec3{pos.x + 0.5f, h + 0.5f, pos.z + 0.5f};
    }
    finishTrip(t.id, t.srcDim, dest, *result, t.yaw, t.pitch);
}

/// TFPortalBlock#getPortalDestination, then the trip.
void startTrip(Player& player, BlockPos const& entry) {
    auto const& cfg = gf::config::get<DimensionConfig>();
    int         src = static_cast<int>(player.getDimensionId());
    int         tf  = twilightId();
    if (tf < 0) return;
    std::optional<int> dest = src != tf ? std::optional<int>(tf) : dimensionByName(cfg.originDimension);
    if (!dest) {
        log().warn("dimension: the portal's origin dimension {} does not exist", cfg.originDimension);
        return;
    }
    double scale = coordinateScaleOf(src) / coordinateScaleOf(*dest);
    // WorldBorder#clampToBounds (Bedrock: the +-29999984 limits)
    auto clamp = [](double v) { return static_cast<int>(std::floor(std::clamp(v, -29999984.0, 29999983.0))); };
    BlockPos newPos{clamp(entry.x * scale), entry.y, clamp(entry.z * scale)};
    auto     t = tp::capture(player);
    states()[t.id].travelling = true;
    log().info(
        "dimension: {} enters the portal at {} {} {} in {} -> {}",
        t.name,
        entry.x,
        entry.y,
        entry.z,
        dimensionName(src),
        dimensionName(*dest)
    );
    ll::coro::keepThis([t = std::move(t), dest = *dest, newPos]() -> ll::coro::CoroTask<> {
        co_await runTransition(t, dest, newPos, false);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

} // namespace

// ---- CapabilityEvents#newSpawnInTwilightForest ----------------------------------------------------------------------------

void newSpawnInTwilightForest(Player& player) {
    auto const& cfg = gf::config::get<DimensionConfig>();
    if (!cfg.newPlayersSpawnInTF) return;
    int tf = twilightId();
    if (tf < 0) return;
    auto t                    = tp::capture(player);
    states()[t.id].travelling = true;
    ll::coro::keepThis([t = std::move(t), tf, portal = cfg.portalForNewPlayer]() -> ll::coro::CoroTask<> {
        TripGuard guard{t.id};
        BlockPos  column{t.srcPos.x, 0, t.srcPos.z};
        if (!co_await arriveAt(t.id, tf, Vec3{column.x + 0.5f, topOf(tf), column.z + 0.5f}, t.yaw, t.pitch, column, kLoadRadius)) co_return;
        auto* region = game::regionOf(tf);
        if (!region) co_return;
        BlockPos newDefaultSpawn{column.x, tp::heightmap(*region, column.x, column.z), column.z};
        if (portal) {
            co_await runTransition(t, tf, newDefaultSpawn, true);
        } else {
            // NoReturnTeleporter#createNoPortalTransition
            BlockPos safe = tp::safeColumn(*region, t, newDefaultSpawn);
            if (!tp::areaLoaded(*region, safe, kLoadRadius)) {
                if (!co_await arriveAt(t.id, tf, Vec3{safe.x + 0.5f, topOf(tf), safe.z + 0.5f}, t.yaw, t.pitch, safe, kLoadRadius)) co_return;
                region = game::regionOf(tf);
                if (!region) co_return;
            }
            Vec3 safePos = tp::safePosInColumn(*region, t, Vec3{safe.x + 0.5f, safe.y + 0.5f, safe.z + 0.5f});
            finishTrip(t.id, t.srcDim, tf, tp::noReturnPlacePosition(*region, t, safePos), t.yaw, t.pitch);
        }
        auto* p = playerById(t.id);
        if (!p) co_return;
        p->setRespawnPosition(newDefaultSpawn, DimensionType(tf));
        setBanished(*p);
        log().info("dimension: {} now lives in the Twilight Forest (spawn {} {} {})", t.name, newDefaultSpawn.x, newDefaultSpawn.y, newDefaultSpawn.z);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

namespace {

bool hasRespawnPosition(Player const& player) { return *player.mPlayerRespawnPoint->mSpawnBlockPos != BlockPos::MIN(); }

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

void displayTicks(Player& player, BlockSource& region, State& st) {
    // the portal blocks within reach of the display ticks, refreshed every second
    if (st.tickCount % 20 == 1) st.nearbyPortals = tp::portalBlocksNear(region, game::blockPosOf(player.getFeetPos()), 31);
    if (st.nearbyPortals.empty()) return;
    BlockPos at = game::blockPosOf(player.getFeetPos());
    for (auto const& p : st.nearbyPortals) {
        double chance = displayTickChance(p.x - at.x, p.y - (at.y + 1), p.z - at.z);
        if (chance <= 0.0 || nextFloat() >= chance) continue;
        if (!portalAt(region, p)) continue;
        animateTick(region.getDimension(), region.getBlock(p), p);
    }
}

// ---- per tick -------------------------------------------------------------------------------------------------------------

void tickPlayer(Player& player) {
    auto  id     = idOf(player);
    auto& st     = states()[id];
    auto* region = game::regionOf(static_cast<int>(player.getDimensionId()));
    if (!region) return;
    ++st.tickCount;
    bool wasInside = st.attachInside;

    // Entity#handlePortal (PortalProcessor#processPortalTeleportation)
    if (st.portalCooldown > 0) --st.portalCooldown;
    if (st.hasProcess && !st.travelling) {
        if (!st.insideThisTick) {
            st.portalTime = std::max(st.portalTime - 4, 0);
            if (st.portalTime <= 0) st.hasProcess = false;
        } else {
            st.insideThisTick = false;
            if (st.portalTime++ >= portalDelay(player)) {
                st.portalCooldown = kDimensionDelay;
                st.hasProcess     = false;
                st.portalTime     = 0;
                startTrip(player, st.entry);
            }
        }
    }

    // TFPortalBlock#entityInside
    if (!st.travelling) {
        if (auto pos = touchedPortal(player, *region)) {
            if (!unworthy(player) && player.isAlive() && !player.getVehicle()) {
                // Entity#setAsInsidePortal
                if (st.portalCooldown > 0) {
                    st.portalCooldown = kDimensionDelay;
                } else {
                    if (!st.hasProcess) {
                        st.hasProcess = true;
                        st.portalTime = 0;
                    }
                    st.entry          = *pos;
                    st.insideThisTick = true;
                }
                st.attachInside = true;
            }
        }
    }

    // TFPortalAttachment#tick
    if (st.attachInside) {
        st.portalTimer = std::min(st.portalTimer + 1, kMaxTicks);
        BlockPos pos   = game::blockPosOf(player.getFeetPos());
        if (!portalAt(*region, pos) && !portalAt(*region, BlockPos{pos.x, pos.y - 1, pos.z})) st.attachInside = false;
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

    displayTicks(player, *region, st);
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

class TravelModule {
public:
    void load() {
        game::db::onWorldReady([] {
            states().clear();
            resetNotified();
            loadBanished();
            tp::cacheLoad();
        });
        game::db::onWorldSave([] { tp::cacheSave(); });
        game::db::onWorldClosed([] {
            states().clear();
            banished().clear();
            tp::cacheClear();
        });
    }

    void install() {
        gamerules::registerInt(kDefaultDelayRule, kDefaultDelay);
        gamerules::registerInt(kCreativeDelayRule, kCreativeDelay);
        registerPortalBlockEvents();
        installItemOwners();
        net::on(kClientChannel, [](Player& player, nlohmann::json const& msg) {
            bool native                          = msg.value("overlay", false);
            states()[idOf(player)].nativeOverlay = native;
            log().info("dimension: {} draws the portal overlay {}", player.getRealName(), native ? "natively" : "through the camera fade");
        });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](ll::event::ServerLevelTickEvent& ev) {
            std::vector<std::int64_t> ids;
            ev.level().forEachPlayer([&](Player& p) {
                ids.push_back(idOf(p));
                return true;
            });
            for (auto id : ids)
                if (auto* p = playerById(id)) tickPlayer(*p);
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

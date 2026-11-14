// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/events/ProgressionEvents.java
//   src/main/java/gloamingforest/network/AreaProtectionPacket.java
//   src/main/java/gloamingforest/network/StructureProtectionPacket.java
//   src/main/java/gloamingforest/entity/ProtectionBox.java
//   src/main/java/gloamingforest/util/PlayerHelper.java
//   src/main/java/gloamingforest/util/landmarks/LandmarkUtil.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Landmark protection (ProgressionEvents). While tfEnforcedProgression is on, a player without the
// advancements a progression landmark requires cannot break, use or place blocks in its protected
// pieces, nor hurt its monsters (kobolds excepted). Every refusal shows the protected boxes: upstream
// sends AreaProtectionPacket to the clients within 64 blocks, which spawn client-side
// ProtectionBox entities and 20 protection particles. Here the companion clients get
// landmark.area_protection, and every player near (vanilla clients too) sees the boxes as
// gloamingforest:protection_box entities (BP timer: 60 ticks, refreshed while the protection
// repeats; never kept with the world: a box loaded back with its chunk is removed) and the
// gloamingforest:protection particles. Every 100 ticks companions also get the locked structure's
// boxes (landmark.structure_protection, StructureProtectionPacket) for the protection weather.
// Handlers run at interactions priority Highest (rule R8).
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/server/ServerPlayer.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"

#include "core/Stub.h"
#include "core/landmark/Grid.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/gamerules/GameRules.h"
#include "feature/landmark/Internal.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace gf::feature::landmark {

namespace ia = gf::interactions;

namespace {

constexpr char kProtectionBox[]      = "gloamingforest:protection_box";
constexpr char kProtectionParticle[] = "gloamingforest:protection";
constexpr int  kBoxLifetime          = 60; ///< ProtectionBox#lifeTime
/// The size and refresh properties of the box entity (BP entity, synced to every client).
constexpr char kSizeX[] = "gf:size_x", kSizeY[] = "gf:size_y", kSizeZ[] = "gf:size_z", kEpoch[] = "gf:epoch";

struct LiveBox {
    int           dim = 0;
    BlockBox      box;
    std::int64_t  actor   = 0;
    std::uint64_t expires = 0;
    int           epoch   = 0; ///< refreshes so far (the gf:epoch property, mod 1024)
};

struct State {
    std::mutex                                   mutex;
    std::optional<bool>                          enforcedOverride;
    std::vector<LiveBox>                         boxes;
    std::unordered_map<std::int64_t, std::string> lastStructurePayload; ///< per companion
    std::vector<ia::HandlerId>                   handlers;
    ll::event::ListenerPtr                       disconnect;
    std::uint64_t                                tick = 0;
};
State& state() {
    static State s;
    return s;
}

/// ProgressionEvents#isBlockProtectedFromBreaking: blocks tagged progression_allow_breaking may be broken.
bool blockProtectedFromBreaking(std::string const& blockId) {
    return !tags::has(tags::Registry::Block, "gloamingforest:progression_allow_breaking", blockId);
}

/// Java's Enemy: Bedrock's "monster" family.
bool isEnemy(Actor const& actor) {
    static HashedString const monster("monster");
    return actor.hasFamily(monster);
}

/// The record and descriptor protecting a position against a player, if any (the checks of
/// isAreaProtected without its effects).
struct Hit {
    RecordPtr   record;
    Info const* info = nullptr;
};
std::optional<Hit> protectionAt(Player const& player, int dim, int x, int y, int z) {
    if (player.isCreative() || player.isSpectator() || !enforced()) return std::nullopt;
    auto rec = nearestLandmark(dim, x >> 4, z >> 4, GlobalPos{dim, x, y, z});
    if (!rec) return std::nullopt;
    auto const* inf = info(rec->start.key);
    if (!inf || !inf->traits.progression) return std::nullopt;
    bool inPiece = false;
    for (std::size_t i = 0; i < rec->pieces.size() && !inPiece; ++i)
        inPiece = rec->pieces[i].box.contains(x, y, z) && pieceProtectedAt(*rec, i, x, y, z);
    if (!inPiece) return std::nullopt;
    if (hasRequiredAdvancements(player, *inf)) return std::nullopt;
    return Hit{rec, inf};
}

/// The positions a multi-block item fills next to its target (BlockEvent.EntityMultiPlaceEvent):
/// doors and tall plants grow up one block, beds reach out one block where the player looks.
std::vector<BlockPos> multiPlaceExtras(std::string const& itemId, BlockPos const& target, Player const& player) {
    static char const* const tall[] = {"minecraft:sunflower", "minecraft:lilac", "minecraft:rose_bush", "minecraft:peony", "minecraft:tall_grass",
                                       "minecraft:large_fern", "minecraft:pitcher_plant", "minecraft:small_dripleaf_block", "minecraft:tall_dry_grass"};
    bool door = itemId.ends_with("_door");
    bool plant = std::find(std::begin(tall), std::end(tall), itemId) != std::end(tall);
    if (door || plant) return {BlockPos{target.x, target.y + 1, target.z}};
    if (itemId == "minecraft:bed" || (itemId.starts_with("minecraft:") && itemId.ends_with("_bed"))) {
        auto [dx, dz] = game::facingOffset(game::playerFacing(player));
        return {BlockPos{target.x + dx, target.y, target.z + dz}};
    }
    return {};
}

BlockPos stepOf(BlockPos const& p, int face) {
    static constexpr int dx[] = {0, 0, 0, 0, -1, 1}, dy[] = {-1, 1, 0, 0, 0, 0}, dz[] = {0, 0, -1, 1, 0, 0};
    if (face < 0 || face > 5) return p;
    return {p.x + dx[face], p.y + dy[face], p.z + dz[face]};
}

Json boxArray(BlockBox const& b) { return Json::array({b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ}); }

std::uint64_t now() {
    if (auto level = ll::service::getLevel()) return level->getCurrentTick().tickID;
    return 0;
}

/// Raises a BP event (the protection box's size and lifetime events).
void raise(Actor& actor, std::string const& event) {
    VariantParameterList params{};
    params.mSelf = &actor;
    actor.executeEvent(event, params);
}

/// Queues an int entity property of the box entity (false: refused).
bool setIntProperty(Actor& actor, char const* name, int value) {
    auto props = actor.getEntityContext().tryGetComponent<PropertyComponent>();
    return props && ActorPropertyUtils::tryQueueIntValue(*props, actor, HashedString(name).getHash(), value);
}

/// Every protection box entity of the level (stray ones included).
std::vector<Actor*> boxEntities(Level& level) {
    std::vector<Actor*> out;
    for (auto* a : level.getRuntimeActorList())
        if (a && a->getTypeName() == kProtectionBox) out.push_back(a);
    return out;
}

/// Spawns a protection box entity for `box`, or refreshes the living one (ProtectionBox#matches /
/// resetLifetime). The entity stands on the box's top center; its geometry hangs below and is scaled by the
/// size properties (sizeX * 16 + 2 pixels as ProtectionBoxModel).
void spawnOrRefreshBox(int dim, BlockBox const& box) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& s = state();
    // the lock is never held while the game runs entity code (spawning, BP events): a listener
    // there may call back into the landmark unit
    std::vector<std::int64_t> matching;
    {
        std::lock_guard lock(s.mutex);
        for (auto const& live : s.boxes)
            if (live.dim == dim && live.box == box) matching.push_back(live.actor);
    }
    for (auto actorId : matching) {
        ActorUniqueID id;
        id.rawID = actorId;
        if (auto* a = level->fetchEntity(id, false); a && a->isAlive()) {
            // ProtectionBox#resetLifetime: the BP timer starts again, and the epoch tells the client's
            // render controller the 60 ticks restart (its alpha fade over the last 20)
            raise(*a, "gf:refresh");
            auto expires = now() + kBoxLifetime;
            int  epoch   = 0;
            {
                std::lock_guard lock(s.mutex);
                for (auto& live : s.boxes)
                    if (live.actor == actorId) {
                        live.expires = expires;
                        live.epoch   = (live.epoch + 1) % 1024;
                        epoch        = live.epoch;
                    }
            }
            (void)setIntProperty(*a, kEpoch, epoch);
            return;
        }
    }
    if (!matching.empty()) {
        // the BP timer despawned them: forget the entries
        std::lock_guard lock(s.mutex);
        std::erase_if(s.boxes, [&](LiveBox const& b) { return b.dim == dim && b.box == box; });
    }
    auto* region = game::regionOf(dim);
    if (!region) return;
    int   sx = box.maxX - box.minX + 1, sy = box.maxY - box.minY + 1, sz = box.maxZ - box.minZ + 1;
    float x  = static_cast<float>(box.minX) + static_cast<float>(sx) / 2.0f;
    float y  = static_cast<float>(box.maxY + 1) + 1.0f / 16.0f; // the geometry hangs below the entity
    float z  = static_cast<float>(box.minZ) + static_cast<float>(sz) / 2.0f;
    auto* actor = spawnEntity(*region, kProtectionBox, x, y, z, [sx, sy, sz](Actor& a) {
        // the size properties gf:size_x / _y / _z (declared in the BP entity, synced to every client)
        if (!a.getEntityContext().tryGetComponent<PropertyComponent>()) {
            log().warn("landmark: the protection box has no entity properties");
            return;
        }
        bool ok = true;
        for (auto [name, value] : {std::pair{kSizeX, sx}, std::pair{kSizeY, sy}, std::pair{kSizeZ, sz}})
            ok = setIntProperty(a, name, std::clamp(value, 1, 1023)) && ok;
        static bool warned = false;
        if (!ok && !warned) {
            warned = true;
            log().warn("landmark: the protection box size properties were refused");
        }
    });
    if (!actor) return;
    LiveBox         entry{dim, box, actor->getOrCreateUniqueID().rawID, now() + kBoxLifetime};
    std::lock_guard lock(s.mutex);
    s.boxes.push_back(entry);
}

void sendStructureProtection(Player& player, Json const& payload) {
    auto& s   = state();
    auto  id  = player.getOrCreateUniqueID().rawID;
    auto  txt = payload.dump();
    {
        std::lock_guard lock(s.mutex);
        auto&           last = s.lastStructurePayload[id];
        if (last == txt) return;
        last = txt;
    }
    net::send(player, "landmark.structure_protection", payload);
}

/// ProgressionEvents#checkForLockedStructuresSendPacket.
void checkForLockedStructures(Player& player) {
    int  dim  = static_cast<int>(player.getDimensionId());
    auto feet = player.getFeetBlockPos();
    auto rec  = nearestLandmark(dim, feet.x >> 4, feet.z >> 4, GlobalPos{dim, feet.x, feet.y, feet.z});
    if (!rec) return; // upstream: no start, nothing sent
    auto const* inf = info(rec->start.key);
    if (inf && inf->traits.progression && !hasRequiredAdvancements(player, *inf)) {
        Json boxes = Json::array();
        for (std::size_t i = 0; i < rec->pieces.size(); ++i) {
            // isPieceProtected(piece), the same rule as isAreaProtected (asked at the piece's center)
            auto const& p    = rec->pieces[i];
            auto        c    = core::landmark::boxCenter(p.box);
            bool        prot = pieceProtectedAt(*rec, i, c.x, (p.box.minY + p.box.maxY) / 2, c.z);
            auto        b    = prot ? BlockBox{p.box.minX - 4, p.box.minY - 4, p.box.minZ - 4, p.box.maxX + 4, p.box.maxY + 4, p.box.maxZ + 4} : p.box;
            boxes.push_back(Json{{"box", boxArray(b)}, {"protected", prot}});
        }
        sendStructureProtection(player, Json{{"boxes", boxes}});
        return;
    }
    sendStructureProtection(player, Json{{"boxes", nullptr}}); // all clear
}

} // namespace

// ---- queries -----------------------------------------------------------------------------------------

bool enforced() {
    auto& s = state();
    {
        std::lock_guard lock(s.mutex);
        if (s.enforcedOverride) return *s.enforcedOverride;
    }
    bool value = gamerules::getBool("tfEnforcedProgression");
    // upstream's default (true) while the gamerule store is the F0 stub
    static bool const storeIsStub = stubHit("gamerules.get");
    return storeIsStub ? true : value;
}

void setEnforcedOverride(std::optional<bool> value) {
    std::lock_guard lock(state().mutex);
    state().enforcedOverride = value;
}

bool hasRequiredAdvancements(Player const& player, Info const& inf) {
    // PlayerHelper#playerHasRequiredAdvancements (through doesPlayerHaveRequiredAdvancements): its
    // loop returns at the first advancement, done or not, so the first one of the lock decides (every
    // upstream structure has at most one); no lock: allowed. The progression store answers isDone()
    auto const& lock = inf.d.lockAdvancements;
    return lock.empty() || progression::has(player, lock.front());
}

bool pieceProtectedAt(Record const& record, std::size_t index, int x, int y, int z) {
    if (index >= record.pieces.size()) return false;
    if (auto gen = generatorFor(record.start.key); gen && gen->pieceProtected) return gen->pieceProtected(record.start, BlockPos{x, y, z});
    return record.pieces[index].protectedPiece;
}

bool isAreaProtectedQuery(Player const& player, int dim, int x, int y, int z) { return protectionAt(player, dim, x, y, z).has_value(); }

bool isAreaProtected(Player& player, int dim, int x, int y, int z, bool effects) {
    auto hit = protectionAt(player, dim, x, y, z);
    if (!hit) return false;
    if (effects) {
        // send protection packet (deferred: no world edits inside the refused interaction, R16)
        auto boxes  = pieceBoxesAt(*hit->record, x, y, z);
        auto id     = player.getOrCreateUniqueID();
        auto key    = hit->info->d.key;
        log().info("landmark: protection of {} at {} {} {} refused {}", key, x, y, z, player.getRealName());
        ia::nextTick([dim, x, y, z, boxes, id, key] {
            showAreaProtection(dim, x, y, z, boxes);
            // send a hint monster?
            auto level = ll::service::getLevel();
            if (!level) return;
            if (auto* p = level->getPlayer(id))
                if (auto const* inf = info(key)) trySpawnHintMonster(*inf, *p, x, y, z);
        });
    }
    return true;
}

void showAreaProtection(int dim, int x, int y, int z, std::vector<BlockBox> const& boxes) {
    auto level = ll::service::getLevel();
    if (!level) return;
    // PacketDistributor.sendToPlayersNear(level, null, x, y, z, 64, AreaProtectionPacket)
    Json payload{{"pos", Json::array({x, y, z})}, {"boxes", Json::array()}};
    for (auto const& b : boxes) payload["boxes"].push_back(boxArray(b));
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim) return true;
        if (p.distanceToSqr(Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}) >= 64.0f * 64.0f) return true;
        net::send(p, "landmark.area_protection", payload); // companions only
        return true;
    });
    auto const& cfg = config::get<LandmarkConfig>();
    if (cfg.protectionBoxes)
        for (auto const& b : boxes) spawnOrRefreshBox(dim, b);
    if (!cfg.protectionParticles) return;
    // 20 protection particles around the block (AreaProtectionPacket#handle)
    static thread_local core::landmark::JavaRandom random(static_cast<std::int64_t>(now() * 31 + 7));
    std::vector<std::array<float, 3>>             points;
    for (int i = 0; i < 20; i++) {
        float px = static_cast<float>(x) + 0.5f + random.nextFloat() - random.nextFloat();
        float py = static_cast<float>(y) + 0.5f + random.nextFloat() - random.nextFloat();
        float pz = static_cast<float>(z) + 0.5f + random.nextFloat() - random.nextFloat();
        points.push_back({px, py, pz});
    }
    emitParticles(dim, kProtectionParticle, points);
}

std::vector<BlockBox> liveProtectionBoxes() {
    std::lock_guard       lock(state().mutex);
    std::vector<BlockBox> out;
    for (auto const& b : state().boxes) out.push_back(b.box);
    return out;
}

std::string describeProtectionBoxes() {
    auto level = ll::service::getLevel();
    if (!level) return {};
    std::vector<std::int64_t> ids;
    {
        std::lock_guard lock(state().mutex);
        for (auto const& b : state().boxes) ids.push_back(b.actor);
    }
    std::string out;
    for (auto raw : ids) {
        ActorUniqueID id;
        id.rawID = raw;
        auto* a  = level->fetchEntity(id, false);
        if (!a) continue;
        auto p = a->getFeetPos();
        out += "[" + a->getTypeName() + " at " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + "] ";
    }
    return out;
}

std::size_t sweepStrayProtectionBoxes() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    std::vector<std::int64_t> tracked;
    {
        std::lock_guard lock(state().mutex);
        for (auto const& b : state().boxes) tracked.push_back(b.actor);
    }
    std::size_t removed = 0;
    for (auto* a : boxEntities(*level)) {
        auto id = a->getOrCreateUniqueID().rawID;
        if (std::find(tracked.begin(), tracked.end(), id) != tracked.end()) continue;
        raise(*a, "gf:expire"); // minecraft:instant_despawn
        ++removed;
    }
    if (removed > 0) log().info("landmark: removed {} stray protection box(es)", removed);
    return removed;
}

std::size_t protectionBoxEntities() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    std::lock_guard lock(state().mutex);
    std::size_t     n = 0;
    for (auto const& b : state().boxes) {
        ActorUniqueID id;
        id.rawID = b.actor;
        if (auto* a = level->fetchEntity(id, false); a && a->isAlive()) ++n;
    }
    return n;
}

// ---- tick --------------------------------------------------------------------------------------------

void protectionTick(std::uint64_t tick) {
    auto level = ll::service::getLevel();
    if (!level) return;
    // forget expired boxes (the BP timer despawns the entity itself)
    {
        auto            t = now();
        std::lock_guard lock(state().mutex);
        std::erase_if(state().boxes, [t](LiveBox const& b) { return t > b.expires + 5; });
    }
    // upstream's ProtectionBox is a client entity, never saved: a box that came back with its chunk goes
    // (every 5 s: a loaded box's own BP timer runs out after 3 s anyway once its chunk ticks)
    if (tick % 100 == 50) (void)sweepStrayProtectionBoxes();
    // ProgressionEvents#performProtectionAndPortalChecks: every 100 ticks the nearby locked structure
    if (tick % 100 != 0 || !enforced()) return;
    std::vector<ActorUniqueID> companions;
    level->forEachPlayer([&](Player& p) {
        if (net::isCompanion(p)) companions.push_back(p.getOrCreateUniqueID());
        return true;
    });
    for (auto id : companions) {
        auto* p = level->getPlayer(id);
        if (!p) continue;
        if (p->isCreative() || p->isSpectator()) sendStructureProtection(*p, Json{{"boxes", nullptr}});
        else checkForLockedStructures(*p);
    }
}

// ---- handlers ----------------------------------------------------------------------------------------

void installProtection() {
    auto& s = state();
    // preventLockedAreaBlockBreaking
    s.handlers.push_back(ia::onDestroy({}, [](ia::DestroyContext const& c) {
        if (!blockProtectedFromBreaking(c.blockId)) return ia::Result::Pass;
        return isAreaProtected(c.player, c.pos.dim, c.pos.x, c.pos.y, c.pos.z, true) ? ia::Result::Deny : ia::Result::Pass;
    }, ia::priority::Highest));
    // preventLockedAreaBlockPlacing + preventLockedAreaBlockInteracting (any right-click on a block of
    // the area) + preventLockedAreaMultiblocks (the other blocks a door / bed / tall plant fills).
    // Bedrock repeats use-on while the button is held: effects only for the first event.
    s.handlers.push_back(ia::onUseOn({}, [](ia::UseOnContext const& c) {
        if (isAreaProtected(c.player, c.pos.dim, c.pos.x, c.pos.y, c.pos.z, c.firstEvent)) return ia::Result::Deny;
        if (c.itemId.empty()) return ia::Result::Pass;
        auto* region = game::regionOf(c.pos.dim);
        if (!region) return ia::Result::Pass;
        BlockPos clicked = game::blockPos(c.pos);
        BlockPos target  = game::replaceable(*region, clicked) ? clicked : stepOf(clicked, c.face);
        auto     extras  = multiPlaceExtras(c.itemId, target, c.player);
        if (extras.empty()) return ia::Result::Pass;
        // EntityMultiPlaceEvent#getReplacedBlockSnapshots: the target block and the extra ones
        extras.insert(extras.begin(), target);
        for (auto const& p : extras) {
            if (blockProtectedFromBreaking(game::blockIdAt(*region, p)) && isAreaProtected(c.player, c.pos.dim, p.x, p.y, p.z, c.firstEvent))
                return ia::Result::Deny;
        }
        return ia::Result::Pass;
    }, ia::priority::Highest));
    // preventLockedAreaEntityDamage (melee: the attack itself is refused)
    s.handlers.push_back(ia::onAttack({}, [](ia::EntityContext const& c) {
        if (!isEnemy(c.entity) || c.entityId == "gloamingforest:kobold") return ia::Result::Pass;
        auto pos = c.entity.getFeetBlockPos();
        int  dim = static_cast<int>(c.entity.getDimensionId());
        return isAreaProtected(c.player, dim, pos.x, pos.y, pos.z, true) ? ia::Result::Deny : ia::Result::Pass;
    }, ia::priority::Highest));
    // a companion that joins (again) starts with no structure boxes: send them on the next check
    // (upstream sends every 100 ticks without remembering what it sent)
    s.disconnect = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& e) {
        auto            id = e.self().getOrCreateUniqueID().rawID;
        std::lock_guard lock(state().mutex);
        state().lastStructurePayload.erase(id);
    });
    static bool combatRegistered = false;
    if (!combatRegistered) {
        combatRegistered = true;
        net::onCompanionJoined([](Player& p) {
            auto            id = p.getOrCreateUniqueID().rawID;
            std::lock_guard lock(state().mutex);
            state().lastStructurePayload.erase(id);
        });
        // preventLockedAreaEntityDamage (any damage a player causes: projectiles, ...)
        combat::onIncoming(combat::priority::Highest, [](combat::DamageInfo& d) {
            if (!d.attacker || !d.attacker->isPlayer()) return combat::Verdict::Pass;
            if (!isEnemy(d.victim) || d.victim.getTypeName() == "gloamingforest:kobold") return combat::Verdict::Pass;
            auto pos = d.victim.getFeetBlockPos();
            int  dim = static_cast<int>(d.victim.getDimensionId());
            return isAreaProtected(*static_cast<Player*>(d.attacker), dim, pos.x, pos.y, pos.z, true) ? combat::Verdict::Cancel : combat::Verdict::Pass;
        });
    }
}

void uninstallProtection() {
    for (auto id : state().handlers) ia::remove(id);
    state().handlers.clear();
    if (state().disconnect) ll::event::EventBus::getInstance().removeListener(state().disconnect);
    state().disconnect = nullptr;
}

void resetProtection() {
    std::lock_guard lock(state().mutex);
    state().boxes.clear();
    state().lastStructurePayload.clear();
}

} // namespace gf::feature::landmark

namespace gf::landmark {

bool isProtectedFor(Player const& player, BlockPos const& pos) {
    return feature::landmark::isAreaProtectedQuery(player, static_cast<int>(player.getDimensionId()), pos.x, pos.y, pos.z);
}

bool enforcedProgression() { return feature::landmark::enforced(); }

} // namespace gf::landmark

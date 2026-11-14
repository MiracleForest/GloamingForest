// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/processors/SpawnerProcessor.java (the spawn data it
//     writes: generic.scale for entity_width_max, the custom spawn rules of compile)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// What a Java spawner keeps in its SpawnData and a Bedrock spawner does not: F7 records it with the
// spawner block (BlockData "structure"."spawner", BlockEntities.cpp) and applies it when that
// spawner spawns.
//   - custom_spawn_rules (BaseSpawner: the block and sky light at the spawn position must lie in
//     the ranges): spawns outside them are vetoed (SpawningMobEvent);
//   - the generic.scale attribute (entity_width_max): spawned mobs get the size factor. Java saves
//     attributes with the entity; Bedrock keeps a mob's size in its bounding box data and collision
//     box, saves part of that and sets the rest from the definition when it loads, grows up or
//     reloads its components. So the factor and the size the kit gave the mob are kept on the mob
//     (actor tags, saved with it) and the factor goes back on every part the game reset: after a load
//     (actor-load hook) and whenever the size changed under it (checked every few ticks).
#include "feature/structure/Internal.h"

#include <charconv>
#include <cmath>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/dataloadhelper/DataLoadHelper.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/vanilla_components/AABBShapeComponent.h"
#include "mc/deps/vanilla_components/ActorDataBoundingBoxComponent.h"
#include "mc/deps/vanilla_components/ActorDataDirtyFlagsComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/LevelChunkBlockActorStorage.h"

#include "core/Keys.h"
#include "feature/ext/entity/EntityExt.h"
#include "mod/Log.h"

namespace gf::structure::spawners {

namespace {

/// The actor tags that carry a mob's generic.scale ("gf_structure_scale:0.5714286") and the size
/// the kit gave it last ("gf_structure_size:<data scale>,<data width>,<data height>,<box width>,
/// <box height>").
constexpr std::string_view kScaleTag = "gf_structure_scale:";
constexpr std::string_view kSizeTag  = "gf_structure_size:";
/// Scaled mobs' sizes are checked this often (ticks).
constexpr int kCheckInterval = 10;

/// What makes a mob's size on Bedrock: the bounding box data synced to clients (scale, width,
/// height) and the collision box dimensions.
struct Size {
    float v[5]{};
};

/// A scaled mob the kit watches (server thread): its factor and the size it last gave it.
struct Scaled {
    float scale = 1.0f;
    Size  applied;
};
std::unordered_map<std::int64_t, Scaled> gScaled;
/// Mobs to (re)scale in a few ticks: spawned, loaded (the hook may run off the server thread).
struct PendingScale {
    std::int64_t id    = 0;
    int          ticks = 0;
    int          tries = 0; ///< checks while the mob was not in the level yet
};
/// A pending mob is looked for this many times (one tick apart) before it is given up: a loaded
/// mob may be added to the level a while after it was read.
constexpr int kPendingTries = 200;
std::mutex                 gPendingMutex;
std::vector<PendingScale>  gPending;
int                        gTicks  = 0;
int                        gVetoes = 0;
bool                       gHeld   = false; ///< size checks held (selftests measure the game's own reset)
ll::event::ListenerPtr     gSpawningListener;
ll::event::ListenerPtr     gSpawnedListener;
bool                       gHooked = false;

void queueScale(std::int64_t id, int ticks) {
    std::lock_guard lock(gPendingMutex);
    for (auto& p : gPending)
        if (p.id == id) {
            p.ticks = std::max(p.ticks, ticks);
            return;
        }
    gPending.push_back(PendingScale{id, ticks});
}

void appendFloat(std::string& out, float v) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof(buf), v);
    out.append(buf, r.ptr);
}

std::string scaleTag(float scale) {
    std::string t(kScaleTag);
    appendFloat(t, scale);
    return t;
}

std::string sizeTag(Size const& size) {
    std::string t(kSizeTag);
    for (int i = 0; i < 5; i++) {
        if (i) t += ',';
        appendFloat(t, size.v[i]);
    }
    return t;
}

std::optional<float> parseScaleTag(std::string const& tag) {
    if (!tag.starts_with(kScaleTag)) return std::nullopt;
    float       v     = 0;
    auto const* begin = tag.data() + kScaleTag.size();
    auto        r     = std::from_chars(begin, tag.data() + tag.size(), v);
    if (r.ec != std::errc{} || !std::isfinite(v) || v <= 0) return std::nullopt;
    return v;
}

std::optional<Size> parseSizeTag(std::string const& tag) {
    if (!tag.starts_with(kSizeTag)) return std::nullopt;
    Size        size;
    char const* p   = tag.data() + kSizeTag.size();
    char const* end = tag.data() + tag.size();
    for (int i = 0; i < 5; i++) {
        if (i) {
            if (p >= end || *p != ',') return std::nullopt;
            ++p;
        }
        auto r = std::from_chars(p, end, size.v[i]);
        if (r.ec != std::errc{} || !std::isfinite(size.v[i])) return std::nullopt;
        p = r.ptr;
    }
    return size;
}

std::optional<Size> sizeTagOf(Actor const& actor) {
    if (!actor.hasTags()) return std::nullopt;
    for (auto const& t : actor.getTags())
        if (auto s = parseSizeTag(t)) return s;
    return std::nullopt;
}

std::array<float, 3>* boxData(Actor& actor) {
    auto bb = actor.getEntityContext().tryGetComponent<ActorDataBoundingBoxComponent>();
    return bb ? &bb->mValue : nullptr;
}

std::optional<Size> sizeOf(Actor& actor) {
    auto* data = boxData(actor);
    if (!data) return std::nullopt;
    auto const& shape = *actor.mBuiltInComponents->mAABBShapeComponent;
    return Size{{(*data)[0], (*data)[1], (*data)[2], shape.mBBDim->x, shape.mBBDim->y}};
}

void setSize(Actor& actor, Size const& size) {
    auto* data = boxData(actor);
    if (!data) return;
    for (std::size_t i = 0; i < 3; i++) (*data)[i] = size.v[i];
    auto& dirty = actor.mEntityData.get().mDirtyFlags.get()->mDirtyFlags.get();
    for (auto id : {ActorDataIDs::Reserved038, ActorDataIDs::Reserved053, ActorDataIDs::Reserved054}) dirty.set(static_cast<std::size_t>(id));
    auto& shape  = *actor.mBuiltInComponents->mAABBShapeComponent;
    shape.mBBDim = Vec2{size.v[3], size.v[4]};
    actor._refreshAABB();
}

bool same(float a, float b) { return std::abs(a - b) <= 1e-4f * std::max(1.0f, std::abs(a)); }

bool sameSize(Size const& a, Size const& b) {
    for (int i = 0; i < 5; i++)
        if (!same(a.v[i], b.v[i])) return false;
    return true;
}

/// Puts the factor on the parts of the mob's size the game set anew: a part that still is what the
/// kit made it last (`applied`) stays (Bedrock saves some of a mob's size with it), a part the game
/// reset (loading, growing up, components reloaded) is the game's value times the factor. Without
/// `applied` (a mob just scaled) every part is multiplied. The result is remembered on the mob.
void rescale(Actor& actor, float scale, std::optional<Size> const& applied) {
    auto current = sizeOf(actor);
    if (!current) return;
    Size target  = *current;
    bool changed = false;
    for (int i = 0; i < 5; i++) {
        if (applied && same(current->v[i], applied->v[i])) continue;
        float base  = (i == 0 && current->v[0] <= 0) ? 1.0f : current->v[i]; // the data scale defaults to 1
        target.v[i] = base * scale;
        changed     = true;
    }
    if (changed) setSize(actor, target);
    auto& s   = gScaled[actor.getOrCreateUniqueID().rawID];
    s.scale   = scale;
    s.applied = target;
    if (!changed && sizeTagOf(actor).has_value()) return;
    for (auto const& t : actor.getTags())
        if (t.starts_with(kSizeTag)) (void)actor.removeTag(t);
    (void)actor.addTag(sizeTag(target));
}

std::string canonical(ActorDefinitionIdentifier const& id) { return id.mCanonicalName->getString(); }

std::string namespaced(std::string id) { return id.find(':') == std::string::npos ? "minecraft:" + id : id; }

/// The record of the spawner at `pos` while that spawner still spawns the mob the record was made
/// for. Changing a spawner's mob (a spawn egg, a command) replaces Java's whole SpawnData, its
/// generic.scale attribute and custom spawn rules included: a record of another mob is dropped.
std::optional<Json> recordFor(BlockSource& source, BlockPos const& pos, std::string const& entityId) {
    auto record = blockentities::spawnerRecord(source, pos);
    if (!record) return std::nullopt;
    if (namespaced(record->value("entity", std::string{})) != namespaced(entityId)) {
        blockentities::forgetSpawnerRecord(source, pos);
        return std::nullopt;
    }
    return record;
}

bool inLightRange(Json const& range, int value) {
    if (!range.is_array() || range.size() != 2) return true;
    return value >= range[0].get<int>() && value <= range[1].get<int>();
}

} // namespace

// ---- the actor-load hook ---------------------------------------------------------------------------
// An engine hook outside F8's entity contract (rule R8: entity save / load is F8's; its
// Behaviour{load} only reaches the entity types F8 registers): kept minimal (it only queues a
// rescale for mobs carrying the kit's scale tag) and disclosed for A8 to fold into F8's load
// dispatch once gf::entity has a generic on-load event.

LL_TYPE_INSTANCE_HOOK(
    StructureScaledActorLoadHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::$load,
    bool,
    ::CompoundTag const&  tag,
    ::DataLoadHelper&     dataLoadHelper
) {
    bool ok = origin(tag, dataLoadHelper);
    // a mob scaled by a structure spawner loads with part of its size saved and part set from its
    // definition: once it is set up in the level the factor goes back on the parts that were reset
    if (ok && !isClientSide() && hasTags()) {
        for (auto const& t : getTags())
            if (t.starts_with(kScaleTag)) {
                queueScale(getOrCreateUniqueID().rawID, 2);
                break;
            }
    }
    return ok;
}

// ---- API ------------------------------------------------------------------------------------------------

bool applyScale(Actor& actor, float scale) {
    // generic.scale: the entity's size factor (Bedrock: the scale / width / height data of the
    // bounding box component, synced to clients, and the collision box); it multiplies the mob's
    // own scale (a baby's included)
    if (!std::isfinite(scale) || scale <= 0) return false;
    auto size = sizeOf(actor);
    if (!size) return false;
    Size target = *size;
    target.v[0] = (target.v[0] > 0 ? target.v[0] : 1.0f) * scale;
    for (int i = 1; i < 5; i++) target.v[i] *= scale;
    setSize(actor, target);
    return true;
}

std::optional<float> dataScaleOf(Actor& mob) {
    auto* data = boxData(mob);
    if (!data) return std::nullopt;
    return (*data)[0] > 0 ? (*data)[0] : 1.0f;
}

std::optional<float> scaleOf(Actor const& mob) {
    if (!mob.hasTags()) return std::nullopt;
    for (auto const& t : mob.getTags())
        if (auto s = parseScaleTag(t)) return s;
    return std::nullopt;
}

void scaleMob(Actor& mob, float scale) {
    if (!std::isfinite(scale) || scale <= 0 || scale == 1.0f) return;
    for (auto const& t : mob.getTags())
        if (t.starts_with(kScaleTag) || t.starts_with(kSizeTag)) (void)mob.removeTag(t);
    (void)mob.addTag(scaleTag(scale));
    gScaled.erase(mob.getOrCreateUniqueID().rawID);
    // the mob is set up from its entity definition once it is in the level: scale it then
    queueScale(mob.getOrCreateUniqueID().rawID, 2);
}

std::optional<BlockPos> firingSpawner(BlockSource& source, Vec3 const& at, std::string const& entityId) {
    // BaseMobSpawner spawns within SpawnRange of the spawner's center horizontally and one block
    // up or down; a spawner's range is at most 16 here
    constexpr int           kMaxReach = 17;
    BlockPos                p{pure::mthFloor(at.x), pure::mthFloor(at.y), pure::mthFloor(at.z)};
    std::optional<BlockPos> best;
    bool                    bestFiring = false;
    double                  bestDist   = 0;
    for (int cx = core::chunkOf(p.x - kMaxReach); cx <= core::chunkOf(p.x + kMaxReach); cx++)
        for (int cz = core::chunkOf(p.z - kMaxReach); cz <= core::chunkOf(p.z + kMaxReach); cz++) {
            auto* chunk = source.getChunk(cx, cz);
            if (!chunk) continue;
            for (auto const& [cbp, actor] : chunk->mBlockEntities->mMap.get()) {
                if (!actor || actor->getType() != BlockActorType::MobSpawner) continue;
                auto* spawner = static_cast<MobSpawnerBlockActor&>(*actor).mSpawner.get();
                if (!spawner || canonical(*spawner->mActorDefId) != entityId) continue;
                BlockPos const& q     = actor->getPosition();
                int             range = std::max(1, static_cast<int>(spawner->mSpawnRange));
                double          dx = at.x - (q.x + 0.5), dy = at.y - q.y, dz = at.z - (q.z + 0.5);
                if (std::abs(dx) > range + 1 || std::abs(dz) > range + 1 || std::abs(dy) > 3) continue;
                bool   firing = spawner->mSpawnDelay <= 0;
                double dist   = dx * dx + dy * dy + dz * dz;
                if (!best || (firing && !bestFiring) || (firing == bestFiring && dist < bestDist)) {
                    best       = q;
                    bestFiring = firing;
                    bestDist   = dist;
                }
            }
        }
    // upstream's sinister spawner (F8's block entity, in block data, spawns as a spawner): the ones whose
    // spawn box (SinisterSpawnerLogic: within SpawnRange of the block on every axis) holds the mob
    // and whose block F7 left a spawner record with (a template's spawner data), on the same terms
    // (the one spawning now first: its delay ran out, then the nearest). The kit's index of its
    // spawner records names them (no block data walk per spawn); F8's spawner is decoded only for
    // the records within reach. (The record's own mob is not a filter: a sinister spawner that
    // spawns another mob of its potentials now drops its record, as Java replaces its SpawnData.)
    auto const mob = namespaced(entityId);
    for (int cx = core::chunkOf(p.x - kMaxReach); cx <= core::chunkOf(p.x + kMaxReach); cx++)
        for (int cz = core::chunkOf(p.z - kMaxReach); cz <= core::chunkOf(p.z + kMaxReach); cz++) {
            if (!source.getChunk(cx, cz)) continue;
            for (auto const& q : blockentities::spawnerRecordsIn(source, cx, cz)) {
                double dx = at.x - (q.x + 0.5), dy = at.y - q.y, dz = at.z - (q.z + 0.5);
                if (std::abs(dx) > kMaxReach || std::abs(dz) > kMaxReach || std::abs(dy) > kMaxReach) continue;
                auto sinister = boss::ext::sinisterSpawner(source, q);
                if (!sinister) continue;
                int range = std::max(1, sinister->spawnRange);
                if (std::abs(dx) > range + 1 || std::abs(dz) > range + 1 || std::abs(dy) > range + 1) continue;
                bool spawns = !sinister->nextEntity.empty() && namespaced(sinister->nextEntity) == mob;
                for (auto const& [id, weight] : sinister->potentials) spawns |= namespaced(id) == mob;
                if (!spawns) continue;
                bool   firing = sinister->spawnDelay <= 0;
                double dist   = dx * dx + dy * dy + dz * dz;
                if (!best || (firing && !bestFiring) || (firing == bestFiring && dist < bestDist)) {
                    best       = q;
                    bestFiring = firing;
                    bestDist   = dist;
                }
            }
        }
    return best;
}

namespace {

/// The last spawner lookup (a mob's SpawningMobEvent and its SpawnedMobEvent ask for the same one
/// in the same tick).
struct LastLookup {
    bool                    valid = false;
    int                     tick  = 0;
    int                     dim   = 0;
    Vec3                    at{};
    std::string             mob;
    std::optional<BlockPos> result;
};
LastLookup gLastLookup;

std::optional<BlockPos> firingSpawnerOnce(BlockSource& source, Vec3 const& at, std::string const& mob) {
    int  dim  = static_cast<int>(source.getDimensionId());
    auto& l   = gLastLookup;
    if (l.valid && l.tick == gTicks && l.dim == dim && l.at.x == at.x && l.at.y == at.y && l.at.z == at.z && l.mob == mob) return l.result;
    l = LastLookup{true, gTicks, dim, at, mob, firingSpawner(source, at, mob)};
    return l.result;
}

} // namespace

int vetoes() { return gVetoes; }

void holdSizeChecks(bool held) { gHeld = held; }

void tick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<PendingScale> due;
    {
        std::lock_guard lock(gPendingMutex);
        std::erase_if(gPending, [&](PendingScale& p) {
            if (--p.ticks > 0) return false;
            due.push_back(p);
            return true;
        });
    }
    std::vector<PendingScale> retry;
    for (auto& p : due) {
        ActorUniqueID uid;
        uid.rawID   = p.id;
        auto* actor = level->fetchEntity(uid, false);
        if (!actor) {
            // not in the level (yet): look again next tick, for a while
            if (++p.tries < kPendingTries) retry.push_back(PendingScale{p.id, 1, p.tries});
            continue;
        }
        if (auto s = scaleOf(*actor)) rescale(*actor, *s, sizeTagOf(*actor));
    }
    if (!retry.empty()) {
        std::lock_guard lock(gPendingMutex);
        for (auto const& r : retry) {
            bool queued = false;
            for (auto const& q : gPending) queued = queued || q.id == r.id;
            if (!queued) gPending.push_back(r);
        }
    }
    if (++gTicks % kCheckInterval != 0 || gScaled.empty() || gHeld) return;
    // the game sets a mob's size from its definition when it grows up or its components reload:
    // the factor goes on top of the new size
    for (auto it = gScaled.begin(); it != gScaled.end();) {
        ActorUniqueID uid;
        uid.rawID   = it->first;
        auto* actor = level->fetchEntity(uid, false);
        if (!actor) {
            it = gScaled.erase(it); // unloaded or gone (the load hook picks it up again)
            continue;
        }
        auto size = sizeOf(*actor);
        if (size && !sameSize(it->second.applied, *size)) {
            float scale   = it->second.scale;
            Size  applied = it->second.applied;
            ++it;
            rescale(*actor, scale, applied);
            continue;
        }
        ++it;
    }
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    if (!gSpawningListener) {
        // SpawnData custom spawn rules: BaseSpawner spawns only where the block and sky light at
        // the spawn position lie in the rules' ranges
        gSpawningListener = bus.emplaceListener<ll::event::SpawningMobEvent>([](ll::event::SpawningMobEvent& ev) {
            if (!ev.fromSpawner()) return;
            auto& source  = ev.blockSource();
            auto  mob     = canonical(ev.identifier());
            auto  spawner = firingSpawnerOnce(source, ev.pos(), mob);
            if (!spawner) return;
            auto record = recordFor(source, *spawner, mob);
            if (!record || !record->contains("spawn_rules")) return;
            auto const& rules = (*record)["spawn_rules"];
            Vec3 const& at    = ev.pos();
            auto        light = source.getBrightnessPair(BlockPos{pure::mthFloor(at.x), pure::mthFloor(at.y), pure::mthFloor(at.z)});
            if (inLightRange(rules.value("block_light", Json()), light.block->mValue) && inLightRange(rules.value("sky_light", Json()), light.sky->mValue))
                return;
            gVetoes++;
            ev.cancel();
        });
    }
    if (!gSpawnedListener) {
        // SpawnerProcessor's entity_width_max: mobs of a spawner that recorded a scale get it
        gSpawnedListener = bus.emplaceListener<ll::event::SpawnedMobEvent>([](ll::event::SpawnedMobEvent& ev) {
            if (!ev.fromSpawner() || !ev.mob()) return;
            auto& source  = ev.blockSource();
            auto  mob     = canonical(ev.identifier());
            auto  spawner = firingSpawnerOnce(source, ev.pos(), mob);
            if (!spawner) return;
            auto record = recordFor(source, *spawner, mob);
            if (!record || !record->contains("scale")) return;
            scaleMob(*ev.mob(), (*record)["scale"].get<float>());
        });
    }
    if (!gHooked) {
        StructureScaledActorLoadHook::hook();
        gHooked = true;
    }
}

void clear() {
    gHeld             = false;
    gLastLookup.valid = false;
    gScaled.clear();
    std::lock_guard lock(gPendingMutex);
    gPending.clear();
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    if (gSpawningListener) bus.removeListener(gSpawningListener);
    if (gSpawnedListener) bus.removeListener(gSpawnedListener);
    gSpawningListener.reset();
    gSpawnedListener.reset();
    if (gHooked) {
        StructureScaledActorLoadHook::unhook();
        gHooked = false;
    }
    gHeld = false;
    gScaled.clear();
    std::lock_guard lock(gPendingMutex);
    gPending.clear();
}

} // namespace gf::structure::spawners

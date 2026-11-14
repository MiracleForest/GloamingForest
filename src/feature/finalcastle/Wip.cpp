// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/finalcastle/FinalCastleBossGazeboComponent.java
//   src/main/java/gloamingforest/world/components/structures/TFStructureComponent.java (setInvisibleTextEntity)
//   src/main/java/gloamingforest/events/EntityEvents.java (removeCastleTextIfAttacked)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The gazebo's "Final Castle WIP." texts (docs/spec/finalcastle.md, 1.2 "Gazebo WIP texts"). The
// original gazebo template holds the marker "final_castle_wip" at its centre block (gazebo-local
// (10, 0, 10), where upstream anchors its displays); its handler does what willBeAddingFinalBossSoon does:
// four text carriers (upstream: minecraft:text_display, VERTICAL billboard) at the block centre, 2.3 / 1.3
// / 1.0 / 0.7 above the block, and one punch carrier (upstream: minecraft:interaction tagged
// final_castle_wip) over their box grown by 1 (2.0 wide, 3.6 high, from by - 0.3). Attacking the
// punch carrier removes the text carriers inside its box, then itself (removeCastleTextIfAttacked);
// nothing else removes them.
//
// Bedrock has neither text displays nor interactions: the carriers are U30 helper actors
// (gloamingforest:final_castle_text / _punch, assets/pack/bp/entities/finalcastle: invisible, no
// gravity, no collision, not pushable, invulnerable, persistent). A text carrier shows its line as an
// always-shown name tag (the client mod has no world-text renderer yet: listed deviation, the name
// tag's font size, background and height above the carrier differ from Java's text display); the
// punch carrier's 2.0 x 3.6 collision box is the hit box.
//
// Threads: template placement runs the handler on a world generation thread (the generator's
// window, no BlockSource) or on the server thread (/gloamingforest structure_util place, selftests). The handler
// only queues the block; the server thread spawns the carriers every few ticks once the chunk is
// loaded. The queue is persisted (gf:finalcastle:wip_pending) so a save between generation and the
// chunk's load keeps it.
#include "feature/finalcastle/Mobs.h"

#include <cmath>
#include <mutex>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "core/finalcastle/Mobs.h"
#include "feature/common/Interactions.h"
#include "feature/ext/finalcastle/W0Internals.h"
#include "feature/structure/Structure.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::finalcastle::mobs {

namespace fe = gf::feature::entity;
namespace cf = gf::core::finalcastle;
namespace ia = gf::interactions;

namespace {

constexpr char kPendingKey[] = "gf:finalcastle:wip_pending";
/// Queued blocks are checked this often (ticks).
constexpr int kFlushInterval = 5;

struct Job {
    int      dim = 0;
    BlockPos pos{0, 0, 0};
};

struct State {
    std::mutex                  mutex;
    std::vector<Job>            jobs;
    bool                        dirty = false;
    int                         ticks = 0;
    ll::event::ListenerPtr      listener;
};
State& st() {
    static State s;
    return s;
}

void load() {
    auto&           s = st();
    auto            doc = game::db::get(kPendingKey);
    std::lock_guard lock(s.mutex);
    if (!doc || !doc->is_array()) return;
    // stored blocks join what generation queued since (a world loads its spawn chunks first)
    for (auto const& j : *doc) {
        if (!j.is_array() || j.size() != 4) continue;
        Job job{j[0].get<int>(), BlockPos{j[1].get<int>(), j[2].get<int>(), j[3].get<int>()}};
        bool known = false;
        for (auto const& k : s.jobs) known = known || (k.dim == job.dim && k.pos.x == job.pos.x && k.pos.y == job.pos.y && k.pos.z == job.pos.z);
        if (!known) s.jobs.push_back(job);
    }
}

void save() {
    auto&           s = st();
    nlohmann::json  doc = nlohmann::json::array();
    {
        std::lock_guard lock(s.mutex);
        if (!s.dirty) return;
        s.dirty = false;
        for (auto const& j : s.jobs) doc.push_back({j.dim, j.pos.x, j.pos.y, j.pos.z});
    }
    if (doc.empty()) game::db::erase(kPendingKey);
    else game::db::put(kPendingKey, doc);
}

/// willBeAddingFinalBossSoon at block `pos`: the four lines (setInvisibleTextEntity with forcePlace),
/// then the punch box over the texts that were added.
Actor* addHelper(BlockSource& region, char const* type, cf::DVec const& at) {
    auto& level = region.getLevel();
    auto  owner = level.getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(std::string(type)), nullptr, Vec3{float(at.x), float(at.y), float(at.z)}, Vec2{0.0f, 0.0f});
    if (!owner) {
        static bool warned = false;
        if (!warned) gf::log().warn("finalcastle: cannot spawn {} (behavior pack missing?)", type);
        warned = true;
        return nullptr;
    }
    auto* a = level.addEntity(region, std::move(owner));
    if (a) a->setPersistent();
    return a;
}

void spawnCarriers(BlockSource& region, BlockPos const& pos) {
    auto                  positions = cf::wipTextPositions(cf::IPos{pos.x, pos.y, pos.z});
    std::vector<cf::DVec> added;
    for (std::size_t i = 0; i < positions.size(); i++) {
        // moveTo(x + 0.5, y + yOffset, z + 0.5, 0, 0); Component.literal: the same English in every locale
        if (auto* a = addHelper(region, id::WipText, positions[i])) {
            a->setNameTag(cf::kWipLines[i].text);
            added.push_back(positions[i]); // addFreshEntity succeeded
        }
    }
    auto punchBox = cf::punchFor(added);
    if (!punchBox) return;
    auto* punch = addHelper(region, id::WipPunch, punchBox->pos);
    gf::log().info("finalcastle: the gazebo's WIP texts at {} {} {} ({} lines{})", pos.x, pos.y, pos.z, added.size(), punch ? ", punch box" : "");
}

void onTick() {
    if (++st().ticks % kFlushInterval != 0) return;
    flushWip();
}

} // namespace

void queueWip(worldgen::Region& region, BlockPos const& pos) {
    // a scratch region (selftest hashes, dimension -1) has no world to spawn the carriers in
    if (region.dimension() < 0) return;
    auto&           s = st();
    std::lock_guard lock(s.mutex);
    s.jobs.push_back(Job{region.dimension(), pos});
    s.dirty = true;
}

void flushWip() {
    auto&            s = st();
    std::vector<Job> ready;
    {
        std::lock_guard lock(s.mutex);
        if (s.jobs.empty()) return;
        for (auto it = s.jobs.begin(); it != s.jobs.end();) {
            auto* region = game::regionOf(it->dim);
            if (region && region->hasChunksAt(it->pos, 1, false)) {
                ready.push_back(*it);
                it      = s.jobs.erase(it);
                s.dirty = true;
            } else ++it;
        }
    }
    for (auto const& j : ready)
        if (auto* region = game::regionOf(j.dim)) spawnCarriers(*region, j.pos);
    // persist the shorter queue now: the carriers are persistent, an unclean stop before the next
    // world save would spawn them a second time
    if (!ready.empty()) save();
}

void punch(Actor& punchCarrier) {
    auto uid = fe::uidOf(punchCarrier);
    int  dim = static_cast<int>(punchCarrier.getDimensionId());
    AABB box = punchCarrier.getAABB();
    ia::nextTick([uid, dim, box] {
        auto* region = game::regionOf(dim);
        if (!region) return;
        // level.getEntities(interaction, bounds, e -> e instanceof Display).forEach(Entity::discard)
        int removed = 0;
        for (auto* a : fe::actorsIn(*region, dim, box)) {
            if (a->getTypeName() != id::WipText) continue;
            a->remove();
            removed++;
        }
        if (auto* p = fe::actorById(uid)) p->remove();
        gf::log().info("finalcastle: the gazebo's WIP texts were punched away ({} lines)", removed);
    });
}

int pendingWipForTest() {
    std::lock_guard lock(st().mutex);
    return static_cast<int>(st().jobs.size());
}

void installWip() {
    structure::registerMarkerHandler(id::WipMarker, [](worldgen::Region& region, BlockPos const& pos, std::string const&, nlohmann::json const&, worldgen::RandomSource&) {
        queueWip(region, pos);
    });
    // EntityEvents#removeCastleTextIfAttacked: the attack does nothing else (Interaction#skipAttackInteraction)
    ia::onAttack({.block = id::WipPunch}, [](ia::EntityContext const& c) {
        punch(c.entity);
        return ia::Result::Deny;
    }, ia::priority::High);
    st().listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { onTick(); });
    game::db::onWorldReady([] { load(); });
    game::db::onWorldSave([] { save(); });
    game::db::onWorldClosed([] {
        std::lock_guard lock(st().mutex);
        st().jobs.clear();
        st().dirty = false;
    });
    if (game::db::available()) load();
}

} // namespace gf::feature::finalcastle::mobs

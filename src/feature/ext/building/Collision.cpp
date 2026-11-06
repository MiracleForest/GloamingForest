// Extension of contract "families+blockevents" by U5 (see Collision.h).

#include "feature/ext/building/Collision.h"

#include <atomic>
#include <cmath>
#include <thread>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/actor/GetCollisionShapeActorProxy.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/GetCollisionShapeInterface.h"
#include "mc/world/phys/AABB.h"

#include "mod/Log.h"

#ifdef GF_CLIENT
#include "client/Client.h"
#include "mc/client/player/LocalPlayer.h"
#endif

namespace gf::building::ext {

namespace {

/// One resolved (block type, provider) pair. Snapshots are immutable once published and never freed
/// (a few per world join), so a game thread that loaded one can use it without a lock while the
/// next one is built; providers live in map nodes that are never reassigned or erased.
struct Snapshot {
    std::vector<std::pair<BlockType const*, CollisionProvider const*>> entries;
};

struct Registry {
    std::mutex                                   mutex;
    std::map<std::string, CollisionProvider>     byId;
    std::vector<std::unique_ptr<Snapshot const>> retired;
    std::atomic<Snapshot const*>                 current{nullptr};
};

Registry& reg() {
    static Registry r;
    return r;
}

} // namespace

CollisionProvider const* providerOf(BlockType const* type) {
    auto const* s = reg().current.load(std::memory_order_acquire);
    if (!s) return nullptr;
    for (auto const& [t, p] : s->entries)
        if (t == type) return p;
    return nullptr;
}

void registerCollision(std::string const& blockId, CollisionProvider provider) {
    {
        std::lock_guard lock(reg().mutex);
        // one provider per id, set once: a game thread may be calling it through a snapshot
        if (!reg().byId.try_emplace(blockId, std::move(provider)).second) return;
    }
    refreshCollisionTypes();
}

bool refreshCollisionTypes() {
    auto&           r = reg();
    std::lock_guard lock(r.mutex);
    auto            next = std::make_unique<Snapshot>();
    for (auto const& [id, fn] : r.byId) {
        auto type = BlockType::tryGetFromRegistry(HashedString(id));
        if (type) next->entries.emplace_back(&*type, &fn);
    }
    auto const* old = r.current.load(std::memory_order_acquire);
    if (old && old->entries == next->entries) return false;
    r.current.store(next.get(), std::memory_order_release);
    r.retired.push_back(std::move(next));
    return true;
}

} // namespace gf::building::ext

namespace {

namespace bx = gf::building::ext;

std::atomic<std::thread::id>& serverThread() {
    static std::atomic<std::thread::id> id{};
    return id;
}

ll::event::ListenerPtr& tickListener() {
    static ll::event::ListenerPtr l;
    return l;
}

bool sameBox(AABB const& a, AABB const& b) {
    constexpr float e = 1e-4f;
    return std::abs(a.min.x - b.min.x) < e && std::abs(a.min.y - b.min.y) < e && std::abs(a.min.z - b.min.z) < e && std::abs(a.max.x - b.max.x) < e
        && std::abs(a.max.y - b.max.y) < e && std::abs(a.max.z - b.max.z) < e;
}

/// Whether a sneaking player has exactly this box (the client mod's local player first, then the
/// players of the server level).
bool sneakingPlayerWithBox(AABB const& box) {
    // the server level's players from the server's own thread; any other thread is the client mod's
    // game thread, whose only moving player is the local one
    if (std::this_thread::get_id() != serverThread().load(std::memory_order_relaxed)) {
#ifdef GF_CLIENT
        if (auto* local = gf::client::currentPlayer(); local && sameBox(local->getAABB(), box)) return local->isSneaking();
#endif
        return false;
    }
    bool found = false;
    if (auto level = ll::service::getLevel()) {
        level->forEachPlayer([&](Player& p) {
            if (!sameBox(p.getAABB(), box)) return true;
            found = p.isSneaking();
            return false;
        });
    }
    return found;
}

/// Whether the query's entity is the game's actor proxy (its vtable; no RTTI on game types).
bool isActorProxy(GetCollisionShapeInterface const& e) {
    return *reinterpret_cast<void* const* const*>(&e) == GetCollisionShapeActorProxy::$vftable();
}

LL_TYPE_INSTANCE_HOOK(
    BuildingEntityCollisionHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::$addCollisionShapes,
    bool,
    ::Block const&                                     block,
    ::IConstBlockSource const&                         region,
    ::BlockPos const&                                  pos,
    ::AABB const*                                      intersectTestBox,
    ::std::vector<::AABB>&                             inoutBoxes,
    ::optional_ref<::GetCollisionShapeInterface const> entity
) {
    auto const* provider = bx::providerOf(this);
    if (!provider || !*provider) return origin(block, region, pos, intersectTestBox, inoutBoxes, entity);
    // the rest of the chain (F11's FamiliesCollisionHook, the game) still runs for this block; its
    // static boxes are replaced by the provider's
    {
        std::vector<AABB> chained;
        origin(block, region, pos, intersectTestBox, chained, entity);
    }
    std::optional<bx::CollisionEntity> e;
    if (entity) {
        // Java isDescending: a player holding sneak. Bedrock's canDescendThroughBlock follows the
        // scaffolding descend input; an actor's own sneaking flag counts too (simulated players and
        // a sneaking player standing still)
        bool descending = entity->canDescendThroughBlock();
        if (!descending) {
            if (isActorProxy(*entity)) {
                auto const& actor = static_cast<GetCollisionShapeActorProxy const&>(*entity).mActor;
                descending        = actor.isPlayer() && actor.isSneaking();
            } else {
                // the movement systems ask through their own interface (no actor): the player is the
                // one whose box this is
                descending = sneakingPlayerWithBox(entity->getAABB());
            }
        }
        e = bx::CollisionEntity{entity->getAABB().min.y, descending};
    }
    std::vector<bx::Box> boxes;
    (*provider)(block, pos, e, boxes);
    bool added = false;
    for (auto const& b : boxes) {
        AABB box{
            Vec3{pos.x + b[0] / 16.f, pos.y + b[1] / 16.f, pos.z + b[2] / 16.f},
            Vec3{pos.x + b[3] / 16.f, pos.y + b[4] / 16.f, pos.z + b[5] / 16.f}
        };
        if (intersectTestBox && !intersectTestBox->intersects(box)) continue;
        inoutBoxes.push_back(box);
        added = true;
    }
    return added;
}

} // namespace

namespace gf::building::ext {

void installCollision() {
    BuildingEntityCollisionHook::hook();
    if (!tickListener())
        tickListener() = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            serverThread().store(std::this_thread::get_id(), std::memory_order_relaxed);
        });
}
void uninstallCollision() {
    BuildingEntityCollisionHook::unhook();
    if (tickListener()) {
        ll::event::EventBus::getInstance().removeListener(tickListener());
        tickListener() = nullptr;
    }
}

} // namespace gf::building::ext

// Extension of contract "families+blockevents" by U5 (see Collision.h).

#include "feature/ext/building/Collision.h"

#include <atomic>
#include <map>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/GetCollisionShapeInterface.h"
#include "mc/world/phys/AABB.h"

#include "mod/Log.h"

namespace gf::building::ext {

namespace {

constexpr std::size_t kSlots = 8;

struct Registry {
    std::mutex                                     mutex;
    std::map<std::string, CollisionProvider>       byId;
    std::array<std::atomic<BlockType const*>, kSlots> types{};
    std::array<CollisionProvider const*, kSlots>   providers{};
    std::atomic<std::size_t>                       count{0};
};

Registry& reg() {
    static Registry r;
    return r;
}

CollisionProvider const* providerOf(BlockType const* type) {
    auto& r = reg();
    auto  n = r.count.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < n; ++i)
        if (r.types[i].load(std::memory_order_relaxed) == type) return r.providers[i];
    return nullptr;
}

} // namespace

void registerCollision(std::string const& blockId, CollisionProvider provider) {
    {
        std::lock_guard lock(reg().mutex);
        reg().byId[blockId] = std::move(provider);
    }
    refreshCollisionTypes();
}

void refreshCollisionTypes() {
    auto&           r = reg();
    std::lock_guard lock(r.mutex);
    r.count.store(0, std::memory_order_release);
    std::size_t n = 0;
    for (auto const& [id, fn] : r.byId) {
        if (n == kSlots) {
            log().warn("building: more than {} entity-dependent collision blocks, {} skipped", kSlots, id);
            continue;
        }
        auto type = BlockType::tryGetFromRegistry(HashedString(id));
        if (!type) continue;
        r.types[n].store(&*type, std::memory_order_relaxed);
        r.providers[n] = &fn;
        ++n;
    }
    r.count.store(n, std::memory_order_release);
}

} // namespace gf::building::ext

namespace {

namespace bx = gf::building::ext;

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
    std::optional<bx::CollisionEntity> e;
    if (entity) e = bx::CollisionEntity{entity->getAABB().min.y, entity->canDescendThroughBlock()};
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

void installCollision() { BuildingEntityCollisionHook::hook(); }
void uninstallCollision() { BuildingEntityCollisionHook::unhook(); }

} // namespace gf::building::ext

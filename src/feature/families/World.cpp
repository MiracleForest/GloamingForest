// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/building/World.cpp), LGPL-3.0-or-later.
//
// Block families (F11): world helpers shared by the family behaviours (blocks and states,
// directions, support, redstone input, sounds, items, deferred and scheduled work).

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <atomic>
#include <mutex>
#include <numbers>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/block/ResourceDropsCause.h"
#include "mc/world/level/block/ResourceDropsContext.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Redstone.h"
#include "feature/families/Families.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::families {

namespace state {
std::string side(int dir) { return std::string("twilightforest:") + dirName(dir); }
std::string ctSide(int dir) { return std::string(kKit) + dirName(dir); }
std::string corner(int a, int b) {
    // the two names in alphabetical order (tools/families/families.lua corner_key)
    std::string_view x = dirName(a), y = dirName(b);
    if (x > y) std::swap(x, y);
    return std::string(kKit).append("corner_").append(x).append("_").append(y);
}
} // namespace state

char const* dirName(int dir) {
    static constexpr char const* names[]{"down", "up", "north", "south", "west", "east"};
    return dir >= 0 && dir < 6 ? names[dir] : "north";
}

std::optional<int> dirOf(std::string_view name) {
    for (int d = 0; d < 6; ++d)
        if (name == dirName(d)) return d;
    return std::nullopt;
}

char const* axisName(int axis) { return axis == 0 ? "x" : (axis == 2 ? "z" : "y"); }

int clockwise(int dir) {
    switch (dir) {
    case North: return East;
    case East: return South;
    case South: return West;
    case West: return North;
    default: return dir;
    }
}

int counterClockwise(int dir) {
    switch (dir) {
    case North: return West;
    case West: return South;
    case South: return East;
    case East: return North;
    default: return dir;
    }
}

int to2D(int dir) {
    switch (dir) {
    case South: return 0;
    case West: return 1;
    case North: return 2;
    case East: return 3;
    default: return -1;
    }
}

int from2D(int value) {
    static constexpr int map[4]{South, West, North, East};
    return map[value & 3];
}

GlobalPos offset(GlobalPos p, int dir, int n) {
    p.x += kDx[dir] * n;
    p.y += kDy[dir] * n;
    p.z += kDz[dir] * n;
    return p;
}

BlockPos offset(BlockPos const& p, int dir, int n) { return {p.x + kDx[dir] * n, p.y + kDy[dir] * n, p.z + kDz[dir] * n}; }

BlockSource* regionOf(int dim) { return game::regionOf(dim); }

Block const* blockAt(BlockSource& region, BlockPos const& p) {
    if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight()) return nullptr;
    if (!region.hasChunksAt(p, 0, false)) return nullptr;
    return &region.getBlock(p);
}

Block const* blockAt(GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    return region ? blockAt(*region, BlockPos{p.x, p.y, p.z}) : nullptr;
}

std::string idAt(GlobalPos const& p) {
    auto const* b = blockAt(p);
    return b ? b->getTypeName() : std::string();
}

bool set(GlobalPos const& p, Block const& block) {
    auto* region = regionOf(p.dim);
    return region && game::setBlock(*region, BlockPos{p.x, p.y, p.z}, block);
}

bool clear(GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    return region && game::clearBlock(*region, BlockPos{p.x, p.y, p.z});
}

bool replaceableAt(GlobalPos const& p) {
    auto* region = regionOf(p.dim);
    return region && game::replaceable(*region, BlockPos{p.x, p.y, p.z});
}

namespace {
CompoundTagVariant const* stateTag(Block const& block, std::string_view name) {
    auto const& nbt = *block.mSerializationId;
    if (!nbt.contains("states")) return nullptr;
    auto const& states = nbt["states"];
    if (!states.is_object()) return nullptr;
    auto const& c = states.get<CompoundTag>();
    std::string key(name);
    if (!c.contains(key)) return nullptr;
    return &c[key];
}
} // namespace

std::optional<int> intState(Block const& block, std::string_view name) {
    auto const* t = stateTag(block, name);
    if (!t) return std::nullopt;
    if (t->hold(Tag::Type::Byte)) return static_cast<int>(t->get<ByteTag>().data);
    if (t->hold(Tag::Type::Int)) return t->get<IntTag>().data;
    return std::nullopt;
}

std::optional<bool> boolState(Block const& block, std::string_view name) {
    auto v = intState(block, name);
    if (!v) return std::nullopt;
    return *v != 0;
}

bool flagState(Block const& block, std::string_view name) { return boolState(block, name).value_or(false); }

std::string strState(Block const& block, std::string_view name) {
    auto const* t = stateTag(block, name);
    if (!t) return {};
    if (t->is_string()) return std::string(t->get<StringTag>());
    if (t->hold(Tag::Type::Byte)) return t->get<ByteTag>().data ? "true" : "false";
    if (t->hold(Tag::Type::Int)) return std::to_string(t->get<IntTag>().data);
    return {};
}

int facingOf(Block const& block, int fallback) { return dirOf(strState(block, state::Facing)).value_or(fallback); }

int axisStateOf(Block const& block) {
    auto a = strState(block, state::Axis);
    return a == "x" ? 0 : (a == "z" ? 2 : 1);
}

Block const* withStates(Block const& block, Json const& states) {
    if (!states.is_object() || states.empty()) return &block;
    auto nbt = block.mSerializationId->clone();
    if (!nbt->contains("states") || !(*nbt)["states"].is_object()) return nullptr;
    auto& current = (*nbt)["states"].get<CompoundTag>();
    bool  changed = false;
    for (auto const& [name, value] : states.items()) {
        if (!current.contains(name)) continue; // the block has no such state
        auto& slot = current[name];
        if (slot.hold(Tag::Type::Byte)) {
            int v = value.is_boolean() ? (value.get<bool>() ? 1 : 0) : (value.is_number_integer() ? (value.get<int>() != 0 ? 1 : 0) : -1);
            if (v < 0) return nullptr;
            if (static_cast<int>(slot.get<ByteTag>().data) != v) changed = true;
            slot = ByteTag(static_cast<uchar>(v));
        } else if (slot.hold(Tag::Type::Int)) {
            int v = value.is_boolean() ? (value.get<bool>() ? 1 : 0) : (value.is_number_integer() ? value.get<int>() : -1);
            if (v < 0) return nullptr;
            if (slot.get<IntTag>().data != v) changed = true;
            slot = IntTag(v);
        } else if (slot.is_string()) {
            if (!value.is_string()) return nullptr;
            if (std::string(slot.get<StringTag>()) != value.get<std::string>()) changed = true;
            slot = StringTag(value.get<std::string>());
        } else {
            return nullptr;
        }
    }
    if (!changed) return &block;
    auto found = Block::tryGetFromRegistry(*nbt);
    return found ? found.as_ptr() : nullptr;
}

bool setStates(BlockSource& region, BlockPos const& p, Json const& states) {
    auto const* b = blockAt(region, p);
    if (!b) return false;
    auto const* n = withStates(*b, states);
    if (!n || n == b) return false;
    return game::setBlock(region, p, *n);
}

bool setStates(GlobalPos const& p, Json const& states) {
    auto* region = regionOf(p.dim);
    return region && setStates(*region, BlockPos{p.x, p.y, p.z}, states);
}

int lookDir(Actor const& actor) { return from2D(game::playerFacing(actor)); }

float yawOf(Actor const& actor) { return actor.getRotation().y; }

int rotationSegment(float degrees) {
    // Java RotationSegment.convertToSegment: Mth.floor(degrees / 22.5 + 0.5) & 15
    return static_cast<int>(std::floor(degrees / 22.5f + 0.5f)) & 15;
}

std::array<int, 6> nearestLooking(Actor const& actor) {
    // Java Direction.orderedByNearest(entity): the three axes by how much the look follows them
    auto  rot   = actor.getRotation();
    float pitch = rot.x * std::numbers::pi_v<float> / 180.0f, yaw = -rot.y * std::numbers::pi_v<float> / 180.0f;
    float f2 = std::sin(pitch), f3 = std::cos(pitch), f4 = std::sin(yaw), f5 = std::cos(yaw);
    bool  flag  = f4 > 0.0f, flag1 = f2 < 0.0f, flag2 = f5 > 0.0f;
    float f6 = flag ? f4 : -f4, f7 = flag1 ? -f2 : f2, f8 = flag2 ? f5 : -f5;
    float f9 = f6 * f3, f10 = f8 * f3;
    int   x = flag ? East : West, y = flag1 ? Up : Down, z = flag2 ? South : North;
    auto  make = [](int a, int b, int c) { return std::array<int, 6>{a, b, c, opposite(c), opposite(b), opposite(a)}; };
    if (f6 > f8) {
        if (f7 > f9) return make(y, x, z);
        return f10 > f7 ? make(x, z, y) : make(x, y, z);
    }
    if (f7 > f10) return make(y, z, x);
    return f9 > f7 ? make(z, x, y) : make(z, y, x);
}

bool sturdy(BlockSource& region, BlockPos const& pos, int face) {
    auto const* b = blockAt(region, pos);
    if (!b || b->isAir()) return false;
    return b->canProvideSupport(static_cast<uchar>(face), BlockSupportType::Edge);
}

bool sturdyCenter(BlockSource& region, BlockPos const& pos, int face) {
    auto const* b = blockAt(region, pos);
    if (!b || b->isAir()) return false;
    return b->canProvideSupport(static_cast<uchar>(face), BlockSupportType::Center);
}

bool solidAt(BlockSource& region, BlockPos const& pos) {
    // Java BlockBehaviour.BlockStateBase.calculateSolid: a collision shape whose bounds average
    // 0.729 or are a full block high
    auto const* b = blockAt(region, pos);
    if (!b || b->isAir()) return false;
    std::vector<AABB> boxes;
    b->getBlockType().addCollisionShapes(*b, region, pos, nullptr, boxes, {});
    if (boxes.empty()) return false;
    AABB bounds = boxes.front();
    for (auto const& box : boxes) {
        bounds.min.x = std::min(bounds.min.x, box.min.x), bounds.min.y = std::min(bounds.min.y, box.min.y), bounds.min.z = std::min(bounds.min.z, box.min.z);
        bounds.max.x = std::max(bounds.max.x, box.max.x), bounds.max.y = std::max(bounds.max.y, box.max.y), bounds.max.z = std::max(bounds.max.z, box.max.z);
    }
    float sx = bounds.max.x - bounds.min.x, sy = bounds.max.y - bounds.min.y, sz = bounds.max.z - bounds.min.z;
    return (sx + sy + sz) / 3.0f >= 0.7291666f || sy >= 1.0f;
}

bool solidBlock(BlockSource& region, BlockPos const& pos) {
    auto const* b = blockAt(region, pos);
    return b && !b->isAir() && b->mCachedComponentData->mIsSolid;
}

bool vanillaLeaves(Block const& block) {
    auto const& type = block.getBlockType();
    if ((static_cast<std::uint64_t>(type.mProperties) & static_cast<std::uint64_t>(BlockProperty::Leaves)) != 0) return true;
    std::string const& id = block.getTypeName();
    return id.starts_with("minecraft:") && id.find("leaves") != std::string::npos;
}

bool exceptionForConnection(Block const& block) {
    if (vanillaLeaves(block)) return true;
    if (auto const* e = entryOf(block); e && e->family == Family::Leaves) return true;
    std::string const& id = block.getTypeName();
    if (id.find("shulker_box") != std::string::npos) return true;
    return id == "minecraft:barrier" || id == "minecraft:carved_pumpkin" || id == "minecraft:lit_pumpkin" || id == "minecraft:melon_block"
        || id == "minecraft:pumpkin";
}

bool hasNeighborSignal(BlockSource& region, BlockPos const& pos) { return redstone::powered(region, pos); }

std::vector<Actor*> entitiesIn(BlockSource& region, AABB const& box) {
    auto out = region.getEntities(box, 0.0f);
    auto dim = region.getDimensionId();
    region.getLevel().forEachPlayer([&](Player& p) {
        if (p.getDimensionId() != dim || std::find(out.begin(), out.end(), &p) != out.end()) return true;
        if (p.getAABB().intersects(box)) out.push_back(&p);
        return true;
    });
    return out;
}

void sound(BlockSource& region, BlockPos const& pos, std::string const& event, float volume, float pitch) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    fx::sound(*dim, event, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, volume, pitch);
}

void bedrockSound(BlockSource& region, BlockPos const& pos, std::string const& name, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName   = name;
    pkt.mPos    = BlockPos{pos.x * 8 + 4, pos.y * 8 + 4, pos.z * 8 + 4};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    pkt.sendTo(pos, region.getDimensionId());
}

namespace {
std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
} // namespace

float wobblePitch() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()) * 0.1f + 0.9f; }

void particle(BlockSource& region, BlockPos const& pos, std::string const& effect) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(region.getDimensionId()).lock();
    if (!dim) return;
    level->spawnParticleEffect(effect, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, dim.get());
}

void breakEffect(BlockSource& region, BlockPos const& pos, Block const& block) {
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, static_cast<int>(block.mNetworkId), nullptr
    );
}

ItemStack makeItem(std::string_view id, int count) {
    auto s = game::items::make(id, count);
    return s ? *s : ItemStack{};
}

void drop(BlockSource& region, BlockPos const& pos, ItemStack const& item) {
    if (item.isNull()) return;
    region.getLevel().getSpawner().spawnItem(region, item, nullptr, Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f}, 10);
}

void give(Player& player, ItemStack const& item) {
    if (item.isNull()) return;
    ItemStack stack = item;
    if (!player.add(stack) || !stack.isNull()) {
        if (!stack.isNull()) player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), stack, nullptr, player.getPosition(), 0);
    }
    player.sendInventory(false);
}

void consumeHeld(Player& player, ItemStack const& contextItem) {
    if (player.isCreative()) return;
    auto& item = const_cast<ItemStack&>(contextItem);
    if (item.isNull()) return;
    item.remove(1);
    if (item.mCount <= 0) item = ItemStack::EMPTY_ITEM();
    player.sendInventory(false);
}

void destroyWithDrops(BlockSource& region, BlockPos const& pos) { region.getLevel().destroyBlock(region, pos, true, BlockChangeContext{}); }

void dropAndRemove(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    if (block.isAir()) return;
    // Block.dropResources: the block's loot without a tool or an entity (the loot contract's Java
    // table for a TF block), then Level.removeBlock: no levelEvent 2001 (particles and sound).
    // Java's removeBlock puts the block's fluid in its place (FluidState.createLegacyBlock), and so
    // does Bedrock's: the water of a waterlogged block (its liquid layer) becomes a water source
    // there (selftest families.leaves_waterlogged)
    ResourceDropsContext ctx{ResourceDropsCause::Other, 0.0f, ItemStack::EMPTY_ITEM(), pos, region.getDimensionId(), region};
    block.spawnResources(region, pos, region.getLevel().getIRandom(), ctx, nullptr);
    region.removeBlock(pos, BlockChangeContext{});
}

bool mayBuild(Player const& player) { return player.canUseAbility(AbilitiesIndex::Build); }

// ---- deferred and scheduled work --------------------------------------------------------------

namespace {
struct Scheduled {
    std::int64_t          due;
    std::function<void()> fn;
};
/// The deferred and scheduled work. Lifetime: the queues live on the heap for the whole process
/// and are never destroyed, so nothing that runs late (a world-closed listener at server stop, a
/// static destructor at unload) meets destroyed containers. Thread safety: the server thread runs
/// and fills them; a world close (ServerStoppingEvent, which BDS may raise on another thread while
/// the level still ticks) only hands the pending work to `retired` under the lock, and the work is
/// destroyed by the next tick on the server thread (or never, when no tick comes: the process ends).
struct Queues {
    std::mutex                         mutex;
    std::vector<std::function<void()>> deferred;
    std::list<Scheduled>               scheduled;
    std::vector<std::function<void()>> retired;
    std::list<Scheduled>               retiredScheduled;
};
Queues& queues() {
    static auto* q = new Queues(); // never destroyed (see above)
    return *q;
}
/// The kit's tick count (atomic: schedule() may be called off the server thread).
std::atomic<std::int64_t>& tickCounter() {
    static std::atomic<std::int64_t> t{0};
    return t;
}
} // namespace

std::int64_t now() { return tickCounter(); }

bool repeatedUse(Player& player, GlobalPos const& pos, bool firstEvent) {
    struct LastUse {
        GlobalPos    pos;
        std::int64_t tick;
    };
    static std::map<std::int64_t, LastUse> last;
    auto  uid  = player.getOrCreateUniqueID().rawID;
    auto  now  = tickCounter().load();
    auto  it   = last.find(uid);
    bool  held = !firstEvent && it != last.end() && it->second.pos == pos && now - it->second.tick < 5;
    last[uid]  = {pos, now};
    if (last.size() > 256) last.clear();
    return held;
}

void defer(std::function<void()> fn) {
    auto&           q = queues();
    std::lock_guard lock(q.mutex);
    q.deferred.push_back(std::move(fn));
}

void schedule(int ticks, std::function<void()> fn) {
    auto&           q = queues();
    std::lock_guard lock(q.mutex);
    q.scheduled.push_back({tickCounter() + std::max(1, ticks), std::move(fn)});
}

namespace detail {
/// One server tick of the deferred / scheduled queues (Module.cpp). The work runs outside the lock
/// (it defers and schedules more).
void runQueues() {
    auto& q = queues();
    {
        // the work of a closed world goes here, on the server thread
        std::vector<std::function<void()>> retired;
        std::list<Scheduled>               retiredScheduled;
        {
            std::lock_guard lock(q.mutex);
            retired.swap(q.retired);
            retiredScheduled.swap(q.retiredScheduled);
        }
    }
    ++tickCounter();
    auto run = [](std::function<void()>& fn) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("families: deferred block update failed: {}", e.what());
        }
    };
    // deferred work may defer more (a door half updating the other): a few rounds per tick
    for (int round = 0; round < 8; ++round) {
        std::vector<std::function<void()>> work;
        {
            std::lock_guard lock(q.mutex);
            work.swap(q.deferred);
        }
        if (work.empty()) break;
        for (auto& fn : work) run(fn);
    }
    std::vector<std::function<void()>> due;
    {
        std::lock_guard lock(q.mutex);
        for (auto it = q.scheduled.begin(); it != q.scheduled.end();) {
            if (it->due <= tickCounter()) {
                due.push_back(std::move(it->fn));
                it = q.scheduled.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& fn : due) run(fn);
}
/// A world closed (or the module uninstalls): the pending work is dropped. It is only handed aside
/// here and destroyed by the next tick on the server thread (see Queues).
void clearQueues() {
    auto&           q = queues();
    std::lock_guard lock(q.mutex);
    for (auto& fn : q.deferred) q.retired.push_back(std::move(fn));
    q.deferred.clear();
    q.retiredScheduled.splice(q.retiredScheduled.end(), q.scheduled);
}
} // namespace detail

} // namespace gf::feature::families

// Contract "families+blockevents", block events (F11): the one central dispatch of block callbacks
// (rule R8). Custom Bedrock blocks get no Java-style overrides, so the game's events are turned
// into Java's block callbacks here and handed to the units by block id or block tag ("#ns:path",
// gf::tags):
//
//   random tick       LevelChunk::tickImpl: Java's random ticks (ServerLevel.tickChunk), the
//                     gamerule's randomTickSpeed x 3 positions per 16-block section per tick
//                     (Bedrock's default 1 is Java's default 3), only in sections whose palette
//                     holds a block with handlers
//   neighbour change  every block change (any cause, LeviLamina's BlockChangedEvent) notifies the
//                     six neighbours, at the end of the tick (Java neighborChanged / updateShape)
//   placed / removed  the same changes when the block type changes; the placer is the player of
//                     a use-on placement (interactions::onPlace), else null
//   entity inside     every tick, the blocks an entity's box (deflated by 1e-7, as Java's
//                     checkInsideBlocks) intersects
//   stepped on        every tick, the block under an entity standing on the ground (Java stepOn)
//   projectile hit    ProjectileComponent::onHit on a block
//
// The families themselves also hear when a chunk ticks for the first time after it loaded (doors
// and switches re-arm, sign display entities are checked) and which block a placement replaced.

#include "feature/families/BlockEvents.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunk.h"
#include "mc/world/level/chunk/SubChunkStorage.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/common/Interactions.h"
#include "feature/families/Families.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::families::detail {
using ChunkFn = std::function<void(BlockSource& region, LevelChunk& chunk)>;
}

namespace gf::blockevents {

namespace fam = feature::families;

namespace {

/// Java's RandomSource over a 64-bit Mersenne twister (random ticks are not seeded in Java either).
class TickRandom final : public RandomSource {
public:
    explicit TickRandom(std::uint64_t seed) : mRng(seed) {}
    void         setSeed(std::int64_t seed) override { mRng.seed(static_cast<std::uint64_t>(seed)); }
    int          nextInt() override { return static_cast<int>(static_cast<std::uint32_t>(mRng() >> 32)); }
    int          nextInt(int bound) override { return bound <= 0 ? 0 : static_cast<int>(mRng() % static_cast<std::uint64_t>(bound)); }
    std::int64_t nextLong() override { return static_cast<std::int64_t>(mRng()); }
    bool         nextBoolean() override { return (mRng() >> 63) != 0; }
    float        nextFloat() override { return static_cast<float>(mRng() >> 40) * 0x1.0p-24f; }
    double       nextDouble() override { return static_cast<double>(mRng() >> 11) * 0x1.0p-53; }
    double       nextGaussian() override { return std::normal_distribution<double>(0.0, 1.0)(mRng); }
    std::unique_ptr<RandomSource> fork() override { return std::make_unique<TickRandom>(mRng()); }
    std::unique_ptr<core::jrand::PositionalRandomFactory> forkPositional() override {
        struct Factory final : core::jrand::PositionalRandomFactory {
            std::uint64_t                 seed;
            explicit Factory(std::uint64_t s) : seed(s) {}
            std::unique_ptr<RandomSource> at(int x, int y, int z) override {
                return std::make_unique<TickRandom>(seed ^ (static_cast<std::uint64_t>(x) * 3129871ull) ^ (static_cast<std::uint64_t>(z) * 116129781ull) ^ static_cast<std::uint64_t>(y));
            }
            std::unique_ptr<RandomSource> fromHashOf(std::string_view name) override {
                return std::make_unique<TickRandom>(seed ^ std::hash<std::string_view>{}(name));
            }
        };
        return std::make_unique<Factory>(mRng());
    }

private:
    std::mt19937_64 mRng;
};

TickRandom& tickRandom() {
    static TickRandom r{std::random_device{}()};
    return r;
}

enum Kind { RandomTickK, NeighborK, InsideK, StepK, ProjectileK, PlacedK, RemovedK, KindCount };

struct Handlers {
    std::vector<std::pair<std::string, RandomTickFn>> randomTick;
    std::vector<std::pair<std::string, NeighborFn>>   neighbor;
    std::vector<std::pair<std::string, EntityFn>>     inside;
    std::vector<std::pair<std::string, EntityFn>>     step;
    std::vector<std::pair<std::string, EntityFn>>     projectile;
    std::vector<std::pair<std::string, PlacedFn>>     placed;
    std::vector<std::pair<std::string, RemovedFn>>    removed;
    /// per kind: block type -> indices of the matching handlers (filled on the server thread)
    std::array<std::unordered_map<BlockType const*, std::vector<std::size_t>>, KindCount> cache;
};
Handlers& handlers() {
    static Handlers h;
    return h;
}

template <class Fn>
std::vector<std::pair<std::string, Fn>>& listOf(Kind k);
template <>
std::vector<std::pair<std::string, RandomTickFn>>& listOf<RandomTickFn>(Kind) {
    return handlers().randomTick;
}
template <>
std::vector<std::pair<std::string, NeighborFn>>& listOf<NeighborFn>(Kind) {
    return handlers().neighbor;
}
template <>
std::vector<std::pair<std::string, EntityFn>>& listOf<EntityFn>(Kind k) {
    auto& h = handlers();
    return k == InsideK ? h.inside : (k == StepK ? h.step : h.projectile);
}
template <>
std::vector<std::pair<std::string, PlacedFn>>& listOf<PlacedFn>(Kind) {
    return handlers().placed;
}
template <>
std::vector<std::pair<std::string, RemovedFn>>& listOf<RemovedFn>(Kind) {
    return handlers().removed;
}

bool targetMatches(std::string const& target, std::string const& id) {
    if (target.empty()) return false;
    if (target[0] == '#') return tags::has(tags::Registry::Block, target.substr(1), id);
    return target == id;
}

template <class Fn>
std::vector<std::size_t> const& matching(Kind k, BlockType const& type) {
    auto& cache = handlers().cache[k];
    auto  it    = cache.find(&type);
    if (it != cache.end()) return it->second;
    std::vector<std::size_t> idx;
    auto const&              list = listOf<Fn>(k);
    std::string const&       id   = type.getTypeName();
    for (std::size_t i = 0; i < list.size(); ++i)
        if (targetMatches(list[i].first, id)) idx.push_back(i);
    return cache.emplace(&type, std::move(idx)).first->second;
}

/// A small direct-mapped cache of "has random tick handlers" per block type: the random tick scan
/// asks it for every palette entry of every ticking section.
struct TypeFlagCache {
    struct Slot {
        BlockType const* type = nullptr;
        bool             has  = false;
    };
    std::array<Slot, 512> slots{};
    void clear() { slots.fill({}); }
};
TypeFlagCache& randomTickTypes() {
    static TypeFlagCache c;
    return c;
}

template <class Fn>
void add(Kind k, std::string const& target, Fn fn) {
    if (!fn || target.empty()) return;
    listOf<Fn>(k).emplace_back(target, std::move(fn));
    handlers().cache[k].clear();
    if (k == RandomTickK) randomTickTypes().clear();
}

template <class Fn, class... Args>
void call(Kind k, Block const& block, char const* what, Args&&... args) {
    auto const& idx = matching<Fn>(k, block.getBlockType());
    if (idx.empty()) return;
    auto const& list = listOf<Fn>(k);
    // copies: a handler may register more handlers
    std::vector<Fn> fns;
    fns.reserve(idx.size());
    for (auto i : idx) fns.push_back(list[i].second);
    for (auto& fn : fns) {
        try {
            fn(args...);
        } catch (std::exception const& e) {
            log().error("blockevents: {} handler for {} failed: {}", what, block.getTypeName(), e.what());
        }
    }
}

bool any(Kind k) {
    auto& h = handlers();
    switch (k) {
    case RandomTickK: return !h.randomTick.empty();
    case NeighborK: return !h.neighbor.empty();
    case InsideK: return !h.inside.empty();
    case StepK: return !h.step.empty();
    case ProjectileK: return !h.projectile.empty();
    case PlacedK: return !h.placed.empty();
    case RemovedK: return !h.removed.empty();
    default: return false;
    }
}

template <class Fn>
bool has(Kind k, Block const& block) {
    return any(k) && !matching<Fn>(k, block.getBlockType()).empty();
}

bool randomTicks(BlockType const& type) {
    auto& c    = randomTickTypes();
    auto  key  = reinterpret_cast<std::uintptr_t>(&type);
    auto& slot = c.slots[(key >> 4) % c.slots.size()];
    if (slot.type != &type) {
        slot.type = &type;
        slot.has  = !matching<RandomTickFn>(RandomTickK, type).empty();
    }
    return slot.has;
}

// ---- random ticks ---------------------------------------------------------------------------------

struct TickRate {
    std::int64_t tick  = -1;
    int          speed = 1;
};
TickRate& tickRate() {
    static TickRate r;
    return r;
}

int randomTickSpeed(Level& level) {
    auto& r    = tickRate();
    auto  tick = static_cast<std::int64_t>(level.getCurrentServerTick().tickID);
    if (r.tick != tick) {
        r.tick  = tick;
        r.speed = 1;
        auto const& rules = level.getGameRules().mGameRules.get();
        auto        index = static_cast<std::size_t>(GameRules::GameRulesIndex::RandomTickSpeed);
        if (index < rules.size()) {
            if (auto const* v = std::get_if<int>(&rules[index].mValue.get())) r.speed = std::max(0, *v);
        }
    }
    return r.speed;
}

/// The section's palette holds a block with random tick handlers.
bool sectionTicks(SubChunk const& sub) {
    auto const* storage = sub.mBlocksReadPtr[0];
    if (!storage) return false;
    return storage->hasAnyElementMatchingFilterInPalette([](Block const& b) { return randomTicks(b.getBlockType()); });
}

void randomTicks(LevelChunk& chunk, BlockSource& region) {
    auto& level = region.getLevel();
    if (level.isClientSide()) return;
    int perSection = randomTickSpeed(level) * 3;
    if (perSection <= 0) return;
    ChunkPos const cp   = chunk.mPosition;
    int const      maxY = region.getMaxHeight();
    auto&          rng  = tickRandom();
    for (auto const& sub : *chunk.mSubChunks) {
        if (!sectionTicks(sub)) continue;
        int y0 = static_cast<int>(sub.mAbsoluteIndex) * 16;
        for (int i = 0; i < perSection; ++i) {
            int      bits = rng.nextInt();
            BlockPos pos{cp.x * 16 + (bits & 15), y0 + ((bits >> 4) & 15), cp.z * 16 + ((bits >> 8) & 15)};
            if (pos.y >= maxY) continue;
            Block const& block = region.getBlock(pos);
            if (block.isAir() || !randomTicks(block.getBlockType())) continue;
            call<RandomTickFn>(RandomTickK, block, "random tick", region, pos, block, static_cast<RandomSource&>(rng));
        }
    }
}

// ---- chunks: the first tick after a load ---------------------------------------------------------------

std::vector<fam::detail::ChunkFn>& chunkListeners() {
    static std::vector<fam::detail::ChunkFn> l;
    return l;
}
/// (dimension, chunk) -> the last server tick it ticked; a chunk not ticked for a while has been
/// unloaded (or left the simulation distance) and counts as loaded again.
std::map<std::tuple<int, int, int>, std::int64_t>& tickedChunks() {
    static std::map<std::tuple<int, int, int>, std::int64_t> m;
    return m;
}

void chunkTicked(LevelChunk& chunk, BlockSource& region) {
    if (chunkListeners().empty() || region.getLevel().isClientSide()) return;
    auto           now = fam::now();
    ChunkPos const cp  = chunk.mPosition;
    auto           key = std::make_tuple(static_cast<int>(region.getDimensionId()), cp.x, cp.z);
    auto& m   = tickedChunks();
    auto  it  = m.find(key);
    bool  fresh = it == m.end() || now - it->second > 40;
    m[key]      = now;
    if (now % 600 == 0) std::erase_if(m, [now](auto const& kv) { return now - kv.second > 1200; });
    if (!fresh) return;
    for (auto const& fn : chunkListeners()) {
        try {
            fn(region, chunk);
        } catch (std::exception const& e) {
            log().error("blockevents: chunk load handler failed: {}", e.what());
        }
    }
}

// ---- block changes ----------------------------------------------------------------------------------

struct PendingPlace {
    std::int64_t placer = 0; ///< ActorUniqueID of the player who placed it (0: none)
};
std::map<core::GlobalPos, PendingPlace>& pendingPlaces() {
    static std::map<core::GlobalPos, PendingPlace> m;
    return m;
}
std::set<core::GlobalPos>& pendingRemovals() {
    static std::set<core::GlobalPos> s;
    return s;
}
std::set<std::pair<core::GlobalPos, core::GlobalPos>>& pendingNeighbors() {
    static std::set<std::pair<core::GlobalPos, core::GlobalPos>> s;
    return s;
}
/// The block a family block replaced this tick (Java BlockPlaceContext: the replaced state).
std::map<core::GlobalPos, Block const*>& replaced() {
    static std::map<core::GlobalPos, Block const*> m;
    return m;
}

Actor* actorById(std::int64_t uid) {
    if (uid == 0) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

void onBlockChanged(BlockSource& region, BlockPos const& pos, Block const& previous, Block const& now) {
    if (region.getLevel().isClientSide()) return;
    int  dim         = static_cast<int>(region.getDimensionId());
    auto gp          = game::globalPos(dim, pos);
    bool typeChanged = &previous.getBlockType() != &now.getBlockType();
    if (typeChanged && fam::entryOf(now)) {
        if (replaced().empty()) fam::defer([] { replaced().clear(); });
        replaced()[gp] = &previous;
    }
    // (the game reports some changes twice: one dispatch per position and tick)
    if (typeChanged && has<RemovedFn>(RemovedK, previous) && pendingRemovals().insert(gp).second) {
        Block const* prev = &previous;
        fam::defer([dim, pos, prev, gp] {
            pendingRemovals().erase(gp);
            if (auto* r = fam::regionOf(dim)) call<RemovedFn>(RemovedK, *prev, "removed", *r, pos, *prev);
        });
    }
    if (typeChanged && has<PlacedFn>(PlacedK, now) && !pendingPlaces().contains(gp)) {
        pendingPlaces()[gp] = {};
        fam::defer([dim, pos, gp] {
            auto it     = pendingPlaces().find(gp);
            auto placer = it != pendingPlaces().end() ? it->second.placer : 0;
            if (it != pendingPlaces().end()) pendingPlaces().erase(it);
            auto* r = fam::regionOf(dim);
            if (!r) return;
            auto const* b = fam::blockAt(*r, pos);
            if (!b) return;
            call<PlacedFn>(PlacedK, *b, "placed", *r, pos, *b, actorById(placer));
        });
    }
    if (!any(NeighborK)) return;
    for (int d = 0; d < 6; ++d) {
        BlockPos     n  = fam::offset(pos, d);
        Block const* nb = fam::blockAt(region, n);
        if (!nb || !has<NeighborFn>(NeighborK, *nb)) continue;
        auto key = std::make_pair(game::globalPos(dim, n), gp);
        if (!pendingNeighbors().insert(key).second) continue;
        fam::defer([dim, n, pos, key] {
            pendingNeighbors().erase(key);
            auto* r = fam::regionOf(dim);
            if (!r) return;
            auto const* b = fam::blockAt(*r, n);
            if (!b) return;
            call<NeighborFn>(NeighborK, *b, "neighbour change", *r, n, pos);
        });
    }
}

// ---- entities ------------------------------------------------------------------------------------------

void entityTick() {
    if (!any(InsideK) && !any(StepK)) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    // every runtime actor and every player (simulated players included); the buffers are reused
    static std::vector<Actor*>             actors;
    static std::unordered_set<Actor const*> seen;
    actors = level->getRuntimeActorList();
    seen.clear();
    seen.insert(actors.begin(), actors.end());
    level->forEachPlayer([&](Player& p) {
        if (seen.insert(&p).second) actors.push_back(&p);
        return true;
    });
    for (Actor* actor : actors) {
        if (!actor || !actor->isAlive()) continue;
        auto& region = actor->getDimensionBlockSource();
        AABB  box    = actor->getAABB();
        if (any(InsideK)) {
            // Java Entity.checkInsideBlocks: the box deflated by 1e-7, every block it touches
            int x0 = static_cast<int>(std::floor(box.min.x + 1e-7)), x1 = static_cast<int>(std::floor(box.max.x - 1e-7));
            int y0 = static_cast<int>(std::floor(box.min.y + 1e-7)), y1 = static_cast<int>(std::floor(box.max.y - 1e-7));
            int z0 = static_cast<int>(std::floor(box.min.z + 1e-7)), z1 = static_cast<int>(std::floor(box.max.z - 1e-7));
            bool alive = true;
            for (int x = x0; x <= x1 && alive; ++x)
                for (int y = y0; y <= y1 && alive; ++y)
                    for (int z = z0; z <= z1 && alive; ++z) {
                        BlockPos     p{x, y, z};
                        Block const* b = fam::blockAt(region, p);
                        if (!b || b->isAir() || !has<EntityFn>(InsideK, *b)) continue;
                        call<EntityFn>(InsideK, *b, "entity inside", region, p, *actor);
                        alive = actor->isAlive();
                    }
        }
        if (any(StepK) && actor->isOnGround() && actor->isAlive()) {
            // Java Entity.getOnPos: the block 0.2 below the feet
            BlockPos     p{static_cast<int>(std::floor(actor->getPosition().x)), static_cast<int>(std::floor(box.min.y - 0.2f)),
                           static_cast<int>(std::floor(actor->getPosition().z))};
            Block const* b = fam::blockAt(region, p);
            if (b && !b->isAir() && has<EntityFn>(StepK, *b)) call<EntityFn>(StepK, *b, "stepped on", region, p, *actor);
        }
    }
}

std::vector<ll::event::ListenerPtr>& listeners() {
    static std::vector<ll::event::ListenerPtr> l;
    return l;
}
std::vector<interactions::HandlerId>& placeHandlers() {
    static std::vector<interactions::HandlerId> h;
    return h;
}

} // namespace

// ---- hooks ---------------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    BlockEventsChunkTickHook,
    ll::memory::HookPriority::Normal,
    LevelChunk,
    &LevelChunk::tickImpl,
    void,
    ::BlockSource&          tickRegion,
    ::Tick const&           tick,
    ::std::function<void()> spawnerCallback
) {
    origin(tickRegion, tick, std::move(spawnerCallback));
    try {
        chunkTicked(*this, tickRegion);
        if (any(RandomTickK)) randomTicks(*this, tickRegion);
    } catch (std::exception const& e) {
        log().error("blockevents: random ticks failed: {}", e.what());
    }
}

LL_TYPE_INSTANCE_HOOK(
    BlockEventsProjectileHitHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    ::Actor&           owner,
    ::HitResult const& hitResult
) {
    if (hitResult.mType == HitResultType::Tile && any(ProjectileK) && !owner.isClientSide()) {
        auto&        region = owner.getDimensionBlockSource();
        BlockPos     pos    = hitResult.mBlock;
        Block const* b      = fam::blockAt(region, pos);
        if (b && has<EntityFn>(ProjectileK, *b)) call<EntityFn>(ProjectileK, *b, "projectile hit", region, pos, owner);
    }
    origin(owner, hitResult);
}

// ---- contract ------------------------------------------------------------------------------------------

void onRandomTick(std::string const& target, RandomTickFn fn) { add(RandomTickK, target, std::move(fn)); }
void onNeighborChanged(std::string const& target, NeighborFn fn) { add(NeighborK, target, std::move(fn)); }
void onEntityInside(std::string const& target, EntityFn fn) { add(InsideK, target, std::move(fn)); }
void onSteppedOn(std::string const& target, EntityFn fn) { add(StepK, target, std::move(fn)); }
void onProjectileHit(std::string const& target, EntityFn fn) { add(ProjectileK, target, std::move(fn)); }
void onPlaced(std::string const& target, PlacedFn fn) { add(PlacedK, target, std::move(fn)); }
void onRemoved(std::string const& target, RemovedFn fn) { add(RemovedK, target, std::move(fn)); }

} // namespace gf::blockevents

namespace gf::feature::families::detail {

/// Block event dispatch (Module.cpp): hooks, listeners and the per-tick entity scan.
void installBlockEvents() {
    using namespace gf::blockevents;
    BlockEventsChunkTickHook::hook();
    BlockEventsProjectileHitHook::hook();
    auto& bus = ll::event::EventBus::getInstance();
    listeners().push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
        if (ev.layer() != 0) return;
        try {
            onBlockChanged(ev.blockSource(), ev.pos(), ev.previousBlock(), ev.newBlock());
        } catch (std::exception const& e) {
            log().error("blockevents: block change dispatch failed: {}", e.what());
        }
    }));
    // the player of a use-on placement (Java setPlacedBy's placer)
    placeHandlers().push_back(interactions::onPlace(
        {},
        [](interactions::PlaceContext const& c) {
            auto it = pendingPlaces().find(c.pos);
            if (it != pendingPlaces().end()) it->second.placer = c.player.getOrCreateUniqueID().rawID;
        },
        interactions::priority::Highest
    ));
}

void tickBlockEvents() {
    try {
        gf::blockevents::entityTick();
    } catch (std::exception const& e) {
        log().error("blockevents: entity scan failed: {}", e.what());
    }
}

/// Selftests: one random tick of the block at `pos` (the handlers registered for it).
void randomTickAt(BlockSource& region, BlockPos const& pos) {
    Block const* b = blockAt(region, pos);
    if (!b || b->isAir()) return;
    gf::blockevents::call<gf::blockevents::RandomTickFn>(
        gf::blockevents::RandomTickK, *b, "random tick", region, pos, *b, static_cast<gf::blockevents::RandomSource&>(gf::blockevents::tickRandom())
    );
}

/// The families hear when a chunk ticks for the first time after it loaded.
void onChunkLoaded(ChunkFn fn) { gf::blockevents::chunkListeners().push_back(std::move(fn)); }

/// Every block of `chunk` whose type `want` accepts (only the sections whose palette holds one).
void forEachInChunk(BlockSource& region, LevelChunk& chunk, std::function<bool(BlockType const&)> const& want, std::function<void(BlockPos const&, Block const&)> const& fn) {
    ChunkPos const cp   = chunk.mPosition;
    int const      maxY = region.getMaxHeight();
    for (auto const& sub : *chunk.mSubChunks) {
        auto const* storage = sub.mBlocksReadPtr[0];
        if (!storage || !storage->hasAnyElementMatchingFilterInPalette([&](Block const& b) { return want(b.getBlockType()); })) continue;
        int y0 = static_cast<int>(sub.mAbsoluteIndex) * 16;
        for (int y = y0; y < y0 + 16 && y < maxY; ++y)
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z) {
                    BlockPos     p{cp.x * 16 + x, y, cp.z * 16 + z};
                    Block const& b = region.getBlock(p);
                    if (want(b.getBlockType())) fn(p, b);
                }
    }
}

/// The block a family block replaced at `pos` this tick (nullptr: unknown).
Block const* replacedBlock(GlobalPos const& pos) {
    auto& m  = gf::blockevents::replaced();
    auto  it = m.find(pos);
    return it == m.end() ? nullptr : it->second;
}

/// Whether the random tick scan of a section would look at this block type (selftests).
bool randomTicked(BlockType const& type) { return gf::blockevents::randomTicks(type); }

/// The block type cache (block types change between worlds on the client).
void resetBlockEvents() {
    for (auto& c : gf::blockevents::handlers().cache) c.clear();
    gf::blockevents::randomTickTypes().clear();
    gf::blockevents::pendingPlaces().clear();
    gf::blockevents::pendingRemovals().clear();
    gf::blockevents::pendingNeighbors().clear();
    gf::blockevents::replaced().clear();
    gf::blockevents::tickedChunks().clear();
}

void uninstallBlockEvents() {
    using namespace gf::blockevents;
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : listeners()) bus.removeListener(l);
    listeners().clear();
    for (auto id : placeHandlers()) interactions::remove(id);
    placeHandlers().clear();
    BlockEventsProjectileHitHook::unhook();
    BlockEventsChunkTickHook::unhook();
    resetBlockEvents();
}

} // namespace gf::feature::families::detail

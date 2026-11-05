// U26 (darktower): world helpers (see Util.h).
#include "feature/darktower/Util.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Redstone.h"
#include "feature/entity/Entity.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"

namespace gf::feature::darktower {

namespace dt = core::darktower;

Level* level() {
    auto l = ll::service::getLevel();
    return l ? l.as_ptr() : nullptr;
}

std::uint64_t now() {
    auto* l = level();
    return l ? l->getCurrentServerTick().tickID : 0;
}

core::jrand::RandomSource& random() {
    static core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 16 ^ std::random_device{}());
    return r;
}

int dimOf(BlockSource& region) { return static_cast<int>(region.getDimensionId()); }

std::string idAt(BlockSource& region, BlockPos const& pos) { return game::blockIdAt(region, pos); }

bool boolState(Block const& block, char const* state) {
    // by the serialized value: an integer read is the value's position in the state's list, and
    // some TF booleans are declared [true, false] (locked)
    return textState(block, state) == "true";
}

bool boolStateAt(BlockSource& region, BlockPos const& pos, char const* state) { return boolState(region.getBlock(pos), state); }

std::string textState(Block const& block, char const* state) {
    auto all = game::statesOf(block);
    auto it  = all.find(state);
    return it == all.end() ? std::string{} : it->second;
}

bool set(BlockSource& region, BlockPos const& pos, std::string const& blockId, Json const& states) {
    auto const* b = game::blockWithStates(blockId, states);
    return b && game::setBlock(region, pos, *b);
}

void remove(BlockSource& region, BlockPos const& pos) { region.removeBlock(pos, BlockChangeContext{}); }

void destroy(BlockSource& region, BlockPos const& pos, bool drops) {
    region.getLevel().destroyBlock(region, pos, drops, BlockChangeContext{});
}

void breakEffect(BlockSource& region, BlockPos const& pos, Block const& block) {
    region.getLevel().broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock,
        Vec3{pos.x + .5f, pos.y + .5f, pos.z + .5f},
        static_cast<int>(block.mNetworkId),
        nullptr
    );
}

bool isAir(BlockSource& region, BlockPos const& pos) { return region.getBlock(pos).isAir(); }

bool blocksMotion(BlockSource& region, BlockPos const& pos) {
    // Java BlockStateBase#blocksMotion: legacy solid (a collision shape averaging 0.729 or a full
    // block high), cobwebs and bamboo saplings excepted
    Block const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    auto id = game::blockId(b);
    if (id == "minecraft:web" || id == "minecraft:bamboo_sapling") return false;
    std::vector<AABB> boxes;
    b.getBlockType().addCollisionShapes(b, region, pos, nullptr, boxes, {});
    if (boxes.empty()) return false;
    AABB bounds = boxes.front();
    for (auto const& box : boxes) {
        bounds.min.x = std::min(bounds.min.x, box.min.x), bounds.min.y = std::min(bounds.min.y, box.min.y),
        bounds.min.z = std::min(bounds.min.z, box.min.z);
        bounds.max.x = std::max(bounds.max.x, box.max.x), bounds.max.y = std::max(bounds.max.y, box.max.y),
        bounds.max.z = std::max(bounds.max.z, box.max.z);
    }
    float sx = bounds.max.x - bounds.min.x, sy = bounds.max.y - bounds.min.y, sz = bounds.max.z - bounds.min.z;
    return (sx + sy + sz) / 3.0f >= 0.7291666f || sy >= 1.0f;
}

bool unbreakable(Block const& block) { return block.getBlockType().getDestroySpeed() < 0.0f; }

bool loaded(BlockSource& region, BlockPos const& pos, int radius) { return region.hasChunksAt(pos, radius, false); }

bool powered(BlockSource& region, BlockPos const& pos) { return redstone::powered(region, pos); }

namespace {
bool gameRule(GameRules::GameRulesIndex index) {
    auto* l = level();
    if (!l) return true;
    GameRuleId rule;
    rule.mValue = static_cast<int>(index);
    return l->getGameRules().getBool(rule, true);
}
} // namespace

bool mobGriefing() { return gameRule(GameRules::GameRulesIndex::MobGriefing); }
bool doTileDrops() { return gameRule(GameRules::GameRulesIndex::DoTileDrops); }

BlockPos offset(BlockPos const& p, dt::Facing f) {
    auto o = dt::offsetOf(f);
    return BlockPos{p.x + o[0], p.y + o[1], p.z + o[2]};
}

Vec3 center(BlockPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

void sound(BlockSource& region, BlockPos const& pos, char const* event, float volume, float pitch) {
    fx::sound(region.getDimension(), event, center(pos), volume, pitch);
}

void soundAt(BlockSource& region, Vec3 const& pos, char const* event, float volume, float pitch) {
    fx::sound(region.getDimension(), event, pos, volume, pitch);
}

void particle(BlockSource& region, char const* id, Vec3 const& pos, Vec3 const& velocity, int count, Json const& params) {
    fx::particle(region.getDimension(), id, pos, velocity, count, params);
}

void redstoneDust(BlockSource& region, Vec3 const& pos) { fx::particle(region.getDimension(), "minecraft:dust", pos); }

void sparkle(BlockSource& region, BlockPos const& pos) {
    auto& r = random();
    for (int f = 0; f < 6; ++f) {
        auto     o = dt::offsetOf(static_cast<dt::Facing>(f));
        BlockPos n{pos.x + o[0], pos.y + o[1], pos.z + o[2]};
        if (blocksMotion(region, n)) continue; // !isSolidRender
        double d1 = o[0] != 0 ? 0.5 + 0.5625 * o[0] : r.nextFloat();
        double d2 = o[1] != 0 ? 0.5 + 0.5625 * o[1] : r.nextFloat();
        double d3 = o[2] != 0 ? 0.5 + 0.5625 * o[2] : r.nextFloat();
        redstoneDust(region, Vec3{float(pos.x + d1), float(pos.y + d2), float(pos.z + d3)});
    }
}

std::int64_t uidOf(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Actor* actorById(std::int64_t uid) {
    auto* l = level();
    if (!uid || !l) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return l->fetchEntity(id, false);
}

Mob* asMob(Actor& actor) { return actor.hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(&actor) : nullptr; }

std::string typeOf(Actor const& actor) { return actor.getTypeName(); }

std::vector<Player*> playersNear(int dim, Vec3 const& c, double radius, bool spectators) {
    std::vector<std::pair<double, Player*>> found;
    auto*                                   l = level();
    if (!l) return {};
    l->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !p.isAlive()) return true;
        if (!spectators && p.isSpectator()) return true;
        auto   q  = p.getFeetPos();
        double dx = q.x - c.x, dy = q.y - c.y, dz = q.z - c.z, d = dx * dx + dy * dy + dz * dz;
        if (radius < 0 || d < radius * radius) found.emplace_back(d, &p);
        return true;
    }));
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<Player*> out;
    for (auto& [d, p] : found) out.push_back(p);
    return out;
}

Player* nearestPlayer(int dim, Vec3 const& c, double range) {
    auto v = playersNear(dim, c, range);
    return v.empty() ? nullptr : v.front();
}

std::vector<Actor*> actorsIn(BlockSource& region, AABB const& box) {
    auto  out = region.getEntities(box, 0.0f);
    auto* l   = level();
    if (!l) return out;
    int dim = dimOf(region);
    l->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !p.isAlive()) return true;
        auto const& b   = p.getAABB();
        bool        hit = b.min.x < box.max.x && b.max.x > box.min.x && b.min.y < box.max.y && b.max.y > box.min.y
                && b.min.z < box.max.z && b.max.z > box.min.z;
        if (hit && std::find(out.begin(), out.end(), &p) == out.end()) out.push_back(&p);
        return true;
    }));
    return out;
}

void setMotion(Actor& actor, Vec3 const& motion) {
    const_cast<Vec3&>(actor.getPosDelta()) = motion;
    if (actor.isPlayer()) {
        // the player's own client moves it: tell it (SetActorMotionPacket)
        auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
        if (!packet) return;
        auto& pkt      = static_cast<SetActorMotionPacket&>(*packet);
        pkt.mRuntimeId = actor.getRuntimeID();
        pkt.mMotion    = motion;
        pkt.sendTo(static_cast<Player&>(actor));
    }
}

void push(Actor& actor, Vec3 const& delta) {
    auto v = actor.getPosDelta();
    setMotion(actor, Vec3{v.x + delta.x, v.y + delta.y, v.z + delta.z});
}

Mob* spawnMob(BlockSource& region, std::string const& entityId, Vec3 const& at, float yaw) {
    auto* l = level();
    if (!l) return nullptr;
    auto* mob = l->getSpawner().spawnMob(region, ActorDefinitionIdentifier(entityId), nullptr, at, false, false, false);
    if (mob) {
        const_cast<Vec2&>(mob->getRotation()) = Vec2{0.0f, yaw};
        mob->setYBodyRotation(yaw);
    }
    return mob;
}

void visualLightning(BlockSource& region, Vec3 const& at) {
    // Bedrock's lightning bolt has no visual-only flag: the thunder alone (no fire, no damage)
    fx::sound(region.getDimension(), "entity.lightning_bolt.thunder", at, 10000.0f, 0.8f);
    fx::sound(region.getDimension(), "entity.lightning_bolt.impact", at, 2.0f, 0.5f);
}

int deathTime(Actor const& actor) {
    if (!actor.hasCategory(ActorCategory::Mob)) return 0;
    return static_cast<Mob const&>(actor).getDeathTime();
}

bool dying(Actor const& actor) { return !actor.isAlive() || deathTime(actor) > 0; }

} // namespace gf::feature::darktower

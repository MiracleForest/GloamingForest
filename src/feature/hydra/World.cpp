// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U24 (hydra): the world helpers of World.h on the game's API (no F8 internals).

#include "feature/hydra/World.h"

#include <algorithm>
#include <map>
#include <random>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/Interactions.h"
#include "feature/ext/entity/EntityExt.h"

namespace gf::feature::hydra::world {

namespace {
struct TickState {
    std::map<int, std::function<void()>> fns;
    int                                  next = 1;
    ll::event::ListenerPtr               listener;
};
TickState& ticks() {
    static TickState s;
    return s;
}
} // namespace

std::int64_t freshSeed() {
    static std::mt19937_64 rng{std::random_device{}()};
    return static_cast<std::int64_t>(rng());
}

std::int64_t uidOf(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Actor* actorById(std::int64_t uid) {
    if (!uid) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

Player* playerById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

Mob* asMob(Actor& actor) { return actor.hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(&actor) : nullptr; }

std::uint64_t currentTick() {
    auto level = ll::service::getLevel();
    return level ? level->getCurrentServerTick().tickID : 0;
}

std::int64_t parentOfPart(Actor const& part) {
    Actor* parent = gf::entity::ext::parentOf(part);
    return parent ? uidOf(*parent) : 0;
}

void setMotion(Actor& actor, Vec3 const& motion) {
    const_cast<Vec3&>(actor.getPosDelta()) = motion;
    if (actor.isPlayer()) {
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

std::vector<Player*> playersNear(int dim, Vec3 const& center, double radius) {
    std::vector<Player*> out;
    auto                 level = ll::service::getLevel();
    if (!level) return out;
    double r2 = radius * radius;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !p.isAlive()) return true;
        auto   pos = p.getFeetPos();
        double dx = pos.x - center.x, dy = pos.y - center.y, dz = pos.z - center.z;
        if (dx * dx + dy * dy + dz * dz < r2) out.push_back(&p);
        return true;
    }));
    return out;
}

std::vector<Actor*> actorsIn(BlockSource& region, int dim, AABB const& box) {
    // the chunks' actor lists hold no players: add them by their boxes
    auto out   = region.getEntities(box, 0.0f);
    auto level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || !p.isAlive()) return true;
        auto const& b   = p.getAABB();
        bool        hit = b.min.x < box.max.x && b.max.x > box.min.x && b.min.y < box.max.y && b.max.y > box.min.y && b.min.z < box.max.z && b.max.z > box.min.z;
        if (hit && std::find(out.begin(), out.end(), &p) == out.end()) out.push_back(&p);
        return true;
    }));
    return out;
}

double distanceSqr(Actor const& a, Vec3 const& p) {
    auto   q  = a.getFeetPos();
    double dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
    return dx * dx + dy * dy + dz * dz;
}

double distanceSqr(Actor const& a, Actor const& b) { return distanceSqr(a, b.getFeetPos()); }

bool hasLineOfSight(Mob& mob, Actor& target) {
    if (&target.getDimension() != &mob.getDimension()) return false;
    if (distanceSqr(mob, target) > 128.0 * 128.0) return false;
    return mob.canSee(target.getEyePos(), ShapeType::Collision);
}

bool legacySolid(BlockSource& region, BlockPos const& pos) {
    Block const& block = region.getBlock(pos);
    AABB         b     = block.getBlockType().getCollisionShape(block, region, pos, nullptr);
    double       w = b.max.x - b.min.x, h = b.max.y - b.min.y, d = b.max.z - b.min.z;
    if (w <= 1e-6 || h <= 1e-6 || d <= 1e-6) return false;
    return (w + h + d) / 3.0 >= 0.7291666666666666 || h >= 1.0;
}

bool mobGriefing() {
    auto lv = ll::service::getLevel();
    if (!lv) return false;
    GameRuleId id;
    id.mValue = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
    return lv->getGameRules().getBool(id, true);
}

bool hard() {
    auto lv = ll::service::getLevel();
    return lv && lv->getDifficulty() == SharedTypes::Legacy::Difficulty::Hard;
}

int onLevelTick(std::function<void()> fn) {
    auto& s  = ticks();
    int   id = s.next++;
    s.fns.emplace(id, std::move(fn));
    if (!s.listener)
        s.listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            // a copy: a callback may remove itself
            std::vector<std::function<void()>> run;
            for (auto const& [k, f] : ticks().fns) run.push_back(f);
            for (auto const& f : run) f();
        });
    return id;
}

void removeLevelTick(int id) {
    auto& s = ticks();
    s.fns.erase(id);
    if (s.fns.empty() && s.listener) {
        ll::event::EventBus::getInstance().removeListener(s.listener);
        s.listener = nullptr;
    }
}

void removeLater(std::int64_t uid) {
    gf::interactions::nextTick([uid] {
        if (auto* a = actorById(uid)) a->remove();
    });
}

} // namespace gf::feature::hydra::world

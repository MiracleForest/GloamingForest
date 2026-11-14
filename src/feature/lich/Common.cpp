// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U21 (lich): helpers of the Lich unit (see Common.h).

#include "feature/lich/Common.h"

#include <atomic>
#include <thread>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/entity/components/LookControlComponent.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/pathfinder/Path.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/common/Interactions.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"

namespace gf::feature::lich {

// ---- actors ----------------------------------------------------------------------------------------

std::int64_t uidOf(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Actor* actorById(std::int64_t uid) {
    if (!uid) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

Mob* asMob(Actor& actor) { return actor.hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(&actor) : nullptr; }

Mob* mobById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a ? asMob(*a) : nullptr;
}

bool isType(Actor const& actor, char const* id) { return actor.getTypeName() == id; }

bool isServerActor(Actor const& actor) {
    auto level = ll::service::getLevel();
    return level && &actor.getLevel() == level.as_ptr();
}

int dimOf(Actor const& actor) { return static_cast<int>(actor.getDimensionId()); }

Actor* targetOf(Mob& mob) {
    auto* t = actorById(mob.mTargetId->rawID);
    return t && t->isAlive() ? t : nullptr;
}

void setTarget(Mob& mob, Actor* target) { mob.setTarget(target); }

Actor* lastHurtBy(Mob& mob) {
    auto* t = actorById(mob.mLastHurtByMobId->rawID);
    return t && t->isAlive() ? t : nullptr;
}

void setLastHurtBy(Mob& mob, Actor* by) {
    if (!by) {
        mob.setLastHurtByMob(nullptr);
        return;
    }
    if (by->isPlayer()) mob.setLastHurtByPlayer(static_cast<Player*>(by));
    else if (auto* m = asMob(*by)) mob.setLastHurtByMob(m);
}

namespace {
std::unordered_map<std::int64_t, std::unique_ptr<Random>>& randoms() {
    static std::unordered_map<std::int64_t, std::unique_ptr<Random>> m;
    return m;
}
std::int64_t freshSeed() {
    static std::mt19937_64 rng{std::random_device{}()};
    return static_cast<std::int64_t>(rng());
}
} // namespace

Random& randomOf(Actor const& actor) {
    auto& slot = randoms()[uidOf(actor)];
    if (!slot) slot = std::make_unique<Random>(freshSeed());
    return *slot;
}

void seedRandom(Actor const& actor, std::int64_t seed) { randoms()[uidOf(actor)] = std::make_unique<Random>(seed); }

void forgetRandom(std::int64_t uid) { randoms().erase(uid); }

std::vector<Player*> playersIn(int dim) {
    std::vector<Player*> out;
    auto                 level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (dimOf(p) == dim && p.isAlive()) out.push_back(&p);
        return true;
    }));
    return out;
}

std::vector<Player*> playersNear(int dim, Vec3 const& center, double radius) {
    std::vector<Player*> out;
    for (auto* p : playersIn(dim))
        if (distSqr(p->getFeetPos(), center) < radius * radius) out.push_back(p);
    return out;
}

std::vector<Actor*> actorsIn(BlockSource& region, int dim, AABB const& box) {
    auto out = region.getEntities(box, 0.0f);
    for (auto* p : playersIn(dim)) {
        auto const& b   = p->getAABB();
        bool        hit = b.min.x < box.max.x && b.max.x > box.min.x && b.min.y < box.max.y && b.max.y > box.min.y
                && b.min.z < box.max.z && b.max.z > box.min.z;
        if (hit && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return out;
}

Player* nearestPlayer(int dim, Vec3 const& c, double range) {
    Player* best = nullptr;
    double  bd   = 1e300;
    for (auto* p : playersIn(dim)) {
        if (p->isSpectator()) continue;
        double d = distSqr(p->getFeetPos(), c);
        if (range >= 0 && d >= range * range) continue;
        if (d < bd) bd = d, best = p;
    }
    return best;
}

// ---- geometry ----------------------------------------------------------------------------------------

double distSqr(Vec3 const& a, Vec3 const& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

double distance(Actor const& a, Actor const& b) { return std::sqrt(distSqr(a.getFeetPos(), b.getFeetPos())); }

Vec3 eyeOf(Actor const& actor) { return actor.getEyePos(); }

float bbHeight(Actor const& actor) {
    auto const& b = actor.getAABB();
    return b.max.y - b.min.y;
}

float bbWidth(Actor const& actor) {
    auto const& b = actor.getAABB();
    return b.max.x - b.min.x;
}

bool clearPath(BlockSource& region, Vec3 const& from, Vec3 const& to) {
    HitResult hr = region.clip(from, to, false, ShapeType::Collision, 200, false, false, nullptr, [](BlockSource const&, Block const&, bool) { return true; }, false);
    return hr.mType != HitResultType::Tile;
}

bool hasLineOfSight(Mob& mob, Actor& target) {
    if (&target.getDimension() != &mob.getDimension()) return false;
    if (distSqr(target.getFeetPos(), mob.getFeetPos()) > 128.0 * 128.0) return false;
    return mob.canSee(target.getEyePos(), ShapeType::Collision);
}

Vec3 viewVector(Actor const& actor) {
    auto   rot   = actor.getRotation(); // x pitch, y yaw (degrees)
    double pitch = rot.x * std::numbers::pi / 180.0, yaw = -rot.y * std::numbers::pi / 180.0;
    double c     = std::cos(pitch);
    return Vec3{float(std::sin(yaw) * c), float(-std::sin(pitch)), float(std::cos(yaw) * c)};
}

bool isLookedAtBy(Actor& self, Player& viewer, double tolerance, std::vector<double> const& ys) {
    Vec3 view = viewVector(viewer);
    Vec3 eye  = viewer.getEyePos();
    auto pos  = self.getFeetPos();
    for (double y : ys) {
        double dx = pos.x - viewer.getFeetPos().x, dy = y - eye.y, dz = pos.z - viewer.getFeetPos().z;
        double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-9) continue;
        double dot = (view.x * dx + view.y * dy + view.z * dz) / len;
        if (dot <= 1.0 - tolerance) continue;
        Vec3 from{pos.x, float(y), pos.z};
        Vec3 to{viewer.getFeetPos().x, eye.y, viewer.getFeetPos().z};
        if (distSqr(from, to) > 128.0 * 128.0) continue;
        if (clearPath(self.getDimensionBlockSource(), from, to)) return true;
    }
    return false;
}

// ---- mobs ------------------------------------------------------------------------------------------

namespace {
NavigationComponent* navOf(Mob& mob) {
    auto nav = mob.getEntityContext().tryGetComponent<NavigationComponent>();
    return nav ? nav.as_ptr() : nullptr;
}
} // namespace

bool moveTo(Mob& mob, Vec3 const& pos, double speed) {
    auto* nav = navOf(mob);
    if (!nav || !nav->mNavigation.get()) return false;
    return nav->mNavigation.get()->moveTo(*nav, mob, pos, static_cast<float>(speed));
}

void stopNavigation(Mob& mob) {
    if (auto* nav = navOf(mob); nav && nav->mNavigation.get()) nav->mNavigation.get()->stop(*nav, mob);
}

bool navigationDone(Mob& mob) {
    auto* nav = navOf(mob);
    if (!nav) return true;
    auto const& path = nav->mPath.get();
    return !path || path->mIndex >= path->mNodes->size();
}

void lookAt(Mob& mob, Actor& target, float yMax, float xMax) {
    auto look = mob.getEntityContext().tryGetComponent<LookControlComponent>();
    if (!look) return;
    look->setLookAtPosition(&target, yMax, xMax);
}

void lookAt(Mob& mob, Vec3 const& pos, float yMax, float xMax) {
    auto look = mob.getEntityContext().tryGetComponent<LookControlComponent>();
    if (!look) return;
    look->mWantedPosition    = pos;
    look->mHasWantedPosition = true;
    look->mYMax              = yMax;
    look->mXMax              = xMax;
}

bool withinMeleeRange(Mob& mob, Actor& target) {
    static double const reach = std::sqrt(2.04F) - 0.6F; // Mob#DEFAULT_ATTACK_REACH
    auto const&         a     = mob.getAABB();
    auto const&         b     = target.getAABB();
    return a.min.x - reach < b.max.x && a.max.x + reach > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y
        && a.min.z - reach < b.max.z && a.max.z + reach > b.min.z;
}

bool doHurtTarget(Mob& mob, Actor& target) {
    return static_cast<bool>(mob.doHurtTarget(&target, SharedTypes::Legacy::ActorDamageCause::EntityAttack));
}

void swing(Mob& mob) { mob.swing(ActorSwingSource::Attack, HandSlot::Mainhand); }

void setMainHand(Mob& mob, std::string const& itemId) {
    if (itemId.empty()) {
        mob.setCarriedItem(game::items::empty());
        return;
    }
    if (auto s = game::items::make(itemId)) mob.setCarriedItem(*s);
    else mob.setCarriedItem(game::items::empty());
}

std::string mainHandId(Mob& mob) {
    auto const& s = mob.getCarriedItem();
    return game::items::isEmpty(s) ? std::string() : game::items::idOf(s);
}

void heal(Mob& mob, float amount) {
    int n = static_cast<int>(std::lround(amount));
    if (amount > 0 && n < 1) n = 1;
    if (n > 0) mob.heal(n);
}

float health(Actor const& actor) { return static_cast<float>(actor.getHealth()); }
float maxHealth(Actor const& actor) { return static_cast<float>(actor.getMaxHealth()); }

void teleportTo(Actor& actor, Vec3 const& pos) { actor.teleportTo(pos, true, 0, 1, false); }

void setMotion(Actor& actor, Vec3 const& v) { const_cast<Vec3&>(actor.getPosDelta()) = v; }

Mob* spawnMob(BlockSource& region, std::string const& id, Vec3 const& pos, float yaw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto* mob = level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(id), nullptr, pos, false, false, true);
    if (!mob) return nullptr;
    mob->setYBodyRotation(yaw);
    const_cast<Vec2&>(mob->getRotation()) = Vec2{0.0f, yaw};
    return mob;
}

void discard(Actor& actor) { actor.remove(); }

void discardLater(Actor& actor) {
    // never inside the actor's own tick: the next server tick removes it
    gf::feature::common::interactions::nextTick([uid = uidOf(actor)] {
        if (auto* a = actorById(uid)) a->remove();
    });
}

void pruneRandoms() {
    for (auto it = randoms().begin(); it != randoms().end();) it = actorById(it->first) ? std::next(it) : randoms().erase(it);
}

void forgetAllRandoms() { randoms().clear(); }

namespace {
std::atomic<std::thread::id> gServerThread{};
}
void noteServerThread() { gServerThread.store(std::this_thread::get_id(), std::memory_order_relaxed); }
bool onServerThread() { return gServerThread.load(std::memory_order_relaxed) == std::this_thread::get_id(); }
std::optional<Vec3> randomTeleportSpot(BlockSource& region, Vec3 const& at, float width, float height) {
    // LivingEntity#randomTeleport(x, y, z, false): down to the first motion-blocking block below
    BlockPos p{int(std::floor(at.x)), int(std::floor(at.y)), int(std::floor(at.z))};
    if (!region.hasChunksAt(p, 1, false)) return std::nullopt;
    bool   found = false;
    double y     = at.y; // Java's d3: the y keeps its fraction, one block lower per step down
    while (!found && p.y > region.getMinHeight()) {
        BlockPos below{p.x, p.y - 1, p.z};
        Block const& b = region.getBlock(below);
        AABB         c = b.getBlockType().getCollisionShape(b, region, below, nullptr);
        if (!b.isAir() && c.max.y > c.min.y) found = true; // BlockState#blocksMotion
        else {
            p.y--;
            y -= 1.0;
        }
    }
    if (!found) return std::nullopt;
    Vec3  spot{at.x, float(y), at.z};
    float hw = width / 2.0f;
    AABB  box{Vec3{spot.x - hw, spot.y, spot.z - hw}, Vec3{spot.x + hw, spot.y + height, spot.z + hw}};
    std::vector<AABB> shapes;
    region.fetchAABBs(shapes, box, false);
    for (auto const& s : shapes)
        if (s.min.x < box.max.x && s.max.x > box.min.x && s.min.y < box.max.y && s.max.y > box.min.y && s.min.z < box.max.z && s.max.z > box.min.z)
            return std::nullopt;
    if (region.containsAnyLiquid(box)) return std::nullopt;
    return spot;
}

// ---- effects ---------------------------------------------------------------------------------------

void playSound(Actor& actor, std::string const& event, float volume, float pitch) {
    gf::fx::sound(actor.getDimension(), event, actor.getPosition(), volume, pitch);
}

void playSoundAt(Actor& at, Vec3 const& pos, std::string const& event, float volume, float pitch) {
    gf::fx::sound(at.getDimension(), event, pos, volume, pitch);
}

void particle(Actor& in, std::string const& id, Vec3 const& pos, Vec3 const& velocity, Json const& params) {
    gf::fx::particle(in.getDimension(), id, pos, velocity, 1, params);
}

float voicePitch(Actor& actor) {
    auto& r = randomOf(actor);
    return actor.isBaby() ? (r.nextFloat() - r.nextFloat()) * 0.2F + 1.5F : (r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F;
}

bool bypassesInvulnerability(std::string const& damageType) {
    return damageType == "minecraft:out_of_world" || damageType == "minecraft:generic_kill"
        || gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_invulnerability", damageType);
}

bool damageTypeIn(std::string const& tag, std::string const& damageType) {
    return gf::tags::has(gf::tags::Registry::DamageType, tag, damageType);
}

bool entityTypeIn(std::string const& tag, std::string const& entityId) {
    return gf::tags::has(gf::tags::Registry::EntityType, tag, entityId);
}

} // namespace gf::feature::lich

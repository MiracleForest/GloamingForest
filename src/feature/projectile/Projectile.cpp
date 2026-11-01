// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/projectile/TFThrowable.java
//   src/main/java/twilightforest/entity/projectile/TFArrow.java
//   src/main/java/twilightforest/entity/projectile/ITFProjectile.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Contract "projectile" (F8): TF's throwables and arrows are BP entities without engine physics
// (`minecraft:physics` without gravity / collision); the server simulates them the way
// ThrowableProjectile#tick (TFThrowable) and AbstractArrow#tick (TFArrow) do:
//   - the hit test runs first, along this tick's motion (ProjectileUtil#getHitResultOnMoveVector:
//     blocks by their collision shape, entities with a 0.3 margin, the owner only after the
//     projectile left it), then the move, then drag (0.99, 0.8 in water) and gravity;
//   - Spec::onHitEntity / onHitBlock decide whether the projectile is gone (no callback: gone);
//     a block hit that keeps the projectile sticks it in the ground like an arrow (despawns after
//     1200 ticks, AbstractArrow#tickDespawn);
//   - steering functions (seeker arrows, moonworm shots) may replace the velocity every tick.
// The entity is moved every tick (clients interpolate); its state is saved with the actor and the
// flight resumes after a reload.

#include "feature/projectile/Projectile.h"
#include "feature/projectile/ProjectileInternal.h"

#include <cmath>
#include <numbers>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::projectile {

namespace fe = gf::feature::entity;
using gf::projectile::Spec;

namespace {

constexpr double kEntityMargin  = 0.3;  ///< ProjectileUtil.DEFAULT_ENTITY_HIT_RESULT_MARGIN
constexpr int    kInGroundLimit = 1200; ///< AbstractArrow#tickDespawn
constexpr char const* kDataKey = "entity:projectile";

struct Sim {
    std::string  type;
    Vec3         vel{};
    std::int64_t owner     = 0;
    bool         leftOwner = false;
    bool         inGround  = false;
    int          inGroundTicks = 0;
    int          age           = 0;
};

struct State {
    std::unordered_map<std::string, Spec>       specs;
    std::vector<gf::projectile::SteeringFn>     steering;
    std::unordered_map<std::int64_t, Sim>       live;
    std::vector<std::int64_t>                   toRemove;
};
State& S() {
    static State s;
    return s;
}

Spec const& specOf(std::string const& type) {
    static Spec const fallback{};
    auto              it = S().specs.find(type);
    return it != S().specs.end() ? it->second : fallback;
}

Vec2 rotationOf(Vec3 const& v) {
    double horizontal = std::sqrt(v.x * v.x + v.z * v.z);
    auto   yaw        = static_cast<float>(-std::atan2(v.x, v.z) * 180.0 / std::numbers::pi);
    auto   pitch      = static_cast<float>(-std::atan2(v.y, horizontal) * 180.0 / std::numbers::pi);
    return Vec2{pitch, yaw};
}

nlohmann::json toJson(Sim const& s) {
    return {
        {"type",     s.type         },
        {"vel",      {s.vel.x, s.vel.y, s.vel.z}},
        {"owner",    s.owner        },
        {"left",     s.leftOwner    },
        {"inGround", s.inGround     },
        {"ground",   s.inGroundTicks},
        {"age",      s.age          },
    };
}

Sim fromJson(nlohmann::json const& j, std::string const& type) {
    Sim s;
    s.type = j.value("type", type);
    if (auto v = j.find("vel"); v != j.end() && v->is_array() && v->size() == 3)
        s.vel = Vec3{(*v)[0].get<float>(), (*v)[1].get<float>(), (*v)[2].get<float>()};
    s.owner         = j.value("owner", std::int64_t{0});
    s.leftOwner     = j.value("left", false);
    s.inGround      = j.value("inGround", false);
    s.inGroundTicks = j.value("ground", 0);
    s.age           = j.value("age", 0);
    return s;
}

/// Ray / box slab test: the parameter of the first hit of segment a -> b with the box (0..1).
std::optional<double> segmentBox(Vec3 const& a, Vec3 const& b, AABB const& box, double margin) {
    double t0 = 0, t1 = 1;
    double av[3] = {a.x, a.y, a.z}, bv[3] = {b.x, b.y, b.z};
    double lo[3] = {box.min.x - margin, box.min.y - margin, box.min.z - margin};
    double hi[3] = {box.max.x + margin, box.max.y + margin, box.max.z + margin};
    for (int i = 0; i < 3; ++i) {
        double d = bv[i] - av[i];
        if (std::abs(d) < 1e-9) {
            if (av[i] < lo[i] || av[i] > hi[i]) return std::nullopt;
            continue;
        }
        double u = (lo[i] - av[i]) / d, v = (hi[i] - av[i]) / d;
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u);
        t1 = std::min(t1, v);
        if (t0 > t1) return std::nullopt;
    }
    return t0;
}

bool intersects(AABB const& a, AABB const& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z && a.max.z > b.min.z;
}

/// Projectile#canHitEntity (living things, players, multipart parts; never another projectile).
bool canHit(Actor& projectile, Sim const& s, Actor& target) {
    if (&target == &projectile || !target.isAlive()) return false;
    if (target.isPlayer() && static_cast<Player&>(target).isSpectator()) return false;
    if (S().live.contains(fe::uidOf(target))) return false;
    bool pickable = target.hasCategory(ActorCategory::Mob) || target.isPlayer() || fe::parentOfPart(target) != 0;
    if (!pickable) return false;
    if (!s.leftOwner && s.owner && fe::uidOf(target) == s.owner) return false;
    return true;
}

void markRemoved(Actor& projectile) {
    S().toRemove.push_back(fe::uidOf(projectile));
    S().live.erase(fe::uidOf(projectile));
}

/// ThrowableProjectile#tick / AbstractArrow#tick, server side.
void tickProjectile(Actor& p, fe::ActorState& state) {
    auto& live = S().live;
    auto  uid  = fe::uidOf(p);
    auto  it   = live.find(uid);
    if (it == live.end()) {
        // a projectile loaded from the world (or summoned): resume from the saved state
        Sim s;
        if (state.data.is_object() && state.data.contains(kDataKey)) s = fromJson(state.data[kDataKey], p.getTypeName());
        else {
            s.type = p.getTypeName();
            s.vel  = p.getPosDelta();
        }
        it = live.emplace(uid, std::move(s)).first;
    }
    Sim&        s    = it->second;
    Spec const& spec = specOf(s.type);
    if (spec.tick) spec.tick(p);
    if (!p.isAlive() || !live.contains(uid)) return;
    auto& region = p.getDimensionBlockSource();
    Vec3  pos    = p.getPosition();
    if (s.inGround) {
        if (++s.inGroundTicks >= kInGroundLimit) markRemoved(p);
        return;
    }
    ++s.age;
    for (auto const& steer : S().steering)
        if (steer)
            if (auto v = steer(p, s.type)) s.vel = *v;
    // Projectile#checkLeftOwner
    if (!s.leftOwner) {
        auto* owner = fe::actorById(s.owner);
        if (!owner) s.leftOwner = true;
        else {
            auto const& bb = p.getAABB();
            AABB        grown{
                Vec3{std::min(bb.min.x, bb.min.x + s.vel.x) - 1.0f, std::min(bb.min.y, bb.min.y + s.vel.y) - 1.0f, std::min(bb.min.z, bb.min.z + s.vel.z) - 1.0f},
                Vec3{std::max(bb.max.x, bb.max.x + s.vel.x) + 1.0f, std::max(bb.max.y, bb.max.y + s.vel.y) + 1.0f, std::max(bb.max.z, bb.max.z + s.vel.z) + 1.0f}
            };
            if (!intersects(grown, owner->getAABB())) s.leftOwner = true;
        }
    }
    // the hit test along this tick's motion (blocks, then entities in front of the block hit)
    Vec3      a = pos;
    Vec3      b{pos.x + s.vel.x, pos.y + s.vel.y, pos.z + s.vel.z};
    HitResult hr = region.clip(a, b, false, ShapeType::Collision, 64, false, false, nullptr, [](BlockSource const&, Block const&, bool) { return true; }, false);
    double    len = std::sqrt(s.vel.x * s.vel.x + s.vel.y * s.vel.y + s.vel.z * s.vel.z);
    double    tBlock = 2;
    if (hr.mType == HitResultType::Tile) {
        double dx = hr.mPos.x - a.x, dy = hr.mPos.y - a.y, dz = hr.mPos.z - a.z;
        tBlock    = len > 1e-9 ? std::sqrt(dx * dx + dy * dy + dz * dz) / len : 0;
        b         = hr.mPos;
    }
    Actor* best  = nullptr;
    double tBest = 2;
    {
        auto const& bb = p.getAABB();
        AABB        search{
            Vec3{std::min(bb.min.x, bb.min.x + s.vel.x) - 1.0f, std::min(bb.min.y, bb.min.y + s.vel.y) - 1.0f, std::min(bb.min.z, bb.min.z + s.vel.z) - 1.0f},
            Vec3{std::max(bb.max.x, bb.max.x + s.vel.x) + 1.0f, std::max(bb.max.y, bb.max.y + s.vel.y) + 1.0f, std::max(bb.max.z, bb.max.z + s.vel.z) + 1.0f}
        };
        Vec3 full{pos.x + s.vel.x, pos.y + s.vel.y, pos.z + s.vel.z};
        for (auto* e : region.getEntities(search, 0.0f)) {
            if (!e || !canHit(p, s, *e)) continue;
            auto t = segmentBox(a, full, e->getAABB(), kEntityMargin);
            if (t && *t < tBest && *t <= tBlock) tBest = *t, best = e;
        }
    }
    bool gone = false;
    if (best) {
        gone = spec.onHitEntity ? spec.onHitEntity(p, *best) : true;
    } else if (hr.mType == HitResultType::Tile) {
        if (spec.onHitBlock) {
            gone = spec.onHitBlock(p, hr.mBlock, hr.mFacing);
            if (!gone && live.contains(uid)) {
                // AbstractArrow: stuck in the block
                s.inGround = true;
                s.vel      = Vec3{0, 0, 0};
                p.moveTo(hr.mPos, p.getRotation());
                return;
            }
        } else gone = true;
    }
    if (gone || !live.contains(uid)) {
        if (live.contains(uid)) markRemoved(p);
        return;
    }
    // move, then drag and gravity
    Vec3  next{pos.x + s.vel.x, pos.y + s.vel.y, pos.z + s.vel.z};
    float f = p.isInWater() ? 0.8f : spec.drag;
    s.vel   = Vec3{s.vel.x * f, s.vel.y * f - spec.gravity, s.vel.z * f};
    if (next.y < region.getMinHeight() - 64) {
        markRemoved(p); // Entity#onBelowWorld
        return;
    }
    p.moveTo(next, rotationOf(Vec3{next.x - pos.x, next.y - pos.y, next.z - pos.z}));
}

} // namespace

} // namespace gf::feature::projectile

namespace gf::feature::projectile {
void unregisterTypeForTest(std::string const& entityId) { S().specs.erase(entityId); }
int  liveCount() { return static_cast<int>(S().live.size()); }
} // namespace gf::feature::projectile

namespace gf::projectile {

namespace fp = gf::feature::projectile;
namespace fe = gf::feature::entity;

void registerType(std::string const& entityId, Spec spec) { fp::S().specs[entityId] = std::move(spec); }

Actor* shoot(Actor& shooter, std::string const& entityId, Vec3 const& direction, float speed, float inaccuracy, nlohmann::json const& init) {
    auto  level  = ll::service::getLevel();
    auto& region = shooter.getDimensionBlockSource();
    if (!level) return nullptr;
    auto feet = shooter.getFeetPos();
    Vec3 pos{feet.x, shooter.getEyePos().y - 0.1f, feet.z};
    // Projectile#getMovementToShoot: normalize, a triangle spread, scale
    double len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len < 1e-9) return nullptr;
    fe::JavaRandom r(fe::freshSeed());
    double         spread = 0.0172275 * inaccuracy;
    double         vx     = (direction.x / len + r.triangle(0.0, spread)) * speed;
    double         vy     = (direction.y / len + r.triangle(0.0, spread)) * speed;
    double         vz     = (direction.z / len + r.triangle(0.0, spread)) * speed;
    Vec3           vel{float(vx), float(vy), float(vz)};
    auto owner = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(entityId), &shooter, pos, fp::rotationOf(vel));
    if (!owner) {
        log().warn("projectile: unknown projectile entity {}", entityId);
        return nullptr;
    }
    auto* added = level->addEntity(region, std::move(owner));
    if (!added) return nullptr;
    fp::Sim s;
    s.type  = entityId;
    s.vel   = vel;
    s.owner = fe::uidOf(shooter);
    fp::S().live[fe::uidOf(*added)] = s;
    if (!init.is_null()) gf::entity::data(*added, "init") = init;
    return added;
}

void addSteering(SteeringFn fn) {
    if (fn) fp::S().steering.push_back(std::move(fn));
}

namespace ext {

bool isProjectile(Actor const& actor) { return fp::S().live.contains(fe::uidOf(actor)) || fp::S().specs.contains(actor.getTypeName()); }

Actor* owner(Actor const& projectile) {
    auto it = fp::S().live.find(fe::uidOf(projectile));
    return it != fp::S().live.end() ? fe::actorById(it->second.owner) : nullptr;
}

Vec3 velocity(Actor const& projectile) {
    auto it = fp::S().live.find(fe::uidOf(projectile));
    return it != fp::S().live.end() ? it->second.vel : Vec3{0, 0, 0};
}

void setVelocity(Actor& projectile, Vec3 const& v) {
    auto it = fp::S().live.find(fe::uidOf(projectile));
    if (it == fp::S().live.end()) return;
    it->second.vel      = v;
    it->second.inGround = false;
}

} // namespace ext

} // namespace gf::projectile

namespace gf::feature::projectile {
namespace {

class ProjectileModule {
public:
    void install() {
        static bool once = false;
        if (std::exchange(once, true)) return;
        fe::addInternalHooks({
            .interesting = [](std::string const& type) { return S().specs.contains(type); },
            .tick =
                [](Actor& actor, fe::ActorState& state) {
                    if (S().specs.contains(state.typeId) || S().live.contains(fe::uidOf(actor))) tickProjectile(actor, state);
                },
            .persist =
                [](Actor const& actor, fe::ActorState& state) {
                    auto it = S().live.find(fe::uidOf(actor));
                    if (it != S().live.end()) state.data[kDataKey] = toJson(it->second);
                },
        });
        fe::onLevelTick([] {
            auto& s = S();
            // projectiles that unloaded with their chunk resume from their save when they load again
            if (fe::currentTick() % 200 == 0) std::erase_if(s.live, [](auto const& kv) { return !fe::actorById(kv.first); });
            if (s.toRemove.empty()) return;
            auto ids = std::move(s.toRemove);
            s.toRemove.clear();
            for (auto id : ids)
                if (auto* a = fe::actorById(id)) a->remove();
        });
        game::db::onWorldClosed([] {
            S().live.clear();
            S().toRemove.clear();
        });
    }
    void uninstall() {}
};

} // namespace
} // namespace gf::feature::projectile

GF_REGISTER_MODULE_ORDERED(gf::feature::projectile::ProjectileModule, gf::ModuleOrder::Contracts + 1);

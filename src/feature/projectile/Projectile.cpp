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
//     projectile left it), then the move, then drag (0.99; in water 0.8, arrows 0.6) and gravity;
//   - throwables: Spec::onHitEntity / onHitBlock decide whether the projectile is gone (no
//     callback: gone); a block hit that keeps the projectile sticks it in the ground;
//   - arrows (ext::registerArrow, TFArrow over AbstractArrow): an entity hit deals
//     ceil(speed * baseDamage) (+ the crit bonus) of minecraft:arrow by the owner and removes the
//     arrow, or deflects it when the hit did not land; a block hit sticks it (shake 7, despawn
//     after 1200 ticks, falling again when its block goes); a player touching a stuck arrow picks
//     it up by its pickup rule; the Spec hooks run after as the subclass's additions;
//   - steering functions (seeker arrows, moonworm shots) may replace the velocity every tick.
// The entity is moved every tick (clients interpolate); its state is saved with the actor and the
// flight resumes after a reload.

#include "feature/projectile/Projectile.h"
#include "feature/projectile/ProjectileInternal.h"

#include <climits>
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
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/combat/Combat.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::projectile {

namespace fe = gf::feature::entity;
namespace pe = gf::projectile::ext;
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
    int          inGroundTicks = 0; ///< AbstractArrow#life while in the ground
    int          age           = 0;
    // AbstractArrow
    double       baseDamage = 2.0;
    bool         crit       = false;
    int          pickup     = 0; ///< pe::Pickup
    int          shake      = 0;
    double       knockback  = 0.0; ///< the firing weapon's knockback (punch)
    std::string  groundBlock;       ///< AbstractArrow#lastState (the full state of the block it is stuck in)
    nlohmann::json pickupStack;     ///< AbstractArrow#pickupItemStack (null: ArrowSpec::pickupItem)
    std::uint32_t  groundNetworkId = 0; ///< the block permutation of groundBlock this session (0: unknown; not saved)
};

/// A block's full state (id and every state value) as AbstractArrow#lastState compares it: an
/// opened door or trapdoor is another state.
std::string fullState(Block const& block) {
    std::string out = game::blockId(block);
    for (auto const& [name, value] : game::statesOf(block)) out += "|" + name + "=" + value;
    return out;
}

struct State {
    std::unordered_map<std::string, Spec>         specs;
    std::unordered_map<std::string, pe::ArrowSpec> arrows;
    std::vector<gf::projectile::SteeringFn>       steering;
    std::unordered_map<std::int64_t, Sim>         live;
    std::vector<std::int64_t>                     toRemove;
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

pe::ArrowSpec const* arrowOf(std::string const& type) {
    auto it = S().arrows.find(type);
    return it != S().arrows.end() ? &it->second : nullptr;
}

Vec2 rotationOf(Vec3 const& v) {
    double horizontal = std::sqrt(v.x * v.x + v.z * v.z);
    auto   yaw        = static_cast<float>(-std::atan2(v.x, v.z) * 180.0 / std::numbers::pi);
    auto   pitch      = static_cast<float>(-std::atan2(v.y, horizontal) * 180.0 / std::numbers::pi);
    return Vec2{pitch, yaw};
}

double lengthOf(Vec3 const& v) { return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z); }

nlohmann::json toJson(Sim const& s) {
    return {
        {"type",      s.type         },
        {"vel",       {s.vel.x, s.vel.y, s.vel.z}},
        {"owner",     s.owner        },
        {"left",      s.leftOwner    },
        {"inGround",  s.inGround     },
        {"ground",    s.inGroundTicks},
        {"age",       s.age          },
        {"damage",    s.baseDamage   },
        {"crit",      s.crit         },
        {"pickup",    s.pickup       },
        {"shake",     s.shake        },
        {"knockback", s.knockback    },
        {"block",     s.groundBlock  },
        {"pickupStack", s.pickupStack},
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
    s.baseDamage    = j.value("damage", 2.0);
    s.crit          = j.value("crit", false);
    s.pickup        = j.value("pickup", 0);
    s.shake         = j.value("shake", 0);
    s.knockback     = j.value("knockback", 0.0);
    s.groundBlock   = j.value("block", std::string());
    s.pickupStack   = j.value("pickupStack", nlohmann::json());
    return s;
}

/// AbstractArrow#getPickupItem: the stack the arrow was fired with, else the type's default item.
std::optional<ItemStack> pickupItemOf(Sim const& s, pe::ArrowSpec const& arrow) {
    if (s.pickupStack.is_object()) {
        auto stack = game::items::fromJson(s.pickupStack);
        if (!game::items::isEmpty(stack)) return game::items::withCount(stack, 1);
    }
    return game::items::make(arrow.pickupItem, 1);
}

/// Entity#getRootVehicle.
Actor const* rootVehicle(Actor const& actor) {
    Actor const* root = &actor;
    for (int i = 0; i < 16; ++i) {
        auto* v = root->getVehicle();
        if (!v) break;
        root = v;
    }
    return root;
}

/// Player#canHarmPlayer: Bedrock has no teams; players harm players when the pvp gamerule allows.
bool canHarmPlayer() {
    auto level = ll::service::getLevel();
    if (!level) return true;
    GameRuleId pvp;
    pvp.mValue = static_cast<int>(GameRules::GameRulesIndex::Pvp);
    return level->getGameRules().getBool(pvp, true);
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

/// Projectile#canHitEntity (living things, players, multipart parts; never another projectile):
/// until the projectile left its owner, nothing of the owner's vehicle stack
/// (Entity#isPassengerOfSameVehicle: the owner itself, its mount, the mount's other riders); an
/// arrow (AbstractArrow#tick) passes through players its player owner may not harm.
bool canHit(Actor& projectile, Sim const& s, Actor& target) {
    if (&target == &projectile || !target.isAlive()) return false;
    if (target.isPlayer() && static_cast<Player&>(target).isSpectator()) return false;
    if (S().live.contains(fe::uidOf(target))) return false;
    bool pickable = target.hasCategory(ActorCategory::Mob) || target.isPlayer() || fe::parentOfPart(target) != 0;
    if (!pickable) return false;
    Actor* owner = s.owner ? fe::actorById(s.owner) : nullptr;
    if (!s.leftOwner && owner && rootVehicle(*owner) == rootVehicle(target)) return false;
    if (arrowOf(s.type) && owner && owner->isPlayer() && target.isPlayer() && owner != &target && !canHarmPlayer()) return false;
    return true;
}

void markRemoved(Actor& projectile) {
    S().toRemove.push_back(fe::uidOf(projectile));
    S().live.erase(fe::uidOf(projectile));
}

/// The arrow's hit sound pitch: 1.2 / (nextFloat() * 0.2 + 0.9).
float hitPitch(Actor& p) { return 1.2F / (fe::randomOf(p).nextFloat() * 0.2F + 0.9F); }

/// AbstractArrow#onHitEntity. True: the arrow is gone.
bool arrowHitEntity(Actor& p, Sim& s, pe::ArrowSpec const& arrow, Actor& target) {
    if (arrow.beforeHitEntity) arrow.beforeHitEntity(p, target);
    auto&  random = fe::randomOf(p);
    double speed  = lengthOf(s.vel);
    int    dmg    = static_cast<int>(std::ceil(std::clamp(speed * s.baseDamage, 0.0, 2.147483647E9)));
    if (s.crit) {
        long long k = random.nextInt(dmg / 2 + 2);
        dmg         = static_cast<int>(std::min<long long>(k + dmg, INT_MAX));
    }
    Actor* owner     = fe::actorById(s.owner);
    bool   enderman  = target.getTypeName() == "minecraft:enderman";
    if (p.isOnFire() && !enderman) target.setOnFire(5, true);
    bool   landed    = gf::combat::hurt(target, static_cast<float>(dmg), "minecraft:arrow", owner ? owner : &p, &p);
    if (landed) {
        if (enderman) return false;
        if (s.knockback > 0.0) {
            // AbstractArrow#doKnockback
            double resist = 0.0;
            auto   attr   = target.getAttribute(SharedAttributes::KNOCKBACK_RESISTANCE());
            if (attr.mPtr) resist = attr.mPtr->mCurrentValue;
            double h = std::sqrt(double(s.vel.x) * s.vel.x + double(s.vel.z) * s.vel.z);
            if (h > 1e-9) {
                double f = s.knockback * 0.6 * std::max(0.0, 1.0 - resist) / h;
                fe::push(target, Vec3{float(s.vel.x * f), 0.1f, float(s.vel.z * f)});
            }
        }
        // ClientboundGameEventPacket.ARROW_HIT_PLAYER: the shooter hears its hit on a player
        if (target.isPlayer() && owner && owner != &target && owner->isPlayer())
            gf::fx::soundTo(static_cast<Player&>(*owner), "minecraft:entity.arrow.hit_player", 0.18F, 0.45F);
        gf::fx::sound(p.getDimension(), arrow.hitSound, p.getPosition(), 1.0F, hitPitch(p));
        return true;
    }
    // ProjectileDeflection.REVERSE, then * 0.2
    float turn = 170.0F + random.nextFloat() * 20.0F;
    s.vel      = Vec3{s.vel.x * -0.1f, s.vel.y * -0.1f, s.vel.z * -0.1f};
    auto rot   = p.getRotation();
    const_cast<Vec2&>(p.getRotation()) = Vec2{rot.x, rot.y + turn};
    if (double(s.vel.x) * s.vel.x + double(s.vel.y) * s.vel.y + double(s.vel.z) * s.vel.z < 1.0E-7) {
        if (s.pickup == static_cast<int>(pe::Pickup::Allowed)) {
            if (auto item = pickupItemOf(s, arrow)) {
                if (auto* lv = ll::service::getLevel().as_ptr()) {
                    auto pos = p.getPosition();
                    lv->getSpawner().spawnItem(p.getDimensionBlockSource(), *item, nullptr, Vec3{pos.x, pos.y + 0.1f, pos.z}, 0);
                }
            }
        }
        return true;
    }
    return false;
}

/// AbstractArrow#playerTouch / #tryPickup for a stuck arrow.
void arrowPickup(Actor& p, Sim& s, pe::ArrowSpec const& arrow) {
    if (s.shake > 0 || s.pickup == static_cast<int>(pe::Pickup::Disallowed)) return;
    auto const& bb = p.getAABB();
    for (auto* player : fe::playersNear(static_cast<int>(p.getDimensionId()), p.getPosition(), 4.0)) {
        if (player->isSpectator()) continue;
        auto const& pb = player->getAABB();
        AABB        reach{Vec3{pb.min.x - 1.0f, pb.min.y - 0.5f, pb.min.z - 1.0f}, Vec3{pb.max.x + 1.0f, pb.max.y + 0.5f, pb.max.z + 1.0f}};
        if (!intersects(reach, bb)) continue;
        bool taken = false;
        if (s.pickup == static_cast<int>(pe::Pickup::CreativeOnly)) taken = player->isCreative();
        else if (auto item = pickupItemOf(s, arrow)) {
            taken = player->add(*item);
            if (taken) player->sendInventory(false);
        }
        if (!taken) continue;
        // Player#take: the pickup sound
        gf::fx::sound(p.getDimension(), "minecraft:entity.item.pickup", p.getPosition(), 0.2F, (fe::randomOf(p).nextFloat() - fe::randomOf(p).nextFloat()) * 1.4F + 2.0F);
        log().info("projectile: {} picked up {}", player->getRealName(), s.type);
        markRemoved(p);
        return;
    }
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
            if (auto const* a = arrowOf(s.type)) s.baseDamage = a->baseDamage;
        }
        it = live.emplace(uid, std::move(s)).first;
    }
    Sim&        s     = it->second;
    Spec const& spec  = specOf(s.type);
    auto const* arrow = arrowOf(s.type);
    if (spec.tick) spec.tick(p);
    if (!p.isAlive() || !live.contains(uid)) return;
    auto& region = p.getDimensionBlockSource();
    Vec3  pos    = p.getPosition();
    if (s.shake > 0) --s.shake;
    if (s.inGround) {
        if (arrow) {
            // AbstractArrow: falls again when its block changed state (lastState != the block
            // state there: broken, or a door / trapdoor opened) and nothing holds it (shouldFall:
            // no collision around it)
            BlockPos     at    = game::blockPosOf(pos);
            Block const& there = region.getBlock(at);
            // the full state string only when the permutation differs from the one last seen
            bool changed = false;
            if (s.groundNetworkId == 0 || there.mNetworkId != s.groundNetworkId) {
                changed = fullState(there) != s.groundBlock;
                if (!changed) s.groundNetworkId = there.mNetworkId;
            }
            if (changed) {
                std::vector<AABB> shapes;
                AABB              box{Vec3{pos.x - 0.06f, pos.y - 0.06f, pos.z - 0.06f}, Vec3{pos.x + 0.06f, pos.y + 0.06f, pos.z + 0.06f}};
                region.fetchAABBs(shapes, box, false);
                bool blocked = false;
                for (auto const& sh : shapes) blocked |= intersects(sh, box);
                if (!blocked) {
                    // AbstractArrow#startFalling
                    auto& r         = fe::randomOf(p);
                    s.inGround      = false;
                    s.vel           = Vec3{s.vel.x * r.nextFloat() * 0.2f, s.vel.y * r.nextFloat() * 0.2f, s.vel.z * r.nextFloat() * 0.2f};
                    s.inGroundTicks = 0;
                }
            }
            if (s.inGround) {
                if (++s.inGroundTicks >= kInGroundLimit) markRemoved(p);
                else arrowPickup(p, s, *arrow);
                return;
            }
        } else {
            if (++s.inGroundTicks >= kInGroundLimit) markRemoved(p);
            return;
        }
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
    double    len = lengthOf(s.vel);
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
        for (auto* e : fe::actorsIn(region, static_cast<int>(p.getDimensionId()), search)) {
            if (!e || !canHit(p, s, *e)) continue;
            auto t = segmentBox(a, full, e->getAABB(), kEntityMargin);
            if (t && *t < tBest && *t <= tBlock) tBest = *t, best = e;
        }
    }
    bool gone = false;
    if (best) {
        if (arrow) {
            gone = arrowHitEntity(p, s, *arrow, *best);
            if (spec.onHitEntity) spec.onHitEntity(p, *best); // the subclass's additions (IceArrow's chill)
        } else {
            gone = spec.onHitEntity ? spec.onHitEntity(p, *best) : true;
        }
    } else if (hr.mType == HitResultType::Tile) {
        if (arrow) {
            // AbstractArrow#onHitBlock: stuck, 0.05 back along the motion
            if (spec.onHitBlock) spec.onHitBlock(p, hr.mBlock, hr.mFacing);
            if (!live.contains(uid)) return;
            Vec3   d{hr.mPos.x - pos.x, hr.mPos.y - pos.y, hr.mPos.z - pos.z};
            double dl = lengthOf(d);
            Vec3   back = dl > 1e-9 ? Vec3{float(d.x / dl * 0.05), float(d.y / dl * 0.05), float(d.z / dl * 0.05)} : Vec3{0, 0, 0};
            gf::fx::sound(p.getDimension(), arrow->hitSound, p.getPosition(), 1.0F, hitPitch(p));
            s.inGround      = true;
            s.inGroundTicks = 0;
            s.shake         = 7;
            s.crit          = false;
            Vec3 at{hr.mPos.x - back.x, hr.mPos.y - back.y, hr.mPos.z - back.z};
            s.groundBlock     = fullState(region.getBlock(hr.mBlock)); // lastState: the block that was hit
            s.groundNetworkId = region.getBlock(hr.mBlock).mNetworkId;
            // the rest of AbstractArrow#tick still runs on the hit vector (setDeltaMovement(hit -
            // pos)): drag (0.99, in water the water inertia) and gravity, so a falling arrow
            // (startFalling) starts from that
            float f = p.isInWater() ? arrow->waterInertia : spec.drag;
            s.vel   = Vec3{d.x * f, d.y * f - arrow->gravity, d.z * f};
            p.moveTo(at, rotationOf(d));
            return;
        }
        if (spec.onHitBlock) {
            gone = spec.onHitBlock(p, hr.mBlock, hr.mFacing);
            if (!gone && live.contains(uid)) {
                // stuck in the block
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
    // AbstractArrow: the crit trail (client side in Java)
    if (arrow && s.crit)
        for (int i = 0; i < 4; ++i)
            gf::fx::particle(p.getDimension(), "minecraft:crit", Vec3{pos.x + s.vel.x * i / 4.0f, pos.y + s.vel.y * i / 4.0f, pos.z + s.vel.z * i / 4.0f}, Vec3{-s.vel.x, -s.vel.y + 0.2f, -s.vel.z});
    // move, then drag and gravity
    Vec3  next{pos.x + s.vel.x, pos.y + s.vel.y, pos.z + s.vel.z};
    float f = p.isInWater() ? (arrow ? arrow->waterInertia : 0.8f) : spec.drag;
    float g = arrow ? arrow->gravity : spec.gravity;
    s.vel   = Vec3{s.vel.x * f, s.vel.y * f - g, s.vel.z * f};
    if (next.y < region.getMinHeight() - 64) {
        markRemoved(p); // Entity#onBelowWorld
        return;
    }
    p.moveTo(next, rotationOf(Vec3{next.x - pos.x, next.y - pos.y, next.z - pos.z}));
}

} // namespace

} // namespace gf::feature::projectile

namespace gf::feature::projectile {
void unregisterTypeForTest(std::string const& entityId) {
    S().specs.erase(entityId);
    S().arrows.erase(entityId);
    fe::invalidateInterest();
}
int liveCount() { return static_cast<int>(S().live.size()); }
} // namespace gf::feature::projectile

namespace gf::projectile {

namespace fp = gf::feature::projectile;
namespace fe = gf::feature::entity;

void registerType(std::string const& entityId, Spec spec) {
    fp::S().specs[entityId] = std::move(spec);
    fe::invalidateInterest();
}

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
    if (auto const* arrow = fp::arrowOf(entityId)) {
        s.baseDamage = arrow->baseDamage;
        // AbstractArrow(type, owner, ...): a player's arrow may be picked up
        s.pickup = static_cast<int>(shooter.isPlayer() ? ext::Pickup::Allowed : ext::Pickup::Disallowed);
        // AbstractArrow(..., pickupItemStack, firedFromWeapon): init {"pickupItem": <item JSON>} is
        // the stack the arrow was fired with (a tipped or spectral arrow comes back as itself)
        if (init.is_object())
            if (auto it = init.find("pickupItem"); it != init.end() && it->is_object()) s.pickupStack = *it;
    }
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

void registerArrow(std::string const& entityId, Spec spec, ArrowSpec arrow) {
    fp::S().specs[entityId]  = std::move(spec);
    fp::S().arrows[entityId] = std::move(arrow);
    fe::invalidateInterest();
}

namespace {
fp::Sim* simOf(Actor const& a) {
    auto it = fp::S().live.find(fe::uidOf(a));
    return it != fp::S().live.end() ? &it->second : nullptr;
}
} // namespace

void setBaseDamage(Actor& arrow, double damage) {
    if (auto* s = simOf(arrow)) s->baseDamage = damage;
}
double baseDamage(Actor const& arrow) {
    auto* s = simOf(arrow);
    return s ? s->baseDamage : 2.0;
}
void setCritArrow(Actor& arrow, bool crit) {
    if (auto* s = simOf(arrow)) s->crit = crit;
}
bool isCritArrow(Actor const& arrow) {
    auto* s = simOf(arrow);
    return s && s->crit;
}
void setPickup(Actor& arrow, Pickup pickup) {
    if (auto* s = simOf(arrow)) s->pickup = static_cast<int>(pickup);
}
Pickup pickup(Actor const& arrow) {
    auto* s = simOf(arrow);
    return s ? static_cast<Pickup>(s->pickup) : Pickup::Disallowed;
}
bool inGround(Actor const& arrow) {
    auto* s = simOf(arrow);
    return s && s->inGround;
}
void setKnockback(Actor& arrow, double knockback) {
    if (auto* s = simOf(arrow)) s->knockback = knockback;
}
void setPickupItem(Actor& arrow, ItemStack const& stack) {
    if (auto* s = simOf(arrow)) s->pickupStack = game::items::isEmpty(stack) ? nlohmann::json() : game::items::toJson(game::items::withCount(stack, 1));
}
std::optional<ItemStack> pickupItem(Actor const& arrow) {
    auto* s = simOf(arrow);
    if (!s) return std::nullopt;
    auto const* a = fp::arrowOf(s->type);
    return a ? fp::pickupItemOf(*s, *a) : std::nullopt;
}

void makeTrail(Actor& projectile, std::string const& particle, int amount, Vec3 const& rgb) {
    auto& random = fe::randomOf(projectile);
    auto  pos    = projectile.getPosition();
    for (int i = 0; i < amount; ++i) {
        double dx = pos.x + 0.5 * (random.nextDouble() - random.nextDouble());
        double dy = pos.y + 0.5 * (random.nextDouble() - random.nextDouble());
        double dz = pos.z + 0.5 * (random.nextDouble() - random.nextDouble());
        gf::fx::particle(projectile.getDimension(), particle, Vec3{float(dx), float(dy), float(dz)}, rgb);
    }
}

} // namespace ext

} // namespace gf::projectile

namespace gf::feature::projectile {
namespace {

class ProjectileModule {
public:
    // The projectile kit hooks nothing of its own: it runs inside F8's entity hooks (internal tick
    // and persist hooks) and level tick, whose registries have no removal, so those are registered
    // once per process and stay (the entity module's uninstall unhooks the engine, which stops
    // them); the world lifecycle clears the flights of a closed world.
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

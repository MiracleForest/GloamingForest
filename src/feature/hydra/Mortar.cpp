// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/boss/HydraMortar.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// gloamingforest:hydra_mortar, a ThrowableProjectile simulated by F8's projectile kit (gravity
// 0.05, drag 0.99). Its own state (fuse, blasting, on ground, the owner that a reflection changes)
// is kept in data(mortar, "hydra_mortar"). A mortar landing on a block's top stays on the ground
// and slides on with its horizontal speed (Java's ThrowableProjectile keeps moving it and never
// clears onGround): a wall it slides into, any side or bottom face it hits, or a blasting mortar's
// any block detonates it; on the ground its fuse (80) counts down. A player's hit reflects it
// along the player's look (speed 1.5, inaccuracy 0.1, fuse + 20) and makes the player its owner.

#include "feature/hydra/Hydra.h"
#include "feature/hydra/World.h"

#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/world/phys/AABB.h"

#include "core/hydra/Mortar.h"
#include "core/jrand/JavaOrder.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/projectile/Projectile.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::hydra {

namespace fe = gf::feature::hydra::world;
namespace ge = gf::entity;
namespace gp = gf::projectile;
namespace ia = gf::interactions;

namespace {

constexpr int   kFuse         = 80;
constexpr int   kBurnSeconds  = 5;  ///< BURN_FACTOR
constexpr float kDirectDamage = 18; ///< DIRECT_DAMAGE
constexpr int   kFaceUp       = 1;  ///< FacingID up
constexpr float kDegToRad     = 3.1415927F / 180.0F;

int                        gDetonations = 0;
std::vector<ia::HandlerId> gHandlers;

ge::Json& stateOf(Actor& mortar) {
    auto& d = ge::data(mortar, "hydra_mortar");
    if (!d.is_object()) d = ge::Json{{"fuse", kFuse}, {"blasting", false}, {"onGround", false}, {"stuck", false}, {"owner", 0}, {"detonated", false}};
    return d;
}

using world::mobGriefing;

Actor* ownerOf(Actor& mortar) {
    auto uid = stateOf(mortar).value("owner", std::int64_t{0});
    return uid ? fe::actorById(uid) : nullptr;
}

/// Entity#is: an upstream part "is" its parent too (HydraPart#is).
bool sameEntity(Actor& a, Actor* b) {
    if (!b) return false;
    if (&a == b) return true;
    return fe::parentOfPart(a) == fe::uidOf(*b);
}

/// The resistance in a permutation's direct data (the explosion's rays read it from there; written
/// atomically as F4's portal window does).
std::atomic_ref<float> resistanceOf(Block const& block) { return std::atomic_ref<float>(const_cast<Block&>(block).mDirectData->mExplosionResistance); }

/// HydraMortar#getBlockExplosionResistance of a blasting mortar: min(0.8, resistance) for every
/// block but #gloamingforest:common_protections, for the duration of its own explosion. The
/// permutations in reach of the blast (power 4: rays end within 7 blocks) hold the capped value
/// while Level::explode runs and get their own back afterwards.
class BlastingResistance {
public:
    BlastingResistance(BlockSource& region, Vec3 const& at) {
        BlockPos c{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)), static_cast<int>(std::floor(at.z))};
        std::unordered_set<Block const*> seen;
        for (int x = c.x - kReach; x <= c.x + kReach; ++x)
            for (int y = c.y - kReach; y <= c.y + kReach; ++y)
                for (int z = c.z - kReach; z <= c.z + kReach; ++z) {
                    BlockPos p{x, y, z};
                    if (y < region.getMinHeight() || y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) continue;
                    for (Block const* b : {&region.getBlock(p), &region.getLiquidBlock(p)}) {
                        if (!seen.insert(b).second || b->isAir()) continue;
                        if (gf::tags::has(gf::tags::Registry::Block, "gloamingforest:common_protections", game::blockId(*b))) continue;
                        float own = resistanceOf(*b).load();
                        if (own <= kCap) continue;
                        mSaved.emplace_back(b, own);
                        resistanceOf(*b).store(kCap);
                    }
                }
    }
    ~BlastingResistance() {
        for (auto it = mSaved.rbegin(); it != mSaved.rend(); ++it) resistanceOf(*it->first).store(it->second);
    }
    BlastingResistance(BlastingResistance const&)            = delete;
    BlastingResistance& operator=(BlastingResistance const&) = delete;

private:
    static constexpr int   kReach = 8;
    static constexpr float kCap   = 0.8F;
    std::vector<std::pair<Block const*, float>> mSaved;
};

/// HydraMortar#detonate.
void detonate(Actor& mortar) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return;
    st["detonated"] = true;
    ++gDetonations;
    bool  blasting = st.value("blasting", false);
    float power    = blasting ? 4.0F : 0.1F;
    bool  grief    = mobGriefing();
    auto  pos      = mortar.getPosition();
    auto& region   = mortar.getDimensionBlockSource();
    if (auto lv = ll::service::getLevel()) {
        std::optional<BlastingResistance> cap;
        if (blasting) cap.emplace(region, pos);
        lv->explode(region, &mortar, pos, power, grief, grief, std::numeric_limits<float>::max(), false);
    }
    Actor*      owner = ownerOf(mortar);
    auto const& bb    = mortar.getAABB();
    AABB        near{Vec3{bb.min.x - 1.0F, bb.min.y - 1.0F, bb.min.z - 1.0F}, Vec3{bb.max.x + 1.0F, bb.max.y + 1.0F, bb.max.z + 1.0F}};
    int         hits = 0;
    for (auto* e : fe::actorsIn(region, static_cast<int>(mortar.getDimensionId()), near)) {
        if (!e || e == &mortar || !e->isAlive()) continue;
        bool part  = fe::parentOfPart(*e) != 0;
        bool hydra = e->getTypeName() == kHydra || (part && ge::ext::parentOf(*e) && ge::ext::parentOf(*e)->getTypeName() == kHydra);
        // every entity in reach (items, vehicles, other mortars: their hurt reflects them)
        auto id = fe::uidOf(*e);
        if ((!e->isFireImmune() || hydra) && gf::combat::hurt(*e, kDirectDamage, "gloamingforest:hydra_mortar", owner, &mortar)) {
            if (!part && (e = fe::actorById(id)) && e->isAlive()) e->setOnFire(kBurnSeconds, true);
            ++hits;
        }
    }
    log().info("hydra: mortar detonated ({}, {} hit)", blasting ? "blasting" : "normal", hits);
    fe::removeLater(fe::uidOf(mortar));
}

bool onHitEntity(Actor& mortar, Actor& target) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return true;
    Actor* owner = ownerOf(mortar);
    if (!owner) return false;
    bool otherMortar = target.getTypeName() == kMortar;
    bool sameOwner   = otherMortar && sameEntity(*owner, ownerOf(target));
    bool partOfOwner = owner->getTypeName() == kHydra && fe::parentOfPart(target) == fe::uidOf(*owner);
    if ((!otherMortar || sameOwner) && !sameEntity(target, owner) && !partOfOwner) {
        detonate(mortar);
        return true;
    }
    return false;
}

bool onHitBlock(Actor& mortar, BlockPos const&, int face) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return true;
    if (!st.value("blasting", false)) {
        // if we hit a wall, explode
        if (face != kFaceUp) {
            detonate(mortar);
            return true;
        }
        // we hit the ground: y motion 0, on ground, sliding on
        auto v         = gp::ext::velocity(mortar);
        st["onGround"] = true;
        st["stuck"]    = true;
        st["slide"]    = {v.x, v.z};
        return false;
    }
    detonate(mortar);
    return true;
}

bool solidAt(BlockSource& region, BlockPos const& p) { return fe::legacySolid(region, p); }

void tick(Actor& mortar) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return;
    // HydraMortar#isOnFire is always true: the client draws it burning (displayFireAnimation); its
    // damage sensor takes no fire damage
    if (!mortar.isOnFire()) mortar.setOnFire(60, true);
    if (!st.value("onGround", false)) return;
    if (st.value("stuck", false) && st.contains("slide") && st["slide"].is_array()) {
        // ThrowableProjectile#tick on the ground: the horizontal motion goes on (0.99 drag)
        double sx = st["slide"][0].get<double>(), sz = st["slide"][1].get<double>();
        if (sx * sx + sz * sz > 1.0E-6) {
            auto&    region = mortar.getDimensionBlockSource();
            auto     p      = mortar.getPosition();
            Vec3     next{static_cast<float>(p.x + sx), p.y, static_cast<float>(p.z + sz)};
            BlockPos at{static_cast<int>(std::floor(next.x)), static_cast<int>(std::floor(next.y + 0.01)), static_cast<int>(std::floor(next.z))};
            BlockPos below{at.x, at.y - 1, at.z};
            if (solidAt(region, at)) {
                // a side face: explode
                detonate(mortar);
                return;
            }
            if (!solidAt(region, below)) {
                // off a ledge: flying again (still "on ground" for the fuse, as in Java)
                st["stuck"] = false;
                gp::ext::setVelocity(mortar, Vec3{static_cast<float>(sx), 0.0F, static_cast<float>(sz)});
            } else {
                mortar.moveTo(next, mortar.getRotation());
            }
            st["slide"] = {sx * 0.99, sz * 0.99};
        }
    }
    int fuse   = st.value("fuse", kFuse);
    st["fuse"] = fuse - 1;
    if (fuse <= 0) detonate(mortar);
}

ia::Result onAttack(ia::EntityContext const& c) {
    reflectMortar(c.entity, c.player);
    return ia::Result::Consume;
}

/// Whether a projectile hit on `mortar` would reach it in Java. The mortar's custom_hit_test is
/// its pick box (0.75 inflated by getPickRadius 1.5, for the player's crosshair and melee), which
/// Bedrock projectiles use too; a Java projectile (ProjectileUtil#getEntityHitResult) clips its
/// path against the 0.75 box inflated by 0.3 only. Bedrock stops the projectile on the pick box
/// and may report its motion before or after the move (or zeroed), so its whole line of motion
/// (or its look) is clipped (core::hydra::projectilePath).
bool projectileReachesJavaBox(Actor& mortar, Actor& projectile) {
    namespace ch    = gf::core::hydra;
    auto const& bb  = mortar.getAABB();
    ch::V3      feet{(bb.min.x + bb.max.x) / 2.0, bb.min.y, (bb.min.z + bb.max.z) / 2.0};
    // (Java's path is the projectile's position, its feet: Entity#position)
    auto const& p    = projectile.getPosition();
    auto const& dp   = projectile.getPosDelta();
    auto        look = projectile.getViewVector(1.0F);
    return ch::projectileReachesJavaBox(feet, ch::V3{p.x, p.y, p.z}, ch::V3{dp.x, dp.y, dp.z}, ch::V3{look.x, look.y, look.z});
}

/// HydraMortar#hurt for everything but a player's melee (onAttack): any damage with a causing
/// entity (DamageSource#getEntity: an arrow's or a snowball's shooter, a shooterless arrow or
/// trident itself, a mob's hit, an explosion's source) reflects the mortar along that entity's
/// look; the owner changes only for a living one. The mortar itself never takes damage.
gf::combat::Verdict onIncoming(gf::combat::DamageInfo& d) {
    if (d.victim.getTypeName() != kMortar) return gf::combat::Verdict::Pass;
    Actor* causing = d.attacker; // DamageSource#getEntity
    Actor* direct  = d.direct;   // #getDirectEntity
    // a vanilla projectile inside the pick box but outside Java's projectile box missed in Java:
    // no reflection (the Bedrock projectile still stops on the hit test)
    Actor* projectile = direct && direct != &d.victim && !fe::asMob(*direct) ? direct : (causing && !fe::asMob(*causing) ? causing : nullptr);
    if (projectile && projectile != &d.victim && projectile->getTypeName() != kMortar && fe::isProjectileDamage(d.type)
        && !projectileReachesJavaBox(d.victim, *projectile)) {
        auto const& pp = projectile->getPosition();
        auto const& pd = projectile->getPosDelta();
        auto        pl = projectile->getViewVector(1.0F);
        auto const& mb = d.victim.getAABB();
        log().info("hydra: mortar missed by {} (outside the Java projectile box; at {:.2f} {:.2f} {:.2f}, motion {:.3f} {:.3f} {:.3f}, look {:.2f} {:.2f} {:.2f}, "
                   "mortar box {:.2f} {:.2f} {:.2f})",
                   projectile->getTypeName(), pp.x, pp.y, pp.z, pd.x, pd.y, pd.z, pl.x, pl.y, pl.z, mb.min.x, mb.min.y, mb.min.z);
        return gf::combat::Verdict::Cancel;
    }
    // Explosion#getIndirectSourceEntity: an exploding mortar stands for its owner
    if (causing && causing->getTypeName() == kMortar) causing = ownerOf(*causing);
    // DamageSource#getEntity of a shooterless projectile: an arrow or a trident is its own causing
    // entity in 1.21.1 (AbstractArrow / ThrownTrident#onHitEntity: arrow(this, owner ?: this)); every
    // other projectile names its owner only (thrown, fireball, wither skull, wind charge, mob
    // projectile: null without one), as does any other non-living source (PrimedTnt's indirect
    // source is its owner)
    bool selfCaused = causing && causing == projectile && (d.type == "minecraft:arrow" || d.type == "minecraft:trident");
    if (causing && !fe::asMob(*causing) && !selfCaused) causing = nullptr;
    if (causing && causing != &d.victim) reflectMortar(d.victim, *causing, selfCaused);
    return gf::combat::Verdict::Cancel;
}

} // namespace

Actor* launchMortar(Mob& hydra, int head, Vec3 const& headPos, float headXRot, float headYRot, bool blasting) {
    // HydraMortar(type, level, head): 3.5 along the head's look, 1 up
    auto   look = core::hydra::viewVector(headXRot, headYRot);
    Vec3   pos{static_cast<float>(headPos.x + look.x * 3.5), static_cast<float>(headPos.y + 1.0 + look.y * 3.5), static_cast<float>(headPos.z + look.z * 3.5)};
    // shootFromRotation(head, xRot, yRot, -20, 0.5, 1)
    float  f  = -core::jrand::mthSin(headYRot * kDegToRad) * core::jrand::mthCos(headXRot * kDegToRad);
    float  f1 = -core::jrand::mthSin((headXRot + -20.0F) * kDegToRad);
    float  f2 = core::jrand::mthCos(headYRot * kDegToRad) * core::jrand::mthCos(headXRot * kDegToRad);
    Actor* m  = gp::shoot(hydra, kMortar, Vec3{f, f1, f2}, 0.5F, 1.0F);
    if (!m) return nullptr;
    m->moveTo(pos, Vec2{0, 0});
    auto& st       = stateOf(*m);
    st["blasting"] = blasting;
    st["owner"]    = fe::uidOf(hydra);
    st["head"]     = head;
    return m;
}

std::optional<MortarView> mortarView(Actor& mortar) {
    if (mortar.getTypeName() != kMortar) return std::nullopt;
    auto&      st = stateOf(mortar);
    MortarView v;
    v.fuse     = st.value("fuse", kFuse);
    v.onGround = st.value("onGround", false);
    v.blasting = st.value("blasting", false);
    v.owner    = st.value("owner", std::int64_t{0});
    return v;
}

void reflectMortar(Actor& mortar, Actor& attacker, bool projectile) {
    auto& st = stateOf(mortar);
    if (st.value("detonated", false)) return;
    // reflect faster and more accurately: shoot(getLookAngle, 1.5, 0.1)
    Vec3 look = attacker.getViewVector(1.0F);
    if (projectile) {
        // a shooterless arrow or trident: its Java rotation follows its motion (mirrored)
        auto const& dp = attacker.getPosDelta();
        Vec3        m  = dp.x * dp.x + dp.y * dp.y + dp.z * dp.z > 1.0E-8F ? dp : look;
        auto        jl = core::hydra::projectileLook(core::hydra::V3{m.x, m.y, m.z});
        look           = Vec3{static_cast<float>(jl.x), static_cast<float>(jl.y), static_cast<float>(jl.z)};
    }
    double         len  = std::sqrt(look.x * look.x + look.y * look.y + look.z * look.z);
    if (len < 1.0E-9) return;
    fe::JavaRandom r(fe::freshSeed());
    double         spread = 0.0172275 * 0.1;
    Vec3           v{static_cast<float>((look.x / len + r.triangle(0.0, spread)) * 1.5), static_cast<float>((look.y / len + r.triangle(0.0, spread)) * 1.5),
           static_cast<float>((look.z / len + r.triangle(0.0, spread)) * 1.5)};
    gp::ext::setVelocity(mortar, v);
    st["onGround"] = false;
    st["stuck"]    = false;
    st["fuse"]     = st.value("fuse", kFuse) + 20;
    if (fe::asMob(attacker)) st["owner"] = fe::uidOf(attacker);
    log().info("hydra: mortar reflected by {}", attacker.getTypeName());
}

int detonationsForTest() { return gDetonations; }

void installMortar() {
    gp::registerType(kMortar, {
                                  .gravity     = 0.05F, // HydraMortar#getDefaultGravity
                                  .drag        = 0.99F,
                                  .onHitEntity = onHitEntity,
                                  .onHitBlock  = onHitBlock,
                                  .tick        = tick,
                              });
    gHandlers.push_back(ia::onAttack({.block = kMortar}, onAttack, ia::priority::High));
    // combat handlers have no removal: once per process (the handler checks the victim's type)
    static bool combatHooked = false;
    if (!std::exchange(combatHooked, true)) gf::combat::onIncoming(gf::combat::priority::High, onIncoming);
}

void uninstallMortar() {
    for (auto id : gHandlers) ia::remove(id);
    gHandlers.clear();
}

} // namespace gf::feature::hydra

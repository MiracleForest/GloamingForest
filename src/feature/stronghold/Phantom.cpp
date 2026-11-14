// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/boss/KnightPhantom.java
//   src/main/java/gloamingforest/entity/ai/goal/PhantomWatchAndAttackGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/PhantomUpdateFormationAndMoveGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/PhantomAttackStartGoal.java
//   src/main/java/gloamingforest/entity/ai/goal/PhantomThrowWeaponGoal.java
//   src/main/java/gloamingforest/block/entity/spawner/KnightPhantomSpawnerBlockEntity.java
//   src/main/java/gloamingforest/entity/projectile/ThrownWep.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The six Knight Phantoms (F8 boss kit) and their thrown weapons. What the engine cannot carry is
// native: the numbering, the formations and their movement (NoClipMoveControl through F8), the
// melee with the haunt damage, the axe / pickaxe throws, the shared boss bar, the loot handed to the
// surviving knights and the last knight's conquest. The BP gives the target selection (the nearest
// player, must see false), no gravity / no block collision (noPhysics) and the charging hitbox
// (component groups); the RP shows the charging pose, scale and particles from the synced
// properties.
#include "feature/stronghold/Phantom.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <numbers>
#include <unordered_map>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/landmark/Landmark.h"
#include "feature/progression/Progression.h"
#include "feature/projectile/Projectile.h"
#include "feature/spawn/Spawn.h"
#include "feature/stronghold/Util.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::stronghold::phantom {

namespace ge = gf::entity;
namespace fe = gf::feature::entity;
using Slot   = SharedTypes::Legacy::EquipmentSlot;

namespace {

constexpr int   kDyingTicks    = 18;
constexpr int   kParticleTicks = 70;
constexpr int   kHomeRadius    = 30;
constexpr float kSmallRadius   = 2.5F;
constexpr float kLargeRadius   = 8.5F;
constexpr char  kData[]        = "knight_phantom";
constexpr char  kSword[]       = "gloamingforest:knightmetal_sword";
constexpr char  kAxe[]         = "gloamingforest:knightmetal_axe";
constexpr char  kPick[]        = "gloamingforest:knightmetal_pickaxe";

constexpr int kDuration[] = {90, 180, 90, 180, 90, 180, 180, 180, 180, 10, 50, 50};

struct Knight {
    int       number            = 0;
    int       totalKnownKnights = INT_MIN;
    int       ticksProgress     = 0;
    Formation formation         = Formation::Hover;
    BlockPos  chargePos{0, 0, 0};
    bool      charging    = false;
    bool      over        = false; ///< IT_IS_OVER
    bool      guarding    = false; ///< the shield is raised (PhantomWatchAndAttackGoal)
    bool      untouchable = false;
};

std::unordered_map<std::int64_t, Knight>& states() {
    static std::unordered_map<std::int64_t, Knight> m;
    return m;
}

Knight* find(Actor const& a) {
    auto it = states().find(fe::uidOf(a));
    return it == states().end() ? nullptr : &it->second;
}

bool isKnight(Actor const& a) { return a.getTypeName() == id::KnightPhantom; }

void persist(Actor& a, Knight const& k) {
    ge::data(a, kData) = Json{{"TotalKnownKnights", k.totalKnownKnights},
                              {"MyNumber", k.number},
                              {"Formation", static_cast<int>(k.formation)},
                              {"TicksProgress", k.ticksProgress},
                              {"IsItOver", k.over},
                              {"chargePos", {k.chargePos.x, k.chargePos.y, k.chargePos.z}}};
}

Knight& stateOf(Actor& a) {
    auto [it, fresh] = states().try_emplace(fe::uidOf(a));
    if (fresh) {
        auto const& d = ge::data(a, kData);
        if (d.is_object()) {
            auto& k             = it->second;
            k.totalKnownKnights = d.value("TotalKnownKnights", INT_MIN);
            k.number            = d.value("MyNumber", 0);
            k.formation         = static_cast<Formation>(std::clamp(d.value("Formation", 0), 0, 11));
            k.ticksProgress     = d.value("TicksProgress", 0);
            k.over              = d.value("IsItOver", false);
            if (auto c = d.find("chargePos"); c != d.end() && c->is_array() && c->size() == 3) k.chargePos = BlockPos{(*c)[0].get<int>(), (*c)[1].get<int>(), (*c)[2].get<int>()};
            k.charging = k.formation == Formation::AttackPlayerStart || k.formation == Formation::AttackPlayerAttack;
        }
    }
    return it->second;
}

int deathTimeOf(Mob& m) { return m.isAlive() ? 0 : gf::boss::ext::deathTime(m); }

/// KnightPhantom#getNearbyKnights: live knights within the box inflated by 64 (this one included when alive).
std::vector<Mob*> nearbyKnights(Actor& self) {
    std::vector<Mob*> out;
    for (auto* a : actorsAround(self, id::KnightPhantom, 64.0))
        if (auto* m = fe::asMob(*a); m && m->isAlive()) out.push_back(m);
    return out;
}

/// Every knight within 64, alive or dying.
std::vector<Mob*> allKnightsAround(Actor& self) {
    std::vector<Mob*> out;
    auto const&       b   = self.getAABB();
    int               dim = static_cast<int>(self.getDimensionId());
    auto*             region = game::regionOf(dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, dim, AABB{Vec3{b.min.x - 64, b.min.y - 64, b.min.z - 64}, Vec3{b.max.x + 64, b.max.y + 64, b.max.z + 64}}))
        if (a && isKnight(*a))
            if (auto* m = fe::asMob(*a)) out.push_back(m);
    return out;
}

void event(Mob& m, char const* name) {
    VariantParameterList params{};
    params.mSelf = &m;
    m.executeEvent(name, params);
}

void setWeapon(Mob& m, char const* item) {
    if (auto s = game::items::make(item)) m.setItemSlot(Slot::Mainhand, *s);
}

/// KnightPhantom#setNumber.
void setNumber(Mob& m, Knight& k, int number) {
    k.number = number;
    // only knight 0 shows the shared bar (setNumber(0) adds the players within 64)
    gf::boss::bar(m).setVisible(number == 0 && m.isAlive());
    // set weapon per number
    switch (number % 3) {
    case 0: setWeapon(m, kSword); break;
    case 1: setWeapon(m, kAxe); break;
    case 2: setWeapon(m, kPick); break;
    default: break;
    }
    ge::setProperty(m, "gloamingforest:number", std::clamp(number, 0, 15));
    persist(m, k);
}

/// KnightPhantom#updateMyNumber.
void updateMyNumber(Mob& m, Knight& k) {
    std::vector<int> nums;
    auto             knights = nearbyKnights(m);
    for (auto* knight : knights) {
        if (knight == &m || !knight->isAlive()) continue;
        auto& other = stateOf(*knight);
        nums.push_back(other.number);
        if (other.number == 0)
            if (auto h = ge::home(*knight)) {
                // setRestrictionPoint: the charge position follows the home
                ge::setHome(m, h->pos, kHomeRadius);
                k.chargePos = BlockPos{h->pos.x, h->pos.y, h->pos.z};
            }
    }
    if (nums.empty()) {
        setNumber(m, k, 0);
        return;
    }
    std::sort(nums.begin(), nums.end());
    int smallest       = nums[0];
    int largest        = static_cast<int>(knights.size());
    int smallestUnused = largest + 1;
    if (smallest > 0) {
        smallestUnused = 0;
    } else {
        for (int i = 1; i < largest; i++)
            if (!std::binary_search(nums.begin(), nums.end(), i)) {
                smallestUnused = i;
                break;
            }
    }
    if (k.totalKnownKnights < largest) k.totalKnownKnights = largest;
    if (k.number > smallestUnused || std::find(nums.begin(), nums.end(), k.number) != nums.end()) setNumber(m, k, smallestUnused);
    persist(m, k);
}

/// KnightPhantom#setChargingAtPlayer (the attribute modifiers are applied in the hurt / melee code).
void setCharging(Mob& m, Knight& k, bool flag) {
    bool changed = k.charging != flag;
    k.charging   = flag;
    ge::setProperty(m, "gloamingforest:charging", flag);
    // refreshDimensions: 1.75 x 4.0 while charging, else 1.25 x 2.5
    if (changed) event(m, flag ? "charging" : "not_charging");
}

/// KnightPhantom#switchToFormation.
void switchToFormation(Mob& m, Knight& k, Formation formation) {
    k.formation     = formation;
    k.ticksProgress = 0;
    updateMyNumber(m, k);
    setCharging(m, k, formation == Formation::AttackPlayerStart || formation == Formation::AttackPlayerAttack);
    ge::setProperty(m, "gloamingforest:formation", static_cast<int>(formation));
    persist(m, k);
}

bool hasYetToDisappear(Mob& m, Knight const& k) {
    if (m.isAlive()) return true;
    return deathTimeOf(m) <= kDyingTicks && (k.over || !nearbyKnights(m).empty());
}

/// The melee damage: Attributes.ATTACK_DAMAGE 1 with the held weapon's modifier (knightmetal sword
/// +6, axe +9, pickaxe +4: the tier's 3 plus the tool's) and CHARGING_MODIFIER +7 while charging.
float attackDamage(Mob& m, Knight const& k) {
    float d = 1.0F;
    if (holds(m, kSword)) d += 6.0F;
    else if (holds(m, kAxe)) d += 9.0F;
    else if (holds(m, kPick)) d += 4.0F;
    if (k.charging) d += 7.0F;
    return d;
}

/// EnforcedHomePoint#isMobWithinHomeArea.
bool withinHome(Mob& m) {
    auto h = ge::home(m);
    if (!h) return true;
    auto   p  = m.getFeetPos();
    double dx = std::floor(p.x) - h->pos.x, dy = std::floor(p.y) - h->pos.y, dz = std::floor(p.z) - h->pos.z;
    return dx * dx + dy * dy + dz * dz < static_cast<double>(h->radius) * h->radius;
}

BlockPos homePos(Mob& m) {
    if (auto h = ge::home(m)) return BlockPos{h->pos.x, h->pos.y, h->pos.z};
    // getDestination: setRestrictionPoint(blockPosition()) when there is none
    auto     p = m.getFeetPos();
    BlockPos b{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    ge::setHome(m, core::GlobalPos{static_cast<int>(m.getDimensionId()), b.x, b.y, b.z}, kHomeRadius);
    return b;
}

// ---- goals --------------------------------------------------------------------------------------------------

class KnightGoal : public ge::Goal {
public:
    explicit KnightGoal(std::int64_t uid) : mUid(uid) {}

protected:
    Mob* mob() const {
        auto* a = fe::actorById(mUid);
        return a ? fe::asMob(*a) : nullptr;
    }
    std::int64_t mUid;
};

/// PhantomWatchAndAttackGoal.
class WatchAndAttackGoal final : public KnightGoal {
public:
    using KnightGoal::KnightGoal;
    bool canUse() override {
        auto* m = mob();
        return m && fe::mobs::target(*m);
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        auto* target = fe::mobs::target(*m);
        if (!target) return;
        auto& k = stateOf(*m);
        fe::mobs::lookAt(*m, *target, 10.0F, 500.0F);
        if (!target->isAlive()) return;
        auto  tp = target->getFeetPos();
        auto  mp = m->getFeetPos();
        float f1 = static_cast<float>(std::sqrt((tp.x - mp.x) * (tp.x - mp.x) + (tp.y - mp.y) * (tp.y - mp.y) + (tp.z - mp.z) * (tp.z - mp.z)));
        if (fe::mobs::hasLineOfSight(*m, *target)) {
            auto const& tb = target->getAABB();
            auto const& mb = m->getAABB();
            if (mAttackTime-- <= 0 && f1 < 2.0F && tb.max.y > mb.min.y && tb.min.y < mb.max.y) {
                mAttackTime = 20;
                doHurtTarget(*m, *target);
            }
        }
        bool shield = game::items::idOf(m->getOffhandSlot()) == "gloamingforest:knightmetal_shield";
        bool raise  = shield && k.formation != Formation::AttackPlayerAttack && mIsGuard;
        if (raise != k.guarding) {
            k.guarding = raise;
            ge::setProperty(*m, "gloamingforest:guarding", raise);
        }
        if (mIsGuard) {
            if (mGuardCoolDownTime <= 180) ++mGuardCoolDownTime;
            else mIsGuard = false;
        } else {
            if (mGuardCoolDownTime > 0) --mGuardCoolDownTime;
            else mIsGuard = true;
        }
    }

private:
    int  mAttackTime        = 0;
    int  mGuardCoolDownTime = 0;
    bool mIsGuard           = false;
};

/// PhantomUpdateFormationAndMoveGoal.
class UpdateFormationAndMoveGoal final : public KnightGoal {
public:
    using KnightGoal::KnightGoal;
    bool canUse() override { return true; }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        auto& k = stateOf(*m);
        // noPhysics = getTicksProgress() % 20 != 0: the BP keeps the knights without block collision
        k.ticksProgress++;
        if (k.ticksProgress >= kDuration[static_cast<int>(k.formation)]) switchToNextFormation(*m, k);
        Vec3 dest = destination(*m, k);
        ge::ext::setWantedPosition(*m, dest, k.charging ? 2.0 : 1.0);
        persist(*m, k);
    }

private:
    static void switchToNextFormation(Mob& m, Knight& k) {
        auto  knights = nearbyKnights(m);
        auto& random  = fe::randomOf(m);
        if (k.formation == Formation::AttackPlayerStart) {
            switchToFormation(m, k, Formation::AttackPlayerAttack);
        } else if (k.formation == Formation::AttackPlayerAttack) {
            if (knights.size() > 1) {
                switchToFormation(m, k, Formation::WaitingForLeader);
            } else {
                // random weapon switch!
                switch (random.nextInt(3)) {
                case 0: setWeapon(m, kSword); break;
                case 1: setWeapon(m, kAxe); break;
                default: setWeapon(m, kPick); break;
                }
                switchToFormation(m, k, Formation::AttackPlayerStart);
            }
        } else if (k.formation == Formation::WaitingForLeader) {
            // try to find a nearby knight and do what they're doing
            if (knights.size() > 1) {
                auto& other = stateOf(*knights[1]);
                switchToFormation(m, k, other.formation);
                k.ticksProgress = other.ticksProgress;
            } else {
                switchToFormation(m, k, Formation::AttackPlayerStart);
            }
        } else {
            // the lowest-numbered knight leads
            bool leader = std::none_of(knights.begin(), knights.end(), [&](Mob* kn) { return stateOf(*kn).number < k.number; });
            if (!leader) return;
            // pick a random formation
            switch (random.nextInt(8)) {
            case 0:
            case 7: switchToFormation(m, k, Formation::SmallClockwise); break;
            case 1:
            case 2: switchToFormation(m, k, Formation::SmallAnticlockwise); break;
            case 3: switchToFormation(m, k, Formation::ChargePlusX); break;
            case 4: switchToFormation(m, k, Formation::ChargeMinusX); break;
            case 5: switchToFormation(m, k, Formation::ChargePlusZ); break;
            case 6: switchToFormation(m, k, Formation::ChargeMinusZ); break;
            default: break;
            }
            // broadcast it
            for (auto* kn : knights) {
                auto& other = stateOf(*kn);
                if (!other.charging) switchToFormation(*kn, other, k.formation);
            }
            // if no one is charging, a random knight charges
            bool nobody = std::none_of(knights.begin(), knights.end(), [](Mob* kn) { return stateOf(*kn).charging; });
            if (nobody && !knights.empty()) {
                auto* chosen = knights[static_cast<std::size_t>(random.nextInt(static_cast<int>(knights.size())))];
                switchToFormation(*chosen, stateOf(*chosen), Formation::AttackPlayerStart);
            }
        }
    }

    static Vec3 destination(Mob& m, Knight& k) {
        BlockPos home = homePos(m);
        double   bob  = std::cos(static_cast<float>(k.ticksProgress) / 7.0F + static_cast<float>(k.number));
        auto     circle = [&](float distance, bool clockwise) {
            float angle = static_cast<float>(k.ticksProgress) * 2.0F;
            if (!clockwise) angle *= -1;
            angle += 60.0F * static_cast<float>(k.number);
            return Vec3{static_cast<float>(home.x + std::cos(angle * std::numbers::pi / 180.0) * distance), static_cast<float>(home.y + bob),
                        static_cast<float>(home.z + std::sin(angle * std::numbers::pi / 180.0) * distance)};
        };
        auto across = [&](bool plus, bool alongX) {
            float offset0 = static_cast<float>(k.number) * 3.0F - 7.5F;
            float offset1 = k.ticksProgress < 60 ? -7.0F : -7.0F + ((static_cast<float>(k.ticksProgress - 60) / 120.0F) * 14.0F);
            if (!plus) offset1 *= -1;
            return Vec3{static_cast<float>(home.x + (alongX ? offset0 : offset1)), static_cast<float>(home.y + bob),
                        static_cast<float>(home.z + (alongX ? offset1 : offset0))};
        };
        auto hover = [&](float distance) {
            // xOld / zOld: the position at the start of the tick
            auto   p  = m.getFeetPos();
            auto   v  = m.getPosDelta();
            double dx = p.x - v.x;
            double dz = p.z - v.z;
            double ox = home.x - dx;
            double oz = home.z - dz;
            double dd = std::sqrt(ox * ox + oz * oz);
            if (dd > distance) {
                dx = home.x + (ox / dd * distance);
                dz = home.z + (oz / dd * distance);
            }
            return Vec3{static_cast<float>(dx), static_cast<float>(home.y + bob), static_cast<float>(dz)};
        };
        switch (k.formation) {
        case Formation::LargeClockwise: return circle(kLargeRadius, true);
        case Formation::SmallClockwise: return circle(kSmallRadius, true);
        case Formation::LargeAnticlockwise: return circle(kLargeRadius, false);
        case Formation::SmallAnticlockwise: return circle(kSmallRadius, false);
        case Formation::ChargePlusX: return across(true, true);
        case Formation::ChargeMinusX: return across(false, true);
        case Formation::ChargePlusZ: return across(true, false);
        case Formation::ChargeMinusZ: return across(false, false);
        case Formation::AttackPlayerStart:
        case Formation::Hover: return hover(kLargeRadius);
        case Formation::AttackPlayerAttack:
            if (holds(m, kSword)) return Vec3{static_cast<float>(k.chargePos.x), static_cast<float>(k.chargePos.y), static_cast<float>(k.chargePos.z)};
            return hover(kLargeRadius);
        default: return Vec3{static_cast<float>(home.x), static_cast<float>(home.y + bob), static_cast<float>(home.z)};
        }
    }
};

/// PhantomAttackStartGoal.
class AttackStartGoal final : public KnightGoal {
public:
    explicit AttackStartGoal(std::int64_t uid) : KnightGoal(uid) { flags = Look; }
    bool canUse() override {
        auto* m = mob();
        return m && fe::mobs::target(*m) && stateOf(*m).formation == Formation::AttackPlayerStart;
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        auto* target = fe::mobs::target(*m);
        if (!target) return;
        auto& k = stateOf(*m);
        // BlockPos.containing(target.xOld, target.yOld, target.zOld)
        auto p = target->getFeetPos();
        auto v = target->getPosDelta();
        BlockPos targetPos{static_cast<int>(std::floor(p.x - v.x)), static_cast<int>(std::floor(p.y - v.y)), static_cast<int>(std::floor(p.z - v.z))};
        k.chargePos = withinHome(*m) ? targetPos : homePos(*m);
    }
};

/// PhantomThrowWeaponGoal.
class ThrowWeaponGoal final : public KnightGoal {
public:
    explicit ThrowWeaponGoal(std::int64_t uid) : KnightGoal(uid) { flags = Look; }
    bool canUse() override {
        auto* m = mob();
        return m && fe::mobs::target(*m) && stateOf(*m).formation == Formation::AttackPlayerAttack;
    }
    void tick() override {
        auto* m = mob();
        if (!m) return;
        auto* target = fe::mobs::target(*m);
        if (!target || stateOf(*m).ticksProgress % 4 != 0) return;
        if (holds(*m, kAxe)) launchAxeAt(*m, *target);
        else if (holds(*m, kPick)) launchPicks(*m);
    }
};

} // namespace

// ---- the knight's actions ------------------------------------------------------------------------------

bool doHurtTarget(Mob& knight, Actor& target) {
    auto& k = stateOf(knight);
    // EntityUtil.properlyApplyCustomDamageSource(this, entity, haunt damage by this, null)
    return gf::combat::hurt(target, attackDamage(knight, k), "gloamingforest:haunt", &knight, &knight);
}

void launchAxeAt(Mob& knight, Actor& target) {
    auto&  random           = fe::randomOf(knight);
    float  bodyFacingAngle  = knight.getYBodyRotation() * std::numbers::pi_v<float> / 180.0F;
    auto   p                = knight.getFeetPos();
    auto   b                = knight.getAABB();
    double height           = b.max.y - b.min.y;
    double sx               = p.x + std::cos(bodyFacingAngle) * 1;
    double sy               = p.y + height * 0.82;
    double sz               = p.z + std::sin(bodyFacingAngle) * 1;
    auto   tb               = target.getAABB();
    auto   tp               = target.getFeetPos();
    double tx               = tp.x - sx;
    double ty               = (tb.min.y + (tb.max.y - tb.min.y) / 2.0F) - (p.y + height / 2.0F);
    double tz               = tp.z - sz;
    float  r1               = random.nextFloat();
    float  r2               = random.nextFloat();
    playSound(knight, "entity.gloamingforest.knight_phantom.axe", 1.0F, (r1 - r2) * 0.2F + 0.4F);
    Actor* projectile = gf::projectile::shoot(knight, id::ThrownWep, Vec3{static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz)}, 0.75F, 1.0F,
                                              Json{{"item", kAxe}, {"damage", 6.0}, {"gravity", 0.001}});
    if (projectile) projectile->moveTo(Vec3{static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)}, knight.getRotation());
}

void launchPicks(Mob& knight) {
    auto& random = fe::randomOf(knight);
    float r1     = random.nextFloat();
    float r2     = random.nextFloat();
    playSound(knight, "entity.gloamingforest.knight_phantom.pickaxe", 1.0F, (r1 - r2) * 0.2F + 0.4F);
    auto   p      = knight.getFeetPos();
    auto   b      = knight.getAABB();
    double height = b.max.y - b.min.y;
    for (int i = 0; i < 8; i++) {
        float  throwAngle = static_cast<float>(i) * std::numbers::pi_v<float> / 4.0F;
        double sx         = p.x + std::cos(throwAngle) * 1;
        double sy         = p.y + height * 0.82;
        double sz         = p.z + std::sin(throwAngle) * 1;
        Actor* projectile = gf::projectile::shoot(knight, id::ThrownWep, Vec3{std::cos(throwAngle), 0.0F, std::sin(throwAngle)}, 0.5F, 1.0F,
                                                  Json{{"item", kPick}, {"damage", 3.0}, {"gravity", 0.015}});
        if (projectile) projectile->moveTo(Vec3{static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)}, Vec2{knight.getRotation().x, i * 45.0F});
    }
}

// ---- lifecycle ----------------------------------------------------------------------------------------

namespace {

/// KnightPhantom#giveKnightLoot.
void giveKnightLoot(Mob& phantom, std::vector<ItemStack> const& items, std::vector<int>& list, Vec3 const& dropOff) {
    for (auto const& itemstack : items) {
        if (!list.empty()) {
            // if there are still more slots to be occupied, occupy them
            auto buffer = gf::boss::ext::lootBuffer(phantom);
            while (!list.empty()) {
                int index = list.back();
                list.pop_back();
                if (index >= 0 && index < static_cast<int>(buffer.size()) && game::items::isEmpty(buffer[static_cast<std::size_t>(index)])) {
                    gf::boss::ext::setLootBufferSlot(phantom, index, itemstack);
                    break;
                }
            }
        } else {
            // if all slots have been used up, throw the items on the ground
            auto& region = phantom.getDimensionBlockSource();
            phantom.getLevel().getSpawner().spawnItem(region, itemstack, nullptr, dropOff, 0);
        }
    }
}

/// KnightPhantom#postmortem.
void postmortem(Mob& boss, std::string const& damageType, Actor* killer) {
    auto& k       = stateOf(boss);
    auto  knights = nearbyKnights(boss);
    auto& random  = fe::randomOf(boss);
    auto  items   = gf::boss::ext::rollLoot(boss, "", damageType, killer);
    if (!knights.empty()) {
        for (auto* kn : knights) updateMyNumber(*kn, stateOf(*kn));
        for (auto const& s : gf::boss::ext::lootBuffer(boss))
            if (!game::items::isEmpty(s)) items.push_back(s);
        auto list = gf::boss::ext::availableSlots(random);
        gf::boss::ext::shuffleAndSplitItems(items, static_cast<int>(list.size()), random);
        giveKnightLoot(*knights.front(), items, list, boss.getFeetPos());
        // die: the bar goes away when another knight lives (knight 0 shows it again)
        gf::boss::bar(boss).setVisible(false);
        gf::log().info("stronghold: a knight phantom died, {} remain", knights.size());
        return;
    }
    gf::boss::bar(boss).setProgress(0.0F);
    int      dim  = static_cast<int>(boss.getDimensionId());
    auto     home = ge::home(boss);
    auto     feet = boss.getFeetPos();
    BlockPos treasurePos{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y)), static_cast<int>(std::floor(feet.z))};
    if (home) {
        BlockPos h{home->pos.x, home->pos.y, home->pos.z};
        BlockPos below{h.x, h.y - 1, h.z};
        auto*    region = game::regionOf(home->pos.dim);
        treasurePos     = region && game::replaceable(*region, below) ? below : h;
    }
    for (auto& s : gf::boss::ext::rollLoot(boss, "gloamingforest:entities/knight_phantom_defeated", damageType, killer)) items.push_back(s);
    auto list = gf::boss::ext::availableSlots(random);
    gf::boss::ext::shuffleAndSplitItems(items, static_cast<int>(list.size()), random);
    giveKnightLoot(boss, items, list, boss.getFeetPos());
    // trigger criteria for killing every phantom in a group
    if (killer && killer->isPlayer()) {
        gf::progression::trigger(static_cast<Player&>(*killer), "gloamingforest:kill_all_phantoms");
        for (auto* p : fe::playersNear(dim, Vec3{treasurePos.x + 0.5f, treasurePos.y + 0.5f, treasurePos.z + 0.5f}, 64.0)) {
            auto pp = p->getFeetPos();
            if (std::abs(pp.x - (treasurePos.x + 0.5)) <= 32.5 && std::abs(pp.y - (treasurePos.y + 0.5)) <= 32.5 && std::abs(pp.z - (treasurePos.z + 0.5)) <= 32.5)
                gf::progression::trigger(*p, "gloamingforest:kill_all_phantoms");
        }
    }
    // mark the stronghold as defeated
    if (home && home->pos.dim == dim) gf::landmark::setConquered(dim, BlockPos{home->pos.x, home->pos.y, home->pos.z}, id::Stronghold, true);
    // tell the other knights to reset their animation
    for (auto* kn : allKnightsAround(boss))
        if (kn != &boss && !kn->isAlive()) gf::boss::ext::setDeathTime(*kn, 1);
    k.over = true;
    persist(boss, k);
    gf::log().info("stronghold: the last knight phantom died, the stronghold is conquered");
}

void particle(Mob& m, char const* id, Vec3 const& at, Vec3 const& vel = {}) { gf::fx::particle(m.getDimension(), id, at, vel); }

/// KnightPhantom#tickDeath + #tickDeathAnimation (the client's particles, sent through fx).
void tickDeath(Mob& m, int deathTime) {
    auto& k      = stateOf(m);
    auto& random = fe::randomOf(m);
    bool  visible = hasYetToDisappear(m, k);
    // getDeltaMovement: DYING_ASCENT while still visible
    if (visible) {
        auto p = m.getFeetPos();
        m.moveTo(Vec3{p.x, p.y + 0.015f, p.z}, m.getRotation());
    }
    ge::setProperty(m, "gloamingforest:dying_visible", visible);
    // the hitbox goes away for the second part of the animation
    if (deathTime >= kDyingTicks && !k.untouchable) {
        k.untouchable = true;
        event(m, "untouchable");
    }
    auto const& b     = m.getAABB();
    float       width = b.max.x - b.min.x;
    float       h     = b.max.y - b.min.y;
    auto        p     = m.getFeetPos();
    auto        others = allKnightsAround(m);
    bool        allPast = std::none_of(others.begin(), others.end(), [](Mob* kn) { return deathTimeOf(*kn) < kDyingTicks; });
    double      time    = static_cast<double>(m.mTickCount) + static_cast<double>(m.getRuntimeID().rawID % 1000) * 8.0;
    if (allPast) {
        // smoke trail to the chest position
        Vec3     start{p.x, p.y + 1.0f, p.z};
        BlockPos chest{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
        if (auto home = ge::home(m)) chest = BlockPos{home->pos.x, home->pos.y - 1, home->pos.z};
        Vec3   end{chest.x + 0.5f, chest.y + 0.5f, chest.z + 0.5f};
        double factor = std::min(static_cast<double>(deathTime - kDyingTicks + 1) / static_cast<double>(kParticleTicks), 1.0);
        Vec3   at{static_cast<float>(start.x + (end.x - start.x) * factor + std::sin(time * 0.3) * 0.25),
                static_cast<float>(start.y + (end.y - start.y) * factor + std::sin(time * 0.15) * 0.25),
                static_cast<float>(start.z + (end.z - start.z) * factor + std::cos(time * 0.35) * 0.25)};
        for (int i = 0; i < 3; i++) {
            double x = (random.nextDouble() - 0.5) * 0.15 * i;
            double y = (random.nextDouble() - 0.5) * 0.15 * i;
            double z = (random.nextDouble() - 0.5) * 0.15 * i;
            particle(m, "minecraft:smoke", Vec3{static_cast<float>(at.x + x), static_cast<float>(at.y + y), static_cast<float>(at.z + z)});
        }
    } else if (!nearbyKnights(m).empty() || k.over) {
        auto randomAt = [&](float scale) {
            float rx = random.nextFloat();
            float ry = random.nextFloat();
            float rz = random.nextFloat();
            return Vec3{p.x + (2.0f * rx - 1.0f) * width * scale, p.y + h * ry, p.z + (2.0f * rz - 1.0f) * width * scale};
        };
        if (deathTime == kDyingTicks) {
            // poof when going invisible
            for (int i = 0; i < 20; ++i) particle(m, random.nextBoolean() ? "minecraft:smoke" : "minecraft:poof", randomAt(1.5f));
        } else if (deathTime <= kDyingTicks) {
            // particles while floating up
            for (int i = 0; i < 10; ++i) {
                if (random.nextInt(4) == 0) particle(m, random.nextBoolean() ? "minecraft:poof" : "minecraft:smoke", randomAt(0.75f));
                if (random.nextInt(5) == 0) {
                    float fx = random.nextFloat(), fy = random.nextFloat(), fz = random.nextFloat();
                    gf::fx::particle(m.getDimension(), "minecraft:item", Vec3{p.x + (fx - 0.5f) * width, p.y + fy * (h - 0.75f) + 0.5f, p.z + (fz - 0.5f) * width},
                                     Vec3{0.0f, -0.1f, 0.0f}, 1,
                                     Json{{"item", random.nextBoolean() ? "gloamingforest:phantom_helmet" : kSword}});
                }
            }
        } else {
            // smoke swirl while other knights are still alive
            Vec3 at{static_cast<float>(p.x + std::sin(time * 0.3) * 0.25), static_cast<float>(p.y + 1.0 + std::sin(time * 0.15) * 0.25),
                    static_cast<float>(p.z + std::cos(time * 0.35) * 0.25)};
            for (int i = 0; i < 3; i++) {
                double x = (random.nextDouble() - 0.5) * 0.15 * i;
                double y = (random.nextDouble() - 0.5) * 0.15 * i;
                double z = (random.nextDouble() - 0.5) * 0.15 * i;
                particle(m, "minecraft:smoke", Vec3{static_cast<float>(at.x + x), static_cast<float>(at.y + y), static_cast<float>(at.z + z)});
            }
        }
    }
}

void onSpawn(Actor& self) {
    auto* m = fe::asMob(self);
    if (!m) return;
    ge::setMoveControl(*m, ge::MoveKind::NoClip);
    bool  loaded = ge::data(self, kData).is_object();
    auto& k      = stateOf(self);
    if (!loaded) {
        // finalizeSpawn: populateDefaultEquipmentSlots
        setWeapon(*m, kSword);
        if (auto s = game::items::make("gloamingforest:phantom_chestplate")) m->setItemSlot(Slot::Torso, *s);
        if (auto s = game::items::make("gloamingforest:phantom_helmet")) m->setItemSlot(Slot::Head, *s);
        // KnightPhantomSpawnerBlockEntity#spawnMyBoss: setNumber(i) after the spawn
        if (auto const& n = ge::data(self, "knight_phantom:number"); n.is_number_integer()) setNumber(*m, k, n.get<int>());
    } else {
        // readAdditionalSaveData: setNumber (the weapon) and the formation
        setNumber(*m, k, k.number);
    }
    // setRestrictionPoint: the charge position starts at home
    if (auto h = ge::home(*m); h && !loaded) k.chargePos = BlockPos{h->pos.x, h->pos.y, h->pos.z};
    ge::setProperty(*m, "gloamingforest:charging", k.charging);
    ge::setProperty(*m, "gloamingforest:formation", static_cast<int>(k.formation));
    if (k.charging) event(*m, "charging");
    gf::boss::bar(*m).setName(gf::tr("entity.gloamingforest.knight_phantom.plural", "Knight Phantoms"));
    gf::boss::bar(*m).setVisible(k.number == 0);
    auto uid = fe::uidOf(self);
    ge::addGoal(*m, 0, std::make_shared<WatchAndAttackGoal>(uid));
    ge::addGoal(*m, 1, std::make_shared<UpdateFormationAndMoveGoal>(uid));
    ge::addGoal(*m, 2, std::make_shared<AttackStartGoal>(uid));
    ge::addGoal(*m, 3, std::make_shared<ThrowWeaponGoal>(uid));
    persist(self, k);
}

void onTick(Actor& self) {
    auto* m = fe::asMob(self);
    if (!m || !m->isAlive()) return;
    auto& k = stateOf(self);
    // customServerAiStep: the first tick's numbering
    if (k.totalKnownKnights == INT_MIN) updateMyNumber(*m, k);
}

/// customServerAiStep's bar: the sum of the live knights' health over (their max + max x missing
/// knights). Run after the boss kit's per-boss update (a level tick), which would set health / max.
void updateBars() {
    for (auto it = states().begin(); it != states().end();) {
        auto* a = fe::actorById(it->first);
        if (!a) {
            it = states().erase(it);
            continue;
        }
        auto* m = fe::asMob(*a);
        if (m && m->isAlive()) {
            float health = 0.0F, maxHealth = 0.0F;
            int   amount = 0;
            for (auto* kn : nearbyKnights(*m)) {
                health    += static_cast<float>(kn->getHealth());
                maxHealth += static_cast<float>(kn->getMaxHealth());
                amount++;
            }
            int remaining = it->second.totalKnownKnights - amount;
            if (remaining > 0) maxHealth += static_cast<float>(m->getMaxHealth()) * static_cast<float>(remaining);
            if (maxHealth > 0.0F) gf::boss::bar(*m).setProgress(health / maxHealth);
        }
        ++it;
    }
}

// ---- ThrownWep ---------------------------------------------------------------------------------------

void thrownSmoke(Actor& projectile) {
    // handleEntityEvent(3): 8 large smoke particles
    for (int i = 0; i < 8; ++i) gf::fx::particle(projectile.getDimension(), "minecraft:large_smoke", projectile.getFeetPos());
}

Json initOf(Actor& projectile) {
    auto const& j = ge::data(projectile, "init");
    return j.is_object() ? j : Json::object();
}

} // namespace

std::optional<Formation> formationOf(Actor const& knight) {
    auto* k = find(knight);
    return k ? std::optional<Formation>(k->formation) : std::nullopt;
}

int numberOf(Actor const& knight) {
    auto* k = find(knight);
    return k ? k->number : -1;
}

bool isCharging(Actor const& knight) {
    auto* k = find(knight);
    return k && k->charging;
}

bool isOver(Actor const& knight) {
    auto* k = find(knight);
    return k && k->over;
}

int ticksProgressOf(Actor const& knight) {
    auto* k = find(knight);
    return k ? k->ticksProgress : 0;
}

void forceFormationForTest(Mob& knight, Formation formation, int ticksProgress) {
    auto& k = stateOf(knight);
    switchToFormation(knight, k, formation);
    k.ticksProgress = ticksProgress;
    persist(knight, k);
}

void setNumberForTest(Mob& knight, int number) { setNumber(knight, stateOf(knight), number); }

void install() {
    gf::boss::registerBoss({
        .entityId        = id::KnightPhantom,
        .homeStructure   = id::Stronghold,
        .lootTable       = "gloamingforest:entities/knight_phantom",
        .deathChestBlock = "gloamingforest:dark_chest",
        .bar             = {.rgb = 0x86BF37},
        .deathTicks      = kDyingTicks + kParticleTicks,
    });
    gf::boss::ext::setHomeRadius(id::KnightPhantom, kHomeRadius);
    // KnightPhantomSpawnerBlockEntity: the boss kit's KnightPhantoms variant (nearest player within 9
    // above y - 2; six knights on a circle of 4, knight 5 shielded on hard, numbered by their slot)
    gf::boss::registerSpawner({.blockId = id::Spawner, .entityId = id::KnightPhantom, .range = 9, .particle = "gloamingforest:ominous_flame"});
    gf::boss::ext::setLifecycle(id::KnightPhantom, {
                                                       .postmortem             = postmortem,
                                                       .tickDeath              = tickDeath,
                                                       .deathAnimationFinished = [](Mob& boss,
                                                                                    int  deathTime) { return deathTime >= kDyingTicks + kParticleTicks && nearbyKnights(boss).empty(); },
                                                       .shouldDeposit = [](Mob& boss) { return stateOf(boss).over; },
                                                   });
    ge::registerType(id::KnightPhantom, {
                                            .onSpawn = onSpawn,
                                            .tick    = onTick,
                                            .hurt =
                                                [](Actor& self, float& amount, std::string const& type, Actor* attacker) {
                                                    // isInvulnerableTo: in-wall damage
                                                    if (type == "minecraft:in_wall") return false;
                                                    auto& k = stateOf(self);
                                                    // isDamageSourceBlocked: a raised shield blocks a hit from the front
                                                    if (k.guarding && attacker && !gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_shield", type)) {
                                                        auto   sp = self.getFeetPos();
                                                        auto   ap = attacker->getFeetPos();
                                                        float  yaw = self.getRotation().y * std::numbers::pi_v<float> / 180.0F;
                                                        double vx = -std::sin(yaw), vz = std::cos(yaw);
                                                        double dx = ap.x - sp.x, dz = ap.z - sp.z;
                                                        double len = std::sqrt(dx * dx + dz * dz);
                                                        if (len > 1e-6 && (dx / len) * vx + (dz / len) * vz > 0.0) {
                                                            auto& r  = levelRandom();
                                                            playSound(self, "minecraft:item.shield.block", 1.0F, 0.8F + r.nextFloat() * 0.4F);
                                                            return false;
                                                        }
                                                    }
                                                    // NON_CHARGING_ARMOR_MODIFIER: the armour x5 while not charging
                                                    // (phantom helmet + chestplate: 11 armour, 5 toughness)
                                                    if (!k.charging && !gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_armor", type))
                                                        amount *= armorFactor(amount, 55.0F, 5.0F) / armorFactor(amount, 11.0F, 5.0F);
                                                    return true;
                                                },
                                        });
    gf::spawn::registerPlacement(id::KnightPhantom, gf::spawn::PlacementRule::OnGround);
    fe::onLevelTick(updateBars);
    // ThrownWep
    gf::projectile::registerType(id::ThrownWep, {
                                                     .gravity = 0.0F,
                                                     .drag    = 0.99F,
                                                     .onHitEntity =
                                                         [](Actor& projectile, Actor& target) {
                                                             auto  init  = initOf(projectile);
                                                             auto* owner = gf::projectile::ext::owner(projectile);
                                                             if (!isKnight(target) && &target != owner) {
                                                                 bool pick = init.value("item", std::string{}) == kPick;
                                                                 gf::combat::hurt(target, static_cast<float>(init.value("damage", 6.0)),
                                                                                  pick ? "gloamingforest:thrown_pickaxe" : "gloamingforest:thrown_axe");
                                                             }
                                                             thrownSmoke(projectile);
                                                             return true;
                                                         },
                                                     .onHitBlock =
                                                         [](Actor& projectile, BlockPos const&, int) {
                                                             thrownSmoke(projectile);
                                                             return true;
                                                         },
                                                     .tick =
                                                         [](Actor& projectile) {
                                                             // DATA_VELOCITY: the thrower's gravity (axes 0.001, picks 0.015)
                                                             auto  init = initOf(projectile);
                                                             float g    = static_cast<float>(init.value("gravity", 0.001));
                                                             auto  v    = gf::projectile::ext::velocity(projectile);
                                                             gf::projectile::ext::setVelocity(projectile, Vec3{v.x, v.y - g, v.z});
                                                             ge::setProperty(projectile, "gloamingforest:pickaxe", init.value("item", std::string{}) == kPick);
                                                         },
                                                 });
    game::db::onWorldClosed([] { states().clear(); });
}

} // namespace gf::feature::stronghold::phantom

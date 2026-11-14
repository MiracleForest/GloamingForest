// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/boss/Hydra.java
//   src/main/java/gloamingforest/entity/boss/HydraHeadContainer.java
//   src/main/java/gloamingforest/entity/boss/HydraHead.java
//   src/main/java/gloamingforest/entity/boss/HydraNeck.java
//   src/main/java/gloamingforest/entity/boss/HydraPart.java
//   src/main/java/gloamingforest/entity/boss/HydraSmallPart.java
//   src/main/java/gloamingforest/block/entity/spawner/HydraSpawnerBlockEntity.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Hydra (U24). Spike S4's multipart path: every upstream part is a linked part actor (F8's addPart)
// of type gloamingforest:hydra_part, whose integer property gloamingforest:part (the part's index:
// 0 body, 1 tail, then per head the head and its five necks) picks its geometry in the resource
// pack; the head state, mouth openness, activity and the red hurt overlay travel as entity
// properties (gloamingforest:active, :mouth, :red), the head / neck facing as the part actor's
// rotation (F8's setPartRotation), so vanilla clients draw the same Hydra as the client mod.
// The server runs HydraHeadContainer for each head every tick (state machine, poses, facing,
// bite / flame / mortar attacks, sounds) and the Hydra's brain (target, attack choice, secondary
// attacks, griefing, the head deaths and respawns); the client-side particles of upstream (mouth
// particles, dying parts, death animation) are sent through gf::fx.

#include "feature/hydra/Hydra.h"
#include "feature/hydra/World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"

#include "core/jrand/JavaOrder.h"
#include "feature/boss/Boss.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/fx/Fx.h"
#include "feature/inventory/Inventory.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::hydra {

namespace ch = gf::core::hydra;
namespace fe = gf::feature::hydra::world;
namespace ge = gf::entity;
namespace ia = gf::interactions;
using ch::State;
using ch::V3;

namespace {

constexpr float kPi        = 3.1415927F;
constexpr int   kHurtTime  = 10;  ///< LivingEntity#hurtDuration
constexpr float kEyeHeight = 10.2F; ///< 0.85 x 12
constexpr float kHalfWidth = 8.0F;  ///< the Java hydra's 16 x 12 box (the Bedrock actor's box is small)
constexpr float kHeight    = 12.0F;

// ---- runtime -------------------------------------------------------------------------------------

struct PartRt {
    V3    pos;
    float yRot = 0, xRot = 0;
    bool  active     = true;  ///< HydraPart#isActive (DATA_SIZEACTIVE)
    bool  markedDead = false;
    int   deathTime  = 0;
    int   hurtTime   = 0;
    // last values sent as entity properties / name
    int         sentActive = -1;
    int         sentRed    = -1;
    int         sentDeath  = -1;
    float       sentMouth  = -1.0F;
    std::string sentName;
    bool        nameSent = false;
};

struct HeadRt {
    ch::Head     head;
    std::int64_t target = 0; ///< targetEntity
    double       tx = 0, ty = 0, tz = 0;
    int          tickCount = 0; ///< headEntity.tickCount
    float        mouth = 0, mouthLast = 0;
};

struct Runtime {
    std::array<HeadRt, ch::kMaxHeads>             heads;
    std::array<PartRt, ch::kPartCount>            parts;
    std::array<ge::PartHandle, ch::kPartCount>    handles;
    std::array<std::string, ch::kMaxHeads>        names;
    float         yRot = 0, xRot = 0, bodyYaw = 0;
    float         randomYawVelocity = 0;
    int           ticksSinceDamaged = 0;
    int           chase  = 0;
    std::int64_t  target = 0;
    int           tickCount = 0;
    int           hurtTime  = 0;
    int           invulnerableTime = 0; ///< LivingEntity#invulnerableTime (20 after an accepted hit)
    float         lastHurt  = 0;        ///< LivingEntity#lastHurt
    int           deathTime = 0;
    std::uint64_t lastTick  = 0;
    bool          brain = true;
    float         damageTaken = 0;
    int           ambientSoundTime = 0; ///< Mob#ambientSoundTime
    int           sentRed = -1;         ///< the body's red overlay property last sent
    bool          gravity = true;       ///< the actor's gravity flag (resting on the Java box)
    std::unique_ptr<fe::JavaRandom> random; ///< the hydra's own random (Entity#random)
};

struct State_ {
    std::unordered_map<std::int64_t, Runtime>      runtimes;
    std::unordered_map<std::int64_t, ge::Json>     pendingLoad;
    std::unordered_map<std::int64_t, std::int64_t> pendingSeed; ///< seedForTest before the first tick
    int                                            levelTick = 0;
    std::vector<ia::HandlerId>                    handlers;
    std::unique_ptr<fe::JavaRandom>               levelRandom;
    /// The direct entity of the damage now going through the pipeline to a hydra (DamageSource#
    /// getDirectEntity: Behaviour::hurt only gets the causing entity), by uid (0: none).
    std::int64_t                                  hurtDirect  = 0; ///< noteDirect: the direct entity of the hit on its way
    std::int64_t                                  hurtVictim  = 0; ///< ... to this hydra
    std::uint64_t                                 hurtTick    = 0; ///< ... in this tick
    bool                                          combatHooked = false;
};
State_& S() {
    static State_ s;
    return s;
}

fe::JavaRandom& levelRandom() {
    if (!S().levelRandom) S().levelRandom = std::make_unique<fe::JavaRandom>(fe::freshSeed());
    return *S().levelRandom;
}

Runtime* find(std::int64_t uid) {
    auto it = S().runtimes.find(uid);
    return it == S().runtimes.end() ? nullptr : &it->second;
}

V3   toV3(Vec3 const& v) { return {v.x, v.y, v.z}; }
Vec3 toVec(V3 const& v) { return Vec3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

std::string partId(int idx) {
    if (idx == 0) return "body";
    if (idx == 1) return "tail";
    int h = (idx - ch::kBodyParts) / ch::kPartsPerHead;
    int k = (idx - ch::kBodyParts) % ch::kPartsPerHead;
    if (k == 0) return "head" + std::to_string(h);
    return "neck" + std::to_string(h) + static_cast<char>('a' + k - 1);
}

int partIndex(std::string const& id) {
    if (id == "body") return 0;
    if (id == "tail") return 1;
    if (id.size() == 5 && id.starts_with("head")) return ch::headPartIndex(id[4] - '0');
    if (id.size() == 6 && id.starts_with("neck")) return ch::neckPartIndex(id[4] - '0', id[5] - 'a');
    return -1;
}

/// The head a part belongs to (-1: body or tail).
int headOf(int idx) { return idx < ch::kBodyParts ? -1 : (idx - ch::kBodyParts) / ch::kPartsPerHead; }
bool isHeadPart(int idx) { return idx >= ch::kBodyParts && (idx - ch::kBodyParts) % ch::kPartsPerHead == 0; }

ge::PartSize fullSize(int idx) {
    if (idx == 0) return {6.0F, 6.0F};
    if (idx == 1) return {6.0F, 2.0F};
    return isHeadPart(idx) ? ge::PartSize{4.0F, 4.0F} : ge::PartSize{2.0F, 2.0F};
}
/// HydraPart#deactivate: dimensions 0 x 0 (a tiny box: Bedrock keeps actors with a positive size).
constexpr ge::PartSize kInactiveSize{0.01F, 0.01F};

V3 hydraPos(Actor const& a) { return toV3(a.getFeetPos()); }

using world::hard;
using world::mobGriefing;

/// Whether `actor` is a part of the hydra `uid`.
bool partOf(Actor const& actor, std::int64_t uid) { return fe::parentOfPart(actor) == uid; }

/// Java's LivingEntity: a mob that is no upstream part.
bool living(Actor& a) { return fe::asMob(a) != nullptr && fe::parentOfPart(a) == 0 && a.isAlive(); }

AABB box(V3 const& feet, float width, float height) {
    float hw = width * 0.5F;
    return AABB{Vec3{static_cast<float>(feet.x - hw), static_cast<float>(feet.y), static_cast<float>(feet.z - hw)},
                Vec3{static_cast<float>(feet.x + hw), static_cast<float>(feet.y + height), static_cast<float>(feet.z + hw)}};
}

AABB inflate(AABB const& b, float x, float y, float z) {
    return AABB{Vec3{b.min.x - x, b.min.y - y, b.min.z - z}, Vec3{b.max.x + x, b.max.y + y, b.max.z + z}};
}

/// Entity#setXRot's clamp.
float clampPitch(float x) {
    if (!std::isfinite(x)) return 0.0F;
    float m = std::fmod(x, 360.0F);
    return std::clamp(m, -90.0F, 90.0F);
}

/// The slab test of AABB#clip: the fraction of a -> b where the segment enters `bb` (nullopt: misses).
std::optional<double> clipBox(AABB const& bb, V3 a, V3 b) {
    double tmin = 0.0, tmax = 1.0;
    double d[3]  = {b.x - a.x, b.y - a.y, b.z - a.z};
    double o[3]  = {a.x, a.y, a.z};
    double lo[3] = {bb.min.x, bb.min.y, bb.min.z};
    double hi[3] = {bb.max.x, bb.max.y, bb.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-12) {
            if (o[i] < lo[i] || o[i] > hi[i]) return std::nullopt;
            continue;
        }
        double t1 = (lo[i] - o[i]) / d[i], t2 = (hi[i] - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return std::nullopt;
    }
    return tmin;
}

bool contains(AABB const& bb, V3 p) {
    return p.x >= bb.min.x && p.x < bb.max.x && p.y >= bb.min.y && p.y < bb.max.y && p.z >= bb.min.z && p.z < bb.max.z;
}

void sound(Actor& at, V3 pos, char const* event, float volume, float pitch) { gf::fx::sound(at.getDimension(), event, toVec(pos), volume, pitch); }

void particle(Actor& at, char const* id, V3 pos, V3 vel = {}, int count = 1, nlohmann::json const& params = {}) {
    gf::fx::particle(at.getDimension(), id, toVec(pos), toVec(vel), count, params);
}

// ---- parts -------------------------------------------------------------------------------------------

void setPartActive(Runtime& rt, int idx, bool active) {
    auto& p  = rt.parts[idx];
    p.active = active;
    if (active) {
        // HydraPart#onSyncedDataUpdated: an activated part renders again
        p.markedDead = false;
        p.deathTime  = 0;
    }
    if (rt.handles[idx].valid()) ge::ext::setPartSize(rt.handles[idx], active ? fullSize(idx) : kInactiveSize);
}

/// HydraPart#tick (server side; the client's particles of a dying part go out through gf::fx).
void tickPart(Actor& hydra, Runtime& rt, int idx) {
    auto& p = rt.parts[idx];
    if (p.hurtTime > 0) p.hurtTime--;
    if (p.markedDead) p.deathTime++;
    if (p.markedDead && p.active) {
        auto  size = fullSize(idx);
        auto& r    = levelRandom();
        // 10 particles a tick: an explosion one time in five, else poof, each in the box from the
        // part's position to position + (width, height, width) (upstream's offset box), with gaussian
        // 0.02 velocities
        for (int k = 0; k < 10; ++k) {
            char const* id = r.nextInt(5) == 0 ? "minecraft:explosion" : "minecraft:poof";
            V3          at{p.pos.x + r.nextFloat() * size.width, p.pos.y + r.nextFloat() * size.height, p.pos.z + r.nextFloat() * size.width};
            V3          v{r.nextGaussian() * 0.02, r.nextGaussian() * 0.02, r.nextGaussian() * 0.02};
            particle(hydra, id, at, v);
        }
    }
    if (p.deathTime == 20) setPartActive(rt, idx, false);
}

/// Keeps the part actors' properties, names and rotations up to date.
void syncParts(Mob& hydra, Runtime& rt) {
    bool parentRed = rt.hurtTime > 0 || rt.deathTime > 0;
    // the body's own overlay (LivingEntityRenderer: hurtTime > 0 || deathTime > 0)
    if (int red = parentRed ? 1 : 0; red != rt.sentRed) {
        ge::setProperty(hydra, "gloamingforest:red", parentRed);
        rt.sentRed = red;
    }
    for (int idx = 0; idx < ch::kPartCount; ++idx) {
        auto const& h = rt.handles[idx];
        if (!h.valid()) continue;
        auto& p = rt.parts[idx];
        if (idx >= ch::kBodyParts) ge::ext::setPartRotation(h, p.yRot, p.xRot);
        Actor* a = ge::partActor(h);
        if (!a) continue;
        int active = p.active ? 1 : 0;
        if (active != p.sentActive) {
            ge::setProperty(*a, "gloamingforest:active", p.active);
            p.sentActive = active;
        }
        int red = (parentRed || p.hurtTime > 0 || p.deathTime > 0) ? 1 : 0;
        if (red != p.sentRed) {
            ge::setProperty(*a, "gloamingforest:red", red == 1);
            p.sentRed = red;
        }
        if (isHeadPart(idx)) {
            // GloamingPartRenderer#setupRotations: a dying head flips onto its side (the necks' flip is 0)
            int death = std::min(p.deathTime, 20);
            if (death != p.sentDeath) {
                ge::setProperty(*a, "gloamingforest:death", death);
                p.sentDeath = death;
            }
            int   head  = headOf(idx);
            float mouth = rt.heads[head].mouth;
            if (std::fabs(mouth - p.sentMouth) > 1e-4F) {
                ge::setProperty(*a, "gloamingforest:mouth", mouth);
                p.sentMouth = mouth;
            }
            auto const& name = rt.names[head];
            if (!p.nameSent || name != p.sentName) {
                // HydraHeadRenderer#shouldShowName: a named head always shows its name (the part's
                // nameable always_show; a name set by code leaves CanShowName off)
                a->setNameTag(name);
                SynchedActorDataAccess::setActorFlag(a->getEntityContext(), ActorFlags::CanShowName, !name.empty());
                p.sentName = name;
                p.nameSent = true;
            }
        }
    }
    (void)hydra;
}

// ---- persistence (HEAD_NAMES, NumHeads) ------------------------------------------------------------

void persist(Actor& hydra, Runtime const& rt) {
    std::array<ch::Head, ch::kMaxHeads> heads;
    for (int i = 0; i < ch::kMaxHeads; ++i) heads[i] = rt.heads[i].head;
    auto& doc        = ge::data(hydra, "hydra");
    doc              = ge::Json::object();
    doc["NumHeads"]  = ch::activeMask(heads);
    doc["HeadNames"] = ge::Json(std::vector<std::string>(rt.names.begin(), rt.names.end()));
    ge::ext::setStringList(hydra, "hydra:head_names", std::vector<std::string>(rt.names.begin(), rt.names.end()));
}

void applySaved(Runtime& rt, ge::Json const& doc) {
    if (!doc.is_object()) return;
    if (doc.contains("NumHeads") && doc["NumHeads"].is_number_integer()) {
        std::array<ch::Head, ch::kMaxHeads> heads;
        for (int i = 0; i < ch::kMaxHeads; ++i) heads[i] = rt.heads[i].head;
        ch::activateHeadsOnLoad(heads, static_cast<std::uint8_t>(doc["NumHeads"].get<int>()));
        for (int i = 0; i < ch::kMaxHeads; ++i) rt.heads[i].head = heads[i];
    }
    if (doc.contains("HeadNames") && doc["HeadNames"].is_array()) {
        auto const& names = doc["HeadNames"];
        for (std::size_t i = 0; i < names.size() && i < rt.names.size(); ++i)
            if (names[i].is_string()) rt.names[i] = names[i].get<std::string>();
    }
}

// ---- head geometry and facing ------------------------------------------------------------------------

void setHeadPosition(Actor& hydra, Runtime& rt, int i) {
    auto& hr = rt.heads[i];
    auto& hp = rt.parts[ch::headPartIndex(i)];
    hp.pos   = ch::headPosition(hr.head, hydraPos(hydra), rt.bodyYaw, rt.tickCount);
    // HydraHead#setMouthOpen
    hr.mouthLast = hr.mouth;
    hr.mouth     = hr.head.currentMouthOpen();
}

void setNeckPosition(Actor& hydra, Runtime& rt, int i) {
    auto&  hp    = rt.parts[ch::headPartIndex(i)];
    V3     start = ch::neckAnchor(i, hydraPos(hydra), rt.bodyYaw);
    auto   necks = ch::neckPositions(start, rt.bodyYaw, hp.pos, hp.yRot, hp.xRot);
    for (int k = 0; k < 5; ++k) {
        auto& n = rt.parts[ch::neckPartIndex(i, k)];
        n.pos   = necks[k].pos;
        n.yRot  = necks[k].yRot;
        n.xRot  = necks[k].xRot;
    }
}

void faceVec(Runtime& rt, int i, double x, double y, double z, float yawC, float pitchC) {
    auto& hp = rt.parts[ch::headPartIndex(i)];
    auto  f  = ch::faceVec(hp.pos, hp.yRot, hp.xRot, V3{x, y, z}, yawC, pitchC);
    hp.yRot  = f.yRot;
    hp.xRot  = f.xRot;
}

void faceEntity(Runtime& rt, int i, Actor& e, float yawC, float pitchC) {
    auto   feet = e.getFeetPos();
    double yTarget;
    if (fe::asMob(e) && fe::parentOfPart(e) == 0) yTarget = e.getEyePos().y;
    else {
        auto const& bb = e.getAABB();
        yTarget        = (bb.min.y + bb.max.y) / 2.0;
    }
    faceVec(rt, i, feet.x, yTarget, feet.z, yawC, pitchC);
    auto& hr = rt.heads[i];
    hr.tx    = feet.x;
    hr.ty    = feet.y;
    hr.tz    = feet.z;
}

void faceIdle(Actor& hydra, Runtime& rt, int i, float yawC, float pitchC) {
    float angle = (rt.yRot * 3.141593F) / 180.0F;
    V3    h     = hydraPos(hydra);
    faceVec(rt, i, h.x - core::jrand::mthSin(angle) * 30.0F, h.y + 3.0, h.z + core::jrand::mthCos(angle) * 30.0F, yawC, pitchC);
}

void moveTargetTowardsTarget(HeadRt& hr, double distance) {
    Actor* t = hr.target ? fe::actorById(hr.target) : nullptr;
    if (!t) return;
    auto   p = t->getFeetPos();
    double vx = p.x - hr.tx, vy = p.y - hr.ty, vz = p.z - hr.tz;
    double len = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (len < 1.0E-4) return; // Vec3#normalize: ZERO
    hr.tx += vx / len * distance;
    hr.ty += vy / len * distance;
    hr.tz += vz / len * distance;
}

void setHeadFacing(Actor& hydra, Runtime& rt, int i) {
    auto&  hr     = rt.heads[i];
    auto&  hp     = rt.parts[ch::headPartIndex(i)];
    Actor* target = hr.target ? fe::actorById(hr.target) : nullptr;
    float  pitchC = static_cast<float>(ch::kMaxHeadXRot);
    switch (hr.head.currentState) {
    case State::BiteReady: {
        if (target) faceEntity(rt, i, *target, 5.0F, pitchC);
        // head 0 and 1 max yaw; head 2 (upstream's max / min as written)
        float biteMaxYaw = -60.0F, biteMinYaw = -90.0F;
        if (i == 2) {
            biteMaxYaw = 60.0F;
            biteMinYaw = 90.0F;
        }
        float off = ch::wrapDegrees(hp.yRot - rt.bodyYaw);
        if (off > biteMaxYaw) hp.yRot = rt.bodyYaw + biteMaxYaw;
        if (off < biteMinYaw) hp.yRot = rt.bodyYaw + biteMinYaw;
        // the target point: 16 blocks ahead of where the head faces
        V3 look = ch::viewVector(hp.xRot, hp.yRot);
        hr.tx   = hp.pos.x + look.x * 16.0;
        hr.ty   = hp.pos.y + 1.5 + look.y * 16.0;
        hr.tz   = hp.pos.z + look.z * 16.0;
        break;
    }
    case State::Biting:
    case State::BiteEnding:
        if (target) faceEntity(rt, i, *target, 5.0F, pitchC);
        hp.xRot = clampPitch(hp.xRot + static_cast<float>(std::numbers::pi / 4.0)); // upstream adds PI / 4 to degrees
        break;
    case State::RoarRawr: faceVec(rt, i, hr.tx, hr.ty, hr.tz, 10.0F, pitchC); break;
    case State::Flaming:
    case State::FlameBeginning:
        moveTargetTowardsTarget(hr, hard() ? 0.1 : 0.04); // FLAME_BREATH_TRACKING_SPEED
        faceVec(rt, i, hr.tx, hr.ty, hr.tz, 5.0F, pitchC);
        break;
    default:
        if (!hr.head.isDead()) {
            if (target) faceEntity(rt, i, *target, 5.0F, pitchC);
            else faceIdle(hydra, rt, i, 1.5F, pitchC);
        }
        break;
    }
}

// ---- attacks -----------------------------------------------------------------------------------------

/// HydraPart#canEntityBeSeen: a COLLIDER clip between the eyes (the 4-tall head's eye at 3.4).
bool headCanSee(Actor& hydra, V3 headPos, Actor& target) {
    auto& region = hydra.getDimensionBlockSource();
    Vec3  a{static_cast<float>(headPos.x), static_cast<float>(headPos.y + 3.4), static_cast<float>(headPos.z)};
    auto  te = target.getEyePos();
    Vec3  b{target.getFeetPos().x, te.y, target.getFeetPos().z};
    auto  hr = region.clip(a, b, false, ShapeType::Collision, 200, false, false, nullptr, [](BlockSource const&, Block const&, bool) { return true; }, false);
    return hr.mType != HitResultType::Tile;
}

/// Direction#fromYRot of a yaw: (stepX, stepZ).
std::pair<int, int> stepOf(float yRot) {
    int d = static_cast<int>(std::floor(yRot / 90.0 + 0.5)) & 3;
    switch (d) {
    case 0: return {0, 1};  // SOUTH
    case 1: return {-1, 0}; // WEST
    case 2: return {0, -1}; // NORTH
    default: return {1, 0}; // EAST
    }
}

/// LivingEntity#knockback(strength, x, z).
void knockback(Actor& living, double strength, double x, double z) {
    if (strength <= 0) return;
    auto& r = levelRandom();
    while (x * x + z * z < 1.0E-5) {
        x = (r.nextDouble() - r.nextDouble()) * 0.01;
        z = (r.nextDouble() - r.nextDouble()) * 0.01;
    }
    double len = std::sqrt(x * x + z * z);
    double vx = x / len * strength, vz = z / len * strength;
    auto   d  = living.getPosDelta();
    double y  = living.isOnGround() ? std::min(0.4, d.y / 2.0 + strength) : d.y;
    fe::setMotion(living, Vec3{static_cast<float>(d.x / 2.0 - vx), static_cast<float>(y), static_cast<float>(d.z / 2.0 - vz)});
}

void bite(Mob& hydra, Runtime& rt, int i) {
    auto&  hp     = rt.parts[ch::headPartIndex(i)];
    auto&  region = hydra.getDimensionBlockSource();
    int    dim    = static_cast<int>(hydra.getDimensionId());
    AABB   b      = inflate(box(hp.pos, 4.0F, 4.0F), 0.0F, 1.0F, 0.0F);
    auto   hydraId = fe::uidOf(hydra);
    auto [sx, sz] = stepOf(hp.yRot);
    for (auto* e : fe::actorsIn(region, dim, b)) {
        if (!e || e == &hydra || !living(*e) || partOf(*e, hydraId)) continue;
        if (e->isPlayer()) {
            auto& player = static_cast<Player&>(*e);
            if (player.isBlocking()) {
                // the shield in use: the main hand when it holds one, else the off hand
                bool        main = game::items::idOf(player.getCarriedItem()) == "minecraft:shield"
                         || game::items::idOf(player.getCarriedItem()) == "gloamingforest:knightmetal_shield";
                ItemStack&  use  = const_cast<ItemStack&>(main ? player.getCarriedItem() : player.getOffhandSlot());
                HashedString category("shield");
                if (!player.isItemOnCooldown(category)) {
                    // cause severe damage and play a shatter sound
                    auto& r     = levelRandom();
                    float pitch = (r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F; // LivingEntity#getVoicePitch
                    gf::fx::sound(player.getDimension(), game::items::idOf(use) == "minecraft:shield" ? "item.gloamingforest.shield.shatter" : "item.gloamingforest.knightmetal_shield.shatter",
                                  player.getFeetPos(), 1.0F, pitch);
                    use.hurtAndBreak(112, &player);
                    if (main) player.setCarriedItem(use);
                    else player.setOffhandSlot(use);
                }
                // add cooldown and knockback
                player.startItemCooldown(category, 200, true);
                player.stopUsingItem();
                fe::push(player, Vec3{-sx * 0.5F, 0.15F, -sz * 0.5F});
            }
        }
        // bite it!
        gf::combat::hurt(*e, static_cast<float>(ch::kBiteDamage), "gloamingforest:hydra_bite", &hydra);
        // knockback (upstream passes the step as the strength and 0.1 as the x ratio)
        if (e->isPlayer()) fe::push(*e, Vec3{-sx * 0.5F, 0.1F, -sz * 0.5F});
        else knockback(*e, -sx, 0.1, -sz);
    }
    (void)rt;
}

/// HydraHeadContainer#getHeadLookTarget.
Actor* headLookTarget(Mob& hydra, Runtime& rt, int i) {
    auto&  hp     = rt.parts[ch::headPartIndex(i)];
    auto&  region = hydra.getDimensionBlockSource();
    int    dim    = static_cast<int>(hydra.getDimensionId());
    double range  = 30.0;
    V3     src{hp.pos.x, hp.pos.y + 1.0, hp.pos.z};
    V3     look = ch::viewVector(hp.xRot, hp.yRot);
    V3     dest{src.x + look.x * range, src.y + look.y * range, src.z + look.z * range};
    auto   hr = region.clip(toVec(src), toVec(dest), true, ShapeType::Outline, 200, false, false, nullptr, [](BlockSource const&, Block const&, bool) { return true; }, false);
    // a miss still has a block position: the end's
    BlockPos hit = hr.mType == HitResultType::Tile ? hr.mBlock : BlockPos{static_cast<int>(std::floor(dest.x)), static_cast<int>(std::floor(dest.y)), static_cast<int>(std::floor(dest.z))};
    double rx = std::min(range, std::fabs(hp.pos.x - hit.x));
    double ry = std::min(range, std::fabs(hp.pos.y - hit.y));
    double rz = std::min(range, std::fabs(hp.pos.z - hit.z));
    AABB   hb = box(hp.pos, 4.0F, 4.0F);
    AABB   moved{Vec3{static_cast<float>(hb.min.x + look.x * rx), static_cast<float>(hb.min.y + look.y * ry), static_cast<float>(hb.min.z + look.z * rz)},
               Vec3{static_cast<float>(hb.max.x + look.x * rx), static_cast<float>(hb.max.y + look.y * ry), static_cast<float>(hb.max.z + look.z * rz)}};
    AABB   search = inflate(moved, 3.0F, 3.0F, 3.0F);
    Actor* headActor = ge::partActor(rt.handles[ch::headPartIndex(i)]);
    Actor* skip[3]   = {ge::partActor(rt.handles[ch::neckPartIndex(i, 0)]), ge::partActor(rt.handles[ch::neckPartIndex(i, 1)]), ge::partActor(rt.handles[ch::neckPartIndex(i, 2)])};
    Actor* pointed = nullptr;
    double hitDist = 0;
    for (auto* e : fe::actorsIn(region, dim, search)) {
        if (!e || e == headActor || e == skip[0] || e == skip[1] || e == skip[2]) continue;
        bool isPart   = fe::parentOfPart(*e) != 0;
        bool isMortar = e->getTypeName() == kMortar; // HydraMortar#isPickable, #getPickRadius 1.5
        bool pickable = isPart || isMortar || (living(*e) && !e->isSpectator());
        if (!pickable) continue;
        float border = isMortar ? 1.5F : 0.0F;
        AABB  bb     = inflate(e->getAABB(), border, border, border);
        if (contains(bb, src)) {
            pointed = e;
            hitDist = 0.0;
        } else if (auto t = clipBox(bb, src, dest)) {
            double d = *t * range;
            if (d < hitDist || hitDist == 0.0) {
                pointed = e;
                hitDist = d;
            }
        }
    }
    return pointed;
}

void flame(Mob& hydra, Runtime& rt, int i) {
    Actor* target = headLookTarget(hydra, rt, i);
    if (!target || target == &hydra || partOf(*target, fe::uidOf(hydra))) return;
    if (!target->isFireImmune() && gf::combat::hurt(*target, static_cast<float>(ch::kFlameDamage), "gloamingforest:hydra_fire", &hydra))
        target->setOnFire(ch::kFlameBurnSeconds, true);
}

void executeAttacks(Mob& hydra, Runtime& rt, int i) {
    auto& hr = rt.heads[i];
    auto& hp = rt.parts[ch::headPartIndex(i)];
    if (hr.head.currentState == State::MortarShooting && hr.head.ticksProgress % 10 == 0) {
        Actor* target   = hr.target ? fe::actorById(hr.target) : nullptr;
        // launch blasting mortars if the player is hiding
        bool   blasting = target && !headCanSee(hydra, hp.pos, *target);
        auto&  r        = levelRandom();
        sound(hydra, hp.pos, "entity.gloamingforest.hydra.shoot", 10.0F, (r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F);
        launchMortar(hydra, i, toVec(hp.pos), hp.xRot, hp.yRot, blasting);
    }
    if (hr.head.currentState == State::Biting) bite(hydra, rt, i);
    if (hr.head.currentState == State::Flaming) flame(hydra, rt, i);
}

void playSounds(Actor& hydra, Runtime& rt, int i) {
    auto& hr = rt.heads[i];
    auto& hp = rt.parts[ch::headPartIndex(i)];
    auto& r  = levelRandom();
    if (hr.head.currentState == State::Flaming && hr.tickCount % 5 == 0) {
        float v = 0.5F + r.nextFloat();
        sound(hydra, hp.pos, "entity.gloamingforest.hydra.shoot_fire", v, r.nextFloat() * 0.7F + 0.3F);
    }
    if (hr.head.currentState == State::RoarRawr) sound(hydra, hp.pos, "entity.gloamingforest.hydra.roar", 1.25F, r.nextFloat() * 0.3F + 0.7F);
    if (hr.head.currentState == State::BiteReady && hr.head.ticksProgress == 60) {
        sound(hydra, hp.pos, "entity.gloamingforest.hydra.warn", 2.0F, r.nextFloat() * 0.3F + 0.7F);
        log().info("hydra: head {} warns before biting", i);
    }
}

/// HydraHeadContainer#addMouthParticles (client side in upstream, sent through gf::fx).
void mouthParticles(Actor& hydra, Runtime& rt, int i) {
    auto& hr = rt.heads[i];
    auto& hp = rt.parts[ch::headPartIndex(i)];
    V3    look = ch::viewVector(hp.xRot, hp.yRot);
    double dist = 3.5;
    V3    p{hp.pos.x + look.x * dist, hp.pos.y + 1.0 + look.y * dist, hp.pos.z + look.z * dist};
    auto& r  = levelRandom();
    auto  jitter = [&]() { return V3{p.x + r.nextDouble() - 0.5, p.y + r.nextDouble() - 0.5, p.z + r.nextDouble() - 0.5}; };
    switch (hr.head.currentState) {
    case State::FlameBeginning:
        particle(hydra, "minecraft:flame", jitter());
        particle(hydra, "minecraft:smoke", jitter());
        break;
    case State::Flaming:
        for (int k = 0; k < 5; ++k) {
            double spread   = 5.0 + r.nextDouble() * 2.5;
            double velocity = 1.0 + r.nextDouble();
            double dx = look.x + r.nextGaussian() * 0.0075 * spread;
            double dy = look.y + r.nextGaussian() * 0.0075 * spread;
            double dz = look.z + r.nextGaussian() * 0.0075 * spread;
            particle(hydra, "gloamingforest:large_flame", p, V3{dx * velocity, dy * velocity, dz * velocity});
        }
        break;
    case State::BiteBeginning:
    case State::BiteReady: particle(hydra, "minecraft:splash", jitter()); break;
    case State::MortarBeginning: particle(hydra, "minecraft:large_smoke", jitter()); break;
    default: break;
    }
}

void setHeadHurtTime(Runtime& rt, int i, int hurtTime) {
    rt.parts[ch::headPartIndex(i)].hurtTime = hurtTime;
    for (int k = 0; k < 5; ++k) rt.parts[ch::neckPartIndex(i, k)].hurtTime = hurtTime;
}

void animateHeadDeath(Actor& hydra, Runtime& rt, int i) {
    auto& h = rt.heads[i].head;
    if (h.currentState == State::Dying) {
        h.deathTime++;
        // mark parts as dead so they explode and die off
        if (h.deathTime == 1) {
            rt.parts[ch::headPartIndex(i)].markedDead = true;
            // the head died, so the name shouldn't persist
            rt.names[i] = "";
            persist(hydra, rt);
        } else if (h.deathTime % 10 == 0 && h.deathTime <= 50) {
            rt.parts[ch::neckPartIndex(i, h.deathTime / 10 - 1)].markedDead = true;
        }
        // turn necks red
        setHeadHurtTime(rt, i, 20);
    } else {
        h.deathTime = 0;
    }
}

/// HydraHeadContainer#tick (server side).
void tickHead(Mob& hydra, Runtime& rt, int i) {
    auto& hr = rt.heads[i];
    auto& h  = hr.head;
    // headEntity.tick() and the necks' ticks
    tickPart(hydra, rt, ch::headPartIndex(i));
    for (int k = 0; k < 5; ++k) tickPart(hydra, rt, ch::neckPartIndex(i, k));
    hr.tickCount++;
    // make sure this is set up
    bool headSized = rt.parts[ch::headPartIndex(i)].active;
    if (!h.isDead() && !headSized) {
        setPartActive(rt, ch::headPartIndex(i), true);
        for (int k = 0; k < 5; ++k) setPartActive(rt, ch::neckPartIndex(i, k), true);
    } else if (!h.isActive() && headSized) {
        setPartActive(rt, ch::headPartIndex(i), false);
        for (int k = 0; k < 5; ++k) setPartActive(rt, ch::neckPartIndex(i, k), false);
    }
    State before = h.currentState;
    if (h.advanceRespawnCounter()) log().info("hydra: head {} is born again", i);
    h.advanceHeadState();
    if (h.currentState != before) {
        if (h.currentState == State::Dead || h.currentState == State::Born || h.currentState == State::Dying) persist(hydra, rt);
    }
    setHeadPosition(hydra, rt, i);
    setHeadFacing(hydra, rt, i);
    executeAttacks(hydra, rt, i);
    playSounds(hydra, rt, i);
    mouthParticles(hydra, rt, i);
    animateHeadDeath(hydra, rt, i);
    setNeckPosition(hydra, rt, i);
}

// ---- the brain ---------------------------------------------------------------------------------------

/// Mob#lookAt(entity, maxYRotIncrease, maxXRotIncrease) on the runtime's rotation.
void lookAt(Actor& hydra, Runtime& rt, Actor& e, float maxY, float maxX) {
    auto   hp = hydra.getFeetPos();
    auto   ep = e.getFeetPos();
    double d0 = ep.x - hp.x;
    double d2 = ep.z - hp.z;
    double d1;
    if (living(e)) d1 = e.getEyePos().y - (hp.y + kEyeHeight);
    else {
        auto const& bb = e.getAABB();
        d1             = (bb.min.y + bb.max.y) / 2.0 - (hp.y + kEyeHeight);
    }
    double d3 = std::sqrt(d0 * d0 + d2 * d2);
    float  f  = static_cast<float>(std::atan2(d2, d0) * 180.0 / std::numbers::pi) - 90.0F;
    float  f1 = static_cast<float>(-(std::atan2(d1, d3) * 180.0 / std::numbers::pi));
    rt.xRot   = clampPitch(ch::rotlerp(rt.xRot, f1, maxX));
    rt.yRot   = ch::rotlerp(rt.yRot, f, maxY);
}

Player* nearestPlayer(Actor& hydra, double range) {
    Player* best  = nullptr;
    double  bestD = std::numeric_limits<double>::max();
    for (auto* p : fe::playersNear(static_cast<int>(hydra.getDimensionId()), hydra.getFeetPos(), range)) {
        if (!p || p->isSpectator()) continue;
        double d = fe::distanceSqr(hydra, *p);
        if (d < bestD) {
            bestD = d;
            best  = p;
        }
    }
    return best;
}

std::array<ch::Head, ch::kMaxHeads> headArray(Runtime const& rt) {
    std::array<ch::Head, ch::kMaxHeads> a;
    for (int i = 0; i < ch::kMaxHeads; ++i) a[i] = rt.heads[i].head;
    return a;
}

void attackEntity(Mob& hydra, Runtime& rt, Actor& target, float distance, fe::JavaRandom& rnd) {
    bool targetAbove = target.getAABB().min.y > hydra.getFeetPos().y + kHeight;
    bool isHard      = hard();
    // three main heads can do these kinds of attacks
    for (int i = 0; i < 3; ++i) {
        auto& h = rt.heads[i].head;
        if (h.isIdle() && !ch::tooManyAttacking(headArray(rt), i, isHard)) {
            if (distance > 4 && distance < 10 && rnd.nextInt(ch::kBiteChance) == 0 && ch::countActiveHeads(headArray(rt)) > 2
                && !ch::otherHeadsBiting(headArray(rt), i)) {
                h.setNextState(State::BiteBeginning);
            } else if (distance > 0 && distance < 20 && rnd.nextInt(ch::kFlameChance) == 0) {
                h.setNextState(State::FlameBeginning);
            } else if (distance > 8 && distance < 32 && !targetAbove && rnd.nextInt(ch::kMortarChance) == 0) {
                h.setNextState(State::MortarBeginning);
            }
        }
    }
    // heads 4-7 can do everything but bite
    for (int i = 3; i < ch::kMaxHeads; ++i) {
        auto& h = rt.heads[i].head;
        if (h.isIdle() && !ch::tooManyAttacking(headArray(rt), i, isHard)) {
            if (distance > 0 && distance < 20 && rnd.nextInt(ch::kFlameChance) == 0) h.setNextState(State::FlameBeginning);
            else if (distance > 8 && distance < 32 && !targetAbove && rnd.nextInt(ch::kMortarChance) == 0) h.setNextState(State::MortarBeginning);
        }
    }
}

bool anyHeadTargeting(Runtime const& rt, std::int64_t uid) {
    for (auto const& hr : rt.heads)
        if (hr.target == uid) return true;
    return false;
}

Actor* findSecondaryTarget(Mob& hydra, Runtime& rt, double range) {
    auto  p = hydra.getFeetPos();
    AABB  b{Vec3{static_cast<float>(p.x - range), static_cast<float>(p.y - range), static_cast<float>(p.z - range)},
           Vec3{static_cast<float>(p.x + 1 + range), static_cast<float>(p.y + 1 + range), static_cast<float>(p.z + 1 + range)}};
    Actor* best  = nullptr;
    double bestD = std::numeric_limits<double>::max();
    for (auto* e : fe::actorsIn(hydra.getDimensionBlockSource(), static_cast<int>(hydra.getDimensionId()), b)) {
        if (!e || e == &hydra || !living(*e) || e->getTypeName() == kHydra) continue;
        auto uid = fe::uidOf(*e);
        if (uid == rt.target || anyHeadTargeting(rt, uid)) continue;
        if (!fe::hasLineOfSight(hydra, *e)) continue;
        if (e->isPlayer() && (e->isCreative() || e->isSpectator())) continue; // NO_CREATIVE_OR_SPECTATOR
        double d = fe::distanceSqr(hydra, *e);
        if (d < bestD) {
            bestD = d;
            best  = e;
        }
    }
    return best;
}

double distanceSqXZ(V3 a, Vec3 const& b) {
    double dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

void secondaryAttacks(Mob& hydra, Runtime& rt, fe::JavaRandom& rnd) {
    Actor* sec = findSecondaryTarget(hydra, rt, 20.0);
    if (!sec) return;
    float distance = std::sqrt(static_cast<float>(fe::distanceSqr(*sec, hydra)));
    for (int i = 1; i < ch::kMaxHeads; ++i) {
        auto& hr = rt.heads[i];
        if (hr.head.isDead() || !hr.head.isIdle()) continue;
        // isTargetOnThisSide: heads don't attack across the whole body
        if (distanceSqXZ(rt.parts[ch::headPartIndex(i)].pos, sec->getFeetPos()) >= distanceSqXZ(hydraPos(hydra), sec->getFeetPos())) continue;
        if (distance > 0 && distance < 20 && rnd.nextInt(ch::kSecondaryFlameChance) == 0) {
            hr.target                    = fe::uidOf(*sec);
            hr.head.isSecondaryAttacking = true;
            hr.head.setNextState(State::FlameBeginning);
        } else if (distance > 8 && distance < 32 && rnd.nextInt(ch::kSecondaryMortarChance) == 0) {
            hr.target                    = fe::uidOf(*sec);
            hr.head.isSecondaryAttacking = true;
            hr.head.setNextState(State::MortarBeginning);
        }
    }
}

/// `be instanceof Container` of Java's block entities (BaseContainerBlockEntity, the
/// ContainerSingleItem jukebox / decorated pot, the chiseled bookshelf). Bedrock's item frames are
/// block actors where Java's are entities that destroyBlock never touches: they are spared too.
bool isContainer(BlockActor const& be) {
    switch (be.getType()) {
    case BlockActorType::Furnace:
    case BlockActorType::BlastFurnace:
    case BlockActorType::Smoker:
    case BlockActorType::Chest:
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::Hopper:
    case BlockActorType::ShulkerBox:
    case BlockActorType::BarrelBlock:
    case BlockActorType::BrewingStand:
    case BlockActorType::Crafter:
    case BlockActorType::Jukebox:
    case BlockActorType::ChiseledBookshelf:
    case BlockActorType::DecoratedPot:
    case BlockActorType::ItemFrame:
    case BlockActorType::GlowItemFrame: return true;
    default: return false;
    }
}

/// EntityUtil#canDestroyBlock + Level#destroyBlock(pos, false) over WorldUtil#getAllInBB(box).
void destroyBlocksInAABB(Mob& hydra, Runtime& rt, AABB const& b) {
    if (rt.deathTime > 0 || !mobGriefing()) return;
    auto  level = ll::service::getLevel();
    if (!level) return;
    auto& region = hydra.getDimensionBlockSource();
    // BlockPos.betweenClosed((int) min, (int) max)
    BlockPos lo{static_cast<int>(b.min.x), static_cast<int>(b.min.y), static_cast<int>(b.min.z)};
    BlockPos hi{static_cast<int>(b.max.x), static_cast<int>(b.max.y), static_cast<int>(b.max.z)};
    if (!region.hasChunksAt(lo, 0, false) || !region.hasChunksAt(hi, 0, false)) return;
    for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z) {
                BlockPos     pos{x, y, z};
                Block const& block    = region.getBlock(pos);
                float        hardness = block.getBlockType().getDestroySpeed();
                if (block.isAir() || hardness < 0.0F || hardness >= 50.0F) continue;
                // EntityUtil#canDestroyBlock spares only containers (signs, banners, beds, skulls and
                // spawners are destroyed); custom containers have no block entity: gf::inventory knows them
                if (auto* be = region.getBlockEntity(pos); be && isContainer(*be)) continue;
                if (block.getBlockType().mNameInfo->mNamespaceName.get() != "minecraft" && gf::inventory::find(region, pos)) continue;
                BlockChangeContext ctx{};
                level->destroyBlock(region, pos, false, ctx);
            }
}

/// isUnsteadySurfaceBeneath: less than 60 % of the layer under the (Java) box is solid.
bool unsteadySurfaceBeneath(Mob& hydra) {
    auto& region = hydra.getDimensionBlockSource();
    auto  p      = hydra.getFeetPos();
    int   minX = static_cast<int>(std::floor(p.x - kHalfWidth)), maxX = static_cast<int>(std::floor(p.x + kHalfWidth));
    int   minZ = static_cast<int>(std::floor(p.z - kHalfWidth)), maxZ = static_cast<int>(std::floor(p.z + kHalfWidth));
    int   dy   = static_cast<int>(std::floor(p.y)) - 1;
    int   solid = 0, total = 0;
    for (int x = minX; x <= maxX; ++x)
        for (int z = minZ; z <= maxZ; ++z) {
            total++;
            if (fe::legacySolid(region, BlockPos{x, dy, z})) solid++;
        }
    return static_cast<float>(solid) / static_cast<float>(total) < 0.6F;
}

/// The Java hydra's 16 x 12 box rests on any block under its footprint; the Bedrock actor's box is
/// 2 x 3. Hydra#isPickable is false: the client's picking skips the actor through its near-empty
/// minecraft:custom_hit_test, but F8's simulated projectiles (Projectile.cpp canHit: every mob is
/// pickable; the hit test uses getAABB) would stop on a 16 x 12 box, and nearly every head and
/// neck of the hydra lies inside that box: a seeker or ice arrow aimed at a head would deflect off
/// the body. While a block under the Java footprint holds the box the actor has no gravity, so it
/// never drops into a gap or a hole the Java hydra would stand over.
void restOnJavaBox(Mob& hydra, Runtime& rt) {
    auto&  region = hydra.getDimensionBlockSource();
    auto   p      = hydra.getFeetPos();
    int    y      = static_cast<int>(std::floor(p.y - 0.01));
    float  top    = -1.0E9F; // the highest collision top in the layer under the footprint
    if (y >= region.getMinHeight() && y < region.getMaxHeight()) {
        int minX = static_cast<int>(std::floor(p.x - kHalfWidth)), maxX = static_cast<int>(std::floor(p.x + kHalfWidth - 1.0E-4));
        int minZ = static_cast<int>(std::floor(p.z - kHalfWidth)), maxZ = static_cast<int>(std::floor(p.z + kHalfWidth - 1.0E-4));
        for (int x = minX; x <= maxX; ++x)
            for (int z = minZ; z <= maxZ; ++z) {
                BlockPos     at{x, y, z};
                Block const& b = region.getBlock(at);
                if (b.isAir()) continue;
                AABB s = b.getBlockType().getCollisionShape(b, region, at, nullptr);
                if (s.max.x - s.min.x <= 1e-6F || s.max.y - s.min.y <= 1e-6F || s.max.z - s.min.z <= 1e-6F) continue;
                top = std::max(top, s.max.y);
            }
    }
    bool held = top >= p.y - 0.02F;
    // a fall that went past the top in one tick lands on it (Java's collision stops the box there)
    if (held && top > p.y + 1.0E-3F && top - p.y < 1.0F) hydra.teleport(Vec3{p.x, top, p.z}, hydra.getDimensionId());
    bool gravity = !held;
    if (gravity != rt.gravity) {
        SynchedActorDataAccess::setActorFlag(hydra.getEntityContext(), ActorFlags::HasGravity, gravity);
        rt.gravity = gravity;
    }
    if (held && hydra.getPosDelta().y < 0.0F) {
        auto d = hydra.getPosDelta();
        fe::setMotion(hydra, Vec3{d.x, 0.0F, d.z});
    }
}

/// Hydra#collideWithEntities (EnderDragon#knockBack).
void collideWithEntities(Mob& hydra, Runtime& rt, int idx) {
    auto  size = fullSize(idx);
    AABB  b    = box(rt.parts[idx].pos, size.width, size.height);
    double d0  = (b.min.x + b.max.x) / 2.0;
    double d1  = (b.min.z + b.max.z) / 2.0;
    auto  uid  = fe::uidOf(hydra);
    for (auto* e : fe::actorsIn(hydra.getDimensionBlockSource(), static_cast<int>(hydra.getDimensionId()), b)) {
        if (!e || e == &hydra || partOf(*e, uid)) continue;
        if (e->isPlayer() && e->isCreative()) continue;
        if (!living(*e)) continue;
        auto   p  = e->getFeetPos();
        double d2 = p.x - d0;
        double d3 = p.z - d1;
        double d4 = std::max(d2 * d2 + d3 * d3, 0.1);
        fe::push(*e, Vec3{static_cast<float>(d2 / d4 * 8.0), 0.2F, static_cast<float>(d3 / d4 * 8.0)});
    }
}

int randomDeadHead(Runtime const& rt, fe::JavaRandom& rnd) {
    std::vector<int> ids;
    for (int i = 0; i < ch::kMaxHeads; ++i)
        if (rt.heads[i].head.canRespawn()) ids.push_back(i);
    return ids.empty() ? -1 : ids[rnd.nextInt(static_cast<int>(ids.size()))];
}

/// Hydra#customServerAiStep.
void brain(Mob& hydra, Runtime& rt) {
    auto& rnd = *rt.random;
    if (rt.ticksSinceDamaged > ch::kTicksBeforeHeal && rt.ticksSinceDamaged % 5 == 0) hydra.heal(1);
    // kill heads that have taken too much damage
    for (int i = 0; i < ch::kMaxHeads; ++i) {
        auto& h = rt.heads[i].head;
        if (!h.isDead() && h.damageTaken > ch::kHeadMaxDamage) {
            h.setNextState(State::Dying);
            h.endCurrentAction();
            // set this head and a random dead head to respawn
            h.respawnCounter = ch::kHeadRespawnTicks;
            int other        = randomDeadHead(rt, rnd);
            if (other != -1) rt.heads[other].head.respawnCounter = ch::kHeadRespawnTicks;
            log().info("hydra: head {} slain (damage {}), head {} will grow back too", i, h.damageTaken, other);
        }
    }
    if (!rt.brain) return;
    if (rnd.nextFloat() < 0.7F) {
        Player* p = nearestPlayer(hydra, 48.0);
        if (p && !p->isCreative()) {
            rt.target = fe::uidOf(*p);
            rt.chase  = 100 + rnd.nextInt(20);
        } else {
            rt.randomYawVelocity = (rnd.nextFloat() - 0.5F) * 20.0F;
        }
    }
    // destroy blocks
    destroyBlocksInAABB(hydra, rt, box(rt.parts[0].pos, 6.0F, 6.0F));
    destroyBlocksInAABB(hydra, rt, box(rt.parts[1].pos, 6.0F, 2.0F));
    for (int i = 0; i < ch::kMaxHeads; ++i)
        if (!rt.heads[i].head.isDead()) destroyBlocksInAABB(hydra, rt, box(rt.parts[ch::headPartIndex(i)].pos, 4.0F, 4.0F));
    // smash blocks beneath us too
    if (rt.tickCount % 20 == 0 && unsteadySurfaceBeneath(hydra)) {
        V3 p = hydraPos(hydra);
        p.y -= 1.0;
        destroyBlocksInAABB(hydra, rt, box(p, kHalfWidth * 2, kHeight));
    }
    Actor* target = rt.target ? fe::actorById(rt.target) : nullptr;
    if (rt.target && !target) rt.target = 0;
    if (target) {
        lookAt(hydra, rt, *target, 10.0F, static_cast<float>(ch::kMaxHeadXRot));
        // have any heads not currently attacking switch to the primary target
        for (auto& hr : rt.heads)
            if (!hr.head.isAttacking() && !hr.head.isSecondaryAttacking) hr.target = rt.target;
        // let's pick an attack
        if (target->isAlive()) {
            float distance = std::sqrt(static_cast<float>(fe::distanceSqr(*target, hydra)));
            if (fe::hasLineOfSight(hydra, *target)) attackEntity(hydra, rt, *target, distance, rnd);
        }
        if (rt.chase-- <= 0 || !target->isAlive() || fe::distanceSqr(*target, hydra) > 48.0 * 48.0) rt.target = 0;
    } else {
        if (rnd.nextFloat() < 0.05F) rt.randomYawVelocity = (rnd.nextFloat() - 0.5F) * 20.0F;
        rt.yRot += rt.randomYawVelocity;
        rt.xRot = 0;
        // set idle heads to no target
        for (auto& hr : rt.heads)
            if (hr.head.isIdle()) hr.target = 0;
    }
    // heads that are free at this point may consider attacking secondary targets
    secondaryAttacks(hydra, rt, rnd);
}

// ---- the tick ----------------------------------------------------------------------------------------

Runtime& createRuntime(Mob& hydra) {
    auto  uid = fe::uidOf(hydra);
    auto& rt  = S().runtimes[uid];
    rt        = Runtime{};
    for (int i = 0; i < ch::kMaxHeads; ++i) rt.heads[i].head = ch::Head(i, i < 3);
    std::int64_t seed = fe::freshSeed();
    if (auto it = S().pendingSeed.find(uid); it != S().pendingSeed.end()) {
        seed = it->second;
        S().pendingSeed.erase(it);
    }
    rt.random  = std::make_unique<fe::JavaRandom>(seed);
    rt.yRot    = hydra.getRotation().y;
    rt.bodyYaw = rt.yRot;
    if (auto it = S().pendingLoad.find(uid); it != S().pendingLoad.end()) {
        applySaved(rt, it->second);
        S().pendingLoad.erase(it);
    } else if (auto const& doc = ge::data(hydra, "hydra"); doc.is_object()) {
        applySaved(rt, doc);
    }
    // the constructor's positions (setHeadPosition / setNeckPosition)
    V3 h = hydraPos(hydra);
    rt.parts[0].pos = ch::bodyPosition(h, rt.bodyYaw);
    rt.parts[1].pos = ch::tailPosition(h, rt.bodyYaw);
    for (int i = 0; i < ch::kMaxHeads; ++i) {
        setHeadPosition(hydra, rt, i);
        setNeckPosition(hydra, rt, i);
        bool active = rt.heads[i].head.isActive();
        rt.parts[ch::headPartIndex(i)].active = active;
        for (int k = 0; k < 5; ++k) rt.parts[ch::neckPartIndex(i, k)].active = active;
    }
    return rt;
}

void addParts(Mob& hydra, Runtime& rt) {
    auto uid = fe::uidOf(hydra);
    for (int idx = 0; idx < ch::kPartCount; ++idx) {
        auto size = rt.parts[idx].active ? fullSize(idx) : kInactiveSize;
        rt.handles[idx] = ge::addPart(hydra, partId(idx), size, [uid, idx](Mob& parent) {
            auto* r = find(uid);
            if (!r) return Vec3{0, 0, 0};
            auto f = parent.getFeetPos();
            auto p = r->parts[idx].pos;
            return Vec3{static_cast<float>(p.x - f.x), static_cast<float>(p.y - f.y), static_cast<float>(p.z - f.z)};
        });
    }
}

void onSpawn(Actor& actor) {
    auto* mob = fe::asMob(actor);
    if (!mob) return;
    auto& rt = createRuntime(*mob);
    addParts(*mob, rt);
    persist(actor, rt);
    syncParts(*mob, rt);
    log().info("hydra: spawned at {:.1f} {:.1f} {:.1f} with {} heads", actor.getFeetPos().x, actor.getFeetPos().y, actor.getFeetPos().z,
               ch::countActiveHeads(headArray(rt)));
}

void applyRotation(Mob& hydra, Runtime const& rt) {
    // the rendered body faces yBodyRot (the Java yRot is kept in the runtime for the brain)
    const_cast<Vec2&>(hydra.getRotation()) = Vec2{rt.xRot, rt.bodyYaw};
    hydra.setYBodyRotation(rt.bodyYaw);
    hydra.setYHeadRot(rt.bodyYaw);
}

void tickHydra(Mob& hydra, Runtime& rt) {
    bool alive = hydra.isAlive() && hydra.getHealth() > 0;
    // Mob#baseTick: the ambient growl (getAmbientSoundInterval 80, volume 2, the voice pitch)
    if (alive && rt.random->nextInt(1000) < rt.ambientSoundTime++) {
        rt.ambientSoundTime = -80;
        float pitch         = (rt.random->nextFloat() - rt.random->nextFloat()) * 0.2F + 1.0F;
        sound(hydra, hydraPos(hydra), "entity.gloamingforest.hydra.growl", 2.0F, pitch);
    }
    rt.tickCount++;
    // update all heads
    for (int i = 0; i < ch::kMaxHeads; ++i) tickHead(hydra, rt, i);
    if (rt.hurtTime > 0)
        for (int i = 0; i < ch::kMaxHeads; ++i) setHeadHurtTime(rt, i, rt.hurtTime);
    rt.ticksSinceDamaged++;
    if (alive) brain(hydra, rt);
    restOnJavaBox(hydra, rt);
    // set body part positions: the body goes behind the actual position of the hydra
    V3 h = hydraPos(hydra);
    rt.parts[0].pos  = ch::bodyPosition(h, rt.bodyYaw);
    rt.parts[0].yRot = rt.bodyYaw;
    rt.parts[1].pos  = ch::tailPosition(h, rt.bodyYaw);
    rt.parts[1].yRot = rt.bodyYaw;
    if (rt.hurtTime == 0 && alive) {
        collideWithEntities(hydra, rt, 0);
        collideWithEntities(hydra, rt, 1);
    }
    if (rt.hurtTime > 0) rt.hurtTime--;
    // LivingEntity#baseTick
    if (rt.invulnerableTime > 0) rt.invulnerableTime--;
    // LivingEntity#tick after aiStep: the body turns (Hydra#tickHeadTurn)
    rt.bodyYaw = ch::tickBodyTurn(rt.bodyYaw, rt.yRot);
    applyRotation(hydra, rt);
    // Hydra#tickDeathAnimation (client side in upstream): 10 particles a tick around the body
    if (rt.deathTime > 0) {
        auto& r = levelRandom();
        for (int k = 0; k < 10; ++k) {
            V3          v{r.nextGaussian() * 0.02, r.nextGaussian() * 0.02, r.nextGaussian() * 0.02};
            char const* id = r.nextInt(2) == 0 ? "minecraft:explosion" : "minecraft:poof";
            V3          at{h.x + r.nextFloat() * 6.0F * 2.0F - 6.0F, h.y + r.nextFloat() * 6.0F, h.z + r.nextFloat() * 6.0F * 2.0F - 6.0F};
            particle(hydra, id, at, v);
        }
    }
    syncParts(hydra, rt);
}

void aiStep(Actor& actor) {
    auto* mob = fe::asMob(actor);
    if (!mob) return;
    auto* rt = find(fe::uidOf(actor));
    if (!rt) {
        onSpawn(actor);
        rt = find(fe::uidOf(actor));
        if (!rt) return;
    }
    auto now = fe::currentTick();
    if (rt->lastTick == now) return;
    rt->lastTick = now;
    tickHydra(*mob, *rt);
}

/// Hydra#attackEntityFromPart, Hydra#hurt and #isInvulnerableTo.
bool hurt(Actor& self, float& amount, std::string const& type, Actor* attacker) {
    // read once: a later hit never sees this one's direct entity (noteDirect)
    // (only a note for this very hydra in this very tick: a hit a handler cancelled between
    // noteDirect and this dispatch leaves a note that a later hit never reads)
    std::int64_t const directId    = std::exchange(S().hurtDirect, 0);
    bool const         noteMatches = std::exchange(S().hurtVictim, 0) == fe::uidOf(self) && S().hurtTick == fe::currentTick();
    std::string const& part     = ge::ext::hurtPart();
    if (part.empty()) {
        // the hydra itself only takes damage that bypasses invulnerability (/kill)
        return type == "minecraft:generic_kill" || type == "minecraft:out_of_world"
            || gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_invulnerability", type);
    }
    auto* rt = find(fe::uidOf(self));
    if (!rt) return true;
    // a fire-immune boss ignores fire, except its own mortars (Hydra#isInvulnerableTo, checked by
    // super.hurt): the hit is refused below, after the head counted it (attackEntityFromPart calls
    // headCon.addDamage whatever super.hurt returns)
    bool const fireRefused = type != "gloamingforest:hydra_mortar" && fe::isFireDamage(type);
    // explosions reach the parts (NeoForge's Level#getEntities returns PartEntity instances): a
    // player's TNT, a creeper, a reflected mortar; the hydra's own mortars are refused just below
    // (their causing entity is the hydra)
    auto   uid    = fe::uidOf(self);
    // DamageSource#getEntity (attacker) / #getDirectEntity (the note of this hit)
    Actor* direct = directId && noteMatches ? fe::actorById(directId) : nullptr;
    // environmental fire (lava, a fire block, burning) never reaches a part in upstream: the parts share
    // the hydra's fire-immune type, so Entity#lavaHurt, BaseFireBlock#entityInside and the burning
    // tick skip them; only an entity's fire hit (a fireball) gets to attackEntityFromPart
    if (fireRefused && !attacker && !direct) return false;
    // the explosion of a mortar (Explosion#getIndirectSourceEntity): caused by the mortar's owner (the
    // hydra for its own mortars, the player who reflected one), the mortar is the direct entity
    if (attacker && attacker->getTypeName() == kMortar) {
        if (!direct) direct = attacker;
        auto mv  = mortarView(*attacker);
        attacker = mv && mv->owner ? fe::actorById(mv->owner) : nullptr;
    }
    if (attacker && (attacker == &self || partOf(*attacker, uid))) return false;
    if (direct && (direct == &self || partOf(*direct, uid))) return false;
    int idx     = partIndex(part);
    int headCon = -1;
    if (idx >= 0 && isHeadPart(idx)) headCon = headOf(idx);
    else if (idx >= 0 && headOf(idx) >= 0 && rt->heads[headOf(idx)].head.isDead()) return false; // a dead head's neck
    double range = attacker ? fe::distanceSqr(self, *attacker) : -1.0;
    // give some leeway for reflected mortars: source.getDirectEntity() instanceof HydraMortar (the
    // mortar's own damage and the explosion it causes both have the mortar as direct entity)
    bool fromMortar = (direct && direct->getTypeName() == kMortar) || type == "gloamingforest:hydra_mortar";
    if (range > 400.0 + (fromMortar ? 200.0 : 0.0)) return false;
    // ignore hits on dying heads, it's weird
    if (headCon >= 0 && rt->heads[headCon].head.isDead()) return false;
    if (headCon >= 0 && rt->heads[headCon].head.currentMouthOpen() > 0.5F) {
        rt->heads[headCon].head.addDamage(amount);
    } else {
        int armored = static_cast<int>(std::lround(amount / ch::kArmorMultiplier));
        amount      = static_cast<float>(armored);
        if (headCon >= 0) rt->heads[headCon].head.addDamage(static_cast<float>(armored));
    }
    // super.hurt refuses fire before its invulnerability window (no state changes, no tookDamage)
    if (fireRefused) return false;
    // super.hurt (LivingEntity#hurt): tookDamage. Inside the second half of the 20-tick
    // invulnerability window only a stronger hit lands (for the difference, without a new hurt
    // flash); outside it every hit lands, a hit of 0 too (the flash, the heal timer)
    bool took = true;
    if (rt->invulnerableTime > 10 && !gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_cooldown", type)) {
        took = amount > rt->lastHurt;
        if (took) rt->lastHurt = amount;
    } else {
        rt->lastHurt         = amount;
        rt->invulnerableTime = 20;
        rt->hurtTime         = kHurtTime;
    }
    if (!took) return false;
    rt->ticksSinceDamaged = 0;
    // the engine is not asked to apply nothing (its own window applies the difference)
    if (amount <= 0.0F) return false;
    rt->damageTaken += amount;
    return true;
}

/// Records the direct entity of damage on its way to a hydra (read by hurt, which the contract's
/// Behaviour::hurt calls with the causing entity only). Runs after F8's part forwarding (High) and
/// before F8's Behaviour::hurt dispatch (Normal).
gf::combat::Verdict noteDirect(gf::combat::DamageInfo& d) {
    if (d.victim.getTypeName() != kHydra) return gf::combat::Verdict::Pass;
    S().hurtDirect = d.direct ? fe::uidOf(*d.direct) : 0;
    S().hurtVictim = fe::uidOf(d.victim);
    S().hurtTick   = fe::currentTick();
    return gf::combat::Verdict::Pass;
}

/// HydraHead#interact (a neck forwards to its head): a named name tag names the head.
ia::Result nameTag(ia::EntityContext const& c) {
    if (c.itemId != "minecraft:name_tag") return ia::Result::Pass;
    std::string name = c.item.getCustomName();
    if (name.empty()) return ia::Result::Pass;
    Actor* parent = ge::ext::parentOf(c.entity);
    if (!parent || parent->getTypeName() != kHydra || !parent->isAlive()) return ia::Result::Pass;
    auto* rt = find(fe::uidOf(*parent));
    if (!rt) return ia::Result::Pass;
    int idx  = -1;
    for (int k = 0; k < ch::kPartCount; ++k)
        if (rt->handles[k].valid() && ge::partActor(rt->handles[k]) == &c.entity) idx = k;
    int head = idx >= 0 ? headOf(idx) : -1;
    if (head < 0) return ia::Result::Pass; // the body and the tail are HydraSmallParts: no naming
    rt->names[head] = name;
    persist(*parent, *rt);
    if (!c.player.isCreative()) ia::mutableItem(c.item).remove(1); // stack.shrink(1); Consume resends the slot
    log().info("hydra: head {} named {}", head, name);
    return ia::Result::Consume;
}

} // namespace

// ---- selftest access -------------------------------------------------------------------------------

std::optional<HeadView> headView(Actor& hydra, int head) {
    auto* rt = find(fe::uidOf(hydra));
    if (!rt || head < 0 || head >= ch::kMaxHeads) return std::nullopt;
    auto const& hr = rt->heads[head];
    auto const& hp = rt->parts[ch::headPartIndex(head)];
    HeadView    v;
    v.state          = hr.head.currentState;
    v.damageTaken    = hr.head.damageTaken;
    v.respawnCounter = hr.head.respawnCounter;
    v.pos            = toVec(hp.pos);
    v.yRot           = hp.yRot;
    v.xRot           = hp.xRot;
    v.mouth          = hr.mouth;
    v.active         = hp.active;
    v.name           = rt->names[head];
    return v;
}

std::vector<Vec3> partPositions(Actor& hydra) {
    std::vector<Vec3> out;
    auto*             rt = find(fe::uidOf(hydra));
    if (!rt) return out;
    for (auto const& h : rt->handles) {
        Actor* a = h.valid() ? ge::partActor(h) : nullptr;
        out.push_back(a ? a->getFeetPos() : Vec3{0, -1000, 0});
    }
    return out;
}

std::vector<Vec3> expectedPartPositions(Actor& hydra) {
    std::vector<Vec3> out;
    auto*             rt = find(fe::uidOf(hydra));
    if (!rt) return out;
    for (auto const& p : rt->parts) out.push_back(toVec(p.pos));
    return out;
}

void forceHeadState(Actor& hydra, int head, State state) {
    auto* rt = find(fe::uidOf(hydra));
    if (!rt || head < 0 || head >= ch::kMaxHeads) return;
    rt->heads[head].head.setNextState(state);
    rt->heads[head].head.endCurrentAction();
}

void addHeadDamage(Actor& hydra, int head, int damage) {
    auto* rt = find(fe::uidOf(hydra));
    if (!rt || head < 0 || head >= ch::kMaxHeads) return;
    rt->heads[head].head.damageTaken += damage;
}

void setTarget(Actor& hydra, Actor* target) {
    auto* rt = find(fe::uidOf(hydra));
    if (!rt) return;
    rt->target = target ? fe::uidOf(*target) : 0;
    for (auto& hr : rt->heads) hr.target = rt->target;
}

void setBrainEnabled(Actor& hydra, bool enabled) {
    if (auto* rt = find(fe::uidOf(hydra))) rt->brain = enabled;
}

void aimHeadForTest(Actor& hydra, int head, Vec3 const& point) {
    auto* rt = find(fe::uidOf(hydra));
    if (!rt || head < 0 || head >= ch::kMaxHeads) return;
    auto& hr  = rt->heads[head];
    hr.target = 0;
    hr.tx     = point.x;
    hr.ty     = point.y;
    hr.tz     = point.z;
}

void seedForTest(Actor& hydra, std::int64_t seed) {
    if (auto* rt = find(fe::uidOf(hydra))) rt->random = std::make_unique<fe::JavaRandom>(seed);
    else S().pendingSeed[fe::uidOf(hydra)] = seed;
}

bool hasGravityForTest(Actor& hydra) {
    auto* rt = find(fe::uidOf(hydra));
    return !rt || rt->gravity;
}

float damageTakenForTest(Actor& hydra) {
    auto* rt = find(fe::uidOf(hydra));
    return rt ? rt->damageTaken : 0.0F;
}

int ticksSinceDamagedForTest(Actor& hydra) {
    auto* rt = find(fe::uidOf(hydra));
    return rt ? rt->ticksSinceDamaged : -1;
}

std::string partIdOf(Actor& part) {
    Actor* parent = ge::ext::parentOf(part);
    if (!parent) return {};
    auto* rt = find(fe::uidOf(*parent));
    if (!rt) return {};
    for (int k = 0; k < ch::kPartCount; ++k)
        if (rt->handles[k].valid() && ge::partActor(rt->handles[k]) == &part) return partId(k);
    return {};
}

// ---- registration ----------------------------------------------------------------------------------

void installHydra() {
    ge::registerType(kHydra, {
                                 .onSpawn = onSpawn,
                                 .aiStep  = aiStep,
                                 .hurt    = hurt,
                                 .save =
                                     [](Actor& self, ge::Json& out) {
                                         if (auto* rt = find(fe::uidOf(self))) {
                                             persist(self, *rt);
                                             out = ge::data(self, "hydra");
                                         }
                                     },
                                 .load =
                                     [](Actor& self, ge::Json const& in) {
                                         if (auto* rt = find(fe::uidOf(self))) applySaved(*rt, in);
                                         else S().pendingLoad[fe::uidOf(self)] = in;
                                     },
                             });
    gf::boss::registerBoss({
        .entityId        = kHydra,
        .homeStructure   = kLair,
        .lootTable       = "gloamingforest:entities/hydra",
        .deathChestBlock = "gloamingforest:mangrove_chest",
        .bar             = {.rgb = ch::kBarColor},
        .deathTicks      = 200,
    });
    gf::boss::ext::setHomeRadius(kHydra, ch::kHomeRadius);
    gf::boss::ext::setLifecycle(kHydra, {
                                             .tickDeath =
                                                 [](Mob& boss, int deathTime) {
                                                     auto* rt = find(fe::uidOf(boss));
                                                     if (!rt) return;
                                                     rt->deathTime = deathTime;
                                                     // stop any head actions on death
                                                     if (deathTime == 1) {
                                                         for (int i = 0; i < ch::kMaxHeads; ++i) {
                                                             auto& h          = rt->heads[i].head;
                                                             h.respawnCounter = -1;
                                                             if (!h.isDead()) {
                                                                 h.setNextState(State::Idle);
                                                                 h.endCurrentAction();
                                                                 setHeadHurtTime(*rt, i, 200);
                                                             }
                                                         }
                                                         log().info("hydra: dying");
                                                     }
                                                     // heads die off one by one
                                                     if (deathTime <= 140 && deathTime % 20 == 0) {
                                                         int   headToDie = deathTime / 20 - 1;
                                                         auto& h         = rt->heads[headToDie].head;
                                                         if (!h.isDead()) {
                                                             h.setNextState(State::Dying);
                                                             h.endCurrentAction();
                                                         }
                                                     }
                                                 },
                                         });
    gf::boss::registerSpawner({
        .blockId  = kSpawner,
        .entityId = kHydra,
        .range    = 50, // BossSpawnerBlockEntity.LONG_RANGE
        .particle = "minecraft:flame",
    });
    S().handlers.push_back(ia::onInteract({.block = kHydraPart}, nameTag, ia::priority::High + 10));
    // the contract has no removal: once per process
    if (!std::exchange(S().combatHooked, true)) gf::combat::onIncoming(gf::combat::priority::Normal + 1, noteDirect);
    // forget the runtimes of hydras that are gone (every 100 ticks)
    if (!S().levelTick)
        S().levelTick = fe::onLevelTick([] {
            if (fe::currentTick() % 100 != 0) return;
            std::erase_if(S().runtimes, [](auto const& kv) { return fe::actorById(kv.first) == nullptr; });
        });
}

void uninstallHydra() {
    for (auto id : S().handlers) ia::remove(id);
    S().handlers.clear();
    if (S().levelTick) fe::removeLevelTick(S().levelTick);
    S().levelTick = 0;
    S().pendingSeed.clear();
    S().runtimes.clear();
    S().pendingLoad.clear();
}

} // namespace gf::feature::hydra

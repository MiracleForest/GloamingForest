// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/passive/FlyingBird.java
//   src/main/java/twilightforest/entity/passive/Bird.java
//   src/main/java/twilightforest/entity/passive/TinyBird.java
//   src/main/java/twilightforest/entity/passive/Raven.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U16's flying birds (tiny bird, raven): FlyingBird's landed flag (DATA_BIRDFLAGS bit 0, the
// twilightforest:landed property the client animations read, and the BP groups "landed" (the
// walking goals) / "flying"), take-off when spooked, in water or at random over unlandable ground,
// and the flight (Bat#customServerAiStep with TF's edits) steered natively every tick; Bird#aiStep's
// slow fall; the tiny bird's variant (finalizeSpawn by biome, the saved "variant" string) and its
// ambient sound (TinyBird#getAmbientSound: the song 1 time in 20, else a chirp).
#include <cmath>
#include <iterator>
#include <numbers>
#include <optional>
#include <unordered_map>

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/entity/Entity.h"
#include "feature/ext/fauna/W0Internals.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/fauna/Fauna.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"

namespace gf::feature::fauna {

namespace fe  = gf::feature::entity;
namespace cf  = gf::core::fauna;
namespace kit = gf::features::kit;

namespace {

/// FlyingBird's unsaved fields (Bat copy) and Mob#ambientSoundTime, per actor.
struct BirdState {
    bool                              landed = true;
    std::optional<std::array<int, 3>> target;
    int                               flightTime   = 0;
    int                               ambientTime  = 0;
};
std::unordered_map<std::int64_t, BirdState>& birds() {
    static std::unordered_map<std::int64_t, BirdState> m;
    return m;
}
BirdState& stateOf(Actor& a) { return birds()[a.getOrCreateUniqueID().rawID]; }

/// #c:seeds (the tiny bird's and raven's tempt items) as NeoForge 21.1 defines it.
bool isSeed(std::string const& id) {
    if (id.empty()) return false;
    if (gf::tags::has(gf::tags::Registry::Item, "c:seeds", id)) return true;
    return id == "minecraft:wheat_seeds" || id == "minecraft:beetroot_seeds" || id == "minecraft:melon_seeds" || id == "minecraft:pumpkin_seeds"
        || id == "minecraft:torchflower_seeds";
}

bool isLeaves(Block const& b) { return kit::isLeaves(b) || gf::tags::has(gf::tags::Registry::Block, "minecraft:leaves", b.getTypeName()); }

/// FlyingBird#isLandableBlock: not air, and leaves or a sturdy top face.
bool landable(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (b.isAir()) return false;
    // Direction.UP sturdy (SupportType.FULL): the whole top face supports
    return isLeaves(b) || (b.canProvideSupport(1, BlockSupportType::Center) && b.canProvideSupport(1, BlockSupportType::Edge));
}

bool fluidAt(BlockSource& region, BlockPos const& pos) { return kit::isFluid(region.getBlock(pos)); }

/// TinyBird#isSpooked / Raven#isSpooked.
bool spooked(Mob& bird) {
    if (fe::mobs::lastHurtByMob(bird)) return true;
    if (bird.getTypeName() != kTinyBird) return false;
    // Level#getNearestPlayer(x, y, z, 4.0, true): no creative or spectator players
    Player* nearest = nullptr;
    double  best    = -1.0;
    for (auto* p : fe::playersNear(static_cast<int>(bird.getDimensionId()), bird.getFeetPos(), 4.0)) {
        if (!p || p->isCreative() || p->isSpectator()) continue;
        double d = fe::mobs::distanceSqr(*p, bird);
        if (d <= 16.0 && (best < 0.0 || d < best)) {
            best    = d;
            nearest = p;
        }
    }
    if (!nearest) return false;
    return isSeed(game::items::idOf(nearest->getSelectedItem())) || isSeed(game::items::idOf(nearest->getOffhandSlot()));
}

void setYaw(Mob& mob, float yaw) {
    auto const& rot                     = mob.getRotation();
    const_cast<Vec2&>(mob.getRotation()) = Vec2{rot.x, yaw};
    mob.setYBodyRotation(yaw);
}

/// FlyingBird#customServerAiStep, flying: the Bat's flight with TF's edits.
void fly(Mob& bird, BirdState& s) {
    auto& region = bird.getDimensionBlockSource();
    auto& r      = fe::randomOf(bird);
    s.flightTime++;
    auto pos  = bird.getFeetPos();
    auto here = blockPosOf(bird);
    if (s.target) {
        BlockPos t{(*s.target)[0], (*s.target)[1], (*s.target)[2]};
        if (!region.getBlock(t).isAir() || t.y <= region.getMinHeight()) s.target.reset();
    }
    auto motion = bird.getPosDelta();
    // TF - no drowning birds
    if (bird.isInWater() || fluidAt(region, BlockPos{here.x, here.y - 1, here.z})) {
        s.flightTime = 0;
        motion.y     = 0.1f;
    }
    auto closer = [&] {
        double dx = (*s.target)[0] + 0.5 - pos.x, dy = (*s.target)[1] + 0.5 - pos.y, dz = (*s.target)[2] + 0.5 - pos.z;
        return dx * dx + dy * dy + dz * dz < 4.0;
    };
    if (!s.target || r.nextInt(30) == 0 || closer()) s.target = cf::nextFlightTarget(pos.x, pos.y, pos.z, s.flightTime, r);
    auto m = cf::steerTowards({motion.x, motion.y, motion.z}, {pos.x, pos.y, pos.z}, *s.target);
    float yaw  = cf::yawOf(m[0], m[2]);
    float cur  = bird.getRotation().y;
    float next = cur + cf::wrapDegrees(yaw - cur);
    setYaw(bird, next);
    // zza = 0.5: LivingEntity#travel in the air moves 0.5 * 0.02 forward
    double rad = next * std::numbers::pi / 180.0;
    m[0] += -std::sin(rad) * 0.01;
    m[2] += std::cos(rad) * 0.01;
    // TF - change chance 100 -> 10; change check to isLandable
    if (r.nextInt(10) == 0 && landable(region, BlockPos{here.x, here.y - 1, here.z})) {
        setBirdLanded(bird, true, false);
        m[1] = 0.0;
    }
    fe::setMotion(bird, Vec3{static_cast<float>(m[0]), static_cast<float>(m[1]), static_cast<float>(m[2])});
}

void birdSpawn(Actor& self) {
    // FlyingBird's constructor: landed (not saved: a loaded bird starts landed too)
    auto& s  = stateOf(self);
    s        = BirdState{};
    setBirdLanded(self, true, false);
    if (self.getTypeName() == kTinyBird) variantSpawn(self);
    else applyJavaEyeHeight(self);
}

void birdTick(Actor& self) {
    applyJavaEyeHeight(self);
    auto* bird = fe::asMob(self);
    if (!bird || !self.isAlive()) return;
    auto& s = stateOf(self);
    // Bird#aiStep and FlyingBird#tick (while flying, level out)
    birdSlowFall(self);
    if (!s.landed) {
        auto v = self.getPosDelta();
        fe::setMotion(self, Vec3{v.x, v.y * 0.6f, v.z});
    }
    // FlyingBird#customServerAiStep
    if (s.landed) {
        s.flightTime = 0;
        auto& region = self.getDimensionBlockSource();
        auto  below  = blockPosOf(self);
        below.y -= 1;
        auto& r = fe::randomOf(self);
        if (spooked(*bird) || self.isInWater() || fluidAt(region, below) || (r.nextInt(200) == 0 && !landable(region, below)))
            setBirdLanded(self, false, true);
    } else {
        fly(*bird, s);
    }
    // TinyBird#getAmbientSound through Mob#baseTick's timer (the BP's ambient is off for the tiny bird)
    if (self.getTypeName() == kTinyBird) {
        auto& r = fe::randomOf(self);
        if (r.nextInt(1000) < s.ambientTime++) {
            s.ambientTime = -80;
            if (!self.isSilent()) {
                auto event = r.nextInt(20) == 0 ? "entity.twilightforest.tiny_bird.song" : "entity.twilightforest.tiny_bird.chirp";
                gf::fx::sound(self.getDimension(), event, self.getFeetPos(), 1.0f, cf::voicePitch(r, false));
            }
        }
    }
}

} // namespace

bool birdLanded(Actor& bird) { return stateOf(bird).landed; }

void setBirdLanded(Actor& bird, bool landed, bool sound) {
    auto& s  = stateOf(bird);
    bool  was = s.landed;
    s.landed = landed;
    fireEvent(bird, landed ? "land" : "take_off");
    gf::entity::setProperty(bird, "twilightforest:landed", landed);
    if (!landed) s.target.reset();
    if (was && !landed && sound && !bird.isSilent())
        gf::fx::sound(bird.getDimension(), "entity.twilightforest.tiny_bird.takeoff", bird.getFeetPos(), 0.05f, cf::voicePitch(fe::randomOf(bird), false));
}

void installBirds() {
    for (char const* id : {kTinyBird, kRaven})
        gf::entity::registerType(id, {.onSpawn = birdSpawn, .tick = birdTick, .die = [](Actor& self, std::string const&, Actor*) {
                                          birds().erase(self.getOrCreateUniqueID().rawID);
                                      }});
    // the state of birds that are gone (unloaded, removed)
    fe::onLevelTick([] {
        if (fe::currentTick() % 600 != 0) return;
        for (auto it = birds().begin(); it != birds().end();) it = fe::actorById(it->first) ? std::next(it) : birds().erase(it);
    });
}

} // namespace gf::feature::fauna

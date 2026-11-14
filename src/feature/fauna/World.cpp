// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/events/RegistrationEvents.java (registerSpawnPlacements)
//   src/main/java/gloamingforest/events/MiscEvents.java (addPrey)
//   src/main/java/gloamingforest/data/DataMapGenerator.java (the parrot imitations)
//   src/main/java/gloamingforest/entity/passive/{FlyingBird,Squirrel,DwarfRabbit,Bighorn}.java (getWalkTargetValue)
//   net/minecraft/world/entity/animal/Parrot.java (vanilla, imitateNearbyMobs)
//   net/minecraft/world/entity/ai/goal/target/NearestAttackableTargetGoal.java, NonTameRandomTargetGoal.java (vanilla)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U16 in the world: the spawn placements and spawn rules of the ten mobs (U1's biome tables name
// them by string), the prey goals vanilla cats, ocelots, foxes and wolves get (addPrey), and the
// parrots' imitations of upstream's mobs (the neoforge:parrot_imitations data map: Bedrock's parrots
// imitate only vanilla mobs, so a server tick over loaded parrots does Parrot#imitateNearbyMobs).
#include <algorithm>
#include <cmath>
#include <functional>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/phys/AABB.h"

#include "feature/datamaps/DataMaps.h"
#include "feature/entity/Entity.h"
#include "feature/ext/dimension/DimensionType.h"
#include "feature/ext/fauna/W0Internals.h"
#include "feature/ext/worldgen/FeatureKit.h"
#include "feature/ext/worldgen/Spawning.h"
#include "feature/fauna/Fauna.h"
#include "feature/fx/Fx.h"
#include "feature/spawn/Spawn.h"

namespace gf::feature::fauna {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;
namespace kit  = gf::features::kit;
namespace sx   = gf::spawn::ext;
using gf::spawn::SpawnReason;

namespace {

// ---- light ---------------------------------------------------------------------------------------------

/// The sky light estimated from the blocks above (no light yet during chunk generation): leaves and
/// fluids dim it by one, blocks that stop motion block it.
int estimatedSky(worldgen::Region& region, BlockPos const& pos) {
    int sky = 15;
    for (int y = pos.y; y < region.maxY() && sky > 0; ++y) {
        auto const& b = region.getBlock(BlockPos{pos.x, y, pos.z});
        if (kit::isAir(b)) continue;
        if (kit::isLeaves(b) || kit::isFluid(b)) --sky;
        else if (kit::blocksMotion(b)) sky = 0;
    }
    return std::max(sky, 0);
}

/// LevelReader#getMaxLocalRawBrightness (the engine's light; estimated while generating).
int rawBrightness(worldgen::Region& region, BlockPos const& pos, SpawnReason reason) {
    if (reason != SpawnReason::ChunkGeneration)
        if (auto* src = kit::blockSource(region)) {
            auto pair = src->getBrightnessPair(pos);
            return std::max(static_cast<int>(pair.sky->mValue), static_cast<int>(pair.block->mValue));
        }
    return estimatedSky(region, pos);
}

int rawBrightness(BlockSource& region, BlockPos const& pos, SpawnReason reason) {
    if (reason != SpawnReason::ChunkGeneration) {
        auto pair = region.getBrightnessPair(pos);
        return std::max(static_cast<int>(pair.sky->mValue), static_cast<int>(pair.block->mValue));
    }
    int sky = 15;
    for (int y = pos.y; y < region.getMaxHeight() && sky > 0; ++y) {
        auto const& b = region.getBlock(BlockPos{pos.x, y, pos.z});
        if (kit::isAir(b)) continue;
        if (kit::isLeaves(b) || kit::isFluid(b)) --sky;
        else if (kit::blocksMotion(b)) sky = 0;
    }
    return std::max(sky, 0);
}

/// DimensionType#brightness of the Gloaming Forest (ambient light 0): light / (4 - 3 light / 15).
float brightness(int light) {
    float f = static_cast<float>(light) / 15.0f;
    return f / (4.0f - 3.0f * f);
}

// ---- spawn rules -----------------------------------------------------------------------------------------

/// Animal#checkAnimalSpawnRules: #minecraft:animals_spawnable_on below (grass in 1.21.1) and raw
/// brightness above 8.
bool checkAnimalSpawnRules(BlockSource& region, std::string const&, BlockPos const& pos, SpawnReason reason, gf::spawn::RandomSource&) {
    auto const& below = region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z});
    if (!kit::hasTag(below, "minecraft:animals_spawnable_on") && kit::javaId(below) != "minecraft:grass_block") return false;
    return rawBrightness(region, pos, reason) > 8;
}

/// Penguin#canSpawn: #gloamingforest:penguins_spawnable_on (#minecraft:ice) below.
bool checkPenguinSpawn(BlockSource& region, std::string const&, BlockPos const& pos, SpawnReason, gf::spawn::RandomSource&) {
    auto const& below = region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z});
    if (kit::hasTag(below, "gloamingforest:penguins_spawnable_on") || kit::hasTag(below, "minecraft:ice")) return true;
    auto id = kit::javaId(below);
    return id == "minecraft:ice" || id == "minecraft:packed_ice" || id == "minecraft:blue_ice" || id == "minecraft:frosted_ice";
}

/// Monster#checkMonsterSpawnRules: not on peaceful, dark enough unless a spawner places it.
bool checkMonsterSpawnRules(BlockSource& region, std::string const&, BlockPos const& pos, SpawnReason reason, gf::spawn::RandomSource& random) {
    auto level = ll::service::getLevel();
    if (!level || level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful) return false;
    if (reason == SpawnReason::Spawner || reason == SpawnReason::TrialSpawner) return true;
    return gf::world::ext::isDarkEnoughToSpawn(region, pos, random);
}

/// Bighorn#getWalkTargetValue(pos, level) >= 0 (Mob#checkSpawnRules): grass or podzol 10, else
/// LevelReader#getPathfindingCostFromLightLevels (brightness - 0.5). The flying birds', squirrel's and
/// dwarf rabbit's overrides are the one-argument getWalkTargetValue(pos), which checkSpawnRules does
/// not call: they keep Animal's (F5's WalkTarget::Animal).
bool bighornSpawnRules(worldgen::Region& region, BlockPos const& pos, SpawnReason reason) {
    auto id = kit::javaId(region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z}));
    if (id == "minecraft:grass_block" || id == "minecraft:podzol") return true;
    return brightness(rawBrightness(region, pos, reason)) - 0.5f >= 0.0f;
}

void registerSpawning() {
    struct Row {
        char const* id;
        double      w, h;
    };
    for (auto r : {Row{kBoar, 0.9, 0.9}, Row{kBighorn, 0.9, 1.3}, Row{kDeer, 0.7, 1.8}, Row{kTinyBird, 0.3, 0.3}, Row{kSquirrel, 0.3, 0.5},
                   Row{kDwarfRabbit, 0.4, 0.4}, Row{kRaven, 0.3, 0.5}}) {
        gf::spawn::registerPlacement(r.id, gf::spawn::PlacementRule::OnGround, checkAnimalSpawnRules);
        sx::SpawnBehaviour b;
        b.width      = r.w;
        b.height     = r.h;
        b.walkTarget = sx::WalkTarget::Animal;
        if (std::string(r.id) == kBighorn) b.checkSpawnRules = bighornSpawnRules;
        sx::registerSpawnBehaviour(r.id, std::move(b));
    }
    gf::spawn::registerPlacement(kPenguin, gf::spawn::PlacementRule::NoRestrictions, checkPenguinSpawn);
    // Penguin#checkSpawnRules: always true
    sx::registerSpawnBehaviour(kPenguin, {.width = 0.5, .height = 0.9, .walkTarget = sx::WalkTarget::Zero});
    gf::spawn::registerPlacement(kKobold, gf::spawn::PlacementRule::OnGround, checkMonsterSpawnRules);
    sx::registerSpawnBehaviour(kKobold, {.width = 0.8, .height = 1.1, .maxClusterSize = 1, .walkTarget = sx::WalkTarget::Monster});
    gf::spawn::registerPlacement(kMosquitoSwarm, gf::spawn::PlacementRule::OnGround, checkMonsterSpawnRules);
    sx::registerSpawnBehaviour(kMosquitoSwarm, {.width = 0.7, .height = 1.9, .maxClusterSize = 1, .walkTarget = sx::WalkTarget::Monster});
}

// ---- prey (MiscEvents#addPrey) ----------------------------------------------------------------------------

/// NearestAttackableTargetGoal(mob, prey, mustSee = true) and NonTameRandomTargetGoal (the same while
/// the cat / wolf is not tame).
class PreyTargetGoal final : public gf::entity::Goal {
public:
    PreyTargetGoal(Mob& mob, std::string prey, bool nonTame) : mUid(mob.getOrCreateUniqueID().rawID), mPrey(std::move(prey)), mNonTame(nonTame) {
        flags = Target;
    }
    bool canUse() override {
        auto* m = self();
        if (!m) return false;
        if (mNonTame && m->isTame()) return false;
        // randomInterval reducedTickDelay(10)
        if (fe::randomOf(*m).nextInt(5) != 0) return false;
        mTarget = 0;
        auto const& bb    = m->getAABB();
        float       range = kFollowRange;
        AABB        box{Vec3{bb.min.x - range, bb.min.y - 4.0f, bb.min.z - range}, Vec3{bb.max.x + range, bb.max.y + 4.0f, bb.max.z + range}};
        double      best = -1.0;
        for (auto* a : fe::actorsIn(m->getDimensionBlockSource(), static_cast<int>(m->getDimensionId()), box)) {
            if (!a || a == m || !a->isAlive() || a->getTypeName() != mPrey) continue;
            if (!mobs::hasLineOfSight(*m, *a)) continue;
            double d = mobs::distanceSqr(*m, *a);
            if (d > range * range) continue;
            if (best < 0.0 || d < best) {
                best    = d;
                mTarget = a->getOrCreateUniqueID().rawID;
            }
        }
        return mTarget != 0;
    }
    bool canContinueToUse() override {
        auto* m = self();
        auto* t = mTarget ? fe::actorById(mTarget) : nullptr;
        if (!m || !t || !t->isAlive() || mobs::target(*m) != t) return false;
        if (mNonTame && m->isTame()) return false;
        if (mobs::distanceSqr(*m, *t) > kFollowRange * kFollowRange) return false;
        // TargetGoal#canContinueToUse with mustSee: unseen for more than 60 ticks
        if (mobs::hasLineOfSight(*m, *t)) mUnseen = 0;
        else if (++mUnseen > 60) return false;
        return true;
    }
    void start() override {
        auto* m = self();
        auto* t = mTarget ? fe::actorById(mTarget) : nullptr;
        if (m && t) m->setTarget(t);
        mUnseen = 0;
    }
    void stop() override {
        auto* m = self();
        if (m && mTarget && mobs::target(*m) == fe::actorById(mTarget)) m->setTarget(nullptr);
        mTarget = 0;
    }

private:
    static constexpr float kFollowRange = 16.0f;
    Mob* self() const {
        auto* a = fe::actorById(mUid);
        return a ? fe::asMob(*a) : nullptr;
    }
    std::int64_t mUid;
    std::string  mPrey;
    bool         mNonTame;
    std::int64_t mTarget = 0;
    int          mUnseen = 0;
};

struct PreyRule {
    char const*              predator;
    int                      priority;
    bool                     nonTame;
    std::vector<char const*> prey;
};
std::vector<PreyRule> const& preyRules() {
    static std::vector<PreyRule> const rules = {
        {"minecraft:cat", 1, true, {kDwarfRabbit, kSquirrel, kTinyBird}},
        {"minecraft:ocelot", 1, false, {kDwarfRabbit, kSquirrel, kTinyBird}},
        {"minecraft:fox", 6, false, {kDwarfRabbit, kSquirrel}},
        {"minecraft:wolf", 7, true, {kDwarfRabbit, kSquirrel, kBighorn}},
    };
    return rules;
}

void addPrey(Actor& self) {
    auto* mob = fe::asMob(self);
    if (!mob || nativeTargetGoalsPresent(self)) return;
    for (auto const& r : preyRules())
        if (self.getTypeName() == r.predator)
            for (char const* prey : r.prey) gf::entity::addTargetGoal(*mob, r.priority, std::make_shared<PreyTargetGoal>(*mob, prey, r.nonTame));
}

// ---- parrots ---------------------------------------------------------------------------------------------

/// Parrot#aiStep (1 in 400) -> imitateNearbyMobs (1 in 2): a random imitable mob within 20 blocks.
void parrotTick(Actor& parrot) {
    if (!parrot.isAlive() || parrot.isSilent()) return;
    auto& r = rng();
    if (r.nextInt(400) != 0 || r.nextInt(2) != 0) return;
    auto const&         bb = parrot.getAABB();
    AABB                box{Vec3{bb.min.x - 20.0f, bb.min.y - 20.0f, bb.min.z - 20.0f}, Vec3{bb.max.x + 20.0f, bb.max.y + 20.0f, bb.max.z + 20.0f}};
    std::vector<Actor*> list;
    for (auto* a : fe::actorsIn(parrot.getDimensionBlockSource(), static_cast<int>(parrot.getDimensionId()), box))
        if (a && a != &parrot && fe::asMob(*a) && !parrotImitation(a->getTypeName()).empty()) list.push_back(a);
    if (list.empty()) return;
    auto* mob = list[static_cast<std::size_t>(r.nextInt(static_cast<int>(list.size())))];
    if (mob->isSilent()) return;
    auto sound = parrotImitation(mob->getTypeName());
    if (sound.starts_with("gloamingforest:")) sound = sound.substr(15);
    gf::fx::sound(parrot.getDimension(), sound, parrot.getFeetPos(), 0.7f, core::fauna::parrotPitch(r));
}

} // namespace

std::string parrotImitation(std::string const& entityId) {
    // only upstream's entries: Bedrock's parrots imitate the vanilla mobs themselves
    if (!entityId.starts_with("gloamingforest:")) return {};
    auto const* v = gf::datamaps::get("neoforge:parrot_imitations", entityId);
    if (!v || !v->is_object()) return {};
    return v->value("sound", std::string{});
}

bool penguinCanSpawnOn(BlockSource& region, BlockPos const& pos) {
    core::jrand::LegacyRandom r(0);
    return checkPenguinSpawn(region, kPenguin, pos, SpawnReason::Natural, r);
}

int preyGoalCount(std::string const& predatorId) {
    for (auto const& r : preyRules())
        if (predatorId == r.predator) return static_cast<int>(r.prey.size());
    return 0;
}

void installWorld() {
    registerSpawning();
    for (auto const& r : preyRules()) gf::entity::registerType(r.predator, {.onSpawn = addPrey, .tick = addPrey});
    gf::entity::registerType("minecraft:parrot", {.tick = parrotTick});
}

} // namespace gf::feature::fauna

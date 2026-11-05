// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// The hydra.* selftests of U24 (rule R12: dx 2400..2499). The unit record's e2e: a boss fight with
// a seeded random (head loss and regrowth, parts following the body, the mortar), jets firing on
// redstone; plus the lair's golden volume hash, the bite, the name tags, the death and the spawner.
// Every test spawns its own hydra with the brain off (seedForTest), drives the heads and checks
// what TF's code does.

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/safety/RedactableString.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "core/hydra/Head.h"
#include "core/hydra/Lair.h"
#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/landmark/LandmarkTest.h"
#include "feature/hydra/Hydra.h"
#include "feature/hydra/World.h"
#include "feature/hydra/Lair.h"
#include "feature/inventory/Inventory.h"
#include "feature/landmark/Landmark.h"
#include "feature/projectile/Projectile.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::hydra::tests {

using gf::selftest::Context;
namespace ch = gf::core::hydra;
namespace fe = gf::feature::hydra::world;
namespace ge = gf::entity;
using ch::State;
using Difficulty = SharedTypes::Legacy::Difficulty;

namespace {

/// The golden volume hash of the hydra lair piece in its test volume (reviewed once with screenshots).
constexpr char kLairGolden[] = "b69df127eecbe821";

struct DifficultyScope {
    Difficulty previous = Difficulty::Normal;
    explicit DifficultyScope(Difficulty d) {
        if (auto level = ll::service::getLevel()) {
            previous = level->getDifficulty();
            level->setDifficulty(d);
        }
    }
    ~DifficultyScope() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(previous);
    }
};

Mob* spawnMob(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y), at.z + dz}, false, false, false);
}

Actor* byId(std::int64_t uid) { return fe::actorById(uid); }

/// Spawns a seeded hydra with its brain off; its uid (0: failed).
std::int64_t spawnHydra(Context& ctx, core::GlobalPos const& at, std::int64_t seed) {
    auto* h = spawnMob(at, kHydra);
    if (!h) return 0;
    ge::seedForTest(*h, seed);
    seedForTest(*h, seed);
    auto uid = fe::uidOf(*h);
    (void)ctx;
    return uid;
}

/// Keeps a simulated player alive through the hydra's hits (resistance 255 for `seconds`).
void resistant(Player& p, int seconds) {
    (void)gf::selftest::runServerCommand("effect \"" + p.getRealName() + "\" resistance " + std::to_string(seconds) + " 255 true", static_cast<int>(p.getDimensionId()));
}

bool attacking(State s) {
    switch (s) {
    case State::BiteBeginning:
    case State::BiteReady:
    case State::Biting:
    case State::BiteEnding:
    case State::FlameBeginning:
    case State::Flaming:
    case State::FlameEnding:
    case State::MortarBeginning:
    case State::MortarShooting:
    case State::MortarEnding: return true;
    default: return false;
    }
}

/// Waits until the hydra's runtime exists (its first tick) and turns its brain off.
ll::coro::CoroTask<bool> ready(std::int64_t uid) {
    for (int i = 0; i < 10; ++i) {
        co_await Context::ticks(1);
        if (auto* h = byId(uid); h && headView(*h, 0)) {
            setBrainEnabled(*h, false);
            co_return true;
        }
    }
    co_return false;
}

void discard(std::int64_t uid) {
    if (auto* h = byId(uid)) h->remove();
}

int activeHeads(Actor& h) {
    int n = 0;
    for (int i = 0; i < ch::kMaxHeads; ++i)
        if (auto v = headView(h, i); v && v->state != State::Dead && v->state != State::Dying) ++n;
    return n;
}

double dist(Vec3 const& a, Vec3 const& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

} // namespace

// The lair piece (HydraLairComponent#postProcess, rule R19): stone dome, grass patch, speleothems,
// the boss spawner at local (27, 3, 27); a golden volume hash of the clipped test volume.
GF_REGISTER_SELFTEST("hydra.lair_golden", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(2400, 80, 64); // up to the hill's roof (the stalactites' heightmap)
    auto region = ctx.region();
    ctx.require(region != nullptr, "test region");
    // a known floor: four layers of dirt at the bottom of the volume, grass on top
    auto const* dirt  = game::blockWith("minecraft:dirt");
    auto const* grass = game::blockWith("minecraft:grass_block");
    ctx.require(dirt && grass, "dirt and grass blocks");
    for (int x = box.minX; x <= box.maxX; ++x)
        for (int z = box.minZ; z <= box.maxZ; ++z)
            for (int y = box.minY; y <= box.minY + 3; ++y) game::setBlock(*region, BlockPos{x, y, z}, y == box.minY + 3 ? *grass : *dirt);
    // the start at the volume's middle; the piece's box starts 4 above the volume's floor (its
    // domes start 4 and 1 below the piece)
    int  cx = box.minX + 40, cz = box.minZ + 40, cy = box.minY + 4;
    // with the terraformer's hill (TF's terrain pass at sea level, translated to cy): the speleothems
    // hang from its ceiling and stand on its floor inside the volume
    auto pieceBox = placeLairForTest(*region, box, cx, cy, cz, 2400, true);
    ctx.check(pieceBox.maxX - pieceBox.minX + 1 == 69 && pieceBox.maxY - pieceBox.minY == 17, "the piece box is 69 x 18 x 69 (radius 34, hill size 2)");
    // the spawner at piece-local (27, 3, 27)
    int spawners = 0;
    for (int x = box.minX; x <= box.maxX; ++x)
        for (int z = box.minZ; z <= box.maxZ; ++z)
            if (game::blockIdAt(*region, BlockPos{x, pieceBox.minY + 3, z}) == kSpawner) ++spawners;
    ctx.check(spawners == 1, "one hydra boss spawner at local y 3 (" + std::to_string(spawners) + ")");
    // the stone dome under the hill, the grass patch of the arena
    int stone = 0;
    for (int x = box.minX; x <= box.maxX; ++x)
        for (int z = box.minZ; z <= box.maxZ; ++z)
            if (game::blockIdAt(*region, BlockPos{x, box.minY, z}) == "minecraft:stone") ++stone;
    ctx.check(stone > 1000, "the dome turned the dirt to stone (" + std::to_string(stone) + " columns)");
    // the ore stalactites (iron, coal, copper, glowstone, gold, redstone) hang inside the volume
    {
        int  hill = 0;
        auto c    = ch::boxCenter(pieceBox);
        std::string profile;
        for (int y = cy; y <= box.maxY; ++y) {
            auto id = game::blockIdAt(*region, BlockPos{c.x + 10, y, c.z + 10});
            profile += id == "minecraft:air" ? '.' : (id.ends_with("_ore") ? 'o' : '#');
        }
        for (int x = box.minX; x <= box.maxX; ++x)
            for (int z = box.minZ; z <= box.maxZ; ++z)
                for (int y = cy; y <= box.maxY; ++y)
                    if (game::blockIdAt(*region, BlockPos{x, y, z}) != "minecraft:air") ++hill;
        ctx.info("hill blocks above the ground " + std::to_string(hill) + ", speleothem tries " + std::to_string(speleothemTriesForTest()) + ", column at the centre +10 from y " +
                 std::to_string(cy) + ": " + profile);
    }
    int ores = 0;
    for (int x = box.minX; x <= box.maxX; ++x)
        for (int z = box.minZ; z <= box.maxZ; ++z)
            for (int y = cy + 1; y <= box.maxY; ++y) {
                auto id = game::blockIdAt(*region, BlockPos{x, y, z});
                if (id.ends_with("_ore") || id == "minecraft:glowstone") ++ores;
            }
    ctx.check(ores > 50, "ore stalactites hang in the lair (" + std::to_string(ores) + " blocks)");
    auto hash = ctx.snapshotVolume(box);
    ctx.info("lair volume hash " + hash);
    ctx.check(hash == kLairGolden, "golden volume hash " + std::string(kLairGolden) + " (got " + hash + ")");
});

// Spawn: 3 heads active, 4 dead, 44 part actors following the body by TF's formulas, the boss's
// attributes (tests/golden/mobs.json: 360 health).
GF_REGISTER_SELFTEST("hydra.spawn_parts", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto home = ctx.testPos(2444, 2);
    auto uid  = spawnHydra(ctx, home, 2420);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    co_await ctx.ticks(5);
    auto* h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(h->getMaxHealth() == ch::kMaxHealth && h->getHealth() == ch::kMaxHealth, "max health 360 (" + std::to_string(h->getMaxHealth()) + ")");
    ctx.check(h->isFireImmune(), "fire immune");
    ctx.check(activeHeads(*h) == 3, "3 heads active at spawn (" + std::to_string(activeHeads(*h)) + ")");
    for (int i = 3; i < ch::kMaxHeads; ++i) ctx.check(headView(*h, i)->state == State::Dead, "head " + std::to_string(i) + " starts DEAD");
    auto* mob   = fe::asMob(*h);
    auto  parts = ge::ext::parts(*mob);
    ctx.check(parts.size() == ch::kPartCount, "44 part actors (" + std::to_string(parts.size()) + ")");
    int typed = 0;
    for (auto const& p : parts)
        if (auto* a = ge::partActor(p); a && a->getTypeName() == kHydraPart) ++typed;
    ctx.check(typed == ch::kPartCount, "every part is a twilightforest:hydra_part (" + std::to_string(typed) + ")");
    // turn the body and check that the parts follow the formulas
    const_cast<Vec2&>(h->getRotation()) = Vec2{0, 90};
    co_await ctx.ticks(10);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is still there");
    auto got  = partPositions(*h);
    auto want = expectedPartPositions(*h);
    ctx.require(got.size() == want.size() && !got.empty(), "positions known");
    double worst = 0;
    for (std::size_t i = 0; i < got.size(); ++i) worst = std::max(worst, dist(got[i], want[i]));
    ctx.check(worst < 1.6, "the part actors follow the body, heads and necks (worst " + std::to_string(worst) + " blocks)");
    auto body = want[0], feet = h->getFeetPos();
    ctx.check(std::abs(std::hypot(body.x - feet.x, body.z - feet.z) - 3.0) < 0.05, "the body part is 3 blocks behind the hydra");
    auto tail = want[1];
    ctx.check(std::abs(std::hypot(tail.x - feet.x, tail.z - feet.z) - 10.5) < 0.05, "the tail part is 10.5 blocks behind");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// Head loss and regrowth (Hydra#customServerAiStep): a head past 120 damage dies (DYING 70 ticks,
// DEAD), it and one random dead head respawn 140 ticks later (BORN, ROAR); the number of heads grows.
GF_REGISTER_SELFTEST("hydra.head_regrow", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto home = ctx.testPos(2444, 2);
    auto uid  = spawnHydra(ctx, home, 2421);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto* h = byId(uid);
    addHeadDamage(*h, 1, 121);
    co_await ctx.ticks(2);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    auto v = headView(*h, 1);
    ctx.check(v && v->state == State::Dying, "head 1 is dying after 121 damage");
    ctx.check(v && v->respawnCounter == ch::kHeadRespawnTicks, "its respawn counter is 140");
    int other = -1;
    for (int i = 3; i < ch::kMaxHeads; ++i)
        if (auto w = headView(*h, i); w && w->respawnCounter > 0) other = i;
    ctx.check(other >= 3, "one random dead head will respawn too (" + std::to_string(other) + ")");
    co_await ctx.ticks(72);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(headView(*h, 1)->state == State::Dead, "head 1 is DEAD after its 70 dying ticks");
    ctx.check(!headView(*h, 1)->active, "the dead head's parts are inactive");
    // the other head counts while DEAD: born 140 ticks after the kill
    co_await ctx.ticks(70);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    if (other >= 0) {
        auto s = headView(*h, other)->state;
        ctx.check(s == State::Born || s == State::RoarStart || s == State::RoarRawr, "head " + std::to_string(other) + " was born (" + std::string(ch::name(s)) + ")");
    }
    // head 1 counts from DEAD: 70 + 140 ticks after the kill
    co_await ctx.ticks(75);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    auto s1 = headView(*h, 1)->state;
    ctx.check(s1 == State::Born || s1 == State::RoarStart || s1 == State::RoarRawr || s1 == State::Idle, "head 1 grew back (" + std::string(ch::name(s1)) + ")");
    ctx.check(activeHeads(*h) == 4, "4 heads now (" + std::to_string(activeHeads(*h)) + ")");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// Hits on parts (Hydra#attackEntityFromPart): an armoured head takes round(damage / 8), an open
// mouth the full damage; the head counts it; the body's direct hits are refused.
GF_REGISTER_SELFTEST("hydra.part_damage", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2422);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 6.5f}, DimensionType(home.dim));
    co_await ctx.ticks(3);
    auto* h  = byId(uid);
    auto* pl = fe::playerById(spId);
    ctx.require(h && pl, "hydra and player");
    int  before = h->getHealth();
    auto head0  = ge::partActor(ge::ext::parts(*fe::asMob(*h))[ch::headPartIndex(0)]);
    ctx.require(head0 != nullptr, "head 0's part actor");
    gf::combat::hurt(*head0, 16.0F, "minecraft:player_attack", pl, pl);
    co_await ctx.ticks(1);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(before - h->getHealth() == 2, "a closed mouth takes round(16 / 8) = 2 (" + std::to_string(before - h->getHealth()) + ")");
    ctx.check(headView(*h, 0)->damageTaken == 2, "head 0 counted 2 (" + std::to_string(headView(*h, 0)->damageTaken) + ")");
    // the hydra itself takes nothing but /kill
    int mid = h->getHealth();
    gf::combat::hurt(*h, 10.0F, "minecraft:player_attack", fe::playerById(spId));
    co_await ctx.ticks(1);
    h = byId(uid);
    ctx.check(h && h->getHealth() == mid, "a direct hit on the hydra is refused");
    // open the mouth: FLAME_BEGINNING (40 ticks) then FLAMING starts at 0.75
    forceHeadState(*h, 0, State::FlameBeginning);
    co_await ctx.ticks(42);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(headView(*h, 0)->mouth > 0.5F, "head 0's mouth is open (" + std::to_string(headView(*h, 0)->mouth) + ")");
    before = h->getHealth();
    head0  = ge::partActor(ge::ext::parts(*fe::asMob(*h))[ch::headPartIndex(0)]);
    pl     = fe::playerById(spId);
    ctx.require(head0 && pl, "head part and player");
    gf::combat::hurt(*head0, 16.0F, "minecraft:player_attack", pl, pl);
    co_await ctx.ticks(1);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(before - h->getHealth() == 16, "an open mouth takes the full 16 (" + std::to_string(before - h->getHealth()) + ")");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// The bite (BITING, every tick): 48 hydra_bite damage to the living things in the head's box.
GF_REGISTER_SELFTEST("hydra.bite", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2423);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto* golem = spawnMob(ctx.testPos(2430, 2), "minecraft:iron_golem");
    ctx.require(golem != nullptr, "an iron golem to bite");
    auto gid    = fe::uidOf(*golem);
    int  before = golem->getHealth();
    forceHeadState(*byId(uid), 0, State::Biting);
    int  bitten = 0;
    for (int t = 0; t < 8; ++t) {
        co_await ctx.ticks(1);
        auto* h = byId(uid);
        auto* g = byId(gid);
        if (!h || !g) break;
        auto v = headView(*h, 0);
        if (v && v->state == State::Biting) {
            // keep the golem in the head's box
            g->teleport(Vec3{v->pos.x, v->pos.y, v->pos.z}, DimensionType(home.dim));
            ++bitten;
        }
    }
    co_await ctx.ticks(2);
    auto* g = byId(gid);
    ctx.check(bitten > 0, "head 0 was biting");
    ctx.check(!g || before - g->getHealth() >= ch::kBiteDamage, "the golem lost at least 48 health (" + std::to_string(g ? before - g->getHealth() : before) + ")");
    if (g) g->remove();
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// The mortar (HydraMortar): lands, its fuse runs on the ground (80), it detonates; a reflected one
// changes owner (fuse + 20) and damages the hydra it hits.
GF_REGISTER_SELFTEST("hydra.mortar", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2424);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto* h = byId(uid);
    auto  v = headView(*h, 0);
    ctx.require(v.has_value(), "head 0");
    int   det0 = detonationsForTest();
    auto* m    = launchMortar(*fe::asMob(*h), 0, v->pos, v->xRot, v->yRot, false);
    ctx.require(m != nullptr, "a mortar was launched");
    auto mid      = fe::uidOf(*m);
    int  landedAt = -1;
    for (int t = 0; t < 200 && detonationsForTest() == det0; ++t) {
        co_await ctx.ticks(1);
        auto* mm = byId(mid);
        if (mm)
            if (auto mv = mortarView(*mm); mv && mv->onGround && landedAt < 0) landedAt = t;
    }
    ctx.check(landedAt >= 0, "the mortar landed");
    ctx.check(detonationsForTest() == det0 + 1, "the mortar detonated");
    // a reflected mortar: the player becomes its owner and it hurts the hydra
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 7.5f}, DimensionType(home.dim));
    co_await ctx.ticks(2);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    v = headView(*h, 0);
    m = launchMortar(*fe::asMob(*h), 0, v->pos, v->xRot, v->yRot, false);
    ctx.require(m != nullptr, "a second mortar");
    mid        = fe::uidOf(*m);
    auto* pl   = fe::playerById(spId);
    ctx.require(pl != nullptr, "the player");
    int fuse0 = mortarView(*m)->fuse;
    reflectMortar(*m, *pl);
    auto mv = mortarView(*m);
    ctx.check(mv && mv->owner == spId, "the reflecting player owns the mortar");
    ctx.check(mv && mv->fuse == fuse0 + 20, "the fuse got 20 more ticks");
    // aim it at head 0
    float before = damageTakenForTest(*h);
    auto  hp     = headView(*h, 0)->pos;
    Vec3  from{hp.x, hp.y + 6.0f, hp.z};
    m->moveTo(from, Vec2{0, 0});
    gf::projectile::ext::setVelocity(*m, Vec3{0.0f, -1.0f, 0.0f});
    int det1 = detonationsForTest();
    for (int t = 0; t < 40 && detonationsForTest() == det1; ++t) co_await ctx.ticks(1);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(detonationsForTest() == det1 + 1, "the reflected mortar detonated on the hydra");
    ctx.check(damageTakenForTest(*h) > before, "the hydra took the reflected mortar's damage (" + std::to_string(damageTakenForTest(*h) - before) + ")");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// Name tags on heads (HydraHead#interact): the head is named, the tag used, the name saved
// (HeadNames) and cleared when the head dies.
GF_REGISTER_SELFTEST("hydra.head_names", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto home = ctx.testPos(2444, 2);
    auto uid  = spawnHydra(ctx, home, 2425);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 6.5f}, DimensionType(home.dim));
    co_await ctx.ticks(2);
    auto* pl = fe::playerById(spId);
    auto* h  = byId(uid);
    ctx.require(pl && h, "player and hydra");
    auto tag = game::items::make("minecraft:name_tag", 2);
    ctx.require(tag.has_value(), "a name tag");
    tag->setCustomName(Bedrock::Safety::RedactableString(std::string("Ladon")));
    pl->setCarriedItem(*tag);
    auto* neck = ge::partActor(ge::ext::parts(*fe::asMob(*h))[ch::neckPartIndex(2, 1)]);
    ctx.require(neck != nullptr, "head 2's neck b");
    std::string itemId = "minecraft:name_tag", type = neck->getTypeName();
    gf::interactions::EntityContext c{*pl, pl->getCarriedItem(), itemId, *neck, type, {0.0, 0.0, 0.0}, false};
    ctx.check(gf::interactions::dispatchInteract(c) == gf::interactions::Result::Consume, "the name tag was used on a neck");
    ctx.check(game::items::countOf(pl->getCarriedItem()) == 1, "one tag was used");
    ctx.check(headView(*h, 2)->name == "Ladon", "head 2 (the neck's head) is named Ladon");
    auto const& doc = ge::data(*h, "hydra");
    ctx.check(doc.is_object() && doc["HeadNames"].is_array() && doc["HeadNames"][2] == "Ladon", "HeadNames saved with the hydra");
    ctx.check(ge::ext::stringList(*h, "hydra:head_names").size() == ch::kMaxHeads, "HEAD_NAMES synced as a string list");
    co_await ctx.ticks(2);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    auto* headActor = ge::partActor(ge::ext::parts(*fe::asMob(*h))[ch::headPartIndex(2)]);
    ctx.check(headActor && headActor->getNameTag() == "Ladon", "the head part shows the name");
    // a dying head clears its name
    addHeadDamage(*h, 2, 121);
    co_await ctx.ticks(4);
    h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(headView(*h, 2)->name.empty(), "the dying head lost its name");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// The death (Hydra#tickDeath): heads die one by one every 20 ticks, the body is removed at 200 and
// the mangrove death chest is placed under the home point (F8's lifecycle).
GF_REGISTER_SELFTEST("hydra.death", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2426);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto* h = byId(uid);
    ge::setHome(*fe::asMob(*h), home, ch::kHomeRadius);
    h->kill();
    co_await ctx.ticks(25);
    h = byId(uid);
    ctx.require(h != nullptr, "the dying body stays for its animation");
    auto v0 = headView(*h, 0);
    ctx.check(v0 && (v0->state == State::Dying || v0->state == State::Dead), "head 0 dies at death time 20");
    co_await ctx.ticks(200);
    ctx.check(byId(uid) == nullptr, "the body is gone after 200 ticks");
    auto below = home.offset(0, -1, 0);
    ctx.check(ctx.blockAt(below) == "twilightforest:mangrove_chest", "the mangrove death chest is under the home point (" + ctx.blockAt(below) + ")");
    // put the floor back (the chest emptied first, so nothing drops)
    if (auto* region = game::regionOf(below.dim)) {
        if (auto inv = gf::inventory::find(*region, game::blockPos(below)); inv && inv->set)
            for (int i = 0; i < inv->slots; ++i) inv->set(below, i, game::items::empty());
        if (auto const* g = game::blockWith("minecraft:grass_block")) game::setBlock(*region, game::blockPos(below), *g);
    }
    (void)box;
});

// The boss spawner (HydraSpawnerBlockEntity, range 50): a player in range makes the hydra.
GF_REGISTER_SELFTEST("hydra.spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2420, 48, 32);
    auto            at  = ctx.testPos(2444, 2);
    ctx.require(ctx.setBlock(at, kSpawner), "the hydra boss spawner is placed");
    auto& sp = ctx.spawnPlayer();
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z - 20.5f}, DimensionType(at.dim));
    std::int64_t hydra = 0;
    for (int t = 0; t < 60 && !hydra; ++t) {
        co_await ctx.ticks(1);
        auto* region = game::regionOf(at.dim);
        if (!region) continue;
        for (auto* a : region->getEntities(AABB{Vec3{at.x - 8.0f, at.y - 4.0f, at.z - 8.0f}, Vec3{at.x + 9.0f, at.y + 14.0f, at.z + 9.0f}}, 0.0f))
            if (a && a->getTypeName() == kHydra) hydra = fe::uidOf(*a);
    }
    ctx.check(hydra != 0, "the spawner made a hydra");
    ctx.check(ctx.blockAt(at) != kSpawner, "the spawner is gone");
    auto* h = hydra ? byId(hydra) : nullptr;
    if (h) {
        auto home = ge::home(*fe::asMob(*h));
        ctx.check(home && home->radius == ch::kHomeRadius, "home radius 20");
        h->remove();
    }
    co_await ctx.ticks(2);
    (void)box;
});

// Encased fire jet on redstone: idle -> popping (powered) -> flame after 80 -> timeout after 60,
// burning what stands above (2 fire_jet damage, fire); timeout -> idle once unpowered.
GF_REGISTER_SELFTEST("hydra.encased_jet", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box    = co_await ctx.flatArea(2480, 8, 8);
    auto            jet    = ctx.testPos(2482, 2);
    auto            region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.require(ctx.setBlock(jet, kEncasedJet), "an encased fire jet");
    co_await ctx.ticks(3);
    ctx.check(jetState(*region, game::blockPos(jet)) == "idle", "it starts idle");
    ctx.require(ctx.setBlock(jet.offset(1, 0, 0), "minecraft:redstone_block"), "a redstone block next to it");
    std::string s;
    for (int t = 0; t < 10 && s != "popping"; ++t) {
        co_await ctx.ticks(1);
        s = jetState(*region, game::blockPos(jet));
    }
    ctx.check(s == "popping", "powered: popping (" + s + ")");
    auto* golem = spawnMob(jet.offset(0, 1, 0), "minecraft:iron_golem");
    ctx.require(golem != nullptr, "a golem on the jet");
    auto gid    = fe::uidOf(*golem);
    int  before = golem->getHealth();
    for (int t = 0; t < 85 && s != "flame"; ++t) {
        co_await ctx.ticks(1);
        s = jetState(*region, game::blockPos(jet));
    }
    ctx.check(s == "flame", "flame after 80 popping ticks (" + s + ")");
    co_await ctx.ticks(12);
    auto* g = byId(gid);
    ctx.check(g && g->getHealth() < before, "the flame hurt the golem (" + std::to_string(g ? before - g->getHealth() : 0) + ")");
    ctx.check(g && g->isOnFire(), "and set it on fire");
    for (int t = 0; t < 60 && s != "timeout"; ++t) {
        co_await ctx.ticks(1);
        s = jetState(*region, game::blockPos(jet));
    }
    ctx.check(s == "timeout", "timeout after 60 flame ticks (" + s + ")");
    ctx.setBlock(jet.offset(1, 0, 0), "minecraft:air");
    for (int t = 0; t < 10 && s != "idle"; ++t) {
        co_await ctx.ticks(1);
        s = jetState(*region, game::blockPos(jet));
    }
    ctx.check(s == "idle", "unpowered: idle again (" + s + ")");
    if ((g = byId(gid))) g->remove();
    (void)box;
});

// A natural fire jet takes the lava under it on a random tick and pops.
GF_REGISTER_SELFTEST("hydra.fire_jet_lava", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(2488, 6, 6);
    auto jet    = ctx.testPos(2490, 2).offset(0, 1, 0);
    auto region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.require(ctx.setBlock(jet.offset(0, -1, 0), "minecraft:lava"), "lava");
    ctx.require(ctx.setBlock(jet, kFireJet), "a fire jet over it");
    co_await ctx.ticks(1);
    randomTickJet(*region, game::blockPos(jet));
    ctx.check(jetState(*region, game::blockPos(jet)) == "popping", "the jet pops");
    ctx.check(ctx.blockAt(jet.offset(0, -1, 0)) == "minecraft:air", "the lava is used up");
    co_await ctx.ticks(2);
    ctx.check(jetCounter(jet) > 0, "the jet's block entity ticks (" + std::to_string(jetCounter(jet)) + ")");
    ctx.setBlock(jet, "minecraft:air");
    (void)box;
});

// Encased smoker: active while powered; smokers puff huge_smoke every 4 ticks.
GF_REGISTER_SELFTEST("hydra.smokers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box    = co_await ctx.flatArea(2494, 6, 6);
    auto sm     = ctx.testPos(2495, 2);
    auto plain  = ctx.testPos(2497, 2);
    auto region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.require(ctx.setBlock(sm, kEncasedSmoker), "an encased smoker");
    ctx.require(ctx.setBlock(plain, kSmoker), "a smoker");
    (void)ctx.player(); // a player near the smokers
    int puffs = smokeParticlesForTest();
    ctx.require(ctx.setBlock(sm.offset(0, 0, 1), "minecraft:redstone_block"), "power");
    bool on = false;
    for (int t = 0; t < 10 && !on; ++t) {
        co_await ctx.ticks(1);
        on = game::statesOf(region->getBlock(game::blockPos(sm)))["twilightforest:active"] == "true";
    }
    ctx.check(on, "powered: active");
    co_await ctx.ticks(20);
    ctx.check(smokeParticlesForTest() >= puffs + 5, "the smokers puff (" + std::to_string(smokeParticlesForTest() - puffs) + ")");
    ctx.setBlock(sm.offset(0, 0, 1), "minecraft:air");
    for (int t = 0; t < 10 && on; ++t) {
        co_await ctx.ticks(1);
        on = game::statesOf(region->getBlock(game::blockPos(sm)))["twilightforest:active"] == "true";
    }
    ctx.check(!on, "unpowered: inactive");
    (void)box;
});

// The client e2e's showcase: a hydra in front of the real player (idle heads, then a fire breath and
// a mortar), the fire jets and smokers beside it. Without a real player (the server runs) it only
// checks that it can find none.
GF_REGISTER_SELFTEST("hydra.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    Player* real = nullptr;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
            if (!p.isSimulatedPlayer()) real = &p;
            return real == nullptr;
        }));
    if (!real) {
        ctx.info("no real player: nothing to show");
        co_return;
    }
    auto   f   = real->getFeetPos();
    auto   v   = real->getViewVector(1.0f);
    int    dim = static_cast<int>(real->getDimensionId());
    double len = std::sqrt(double(v.x) * v.x + double(v.z) * v.z);
    double fx = len > 1e-6 ? v.x / len : 0.0, fz = len > 1e-6 ? v.z / len : 1.0;
    auto   spot = [&](double ahead, double side) {
        return core::GlobalPos{dim, static_cast<int>(std::floor(f.x + fx * ahead - fz * side)), static_cast<int>(std::floor(f.y)), static_cast<int>(std::floor(f.z + fz * ahead + fx * side))};
    };
    ctx.info("showing the hydra to " + real->getRealName());
    // the client e2e world is peaceful: the boss kit would turn the hydra back into its spawner
    DifficultyScope normal(Difficulty::Normal);
    // the hydra 22 blocks ahead, facing the player
    auto at = spot(22.0, 0.0);
    auto uid = spawnHydra(ctx, at, 2499);
    ctx.require(uid != 0, "the hydra spawns");
    if (auto* h = byId(uid)) {
        float yaw = static_cast<float>(std::atan2(-fz, -fx) * 180.0 / 3.141592653589793) - 90.0F;
        const_cast<Vec2&>(h->getRotation()) = Vec2{0.0F, yaw};
    }
    ctx.require(co_await ready(uid), "the hydra ticks");
    co_await ctx.ticks(40);
    ctx.info("hydra ready");
    co_await ctx.ticks(100);
    // a fire breath from head 0, aimed at the ground 9 blocks in front of the player and 3 to its
    // left (in frame, and nobody burns)
    if (auto* h = byId(uid)) {
        auto aim = spot(9.0, -3.0);
        aimHeadForTest(*h, 0, Vec3{aim.x + 0.5F, float(aim.y), aim.z + 0.5F});
        forceHeadState(*h, 0, State::FlameBeginning);
    }
    co_await ctx.ticks(50);
    ctx.info("flame ready");
    co_await ctx.ticks(60);
    // a head looking up and down (pitch), its mouth half open, then a bite pose
    if (auto* h = byId(uid)) {
        auto high = spot(4.0, 2.0);
        aimHeadForTest(*h, 1, Vec3{high.x + 0.5F, float(high.y) + 14.0F, high.z + 0.5F});
        forceHeadState(*h, 1, State::FlameBeginning);
        aimHeadForTest(*h, 2, Vec3{high.x + 0.5F, float(high.y), high.z + 0.5F});
        forceHeadState(*h, 2, State::BiteBeginning);
    }
    co_await ctx.ticks(30);
    ctx.info("pitch ready");
    co_await ctx.ticks(60);
    // a head dying: it flips onto its side and its parts burst
    if (auto* h = byId(uid)) addHeadDamage(*h, 1, 121);
    co_await ctx.ticks(12);
    ctx.info("dying head ready");
    co_await ctx.ticks(60);
    // jets and smokers 6 blocks ahead, 2 apart; the encased ones get their redstone one block
    // further away once their watch runs (a powered neighbour placed before is no change)
    auto jet  = spot(6.0, -4.0);
    auto ejet = spot(6.0, -2.0);
    auto smk  = spot(6.0, 2.0);
    auto esmk = spot(6.0, 4.0);
    ctx.setBlock(jet, kFireJet, {{"twilightforest:state", "flame"}});
    ctx.setBlock(ejet, kEncasedJet);
    ctx.setBlock(smk, kSmoker);
    ctx.setBlock(esmk, kEncasedSmoker);
    for (int t = 0; t < 40 && !(encasedWatched(ejet) && encasedWatched(esmk)); ++t) co_await ctx.ticks(1);
    co_await ctx.ticks(4);
    ctx.setBlock(spot(7.0, -2.0), "minecraft:redstone_block");
    ctx.setBlock(spot(7.0, 4.0), "minecraft:redstone_block");
    co_await ctx.ticks(85);
    ctx.info("jets ready");
    co_await ctx.ticks(60);
    // a mortar from head 0
    if (auto* h = byId(uid))
        if (auto hv = headView(*h, 0)) launchMortar(*fe::asMob(*h), 0, hv->pos, hv->xRot, hv->yRot, false);
    co_await ctx.ticks(10);
    ctx.info("mortar ready");
    co_await ctx.ticks(100);
    discard(uid);
    co_await ctx.ticks(2);
});

// Shield shatter (HydraHeadContainer#bite): a player blocking with a shield loses 112 durability
// with the shatter sound, the shield cools down 200 ticks and the player is pushed back.
GF_REGISTER_SELFTEST("hydra.shield_shatter", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2427);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto& sp   = ctx.spawnPlayer("gf_hydra_shield");
    auto  spId = sp.getOrCreateUniqueID().rawID;
    auto  v    = headView(*byId(uid), 0);
    ctx.require(v.has_value(), "head 0");
    sp.teleport(Vec3{v->pos.x, float(home.y), v->pos.z}, DimensionType(home.dim));
    auto shield = game::items::make("minecraft:shield", 1);
    ctx.require(shield.has_value(), "a shield");
    sp.setOffhandSlot(*shield);
    resistant(sp, 30);
    sp.simulateSneaking();
    bool blocking = false;
    for (int t = 0; t < 20 && !blocking; ++t) {
        co_await ctx.ticks(1);
        auto* pl = fe::playerById(spId);
        blocking = pl && pl->isBlocking();
    }
    ctx.require(blocking, "the player blocks with the shield (sneaking)");
    // bite: keep the player in head 0's box while it bites
    forceHeadState(*byId(uid), 0, State::Biting);
    bool hit = false;
    for (int t = 0; t < 6; ++t) {
        auto* h  = byId(uid);
        auto* pl = fe::playerById(spId);
        if (!h || !pl) break;
        if (auto hv = headView(*h, 0); hv && hv->state == State::Biting) pl->teleport(Vec3{hv->pos.x, hv->pos.y, hv->pos.z}, DimensionType(home.dim));
        co_await ctx.ticks(1);
        if ((pl = fe::playerById(spId)) && pl->getItemCooldownLeft(HashedString("shield")) > 0) hit = true;
    }
    auto* pl = fe::playerById(spId);
    ctx.require(pl != nullptr, "the player is there");
    ctx.check(hit, "the bite put the shield on cooldown");
    int left = pl->getItemCooldownLeft(HashedString("shield"));
    ctx.check(left > 180 && left <= 200, "the shield cools down for 200 ticks (" + std::to_string(left) + " left)");
    int damage = pl->getOffhandSlot().getDamageValue();
    ctx.check(damage == 112, "the shield took 112 durability (" + std::to_string(damage) + ")");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// The mortar's fuse (HydraMortar#tick): on the ground it counts 80 down and detonates on the 81st
// ground tick (fuse-- <= 0).
GF_REGISTER_SELFTEST("hydra.mortar_fuse", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2428);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto* h = byId(uid);
    auto  v = headView(*h, 0);
    ctx.require(v.has_value(), "head 0");
    int   det0 = detonationsForTest();
    // straight down onto the flat floor from 2 blocks up beside the hydra (no wall to slide into)
    auto* m = launchMortar(*fe::asMob(*h), 0, v->pos, v->xRot, v->yRot, false);
    ctx.require(m != nullptr, "a mortar was launched");
    auto mid = fe::uidOf(*m);
    m->moveTo(Vec3{home.x + 0.5F, float(home.y) + 2.0F, home.z - 12.5F}, Vec2{0, 0});
    gf::projectile::ext::setVelocity(*m, Vec3{0.0F, -0.2F, 0.0F});
    int landed = -1, detonated = -1;
    for (int t = 0; t < 140 && detonated < 0; ++t) {
        co_await ctx.ticks(1);
        if (auto* mm = byId(mid); mm && landed < 0)
            if (auto mv = mortarView(*mm); mv && mv->onGround) landed = t;
        if (detonationsForTest() > det0) detonated = t;
    }
    ctx.require(landed >= 0 && detonated >= 0, "the mortar landed and detonated");
    int fuse = detonated - landed;
    ctx.check(fuse >= 80 && fuse <= 81, "it detonated 80 ticks after landing (" + std::to_string(fuse) + ")");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// Head names across a save and a load (Hydra#addAdditionalSaveData / readAdditionalSaveData): the
// hydra goes into a structure (NBT, F8's save hook) and comes back as a new actor with the names.
GF_REGISTER_SELFTEST("hydra.names_reload", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2429);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto& sp   = ctx.spawnPlayer();
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 6.5f}, DimensionType(home.dim));
    co_await ctx.ticks(2);
    auto* pl = fe::playerById(spId);
    auto* h  = byId(uid);
    ctx.require(pl && h, "player and hydra");
    auto tag = game::items::make("minecraft:name_tag", 1);
    ctx.require(tag.has_value(), "a name tag");
    tag->setCustomName(Bedrock::Safety::RedactableString(std::string("Kerberos")));
    pl->setCarriedItem(*tag);
    auto* head1 = ge::partActor(ge::ext::parts(*fe::asMob(*h))[ch::headPartIndex(1)]);
    ctx.require(head1 != nullptr, "head 1's part");
    std::string itemId = "minecraft:name_tag", type = head1->getTypeName();
    gf::interactions::EntityContext c{*pl, pl->getCarriedItem(), itemId, *head1, type, {0.0, 0.0, 0.0}, false};
    ctx.require(gf::interactions::dispatchInteract(c) == gf::interactions::Result::Consume, "head 1 named");
    co_await ctx.ticks(2);
    // save: the hydra in a structure (memory), then gone
    auto f = byId(uid)->getFeetPos();
    int  x = int(std::floor(f.x)), y = int(std::floor(f.y)), z = int(std::floor(f.z));
    auto at = [](int a, int b, int cc) { return std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(cc); };
    auto saved = gf::selftest::runServerCommand("structure save gf_hydra_names " + at(x - 1, y, z - 1) + " " + at(x + 1, y + 3, z + 1) + " true memory false", home.dim);
    ctx.require(saved.ok, "the hydra was saved (" + saved.messages + ")");
    discard(uid);
    co_await ctx.ticks(3);
    ctx.require(byId(uid) == nullptr, "the first hydra is gone");
    auto loaded = gf::selftest::runServerCommand("structure load gf_hydra_names " + at(x - 1, y, z - 1) + " 0_degrees none true false", home.dim);
    ctx.require(loaded.ok, "the hydra was loaded (" + loaded.messages + ")");
    std::int64_t again = 0;
    for (int t = 0; t < 20 && !again; ++t) {
        co_await ctx.ticks(1);
        if (auto* region = game::regionOf(home.dim))
            for (auto* a : region->getEntities(AABB{Vec3{x - 3.0F, y - 2.0F, z - 3.0F}, Vec3{x + 4.0F, y + 5.0F, z + 4.0F}}, 0.0f))
                if (a && a->getTypeName() == kHydra && fe::uidOf(*a) != uid) again = fe::uidOf(*a);
    }
    ctx.require(again != 0, "a hydra came back from the save");
    co_await ctx.ticks(3);
    auto* back = byId(again);
    ctx.require(back != nullptr && headView(*back, 1).has_value(), "the loaded hydra ticks");
    ctx.check(headView(*back, 1)->name == "Kerberos", "head 1 is still named Kerberos (" + headView(*back, 1)->name + ")");
    ctx.check(headView(*back, 0)->name.empty(), "head 0 has no name");
    auto* named = ge::partActor(ge::ext::parts(*fe::asMob(*back))[ch::headPartIndex(1)]);
    ctx.check(named && named->getNameTag() == "Kerberos", "the loaded head part shows the name");
    (void)gf::selftest::runServerCommand("structure delete gf_hydra_names", home.dim);
    discard(again);
    co_await ctx.ticks(2);
    (void)box;
});

// Conquest (BaseTFBoss#postmortem): the lair forced around the home point, the hydra its spawner
// made killed: after the death animation the lair start is conquered and the death chest stands.
GF_REGISTER_SELFTEST("hydra.conquest", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box    = co_await ctx.flatArea(2400, 144, 40);
    auto            center = ctx.testPos(2420, 2).offset(0, 6, 18);
    (void)gf::landmark::ext::forgetStartForTest(center, kLair);
    std::optional<gf::landmark::ext::ScopedStart> forced;
    forced.emplace(kLair, center);
    ctx.require(*forced && (*forced)->generated, "forceStart made the hydra lair");
    // the spawner at local (27, 3, 27) of the piece
    core::GlobalPos spawnerAt{};
    bool            found = false;
    if (auto* region = game::regionOf(center.dim))
        for (int dx = -10; dx <= 70 && !found; ++dx)
            for (int dz = -10; dz <= 70 && !found; ++dz)
                for (int dy = 0; dy <= 8 && !found; ++dy)
                    if (game::blockIdAt(*region, BlockPos{center.x + dx, center.y + dy, center.z + dz}) == kSpawner) {
                        spawnerAt = center.offset(dx, dy, dz);
                        found     = true;
                    }
    ctx.require(found, "the lair has its hydra boss spawner");
    auto start = gf::landmark::startAt(spawnerAt.dim, game::blockPos(spawnerAt));
    ctx.require(start.has_value() && start->key == kLair, "the spawner is inside the hydra lair start");
    ctx.check(!gf::landmark::isConquered(*start), "the lair is not conquered before the fight");
    auto& sp = ctx.spawnPlayer("gf_hydra_conquest");
    sp.teleport(Vec3{spawnerAt.x + 0.5f, float(spawnerAt.y) + 1.0f, spawnerAt.z - 18.5f}, DimensionType(spawnerAt.dim));
    resistant(sp, 60);
    std::int64_t uid = 0;
    for (int t = 0; t < 80 && !uid; ++t) {
        co_await ctx.ticks(1);
        if (auto* region = game::regionOf(spawnerAt.dim))
            for (auto* a : region->getEntities(AABB{Vec3{spawnerAt.x - 8.0f, spawnerAt.y - 4.0f, spawnerAt.z - 8.0f}, Vec3{spawnerAt.x + 9.0f, spawnerAt.y + 14.0f, spawnerAt.z + 9.0f}}, 0.0f))
                if (a && a->getTypeName() == kHydra) uid = fe::uidOf(*a);
    }
    ctx.require(uid != 0, "the spawner made the hydra");
    ctx.require(co_await ready(uid), "the hydra ticks");
    auto home = ge::home(*fe::asMob(*byId(uid)));
    ctx.require(home.has_value(), "the spawner gave it a home");
    byId(uid)->kill();
    for (int t = 0; t < 260 && byId(uid); ++t) co_await ctx.ticks(1);
    ctx.check(byId(uid) == nullptr, "the body is gone after its death animation");
    co_await ctx.ticks(5);
    auto after = gf::landmark::startAt(spawnerAt.dim, game::blockPos(spawnerAt));
    ctx.check(after.has_value() && gf::landmark::isConquered(*after), "the hydra lair is conquered");
    // the death chest near the home point (EntityUtil#bossChestLocation), emptied so the area's
    // restore drops nothing
    int chests = 0;
    if (auto* region = game::regionOf(home->pos.dim))
        for (int dx = -3; dx <= 3; ++dx)
            for (int dy = -3; dy <= 3; ++dy)
                for (int dz = -3; dz <= 3; ++dz) {
                    auto p = home->pos.offset(dx, dy, dz);
                    if (ctx.blockAt(p) != "twilightforest:mangrove_chest") continue;
                    ++chests;
                    if (auto inv = gf::inventory::find(*region, game::blockPos(p)); inv && inv->set)
                        for (int i = 0; i < inv->slots; ++i) inv->set(p, i, game::items::empty());
                }
    ctx.check(chests > 0, "the mangrove death chest stands at the home point");
    forced.reset();
    (void)box;
});

// A seeded fight with the brain on (Hydra#customServerAiStep): the hydra takes the player in reach
// as its target, turns to it and its heads pick attacks (bite / flame / mortar odds).
GF_REGISTER_SELFTEST("hydra.brain_fight", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    auto            uid  = spawnHydra(ctx, home, 2430);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    if (auto* h = byId(uid)) setBrainEnabled(*h, true);
    auto& sp   = ctx.spawnPlayer("gf_hydra_brain");
    auto  spId = sp.getOrCreateUniqueID().rawID;
    sp.teleport(Vec3{home.x + 0.5f, float(home.y), home.z + 14.5f}, DimensionType(home.dim));
    resistant(sp, 30);
    int  firstAttack = -1, head = -1;
    for (int t = 0; t < 300 && firstAttack < 0; ++t) {
        co_await ctx.ticks(1);
        auto* h = byId(uid);
        if (!h) break;
        for (int i = 0; i < ch::kMaxHeads && firstAttack < 0; ++i)
            if (auto v = headView(*h, i); v && attacking(v->state)) {
                firstAttack = t;
                head        = i;
            }
    }
    ctx.check(firstAttack >= 0, "a head picked an attack (head " + std::to_string(head) + " after " + std::to_string(firstAttack) + " ticks)");
    co_await ctx.ticks(40);
    auto* h  = byId(uid);
    auto* pl = fe::playerById(spId);
    ctx.require(h && pl, "hydra and player");
    // the body turned to the player (10 degrees a tick at most)
    auto   f   = h->getFeetPos(), p = pl->getFeetPos();
    float  want = static_cast<float>(std::atan2(p.z - f.z, p.x - f.x) * 180.0 / 3.141592653589793) - 90.0F;
    float  diff = std::fmod(std::fabs(h->getRotation().y - want) + 540.0F, 360.0F) - 180.0F;
    ctx.check(std::fabs(diff) < 30.0F, "the hydra faces the player (" + std::to_string(diff) + " degrees off)");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

// The Java box (16 x 12) rests over a gap the 2 x 3 Bedrock box would fall into: no gravity while
// a block under the Java footprint holds it.
GF_REGISTER_SELFTEST("hydra.rests_on_java_box", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2420, 48, 32);
    auto            home = ctx.testPos(2444, 2);
    // a stone deck 3 above the ground (inside the flat area) with a 4 x 4 hole under the spawn point
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto const* stone = game::blockWith("minecraft:stone");
    ctx.require(stone != nullptr, "stone");
    for (int dx = -9; dx <= 9; ++dx)
        for (int dz = -9; dz <= 9; ++dz)
            if (dx < -2 || dx > 1 || dz < -2 || dz > 1) game::setBlock(*region, BlockPos{home.x + dx, home.y + 3, home.z + dz}, *stone);
    auto deck = home.offset(0, 4, 0);
    auto uid  = spawnHydra(ctx, deck, 2431);
    ctx.require(uid != 0, "the hydra spawns");
    ctx.require(co_await ready(uid), "the hydra ticks");
    co_await ctx.ticks(40);
    auto* h = byId(uid);
    ctx.require(h != nullptr, "the hydra is there");
    ctx.check(std::fabs(h->getFeetPos().y - float(deck.y)) < 0.1F, "it stands on the deck over the hole (y " + std::to_string(h->getFeetPos().y) + ")");
    ctx.check(!hasGravityForTest(*h), "resting on the Java box: no gravity");
    discard(uid);
    co_await ctx.ticks(2);
    (void)box;
});

} // namespace gf::feature::hydra::tests

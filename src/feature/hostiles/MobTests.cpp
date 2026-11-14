// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// U17 (hostiles) selftests (rule R12: "hostiles.*", testPos dx 1700..1799): F13's goldens on the
// live actors, and each mob's special against a SimulatedPlayer: the mist wolf's blindness in the
// dark (none on Easy), the winter wolf's and the fire beetle's breath, the pinch beetle's grab, the
// redcap sapper planting and a redcap lighting TNT, the druid's poisoning nature bolt and the bolt
// that turns a log into birch leaves, the slime blob's 4 damage, the swarm spider's 1-in-4 bite,
// the king spider's druid rider, the wraith's flight home through a wall, and the loot tables.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "core/jrand/JRand.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/hostiles/Common.h"
#include "feature/hostiles/Hostiles.h"
#include "feature/loot/Loot.h"
#include "feature/projectile/Projectile.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::hostiles::tests {

namespace {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;
using selftest::Context;
using Difficulty = SharedTypes::Legacy::Difficulty;

struct DifficultyScope {
    Difficulty previous = Difficulty::Peaceful;
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

/// mobGriefing for a test, the previous value restored afterwards.
struct GriefingScope {
    bool previous = mobGriefing();
    explicit GriefingScope(bool on) { (void)selftest::runServerCommand(std::string("gamerule mobgriefing ") + (on ? "true" : "false")); }
    ~GriefingScope() { (void)selftest::runServerCommand(std::string("gamerule mobgriefing ") + (previous ? "true" : "false")); }
};

/// The test player in survival for the test, creative again afterwards.
struct SurvivalScope {
    Context& ctx;
    explicit SurvivalScope(Context& c) : ctx(c) { ctx.player().setPlayerGameType(GameType::Survival); }
    ~SurvivalScope() {
        auto& p = ctx.player();
        if (p.getVehicle()) ge::ext::hostileDismount(p);
        p.setPlayerGameType(GameType::Creative);
        p.heal(20);
    }
};

/// The damage types dealt to one victim while a test listens.
struct DealtLog {
    std::int64_t             victim = 0;
    std::vector<std::string> types;
    std::vector<float>       amounts;
};
DealtLog& dealt() {
    static DealtLog d;
    static bool     registered = false;
    if (!registered) {
        registered = true;
        gf::combat::onDealt(gf::combat::priority::Lowest, [](gf::combat::DamageInfo const& info) {
            if (dealt().victim != 0 && uidOf(info.victim) == dealt().victim) {
                dealt().types.push_back(info.type);
                dealt().amounts.push_back(info.amount);
            }
        });
    }
    return d;
}
void listen(Actor& victim) {
    dealt().victim = uidOf(victim);
    dealt().types.clear();
    dealt().amounts.clear();
}
bool dealtType(std::string const& type) { return std::find(dealt().types.begin(), dealt().types.end(), type) != dealt().types.end(); }

float attribute(Mob& m, Attribute const& a) {
    auto ref = m.getMutableAttribute(a);
    if (auto* inst = ref.mInstance->mPtr) return inst->mCurrentValue;
    return -1.0f;
}

bool near(float a, float b, float eps = 0.011f) { return std::abs(a - b) < eps; }

Mob* spawn(Context& ctx, char const* type, Vec3 const& at) {
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto* m = hostiles::spawnMob(*region, type, at, gf::spawn::SpawnReason::Command);
    ctx.require(m != nullptr, std::string("spawned ") + type);
    return m;
}

Mob* mobById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a ? asMob(*a) : nullptr;
}

void removeIfThere(std::int64_t uid) {
    if (auto* a = actorById(uid)) a->remove();
}

Vec3 center(gf::core::GlobalPos const& p, float dy = 0.0f) { return Vec3{p.x + 0.5f, float(p.y) + dy, p.z + 0.5f}; }

/// A closed stone box around the inner volume [min, min + size) (light 0 inside).
void stoneBox(Context& ctx, gf::core::GlobalPos const& min, int sx, int sy, int sz) {
    for (int x = -1; x <= sx; ++x)
        for (int y = -1; y <= sy; ++y)
            for (int z = -1; z <= sz; ++z) {
                bool inside = x >= 0 && x < sx && y >= 0 && y < sy && z >= 0 && z < sz;
                auto p      = min.offset(x, y, z);
                ctx.setBlock(p, inside ? "minecraft:air" : "minecraft:stone");
            }
}

struct Golden {
    char const* id;
    float       health, width, height, movement, attack;
};
// tests/golden/mobs.json (F13, derived from upstream 4.8): maxHealth, width, height and the movement_speed /
// attack_damage attributes (follow range 16 for all)
constexpr Golden kGoldens[] = {
    {id::HostileWolf, 20.0f, 0.6f, 0.85f, 0.3f, 2.0f},  {id::MistWolf, 30.0f, 1.4f, 1.9f, 0.3f, 6.0f},
    {id::WinterWolf, 30.0f, 1.4f, 1.9f, 0.3f, 6.0f},    {id::KingSpider, 30.0f, 1.6f, 1.6f, 0.35f, 6.0f},
    {id::HedgeSpider, 16.0f, 1.4f, 0.9f, 0.3f, 2.0f},   {id::SwarmSpider, 3.0f, 0.8f, 0.4f, 0.3f, 1.0f},
    {id::Druid, 20.0f, 0.6f, 1.99f, 0.25f, 2.0f},       {id::Redcap, 20.0f, 0.9f, 1.4f, 0.28f, 2.0f},
    {id::RedcapSapper, 30.0f, 0.9f, 1.4f, 0.28f, 2.0f}, {id::Wraith, 20.0f, 0.6f, 2.1f, 0.5f, 5.0f},
    {id::FireBeetle, 25.0f, 1.1f, 0.5f, 0.23f, 4.0f},   {id::SlimeBeetle, 25.0f, 0.9f, 0.5f, 0.23f, 4.0f},
    {id::PinchBeetle, 40.0f, 1.2f, 0.5f, 0.23f, 4.0f},
};

} // namespace

GF_REGISTER_SELFTEST("hostiles.mob_goldens", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(1700, 24, 12);
    (void)box;
    auto at = ctx.testPos(1705, 2).offset(0, 1, 0);
    for (auto const& g : kGoldens) {
        auto* m = spawn(ctx, g.id, center(at));
        auto  b = m->getAABB();
        ctx.check(near(m->getMaxHealth(), g.health), std::string(g.id) + " max health " + std::to_string(m->getMaxHealth()));
        ctx.check(near(b.max.x - b.min.x, g.width) && near(b.max.y - b.min.y, g.height),
                  std::string(g.id) + " box " + std::to_string(b.max.x - b.min.x) + " x " + std::to_string(b.max.y - b.min.y));
        ctx.check(near(attribute(*m, SharedAttributes::MOVEMENT_SPEED()), g.movement), std::string(g.id) + " movement " + std::to_string(attribute(*m, SharedAttributes::MOVEMENT_SPEED())));
        ctx.check(near(attribute(*m, SharedAttributes::FOLLOW_RANGE()), 16.0f), std::string(g.id) + " follow range");
        ctx.check(near(attribute(*m, SharedAttributes::ATTACK_DAMAGE()), g.attack), std::string(g.id) + " attack " + std::to_string(attribute(*m, SharedAttributes::ATTACK_DAMAGE())));
        ctx.check((g.id == std::string(id::Wraith) || g.id == std::string(id::FireBeetle)) == m->isFireImmune(), std::string(g.id) + " fire immunity");
        m->remove();
    }
    co_return;
});

// MistWolf#doHurtTarget: a bite at light 0 blinds for 7 s on Normal, not at all on Easy
GF_REGISTER_SELFTEST("hostiles.mist_wolf_blindness", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy); // a monster leaves on Peaceful (Mob#checkDespawn)
    auto            box = co_await ctx.flatArea(1710, 12, 8);
    (void)box;
    auto inner = ctx.testPos(1712, 0).offset(0, 1, 0);
    stoneBox(ctx, inner, 3, 3, 4);
    SurvivalScope survival(ctx);
    ctx.player().teleportTo(center(inner.offset(1, 0, 3)), true, 0, 1, false);
    auto* wolf = spawn(ctx, id::MistWolf, center(inner.offset(1, 0, 1)));
    auto  wid  = uidOf(*wolf);
    co_await ctx.ticks(10); // the light engine settles inside the box
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    ctx.require(mobById(wid) != nullptr, "the wolf is there");
    int light = maxLocalRawBrightness(*region, blockPosOf(*mobById(wid)));
    ctx.require(light == 0, "dark inside the box (light " + std::to_string(light) + ")");
    for (auto [diff, expect] : {std::pair{Difficulty::Normal, 7 * 20}, std::pair{Difficulty::Easy, 0}}) {
        DifficultyScope d(diff);
        ctx.player().removeAllEffects();
        ctx.player().heal(20);
        auto* w = mobById(wid);
        ctx.require(w != nullptr, "the wolf is there");
        bool hit = mobs::doHurtTarget(*w, ctx.player());
        co_await ctx.ticks(1);
        auto const* blind = ctx.player().getEffect(Effect::Blindness);
        if (expect > 0) {
            ctx.check(hit, "the bite landed");
            ctx.check(blind != nullptr, "the bite blinds on Normal");
            if (blind) {
                int ticks = blind->mDuration->mValue;
                ctx.check(std::abs(ticks - expect) <= 2, "7 s of blindness (" + std::to_string(ticks) + " ticks)");
            }
        } else {
            ctx.check(blind == nullptr, "no blindness on Easy");
        }
        co_await ctx.ticks(12); // the player's invulnerable time
    }
    removeIfThere(wid);
    co_return;
});

// WinterWolf: BreathAttackGoal (5, 30, 0.1) on the mob that hurt it: 2 mob_attack damage per tick
GF_REGISTER_SELFTEST("hostiles.winter_wolf_breath", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1720, 16, 8);
    (void)box;
    SurvivalScope survival(ctx);
    auto          at = ctx.testPos(1722, 2).offset(0, 1, 0);
    ctx.player().teleportTo(center(at.offset(3, 0, 0)), true, 0, 1, false);
    auto* wolf = spawn(ctx, id::WinterWolf, center(at));
    auto  wid  = uidOf(*wolf);
    ge::seedForTest(*wolf, 1700);
    listen(ctx.player());
    gf::combat::hurt(*wolf, 1.0f, "minecraft:player_attack", &ctx.player(), &ctx.player());
    bool breathed = false;
    for (int i = 0; i < 400 && !breathed; ++i) {
        co_await ctx.ticks(1);
        auto* w = mobById(wid);
        ctx.require(w != nullptr && w->isAlive(), "the wolf is there");
        if (ge::ext::isBreathing(*w)) breathed = true;
        ctx.player().heal(20);
    }
    ctx.check(breathed, "the winter wolf breathes at the player that hurt it");
    for (int i = 0; i < 40 && !dealtType("minecraft:mob_attack"); ++i) co_await ctx.ticks(1);
    ctx.check(dealtType("minecraft:mob_attack"), "the frost breath hurts the player");
    // WinterWolf#doBreathAttack: 2 damage
    listen(ctx.player());
    co_await ctx.ticks(12);
    if (auto* w = mobById(wid)) ge::ext::doBreathAttack(*w, ctx.player(), "minecraft:mob_attack");
    ctx.check(!dealt().amounts.empty() && near(dealt().amounts.back(), 2.0f, 0.05f), "the breath deals 2");
    listen(ctx.player());
    dealt().victim = 0;
    removeIfThere(wid);
    co_return;
});

// FireBeetle#doBreathAttack: 2 scorched damage and 10 s of fire; a fire immune target takes none
GF_REGISTER_SELFTEST("hostiles.fire_beetle_breath", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1730, 12, 8);
    (void)box;
    SurvivalScope survival(ctx);
    auto          at = ctx.testPos(1732, 2).offset(0, 1, 0);
    ctx.player().teleportTo(center(at.offset(3, 0, 0)), true, 0, 1, false);
    auto* beetle = spawn(ctx, id::FireBeetle, center(at));
    auto  bid    = uidOf(*beetle);
    listen(ctx.player());
    ge::ext::doBreathAttack(*beetle, ctx.player(), "gloamingforest:scorched");
    ctx.check(dealtType("gloamingforest:scorched"), "the breath is scorched damage");
    ctx.check(!dealt().amounts.empty() && near(dealt().amounts.back(), 2.0f, 0.05f), "the breath deals 2");
    ctx.check(ctx.player().isOnFire(), "the player burns");
    // a breath attack while breathing (doHurtTarget): scorched only
    ctx.player().stopFire();
    dealt().victim = 0;
    removeIfThere(bid);
    // a fire immune target: nothing
    auto* other = spawn(ctx, id::FireBeetle, center(at.offset(0, 0, 2)));
    auto* b2    = spawn(ctx, id::FireBeetle, center(at.offset(2, 0, 2)));
    float before = static_cast<float>(other->getHealth());
    ge::ext::doBreathAttack(*b2, *other, "gloamingforest:scorched");
    ctx.check(static_cast<float>(other->getHealth()) >= before, "a fire beetle is fire immune");
    other->remove();
    b2->remove();
    co_return;
});

// PinchBeetle#doHurtTarget: the player is grabbed (rides the beetle, cannot dismount), squeezed with
// clamped damage, and released when it turns creative
GF_REGISTER_SELFTEST("hostiles.pinch_beetle_grab", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1740, 12, 8);
    (void)box;
    SurvivalScope survival(ctx);
    auto          at = ctx.testPos(1742, 2).offset(0, 1, 0);
    ctx.player().teleportTo(center(at.offset(1, 0, 0)), true, 0, 1, false);
    auto* beetle = spawn(ctx, id::PinchBeetle, center(at));
    auto  bid    = uidOf(*beetle);
    co_await ctx.ticks(2);
    listen(ctx.player());
    if (auto* b = mobById(bid)) (void)mobs::doHurtTarget(*b, ctx.player());
    co_await ctx.ticks(2);
    auto* b = mobById(bid);
    ctx.require(b != nullptr, "the beetle is there");
    ctx.check(ctx.player().getVehicle() == b, "the player rides the beetle");
    ctx.check(dealtType("gloamingforest:clamped"), "the grab squeezes with clamped damage");
    ctx.check(ge::ext::isRidingUnfriendly(ctx.player()), "the player rides unfriendly");
    ctx.player().stopRiding(true, false, false, false);
    co_await ctx.ticks(2);
    ctx.check(ctx.player().getVehicle() == mobById(bid), "the player cannot dismount");
    auto box2 = mobById(bid) ? mobById(bid)->getAABB() : AABB{};
    ctx.check(near(box2.max.x - box2.min.x, 2.2f, 0.05f), "the carrying beetle is 2.2 wide");
    ctx.player().setPlayerGameType(GameType::Creative);
    co_await ctx.ticks(3);
    ctx.check(ctx.player().getVehicle() == nullptr, "a creative player is let go");
    dealt().victim = 0;
    removeIfThere(bid);
    co_return;
});

// RedcapPlantTNTGoal: a sapper behind an unwary target plants TNT; RedcapLightTNTGoal: a redcap
// lights TNT within 8 blocks
GF_REGISTER_SELFTEST("hostiles.redcap_tnt", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    GriefingScope   griefing(true);
    auto            box = co_await ctx.flatArea(1750, 24, 8);
    (void)box;
    auto at = ctx.testPos(1760, 2).offset(0, 1, 0);
    SurvivalScope survival(ctx);
    // the player looks away (+z) from the sapper standing 3 blocks behind it (-z)
    ctx.player().teleportTo(center(at), true, 0, 1, false);
    const_cast<Vec2&>(ctx.player().getRotation()) = Vec2{0.0f, 0.0f};
    auto* sapper = spawn(ctx, id::RedcapSapper, center(at.offset(0, 0, -3)));
    auto  sid    = uidOf(*sapper);
    co_await ctx.ticks(2);
    int before = redcaps::tntLeft(*mobById(sid));
    ctx.check(before == 3, "a sapper carries 3 TNT (" + std::to_string(before) + ")");
    bool planted = false, lit = false;
    auto removePrimed = [&] {
        auto* region = ctx.region();
        if (!region) return;
        auto p = center(at);
        AABB area{Vec3{p.x - 12, p.y - 4, p.z - 12}, Vec3{p.x + 12, p.y + 8, p.z + 12}};
        for (auto* e : fe::actorsIn(*region, ctx.dimension(), area))
            if (e && e->getTypeName() == "minecraft:tnt") {
                lit = true;
                e->remove();
            }
    };
    for (int i = 0; i < 400 && !(planted && lit); ++i) {
        co_await ctx.ticks(1);
        auto* s = mobById(sid);
        ctx.require(s != nullptr, "the sapper is there");
        if (!targetOf(*s)) s->setTarget(&ctx.player());
        const_cast<Vec2&>(ctx.player().getRotation()) = Vec2{0.0f, 0.0f};
        ctx.player().heal(20);
        if (redcaps::tntLeft(*s) < before) planted = true;
        removePrimed();
    }
    ctx.check(planted, "the sapper planted TNT behind the player");
    ctx.check(lit, "the planted TNT was lit");
    removeIfThere(sid);
    // a redcap and a TNT block 4 blocks away: lit
    lit       = false;
    auto* red = spawn(ctx, id::Redcap, center(at.offset(6, 0, 0)));
    auto  rid = uidOf(*red);
    ctx.setBlock(at.offset(6, 0, 4), "minecraft:tnt");
    for (int i = 0; i < 400 && !lit; ++i) {
        co_await ctx.ticks(1);
        removePrimed();
    }
    ctx.check(lit, "the redcap lit the TNT block");
    ctx.check(ctx.blockAt(at.offset(6, 0, 4)) != "minecraft:tnt", "the TNT block is gone");
    removeIfThere(rid);
    co_return;
});

// SkeletonDruid#performRangedAttack: the nature bolt poisons (3 s on Normal) with leaf_brain damage;
// NatureBolt#onHitBlock: a log becomes persistent birch leaves
GF_REGISTER_SELFTEST("hostiles.druid_nature_bolt", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    GriefingScope   griefing(true);
    auto            box = co_await ctx.flatArea(1770, 16, 8);
    (void)box;
    SurvivalScope survival(ctx);
    auto          at = ctx.testPos(1772, 2).offset(0, 1, 0);
    ctx.player().teleportTo(center(at.offset(4, 0, 0)), true, 0, 1, false);
    auto* druid = spawn(ctx, id::Druid, center(at));
    auto  did   = uidOf(*druid);
    co_await ctx.ticks(3);
    ctx.check(gf::game::items::idOf(mobById(did)->getCarriedItem()) == "minecraft:golden_hoe", "the druid holds a golden hoe");
    listen(ctx.player());
    ctx.player().removeAllEffects();
    bool hit = false;
    for (int shot = 0; shot < 4 && !hit; ++shot) {
        if (auto* d = mobById(did)) druid::performRangedAttack(*d, ctx.player());
        for (int i = 0; i < 40 && !hit; ++i) {
            co_await ctx.ticks(1);
            hit = dealtType("gloamingforest:leaf_brain");
        }
    }
    ctx.check(hit, "a nature bolt hit the player with leaf_brain damage");
    if (hit) {
        auto const* poison = ctx.player().getEffect(Effect::Poison);
        ctx.check(poison != nullptr, "the bolt poisons the player");
    }
    dealt().victim = 0;
    ctx.player().removeAllEffects();
    // a bolt at an oak log 5 blocks away (the player out of the way)
    ctx.player().teleportTo(center(at.offset(0, 0, 4)), true, 0, 1, false);
    auto logPos = at.offset(-5, 1, 0);
    ctx.setBlock(logPos, "minecraft:oak_log");
    bool leaves = false;
    for (int shot = 0; shot < 4 && !leaves; ++shot) {
        auto* d = mobById(did);
        ctx.require(d != nullptr, "the druid is there");
        auto eye = d->getEyePos();
        Vec3 dir{logPos.x + 0.5f - d->getFeetPos().x, logPos.y + 0.5f - (eye.y - 0.1f), logPos.z + 0.5f - d->getFeetPos().z};
        gf::projectile::shoot(*d, id::NatureBolt, dir, 0.6f, 0.0f);
        for (int i = 0; i < 40 && !leaves; ++i) {
            co_await ctx.ticks(1);
            leaves = ctx.blockAt(logPos) == "minecraft:birch_leaves";
        }
    }
    ctx.check(leaves, "the bolt turned the log into birch leaves (" + ctx.blockAt(logPos) + ")");
    removeIfThere(did);
    co_return;
});

// SlimeProjectile#onHitEntity: 4 thrown damage
GF_REGISTER_SELFTEST("hostiles.slime_blob", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1780, 12, 8);
    (void)box;
    SurvivalScope survival(ctx);
    auto          at = ctx.testPos(1782, 2).offset(0, 1, 0);
    ctx.player().teleportTo(center(at.offset(4, 0, 0)), true, 0, 1, false);
    auto* beetle = spawn(ctx, id::SlimeBeetle, center(at));
    auto  bid    = uidOf(*beetle);
    co_await ctx.ticks(2);
    listen(ctx.player());
    bool hit = false;
    for (int shot = 0; shot < 4 && !hit; ++shot) {
        if (auto* b = mobById(bid)) beetles::slimeShoot(*b, ctx.player());
        for (int i = 0; i < 40 && !hit; ++i) {
            co_await ctx.ticks(1);
            hit = dealtType("minecraft:thrown");
        }
    }
    ctx.check(hit, "a slime blob hit the player");
    if (hit) {
        auto it = std::find(dealt().types.begin(), dealt().types.end(), "minecraft:thrown");
        ctx.check(near(dealt().amounts[it - dealt().types.begin()], 4.0f, 0.05f), "the blob deals 4");
    }
    dealt().victim = 0;
    removeIfThere(bid);
    co_return;
});

// SwarmSpider#doHurtTarget: nextInt(4) == 0 of the spider's random lets a bite through (seeded)
GF_REGISTER_SELFTEST("hostiles.swarm_spider_bites", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1790, 8, 6);
    (void)box;
    auto  at     = ctx.testPos(1791, 2).offset(0, 1, 0);
    auto* spider = spawn(ctx, id::SwarmSpider, center(at));
    auto* pig    = spawn(ctx, "minecraft:iron_golem", center(at.offset(1, 0, 0)));
    co_await ctx.ticks(2);
    spider = asMob(*actorById(uidOf(*spider)));
    pig    = asMob(*actorById(uidOf(*pig)));
    // count the bites that get past the swarm spider's handler (an onIncoming of the lowest priority)
    static int  passed = 0;
    static bool registered = false;
    static std::int64_t watched = 0;
    if (!registered) {
        registered = true;
        gf::combat::onIncoming(gf::combat::priority::Lowest, [](gf::combat::DamageInfo& d) {
            if (watched != 0 && uidOf(d.victim) == watched && isType(d.attacker, id::SwarmSpider)) ++passed;
            return gf::combat::Verdict::Pass;
        });
    }
    passed  = 0;
    watched = uidOf(*pig);
    ge::seedForTest(*spider, 17);
    gf::core::jrand::LegacyRandom expectRandom(17);
    int expected = 0;
    for (int i = 0; i < 40; ++i) {
        if (expectRandom.nextInt(4) == 0) ++expected;
        (void)mobs::doHurtTarget(*spider, *pig);
    }
    watched = 0;
    ctx.check(passed == expected, "one bite in four lands: " + std::to_string(passed) + " of 40, expected " + std::to_string(expected));
    spider->remove();
    pig->remove();
    co_return;
});

// KingSpider#finalizeSpawn: a skeleton druid always rides it
GF_REGISTER_SELFTEST("hostiles.king_spider_rider", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1795, 5, 6);
    (void)box;
    auto  at   = ctx.testPos(1797, 2).offset(0, 1, 0);
    auto* king = spawn(ctx, id::KingSpider, center(at));
    auto  kid  = uidOf(*king);
    co_await ctx.ticks(3);
    auto* k = actorById(kid);
    ctx.require(k != nullptr, "the king spider is there");
    Actor* rider = k->getFirstPassenger();
    ctx.check(rider != nullptr && rider->getTypeName() == id::Druid, "a skeleton druid rides the king spider");
    if (rider) rider->remove();
    k->remove();
    co_return;
});

// The wraith passes through a wall back into its home area (radius 20)
GF_REGISTER_SELFTEST("hostiles.wraith_home", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1745, 48, 16);
    (void)box;
    auto home = ctx.testPos(1750, 2).offset(0, 3, 0);
    // a stone wall 6 blocks east of home, the wraith 26 blocks east
    for (int y = -2; y < 8; ++y)
        for (int z = -2; z <= 4 - 2; ++z) ctx.setBlock(home.offset(6, y, z), "minecraft:stone");
    // a (creative) player near home: without one in range the engine despawns the wraith
    ctx.player().teleportTo(center(home.offset(-4, -2, 0)), true, 0, 1, false);
    auto* w   = spawn(ctx, id::Wraith, center(home.offset(26, 0, 0)));
    auto  wid = uidOf(*w);
    ge::setHome(*w, gf::core::GlobalPos{home.dim, home.x, home.y, home.z}, 20);
    ge::seedForTest(*w, 1745);
    bool back = false, crossed = false, inStone = false;
    for (int i = 0; i < 1200 && !back; ++i) {
        co_await ctx.ticks(1);
        auto* m = mobById(wid);
        ctx.require(m != nullptr && m->isAlive(), "the wraith is there (tick " + std::to_string(i) + ")");
        auto p = m->getFeetPos();
        if (i % 100 == 0) ctx.info("wraith at " + std::to_string(int(p.x - home.x)) + " " + std::to_string(int(p.y - home.y)) + " " + std::to_string(int(p.z - home.z)));
        if (p.x < home.x + 6) crossed = true;
        if (auto* region = ctx.region(); region && gf::game::blockIdAt(*region, blockPosOf(*m)) == "minecraft:stone") inStone = true;
        double dx = p.x - home.x, dy = p.y - home.y, dz = p.z - home.z;
        back = crossed && dx * dx + dy * dy + dz * dz < 20.0 * 20.0 && p.x < home.x + 5;
    }
    ctx.check(crossed, "the wraith flew through the wall");
    ctx.check(back, "the wraith is home");
    ctx.info(inStone ? "it passed through the stone wall" : "it flew around the wall");
    removeIfThere(wid);
    co_return;
});

// The entity loot tables (upstream data): what each one drops
GF_REGISTER_SELFTEST("hostiles.loot_tables", [](Context& ctx) -> ll::coro::CoroTask<> {
    struct Table {
        char const*              id;
        std::vector<std::string> allowed;
        int                      max;
    };
    std::vector<Table> tables = {
        {"gloamingforest:entities/winter_wolf", {"gloamingforest:arctic_fur"}, 3},
        {"gloamingforest:entities/skeleton_druid", {"minecraft:bone", "gloamingforest:torchberries"}, 6},
        {"gloamingforest:entities/redcap", {"minecraft:coal"}, 2},
        {"gloamingforest:entities/redcap_sapper", {"minecraft:coal"}, 2},
        {"gloamingforest:entities/wraith", {"minecraft:glowstone_dust"}, 3},
        {"gloamingforest:entities/fire_beetle", {"minecraft:gunpowder"}, 3},
        {"gloamingforest:entities/slime_beetle", {"minecraft:slime_ball"}, 3},
        {"gloamingforest:entities/pinch_beetle", {}, 0},
        {"gloamingforest:entities/king_spider", {"minecraft:string", "minecraft:spider_eye"}, 4},
        {"gloamingforest:entities/hedge_spider", {"minecraft:string", "minecraft:spider_eye"}, 4},
        {"gloamingforest:entities/swarm_spider", {"minecraft:string", "minecraft:spider_eye"}, 4},
        {"gloamingforest:entities/hostile_wolf", {}, 0},
        {"gloamingforest:entities/mist_wolf", {}, 0},
    };
    for (auto const& t : tables) {
        bool any = false;
        for (int roll = 0; roll < 32; ++roll) {
            gf::loot::LootContext lc;
            lc.region = ctx.region();
            lc.seed   = 1700 + roll;
            auto items = gf::loot::roll(t.id, lc);
            int  total = 0;
            for (auto const& s : items) {
                auto id = gf::game::items::idOf(s);
                total += gf::game::items::countOf(s);
                any = true;
                ctx.check(std::find(t.allowed.begin(), t.allowed.end(), id) != t.allowed.end(), std::string(t.id) + " drops " + id);
            }
            ctx.check(total <= t.max, std::string(t.id) + " drops at most " + std::to_string(t.max));
        }
        if (t.max > 0 && t.allowed.size() == 1) ctx.check(any, std::string(t.id) + " drops something in 32 rolls");
    }
    co_return;
});

} // namespace gf::feature::hostiles::tests

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of U28's mobs (rule R12: "aurora.*", testPos dx 2800..2899): the attributes against
// F13's goldens, the snow guardian's gear, the ice crystal's 600-tick life, the unstable ice core's
// delayed explosion and block transformation, the stable ice core's snowballs, the Snow Queen's
// phases, shields, summons and beam damage, her death chest, and her spawner's height rule.
#include <algorithm>
#include <cmath>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"

#include "core/aurora/AuroraColor.h"
#include "feature/aurora/Mobs.h"
#include "feature/aurora/Structures.h"
#include "feature/progression/Progression.h"
#include "feature/projectile/Projectile.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/ext/aurora/W0Internals.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::aurora::tests {

namespace {

using selftest::Context;
using Difficulty = SharedTypes::Legacy::Difficulty;
namespace fe     = gf::feature::entity;

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

Mob* spawnAt(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dy = 0.0f, float dz = 0.5f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y) + dy, at.z + dz}, false, false, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

std::int64_t uidOf(Actor& a) { return a.getOrCreateUniqueID().rawID; }

std::vector<Actor*> around(core::GlobalPos const& pos, std::string const& type, float r) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(pos.dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, pos.dim, AABB{Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}}))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

void discardAround(core::GlobalPos const& pos, float r) {
    for (char const* t : {kSnowQueen, kSnowGuardian, kStableIceCore, kUnstableIceCore, kIceCrystal, "twilightforest:ice_snowball"})
        for (auto* a : around(pos, t, r)) a->remove();
}

core::GlobalPos at(core::GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

} // namespace

GF_REGISTER_SELFTEST("aurora.mob_attributes", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(2820, 16);
    auto            base = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY + 1, box.minZ + 2};
    // F13's goldens (tests/golden/mobs.json): max health, movement speed, attack damage, follow
    // range, knockback resistance, the box (width, height) and the eye height, read from the live
    // actors (flying speed: the queen's F8 flying speed, set from the same golden in SnowQueen.cpp)
    struct Row {
        char const* id;
        int         health;
        float       speed, attack, follow, knockback, width, height, eye;
    };
    auto attr = [](Mob& m, Attribute const& a) {
        auto ref = m.getAttribute(a);
        auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr);
        return inst ? inst->mCurrentValue : -1.0f;
    };
    auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    std::vector<std::int64_t> spawned;
    int                       i = 0;
    for (auto r : {Row{kSnowGuardian, 10, 0.23f, 3.0f, 16.0f, 0.0f, 0.6f, 1.8f, 1.53f}, Row{kStableIceCore, 20, 0.23f, 3.0f, 16.0f, 0.0f, 0.8f, 1.8f, 1.35f},
                   Row{kUnstableIceCore, 20, 0.23f, 3.0f, 16.0f, 0.0f, 0.8f, 1.8f, 1.35f}, Row{kIceCrystal, 10, 0.23f, 5.0f, 16.0f, 0.0f, 0.6f, 1.8f, 1.53f},
                   Row{kSnowQueen, 200, 0.23f, 7.0f, 40.0f, 0.75f, 0.7f, 2.5f, 2.125f}}) {
        auto* m = spawnAt(at(base, (i++) * 3, 0, 0), r.id);
        ctx.require(m != nullptr, std::string("spawned ") + r.id);
        std::string const id = r.id;
        ctx.check(m->getMaxHealth() == r.health, id + " max health " + std::to_string(m->getMaxHealth()));
        ctx.check(near(attr(*m, SharedAttributes::MOVEMENT_SPEED()), r.speed), id + " movement speed " + std::to_string(attr(*m, SharedAttributes::MOVEMENT_SPEED())));
        ctx.check(near(attr(*m, SharedAttributes::ATTACK_DAMAGE()), r.attack), id + " attack damage " + std::to_string(attr(*m, SharedAttributes::ATTACK_DAMAGE())));
        ctx.check(near(attr(*m, SharedAttributes::FOLLOW_RANGE()), r.follow), id + " follow range " + std::to_string(attr(*m, SharedAttributes::FOLLOW_RANGE())));
        ctx.check(near(attr(*m, SharedAttributes::KNOCKBACK_RESISTANCE()), r.knockback),
                  id + " knockback resistance " + std::to_string(attr(*m, SharedAttributes::KNOCKBACK_RESISTANCE())));
        auto const& b = m->getAABB();
        ctx.check(near(b.max.x - b.min.x, r.width) && near(b.max.y - b.min.y, r.height),
                  id + " box " + std::to_string(b.max.x - b.min.x) + " x " + std::to_string(b.max.y - b.min.y));
        spawned.push_back(uidOf(*m));
    }
    co_await ctx.ticks(5);
    // the eye height (Entity#getEyeHeight): Bedrock's 0.9 x height replaced by Java's through the
    // offsets component (applyJavaEyeHeight, every tick)
    {
        float const javaEye[] = {1.53f, 1.35f, 1.35f, 1.53f, 2.125f};
        for (std::size_t k = 0; k < spawned.size() && k < 5; k++) {
            auto* m = mobById(spawned[k]);
            if (!m) continue;
            float eye = m->getEyePos().y - m->getFeetPos().y;
            ctx.check(std::abs(eye - javaEye[k]) < 0.01f, m->getTypeName() + " eye height " + std::to_string(eye) + " (Java " + std::to_string(javaEye[k]) + ")");
        }
    }
    if (auto* g = mobById(spawned[0])) {
        auto id = game::items::idOf(g->getCarriedItem());
        ctx.check(id == "twilightforest:ironwood_sword" || id == "twilightforest:steeleaf_sword" || id == "twilightforest:knightmetal_sword",
                  "the snow guardian carries a TF sword: " + id);
    }
    if (auto* q = mobById(spawned[4])) {
        ctx.check(gf::entity::ext::parts(*q).size() == 7, "the Snow Queen has seven ice shields");
        ctx.check(queenPhaseForTest(*q) == 0, "she starts in the summon phase");
        ctx.check(queenSummonsForTest(*q) == 6, "with six summons");
    }
    for (auto uid : spawned)
        if (auto* a = fe::actorById(uid)) a->remove();
    discardAround(base, 20);
});

GF_REGISTER_SELFTEST("aurora.ice_crystal_lifetime", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2840, 5);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY + 1, box.minZ + 2};
    auto*           summoned = spawnAt(pos, kIceCrystal);
    auto*           wild     = spawnAt(at(pos, 2, 0, 0), kIceCrystal);
    ctx.require(summoned && wild, "two ice crystals");
    markSummonedCrystal(*summoned);
    setCrystalAgeForTest(*summoned, 590);
    auto s = uidOf(*summoned), w = uidOf(*wild);
    co_await ctx.ticks(20);
    ctx.check(fe::actorById(s) == nullptr || !fe::actorById(s)->isAlive(), "the summoned crystal is gone after 600 ticks");
    ctx.check(fe::actorById(w) && fe::actorById(w)->isAlive(), "a crystal that was not summoned stays");
    if (auto* a = fe::actorById(w)) a->remove();
    discardAround(pos, 8);
});

namespace {
/// Who progression's onKilled credits for the last unstable ice core (twilight_hunter, U14).
std::int64_t& coreKilledBy() {
    static std::int64_t who        = 0;
    static bool         registered = false;
    if (!registered) {
        registered = true;
        gf::progression::events::onKilled([](Player& killer, Actor& victim, std::string const&) {
            if (victim.getTypeName() == kUnstableIceCore) who = killer.getOrCreateUniqueID().rawID;
        });
    }
    return who;
}
} // namespace

GF_REGISTER_SELFTEST("aurora.unstable_ice_core", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box    = co_await ctx.flatArea(2850, 12);
    auto            pos    = core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY + 1, box.minZ + 6};
    auto*           region = ctx.region();
    // a stone block (a redstone conductor: terracotta) and an ice block (stained glass) one block
    // away: closer than 2, always inside transformBlocks' random range 4 + (r - r) x 2
    auto stonePos = at(pos, 1, 1, 0), icePos = at(pos, -1, 1, 0);
    ctx.require(game::setBlock(*region, game::blockPos(stonePos), *game::blockWithStates("minecraft:stone", nlohmann::json::object())), "stone placed");
    ctx.require(game::setBlock(*region, game::blockPos(icePos), *game::blockWithStates("minecraft:ice", nlohmann::json::object())), "ice placed");
    auto* core = spawnAt(pos, kUnstableIceCore);
    ctx.require(core != nullptr, "an unstable ice core");
    auto uid = uidOf(*core);
    // killed by the player on the tick it appears: its goals never move it away from the blocks
    coreKilledBy() = 0;
    gf::combat::hurt(*core, 1000.0f, "minecraft:player_attack", &ctx.player(), &ctx.player());
    co_await ctx.ticks(20);
    auto* dying = fe::actorById(uid);
    ctx.check(dying && dying->isAlive(), "the core waits 60 ticks after its death");
    std::string lies = "gone";
    if (dying) {
        auto f = dying->getFeetPos();
        lies   = std::to_string(f.x - pos.x) + " " + std::to_string(f.y - pos.y) + " " + std::to_string(f.z - pos.z);
        ctx.check(std::abs(f.x - (pos.x + 0.5f)) < 1.0f && std::abs(f.z - (pos.z + 0.5f)) < 1.0f, "the dying core lies still");
    }
    co_await ctx.ticks(60);
    ctx.check(!fe::actorById(uid) || !fe::actorById(uid)->isAlive(), "then it explodes and is gone");
    ctx.check(coreKilledBy() == uidOf(ctx.player()), "the delayed death is the player's kill (onKilled, twilight_hunter)");
    auto stone = game::blockIdAt(*region, game::blockPos(stonePos));
    auto ice   = game::blockIdAt(*region, game::blockPos(icePos));
    ctx.check(stone.ends_with("_terracotta"), "the stone became terracotta: " + stone);
    ctx.check(ice.ends_with("_stained_glass"), "the ice became stained glass: " + ice + " (the dying core at " + lies + " from its spawn block)");
    discardAround(pos, 10);
});

GF_REGISTER_SELFTEST("aurora.transform_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    // UnstableIceCore#transformBlocks from a fixed spot: glass (a non-solid full cube) -> stained
    // glass, stone -> terracotta, a torch (no full cube) and obsidian (resistance >= 8) stay
    auto  box    = co_await ctx.flatArea(2850, 12);
    auto  pos    = core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY + 2, box.minZ + 6};
    auto* region = ctx.region();
    struct Case {
        int         dx;
        char const* id;
        char const* expectSuffix; ///< "" = unchanged
    };
    std::vector<Case> const cases{{1, "minecraft:glass", "_stained_glass"},     {-1, "minecraft:stone", "_terracotta"},
                                  {0, "minecraft:obsidian", ""},                {2, "minecraft:oak_leaves", "_stained_glass"}};
    for (auto const& k : cases) ctx.require(ctx.setBlock(at(pos, k.dx, 0, 1), k.id), std::string("placed ") + k.id);
    ctx.require(ctx.setBlock(at(pos, 0, -1, -1), "minecraft:stone") && ctx.setBlock(at(pos, 0, 0, -1), "minecraft:torch"), "a torch on stone");
    transformBlocksAt(*region, game::blockPos(pos));
    for (auto const& k : cases) {
        auto got = ctx.blockAt(at(pos, k.dx, 0, 1));
        if (*k.expectSuffix) ctx.check(got.ends_with(k.expectSuffix), std::string(k.id) + " -> " + got);
        else ctx.check(got == k.id, std::string(k.id) + " stays: " + got);
    }
    ctx.check(ctx.blockAt(at(pos, 0, 0, -1)) == "minecraft:torch", "the torch stays: " + ctx.blockAt(at(pos, 0, 0, -1)));
});

GF_REGISTER_SELFTEST("aurora.map_colors", [](Context& ctx) -> ll::coro::CoroTask<> {
    // transformBlock reads MapColor#col: Java's colours of javaMapColor's table (grass and leaves are
    // biome-tinted on Bedrock), Bedrock's own map colour for any other block. Bedrock's colours of
    // the listed blocks are reported (a difference there is why the table exists)
    auto  box    = co_await ctx.flatArea(2850, 8);
    auto* region = ctx.region();
    ctx.check(javaMapColor("minecraft:grass_block") == 0x7FB238u, "grass block: Java's MapColor.GRASS");
    ctx.check(javaMapColor("minecraft:oak_leaves") == 0x007C00u && javaMapColor("minecraft:azalea_leaves_flowered") == 0x007C00u, "leaves: MapColor.PLANT");
    ctx.check(javaMapColor("minecraft:red_terracotta") == 0x8E3C2Eu && javaMapColor("minecraft:lime_wool") == 0x7FCC19u, "terracotta and wool colours");
    ctx.check(!javaMapColor("twilightforest:aurora_block").has_value(), "other blocks use Bedrock's map colour");
    int i = 0, same = 0, n = 0;
    std::string differ;
    for (char const* id : {"minecraft:stone", "minecraft:ice", "minecraft:dirt", "minecraft:snow", "minecraft:packed_ice", "minecraft:grass_block", "minecraft:oak_leaves",
                           "minecraft:sand", "minecraft:oak_planks", "minecraft:white_wool", "minecraft:deepslate", "minecraft:clay"}) {
        auto p = core::GlobalPos{ctx.dimension(), box.minX + (i % 7), box.minY + 1 + i / 7, box.minZ + 2};
        i++;
        ctx.require(ctx.setBlock(p, id), std::string("placed ") + id);
        auto        bp = game::blockPos(p);
        auto const& b  = region->getBlock(bp);
        auto        c  = b.getBlockType().getMapColor(*region, bp, b);
        unsigned    rgb = (static_cast<unsigned>(c.r * 255.0f + 0.5f) << 16) | (static_cast<unsigned>(c.g * 255.0f + 0.5f) << 8) | static_cast<unsigned>(c.b * 255.0f + 0.5f);
        n++;
        if (rgb == javaMapColor(id).value_or(~0u)) same++;
        else {
            char buf[80];
            std::snprintf(buf, sizeof buf, "%s #%06X (Java #%06X); ", id, rgb, javaMapColor(id).value_or(0));
            differ += buf;
        }
        ctx.cleanupBlock(p);
    }
    ctx.info("Bedrock map colours equal to Java's: " + std::to_string(same) + " of " + std::to_string(n) + (differ.empty() ? std::string() : ", differing: " + differ));
    ctx.check(same * 2 >= n, "Bedrock agrees with Java for most plain blocks");
});
GF_REGISTER_SELFTEST("aurora.stable_ice_core_snowballs", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    static int      snowballHits = 0;
    static bool     registered   = false;
    if (!registered) {
        registered = true;
        gf::combat::onDealt(0, [](gf::combat::DamageInfo const& d) {
            if (d.type == "twilightforest:snowball_fight") snowballHits++;
        });
    }
    snowballHits = 0;
    auto box     = co_await ctx.flatArea(2860, 16);
    auto pos     = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY + 1, box.minZ + 8};
    auto* core   = spawnAt(pos, kStableIceCore);
    ctx.require(core != nullptr, "a stable ice core");
    auto  uid    = uidOf(*core);
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    player.teleportTo(Vec3{pos.x + 7.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    if (auto* c = mobById(uid)) c->setTarget(&ctx.player());
    for (int i = 0; i < 20 && snowballHits == 0; i++) {
        co_await ctx.ticks(10);
        if (auto* c = mobById(uid)) c->setTarget(&ctx.player());
    }
    ctx.check(snowballHits > 0, "an ice snowball hit the player with snowball_fight damage");
    ctx.player().setPlayerGameType(GameType::Creative);
    if (auto* c = fe::actorById(uid)) c->remove();
    discardAround(pos, 16);
});

GF_REGISTER_SELFTEST("aurora.snow_queen_fight", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box   = co_await ctx.flatArea(2870, 24, 24);
    auto            pos   = core::GlobalPos{ctx.dimension(), box.minX + 12, box.minY + 1, box.minZ + 12};
    auto*           queen = spawnAt(pos, kSnowQueen, 0.5f, 4.0f);
    ctx.require(queen != nullptr, "the Snow Queen");
    auto  uid    = uidOf(*queen);
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    player.teleportTo(Vec3{pos.x + 6.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    // SUMMON: ice crystals appear near her target
    bool summoned = false;
    for (int i = 0; i < 40 && !summoned; i++) {
        if (auto* q = mobById(uid)) q->setTarget(&ctx.player());
        co_await ctx.ticks(10);
        auto* q  = mobById(uid);
        summoned = q && queenSummonsForTest(*q) < 6 && !around(pos, kIceCrystal, 30).empty();
    }
    ctx.check(summoned, "the summon phase brings ice crystals");
    for (auto* c : around(pos, kIceCrystal, 30)) ctx.check(crystalMaxAgeForTest(*c) == 600, "a summoned crystal dies in 30 seconds");
    // a hit on a shield is blocked
    auto* q = mobById(uid);
    ctx.require(q != nullptr, "the queen is still there");
    float before = static_cast<float>(q->getHealth());
    auto  parts  = gf::entity::ext::parts(*q);
    ctx.require(parts.size() == 7, "seven shields");
    if (auto* shield = gf::entity::partActor(parts[0])) gf::combat::hurt(*shield, 6.0f, "minecraft:player_attack", &ctx.player());
    co_await ctx.ticks(2);
    q = mobById(uid);
    ctx.require(q != nullptr, "the queen is still there");
    ctx.check(static_cast<float>(q->getHealth()) >= before, "a shield blocks the hit");
    // BEAM ends after 25 damage
    setQueenPhaseForTest(*q, 2);
    co_await ctx.ticks(2);
    for (int i = 0; i < 4; i++) {
        if (auto* qq = mobById(uid)) gf::combat::hurt(*qq, 7.0f, "minecraft:player_attack", &ctx.player());
        co_await ctx.ticks(12);
    }
    q = mobById(uid);
    ctx.require(q != nullptr, "the queen survives the test hits");
    ctx.check(queenPhaseForTest(*q) == 0, "25 damage while beaming sends her back to summoning");
    // DROP: 2..4 drops
    setQueenPhaseForTest(*q, 1);
    int maxDrops = 0;
    (void)queenDropsForTest(*q, &maxDrops);
    ctx.check(maxDrops >= 2 && maxDrops <= 4, "2..4 drops: " + std::to_string(maxDrops));
    ctx.player().setPlayerGameType(GameType::Creative);
    if (auto* qq = fe::actorById(uid)) qq->remove();
    discardAround(pos, 30);
});

namespace {

/// Damage the tests count (combat::onDealt; registered once per process).
struct DealtLog {
    std::vector<std::pair<std::string, float>> hits; ///< (type, amount) on the test player
    std::int64_t                               victim = 0;
};
DealtLog& dealt() {
    static DealtLog log;
    static bool     registered = false;
    if (!registered) {
        registered = true;
        gf::combat::onDealt(0, [](gf::combat::DamageInfo const& d) {
            if (dealt().victim && d.victim.getOrCreateUniqueID().rawID == dealt().victim) dealt().hits.push_back({d.type, d.amount});
        });
    }
    return log;
}

int countIce(BlockSource& region, core::BlockBox const& b) {
    int n = 0;
    for (int x = b.minX; x <= b.maxX; x++)
        for (int y = b.minY; y <= b.maxY; y++)
            for (int z = b.minZ; z <= b.maxZ; z++)
                if (game::blockIdAt(region, BlockPos{x, y, z}) == "minecraft:ice") n++;
    return n;
}

} // namespace

GF_REGISTER_SELFTEST("aurora.snow_queen_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    // HoverThenDropGoal: hover 6 above the target, then fall through ice (destroyBlocksInAABB),
    // 2..4 drops, then BEAM
    DifficultyScope normal(Difficulty::Normal);
    auto            box    = co_await ctx.flatArea(2870, 24, 24);
    auto            pos    = core::GlobalPos{ctx.dimension(), box.minX + 12, box.minY + 1, box.minZ + 12};
    auto*           region = ctx.region();
    // an ice ceiling 3 above the player, 9 x 9
    core::BlockBox ceiling{pos.x - 4, pos.y + 3, pos.z - 4, pos.x + 4, pos.y + 3, pos.z + 4};
    for (int x = ceiling.minX; x <= ceiling.maxX; x++)
        for (int z = ceiling.minZ; z <= ceiling.maxZ; z++) ctx.setBlock(core::GlobalPos{pos.dim, x, ceiling.minY, z}, "minecraft:ice");
    int const before = countIce(*region, ceiling);
    auto*     queen  = spawnAt(pos, kSnowQueen, 0.5f, 8.0f);
    ctx.require(queen != nullptr, "the Snow Queen");
    auto uid = uidOf(*queen);
    ctx.player().setPlayerGameType(GameType::Survival);
    ctx.player().teleportTo(Vec3{pos.x + 0.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    setQueenPhaseForTest(*queen, 1);
    int maxDrops = 0;
    (void)queenDropsForTest(*queen, &maxDrops);
    ctx.check(maxDrops >= 2 && maxDrops <= 4, "2..4 drops: " + std::to_string(maxDrops));
    int  bestDrops = 0;
    bool beam      = false;
    for (int i = 0; i < 70 && !beam; i++) {
        if (auto* q = mobById(uid)) {
            q->setTarget(&ctx.player());
            bestDrops = std::max(bestDrops, queenDropsForTest(*q, nullptr));
            beam      = queenPhaseForTest(*q) == 2;
        }
        co_await ctx.ticks(10);
    }
    int const after = countIce(*region, ceiling);
    ctx.check(after < before, "her drops broke ice: " + std::to_string(before) + " -> " + std::to_string(after));
    ctx.check(bestDrops >= 1 || beam, "the drops count: " + std::to_string(bestDrops));
    ctx.check(beam, "after her drops she beams");
    ctx.player().setPlayerGameType(GameType::Creative);
    if (auto* q = fe::actorById(uid)) q->remove();
    discardAround(pos, 30);
});

GF_REGISTER_SELFTEST("aurora.snow_queen_beam", [](Context& ctx) -> ll::coro::CoroTask<> {
    // HoverBeamGoal: once in place, a ray of 4 chilling_breath damage per hit
    DifficultyScope normal(Difficulty::Normal);
    auto            box   = co_await ctx.flatArea(2870, 24, 24);
    auto            pos   = core::GlobalPos{ctx.dimension(), box.minX + 12, box.minY + 1, box.minZ + 12};
    auto*           queen = spawnAt(pos, kSnowQueen, 3.5f, 3.0f);
    ctx.require(queen != nullptr, "the Snow Queen");
    auto uid = uidOf(*queen);
    ctx.player().setPlayerGameType(GameType::Survival);
    ctx.player().teleportTo(Vec3{pos.x + 0.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    dealt().hits.clear();
    dealt().victim = uidOf(ctx.player());
    setQueenPhaseForTest(*queen, 2);
    int  breath    = 0;
    bool breathing = false;
    // up to two goal cycles (seek <= 80, hover 80, beam 100 ticks); stops at the first hit
    for (int i = 0; i < 60 && breath == 0; i++) {
        if (auto* q = mobById(uid)) breathing = breathing || queenBreathingForTest(*q);
        if (auto* q = mobById(uid)) q->setTarget(&ctx.player());
        ctx.player().heal(20); // stay alive through the test
        co_await ctx.ticks(10);
        // (again after the wait: a beam that starts and hits within it ends the loop)
        if (auto* q = mobById(uid)) breathing = breathing || queenBreathingForTest(*q);
        for (auto const& [type, amount] : dealt().hits)
            if (type == "twilightforest:chilling_breath") {
                breath++;
                ctx.check(std::abs(amount - 4.0f) < 0.01f || amount < 4.0f, "a ray hit deals BREATH_DAMAGE 4 (armour may lower it): " + std::to_string(amount));
            }
    }
    if (auto* q = mobById(uid)) {
        auto qp = q->getFeetPos();
        auto pp = ctx.player().getFeetPos();
        ctx.info("player at " + std::to_string(pp.x) + " " + std::to_string(pp.y) + " " + std::to_string(pp.z) + ", queen head yaw " + std::to_string(q->getYHeadRot()) +
                 ", pitch " + std::to_string(q->getRotation().x));
        ctx.info("queen at " + std::to_string(qp.x) + " " + std::to_string(qp.y) + " " + std::to_string(qp.z) + ", phase " + std::to_string(queenPhaseForTest(*q)) +
                 (breathing ? ", she breathed" : ", she never breathed"));
    }
    std::string types;
    for (auto const& [type, amount] : dealt().hits) types += type + " ";
    ctx.info("damage the player took: " + (types.empty() ? std::string("none") : types));
    ctx.check(breathing, "she beams once in place");
    // no forced aim: HoverBeamGoal's own lookAt turns her to the player and its ray hits
    ctx.check(breath > 0, "the goal's beam hit the player with chilling_breath");
    dealt().victim = 0;
    ctx.player().setPlayerGameType(GameType::Creative);
    if (auto* q = fe::actorById(uid)) q->remove();
    discardAround(pos, 30);
});

GF_REGISTER_SELFTEST("aurora.snow_queen_shields", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box   = co_await ctx.flatArea(2870, 24, 24);
    auto            pos   = core::GlobalPos{ctx.dimension(), box.minX + 12, box.minY + 1, box.minZ + 12};
    auto*           queen = spawnAt(pos, kSnowQueen, 0.5f, 2.0f);
    ctx.require(queen != nullptr, "the Snow Queen");
    auto uid = uidOf(*queen);
    co_await ctx.ticks(2);
    auto* q = mobById(uid);
    ctx.require(q && gf::entity::ext::parts(*q).size() == 7, "seven shields");
    // a piercing arrow passes a shield: shot at the shield under her (index 6, it does not orbit), it
    // hurts nobody and flies on beyond the shield
    auto* shield = gf::entity::partActor(gf::entity::ext::parts(*q)[6]);
    ctx.require(shield != nullptr, "the shield under her");
    auto  sp    = shield->getFeetPos();
    auto  level = ll::service::getLevel();
    float before = static_cast<float>(q->getHealth());
    auto* arrow  = level->getSpawner().spawnProjectile(*ctx.region(), ActorDefinitionIdentifier("minecraft:arrow"), &ctx.player(),
                                                      Vec3{sp.x - 2.5f, sp.y + 0.35f, sp.z}, Vec3{1.0f, 0.0f, 0.0f});
    ctx.require(arrow != nullptr, "an arrow");
    if (auto pc = arrow->getEntityContext().tryGetComponent<ProjectileComponent>()) pc->mPiercingEnchantLevel = 1;
    ctx.check(pierceLevelForTest(*arrow) == 1, "the arrow pierces: level " + std::to_string(pierceLevelForTest(*arrow)));
    fe::setMotion(*arrow, Vec3{1.2f, 0.0f, 0.0f});
    auto arrowId = uidOf(*arrow);
    float maxX   = sp.x - 2.5f;
    for (int i = 0; i < 8; i++) {
        co_await ctx.ticks(1);
        if (auto* a = fe::actorById(arrowId)) maxX = std::max(maxX, a->getFeetPos().x);
    }
    q = mobById(uid);
    ctx.require(q != nullptr, "the queen is still there");
    ctx.check(static_cast<float>(q->getHealth()) >= before, "the piercing arrow did not hurt her");
    ctx.check(maxX > sp.x + 1.0f, "the arrow flew on through the shield: reached x " + std::to_string(maxX - sp.x) + " past it");
    if (auto* a = fe::actorById(arrowId)) a->remove();
    // a plain arrow at the shield is blocked, even with a piercing arrow of the same shooter flying
    // by next to it (the pass-through goes to the arrow that hit, not the nearest piercing one)
    {
        q = mobById(uid);
        ctx.require(q != nullptr, "the queen is still there");
        shield = gf::entity::partActor(gf::entity::ext::parts(*q)[6]);
        ctx.require(shield != nullptr, "the shield under her");
        sp           = shield->getFeetPos();
        auto* plain  = level->getSpawner().spawnProjectile(*ctx.region(), ActorDefinitionIdentifier("minecraft:arrow"), &ctx.player(),
                                                          Vec3{sp.x - 2.5f, sp.y + 0.35f, sp.z}, Vec3{1.0f, 0.0f, 0.0f});
        auto* beside = level->getSpawner().spawnProjectile(*ctx.region(), ActorDefinitionIdentifier("minecraft:arrow"), &ctx.player(),
                                                           Vec3{sp.x - 2.5f, sp.y + 0.35f, sp.z + 2.0f}, Vec3{1.0f, 0.0f, 0.0f});
        ctx.require(plain != nullptr && beside != nullptr, "two arrows");
        if (auto pc = beside->getEntityContext().tryGetComponent<ProjectileComponent>()) pc->mPiercingEnchantLevel = 1;
        fe::setMotion(*plain, Vec3{1.2f, 0.0f, 0.0f});
        fe::setMotion(*beside, Vec3{1.2f, 0.0f, 0.0f});
        auto  plainId = uidOf(*plain), besideId = uidOf(*beside);
        float plainX  = sp.x - 2.5f;
        for (int i = 0; i < 8; i++) {
            co_await ctx.ticks(1);
            if (auto* a = fe::actorById(plainId)) plainX = std::max(plainX, a->getFeetPos().x);
        }
        ctx.check(plainX < sp.x + 1.0f, "the plain arrow is blocked by the shield: reached x " + std::to_string(plainX - sp.x) + " past it");
        if (auto* a = fe::actorById(plainId)) a->remove();
        if (auto* a = fe::actorById(besideId)) a->remove();
    }
    // whatever touches a shield is hurt and lifted by 0.4 (applyShieldCollisions)
    ctx.player().setPlayerGameType(GameType::Survival);
    dealt().hits.clear();
    dealt().victim = uidOf(ctx.player());
    bool lifted = false, hurt = false;
    for (int i = 0; i < 20 && !(lifted && hurt); i++) {
        q = mobById(uid);
        if (!q) break;
        auto parts = gf::entity::ext::parts(*q);
        if (auto* s = parts.empty() ? nullptr : gf::entity::partActor(parts[0])) {
            auto at = s->getFeetPos();
            ctx.player().teleportTo(Vec3{at.x, at.y - 0.6f, at.z}, false, 0, 1, false);
            float y0 = ctx.player().getPosDelta().y;
            co_await ctx.ticks(1);
            if (!dealt().hits.empty()) hurt = true;
            if (ctx.player().getPosDelta().y > y0 + 0.2f) lifted = true;
        } else co_await ctx.ticks(1);
        ctx.player().heal(20);
    }
    ctx.check(hurt, "a shield hurts the player who touches it");
    ctx.check(lifted, "and lifts them (motion y + 0.4)");
    dealt().victim = 0;
    ctx.player().setPlayerGameType(GameType::Creative);
    if (auto* qq = fe::actorById(uid)) qq->remove();
    discardAround(pos, 30);
});

namespace {
std::int64_t& queenKilledBy() {
    static std::int64_t who        = 0;
    static bool         registered = false;
    if (!registered) {
        registered = true;
        gf::progression::events::onKilled([](Player& killer, Actor& victim, std::string const&) {
            if (victim.getTypeName() == kSnowQueen) who = killer.getOrCreateUniqueID().rawID;
        });
    }
    return who;
}
} // namespace

GF_REGISTER_SELFTEST("aurora.snow_queen_death", [](Context& ctx) -> ll::coro::CoroTask<> {
    // F8's boss kit: the death chest (twilight oak) with her loot; the kill grants progress_glacier
    DifficultyScope normal(Difficulty::Normal);
    auto            box   = co_await ctx.flatArea(2870, 24, 24);
    auto            pos   = core::GlobalPos{ctx.dimension(), box.minX + 12, box.minY + 1, box.minZ + 12};
    auto*           queen = spawnAt(pos, kSnowQueen, 0.5f, 0.0f);
    ctx.require(queen != nullptr, "the Snow Queen");
    auto uid = uidOf(*queen);
    queenKilledBy() = 0;
    gf::progression::revoke(ctx.player(), "twilightforest:progress_glacier");
    // progress_glacier asks for progress_yeti first (previous_progression)
    gf::progression::grant(ctx.player(), "twilightforest:progress_yeti");
    ctx.player().teleportTo(Vec3{pos.x + 3.5f, float(pos.y), pos.z + 0.5f}, true, 0, 1, false);
    co_await ctx.ticks(2);
    if (auto* q = mobById(uid)) gf::combat::hurt(*q, 10000.0f, "minecraft:player_attack", &ctx.player(), &ctx.player());
    bool chest = false;
    for (int i = 0; i < 30 && !chest; i++) {
        co_await ctx.ticks(10);
        for (int dx = -6; dx <= 6 && !chest; dx++)
            for (int dy = -2; dy <= 6 && !chest; dy++)
                for (int dz = -6; dz <= 6 && !chest; dz++)
                    if (ctx.blockAt(at(pos, dx, dy, dz)) == "twilightforest:twilight_oak_chest") chest = true;
    }
    ctx.check(chest, "a twilight oak death chest");
    // the advancement itself is U14's criteria engine (player_killed_entity / hurt_boss on
    // twilightforest:snow_queen); this unit's part is the kill credit those triggers read
    ctx.check(queenKilledBy() == uidOf(ctx.player()), "the kill is the player's (player_killed_entity for progress_glacier)");
    for (int i = 0; i < 10 && !gf::progression::has(ctx.player(), "twilightforest:progress_glacier"); i++) co_await ctx.ticks(5);
    ctx.info(std::string("progress_glacier ") + (gf::progression::has(ctx.player(), "twilightforest:progress_glacier") ? "granted" : "not granted (U14's criteria engine)"));
    gf::progression::revoke(ctx.player(), "twilightforest:progress_glacier");
    gf::progression::revoke(ctx.player(), "twilightforest:progress_yeti");
    discardAround(pos, 30);
});

GF_REGISTER_SELFTEST("aurora.aurora_block_mining", [](Context& ctx) -> ll::coro::CoroTask<> {
    // AuroraBrickBlock#getDestroyProgress: 0.1 a tick with progress_glacier (10 ticks of mining);
    // a tap does not break it
    auto  box   = co_await ctx.flatArea(2840, 8);
    auto& sp    = ctx.spawnPlayer("aurora_miner");
    auto  spId  = uidOf(sp);
    auto  block = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    sp.teleportTo(Vec3{block.x + 0.5f, float(block.y), block.z - 1.5f}, true, 0, 1, false);
    sp.setPlayerGameType(GameType::Survival);
    gf::progression::grant(sp, "twilightforest:progress_glacier");
    ctx.require(gf::progression::has(sp, "twilightforest:progress_glacier"), "the miner has progress_glacier");
    ctx.require(ctx.setBlock(block, "twilightforest:aurora_block"), "an aurora block");
    co_await ctx.ticks(2);
    // a tap: start, stop at once
    auto* p = static_cast<SimulatedPlayer*>(fe::actorById(spId));
    ctx.require(p != nullptr, "the miner");
    p->simulateLookAt(game::blockPos(block), sim::LookDuration::Instant);
    ctx.check(p->simulateDestroyBlock(game::blockPos(block), ScriptModuleMinecraft::ScriptFacing::North), "mining starts");
    co_await ctx.ticks(2);
    if (auto* pp = static_cast<SimulatedPlayer*>(fe::actorById(spId))) {
        auto* gm = pp->mGameMode.get();
        BlockPos at = gm ? BlockPos(gm->mDestroyBlockPos) : BlockPos{0, 0, 0};
        ctx.info("after 2 ticks: timer " + std::string(isMiningAuroraBlockForTest(*pp) ? "running" : "not running") + ", game mode progress " +
                 std::to_string(gm ? static_cast<float>(gm->mDestroyProgress) : -1.0f) + " at " + std::to_string(at.x) + " " + std::to_string(at.y) + " " + std::to_string(at.z));
        pp->simulateStopDestroyingBlock();
    }
    co_await ctx.ticks(15);
    ctx.check(ctx.blockAt(block) == "twilightforest:aurora_block", "a tap does not break it: " + ctx.blockAt(block));
    // held: broken in about 10 ticks (far below the 15 s of a hardness 10 block)
    p = static_cast<SimulatedPlayer*>(fe::actorById(spId));
    ctx.require(p != nullptr, "the miner");
    ctx.check(p->simulateDestroyBlock(game::blockPos(block), ScriptModuleMinecraft::ScriptFacing::North), "mining starts again");
    int broken = -1;
    for (int t = 1; t <= 30 && broken < 0; t++) {
        co_await ctx.ticks(1);
        if (ctx.blockAt(block) != "twilightforest:aurora_block") broken = t;
    }
    ctx.check(broken > 0 && broken <= 14, "held mining breaks it in ~10 ticks: " + std::to_string(broken));
    // the block drops itself (its loot table): an item on the ground, or already in the miner's
    // inventory (picked up)
    int drops = 0;
    for (int t = 0; t < 10 && drops == 0; t++) {
        co_await ctx.ticks(2);
        for (auto* a : around(block, "minecraft:item", 3))
            if (game::items::idOf(static_cast<ItemActor*>(a)->item()) == "twilightforest:aurora_block") drops++;
        if (auto* pp = static_cast<SimulatedPlayer*>(fe::actorById(spId))) {
            auto& inv = pp->getInventory();
            for (int i = 0; i < inv.getContainerSize(); ++i)
                if (game::items::idOf(inv.getItem(i)) == "twilightforest:aurora_block") drops += game::items::countOf(inv.getItem(i));
        }
    }
    ctx.check(broken < 0 || drops > 0, "the broken aurora block drops itself: " + std::to_string(drops));
    for (auto* a : around(block, "minecraft:item", 3)) a->remove();
    if (auto* pp = static_cast<SimulatedPlayer*>(fe::actorById(spId))) {
        pp->simulateStopDestroyingBlock();
        gf::progression::revoke(*pp, "twilightforest:progress_glacier");
    }
    ctx.cleanupBlock(block);
});

GF_REGISTER_SELFTEST("aurora.client_miner", [](Context& ctx) {
    // the client scenario's setup (the companion mining check): progress_glacier (after
    // progress_yeti, its previous progression) for every real player in creative mode, who then
    // mines an aurora block in survival. Without real players (the server's runs) it does nothing.
    int granted = 0;
    if (auto level = ll::service::getLevel())
        level->forEachPlayer([&](Player& p) {
            if (p.isSimulatedPlayer() || p.getPlayerGameType() != GameType::Creative) return true;
            gf::progression::grant(p, "twilightforest:progress_yeti");
            gf::progression::grant(p, "twilightforest:progress_glacier");
            if (gf::progression::has(p, "twilightforest:progress_glacier")) granted++;
            return true;
        });
    ctx.info("progress_glacier granted to " + std::to_string(granted) + " real player(s)");
});

GF_REGISTER_SELFTEST("aurora.aurora_block_variant",[](Context& ctx) -> ll::coro::CoroTask<> {
    // NoiseVaryingModel: the variant of a placed aurora block is calcVariant of its position
    auto box = co_await ctx.flatArea(2840, 8);
    int  differs = 0;
    for (int i = 0; i < 6; i++) {
        auto p = core::GlobalPos{ctx.dimension(), box.minX + 1 + i, box.minY + 1, box.minZ + 2};
        ctx.require(ctx.setBlock(p, "twilightforest:aurora_block"), "placed");
        auto const* expected = auroraBlockAt(p.x, p.y, p.z);
        ctx.require(expected != nullptr, "the variants resolve");
        int want = game::intState(*expected, "gloamingforest:variant").value_or(-1);
        ctx.check(want == core::aurora::calcVariant(p.x, p.y, p.z, 16), "auroraBlockAt follows calcVariant");
        if (want != 0) differs++;
    }
    ctx.check(differs > 0, "positions get different variants");
    // a block set by a command (/setblock, /fill) gets its position's variant as well
    int cmdChecked = 0, cmdOk = 0;
    for (int i = 0; i < 6; i++) {
        auto p = core::GlobalPos{ctx.dimension(), box.minX + 1 + i, box.minY + 3, box.minZ + 2};
        auto r = gf::selftest::runServerCommand("setblock " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + " twilightforest:aurora_block",
                                                ctx.dimension());
        if (!r) {
            std::string why;
            for (auto const& m : r.messages) why += m + "; ";
            ctx.info("/setblock did not run: " + why);
            continue;
        }
        cmdChecked++;
        co_await ctx.ticks(2);
        int got = game::intState(ctx.region()->getBlock(game::blockPos(p)), "gloamingforest:variant").value_or(-1);
        if (got == core::aurora::calcVariant(p.x, p.y, p.z, 16)) cmdOk++;
        ctx.cleanupBlock(p);
    }
#ifdef GF_CLIENT
    // (the integrated server of a client may refuse the server's own commands right after a world
    // opens: the placements above and below still prove the variants of the new world)
    ctx.check(cmdOk == cmdChecked, "/setblock aurora blocks take calcVariant: " + std::to_string(cmdOk) + " of " + std::to_string(cmdChecked));
#else
    ctx.check(cmdChecked > 0 && cmdOk == cmdChecked, "/setblock aurora blocks take calcVariant: " + std::to_string(cmdOk) + " of " + std::to_string(cmdChecked));
#endif
    // a structure's plain aurora blocks get theirs
    auto* region = ctx.region();
    auto  p      = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 2, box.minZ + 3};
    ctx.require(ctx.setBlock(p, "twilightforest:aurora_block"), "placed plain");
    structure::BlockSourceRegion live(*region, game::blockPos(p));
    structure::BlockBox          one{p.x, p.y, p.z, p.x, p.y, p.z};
    applyAuroraVariants(live, one, one);
    auto got = game::intState(region->getBlock(game::blockPos(p)), "gloamingforest:variant").value_or(-1);
    ctx.check(got == core::aurora::calcVariant(p.x, p.y, p.z, 16), "applyAuroraVariants sets " + std::to_string(got));
});

GF_REGISTER_SELFTEST("aurora.ice_mobs_melt", [](Context& ctx) {
    // BaseIceMob#aiStep: #snow_golem_melts biomes (TF's fire swamp and oak savannah too) burn ice mobs
    ctx.check(meltsSnowIn("twilightforest:fire_swamp"), "the fire swamp melts ice mobs");
    ctx.check(meltsSnowIn("twilightforest:oak_savannah"), "the oak savannah melts ice mobs");
    ctx.check(!meltsSnowIn("twilightforest:glacier"), "the glacier does not");
    ctx.check(!meltsSnowIn("twilightforest:snowy_forest"), "the snowy forest does not");
});

GF_REGISTER_SELFTEST("aurora.snow_guardian_gear", [](Context& ctx) -> ll::coro::CoroTask<> {
    // populateDefaultEquipmentSlots: a sword, a chestplate and a helmet of one tier; no gear drops
    // unless a player hit it (Mob#dropCustomDeathLoot: recentlyHit)
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(2820, 16);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 8, box.minY + 1, box.minZ + 8};
    auto*           g   = spawnAt(pos, kSnowGuardian);
    ctx.require(g != nullptr, "a snow guardian");
    auto uid = uidOf(*g);
    co_await ctx.ticks(3);
    g = mobById(uid);
    ctx.require(g != nullptr, "still there");
    using Slot = SharedTypes::Legacy::EquipmentSlot;
    auto chest = game::items::idOf(g->getItemSlot(Slot::Torso));
    auto head  = game::items::idOf(g->getItemSlot(Slot::Head));
    auto sword = game::items::idOf(g->getItemSlot(Slot::Mainhand));
    ctx.check(sword.ends_with("_sword") && head.ends_with("_helmet"), "gear: " + sword + ", " + head);
    ctx.check(chest.ends_with("_chestplate"), "the chestplate is worn: " + (chest.empty() ? std::string("empty") : chest));
    gf::combat::hurt(*g, 1000.0f, "minecraft:generic");
    co_await ctx.ticks(25);
    int gear = 0;
    for (auto* a : around(pos, "minecraft:item", 6)) {
        auto id = game::items::idOf(static_cast<ItemActor*>(a)->item());
        if (id.ends_with("_sword") || id.ends_with("_chestplate") || id.ends_with("_helmet")) gear++;
    }
    ctx.check(gear == 0, "no gear drops without a player's hit: " + std::to_string(gear));
    for (auto* a : around(pos, "minecraft:item", 6)) a->remove();
    discardAround(pos, 10);
});
GF_REGISTER_SELFTEST("aurora.snow_queen_spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box     = co_await ctx.flatArea(2880, 12, 16);
    auto            spawner = core::GlobalPos{ctx.dimension(), box.minX + 6, box.minY + 8, box.minZ + 6};
    // a player below spawnerY - 4 does not wake her
    ctx.player().teleportTo(Vec3{spawner.x + 3.5f, float(box.minY + 1), spawner.z + 0.5f}, true, 0, 1, false);
    ctx.require(ctx.setBlock(spawner, "twilightforest:snow_queen_boss_spawner"), "spawner placed");
    co_await ctx.ticks(40);
    ctx.check(around(spawner, kSnowQueen, 12).empty(), "no queen for a player more than 4 below the spawner");
    ctx.player().teleportTo(Vec3{spawner.x + 3.5f, float(spawner.y - 2), spawner.z + 0.5f}, true, 0, 1, false);
    bool spawned = false;
    for (int i = 0; i < 20 && !spawned; i++) {
        co_await ctx.ticks(5);
        spawned = !around(spawner, kSnowQueen, 12).empty();
    }
    ctx.check(spawned, "the queen appears for a player within 9 and above spawnerY - 4");
    ctx.check(ctx.blockAt(spawner) != "twilightforest:snow_queen_boss_spawner", "the spawner is used up");
    discardAround(spawner, 30);
});

GF_REGISTER_SELFTEST("aurora.native_state_reload", [](Context& ctx) -> ll::coro::CoroTask<> {
    // F8 drops the state (goals, move control) of an actor that stopped resolving for 200 ticks (an
    // unloaded chunk); the Snow Queen and the stable ice core come back with their goals, and the
    // queen as a new Java entity: SUMMON with six summons
    DifficultyScope normal(Difficulty::Normal);
    auto            box   = co_await ctx.flatArea(2870, 24, 24);
    auto            pos   = core::GlobalPos{ctx.dimension(), box.minX + 12, box.minY + 1, box.minZ + 12};
    auto*           queen = spawnAt(pos, kSnowQueen, 0.5f, 4.0f);
    auto*           core  = spawnAt(at(pos, 6, 0, 6), kStableIceCore);
    ctx.require(queen && core, "a Snow Queen and a stable ice core");
    auto qid = uidOf(*queen), cid = uidOf(*core);
    co_await ctx.ticks(3);
    auto* q = mobById(qid);
    auto* c = mobById(cid);
    ctx.require(q && c, "both still there");
    ctx.check(nativeGoalsPresent(*q) && nativeGoalsPresent(*c), "both have their native goals");
    setQueenPhaseForTest(*q, 2);
    ctx.check(queenPhaseForTest(*q) == 2, "the queen beams");
    dropNativeStateForTest(*q);
    dropNativeStateForTest(*c);
    ctx.check(!nativeGoalsPresent(*q) && !nativeGoalsPresent(*c), "F8's state forgotten (as its sweep does)");
    co_await ctx.ticks(3);
    q = mobById(qid);
    c = mobById(cid);
    ctx.require(q && c, "both still there after the reset");
    ctx.check(nativeGoalsPresent(*q), "the queen's goals are back");
    ctx.check(nativeGoalsPresent(*c), "the stable core's ranged attack goal is back");
    if (auto const* s = fe::findState(qid)) ctx.check(s->moveKind == gf::entity::MoveKind::Hovering, "the queen hovers again");
    ctx.check(queenPhaseForTest(*q) == 0 && queenSummonsForTest(*q) == 6, "she starts over in SUMMON with six summons");
    ctx.check(gf::entity::ext::parts(*q).size() == 7, "with seven shields");
    if (auto* a = fe::actorById(qid)) a->remove();
    if (auto* a = fe::actorById(cid)) a->remove();
    discardAround(pos, 30);
});

// aurora.auroralized_glass_beacon is gone: BDS never computes beacon beam sections (the beam is
// the client's, BeaconBlockActor::generateBeamSections is LL_PLAT_C only). The colour rule is the
// gtest AuroraBeacon.Sections, the client hook is proved by the client scenario's beacon-beam
// screenshot and its "aurora: beacon beam at ... coloured" line.
} // namespace gf::feature::aurora::tests

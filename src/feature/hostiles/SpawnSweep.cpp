// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// A5 mobs audit, U17 (hostiles) selftests (rule R12: "hostiles.*", testPos dx 1700..1799): the spawn
// sweep over every TF entity type (core/hostiles/MobSweep.h, F13's Java goldens):
//   hostiles.spawn_sweep        every type spawns; a living one has the golden max health, movement
//                               speed, attack damage, follow range, knockback resistance, box and fire
//                               immunity; the others their box and fire immunity
//   hostiles.mob_assets         every living type's loot table, hurt / death sounds and far-away
//                               despawn rule are in the mod's packs; /summon refuses what Java does
//   hostiles.peaceful_despawn   Peaceful removes the monsters (Mob#shouldDespawnInPeaceful) and keeps
//                               the rest (bosses: entity.boss_peaceful)
//   hostiles.multiplayer_target two players: a hostile wolf takes the nearer one and turns to the
//                               other when the first goes creative
// Other units' entities are audited here too: a mismatch listed in known() (owner, reason) is reported
// as an info line ("AUDIT <owner> <id>: ..."), anything else fails, so new regressions show; U17's
// own rows always fail (U16 checks its mobs in fauna.*).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "core/hostiles/MobSweep.h"
#include "feature/entity/Entity.h"
#include "feature/ext/hostiles/W0Internals.h"
#include "feature/hostiles/Common.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace gf::feature::hostiles::sweep {

namespace {

namespace fs = std::filesystem;
namespace fe = gf::feature::entity;
using core::hostiles::kMobSweep;
using core::hostiles::MobSweepRow;
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

/// The deviations of other units' entities found by this audit (W2 round 1): reported, not failed.
/// Key "<id>|<check>"; the value says why (and who owns the fix).
std::map<std::string, std::string, std::less<>> const& known() {
    static std::map<std::string, std::string, std::less<>> const m{
        // U24: the hydra's 16 x 12 Java box is its body's 2 x 3 here; its heads and neck are parts
        {"twilightforest:hydra|width", "U24: the body's box, the heads are parts"},
        {"twilightforest:hydra|height", "U24: the body's box, the heads are parts"},
        // U9 / F6: Java's 0 x 0 markers are 0.01 x 0.01 (Bedrock refuses an empty box)
        {"twilightforest:charm_effect|width", "U9: Bedrock's smallest box"},
        {"twilightforest:charm_effect|height", "U9: Bedrock's smallest box"},
        {"twilightforest:protection_box|width", "F6: Bedrock's smallest box"},
        {"twilightforest:protection_box|height", "F6: Bedrock's smallest box"},
        // Bedrock keeps custom monsters on Peaceful: each owner needs Mob#checkDespawn's Peaceful
        // branch (U16 / U17 remove theirs in their ticks; bosses: F8's boss kit)
        {"*|peaceful", "Bedrock keeps custom monsters on Peaceful; the owner must remove them (U17: despawnInPeaceful)"},
        // attributes and fire immunity that differ from the Java goldens
        {"twilightforest:alpha_yeti|fire_immune", "U27: fire immune here, AlphaYeti is not fireImmune in Java"},
        {"twilightforest:hydra|knockback_resistance", "U24: knockback resistance 1 here, Java 0"},
        {"twilightforest:rising_zombie|knockback_resistance", "U21: knockback resistance 1 here, Java 0"},
        {"twilightforest:hydra_mortar|fire_immune", "U24: the mortar is fireImmune in Java; the BP lacks minecraft:fire_immune"},
        {"twilightforest:maze_slime|max_health", "U23: a size-1 slime's 3 / 0.3 / 1 here; Java's created maze slime has 20 / 0.7 / 2"},
        {"twilightforest:maze_slime|movement_speed", "U23: see max_health"},
        {"twilightforest:maze_slime|attack_damage", "U23: see max_health"},
        // entity sounds missing from sounds.json (Java: Monster's entity.hostile.hurt / death, or the class's own)
        {"twilightforest:adherent|sound.hurt", "U30: Monster default entity.hostile.hurt"},
        {"twilightforest:adherent|sound.death", "U30: Monster default entity.hostile.death"},
        {"twilightforest:armored_giant|sound.hurt", "U29: Monster default entity.hostile.hurt"},
        {"twilightforest:armored_giant|sound.death", "U29: Monster default entity.hostile.death"},
        {"twilightforest:giant_miner|sound.hurt", "U29: Monster default entity.hostile.hurt"},
        {"twilightforest:giant_miner|sound.death", "U29: Monster default entity.hostile.death"},
        {"twilightforest:harbinger_cube|sound.hurt", "U30: Monster default entity.hostile.hurt"},
        {"twilightforest:harbinger_cube|sound.death", "U30: Monster default entity.hostile.death"},
        {"twilightforest:roving_cube|sound.hurt", "U30: Monster default entity.hostile.hurt"},
        {"twilightforest:roving_cube|sound.death", "U30: Monster default entity.hostile.death"},
        {"twilightforest:plateau_boss|sound.hurt", "U30: Monster default entity.hostile.hurt"},
        {"twilightforest:plateau_boss|sound.death", "U30: Monster default entity.hostile.death"},
        {"twilightforest:maze_slime|sound.hurt", "U23: MAZE_SLIME_HURT(_SMALL) by size"},
        {"twilightforest:maze_slime|sound.death", "U23: MAZE_SLIME_DEATH(_SMALL) by size"},
        {"twilightforest:unstable_ice_core|sound.death", "U28: BaseIceMob keeps Monster's entity.hostile.death"},
        // far-away despawn (Mob#checkDespawn / removeWhenFarAway)
        {"twilightforest:death_tome|despawn", "U21: a Monster that Java despawns far away; the BP has no minecraft:despawn"},
        {"twilightforest:lich_minion|despawn", "U21: a Zombie that Java despawns far away; the BP has no minecraft:despawn"},
        {"twilightforest:rising_zombie|despawn", "U21: a Monster that Java despawns far away; the BP has no minecraft:despawn"},
        {"twilightforest:loyal_zombie|despawn", "U9: LoyalZombie#removeWhenFarAway is !isTame (only an untamed one despawns): check the BP's filter"},
    };
    return m;
}

/// A finding: fails unless it is a known deviation of another unit (U17's own always fail).
void finding(Context& ctx, MobSweepRow const& row, std::string_view check, std::string const& what) {
    std::string key = std::string(row.id) + "|" + std::string(check);
    bool        own = std::string_view(row.owner) == "U17"; // U16 checks its own in fauna.*
    if (!own) {
        auto it = known().find(key);
        if (it == known().end()) it = known().find("*|" + std::string(check));
        if (it != known().end()) {
            ctx.info(std::string("AUDIT ") + row.owner + " " + row.id + ": " + what + " (known: " + it->second + ")");
            return;
        }
    }
    ctx.check(false, std::string(row.owner) + " " + row.id + ": " + what);
}

float attribute(Mob& m, Attribute const& a) {
    auto ref = m.getMutableAttribute(a);
    if (auto* inst = ref.mInstance->mPtr) return inst->mCurrentValue;
    return -1.0f;
}

bool near(float have, float want, float eps = 0.011f) { return std::abs(have - want) < eps; }

std::string num(float v) {
    auto s = std::to_string(v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

void expectNear(Context& ctx, MobSweepRow const& row, std::string_view check, float have, float want, float eps = 0.011f) {
    if (!near(have, want, eps)) finding(ctx, row, check, std::string(check) + " " + num(have) + ", Java " + num(want));
}

std::string pathOf(char const* id) {
    std::string s(id);
    return s.substr(s.find(':') + 1);
}

fs::path modDir() {
    try {
        return Mod::getInstance().getSelf().getModDir();
    } catch (...) {
        return {};
    }
}

nlohmann::json readJson(fs::path const& p) {
    std::ifstream in(p);
    if (!in.good()) return nullptr;
    return nlohmann::json::parse(in, nullptr, false, true);
}

/// The actors near `at` in the test row other than players (cleanup of parts, minions, drops).
void clearActors(Context& ctx, core::GlobalPos const& min, int sx, int sz) {
    auto* region = ctx.region();
    if (!region) return;
    AABB box{Vec3{float(min.x) - 8.0f, float(min.y) - 8.0f, float(min.z) - 8.0f}, Vec3{float(min.x + sx) + 8.0f, float(min.y) + 40.0f, float(min.z + sz) + 8.0f}};
    for (auto* a : fe::actorsIn(*region, ctx.dimension(), box))
        if (a && !a->isPlayer()) a->remove();
}

} // namespace

GF_REGISTER_SELFTEST("hostiles.spawn_sweep", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope easy(Difficulty::Easy);
    auto            box = co_await ctx.flatArea(1700, 24, 24);
    (void)box;
    auto  at     = ctx.testPos(1712, 2).offset(0, 1, 0);
    Vec3  pos{at.x + 0.5f, float(at.y), at.z + 0.5f};
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int spawned = 0;
    for (auto const& row : kMobSweep) {
        Actor* a = nullptr;
        if (row.living) a = hostiles::spawnMob(*region, row.id, pos, gf::spawn::SpawnReason::Command);
        else a = hostiles::spawnActor(*region, row.id, pos);
        if (!a) {
            finding(ctx, row, "spawn", "does not spawn");
            continue;
        }
        ++spawned;
        auto const& b = a->getAABB();
        expectNear(ctx, row, "width", b.max.x - b.min.x, row.width);
        expectNear(ctx, row, "height", b.max.y - b.min.y, row.height);
        // Bedrock reports projectile-like actors fire immune anyway: only a Java fire-immune one is checked
        if ((row.living || row.fireImmune) && a->isFireImmune() != row.fireImmune)
            finding(ctx, row, "fire_immune", row.fireImmune ? "not fire immune" : "fire immune");
        if (row.living) {
            auto* m = fe::asMob(*a);
            if (!m) {
                finding(ctx, row, "living", "is not a mob");
            } else {
                expectNear(ctx, row, "max_health", float(m->getMaxHealth()), row.maxHealth, 0.5f);
                expectNear(ctx, row, "movement_speed", attribute(*m, SharedAttributes::MOVEMENT_SPEED()), row.movementSpeed);
                if (row.attackDamage > 0.0f) expectNear(ctx, row, "attack_damage", attribute(*m, SharedAttributes::ATTACK_DAMAGE()), row.attackDamage);
                expectNear(ctx, row, "follow_range", attribute(*m, SharedAttributes::FOLLOW_RANGE()), row.followRange, 0.5f);
                expectNear(ctx, row, "knockback_resistance", attribute(*m, SharedAttributes::KNOCKBACK_RESISTANCE()), row.knockbackResistance);
            }
        }
        a->remove();
        // a boss's parts, minions and bars go with it; the rest is swept below
        clearActors(ctx, at, 1, 1);
    }
    ctx.info("spawned " + std::to_string(spawned) + " of " + std::to_string(std::size(kMobSweep)) + " entity types");
    co_await ctx.ticks(2);
    clearActors(ctx, ctx.testPos(1700), 24, 24);
    co_return;
});

GF_REGISTER_SELFTEST("hostiles.mob_assets", [](Context& ctx) {
    auto dir = modDir();
    ctx.require(!dir.empty(), "the mod's folder");
    // the resource packs' entity sounds (sounds.json, merged per entity id by assemble)
    std::map<std::string, std::set<std::string>> events;
    std::error_code                              ec;
    for (auto const& pack : fs::directory_iterator(dir / "resource_packs", ec)) {
        auto j = readJson(pack.path() / "sounds.json");
        if (!j.is_object()) continue;
        auto const& ents = j.value("entity_sounds", nlohmann::json::object()).value("entities", nlohmann::json::object());
        for (auto const& [id, def] : ents.items())
            if (def.is_object() && def.contains("events") && def["events"].is_object())
                for (auto const& [ev, _] : def["events"].items()) events[id].insert(ev);
    }
    // the behavior packs' entities: far-away despawn (minecraft:despawn) and summonability
    std::map<std::string, nlohmann::json> bp;
    for (auto const& pack : fs::directory_iterator(dir / "behavior_packs", ec)) {
        for (auto const& f : fs::recursive_directory_iterator(pack.path() / "entities", ec)) {
            if (f.path().extension() != ".json") continue;
            auto j = readJson(f.path());
            if (!j.is_object() || !j.contains("minecraft:entity")) continue;
            auto const& e  = j["minecraft:entity"];
            auto        id = e.value("description", nlohmann::json::object()).value("identifier", std::string{});
            if (!id.empty()) bp[id] = e;
        }
    }
    ctx.require(!bp.empty(), "the behavior pack's entities");
    auto hasComponent = [](nlohmann::json const& e, char const* name) {
        if (auto c = e.find("components"); c != e.end() && c->is_object() && c->contains(name)) return true;
        auto groups = e.find("component_groups");
        if (groups == e.end() || !groups->is_object()) return false;
        for (auto const& [_, g] : groups->items())
            if (g.is_object() && g.contains(name)) return true;
        return false;
    };
    for (auto const& row : kMobSweep) {
        auto it = bp.find(row.id);
        if (it == bp.end()) {
            finding(ctx, row, "bp", "no behavior pack entity");
            continue;
        }
        auto const& e          = it->second;
        bool        summonable = e.value("description", nlohmann::json::object()).value("is_summonable", false);
        if (summonable != row.canSummon) finding(ctx, row, "summonable", summonable ? "summonable (Java: canSummon false)" : "not summonable");
        if (!row.living) continue;
        // LivingEntity#dropFromLootTable: twilightforest:entities/<path> (F10's death pipeline)
        if (row.lootTable) {
            auto table = dir / "data" / "twilightforest" / "loot_table" / "entities" / (pathOf(row.id) + ".json");
            if (!fs::is_regular_file(table, ec)) finding(ctx, row, "loot", "no loot table entities/" + pathOf(row.id));
        }
        auto const& ev = events[row.id];
        if (row.hurtDeathSounds) {
            for (char const* name : {"hurt", "death"})
                if (!ev.contains(name)) finding(ctx, row, std::string("sound.") + name, std::string("no '") + name + "' entity sound");
        } else {
            for (char const* name : {"hurt", "death"})
                if (ev.contains(name)) finding(ctx, row, std::string("sound.") + name, std::string("a '") + name + "' sound (Java: none)");
        }
        // Mob#checkDespawn far away: monsters (not bosses, BaseTFBoss#removeWhenFarAway; not the loyal
        // zombie) despawn, animals stay (DwarfRabbit#removeWhenFarAway, Animal)
        bool wantDespawn = row.peacefulDespawn && !row.boss;
        if (hasComponent(e, "minecraft:despawn") != wantDespawn)
            finding(ctx, row, "despawn", wantDespawn ? "never despawns far away (Java: it does)" : "despawns far away (Java: it stays)");
    }
    // /summon follows EntityType#canSummon
    auto at = ctx.testPos(1798, 2);
    for (auto const& row : kMobSweep) {
        if (row.canSummon) continue;
        auto r = selftest::runServerCommand("summon " + std::string(row.id) + " " + std::to_string(at.x) + " " + std::to_string(at.y + 1) + " " + std::to_string(at.z), ctx.dimension());
        if (r.ok && r.successCount > 0) {
            finding(ctx, row, "summonable", "/summon spawns it (Java: canSummon false)");
            (void)selftest::runServerCommand("kill @e[type=" + std::string(row.id) + "]", ctx.dimension());
        }
    }
});

GF_REGISTER_SELFTEST("hostiles.peaceful_despawn", [](Context& ctx) -> ll::coro::CoroTask<> {
    std::vector<std::pair<MobSweepRow const*, std::int64_t>> mobs;
    auto                                                     box = co_await ctx.flatArea(1700, 24, 24);
    (void)box;
    auto base = ctx.testPos(1702, 0).offset(0, 1, 0);
    {
        DifficultyScope easy(Difficulty::Easy);
        auto*           region = ctx.region();
        ctx.require(region != nullptr, "region");
        int k = 0;
        for (auto const& row : kMobSweep) {
            if (!row.living || row.boss) continue;
            auto p = base.offset((k % 10) * 2, 0, k / 10); // 51 types: rows dz 0..5 inside the flat area
            ++k;
            if (auto* m = hostiles::spawnMob(*region, row.id, Vec3{p.x + 0.5f, float(p.y), p.z + 0.5f}, gf::spawn::SpawnReason::Command)) mobs.emplace_back(&row, uidOf(*m));
            else finding(ctx, row, "spawn", "does not spawn");
        }
        co_await ctx.ticks(2);
        for (auto& [row, uid] : mobs)
            if (!actorById(uid)) finding(ctx, *row, "peaceful", "gone on Easy within 2 ticks");
        DifficultyScope peaceful(Difficulty::Peaceful);
        for (int t = 0; t < 40; ++t) {
            co_await ctx.ticks(1);
            bool pending = std::any_of(mobs.begin(), mobs.end(), [](auto const& m) { return m.first->peacefulDespawn && actorById(m.second); });
            if (!pending) break;
        }
        co_await ctx.ticks(5);
        for (auto& [row, uid] : mobs) {
            bool there = actorById(uid) != nullptr;
            if (row->peacefulDespawn && there) finding(ctx, *row, "peaceful", "stays on Peaceful (Java: despawns)");
            if (!row->peacefulDespawn && !there) finding(ctx, *row, "peaceful", "gone on Peaceful (Java: stays)");
        }
    }
    for (auto& [row, uid] : mobs)
        if (auto* a = actorById(uid)) a->remove();
    clearActors(ctx, ctx.testPos(1700), 24, 24);
    co_return;
});

// two players: HostileWolf's NearestAttackableTargetGoal (players) picks the nearer one and drops a
// player who turns creative for the other
GF_REGISTER_SELFTEST("hostiles.multiplayer_target", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1730, 24, 8);
    (void)box;
    auto at = ctx.testPos(1732, 2).offset(0, 1, 0);
    auto center = [](core::GlobalPos const& p) { return Vec3{p.x + 0.5f, float(p.y), p.z + 0.5f}; };
    auto& first = ctx.player();
    first.setPlayerGameType(GameType::Survival);
    first.teleportTo(center(at.offset(4, 0, 0)), true, 0, 1, false);
    auto firstId = first.getOrCreateUniqueID().rawID;
    auto& second = ctx.spawnPlayer();
    second.setPlayerGameType(GameType::Survival);
    second.teleportTo(center(at.offset(12, 0, 0)), true, 0, 1, false);
    auto secondId = second.getOrCreateUniqueID().rawID;
    auto* region  = ctx.region();
    ctx.require(region != nullptr, "region");
    auto* wolf = hostiles::spawnMob(*region, id::HostileWolf, center(at), gf::spawn::SpawnReason::Command);
    ctx.require(wolf != nullptr, "a hostile wolf");
    auto wid = uidOf(*wolf);
    auto playerById = [](std::int64_t uid) -> Player* {
        auto* a = actorById(uid);
        return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
    };
    auto targetUid = [&]() -> std::int64_t {
        auto* w = liveMob(wid);
        if (!w) return -1;
        auto* t = targetOf(*w);
        return t ? uidOf(*t) : 0;
    };
    auto keepPlayers = [&] {
        for (auto uid : {firstId, secondId})
            if (auto* p = playerById(uid)) p->heal(20);
    };
    std::int64_t target = 0;
    for (int i = 0; i < 200 && target == 0; ++i) {
        co_await ctx.ticks(1);
        keepPlayers();
        target = targetUid();
    }
    ctx.check(target == firstId, "the wolf targets the nearer player (target " + std::to_string(target) + ")");
    if (auto* p = playerById(firstId)) p->setPlayerGameType(GameType::Creative);
    for (int i = 0; i < 200 && target != secondId; ++i) {
        co_await ctx.ticks(1);
        keepPlayers();
        target = targetUid();
    }
    ctx.check(target == secondId, "a creative player is dropped for the other one (target " + std::to_string(target) + ")");
    if (auto* p = playerById(secondId)) p->setPlayerGameType(GameType::Creative);
    if (auto* w = liveMob(wid)) w->remove();
    co_return;
});

} // namespace gf::feature::hostiles::sweep

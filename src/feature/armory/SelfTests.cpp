// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Selftests of U7 (armory.*, dx 700..799): the 59 items' numbers, the attack damage of the tools,
// knightmetal's and the minotaur axes' bonuses, the fiery tools' fire, the fiery pickaxe's smelting,
// the mazebreaker, the fiery / yeti set reactions, armour toughness, phantom armour kept on death,
// arctic dye, the crafted default enchantments and the emperor's cloth no-template smithing.
// Behaviour from TF 4.8 (see the headers of Module.cpp and core/armory/Armory.h).
#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "core/armory/Armory.h"
#include "feature/armory/Armory.h"
#include "feature/combat/Combat.h"
#include "feature/effects/Effects.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/armory/ArmorHooks.h"
#include "feature/item/Item.h"
#include "feature/recipes/Recipes.h"
#include "feature/recipes/RecipesInternal.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Compat.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace gf::feature::armory::tests {

namespace {

namespace ca    = gf::core::armory;
namespace items = gf::game::items;
using selftest::Context;
using Slot       = SharedTypes::Legacy::ArmorSlot;
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

ItemStack make(std::string const& id, int count = 1) {
    auto s = items::make(id, count);
    return s ? *s : ItemStack::EMPTY_ITEM();
}

std::int64_t uidOf(Actor& a) { return a.getOrCreateUniqueID().rawID; }

Actor* actorById(std::int64_t uid) {
    auto level = ll::service::getLevel();
    if (!level || !uid) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

Mob* mobById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a && a->hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(a) : nullptr;
}

SimulatedPlayer* simById(std::int64_t uid) {
    auto* a = actorById(uid);
    return a && a->isPlayer() ? static_cast<SimulatedPlayer*>(a) : nullptr;
}

Mob* spawnAt(core::GlobalPos const& at, std::string const& id) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, false, false, false);
}

struct Remove {
    std::vector<std::int64_t> uids;
    ~Remove() {
        for (auto uid : uids)
            if (auto* a = actorById(uid)) a->remove();
    }
};

std::vector<Actor*> around(core::GlobalPos const& pos, std::string const& type, float r) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(pos.dim);
    if (!region) return out;
    AABB box{Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}};
    for (auto* a : region->getEntities(box, 0.0f))
        if (a && a->isAlive() && a->getTypeName() == type) out.push_back(a);
    return out;
}

/// The damage a victim's next hurt starts with (before every handler) and ends with (after them).
struct DamageProbe {
    std::mutex                 mutex;
    std::int64_t               victim = 0;
    std::optional<float>       before, after;
    std::string                type;
};

DamageProbe& probe() {
    static DamageProbe p;
    static bool        once = [] {
        combat::onIncoming(combat::priority::Highest + 1, [](combat::DamageInfo& d) {
            auto& p = probe();
            std::lock_guard lock(p.mutex);
            if (p.victim && uidOf(d.victim) == p.victim && !p.before) {
                p.before = d.amount;
                p.type   = d.type;
            }
            return combat::Verdict::Pass;
        });
        combat::onIncoming(combat::priority::Lowest - 1, [](combat::DamageInfo& d) {
            auto& p = probe();
            std::lock_guard lock(p.mutex);
            if (p.victim && uidOf(d.victim) == p.victim && !p.after) p.after = d.amount;
            return combat::Verdict::Pass;
        });
        return true;
    }();
    (void)once;
    return p;
}

void arm(std::int64_t victim) {
    auto&           p = probe();
    std::lock_guard lock(p.mutex);
    p.victim = victim;
    p.before.reset();
    p.after.reset();
    p.type.clear();
}

std::pair<float, float> measured() {
    auto&           p = probe();
    std::lock_guard lock(p.mutex);
    p.victim = 0;
    return {p.before.value_or(-1.f), p.after.value_or(-1.f)};
}

std::string fmt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

/// A survival test player at `at` holding `weapon`, looking at +x.
SimulatedPlayer& fighter(Context& ctx, core::GlobalPos const& at, std::string const& name, std::string const& weapon) {
    auto& sp = ctx.spawnPlayer(name);
    sp.teleport(Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f}, DimensionType(at.dim));
    sp.setPlayerGameType(GameType::Survival);
    sp.setCarriedItem(weapon.empty() ? ItemStack::EMPTY_ITEM() : make(weapon));
    return sp;
}

/// One hit of `attackerUid` on `victimUid`: the damage before and after the handlers.
ll::coro::CoroTask<std::pair<float, float>> hit(std::int64_t attackerUid, std::int64_t victimUid) {
    arm(victimUid);
    if (auto* sp = simById(attackerUid); sp && actorById(victimUid)) sp->simulateAttack(actorById(victimUid));
    co_await Context::ticks(2);
    co_return measured();
}

} // namespace

// ---- items -------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("armory.items", [](Context& ctx) {
    int count = 0;
    for (auto const& p : ca::armorPieces()) {
        auto s = make(std::string(p.id));
        ctx.require(!items::isEmpty(s), "armour item " + std::string(p.id));
        auto const* item = s.mItem.get();
        auto const* m    = ca::armorMaterial(p.material);
        ctx.check(item->getMaxDamage() == ca::armorDurability(p.type, m->durabilityMultiplier),
                  std::string(p.id) + " durability " + std::to_string(item->getMaxDamage()));
        ctx.check(item->getArmorValue() == ca::defenseOf(p), std::string(p.id) + " protection " + std::to_string(item->getArmorValue()));
        ctx.check(item->getEnchantValue() == m->enchantability, std::string(p.id) + " enchantability " + std::to_string(item->getEnchantValue()));
        ctx.check(gf::ext::armory::extraToughness(s, ItemStack::EMPTY_ITEM(), ItemStack::EMPTY_ITEM(), ItemStack::EMPTY_ITEM())
                      == static_cast<int>(std::lround(m->toughness)),
                  std::string(p.id) + " toughness");
        ++count;
    }
    for (auto const& t : ca::tools()) {
        auto s = make(std::string(t.id));
        ctx.require(!items::isEmpty(s), "tool " + std::string(t.id));
        auto const* item = s.mItem.get();
        ctx.check(item->getMaxDamage() == ca::toolTier(t.tier)->uses, std::string(t.id) + " durability " + std::to_string(item->getMaxDamage()));
        ctx.check(item->getEnchantValue() == ca::enchantability(t), std::string(t.id) + " enchantability " + std::to_string(item->getEnchantValue()));
        ctx.info(std::string(t.id) + ": Bedrock attack damage " + std::to_string(item->getAttackDamage()) + " (TF " + fmt(ca::attackDamage(t)) + ")");
        ++count;
    }
    for (auto const& m : ca::materialItems()) {
        ctx.check(!items::isEmpty(make(std::string(m.id))), "material " + std::string(m.id));
        ++count;
    }
    ctx.check(count == 59, "59 items: " + std::to_string(count));
    ctx.check(make("twilightforest:arctic_helmet").mItem->isDyeable(), "the arctic armour is dyeable");
    ctx.check(!make("twilightforest:yeti_helmet").mItem->isDyeable(), "the yeti armour is not");
});

// ---- attack damage, knightmetal, minotaur axes --------------------------------------------------------

GF_REGISTER_SELFTEST("armory.attack_damage", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(700, 5);
    auto            at  = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 1, box.minZ + 2};
    auto&           sp  = fighter(ctx, at, "armory_damage", "twilightforest:ironwood_sword");
    auto            spId = uidOf(sp);
    Remove          rm;
    struct Case {
        char const* weapon;
        float       expected;
    };
    // Java's attack damage (1 + tier + item) on a cow (no armour)
    for (auto const& c : {Case{"twilightforest:ironwood_sword", 6.f}, Case{"twilightforest:steeleaf_shovel", 5.5f}, Case{"twilightforest:fiery_pickaxe", 6.f},
                          Case{"twilightforest:steeleaf_axe", 10.f}, Case{"twilightforest:ironwood_hoe", 1.f}}) {
        auto* cow = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z}, "minecraft:cow");
        ctx.require(cow != nullptr, "a cow");
        rm.uids.push_back(uidOf(*cow));
        auto cowId = uidOf(*cow);
        if (auto* p = simById(spId)) p->setCarriedItem(make(c.weapon));
        co_await ctx.ticks(12); // the attack cooldown
        auto [before, after] = co_await hit(spId, cowId);
        ctx.check(std::abs(after - c.expected) < 0.01f, std::string(c.weapon) + " deals " + fmt(c.expected) + ": " + fmt(before) + " -> " + fmt(after));
        if (auto* m = mobById(cowId)) m->remove();
    }
});

GF_REGISTER_SELFTEST("armory.knightmetal", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(710, 5);
    auto            at   = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 1, box.minZ + 2};
    auto&           sp   = fighter(ctx, at, "armory_knight", ids::kKnightmetalSword);
    auto            spId = uidOf(sp);
    Remove          rm;
    auto            zombieWith = [&](int pieces) -> std::int64_t {
        auto* z = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z}, "minecraft:zombie");
        if (!z) return 0;
        char const* armour[4] = {"minecraft:iron_helmet", "minecraft:iron_chestplate", "minecraft:iron_leggings", "minecraft:iron_boots"};
        for (int i = 0; i < 4; ++i) z->setArmor(static_cast<Slot>(i), i < pieces ? make(armour[i]) : ItemStack::EMPTY_ITEM());
        rm.uids.push_back(uidOf(*z));
        return uidOf(*z);
    };
    // sword vs full iron armour: + (int)(2 x 1.0) = +2
    auto full = zombieWith(4);
    ctx.require(full != 0, "an armoured zombie");
    co_await ctx.ticks(12);
    auto [b1, a1] = co_await hit(spId, full);
    ctx.check(std::abs(a1 - b1 - 2.f) < 0.01f, "knightmetal sword vs full armour: +2 (" + fmt(b1) + " -> " + fmt(a1) + ")");
    if (auto* m = mobById(full)) m->remove();
    // a helmet only: cover 0.25 -> (int)0.5 = +0
    auto helm = zombieWith(1);
    co_await ctx.ticks(12);
    auto [b2, a2] = co_await hit(spId, helm);
    ctx.check(std::abs(a2 - b2) < 0.01f, "knightmetal sword vs one piece: +0 (" + fmt(b2) + " -> " + fmt(a2) + ")");
    if (auto* m = mobById(helm)) m->remove();
    // half cover with the pickaxe: +1
    if (auto* p = simById(spId)) p->setCarriedItem(make(ids::kKnightmetalPickaxe));
    auto half = zombieWith(2);
    co_await ctx.ticks(12);
    auto [b3, a3] = co_await hit(spId, half);
    ctx.check(std::abs(a3 - b3 - 1.f) < 0.01f, "knightmetal pickaxe vs two pieces: +1 (" + fmt(b3) + " -> " + fmt(a3) + ")");
    if (auto* m = mobById(half)) m->remove();
    // a zombie without pieces still has its natural armour (2): the sword's +2 without cover
    if (auto* p = simById(spId)) p->setCarriedItem(make(ids::kKnightmetalSword));
    auto natural = zombieWith(0);
    co_await ctx.ticks(12);
    auto [b0, a0] = co_await hit(spId, natural);
    ctx.check(std::abs(a0 - b0 - 2.f) < 0.01f, "knightmetal sword vs natural armour only: +2 (" + fmt(b0) + " -> " + fmt(a0) + ")");
    if (auto* m = mobById(natural)) m->remove();
    // the axe: +2 against no armour (a cow), nothing against armour
    if (auto* p = simById(spId)) p->setCarriedItem(make(ids::kKnightmetalAxe));
    std::int64_t bare = 0;
    if (auto* cow = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z}, "minecraft:cow")) {
        bare = uidOf(*cow);
        rm.uids.push_back(bare);
    }
    ctx.require(bare != 0, "a cow");
    co_await ctx.ticks(12);
    auto [b4, a4] = co_await hit(spId, bare);
    ctx.check(std::abs(a4 - b4 - 2.f) < 0.01f, "knightmetal axe vs no armour: +2 (" + fmt(b4) + " -> " + fmt(a4) + ")");
    if (auto* m = mobById(bare)) m->remove();
    auto armoured = zombieWith(4);
    co_await ctx.ticks(12);
    auto [b5, a5] = co_await hit(spId, armoured);
    ctx.check(std::abs(a5 - b5) < 0.01f, "knightmetal axe vs armour: +0 (" + fmt(b5) + " -> " + fmt(a5) + ")");
});

GF_REGISTER_SELFTEST("armory.minotaur_axe", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(720, 5);
    auto            at   = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 1, box.minZ + 2};
    auto&           sp   = fighter(ctx, at, "armory_minotaur", ids::kDiamondMinotaurAxe);
    auto            spId = uidOf(sp);
    Remove          rm;
    auto*           z = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z}, "minecraft:iron_golem");
    ctx.require(z != nullptr, "an iron golem");
    auto zid = uidOf(*z);
    rm.uids.push_back(zid);
    co_await ctx.ticks(12);
    auto [b1, a1] = co_await hit(spId, zid);
    ctx.check(std::abs(a1 - b1) < 0.01f, "walking: no bonus (" + fmt(b1) + " -> " + fmt(a1) + ")");
    if (auto* p = simById(spId)) p->setSprinting(true);
    co_await ctx.ticks(12);
    if (auto* p = simById(spId)) ctx.require(p->isSprinting(), "the player sprints");
    auto [b2, a2] = co_await hit(spId, zid);
    ctx.check(std::abs(a2 - b2 - 7.f) < 0.01f, "sprinting: +7 (" + fmt(b2) + " -> " + fmt(a2) + ")");
    if (auto* p = simById(spId)) p->setSprinting(false);
});

// ---- fiery tools, smelting -----------------------------------------------------------------------------

GF_REGISTER_SELFTEST("armory.fiery_tools", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto   box  = co_await ctx.flatArea(730, 5);
    auto   at   = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 1, box.minZ + 2};
    auto&  sp   = fighter(ctx, at, "armory_fiery", ids::kFierySword);
    auto   spId = uidOf(sp);
    Remove rm;
    // an iron golem (a cow would die of the 8 damage and the fire before the second look)
    auto*  cow = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z}, "minecraft:iron_golem");
    ctx.require(cow != nullptr, "an iron golem");
    auto cowId = uidOf(*cow);
    rm.uids.push_back(cowId);
    co_await ctx.ticks(12);
    (void)co_await hit(spId, cowId);
    auto* c = mobById(cowId);
    ctx.require(c != nullptr, "the golem survives the hit");
    ctx.check(c->isOnFire(), "the fiery sword sets it on fire");
    co_await ctx.ticks(60);
    c = mobById(cowId);
    ctx.check(c != nullptr && c->isOnFire(), "still burning 3 s later (15 s)");
    if (c) c->stopFire();
    // ToolEvents.fieryToolSetFire: any damage from a holder of a fiery tool, 1 s
    auto* cow2 = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z + 2}, "minecraft:cow");
    ctx.require(cow2 != nullptr, "a second cow");
    auto cow2Id = uidOf(*cow2);
    rm.uids.push_back(cow2Id);
    if (auto* p = simById(spId)) {
        p->setCarriedItem(make(ids::kFieryPickaxe));
        combat::hurt(*cow2, 1.f, "minecraft:mob_attack", p, p);
    }
    c = mobById(cow2Id);
    ctx.check(c != nullptr && c->isOnFire(), "damage dealt holding the fiery pickaxe sets the victim on fire");
    co_await ctx.ticks(60);
    c = mobById(cow2Id);
    ctx.check(c != nullptr && !c->isOnFire(), "for 1 s only");
});

GF_REGISTER_SELFTEST("armory.fiery_smelting", [](Context& ctx) -> ll::coro::CoroTask<> {
    // the modifier's smelting: vanilla's furnace recipes and TF's data recipes
    std::vector<ItemStack> drops{make("minecraft:raw_iron", 2), make("twilightforest:raw_ironwood", 1), make("minecraft:dirt", 1),
                                 make("twilightforest:armor_shard_cluster", 3)};
    float xp = smeltDrops(drops);
    ctx.check(items::idOf(drops[0]) == "minecraft:iron_ingot" && items::countOf(drops[0]) == 2, "raw iron x2 -> iron ingot x2: " + items::idOf(drops[0]));
    ctx.check(items::idOf(drops[1]) == "twilightforest:ironwood_ingot", "raw ironwood -> ironwood ingot: " + items::idOf(drops[1]));
    ctx.check(items::idOf(drops[2]) == "minecraft:dirt", "dirt stays dirt");
    ctx.check(items::idOf(drops[3]) == "twilightforest:knightmetal_ingot" && items::countOf(drops[3]) == 3, "armor shard cluster x3 -> knightmetal ingot x3");
    ctx.check(xp >= 2.0f, "the recipes' XP (raw ironwood 1.0, knightmetal 1.0, raw iron): " + fmt(xp));
    // a real block broken with the fiery pickaxe
    auto box   = co_await ctx.flatArea(740, 5);
    auto block = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 1, box.minZ + 2};
    ctx.require(ctx.setBlock(block, "minecraft:iron_ore"), "an iron ore");
    auto& sp   = fighter(ctx, core::GlobalPos{block.dim, block.x - 2, block.y, block.z}, "armory_smelter", ids::kFieryPickaxe);
    auto  spId = uidOf(sp);
    sp.simulateLookAt(game::blockPos(block), sim::LookDuration::Instant);
    ctx.check(sp.simulateDestroyBlock(game::blockPos(block), ScriptModuleMinecraft::ScriptFacing::West), "mining starts");
    int broken = -1;
    for (int t = 1; t <= 100 && broken < 0; ++t) {
        co_await ctx.ticks(1);
        if (ctx.blockAt(block) != "minecraft:iron_ore") broken = t;
    }
    ctx.require(broken > 0, "the fiery pickaxe breaks the iron ore");
    co_await ctx.ticks(4);
    int ingots = 0, raw = 0;
    for (auto* a : around(block, "minecraft:item", 3)) {
        auto id = items::idOf(static_cast<ItemActor*>(a)->item());
        if (id == "minecraft:iron_ingot") ingots++;
        if (id == "minecraft:raw_iron") raw++;
    }
    if (auto* p = simById(spId)) {
        auto& inv = p->getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i) {
            if (items::idOf(inv.getItem(i)) == "minecraft:iron_ingot") ingots++;
            if (items::idOf(inv.getItem(i)) == "minecraft:raw_iron") raw++;
        }
    }
    ctx.check(ingots > 0 && raw == 0, "the ore drops an iron ingot, no raw iron (" + std::to_string(ingots) + " / " + std::to_string(raw) + ")");
    ctx.info("xp orbs near the ore: " + std::to_string(around(block, "minecraft:xp_orb", 4).size()));
    for (auto* a : around(block, "minecraft:item", 3)) a->remove();
    for (auto* a : around(block, "minecraft:xp_orb", 4)) a->remove();
});

// ---- mazebreaker ---------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("armory.mazebreaker", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto const* maze  = game::blockWith("twilightforest:mazestone");
    auto const* stone = game::blockWith("minecraft:stone");
    ctx.require(maze && stone, "mazestone and stone");
    ctx.check(gf::tags::has(gf::tags::Registry::Block, ids::kMazebreakerTag, "twilightforest:mazestone"), "mazestone is accelerated");
#ifdef LL_PLAT_S
    float fast = gf::game::compat::destroySpeed(make(ids::kMazebreaker), *maze);
    float slow = gf::game::compat::destroySpeed(make(ids::kMazebreaker), *stone);
    float dia  = gf::game::compat::destroySpeed(make("minecraft:diamond_pickaxe"), *maze);
    ctx.check(std::abs(fast - 128.f) < 0.01f, "the mazebreaker mines mazestone at 8 x 16: " + fmt(fast));
    ctx.check(std::abs(slow - 8.f) < 0.01f, "and stone at 8: " + fmt(slow));
    ctx.check(fast >= dia * 15.f, "16 times a diamond pickaxe there: " + fmt(dia));
#endif
    // another tool breaking an accelerated block loses 16 more
    auto box   = co_await ctx.flatArea(750, 5);
    auto block = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 1, box.minZ + 2};
    ctx.require(ctx.setBlock(block, "twilightforest:castle_brick"), "a castle brick");
    auto pick = make("minecraft:diamond_pickaxe");
    gf::enchant::set(pick, "minecraft:efficiency", 5);
    auto& sp   = fighter(ctx, core::GlobalPos{block.dim, block.x - 2, block.y, block.z}, "armory_mazer", "");
    sp.setCarriedItem(pick);
    auto spId = uidOf(sp);
    sp.simulateLookAt(game::blockPos(block), sim::LookDuration::Instant);
    ctx.check(sp.simulateDestroyBlock(game::blockPos(block), ScriptModuleMinecraft::ScriptFacing::West), "mining starts");
    int broken = -1;
    for (int t = 1; t <= 400 && broken < 0; ++t) {
        co_await ctx.ticks(1);
        if (ctx.blockAt(block) != "twilightforest:castle_brick") broken = t;
    }
    ctx.require(broken > 0, "the diamond pickaxe breaks the castle brick");
    co_await ctx.ticks(2);
    auto* p = simById(spId);
    ctx.require(p != nullptr, "the miner");
    int damage = p->getCarriedItem().getDamageValue();
    ctx.check(damage == 17, "1 + 16 durability lost: " + std::to_string(damage));
    for (auto* a : around(block, "minecraft:item", 3)) a->remove();
});

// ---- set reactions, toughness ----------------------------------------------------------------------------

GF_REGISTER_SELFTEST("armory.set_reactions", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal); // peaceful: mobs do not hurt players
    struct RollReset {
        ~RollReset() { setFireReactRollForTest(std::nullopt); }
    } reset;
    auto   box  = co_await ctx.flatArea(760, 5);
    auto   at   = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 1, box.minZ + 2};
    auto&  sp   = fighter(ctx, at, "armory_wearer", "");
    auto   spId = uidOf(sp);
    Remove rm;
    auto   cowAt = [&](int dz) -> std::int64_t {
        auto* c = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z + dz}, "minecraft:cow");
        if (!c) return 0;
        rm.uids.push_back(uidOf(*c));
        return uidOf(*c);
    };
    auto hurtBy = [&](std::int64_t attacker) {
        auto* p = simById(spId);
        auto* a = actorById(attacker);
        if (p && a) combat::hurt(*p, 4.f, "minecraft:mob_attack", a, a);
    };
    // fiery: 2 pieces -> fireLevel 10; roll 9 < 10 ignites for 5 s, roll 10 does not
    sp.setArmor(Slot::Head, make("twilightforest:fiery_helmet"));
    sp.setArmor(Slot::Torso, make("twilightforest:fiery_chestplate"));
    ctx.check(countPieces(sp, "fiery") == 2, "two fiery pieces worn");
    auto c1 = cowAt(0);
    setFireReactRollForTest(9);
    hurtBy(c1);
    ctx.check(mobById(c1) && mobById(c1)->isOnFire(), "rand(25) = 9 < 10: the attacker burns");
    auto c2 = cowAt(2);
    setFireReactRollForTest(10);
    co_await ctx.ticks(12); // the wearer's invulnerability
    hurtBy(c2);
    ctx.check(mobById(c2) && !mobById(c2)->isOnFire(), "rand(25) = 10: it does not");
    // yeti: 4 pieces -> frosted for 25 ticks at amplifier 4
    if (auto* p = simById(spId)) {
        p->setArmor(Slot::Head, make("twilightforest:yeti_helmet"));
        p->setArmor(Slot::Torso, make("twilightforest:yeti_chestplate"));
        p->setArmor(Slot::Legs, make("twilightforest:yeti_leggings"));
        p->setArmor(Slot::Feet, make("twilightforest:yeti_boots"));
    }
    auto c3 = cowAt(4);
    // twilightforest:frosted is U10's effect: until it is registered the chill aura has nothing to give
    bool frostedKnown = false;
    if (auto* m = mobById(c3)) {
        frostedKnown = gf::effects::apply(*m, ids::kFrosted, 1, 0);
        if (frostedKnown) gf::effects::remove(*m, ids::kFrosted);
    }
    if (!frostedKnown) {
        ctx.info("twilightforest:frosted is not registered (U10): the yeti chill aura is not checked");
        co_return;
    }
    co_await ctx.ticks(12);
    hurtBy(c3);
    auto* cow3 = mobById(c3);
    ctx.check(cow3 && gf::effects::amplifier(*cow3, ids::kFrosted) == 4, "full yeti: the attacker is frosted at amplifier 4 (" +
                                                                          std::to_string(cow3 ? gf::effects::amplifier(*cow3, ids::kFrosted) : -2) + ")");
    co_await ctx.ticks(30);
    cow3 = mobById(c3);
    ctx.check(cow3 && !gf::effects::has(*cow3, ids::kFrosted), "for 25 ticks");
    // an attacker wearing a freeze immune piece is not frosted
    auto c4 = cowAt(1);
    auto* z = mobById(c4);
    ctx.require(z != nullptr, "a cow");
    z->setArmor(Slot::Feet, make("minecraft:leather_boots"));
    co_await ctx.ticks(12);
    hurtBy(c4);
    z = mobById(c4);
    ctx.check(z && !gf::effects::has(*z, ids::kFrosted), "leather boots (#freeze_immune_wearables) keep the frost off");
});

GF_REGISTER_SELFTEST("armory.toughness", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal); // peaceful: mobs do not hurt players
    auto  box  = co_await ctx.flatArea(770, 5);
    auto  at   = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 1, box.minZ + 2};
    auto& sp   = fighter(ctx, at, "armory_tough", "");
    auto  spId = uidOf(sp);
    // full yeti: armour 20, toughness 12
    sp.setArmor(Slot::Head, make("twilightforest:yeti_helmet"));
    sp.setArmor(Slot::Torso, make("twilightforest:yeti_chestplate"));
    sp.setArmor(Slot::Legs, make("twilightforest:yeti_leggings"));
    sp.setArmor(Slot::Feet, make("twilightforest:yeti_boots"));
    ctx.check(sp.getToughnessValue() == 12, "Mob::getToughnessValue adds the yeti toughness: " + std::to_string(sp.getToughnessValue()));
    ctx.check(sp.getArmorValue() == 20, "armour value 20: " + std::to_string(sp.getArmorValue()));
    co_await ctx.ticks(2);
    auto* p = simById(spId);
    ctx.require(p != nullptr, "the wearer");
    Remove rm;
    auto*  cow = spawnAt(core::GlobalPos{at.dim, at.x + 2, at.y, at.z}, "minecraft:cow");
    ctx.require(cow != nullptr, "a cow");
    rm.uids.push_back(uidOf(*cow));
    float before = static_cast<float>(p->getHealth());
    combat::hurt(*p, 10.f, "minecraft:mob_attack", cow, cow);
    p = simById(spId);
    ctx.require(p != nullptr, "the wearer survives");
    float lost = before - static_cast<float>(p->getHealth());
    float java = ca::damageAfterAbsorb(10.f, 20.f, 12.f), plain = ca::damageAfterAbsorb(10.f, 20.f, 0.f);
    ctx.info("10 mob damage on full yeti: " + fmt(lost) + " health lost (Java " + fmt(java) + ", without toughness " + fmt(plain) + ")");
    ctx.check(lost < plain - 0.5f, "toughness absorbs more than armour alone: " + fmt(lost));
});

// ---- phantom armour, arctic dye -------------------------------------------------------------------------

GF_REGISTER_SELFTEST("armory.phantom_kept", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(780);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.check(gf::tags::has(gf::tags::Registry::Item, "twilightforest:kept_on_death", "twilightforest:phantom_helmet"), "phantom helmet in #kept_on_death");
    ctx.check(gf::tags::has(gf::tags::Registry::Item, "twilightforest:kept_on_death", "twilightforest:phantom_chestplate"), "phantom chestplate in #kept_on_death");
    auto& sp = ctx.spawnPlayer("armory_phantom");
    sp.teleport(Vec3{pos.x + 0.5f, float(pos.y), pos.z + 0.5f}, DimensionType(pos.dim));
    sp.setPlayerGameType(GameType::Survival);
    auto id = sp.getOrCreateUniqueID();
    sp.setArmor(Slot::Head, make("twilightforest:phantom_helmet"));
    sp.setArmor(Slot::Torso, make("twilightforest:phantom_chestplate"));
    sp.setArmor(Slot::Legs, make("minecraft:iron_leggings"));
    combat::hurt(sp, 1000.f, "minecraft:generic_kill");
    co_await ctx.ticks(4);
    int dropped = 0, droppedPhantom = 0;
    for (auto* a : around(pos, "minecraft:item", 6)) {
        auto iid = items::idOf(static_cast<ItemActor*>(a)->item());
        dropped++;
        if (iid.find("phantom") != std::string::npos) droppedPhantom++;
    }
    ctx.check(droppedPhantom == 0, "no phantom piece dropped (" + std::to_string(dropped) + " drops)");
    auto* p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player is still in the level");
    static_cast<SimulatedPlayer&>(*p).simulateRespawn();
    co_await ctx.ticks(10);
    p = ll::service::getLevel()->getPlayer(id);
    ctx.require(p != nullptr, "the player respawned");
    int back = 0;
    for (int i = 0; i < 4; ++i)
        if (items::idOf(p->getArmor(static_cast<Slot>(i))).find("phantom") != std::string::npos) back++;
    auto& inv = p->getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (!items::isEmpty(inv.getItem(i)) && items::idOf(inv.getItem(i)).find("phantom") != std::string::npos) back++;
    ctx.check(back == 2, "both phantom pieces came back: " + std::to_string(back));
    for (auto* a : around(pos, "minecraft:item", 6)) a->remove();
});

GF_REGISTER_SELFTEST("armory.arctic_dye", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto helmet = make("twilightforest:arctic_helmet");
    auto* item  = helmet.mItem.get();
    ctx.require(item && item->isDyeable(), "arctic armour is dyeable");
    ctx.check(!item->hasCustomColor(helmet), "undyed");
    auto def = helmet.getColor();
    ctx.info("the undyed colour: " + fmt(def.r) + " " + fmt(def.g) + " " + fmt(def.b));
    item->setColor(helmet, mce::Color(0.2f, 0.4f, 0.8f, 1.0f));
    ctx.check(item->hasCustomColor(helmet), "dyed");
    // a water cauldron washes the dye off
    auto box  = co_await ctx.flatArea(790, 5);
    auto cpos = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 1, box.minZ + 2};
    ctx.require(ctx.setBlock(cpos, "minecraft:cauldron", {{"fill_level", 6}, {"cauldron_liquid", "water"}}), "a water cauldron");
    auto& sp = fighter(ctx, core::GlobalPos{cpos.dim, cpos.x - 2, cpos.y, cpos.z}, "armory_dyer", "");
    ctx.give(sp, helmet, 0);
    sp.setSelectedSlot(0);
    auto spId = uidOf(sp);
    sp.simulateLookAt(game::blockPos(cpos), sim::LookDuration::Instant);
    sp.simulateUseItemInSlotOnBlock(0, game::blockPos(cpos), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(2);
    auto* p = simById(spId);
    ctx.require(p != nullptr, "the dyer");
    auto const& held = p->getInventory().getItem(0);
    ctx.check(items::idOf(held) == "twilightforest:arctic_helmet" && !held.mItem->hasCustomColor(held), "the cauldron washed the dye off");
});

// ---- recipes ------------------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("armory.crafted_enchantments", [](Context& ctx) {
    namespace rc = gf::recipes;
    int n = 0;
    for (auto const& d : ca::defaultEnchantments()) {
        // the table matches the data's result components
        auto const* r = rc::byId(std::string(d.recipe));
        ctx.check(r != nullptr, "recipe " + std::string(d.recipe));
        if (r) {
            auto levels = r->data.value("result", nlohmann::json::object()).value("components", nlohmann::json::object())
                              .value("minecraft:enchantments", nlohmann::json::object()).value("levels", nlohmann::json::object());
            for (auto const& [id, lvl] : d.enchantments)
                ctx.check(levels.value(std::string(id), 0) == lvl, std::string(d.recipe) + " " + std::string(id) + " " + std::to_string(lvl) + " in the data");
            ctx.check(levels.size() == d.enchantments.size(), std::string(d.recipe) + ": as many enchantments as the data");
        }
        rc::CraftingGrid grid;
        auto             out = gf::recipes::internal::assembleSpecial(std::string(d.recipe), grid);
        ctx.check(out && items::idOf(*out) == d.item, std::string(d.recipe) + " crafts " + std::string(d.item));
        if (out)
            for (auto const& [id, lvl] : d.enchantments)
                ctx.check(gf::enchant::level(*out, std::string(id)) == lvl, std::string(d.item) + " has " + std::string(id) + " " + std::to_string(lvl));
        ++n;
    }
    ctx.check(n == 24, "24 crafted results carry enchantments");
});

GF_REGISTER_SELFTEST("armory.no_template_smithing", [](Context& ctx) {
    auto cloth = make(ids::kEmperorsCloth);
    ctx.require(!items::isEmpty(cloth), "the emperor's cloth");
    auto chest = make("twilightforest:knightmetal_chestplate");
    auto out   = noTemplateSmithing(chest, cloth);
    ctx.require(out.has_value(), "knightmetal chestplate + cloth: a result");
    ctx.check(items::idOf(*out) == "twilightforest:knightmetal_chestplate", "a copy of the base");
    ctx.check(gf::item::getData(*out, "twilightforest:emperors_cloth").is_object(), "with twilightforest:emperors_cloth");
    ctx.check(!noTemplateSmithing(*out, cloth).has_value(), "refused when the base has it already");
    ctx.check(!noTemplateSmithing(make("minecraft:stick"), cloth).has_value(), "refused for a base outside #emperors_cloth_applicable");
    ctx.check(noTemplateSmithing(make("minecraft:iron_helmet"), cloth).has_value(), "a vanilla helmet takes it");
    ctx.check(noTemplateSmithing(make("minecraft:elytra"), cloth).has_value(), "the elytra takes it");
    // no crafting-table copy: TF's own crafting recipe (U9's twilightforest:emperors_cloth_recipe) is the one
    ctx.check(!gf::recipes::internal::assembleSpecial(ids::kClothSmithing, gf::recipes::CraftingGrid{}).has_value(),
              "no special crafting registered under twilightforest:emperors_cloth_smithing");
});

} // namespace gf::feature::armory::tests

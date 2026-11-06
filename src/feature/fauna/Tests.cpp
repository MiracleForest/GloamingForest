// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
//
// Selftests of U16 (rule R12: "fauna.*", testPos dx 1600..1699): each mob spawned with F13's
// attributes, the bighorn's shearing, dyeing, lamb colour and loot tables, the variants and their
// saved strings, the kobold's bread, the mosquito's hunger, the penguin's spawn rule, the deer's
// shika senbei, the flying birds' take-off, the parrot imitation map and the vanilla prey goals.
#include <algorithm>
#include <cmath>
#include <set>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/effects/Effects.h"
#include "feature/entity/Entity.h"
#include "feature/ext/fauna/W0Internals.h"
#include "feature/fauna/Fauna.h"
#include "feature/loot/Loot.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::fauna::tests {

namespace {

using selftest::Context;
using Difficulty = SharedTypes::Legacy::Difficulty;
namespace fe     = gf::feature::entity;
namespace ia     = gf::interactions;

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

core::GlobalPos at(core::GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

std::vector<ItemActor*> itemsAround(core::GlobalPos const& pos, float r) {
    std::vector<ItemActor*> out;
    auto*                   region = game::regionOf(pos.dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, pos.dim, AABB{Vec3{pos.x - r, pos.y - r, pos.z - r}, Vec3{pos.x + 1 + r, pos.y + 1 + r, pos.z + 1 + r}}))
        if (a && a->isType(ActorType::ItemEntity)) out.push_back(static_cast<ItemActor*>(a));
    return out;
}

int countItems(core::GlobalPos const& pos, float r, std::string const& id) {
    int n = 0;
    for (auto* i : itemsAround(pos, r))
        if (game::items::idOf(i->item()) == id) n += game::items::countOf(i->item());
    return n;
}

/// Removes the non-player actors around `pos` within `r` blocks in x, staying in the test row (rule
/// R12: z from the row's start to + 4; `pos` must be in the row).
void clearAround(core::GlobalPos const& pos, float r, int rowZ) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return;
    for (auto* a : fe::actorsIn(*region, pos.dim, AABB{Vec3{pos.x - r, pos.y - 8.0f, float(rowZ)}, Vec3{pos.x + 1 + r, pos.y + 8.0f, float(rowZ + 5)}}))
        if (a && !a->isPlayer()) a->remove();
}

float attr(Mob& m, Attribute const& a) {
    auto        ref  = m.getAttribute(a);
    auto const* inst = static_cast<AttributeInstance const*>(ref.mPtr);
    return inst ? inst->mCurrentValue : -1.0f;
}

bool near(float a, float b) { return std::abs(a - b) < 0.01f; }

} // namespace

GF_REGISTER_SELFTEST("fauna.attributes", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box  = co_await ctx.flatArea(1600, 16);
    auto            base = core::GlobalPos{ctx.dimension(), box.minX + 1, box.minY + 1, box.minZ + 2};
    // F13's goldens (tests/golden/mobs.json): health, movement speed, attack damage, box, eye height
    struct Row {
        char const* id;
        int         health;
        float       speed, attack, width, height, eye;
    };
    Row const rows[] = {
        {kBoar, 10, 0.25f, -1, 0.9f, 0.9f, 0.765f},          {kBighorn, 8, 0.23f, -1, 0.9f, 1.3f, 1.105f},
        {kDeer, 10, 0.2f, -1, 0.7f, 1.8f, 1.53f},            {kPenguin, 10, 0.2f, -1, 0.5f, 0.9f, 0.765f},
        {kTinyBird, 4, 0.2f, -1, 0.3f, 0.3f, 0.255f},        {kSquirrel, 6, 0.3f, -1, 0.3f, 0.5f, 0.425f},
        {kDwarfRabbit, 3, 0.3f, -1, 0.4f, 0.4f, 0.34f},      {kRaven, 10, 0.2f, -1, 0.3f, 0.5f, 0.425f},
        {kKobold, 13, 0.28f, 4.0f, 0.8f, 1.1f, 0.935f},      {kMosquitoSwarm, 12, 0.23f, 3.0f, 0.7f, 1.9f, 1.615f},
    };
    std::vector<std::int64_t> spawned;
    int                       i = 0;
    for (auto const& r : rows) {
        int   dz = (i % 2) * 2;
        auto* m  = spawnAt(at(base, i, 0, dz), r.id);
        ++i;
        ctx.require(m != nullptr, std::string("spawned ") + r.id);
        std::string const id = r.id;
        ctx.check(m->getMaxHealth() == r.health, id + " max health " + std::to_string(m->getMaxHealth()));
        ctx.check(near(attr(*m, SharedAttributes::MOVEMENT_SPEED()), r.speed), id + " movement speed " + std::to_string(attr(*m, SharedAttributes::MOVEMENT_SPEED())));
        if (r.attack > 0)
            ctx.check(near(attr(*m, SharedAttributes::ATTACK_DAMAGE()), r.attack), id + " attack damage " + std::to_string(attr(*m, SharedAttributes::ATTACK_DAMAGE())));
        ctx.check(near(attr(*m, SharedAttributes::FOLLOW_RANGE()), 16.0f), id + " follow range");
        auto const& b = m->getAABB();
        ctx.check(near(b.max.x - b.min.x, r.width) && near(b.max.y - b.min.y, r.height),
                  id + " box " + std::to_string(b.max.x - b.min.x) + " x " + std::to_string(b.max.y - b.min.y));
        spawned.push_back(uidOf(*m));
    }
    co_await ctx.ticks(5);
    for (std::size_t k = 0; k < spawned.size(); ++k) {
        auto* m = mobById(spawned[k]);
        ctx.check(m != nullptr, std::string(rows[k].id) + " is still there");
        if (!m) continue;
        float eye = m->getEyePos().y - m->getFeetPos().y;
        ctx.check(std::abs(eye - rows[k].eye) < 0.01f, m->getTypeName() + " eye height " + std::to_string(eye));
        // finalizeSpawn equivalents: the bighorn's fleece, the tiny bird's and dwarf rabbit's variants
        if (m->getTypeName() == kBighorn || m->getTypeName() == kTinyBird || m->getTypeName() == kDwarfRabbit) {
            auto const& init = gf::entity::data(*m, "fauna:init");
            ctx.check(init.is_boolean() && init.get<bool>(), m->getTypeName() + " ran its spawn set-up");
        }
    }
    // the variants and the fleece got their spawn values (variant index in range, saved string)
    if (auto* bird = mobById(spawned[4])) {
        int v = variantOf(*bird);
        ctx.check(v >= 0 && v < 4, "tiny bird variant " + std::to_string(v));
        auto const& s = gf::entity::data(*bird, "fauna:variant");
        ctx.check(s.is_string() && s.get<std::string>() == variantsOf(kTinyBird)[static_cast<std::size_t>(std::clamp(v, 0, 3))].id,
                  "the tiny bird's variant string " + s.dump());
        ctx.check(birdLanded(*bird), "a new flying bird is landed");
    }
    if (auto* rabbit = mobById(spawned[6])) ctx.check(variantOf(*rabbit) >= 0 && variantOf(*rabbit) < 3, "dwarf rabbit variant");
    for (auto uid : spawned)
        if (auto* a = fe::actorById(uid)) a->remove();
    clearAround(at(base, 5, 0, 0), 5, box.minZ);
});

GF_REGISTER_SELFTEST("fauna.bighorn_shear", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box   = co_await ctx.flatArea(1620, 8);
    auto  pos   = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    auto* sheep = spawnAt(pos, kBighorn);
    ctx.require(sheep != nullptr, "a bighorn");
    auto uid = uidOf(*sheep);
    co_await ctx.ticks(2);
    sheep = mobById(uid);
    ctx.require(sheep != nullptr, "the bighorn stays");
    setColor(*sheep, 14);
    co_await ctx.ticks(1);
    sheep = mobById(uid);
    ctx.require(sheep && colorOf(*sheep) == 14, "the fleece is red");
    ctx.check(bighornTable(*sheep) == "twilightforest:entities/bighorn_sheep/red", "the red table while it has its fleece: " + bighornTable(*sheep));
    // shears through the interaction (Sheep#mobInteract): 1..3 red wool, sheared, the shears worn
    auto& player = ctx.player();
    player.setPlayerGameType(GameType::Survival);
    auto shears = game::items::make("minecraft:shears", 1);
    ctx.require(shears.has_value(), "shears");
    player.setSelectedItem(*shears);
    auto const& held = player.getSelectedItem();
    std::string heldId = game::items::idOf(held);
    ia::EntityContext ec{player, held, heldId, *sheep, sheep->getTypeName(), {pos.x + .5, double(pos.y), pos.z + .5}, false};
    ctx.check(ia::dispatchInteract(ec) == ia::Result::Consume, "the shears took the click");
    co_await ctx.ticks(3);
    sheep = mobById(uid);
    ctx.require(sheep != nullptr, "the sheared bighorn");
    ctx.check(isSheared(*sheep), "it is sheared");
    int wool = countItems(pos, 4, "minecraft:red_wool");
    ctx.check(wool >= 1 && wool <= 3, "1..3 red wool dropped: " + std::to_string(wool));
    ctx.check(ctx.player().getSelectedItem().getDamageValue() == 1, "the shears lost 1 durability");
    ctx.check(!shearBighorn(*sheep, nullptr), "a sheared bighorn can't be sheared again");
    ctx.check(bighornTable(*sheep) == "twilightforest:entities/bighorn_sheep", "the sheared table");
    ctx.check(!dyeBighorn(*sheep, 11), "a sheared bighorn can't be dyed");
    // eating grass regrows the fleece (EatBlockGoal -> Sheep#ate)
    fireEvent(*sheep, "regrow_wool");
    co_await ctx.ticks(2);
    sheep = mobById(uid);
    ctx.require(sheep != nullptr, "the bighorn after eating");
    ctx.check(!isSheared(*sheep), "the fleece grew back");
    // dyeing (DyeItem#interactLivingEntity)
    ctx.check(dyeBighorn(*sheep, 11), "blue dye changes the colour");
    co_await ctx.ticks(1);
    sheep = mobById(uid);
    ctx.check(sheep && colorOf(*sheep) == 11, "the fleece is blue");
    ctx.check(sheep && !dyeBighorn(*sheep, 11), "the same colour again does nothing");
    if (sheep) sheep->remove();
    clearAround(pos, 3, box.minZ);
});

GF_REGISTER_SELFTEST("fauna.bighorn_lamb_colour", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box = co_await ctx.flatArea(1630, 8);
    auto  pos = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    auto* ram = spawnAt(pos, kBighorn);
    auto* ewe = spawnAt(at(pos, 1, 0, 0), kBighorn);
    ctx.require(ram && ewe, "two bighorns");
    auto a = uidOf(*ram), b = uidOf(*ewe);
    co_await ctx.ticks(2);
    if (auto* m = mobById(a)) setColor(*m, 14); // red
    if (auto* m = mobById(b)) setColor(*m, 4);  // yellow
    co_await ctx.ticks(1);
    auto* lamb = spawnAt(at(pos, 0, 0, 1), kBighorn);
    ctx.require(lamb != nullptr, "a lamb");
    auto l = uidOf(*lamb);
    fireEvent(*lamb, "minecraft:entity_born");
    co_await ctx.ticks(3);
    lamb = mobById(l);
    ctx.require(lamb != nullptr && lamb->isBaby(), "the lamb is a baby");
    ctx.check(colorOf(*lamb) == 1, "red and yellow parents make an orange lamb: " + std::to_string(colorOf(*lamb)));
    for (auto u : {a, b, l})
        if (auto* m = fe::actorById(u)) m->remove();
});

GF_REGISTER_SELFTEST("fauna.loot_tables", [](Context& ctx) {
    gf::loot::LootContext lc;
    lc.region = ctx.region();
    auto has  = [](std::vector<ItemStack> const& items, std::string const& id) {
        return std::any_of(items.begin(), items.end(), [&](ItemStack const& s) { return game::items::idOf(s) == id; });
    };
    auto red = gf::loot::roll("twilightforest:entities/bighorn_sheep/red", lc);
    ctx.check(has(red, "minecraft:red_wool") && has(red, "minecraft:mutton"), "the red table: red wool and mutton");
    auto sheared = gf::loot::roll("twilightforest:entities/bighorn_sheep", lc);
    ctx.check(has(sheared, "minecraft:mutton") && !has(sheared, "minecraft:red_wool"), "the sheared table: mutton only");
    auto boar = gf::loot::roll("twilightforest:entities/boar", lc);
    ctx.check(has(boar, "minecraft:porkchop"), "the boar drops porkchops");
    auto raven = gf::loot::roll("twilightforest:entities/raven", lc);
    for (auto const& s : raven) ctx.check(game::items::idOf(s) == "twilightforest:raven_feather", "the raven drops raven feathers");
    ctx.check(gf::loot::roll("twilightforest:entities/squirrel", lc).empty(), "the squirrel drops nothing");
    for (int i = 0; i < 20; ++i)
        for (auto const& s : gf::loot::roll("twilightforest:entities/dwarf_rabbit", lc)) {
            auto id = game::items::idOf(s);
            ctx.check(id == "minecraft:rabbit_hide" || id == "minecraft:rabbit", "the dwarf rabbit's vanilla rabbit loot: " + id);
        }
});

GF_REGISTER_SELFTEST("fauna.variants_persist", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box    = co_await ctx.flatArea(1640, 8);
    auto  pos    = core::GlobalPos{ctx.dimension(), box.minX + 3, box.minY + 1, box.minZ + 2};
    auto* bird   = spawnAt(pos, kTinyBird);
    auto* rabbit = spawnAt(at(pos, 2, 0, 0), kDwarfRabbit);
    ctx.require(bird && rabbit, "a tiny bird and a dwarf rabbit");
    auto bu = uidOf(*bird), ru = uidOf(*rabbit);
    co_await ctx.ticks(2);
    ctx.check(variantsOf(kTinyBird).size() == 4 && variantsOf(kTinyBird)[3].id == "twilightforest:red", "the tiny bird variants (blue, brown, gold, red)");
    ctx.check(variantsOf(kDwarfRabbit).size() == 3 && variantsOf(kDwarfRabbit)[0].id == "twilightforest:brown", "the dwarf rabbit variants");
    ctx.check(defaultVariant(kTinyBird) == 3 && defaultVariant(kDwarfRabbit) == 0, "the defaults: red, brown");
    // readAdditionalSaveData: the saved string wins, an unknown id keeps the current variant
    if (auto* b = mobById(bu)) {
        gf::entity::data(*b, "fauna:variant") = "twilightforest:gold";
        restoreVariant(*b);
    }
    if (auto* r = mobById(ru)) {
        gf::entity::data(*r, "fauna:variant") = "twilightforest:dutch";
        restoreVariant(*r);
    }
    co_await ctx.ticks(2);
    auto* b = mobById(bu);
    auto* r = mobById(ru);
    ctx.require(b && r, "both still there");
    ctx.check(variantOf(*b) == 2, "the tiny bird is gold: " + std::to_string(variantOf(*b)));
    ctx.check(variantOf(*r) == 1, "the dwarf rabbit is dutch: " + std::to_string(variantOf(*r)));
    gf::entity::data(*b, "fauna:variant") = "twilightforest:nope";
    restoreVariant(*b);
    co_await ctx.ticks(1);
    b = mobById(bu);
    ctx.check(b && variantOf(*b) == 2, "an unknown saved id keeps the variant");
    // a dwarf rabbit kit takes a parent's variant (19 in 20) or a common one
    auto* kit = spawnAt(at(pos, 2, 0, 1), kDwarfRabbit);
    ctx.require(kit != nullptr, "a kit");
    auto ku = uidOf(*kit);
    fireEvent(*kit, "minecraft:entity_born");
    co_await ctx.ticks(3);
    kit = mobById(ku);
    ctx.check(kit && kit->isBaby() && variantOf(*kit) >= 0 && variantOf(*kit) < 3, "the kit has a variant");
    for (auto u : {bu, ru, ku})
        if (auto* m = fe::actorById(u)) m->remove();
});

GF_REGISTER_SELFTEST("fauna.kobold_bread", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box    = co_await ctx.flatArea(1650, 8);
    auto            pos    = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    auto*           kobold = spawnAt(pos, kKobold);
    ctx.require(kobold != nullptr, "a kobold");
    auto uid = uidOf(*kobold);
    co_await ctx.ticks(2);
    kobold = mobById(uid);
    ctx.require(kobold != nullptr, "the kobold stays");
    ctx.check(nativeGoalsPresent(*kobold), "the kobold has its native goals (flock panic, seek bread, flock)");
    auto bread = game::items::make("minecraft:bread", 3);
    ctx.require(bread.has_value(), "bread");
    auto* item = ll::service::getLevel()->getSpawner().spawnItem(kobold->getDimensionBlockSource(), *bread, nullptr, kobold->getFeetPos(), 0);
    ctx.require(item != nullptr, "bread on the ground");
    auto itemId = uidOf(*item);
    // Kobold#pickUpItem (directly: Mob#aiStep's reach also finds it once its pick-up delay is over)
    bool took = false;
    for (int t = 0; t < 60 && !took; ++t) {
        co_await ctx.ticks(1);
        kobold = mobById(uid);
        ctx.require(kobold != nullptr, "the kobold");
        took = game::items::idOf(kobold->getCarriedItem()) == "minecraft:bread";
        if (!took && t == 30)
            if (auto* it = fe::actorById(itemId)) took = koboldPickUp(*kobold, *it);
    }
    ctx.require(took, "the kobold picked up the bread");
    kobold = mobById(uid);
    ctx.check(game::items::countOf(kobold->getCarriedItem()) == 1, "it holds one bread");
    ctx.check(countItems(pos, 4, "minecraft:bread") == 2, "the other two are dropped: " + std::to_string(countItems(pos, 4, "minecraft:bread")));
    auto t = koboldTimers(*kobold);
    ctx.check(t.eatingTime > 0 && t.eatingTime <= 200 + 600, "the eating time (200 + 0..599 on normal): " + std::to_string(t.eatingTime));
    ctx.check(fe::mobs::target(*kobold) == nullptr, "no target while holding bread");
    // finish eating (the two dropped breads gone first, or it picks one up again)
    for (auto* it : itemsAround(pos, 4))
        if (game::items::idOf(it->item()) == "minecraft:bread") it->remove();
    setKoboldTimers(*kobold, {.eatingTime = 2, .lastEatenBread = 5});
    co_await ctx.ticks(5);
    kobold = mobById(uid);
    ctx.require(kobold != nullptr, "the kobold after eating");
    ctx.check(game::items::isEmpty(kobold->getCarriedItem()), "the bread is eaten");
    // a hint kobold's book is never eaten (F6 gives it the book)
    if (auto book = game::items::make("minecraft:written_book", 1)) {
        kobold->setCarriedItem(*book);
        setKoboldTimers(*kobold, {.eatingTime = 0, .lastEatenBread = 100});
        co_await ctx.ticks(3);
        kobold = mobById(uid);
        ctx.check(kobold && game::items::idOf(kobold->getCarriedItem()) == "minecraft:written_book", "the book stays in its hand");
    }
    if (auto* k = mobById(uid)) k->remove();
    clearAround(pos, 3, box.minZ);
});

GF_REGISTER_SELFTEST("fauna.mosquito_hunger", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyScope normal(Difficulty::Normal);
    auto            box = co_await ctx.flatArea(1660, 8);
    auto            pos = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    auto*           swarm = spawnAt(pos, kMosquitoSwarm);
    auto*           boar  = spawnAt(at(pos, 1, 0, 0), kBoar);
    ctx.require(swarm && boar, "a swarm and a boar");
    auto su = uidOf(*swarm), bu = uidOf(*boar);
    co_await ctx.ticks(2);
    swarm = mobById(su);
    boar  = mobById(bu);
    ctx.require(swarm && boar, "both there");
    ctx.check(!gf::effects::has(*boar, "minecraft:hunger"), "no hunger before the bite");
    fe::mobs::doHurtTarget(*swarm, *boar);
    co_await ctx.ticks(1);
    boar = mobById(bu);
    ctx.check(boar && gf::effects::has(*boar, "minecraft:hunger"), "the bite gives hunger");
    for (auto u : {su, bu})
        if (auto* m = fe::actorById(u)) m->remove();
});

GF_REGISTER_SELFTEST("fauna.penguin_spawn_rule", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(1670, 5);
    auto pos = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY + 1, box.minZ + 2};
    auto* region = ctx.region();
    ctx.require(region != nullptr, "the region");
    ctx.require(ctx.setBlock(at(pos, 0, -1, 0), "minecraft:ice"), "ice");
    ctx.check(penguinCanSpawnOn(*region, BlockPos{pos.x, pos.y, pos.z}), "penguins spawn on ice");
    ctx.require(ctx.setBlock(at(pos, 0, -1, 0), "minecraft:packed_ice"), "packed ice");
    ctx.check(penguinCanSpawnOn(*region, BlockPos{pos.x, pos.y, pos.z}), "penguins spawn on packed ice");
    ctx.require(ctx.setBlock(at(pos, 0, -1, 0), "minecraft:stone"), "stone");
    ctx.check(!penguinCanSpawnOn(*region, BlockPos{pos.x, pos.y, pos.z}), "not on stone");
});

GF_REGISTER_SELFTEST("fauna.deer_senbei", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box  = co_await ctx.flatArea(1675, 5);
    auto  pos  = core::GlobalPos{ctx.dimension(), box.minX + 2, box.minY + 1, box.minZ + 2};
    auto* deer = spawnAt(pos, kDeer);
    ctx.require(deer != nullptr, "a deer");
    auto uid = uidOf(*deer);
    co_await ctx.ticks(2);
    deer = mobById(uid);
    ctx.require(deer != nullptr, "the deer stays");
    gf::combat::hurt(*deer, 6.0f, "minecraft:generic");
    co_await ctx.ticks(1);
    deer = mobById(uid);
    ctx.require(deer != nullptr, "the hurt deer");
    int before = deer->getHealth();
    deerEatSenbei(*deer);
    ctx.check(deer->getHealth() == std::min(before + 4, deer->getMaxHealth()), "shika senbei heals 4: " + std::to_string(before) + " -> " + std::to_string(deer->getHealth()));
    deer->remove();
});

GF_REGISTER_SELFTEST("fauna.flying_bird", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box  = co_await ctx.flatArea(1680, 10);
    auto  pos  = core::GlobalPos{ctx.dimension(), box.minX + 5, box.minY + 1, box.minZ + 2};
    auto* bird = spawnAt(pos, kRaven);
    ctx.require(bird != nullptr, "a raven");
    auto uid = uidOf(*bird);
    co_await ctx.ticks(2);
    bird = mobById(uid);
    ctx.require(bird && birdLanded(*bird), "the raven starts landed");
    // Raven#isSpooked: hurt by a mob
    if (auto* boar = spawnAt(at(pos, 1, 0, 0), kBoar)) {
        gf::combat::hurt(*bird, 1.0f, "minecraft:mob_attack", boar, boar);
        boar->remove();
    }
    bool tookOff = false, landedAgain = false;
    for (int t = 0; t < 400 && !landedAgain; ++t) {
        co_await ctx.ticks(1);
        bird = mobById(uid);
        if (!bird) break;
        if (!birdLanded(*bird)) tookOff = true;
        else if (tookOff && t > 5) landedAgain = true;
    }
    ctx.check(tookOff, "the spooked raven took off");
    ctx.check(landedAgain, "and landed again on solid ground");
    if (auto* b = mobById(uid)) b->remove();
    clearAround(pos, 4, box.minZ);
});

GF_REGISTER_SELFTEST("fauna.parrot_imitations", [](Context& ctx) {
    ctx.check(parrotImitation("twilightforest:kobold") == "twilightforest:entity.twilightforest.parrot.imitate.kobold", "kobold: " + parrotImitation("twilightforest:kobold"));
    ctx.check(parrotImitation("twilightforest:lich") == "minecraft:entity.parrot.imitate.blaze", "lich: blaze");
    ctx.check(parrotImitation("twilightforest:mosquito_swarm") == "twilightforest:entity.twilightforest.parrot.imitate.mosquito", "mosquito swarm");
    ctx.check(parrotImitation("twilightforest:hedge_spider") == "minecraft:entity.parrot.imitate.spider", "hedge spider: spider");
    ctx.check(parrotImitation("twilightforest:boar").empty(), "the boar is not imitated");
    ctx.check(parrotImitation("minecraft:zombie").empty(), "vanilla mobs are Bedrock's parrots' own");
});

GF_REGISTER_SELFTEST("fauna.prey", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  box  = co_await ctx.flatArea(1690, 8);
    auto  pos  = core::GlobalPos{ctx.dimension(), box.minX + 4, box.minY + 1, box.minZ + 2};
    auto* wolf = spawnAt(pos, "minecraft:wolf");
    auto* cat  = spawnAt(at(pos, 2, 0, 0), "minecraft:cat");
    ctx.require(wolf && cat, "a wolf and a cat");
    auto wu = uidOf(*wolf), cu = uidOf(*cat);
    co_await ctx.ticks(3);
    for (auto [u, id] : {std::pair{wu, "minecraft:wolf"}, std::pair{cu, "minecraft:cat"}}) {
        auto const* s = fe::findState(u);
        ctx.check(s && s->targets && static_cast<int>(s->targets->size()) == preyGoalCount(id),
                  std::string(id) + " got its prey goals (" + std::to_string(s && s->targets ? s->targets->size() : 0) + ")");
    }
    ctx.check(preyGoalCount("minecraft:fox") == 2 && preyGoalCount("minecraft:ocelot") == 3, "foxes hunt two kinds, ocelots three");
    for (auto u : {wu, cu})
        if (auto* m = fe::actorById(u)) m->remove();
});

} // namespace gf::feature::fauna::tests

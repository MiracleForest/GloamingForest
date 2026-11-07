// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U8 (weapons) selftests (weapons.*, testPos dx 800..899, rule R12).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerItemInUse.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantResult.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/enchant/Enchant.h"
#include "feature/entity/Entity.h"
#include "feature/ext/item/Item.h"
#include "feature/item/Item.h"
#include "feature/projectile/Projectile.h"
#include "feature/stats/Stats.h"
#include "feature/weapons/Weapons.h"

#include "feature/ext/entity/EntityExt.h"
#include "feature/weapons/World.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::weapons {

extern int gAnnihilated;
extern int gEnderSwaps;
extern int gPunches;
extern int gGlassBreaks;
extern int gParries;
extern int gShieldBlocks;
extern int gLampBurns;
extern int gMoonwormsPlaced;
extern int gIceZoneHits;

namespace tests {

using gf::selftest::Context;

namespace {

Mob* spawnMob(core::GlobalPos const& at, std::string const& id, float dx = 0.5f, float dz = 0.5f, float dy = 0.0f) {
    auto* region = game::regionOf(at.dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return nullptr;
    return level->getSpawner().spawnMob(*region, ActorDefinitionIdentifier(id), nullptr, Vec3{at.x + dx, float(at.y) + dy, at.z + dz}, false, false, false);
}

/// Puts the test player at `at` (block centre) looking at `target`.
void place(Context& ctx, core::GlobalPos const& at, Vec3 const& target) {
    char cmd[256];
    std::snprintf(cmd, sizeof cmd, "tp \"%s\" %.2f %d %.2f facing %.2f %.2f %.2f", ctx.player().getRealName().c_str(), at.x + 0.5, at.y, at.z + 0.5, target.x, target.y, target.z);
    (void)gf::selftest::runServerCommand(cmd, at.dim);
}

void survival(Context& ctx) { (void)gf::selftest::runServerCommand("gamemode survival \"" + ctx.player().getRealName() + "\"", ctx.dimension()); }

BlockPos bp(core::GlobalPos const& p, int dx = 0, int dy = 0, int dz = 0) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }

int maxDamage(ItemStack const& s) { return s.mItem ? s.mItem->getMaxDamage() : 0; }

} // namespace

// Each item's durability, stack size and rarity (TFItems registrations).
GF_REGISTER_SELFTEST("weapons.items", [](Context& ctx) {
    struct Row {
        char const*            id;
        int                    durability;
        int                    stack;
        gf::item::ext::Rarity  rarity;
    };
    using R = gf::item::ext::Rarity;
    Row const rows[] = {
        {kBlockAndChain, 99, 1, R::Common},      {kCrumbleHorn, 1024, 1, R::Rare},     {kCube, 0, 1, R::Uncommon},       {kCubeTalisman, 0, 64, R::Uncommon},
        {kEnderBow, 384, 1, R::Uncommon},        {kGlassSword, 1, 1, R::Uncommon},     {kIceBomb, 0, 16, R::Common},      {kIceBow, 384, 1, R::Uncommon},
        {kIceSword, 32, 1, R::Common},           {kShield, 1024, 1, R::Common},        {kLamp, 1024, 1, R::Uncommon},     {kMoonwormQueen, 256, 1, R::Rare},
        {kOreMagnet, 64, 1, R::Common},          {kFan, 1024, 1, R::Rare},             {kSeekerBow, 384, 1, R::Uncommon}, {kPowder, 0, 64, R::Common},
        {kTripleBow, 384, 1, R::Uncommon},
    };
    for (auto const& r : rows) {
        auto s = game::items::make(r.id);
        ctx.require(s.has_value(), std::string("item ") + r.id + " exists");
        ctx.check(maxDamage(*s) == r.durability, std::string(r.id) + " durability " + std::to_string(maxDamage(*s)) + " (TF " + std::to_string(r.durability) + ")");
        ctx.check(s->getMaxStackSize() == r.stack, std::string(r.id) + " stacks to " + std::to_string(s->getMaxStackSize()));
        ctx.check(gf::item::ext::rarityOf(r.id) == r.rarity, std::string(r.id) + " rarity");
    }
    ctx.check(gf::datamaps::get("twilightforest:crumble_horn", "minecraft:stone") != nullptr, "the crumble_horn data map has stone");
    ctx.check(gf::datamaps::get("twilightforest:transformation_powder", "minecraft:pig") != nullptr, "the transformation_powder data map has the pig");
});

// The moonworm queen repair recipe (MoonwormQueenRepairRecipe).
GF_REGISTER_SELFTEST("weapons.moonworm_repair", [](Context& ctx) {
    auto queen = game::items::make(kMoonwormQueen);
    auto berry = game::items::make(kTorchberries);
    ctx.require(queen && berry, "queen and torchberries exist");
    auto damaged = *queen;
    damaged.setDamageValue(200);
    std::vector<ItemStack const*> grid{&damaged, &*berry, &*berry, nullptr};
    ctx.check(moonwormRepairMatches(grid), "a damaged queen and two berries match");
    auto out = moonwormRepairAssemble(grid);
    ctx.check(game::items::idOf(out) == kMoonwormQueen && out.getDamageValue() == 72, "repaired by 128: damage " + std::to_string(out.getDamageValue()));
    std::vector<ItemStack const*> fresh{&*queen, &*berry};
    ctx.check(!moonwormRepairMatches(fresh), "an undamaged queen does not match");
    std::vector<ItemStack const*> two{&damaged, &damaged, &*berry};
    ctx.check(!moonwormRepairMatches(two), "two queens do not match");
    auto stone = game::items::make("minecraft:stone");
    std::vector<ItemStack const*> other{&damaged, &*berry, &*stone};
    ctx.check(!moonwormRepairMatches(other), "anything else does not match");
    std::vector<ItemStack const*> many{&damaged, &*berry, &*berry, &*berry, &*berry};
    ctx.check(moonwormRepairAssemble(many).getDamageValue() == 0, "repair never goes below 0");
});

// Crumble horn: the data map results around the point 3 blocks ahead, the stat and durability.
GF_REGISTER_SELFTEST("weapons.crumble_horn", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto box = co_await ctx.flatArea(800, 12);
    survival(ctx);
    auto at  = ctx.testPos(800);
    auto pos = core::GlobalPos{at.dim, at.x + 1, at.y, at.z + 2};
    place(ctx, pos, Vec3{at.x + 5.5f, at.y + 1.62f, at.z + 2.5f});
    co_await ctx.ticks(2);
    auto& player = ctx.player();
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    auto eye  = eyeOf(player);
    auto look = lookOf(player);
    auto c    = cw::containing({eye.x + look.x * 3.0, eye.y + look.y * 3.0, eye.z + look.z * 3.0});
    // fill the 5x5x5 box with stone bricks (crumble to cracked) and some cobblestone
    int filled = 0;
    for (int x = c.x - 2; x <= c.x + 2; ++x)
        for (int y = c.y - 2; y <= c.y + 2; ++y)
            for (int z = c.z - 2; z <= c.z + 2; ++z) {
                ctx.setBlock(core::GlobalPos{at.dim, x, y, z}, (x + z) % 2 ? "minecraft:stone_bricks" : "minecraft:cobblestone");
                ++filled;
            }
    auto horn = game::items::make(kCrumbleHorn);
    ctx.require(horn.has_value(), "a crumble horn");
    long long statBefore = gf::stats::value(player, "twilightforest:blocks_crumbled");
    int       n          = crumbleAround(player, *horn, 1234);
    ctx.check(n > 0 && n < filled, "the horn crumbled " + std::to_string(n) + " of " + std::to_string(filled) + " (chance 0.2)");
    ctx.check(gf::stats::value(player, "twilightforest:blocks_crumbled") - statBefore == n, "blocks_crumbled counts each");
    ctx.check(horn->getDamageValue() == n, "one durability per crumble: " + std::to_string(horn->getDamageValue()));
    int cracked = 0, gravel = 0, bad = 0;
    for (int x = c.x - 2; x <= c.x + 2; ++x)
        for (int y = c.y - 2; y <= c.y + 2; ++y)
            for (int z = c.z - 2; z <= c.z + 2; ++z) {
                auto id = game::blockIdAt(*region, BlockPos{x, y, z});
                if (id == "minecraft:cracked_stone_bricks") ++cracked;
                else if (id == "minecraft:gravel") ++gravel;
                else if (id != "minecraft:stone_bricks" && id != "minecraft:cobblestone") ++bad;
            }
    ctx.check(cracked + gravel == n && bad == 0, "results follow the map: " + std::to_string(cracked) + " cracked, " + std::to_string(gravel) + " gravel, " + std::to_string(bad) + " other");
    // the same seed gives the same crumbles (deterministic chance)
    for (int x = c.x - 2; x <= c.x + 2; ++x)
        for (int y = c.y - 2; y <= c.y + 2; ++y)
            for (int z = c.z - 2; z <= c.z + 2; ++z) ctx.setBlock(core::GlobalPos{at.dim, x, y, z}, (x + z) % 2 ? "minecraft:stone_bricks" : "minecraft:cobblestone");
    auto horn2 = game::items::make(kCrumbleHorn);
    ctx.check(crumbleAround(player, *horn2, 1234) == n, "seeded: the same count again");
    (void)box;
});

// Transformation powder converts a pig into a boar and a cow into a deer.
GF_REGISTER_SELFTEST("weapons.transformation_powder", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(820, 8);
    auto at  = ctx.testPos(820, 2);
    auto* pig = spawnMob(at, "minecraft:pig", 3.5f, 0.5f);
    ctx.require(pig != nullptr, "a pig");
    auto powder = game::items::make(kPowder, 3);
    ctx.require(powder.has_value(), "powder");
    auto uid = transform(*pig, &*powder, true);
    ctx.check(uid != 0, "the pig transformed");
    auto* boar = world::actorById(uid);
    ctx.check(boar && boar->getTypeName() == "twilightforest:boar", "into twilightforest:boar (" + (boar ? boar->getTypeName() : std::string("none")) + ")");
    ctx.check(powder->mCount == 2, "one powder used");
    co_await ctx.ticks(2);
    if (auto* b = world::actorById(uid)) {
        auto back = transform(*b, nullptr, false);
        auto* p2  = world::actorById(back);
        ctx.check(p2 && p2->getTypeName() == "minecraft:pig", "and the boar back into a pig");
        if (p2) p2->remove();
    }
    auto* stone = spawnMob(at, "minecraft:zombie", 5.5f, 0.5f);
    if (stone) {
        ctx.check(transform(*stone, nullptr, false) == 0, "a zombie has no transformation");
        stone->remove();
    }
});

// The glass sword breaks on its first hit (survival), triggering broke_glass_sword.
GF_REGISTER_SELFTEST("weapons.glass_sword", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(830, 6);
    survival(ctx);
    co_await ctx.ticks(1);
    auto& player = ctx.player();
    auto  sword  = game::items::make(kGlassSword);
    ctx.require(sword.has_value(), "a glass sword");
    auto* victim = spawnMob(ctx.testPos(830, 2), "minecraft:pig", 2.5f);
    ctx.require(victim != nullptr, "a pig");
    int  before = gGlassBreaks;
    auto copy   = *sword;
    copy.mItem->hurtActor(copy, *victim, player);
    ctx.check(gGlassBreaks == before + 1, "the sword shattered");
    ctx.check(game::items::isEmpty(copy) || copy.mCount == 0, "the stack is gone");
    // the infinite variant never breaks
    auto inf = *sword;
    gf::item::setData(inf, kInfiniteGlassKey, true);
    inf.mItem->hurtActor(inf, *victim, player);
    ctx.check(gGlassBreaks == before + 1 && !game::items::isEmpty(inf), "the infinite glass sword survives");
    victim->remove();
});

// Ice bomb terrain: water -> ice, lava -> obsidian, snow layers grow; the zone freezes mobs.
GF_REGISTER_SELFTEST("weapons.ice_bomb", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(840, 10);
    auto  at     = ctx.testPos(840, 2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    // a dent of water, lava and a snow layer around (+2, 0, +2)
    core::GlobalPos c{at.dim, at.x + 3, at.y, at.z};
    ctx.setBlock(core::GlobalPos{at.dim, c.x - 1, c.y - 1, c.z}, "minecraft:water");
    ctx.setBlock(core::GlobalPos{at.dim, c.x + 1, c.y - 1, c.z}, "minecraft:lava");
    ctx.setBlock(core::GlobalPos{at.dim, c.x, c.y, c.z + 1}, "minecraft:snow_layer", {{"height", 2}});
    iceTerrain(*region, Vec3{c.x + 0.5f, c.y + 0.5f, c.z + 0.5f}, 2);
    ctx.check(game::blockIdAt(*region, bp(c, -1, -1)) == "minecraft:ice", "water froze: " + game::blockIdAt(*region, bp(c, -1, -1)));
    ctx.check(game::blockIdAt(*region, bp(c, 1, -1)) == "minecraft:obsidian", "lava became obsidian: " + game::blockIdAt(*region, bp(c, 1, -1)));
    ctx.check(game::intState(region->getBlock(bp(c, 0, 0, 1)), "height").value_or(-1) == 3, "the snow layer grew");
    ctx.check(game::blockIdAt(*region, bp(c, 2, 0, 0)) == "minecraft:snow_layer", "snow on the open ground");
    ctx.check(game::blockIdAt(*region, bp(c, 2, 0, 2)) != "minecraft:snow_layer", "the corners are skipped");
    for (int x = -3; x <= 3; ++x)
        for (int z = -3; z <= 3; ++z) ctx.cleanupBlock(core::GlobalPos{at.dim, c.x + x, c.y, c.z + z});
    // a bomb thrown down next to a pig: the zone hurts it
    auto* pig = spawnMob(at, "minecraft:pig", 7.5f, 0.5f);
    ctx.require(pig != nullptr, "a pig");
    auto  pigId = world::uidOf(*pig);
    float hp    = static_cast<float>(pig->getHealth());
    int   hits  = gIceZoneHits;
    auto* bomb  = world::shootFrom(*region, Vec3{at.x + 6.5f, at.y + 2.0f, at.z + 0.5f}, kThrownIce, Vec3{0, -1, 0}, 0.3F, 0.0F);
    ctx.require(bomb != nullptr, "a thrown ice bomb");
    co_await ctx.ticks(45);
    ctx.check(gIceZoneHits > hits, "the ice zone hit something");
    auto* p = world::actorById(pigId);
    ctx.check(!p || static_cast<float>(p->getHealth()) < hp, "the pig was frozen");
    if (p) p->remove();
});

// The lamp of cinders burns thorns within 4 of the point 2 ahead and ignites mobs.
GF_REGISTER_SELFTEST("weapons.lamp", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(850, 10);
    auto at  = ctx.testPos(850);
    place(ctx, core::GlobalPos{at.dim, at.x + 1, at.y, at.z + 2}, Vec3{at.x + 6.5f, at.y + 1.62f, at.z + 2.5f});
    co_await ctx.ticks(2);
    auto& player = ctx.player();
    auto  eye    = eyeOf(player);
    auto  look   = lookOf(player);
    auto  c      = cw::containing({eye.x + look.x * 2.0, eye.y + look.y * 2.0, eye.z + look.z * 2.0});
    bool  thorns = ctx.setBlock(core::GlobalPos{at.dim, c.x + 2, c.y, c.z}, "twilightforest:brown_thorns");
    ctx.setBlock(core::GlobalPos{at.dim, c.x + 3, c.y, c.z + 1}, "twilightforest:green_thorns");
    ctx.setBlock(core::GlobalPos{at.dim, c.x + 6, c.y, c.z}, "twilightforest:brown_thorns"); // out of range
    auto* pig = spawnMob(core::GlobalPos{at.dim, c.x + 1, at.y, c.z}, "minecraft:pig");
    lampBurn(ctx.player());
    auto* region = ctx.region();
    if (thorns) {
        ctx.check(game::blockIdAt(*region, BlockPos{c.x + 2, c.y, c.z}) == "twilightforest:burnt_thorns", "brown thorns burnt");
        ctx.check(game::blockIdAt(*region, BlockPos{c.x + 3, c.y, c.z + 1}) == "twilightforest:burnt_thorns", "green thorns burnt");
        ctx.check(game::blockIdAt(*region, BlockPos{c.x + 6, c.y, c.z}) == "twilightforest:brown_thorns", "thorns 6 away stay");
    } else {
        ctx.info("twilightforest:brown_thorns is not placeable here (U30's block)");
    }
    ctx.check(pig && pig->isOnFire(), "the pig is on fire");
    if (pig) pig->remove();
});

// Ore magnet: an iron vein beyond the base block is pulled up to it.
GF_REGISTER_SELFTEST("weapons.ore_magnet", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(860, 16);
    auto  at     = ctx.testPos(860, 2);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    int y = at.y + 1;
    // the line: air, stone (the base) at x+3, air, a 3-block iron vein at x+8..x+10
    ctx.setBlock(core::GlobalPos{at.dim, at.x + 3, y, at.z}, "minecraft:stone");
    for (int i = 8; i <= 10; ++i) ctx.setBlock(core::GlobalPos{at.dim, at.x + i, y, at.z}, "minecraft:iron_ore");
    for (int i = 0; i <= 14; ++i) ctx.cleanupBlock(core::GlobalPos{at.dim, at.x + i, y, at.z});
    int moved = magnetLine(*region, BlockPos{at.x, y, at.z}, BlockPos{at.x + 14, y, at.z});
    ctx.check(moved == 3, "three ore blocks moved: " + std::to_string(moved));
    ctx.check(game::blockIdAt(*region, BlockPos{at.x + 3, y, at.z}) == "minecraft:iron_ore", "the ore sits at the base");
    ctx.check(game::blockIdAt(*region, BlockPos{at.x + 8, y, at.z}) == "minecraft:stone", "its old place is stone");
    // a vein larger than 24 moves at most 24
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 3; ++k) ctx.setBlock(core::GlobalPos{at.dim, at.x + 6 + i, y + k, at.z + j}, "minecraft:coal_ore");
    ctx.check(magnetLine(*region, BlockPos{at.x, y, at.z}, BlockPos{at.x + 14, y, at.z}) == 0, "coal is ignored (#ore_magnet/ignored_ores)");
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 3; ++k) ctx.setBlock(core::GlobalPos{at.dim, at.x + 6 + i, y + k, at.z + j}, "minecraft:copper_ore");
    ctx.setBlock(core::GlobalPos{at.dim, at.x + 3, y, at.z}, "minecraft:stone");
    for (int i = -12; i <= 12; ++i)
        for (int k = -6; k <= 6; ++k)
            if (i != 0 || k != 0) ctx.setBlock(core::GlobalPos{at.dim, at.x + 3, y + k + 7, at.z + i}, "minecraft:dirt");
    int big = magnetLine(*region, BlockPos{at.x, y, at.z}, BlockPos{at.x + 14, y, at.z});
    ctx.check(big > 0 && big <= 24, "a big vein moves at most 24: " + std::to_string(big));
});

// The triple bow fires three arrows (two creative-only), costing one arrow and one durability.
GF_REGISTER_SELFTEST("weapons.triple_bow", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(870, 12);
    survival(ctx);
    auto at = ctx.testPos(870, 2);
    place(ctx, at, Vec3{at.x + 20.5f, at.y + 2.0f, at.z + 0.5f});
    co_await ctx.ticks(2);
    auto& player = ctx.player();
    auto& inv    = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
    int arrowSlot = ctx.give("minecraft:arrow", 5);
    ctx.require(arrowSlot >= 0, "arrows");
    auto bow = game::items::make(kTripleBow);
    ctx.require(bow.has_value(), "a triple bow");
    auto  feet = player.getFeetPos();
    AABB  near{Vec3{feet.x - 4, feet.y - 2, feet.z - 4}, Vec3{feet.x + 8, feet.y + 4, feet.z + 4}};
    auto  countArrows = [&] {
        int n = 0;
        for (auto* e : world::actorsIn(*ctx.region(), ctx.dimension(), near))
            if (e && e->getTypeName() == "minecraft:arrow") ++n;
        return n;
    };
    int before = countArrows();
    bowRelease(player, *bow, 20);
    ctx.check(countArrows() - before == 3, "three arrows: " + std::to_string(countArrows() - before));
    int left = 0;
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (game::items::idOf(inv.getItem(i)) == "minecraft:arrow") left += inv.getItem(i).mCount;
    ctx.check(left == 4, "one arrow used: " + std::to_string(left) + " left");
    ctx.check(bow->getDamageValue() == 1, "one durability");
    bowRelease(player, *bow, 1);
    ctx.check(bow->getDamageValue() == 1, "a 1-tick draw (power < 0.1) does not shoot");
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
});

// Punch on the triple bow's vanilla arrows (AbstractArrow#doKnockback on the hit).
GF_REGISTER_SELFTEST("weapons.bow_punch", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(810, 10);
    survival(ctx);
    auto at = ctx.testPos(810, 2);
    place(ctx, at, Vec3{at.x + 6.5f, at.y + 0.7f, at.z + 0.5f});
    co_await ctx.ticks(2);
    auto* sheep = spawnMob(at, "minecraft:sheep", 6.5f, 0.5f);
    ctx.require(sheep != nullptr, "a sheep ahead");
    auto  sid    = world::uidOf(*sheep);
    float startX = sheep->getPosition().x;
    auto& player = ctx.player();
    auto& inv    = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
    ctx.require(ctx.give("minecraft:arrow", 4) >= 0, "arrows");
    auto bow = game::items::make(kTripleBow);
    ctx.require(bow.has_value(), "a triple bow");
    gf::enchant::set(*bow, "minecraft:punch", 2);
    int before = gPunches;
    bowRelease(player, *bow, 20);
    auto feet = player.getFeetPos();
    AABB near{Vec3{feet.x - 2, feet.y - 2, feet.z - 3}, Vec3{feet.x + 4, feet.y + 4, feet.z + 3}};
    int  marked = 0;
    for (auto* e : world::actorsIn(*ctx.region(), ctx.dimension(), near))
        if (e && e->getTypeName() == "minecraft:arrow" && gf::entity::data(*e, "weapons:punch") == 2) ++marked;
    ctx.check(marked == 3, "the three arrows carry Punch II (" + std::to_string(marked) + ")");
    for (int i = 0; i < 30 && gPunches == before; ++i) co_await ctx.ticks(1);
    ctx.check(gPunches > before, "an arrow's hit knocked the sheep back with Punch");
    co_await ctx.ticks(3);
    if (auto* s = world::actorById(sid)) {
        ctx.info("the sheep moved " + std::to_string(s->getPosition().x - startX) + " along the shot");
        s->remove();
    }
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
    for (auto* e : world::actorsIn(*ctx.region(), ctx.dimension(), AABB{Vec3{at.x - 2.f, at.y - 2.f, at.z - 4.f}, Vec3{at.x + 12.f, at.y + 6.f, at.z + 4.f}}))
        if (e && e->getTypeName() == "minecraft:arrow") e->remove();
});

// The ore magnet takes Unbreaking and Mending books (TF: #minecraft:enchantable/durability), never
// the enchanting table's other tool enchantments.
GF_REGISTER_SELFTEST("weapons.ore_magnet_enchants", [](Context& ctx) {
    auto magnet = game::items::make(kOreMagnet);
    ctx.require(magnet.has_value(), "an ore magnet");
    auto can = [&](Enchant::Type t) {
        EnchantmentInstance e;
        e.mEnchantType = t;
        e.mLevel       = 1;
        return EnchantUtils::canEnchant(*magnet, e, false).result != EnchantResultType::Fail;
    };
    ctx.check(can(Enchant::Type::Unbreaking), "Unbreaking applies");
    ctx.check(can(Enchant::Type::Mending), "Mending applies");
    ctx.check(!can(Enchant::Type::Efficiency), "Efficiency does not");
    ctx.check(!can(Enchant::Type::Fortune), "Fortune does not");
    ctx.check(!can(Enchant::Type::Sharpness), "Sharpness does not");
});

// Seeker arrows home in on a mob beside their path; ice arrows chill.
GF_REGISTER_SELFTEST("weapons.seeker_bow", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(880, 24);
    auto  at  = ctx.testPos(880, 2);
    place(ctx, at, Vec3{at.x + 20.5f, at.y + 1.62f, at.z + 0.5f});
    co_await ctx.ticks(2);
    auto* sheep = spawnMob(at, "minecraft:sheep", 12.5f, 3.0f);
    ctx.require(sheep != nullptr, "a sheep beside the line");
    auto  sid = world::uidOf(*sheep);
    float hp  = static_cast<float>(sheep->getHealth());
    auto& player = ctx.player();
    Actor* arrow = gf::projectile::shoot(player, kSeekerArrow, Vec3{1, 0.02f, 0}, 2.5F, 0.0F);
    ctx.require(arrow != nullptr, "a seeker arrow");
    auto aid = world::uidOf(*arrow);
    float minDist = 1e9f;
    for (int i = 0; i < 20; ++i) {
        co_await ctx.ticks(1);
        auto* a = world::actorById(aid);
        auto* s = world::actorById(sid);
        if (a && s) minDist = std::min(minDist, static_cast<float>(std::sqrt(world::distanceSqr(*a, *s))));
    }
    auto* s = world::actorById(sid);
    ctx.check(!s || static_cast<float>(s->getHealth()) < hp || minDist < 1.5f, "the seeker arrow found the sheep (closest " + std::to_string(minDist) + ")");
    if (s) s->remove();
    if (auto* a = world::actorById(aid)) a->remove();
});

// The chain hits for 10 and comes back; the cube annihilates soft blocks and stops at obsidian.
GF_REGISTER_SELFTEST("weapons.chain_and_cube", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(885, 24);
    survival(ctx);
    auto at = ctx.testPos(885, 2);
    place(ctx, at, Vec3{at.x + 10.5f, at.y + 1.62f, at.z + 0.5f});
    co_await ctx.ticks(2);
    auto* golem = spawnMob(at, "minecraft:iron_golem", 4.5f, 0.5f);
    ctx.require(golem != nullptr, "an iron golem ahead");
    auto  gid = world::uidOf(*golem);
    float hp  = static_cast<float>(golem->getHealth());
    auto& player = ctx.player();
    int   slot   = player.getSelectedItemSlot();
    player.getInventory().setItem(slot, *game::items::make(kBlockAndChain));
    auto  stack  = player.getCarriedItem();
    auto  cid    = throwChain(player, stack);
    ctx.require(cid != 0, "the chain was thrown");
    player.setCarriedItem(stack);
    ctx.check(gf::item::getData(player.getCarriedItem(), kThrownKey).is_number_integer(), "the stack names the thrown chain");
    ctx.check(throwChain(player, stack) == 0, "one chain at a time");
    bool hit = false;
    for (int i = 0; i < 60 && !hit; ++i) {
        co_await ctx.ticks(1);
        if (auto* g = world::actorById(gid)) hit = static_cast<float>(g->getHealth()) <= hp - 9.5f;
    }
    ctx.check(hit, "the golem took 10");
    bool gone = false;
    for (int i = 0; i < 200 && !gone; ++i) {
        co_await ctx.ticks(1);
        gone = world::actorById(cid) == nullptr;
    }
    ctx.check(gone, "the chain came back");
    co_await ctx.ticks(2);
    ctx.check(gf::item::getData(ctx.player().getCarriedItem(), kThrownKey).is_null(), "the thrown data cleared");
    if (auto* g = world::actorById(gid)) g->remove();

    // the cube: a dirt wall at +6 with obsidian behind at +8
    for (int z = -1; z <= 1; ++z)
        for (int y = 0; y <= 2; ++y) {
            ctx.setBlock(core::GlobalPos{at.dim, at.x + 6, at.y + y, at.z + z}, "minecraft:dirt");
            ctx.setBlock(core::GlobalPos{at.dim, at.x + 8, at.y + y, at.z + z}, "minecraft:obsidian");
        }
    auto& p2 = ctx.player();
    p2.getInventory().setItem(slot, *game::items::make(kCube));
    int before = gAnnihilated;
    // the real use: the item's use throws the cube, the BP use component keeps it in use
    ctx.require(static_cast<SimulatedPlayer&>(p2).simulateUseItemInSlot(slot), "the cube is used");
    co_await ctx.ticks(1);
    auto thrown = gf::item::getData(ctx.player().getCarriedItem(), kThrownKey);
    ctx.require(thrown.is_number_integer(), "the cube was thrown");
    auto cubeId = thrown.get<std::int64_t>();
    ctx.check(usedItemId(ctx.player()) == kCube, "the player keeps using the cube (the BP use component): '" + usedItemId(ctx.player()) + "'");
    bool backed = false;
    for (int i = 0; i < 240 && !backed; ++i) {
        co_await ctx.ticks(1);
        backed = world::actorById(cubeId) == nullptr;
        if (i == 60) ctx.player().stopUsingItem();
    }
    if (auto* c = world::actorById(cubeId)) {
        auto cp = c->getFeetPos();
        ctx.info("the cube is still at " + std::to_string(cp.x - at.x) + " " + std::to_string(cp.y - at.y) + " " + std::to_string(cp.z - at.z));
        c->remove();
    }
    ctx.check(gAnnihilated > before, "the cube annihilated " + std::to_string(gAnnihilated - before) + " blocks");
    ctx.check(game::blockIdAt(*ctx.region(), BlockPos{at.x + 8, at.y + 1, at.z}) == "minecraft:obsidian", "obsidian (resistance 1200) stays");
    ctx.check(backed, "the cube returned");
    ctx.player().getInventory().setItem(slot, ItemStack::EMPTY_ITEM());
});

// The knightmetal shield blocks in front and parries TF projectiles in its first 40 ticks; the
// feather fan's flag stops the next fall damage.
GF_REGISTER_SELFTEST("weapons.shield_and_fan", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await ctx.flatArea(890, 10);
    survival(ctx);
    auto at = ctx.testPos(890, 2);
    place(ctx, at, Vec3{at.x + 6.5f, at.y + 1.62f, at.z + 0.5f});
    co_await ctx.ticks(2);
    auto& player = ctx.player();
    int sslot = player.getSelectedItemSlot();
    player.getInventory().setItem(sslot, *game::items::make(kShield));
    // raise it: the real use (BP use component, BLOCK animation)
    ctx.require(static_cast<SimulatedPlayer&>(player).simulateUseItemInSlot(sslot), "the shield is used");
    co_await ctx.ticks(6);
    auto& p     = ctx.player();
    int   ticks = 0;
    ctx.check(usedItemId(p) == kShield, "the shield is in use: '" + usedItemId(p) + "'");
    ctx.check(isBlocking(p, &ticks), "the raised knightmetal shield blocks after 5 ticks (" + std::to_string(ticks) + " ticks)");
    auto* zombie = spawnMob(at, "minecraft:iron_golem", 3.5f, 0.5f);
    ctx.require(zombie != nullptr, "a golem in front");
    int  blocks = gShieldBlocks;
    int  dmg0   = p.getCarriedItem().getDamageValue();
    bool hurt   = gf::combat::hurt(p, 6.0F, "minecraft:mob_attack", zombie, zombie);
    ctx.check(!hurt && gShieldBlocks == blocks + 1, "the shield blocked the golem");
    ctx.check(p.getCarriedItem().getDamageValue() - dmg0 == 7, "the shield took 1 + floor(6): " + std::to_string(p.getCarriedItem().getDamageValue() - dmg0));
    // a TF projectile in the parry window
    Actor* shot = gf::projectile::shoot(*zombie, kIceArrow, Vec3{-1, 0, 0}, 1.0F, 0.0F);
    ctx.require(shot != nullptr, "an ice arrow from the zombie");
    int parries = gParries;
    ctx.check(!gf::combat::hurt(p, 3.0F, "minecraft:arrow", zombie, shot), "the arrow did no damage");
    ctx.check(gParries == parries + 1, "it was parried");
    auto v = gf::projectile::ext::velocity(*shot);
    ctx.check(v.x > 0, "and flies back along the look");
    shot->remove();
    zombie->remove();
    p.stopUsingItem();
    p.getInventory().setItem(sslot, ItemStack::EMPTY_ITEM());
    // the feather fan flag cancels a fall
    setFanFalling(p, true);
    float hp = static_cast<float>(p.getHealth());
    gf::combat::hurt(p, 4.0F, "minecraft:fall");
    ctx.check(static_cast<float>(p.getHealth()) >= hp, "no fall damage while feather_fan_falling");
    setFanFalling(p, false);
    co_await ctx.ticks(4);
    // fanning pushes an item along the look
    auto& p3   = ctx.player();
    auto  eye  = eyeOf(p3);
    auto  look = lookOf(p3);
    auto  lv   = ll::service::getLevel();
    auto* item = lv->getSpawner().spawnItem(*ctx.region(), *game::items::make("minecraft:stick"), nullptr, Vec3{eye.x + look.x * 3, eye.y + look.y * 3 - 0.5f, eye.z + look.z * 3}, 0);
    ctx.require(item != nullptr, "a stick ahead");
    int fanned = fanUse(p3);
    ctx.check(fanned >= 1, "the fan pushed something: " + std::to_string(fanned));
    ctx.check(item->getPosDelta().x > 1.0f, "the stick flies along the look");
    item->remove();
});

// The client e2e's showcase (tools/e2e/client/scenarios/weapons.ps1): with a real player in the
// world it fills the hotbar with the 17 items, throws a block and chain and a cube of annihilation
// in front of the player and an ice bomb beside them, and logs a line for each screenshot. Without a
// real player (the server runs) it only checks that it can find none.
GF_REGISTER_SELFTEST("weapons.client_showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
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
    auto uid = world::uidOf(*real);
    // the hotbar and inventory: every weapons item
    char const* ids[] = {kBlockAndChain, kCrumbleHorn, kCube, kCubeTalisman, kEnderBow, kGlassSword, kIceBomb, kIceBow, kIceSword,
                         kShield, kLamp, kMoonwormQueen, kOreMagnet, kFan, kSeekerBow, kPowder, kTripleBow};
    auto& inv = real->getInventory();
    for (int i = 0; i < 17 && i < inv.getContainerSize(); ++i)
        if (auto s = game::items::make(ids[i])) inv.setItem(i, *s);
    real->sendInventory(false);
    co_await ctx.ticks(20);
    ctx.info("items ready");
    // the chain in flight, then the cube
    real = world::playerById(uid);
    if (!real) co_return;
    auto chainStack = *game::items::make(kBlockAndChain);
    auto chain      = throwChain(*real, chainStack);
    ctx.check(chain != 0, "a chain thrown");
    co_await ctx.ticks(6);
    ctx.info("chain ready");
    co_await ctx.ticks(60);
    real = world::playerById(uid);
    if (!real) co_return;
    auto cubeStack = *game::items::make(kCube);
    auto cube      = throwCube(*real, cubeStack);
    ctx.check(cube != 0, "a cube thrown");
    co_await ctx.ticks(8);
    ctx.info("cube ready");
    co_await ctx.ticks(40);
    real = world::playerById(uid);
    if (!real) co_return;
    auto look = lookOf(*real);
    auto eye  = eyeOf(*real);
    world::shootFrom(real->getDimensionBlockSource(), Vec3{eye.x + look.x * 4, eye.y + 2.0f, eye.z + look.z * 4}, kThrownIce, Vec3{0, -1, 0}, 0.2F, 0.0F);
    co_await ctx.ticks(30);
    ctx.info("ice bomb ready");
    co_await ctx.ticks(80);
});

} // namespace tests
} // namespace gf::feature::weapons

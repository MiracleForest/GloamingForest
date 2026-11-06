// GloamingForest - U22 (lichdecor): selftests of the sinister spawner command, the skull chest
// lock, the wrought iron fence, the chiseled canopy bookshelf spawner, the magic painting and the
// recipes (dx 2250..2279, dz 0..4; the room goldens use 2280.., RoomTests.cpp).

#include <string>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "core/GlobalPos.h"
#include "feature/common/Interactions.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/inventory/Inventory.h"
#include "feature/lichdecor/Lichdecor.h"
#include "feature/lichdecor/TestAccess.h"
#include "feature/recipes/Recipes.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace gf::feature::lichdecor {

namespace {

namespace ia = gf::interactions;
namespace st = gf::selftest;

std::string coords(core::GlobalPos const& p) { return std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z); }

st::CommandResult tf(st::Context& ctx, std::string const& rest) { return st::runServerCommand("twilightforest " + rest, ctx.dimension()); }

/// A use-on as the hooks dispatch it.
ia::Result useOn(st::Context& ctx, core::GlobalPos const& pos, std::string const& itemId, int face, ia::Vec hit, bool sneaking) {
    auto&       player = ctx.player();
    ItemStack   stack  = itemId.empty() ? game::items::empty() : game::items::make(itemId, 1).value_or(game::items::empty());
    auto*       region = ctx.region();
    Block const& block = region->getBlock(game::blockPos(pos));
    std::string blockId = game::blockId(block);
    ia::UseOnContext c{player, stack, itemId, block, blockId, pos, face, hit, sneaking, true};
    return ia::dispatchUseOn(c);
}

ll::coro::CoroTask<> sinisterCommand(st::Context& ctx) {
    auto pos = ctx.testPos(2250);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "twilightforest:sinister_spawner"), "sinister spawner placed");
    auto other = ctx.testPos(2251);
    ctx.require(ctx.setBlock(other, "minecraft:stone"), "stone placed");
    auto at = coords(pos), stone = coords(other);
    auto expect = [&](std::string const& cmd, int count) {
        auto r = tf(ctx, cmd);
        ctx.check(r.successCount == count, "/twilightforest " + cmd + " returns " + std::to_string(count) + " (got " + std::to_string(r.successCount) + ": " + r.messages + ")");
    };
    expect("sinister_spawner add_particle minecraft:smoke " + at, 1);
    expect("sinister_spawner add_particle minecraft:smoke " + at, 0);
    expect("sinister_spawner remove_particle minecraft:smoke " + at, 1);
    expect("sinister_spawner remove_particle minecraft:smoke " + at, 0);
    expect("sinister_spawner set_loot twilightforest:blocks/ominous_spawner_drops " + at, 1);
    expect("sinister_spawner set_loot twilightforest:blocks/ominous_spawner_drops " + at, 0);
    ctx.check(gf::boss::ext::sinisterLootTable(*ctx.region(), game::blockPos(pos)) == "twilightforest:blocks/ominous_spawner_drops", "the loot key is in the spawner's data");
    expect("sinister_spawner clear_loot " + at, 1);
    expect("sinister_spawner clear_loot " + at, 0);
    // not a sinister spawner: 0 everywhere
    expect("sinister_spawner add_particle minecraft:smoke " + stone, 0);
    expect("sinister_spawner remove_particle minecraft:smoke " + stone, 0);
    expect("sinister_spawner set_loot twilightforest:blocks/ominous_spawner_drops " + stone, 0);
    expect("sinister_spawner clear_loot " + stone, 0);
    // argument errors fail the command and change nothing
    auto bad = tf(ctx, "sinister_spawner add_particle twilightforest:no_such_particle " + at);
    ctx.check(!bad.ok && bad.messages.find("Unknown particle: twilightforest:no_such_particle") != std::string::npos, "an unknown particle fails: " + bad.messages);
    auto badLoot = tf(ctx, "sinister_spawner set_loot twilightforest:no_such_table " + at);
    ctx.check(!badLoot.ok && badLoot.messages.find("Failed to parse structure: Failed to get element ResourceKey[minecraft:loot_table / twilightforest:no_such_table]") != std::string::npos,
              "an unknown loot table fails: " + badLoot.messages);
    auto s = gf::boss::ext::sinisterSpawner(*ctx.region(), game::blockPos(pos));
    ctx.check(s && s->particles.empty() && s->lootTable.empty(), "the failed commands changed nothing");
    // never-equal options: each add adds an entry, remove never matches
    std::string dust = "\"minecraft:dust{color:[1.0f,0.0f,0.0f],scale:1.0f}\"";
    expect("sinister_spawner add_particle " + dust + " " + at, 1);
    expect("sinister_spawner add_particle " + dust + " " + at, 1);
    expect("sinister_spawner remove_particle " + dust + " " + at, 0);
    // no 'spawner' alias
    auto alias = tf(ctx, "spawner add_particle minecraft:smoke " + at);
    ctx.check(!alias.ok, "/twilightforest spawner is not registered");
}

ll::coro::CoroTask<> skullChestLock(st::Context& ctx) {
    auto pos = ctx.testPos(2255);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "twilightforest:skull_chest"), "skull chest placed");
    auto bp = game::blockPos(pos);
    setSkullChestOwner(*ctx.region(), bp, Json{{"name", "SomeoneElse"}, {"uuid", "00000000-0000-0000-0000-000000000001"}});
    bool op = static_cast<int>(ctx.player().getCommandPermissionLevel()) >= 3;
    auto r  = useOn(ctx, pos, "", 1, {0.5, 1.0, 0.5}, false);
    if (op) ctx.info("the test player is an operator (level 3+): it opens any skull chest");
    else ctx.check(r == ia::Result::Deny, "another player's skull chest stays shut");
    setSkullChestOwner(*ctx.region(), bp, Json{{"name", ctx.player().getRealName()}, {"uuid", ctx.player().getUuid().asString()}});
    r = useOn(ctx, pos, "", 1, {0.5, 1.0, 0.5}, false);
    ctx.check(r != ia::Result::Deny, "the owner's skull chest opens");
    setSkullChestOwner(*ctx.region(), bp, nullptr);
    ctx.require(ctx.setBlock(pos, "twilightforest:skull_chest", Json{{"twilightforest:multilogged", "obsidian"}}), "obsidian-logged skull chest");
    r = useOn(ctx, pos, "", 1, {0.5, 1.0, 0.5}, false);
    ctx.check(r == ia::Result::Deny, "a chest filled by a block does not open");
}

ll::coro::CoroTask<> fenceFinial(st::Context& ctx) {
    auto base = ctx.testPos(2258, 1);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto* region = ctx.region();
    BlockPos b   = game::blockPos(base);
    // a stone column east of a two-fence column
    core::GlobalPos lower = base, upper = base, stoneLow = base, stoneHigh = base;
    upper.y += 1;
    stoneLow.x += 1;
    stoneHigh.x += 1;
    stoneHigh.y += 1;
    ctx.require(ctx.setBlock(stoneLow, "minecraft:stone") && ctx.setBlock(stoneHigh, "minecraft:stone"), "stone column");
    ctx.require(ctx.setBlock(lower, "twilightforest:wrought_iron_fence") && ctx.setBlock(upper, "twilightforest:wrought_iron_fence"), "fences placed");
    updateFenceForTest(*region, b);
    updateFenceForTest(*region, BlockPos{b.x, b.y + 1, b.z});
    updateFenceForTest(*region, b);
    auto const& lowB = region->getBlock(b);
    auto const& upB  = region->getBlock(BlockPos{b.x, b.y + 1, b.z});
    ctx.check(state(lowB, "east_fence") == "bottom", "the lower fence's east side continues up (" + state(lowB, "east_fence") + ")");
    ctx.check(state(upB, "east_fence") == "top", "the upper fence's east side ends the panel (" + state(upB, "east_fence") + ")");
    ctx.check(state(upB, "west_fence") == "none", "no connection to the open west side");
    ctx.check(state(upB, "post") == "post", "an end of a panel is a post (" + state(upB, "post") + ")");
    // the finial: sneak-use with the fence item on the top fence
    auto r = useOn(ctx, upper, "twilightforest:wrought_iron_fence", 1, {0.5, 1.0, 0.5}, true);
    ctx.check(r == ia::Result::Consume, "the sneak-use is taken");
    ctx.check(state(region->getBlock(BlockPos{b.x, b.y + 1, b.z}), "post") == "capped", "the finial is placed");
    r = useOn(ctx, upper, "twilightforest:wrought_iron_fence", 1, {0.5, 1.0, 0.5}, true);
    ctx.check(state(region->getBlock(BlockPos{b.x, b.y + 1, b.z}), "post") == "post", "the finial is removed again");
    // the lower fence (a fence above it): no finial
    r = useOn(ctx, lower, "twilightforest:wrought_iron_fence", 1, {0.5, 1.0, 0.5}, true);
    ctx.check(r == ia::Result::Pass && state(region->getBlock(b), "post") != "capped", "only the top fence of a column takes a finial");
}

ll::coro::CoroTask<> bookshelfSpawner(st::Context& ctx) {
    auto pos = ctx.testPos(2262, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    // a plain shelf takes a book through the front face
    ctx.require(ctx.setBlock(pos, "twilightforest:chiseled_canopy_bookshelf", Json{{"twilightforest:facing", "north"}}), "shelf placed");
    auto r = useOn(ctx, pos, "minecraft:book", 2, {0.9, 0.75, 0.0}, false);
    ctx.check(r == ia::Result::Consume, "a book goes into the shelf");
    ctx.check(boolState(ctx.region()->getBlock(game::blockPos(pos)), "slot_0_occupied"), "slot 0 (top left seen from the front) is occupied");
    // a spawner shelf refuses interaction and spawns a Death Tome from its book
    ctx.require(ctx.setBlock(pos, "twilightforest:chiseled_canopy_bookshelf", Json{{"twilightforest:facing", "north"}, {"twilightforest:spawner", true}}), "spawner shelf");
    auto h = gf::inventory::find(*ctx.region(), game::blockPos(pos));
    ctx.require(h.has_value() && h->set, "the shelf is an inventory block");
    if (auto book = game::items::make("minecraft:book", 1)) h->set(pos, 3, *book);
    ctx.check(useOn(ctx, pos, "", 2, {0.5, 0.25, 0.0}, false) == ia::Result::Deny, "a spawner shelf refuses interaction");
    auto lvl = ll::service::getLevel();
    if (lvl && static_cast<int>(lvl->getDifficulty()) == 0) {
        ctx.info("peaceful: the Death Tome spawn is not checked");
        co_return;
    }
    (void)ctx.player(); // the player stands next to the shelf (ensureLoaded)
    for (int i = 0; i < 25; ++i) tickShelfForTest(pos);
    auto const& now = ctx.region()->getBlock(game::blockPos(pos));
    ctx.check(!boolState(now, "slot_3_occupied"), "the book became a Death Tome");
    ctx.check(!boolState(now, "spawner"), "an empty spawner shelf stops being a spawner");
}

ll::coro::CoroTask<> magicPainting(st::Context& ctx) {
    auto base = ctx.testPos(2266, 0);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    // a 4 x 3 stone wall along x at z, the picture on its south side (z + 1)
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = 0; dy <= 3; ++dy) {
            auto p = base;
            p.x += dx;
            p.y += dy;
            ctx.setBlock(p, "minecraft:stone");
        }
    BlockPos at{base.x, base.y + 1, base.z + 1};
    Actor*   painting = spawnMagicPaintingForTest(*ctx.region(), at, 3, "twilightforest:darkness");
    ctx.require(painting != nullptr, "a 64 x 32 painting hangs on a 5 x 4 wall");
    ctx.check(spawnMagicPaintingForTest(*ctx.region(), BlockPos{base.x + 10, base.y + 1, base.z + 1}, 3, "twilightforest:darkness") == nullptr,
              "no painting without a wall");
    auto id = painting->getOrCreateUniqueID();
    // the wall goes: the painting drops within 100 ticks
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = 0; dy <= 3; ++dy) {
            auto p = base;
            p.x += dx;
            p.y += dy;
            ctx.setBlock(p, "minecraft:air");
        }
    co_await ctx.ticks(110);
    auto lvl = ll::service::getLevel();
    ctx.check(!lvl || lvl->fetchEntity(id, false) == nullptr, "a painting without its wall breaks");
}

void recipes(st::Context& ctx) {
    auto expect = [&](char const* id, char const* result, int count) {
        auto const* r = gf::recipes::byId(id);
        ctx.check(r != nullptr, std::string("recipe ") + id + " is loaded");
        if (r) ctx.check(r->resultItem == result && r->resultCount == count, std::string(id) + " makes " + std::to_string(count) + " " + result);
    };
    expect("twilightforest:candelabra", "twilightforest:candelabra", 2);
    expect("twilightforest:wrought_iron_fence", "twilightforest:wrought_iron_fence", 3);
    for (char const* to : {"arcs", "curves", "lines"}) {
        std::string out = std::string("twilightforest:terrorcotta_") + to;
        expect(("twilightforest:stonecutting/stone/terrorcotta_" + std::string(to)).c_str(), out.c_str(), 1);
        for (char const* from : {"arcs", "curves", "lines"}) {
            if (std::string(from) == to) continue;
            expect(("twilightforest:stonecutting/terrorcotta_" + std::string(from) + "/terrorcotta_" + to).c_str(), out.c_str(), 1);
        }
    }
}

} // namespace

} // namespace gf::feature::lichdecor

namespace ld = gf::feature::lichdecor;
GF_REGISTER_SELFTEST("lichdecor.sinister_spawner_command", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await ld::sinisterCommand(ctx); });
GF_REGISTER_SELFTEST("lichdecor.skull_chest_lock", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await ld::skullChestLock(ctx); });
GF_REGISTER_SELFTEST("lichdecor.fence_finial", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await ld::fenceFinial(ctx); });
GF_REGISTER_SELFTEST("lichdecor.bookshelf_spawner", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await ld::bookshelfSpawner(ctx); });
GF_REGISTER_SELFTEST("lichdecor.magic_painting", [](gf::selftest::Context& ctx) -> ll::coro::CoroTask<> { co_await ld::magicPainting(ctx); });
GF_REGISTER_SELFTEST("lichdecor.recipes", [](gf::selftest::Context& ctx) { ld::recipes(ctx); });

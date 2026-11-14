// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
//
// F10 selftests: the side rules of container blocks (feature/ext/combat/InventoryFaces.h) as the
// game's hoppers meet them, and the loot table of a decorated pot (upstream's camp pots,
// TemplateMarkerHandlers#tentPot: LootMarkerHandler(DECORATED_POT, CAMP_POT)).

#include <optional>
#include <string>
#include <vector>

#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainer.h"

#include "feature/ext/combat/InventoryFaces.h"
#include "feature/inventory/InventoryInternal.h"
#include "feature/loot/LootInternal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace items = gf::game::items;

constexpr char kChest[] = "gloamingforest:gloaming_oak_chest";

Container* hopperAt(BlockSource& region, BlockPos const& pos) {
    auto* a = region.getBlockEntity(pos);
    if (!a || a->getType() != BlockActorType::Hopper) return nullptr;
    return static_cast<Container*>(static_cast<HopperBlockActor*>(a));
}

/// Restores the side rules a test replaced.
struct RulesScope {
    std::string                                       id;
    std::optional<gf::inventory::ext::FaceRules>      previous;
    explicit RulesScope(std::string blockId) : id(std::move(blockId)), previous(gf::inventory::ext::faceRules(id)) {}
    ~RulesScope() {
        if (previous) gf::inventory::ext::setFaceRules(id, *previous);
        else gf::inventory::ext::clearFaceRules(id);
    }
};

} // namespace

// WorldlyContainer through the native backend: a hopper on top of a chest whose rules open only
// slot 5 through the top fills slot 5; one under it may take only from slot 5 downwards
GF_REGISTER_SELFTEST("combat.inventory_faces", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace ie = gf::inventory::ext;
    if (!gf::game::blockWith(kChest)) {
        ctx.info("no upstream chests in this build: skipped");
        co_return;
    }
    // the pure rule first
    {
        RulesScope scope("gf_test:faces");
        ie::setFaceRules("gf_test:faces", {
            .slotsForFace        = [](auto const&, int face) { return face == 0 ? std::vector<int>{2, 1} : face == 1 ? std::vector<int>{0} : std::vector<int>{1}; },
            .canTakeThroughFace = [](auto const&, int slot, ItemStack const& s, int) { return slot != 1 || items::idOf(s) == "minecraft:bucket"; },
        });
        auto stick = *items::make("minecraft:stick", 1);
        auto bucket = *items::make("minecraft:bucket", 1);
        gf::core::GlobalPos p{0, 0, 0, 0};
        ctx.check(ie::faceAllows("gf_test:faces", p, 0, stick, 1, true) && !ie::faceAllows("gf_test:faces", p, 1, stick, 1, true), "UP reaches the input slot only");
        ctx.check(ie::faceAllows("gf_test:faces", p, 1, stick, 4, true) && !ie::faceAllows("gf_test:faces", p, 0, stick, 4, true), "a side reaches the fuel slot only");
        ctx.check(ie::faceAllows("gf_test:faces", p, 2, stick, 0, false), "DOWN takes the result");
        ctx.check(!ie::faceAllows("gf_test:faces", p, 1, stick, 0, false) && ie::faceAllows("gf_test:faces", p, 1, bucket, 0, false), "DOWN takes only a bucket from the fuel slot");
        ctx.check(ie::faceAllows("gf_test:faces", p, 7, stick, -1, true), "no side: the slot rules alone");
        ctx.check(ie::faceAllows("gf_test:other", p, 7, stick, 1, true), "a block without rules: every slot");
    }
    // the game's hoppers
    auto box = co_await ctx.flatArea(6082, 5);
    auto c   = ctx.testPos(6082, 2).offset(0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(c), "test area loaded");
    auto& region = *ctx.region();
    RulesScope       scope(kChest);
    std::vector<int> faces;
    ie::setFaceRules(kChest, {
        .slotsForFace        = [](auto const&, int face) { return face == 1 ? std::vector<int>{5} : std::vector<int>{3}; },
        .canPlaceThroughFace = [&faces](auto const&, int, ItemStack const&, int face) {
            faces.push_back(face);
            return true;
        },
    });
    ctx.require(ctx.setBlock(c, kChest), "a gloaming oak chest");
    co_await ctx.ticks(1);
    auto* chest = gf::inventory::nativeContainer(region, gf::game::blockPos(c));
    ctx.require(chest != nullptr, "the chest's native container");
    auto top = c.offset(0, 1, 0);
    ctx.require(ctx.setBlock(top, "minecraft:hopper", {{"facing_direction", 0}}), "a hopper on the chest, facing down");
    co_await ctx.ticks(1);
    auto* hopper = hopperAt(region, gf::game::blockPos(top));
    ctx.require(hopper != nullptr, "the hopper's container");
    hopper->setItem(0, *items::make("minecraft:stick", 1));
    bool moved = false;
    for (int i = 0; i < 60 && !moved; ++i) {
        co_await ctx.ticks(1);
        chest  = gf::inventory::nativeContainer(region, gf::game::blockPos(c));
        moved  = chest && !items::isEmpty(chest->getItem(5));
        if (chest && !items::isEmpty(chest->getItem(0))) break; // the rules were not asked
    }
    std::string seen;
    for (int f : faces) seen += std::to_string(f) + " ";
    ctx.info("faces the hopper asked through: " + seen);
    ctx.check(moved, "the hopper on top filled slot 5 (the UP face's only slot)");
    ctx.check(!faces.empty() && faces.front() == 1, "the hopper on top pushes through face 1 (UP): " + seen);
    if (chest)
        for (int i = 0; i < chest->getContainerSize(); ++i) chest->setItem(i, ItemStack{});
    if (auto* h = hopperAt(region, gf::game::blockPos(top))) h->setItem(0, ItemStack{});
    ctx.cleanupBlock(top);
    ctx.cleanupBlock(c);
    (void)box;
});

// A decorated pot with a loot table (DecoratedPotBlockEntity is a RandomizableContainer): the
// table is kept on the pot and rolled into its one slot when it is first used or broken
GF_REGISTER_SELFTEST("combat.loot_decorated_pot", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace loot = gf::loot;
    loot::registerTable("gf_test:combat/pot", nlohmann::json::parse(R"({"type":"minecraft:chest","pools":[
        {"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:emerald","functions":[{"function":"minecraft:set_count","count":3}]}]}]})"));
    struct Unregister {
        ~Unregister() { gf::loot::registerTable("gf_test:combat/pot", nullptr); }
    } unregister;
    auto pos = ctx.testPos(6080);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* region = ctx.region();
    auto  bp     = gf::game::blockPos(pos);
    ctx.cleanupBlock(pos);
    ctx.require(loot::placeContainer(*region, bp, "minecraft:decorated_pot", 2, "gf_test:combat/pot", 11), "a decorated pot with a loot table");
    ctx.check(loot::pendingVanilla(*region, bp).has_value(), "the pot keeps the table until it is used");
    ctx.require(loot::unpackVanilla(*region, bp, &ctx.player()), "using it unpacks the table");
    auto* actor = region->getBlockEntity(bp);
    auto* c     = actor && actor->getType() == BlockActorType::DecoratedPot
                      ? static_cast<Container*>(static_cast<RandomizableBlockActorContainer*>(actor))
                      : nullptr;
    ctx.require(c != nullptr, "the pot's container");
    ctx.check(items::idOf(c->getItem(0)) == "minecraft:emerald" && items::countOf(c->getItem(0)) == 3,
              "the pot holds the rolled stack (" + items::idOf(c->getItem(0)) + " x" + std::to_string(items::countOf(c->getItem(0))) + ")");
    ctx.check(!loot::pendingVanilla(*region, bp).has_value(), "the table is rolled once");
    c->setItem(0, ItemStack{});
});

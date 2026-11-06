// U5 (building): selftests building.* (dx 500..599, rule R12).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/DispenserBlock.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/DispenserBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/boss/Boss.h"
#include "feature/building/Building.h"
#include "feature/common/Interactions.h"
#include "feature/ext/item/Item.h"
#include "feature/recipes/Recipes.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Main.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace b  = gf::feature::building;
namespace ia = gf::interactions;
using Facing = ScriptModuleMinecraft::ScriptFacing;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

std::string st(Context& ctx, GlobalPos const& p, std::string const& state) {
    auto* region = ctx.region();
    if (!region) return {};
    auto const* blk = b::blockAt(*region, gf::game::blockPos(p));
    return blk ? b::stateOf(*blk, "twilightforest:" + state) : std::string();
}

SimulatedPlayer& sim(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

constexpr int kClickGap = 8;

/// Stands the player two blocks off face `face` of `p` (south of it for top / bottom), hotbar 0 =
/// `itemId`, looking at `look` (default: the block).
void stand(Context& ctx, GlobalPos const& p, int face, std::string const& itemId, std::optional<Vec3> look = std::nullopt, std::optional<Vec3> standAt = std::nullopt) {
    auto& sp = sim(ctx);
    Vec3  pos{p.x + 0.5f, float(p.y), p.z + 0.5f};
    if (face >= 2) {
        pos.x += b::kDx[face] * 2.0f;
        pos.z += b::kDz[face] * 2.0f;
    } else {
        pos.z += 2.0f;
        if (face == b::Up) pos.y += 1.0f;
    }
    sp.teleport(standAt ? *standAt : pos, sp.getDimensionId());
    if (look) sp.simulateLookAt(*look, sim::LookDuration::Instant);
    else sp.simulateLookAt(gf::game::blockPos(p), sim::LookDuration::Instant);
    if (itemId.empty()) sp.simulateSetItem(ItemStack::EMPTY_ITEM(), true, 0);
    else if (auto item = gf::game::items::make(itemId, 16)) sp.simulateSetItem(*item, true, 0);
}

void useOn(Context& ctx, GlobalPos const& p, Facing face, Vec3 hit) {
    auto&    sp   = sim(ctx);
    BlockPos bp   = gf::game::blockPos(p);
    auto&    held = const_cast<ItemStack&>(sp.getSelectedItem());
    sp.mGameMode->useItemOn(held, bp, static_cast<uchar>(face), Vec3{bp.x + hit.x, bp.y + hit.y, bp.z + hit.z}, HandSlot::Mainhand, nullptr, true);
}

/// Places `itemId` against face `face` of `against`; the position the game placed it at.
ll::coro::CoroTask<std::optional<GlobalPos>> place(Context& ctx, GlobalPos against, Facing face, Vec3 hit, std::string itemId, std::optional<Vec3> look = std::nullopt, std::optional<Vec3> standAt = std::nullopt) {
    stand(ctx, against, static_cast<int>(face), itemId, look, standAt);
    co_await ctx.ticks(kClickGap);
    std::optional<GlobalPos> placed;
    auto id = ia::onPlace({}, [&placed](ia::PlaceContext const& c) { placed = c.pos; }, ia::priority::Highest + 1000);
    useOn(ctx, against, face, hit);
    ia::remove(id);
    co_return placed;
}

int itemsNear(Context& ctx, GlobalPos const& p, std::string const& id) {
    auto* region = ctx.region();
    if (!region) return 0;
    int  n = 0;
    AABB box{Vec3{p.x - 2.f, p.y - 2.f, p.z - 2.f}, Vec3{p.x + 3.f, p.y + 3.f, p.z + 3.f}};
    for (Actor* a : region->getEntities(box, 0.0f)) {
        if (!a || a->getTypeName() != "minecraft:item") continue;
        auto const& stack = static_cast<ItemActor*>(a)->item();
        if (gf::game::items::idOf(stack) == id) n += stack.mCount;
    }
    return n;
}

void clearItems(Context& ctx, GlobalPos const& p) {
    auto* region = ctx.region();
    if (!region) return;
    AABB box{Vec3{p.x - 3.f, p.y - 3.f, p.z - 3.f}, Vec3{p.x + 4.f, p.y + 4.f, p.z + 4.f}};
    for (Actor* a : region->getEntities(box, 0.0f))
        if (a && a->getTypeName() == "minecraft:item") a->remove();
}

// ---- data ---------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("building.blocks", [](Context& ctx) {
    // every block of the unit is registered; hardness / resistance spot checks (Appendix A)
    static char const* const ids[]{
        "nagastone_head", "nagastone", "spiral_bricks", "etched_nagastone", "mossy_etched_nagastone", "cracked_etched_nagastone",
        "nagastone_pillar", "mossy_nagastone_pillar", "cracked_nagastone_pillar", "nagastone_stairs_left", "nagastone_stairs_right",
        "mossy_nagastone_stairs_left", "mossy_nagastone_stairs_right", "cracked_nagastone_stairs_left", "cracked_nagastone_stairs_right",
        "twisted_stone", "twisted_stone_pillar", "bold_stone_pillar", "mazestone", "mazestone_brick", "cracked_mazestone",
        "mossy_mazestone", "cut_mazestone", "decorative_mazestone", "mazestone_mosaic", "mazestone_border", "underbrick",
        "mossy_underbrick", "cracked_underbrick", "underbrick_floor", "towerwood", "encased_towerwood", "cracked_towerwood",
        "mossy_towerwood", "deadrock", "cracked_deadrock", "weathered_deadrock", "castle_brick", "worn_castle_brick",
        "cracked_castle_brick", "mossy_castle_brick", "castle_roof_tile", "thick_castle_brick", "bold_castle_brick_tile",
        "bold_castle_brick_pillar", "encased_castle_brick_tile", "encased_castle_brick_pillar", "castle_brick_stairs",
        "worn_castle_brick_stairs", "cracked_castle_brick_stairs", "mossy_castle_brick_stairs", "encased_castle_brick_stairs",
        "bold_castle_brick_stairs", "pink_castle_rune_brick", "yellow_castle_rune_brick", "blue_castle_rune_brick",
        "violet_castle_rune_brick", "knightmetal_block", "ironwood_block", "fiery_block", "steeleaf_block", "arctic_fur_block",
        "carminite_block", "canopy_window", "canopy_window_pane", "iron_ladder", "rope", "twilight_portal_miniature_structure",
        "naga_courtyard_miniature_structure", "lich_tower_miniature_structure", "minotaur_labyrinth_miniature_structure",
        "dark_tower_miniature_structure"};
    int missing = 0;
    for (auto const* id : ids)
        if (!gf::game::blockWith(std::string("twilightforest:") + id)) {
            ++missing;
            ctx.check(false, std::string("block registered: ") + id);
        }
    for (auto const& boss : b::trophyBosses()) {
        ctx.check(gf::game::blockWith("twilightforest:" + boss + "_trophy") && gf::game::blockWith("twilightforest:" + boss + "_wall_trophy"), "trophy blocks of " + boss);
        ctx.check(gf::game::items::make("twilightforest:" + boss + "_trophy").has_value(), "trophy item of " + boss);
        ctx.check(gf::game::items::make("twilightforest:" + boss + "_banner_pattern").has_value(), "banner pattern item of " + boss);
    }
    for (auto const* boss : {"naga", "lich", "hydra", "ur_ghast", "knight_phantom", "snow_queen", "minoshroom", "alpha_yeti", "final_boss"}) {
        auto const* sp = gf::game::blockWith(std::string("twilightforest:") + boss + "_boss_spawner");
        ctx.check(sp != nullptr, std::string("boss spawner of ") + boss);
        // strength(-1.0, 3600000.8): unbreakable
        if (sp) ctx.check(sp->getBlockType().getDestroySpeed() < 0, std::string("the boss spawner of ") + boss + " is unbreakable");
    }
    ctx.info("building blocks registered: " + std::to_string(std::size(ids) - missing) + " of " + std::to_string(std::size(ids)));
});

// every block of Appendix A: TF's hardness, explosion resistance, mining tools, tier and sound
struct BlockRow {
    char const* id;
    float       hardness, resistance;
    char const* tools; ///< "|"-joined mineable/* tags
    char const* tier;
    char const* javaSound;
    char const* sound; ///< the Bedrock blocks.json sound F1 maps the Java SoundType to
};
BlockRow const kBlockRows[]{
#include "feature/building/BlockTable.inc"
};

// every stonecutting recipe of the unit (Appendix A): input -> output x count
struct Cut {
    char const* input;
    char const* output;
    int         count;
};
Cut const kCuts[]{
#include "feature/building/StonecutTable.inc"
};

/// The shipped resource pack's blocks.json (the mod's resource_packs/GloamingForest_RP).
nlohmann::json shippedBlocksJson() {
    auto          file = gf::Mod::getInstance().getSelf().getModDir() / "resource_packs" / "GloamingForest_RP" / "blocks.json";
    std::ifstream in(file);
    if (!in) return {};
    return nlohmann::json::parse(in, nullptr, false, true);
}

GF_REGISTER_SELFTEST("building.block_properties", [](Context& ctx) {
    auto const& rows   = kBlockRows;
    auto const  blocks = shippedBlocksJson();
    ctx.require(blocks.is_object(), "the resource pack's blocks.json");
    // Bedrock keeps the explosion resistance in its own units: scale by vanilla stone (Java 6.0)
    auto const* stone = gf::game::blockWith("minecraft:stone");
    ctx.require(stone != nullptr, "minecraft:stone");
    float unit = stone->getBlockType().getExplosionResistance() / 6.0f;
    int   good = 0, bad = 0;
    for (auto const& r : rows) {
        std::string id  = std::string("twilightforest:") + r.id;
        auto const* blk = gf::game::blockWith(id);
        if (!blk) {
            ctx.check(false, id + " registered");
            ++bad;
            continue;
        }
        auto const& type = blk->getBlockType();
        bool        ok   = true;
        float       h    = type.getDestroySpeed();
        // strength(-1, ...): unbreakable (Bedrock: a negative destroy time)
        if (r.hardness < 0 ? h >= 0 : std::abs(h - r.hardness) > 0.01f) {
            ctx.check(false, id + " hardness " + std::to_string(r.hardness) + " (got " + std::to_string(h) + ")");
            ok = false;
        }
        float res  = type.getExplosionResistance() / unit;
        bool  huge = r.resistance >= 1000.0f; // Bedrock caps the unbreakable ones; only "blast proof" matters
        if (huge ? res < 1000.0f : std::abs(res - r.resistance) > std::max(0.01f, r.resistance * 0.001f)) {
            ctx.check(false, id + " explosion resistance " + std::to_string(r.resistance) + " (got " + std::to_string(res) + ")");
            ok = false;
        }
        std::string tools = std::string("|") + r.tools + "|";
        for (std::string tool : {"pickaxe", "axe", "shovel", "hoe"}) {
            bool want = tools.find("|" + tool + "|") != std::string::npos;
            bool got  = type.hasTag(HashedString("minecraft:is_" + tool + "_item_destructible"));
            if (want != got) {
                ctx.check(false, id + (want ? " is mined with the " : " is not mined with the ") + tool);
                ok = false;
            }
        }
        for (std::string tier : {"stone", "iron", "diamond"}) {
            bool want = tier == r.tier;
            bool got  = type.hasTag(HashedString("minecraft:" + tier + "_tier_destructible"));
            if (want != got) {
                ctx.check(false, id + (want ? " needs " : " does not need ") + tier + " tier");
                ok = false;
            }
        }
        // the SoundType (blocks.json "sound": Bedrock's step / break / place sounds)
        auto it = blocks.find(id);
        std::string got = it != blocks.end() && it->is_object() ? it->value("sound", std::string()) : std::string();
        if (got != r.sound) {
            ctx.check(false, id + " sounds like " + r.javaSound + " (" + r.sound + ", got \"" + got + "\")");
            ok = false;
        }
        ok ? ++good : ++bad;
    }
    ctx.check(bad == 0, "block properties as TF: " + std::to_string(good) + " of " + std::to_string(std::size(rows)));
});

GF_REGISTER_SELFTEST("building.trophy_variants", [](Context& ctx) {
    // BossVariant: pedestal comparator values and AbstractTrophyBlock#playSound per variant
    struct Row {
        char const* boss;
        int         comparator;
        char const* sound;
        float       volume, pitch;
    };
    static Row const rows[]{
        {"naga", 5, "entity.twilightforest.naga.rattle", 1.25f, 1.2f},
        {"lich", 6, "entity.twilightforest.lich.ambient", 0.35f, 1.1f},
        {"hydra", 12, "entity.twilightforest.hydra.growl", 1.0f, 1.2f},
        {"ur_ghast", 13, "entity.twilightforest.ur_ghast.ambient", 1.0f, 0.6f},
        {"knight_phantom", 8, "entity.twilightforest.knight_phantom.ambient", 1.0f, 1.1f},
        {"snow_queen", 14, "entity.twilightforest.snow_queen.ambient", 1.0f, 0.9f},
        {"minoshroom", 7, "entity.twilightforest.minoshroom.ambient", 0.75f, 0.7f},
        {"alpha_yeti", 9, "entity.twilightforest.alpha_yeti.growl", 0.75f, 0.75f},
        {"quest_ram", 1, "entity.twilightforest.quest_ram.ambient", 1.0f, 0.7f},
    };
    for (auto const& r : rows) {
        std::string floor = std::string("twilightforest:") + r.boss + "_trophy", wall = std::string("twilightforest:") + r.boss + "_wall_trophy";
        auto        v     = gf::boss::trophyVariant(floor);
        ctx.check(v && v->comparator == r.comparator, floor + " pedestal comparator " + std::to_string(r.comparator) + " (got " + (v ? std::to_string(v->comparator) : "none") + ")");
        auto w = gf::boss::trophyVariant(wall);
        ctx.check(w && w->comparator == 0, wall + " passes 0 (TrophyWallBlock)");
        auto s = b::trophySound(floor, 0.5f);
        ctx.check(s && s->event == r.sound && std::abs(s->volume - r.volume) < 1e-4f && std::abs(s->pitch - r.pitch) < 1e-4f,
                  floor + " sound " + r.sound + " (got " + (s ? s->event + " " + std::to_string(s->volume) + " " + std::to_string(s->pitch) : "none") + ")");
        ctx.check(b::trophyBoss(floor) == std::string(r.boss) && b::trophyBoss(wall) == std::string(r.boss) && b::isWallTrophy(wall) && !b::isWallTrophy(floor), floor + " ids");
    }
    // rarities: TWILIGHT trophies and banner patterns (dark green names), the UNCOMMON fiery block
    namespace ex = gf::item::ext;
    for (auto const& boss : b::trophyBosses()) {
        ctx.check(ex::rarityOf("twilightforest:" + boss + "_trophy") == ex::Rarity::Twilight, boss + " trophy is TWILIGHT");
        ctx.check(ex::rarityOf("twilightforest:" + boss + "_banner_pattern") == ex::Rarity::Twilight, boss + " banner pattern is TWILIGHT");
    }
    ctx.check(ex::rarityOf("twilightforest:fiery_block") == ex::Rarity::Uncommon, "the fiery block is UNCOMMON");
    auto rare = b::trophySound("twilightforest:alpha_yeti_trophy", 0.001f);
    ctx.check(rare && rare->event == "entity.twilightforest.alpha_yeti.roar", "the alpha yeti roars once in 50");
    // TrophyBlock#getStateForPlacement: floor(yaw * 16 / 360 + 0.5) & 15
    ctx.check(b::trophyRotation(0) == 0 && b::trophyRotation(90) == 4 && b::trophyRotation(180) == 8 && b::trophyRotation(-90) == 12 && b::trophyRotation(11.0f) == 0 &&
                  b::trophyRotation(12.0f) == 1 && b::trophyRotation(-12.0f) == 15 && b::trophyRotation(359.0f) == 0,
              "trophy rotation from the yaw");
});

GF_REGISTER_SELFTEST("building.nagastone_variants", [](Context& ctx) -> ll::coro::CoroTask<> {
    using namespace gf::feature::building;
    // NagastoneVariant.getVariantFromDoubleFacing for every 0 / 1 / 2 connection layout
    ctx.check(nagastoneVariant(-1, -1) == "solid", "no connection: solid");
    ctx.check(nagastoneVariant(Down, -1) == "axis_y" && nagastoneVariant(Up, -1) == "axis_y" && nagastoneVariant(North, -1) == "axis_z" && nagastoneVariant(West, -1) == "axis_x", "one connection: its axis");
    ctx.check(nagastoneVariant(Down, Up) == "axis_y" && nagastoneVariant(North, South) == "axis_z" && nagastoneVariant(West, East) == "axis_x", "two on one axis: the axis");
    ctx.check(nagastoneVariant(North, West) == "solid" && nagastoneVariant(South, East) == "solid", "two horizontal on different axes: solid");
    static int const sides[]{North, South, West, East};
    static char const* const names[]{"north", "south", "west", "east"};
    for (int i = 0; i < 4; ++i) {
        ctx.check(nagastoneVariant(Down, sides[i]) == std::string(names[i]) + "_down", std::string("down + ") + names[i]);
        ctx.check(nagastoneVariant(Up, sides[i]) == std::string(names[i]) + "_up", std::string("up + ") + names[i]);
    }
    // in the world: an L of three stones and a head facing into a lone stone
    auto p = ctx.testPos(500, 1);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto mid = at(p, 0, 2, 0), above = at(p, 0, 3, 0), north = at(p, 0, 2, -1);
    ctx.require(ctx.setBlock(mid, "twilightforest:nagastone"), "nagastone");
    co_await ctx.ticks(2);
    ctx.check(st(ctx, mid, "variant") == "solid", "a lone nagastone is solid (" + st(ctx, mid, "variant") + ")");
    ctx.require(ctx.setBlock(above, "twilightforest:nagastone"), "nagastone above");
    co_await ctx.ticks(2);
    ctx.check(st(ctx, mid, "variant") == "axis_y" && st(ctx, above, "variant") == "axis_y", "two stacked: axis_y (" + st(ctx, mid, "variant") + ", " + st(ctx, above, "variant") + ")");
    ctx.require(ctx.setBlock(north, "twilightforest:nagastone"), "nagastone north");
    co_await ctx.ticks(2);
    ctx.check(st(ctx, mid, "variant") == "north_up", "up + north: the north_up elbow (" + st(ctx, mid, "variant") + ")");
    ctx.check(st(ctx, north, "variant") == "axis_z", "the north stone lies along z (" + st(ctx, north, "variant") + ")");
    // a head connects only when it faces the stone
    auto lone = at(p, 3, 2, 2), head = at(p, 3, 2, 1);
    ctx.require(ctx.setBlock(lone, "twilightforest:nagastone"), "lone stone");
    ctx.require(ctx.setBlock(head, "twilightforest:nagastone_head", {{"twilightforest:facing", "east"}}), "a head facing away");
    co_await ctx.ticks(2);
    ctx.check(st(ctx, lone, "variant") == "solid", "a head facing away does not connect (" + st(ctx, lone, "variant") + ")");
    ctx.require(ctx.setBlock(head, "twilightforest:nagastone_head", {{"twilightforest:facing", "north"}}), "the head facing the stone");
    co_await ctx.ticks(2);
    ctx.check(st(ctx, lone, "variant") == "axis_z", "a head facing the stone (north of it) connects (" + st(ctx, lone, "variant") + ")");
});

GF_REGISTER_SELFTEST("building.spiral_bricks", [](Context& ctx) {
    using namespace gf::feature::building;
    // SpiralBrickBlock#getStateForPlacement
    ctx.check(spiralDiagonal(true, North, North, false) == "top_right" && spiralDiagonal(true, South, South, true) == "bottom_left" &&
                  spiralDiagonal(true, East, East, false) == "top_left" && spiralDiagonal(true, West, West, false) == "bottom_right",
              "sneaking: the vertical diagonal from the facing");
    ctx.check(spiralDiagonal(false, North, North, true) == "top_left" && spiralDiagonal(false, North, North, false) == "bottom_left", "north: left (player direction == facing)");
    ctx.check(spiralDiagonal(false, South, South, true) == "top_right" && spiralDiagonal(false, West, West, false) == "bottom_right", "south / west: right");
    ctx.check(spiralDiagonal(false, East, East, true) == "top_left", "east: left");
});

// ---- trophies -----------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("building.trophy_place", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(510, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto ground = at(p, 0, -1, 2);
    // on top of a block, the player south of it looking north (yaw 180): the standing trophy, rotation 8
    auto t = co_await place(ctx, ground, Facing::Up, Vec3{0.5f, 1.0f, 0.5f}, "twilightforest:naga_trophy");
    co_await ctx.ticks(2);
    ctx.check(t && ctx.blockAt(*t) == "twilightforest:naga_trophy", "a trophy on the ground stands (" + (t ? ctx.blockAt(*t) : "not placed") + ")");
    if (t) {
        float yaw      = ctx.player().getRotation().y;
        int   expected = b::trophyRotation(yaw);
        ctx.check(st(ctx, *t, "rotation") == std::to_string(expected), "rotation from the yaw " + std::to_string(yaw) + ": " + std::to_string(expected) + " (got " + st(ctx, *t, "rotation") + ")");
        ctx.check(st(ctx, *t, "powered") == "false", "not powered");
        ctx.setBlock(*t, "minecraft:air");
    }
    // against a wall: the wall trophy facing out of it
    auto wall = at(p, 4, 0, 2);
    ctx.require(ctx.setBlock(wall, "minecraft:stone"), "a wall");
    auto w = co_await place(ctx, wall, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "twilightforest:lich_trophy");
    co_await ctx.ticks(2);
    ctx.check(w && ctx.blockAt(*w) == "twilightforest:lich_wall_trophy" && st(ctx, *w, "facing") == "south",
              "a trophy against a wall hangs on it facing out (" + (w ? ctx.blockAt(*w) + " " + st(ctx, *w, "facing") : "not placed") + ")");
    if (w) ctx.setBlock(*w, "minecraft:air");
});

GF_REGISTER_SELFTEST("building.trophy_powered", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(520, 1);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto t = at(p, 0, 1, 0), source = at(p, 1, 1, 0);
    ctx.require(ctx.setBlock(t, "twilightforest:hydra_trophy", {{"twilightforest:rotation", 4}}), "a hydra trophy");
    co_await ctx.ticks(3);
    ctx.check(st(ctx, t, "powered") == "false", "unpowered at first");
    // the sound plays on the rising edge only (AbstractTrophyBlock#neighborChanged)
    auto sounds = b::trophySoundsPlayed();
    ctx.require(ctx.setBlock(source, "minecraft:redstone_block"), "a redstone block beside it");
    co_await ctx.ticks(6);
    ctx.check(st(ctx, t, "powered") == "true", "a neighbour signal powers the trophy (" + st(ctx, t, "powered") + ")");
    ctx.check(b::trophySoundsPlayed() == sounds + 1, "the rising edge plays the hydra's sound once (" + std::to_string(b::trophySoundsPlayed() - sounds) + ")");
    sounds = b::trophySoundsPlayed();
    ctx.require(ctx.setBlock(source, "minecraft:air"), "signal gone");
    co_await ctx.ticks(6);
    ctx.check(st(ctx, t, "powered") == "false", "and it follows the signal off (" + st(ctx, t, "powered") + ")");
    ctx.check(b::trophySoundsPlayed() == sounds, "the falling edge is silent");
    // a click plays the sound and the particles (any hand)
    stand(ctx, t, static_cast<int>(Facing::South), "");
    co_await ctx.ticks(kClickGap);
    bool consumed = false;
    auto id       = ia::onUseOn({.block = "twilightforest:hydra_trophy"}, [&](ia::UseOnContext const&) { consumed = true; return ia::Result::Pass; }, ia::priority::Highest + 1000);
    sounds        = b::trophySoundsPlayed();
    useOn(ctx, t, Facing::South, Vec3{0.5f, 0.25f, 0.75f});
    ia::remove(id);
    co_await ctx.ticks(1);
    ctx.check(consumed && ctx.blockAt(t) == "twilightforest:hydra_trophy", "the click reaches the trophy and it stays");
    ctx.check(b::trophySoundsPlayed() == sounds + 1, "the click plays the sound");
});

GF_REGISTER_SELFTEST("building.trophy_dispense", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TFDispenserBehaviors: a dispenser equips the trophy on the head of the living entity in front
    auto p = ctx.testPos(530, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto  disp = at(p, 0, 1, 0);
    auto& sp   = sim(ctx);
    sp.setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack::EMPTY_ITEM());
    ctx.require(ctx.setBlock(disp, "minecraft:dispenser", {{"facing_direction", 5}}), "a dispenser facing east");
    sp.teleport(Vec3{disp.x + 1.5f, float(disp.y), disp.z + 0.5f}, sp.getDimensionId());
    co_await ctx.ticks(3);
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    BlockPos bp    = gf::game::blockPos(disp);
    auto*    actor = region->getBlockEntity(bp);
    ctx.require(actor != nullptr, "the dispenser's block actor");
    auto* container = static_cast<DispenserBlockActor*>(actor)->getContainer();
    ctx.require(container != nullptr, "its container");
    auto trophy = gf::game::items::make("twilightforest:minoshroom_trophy", 2);
    ctx.require(trophy.has_value(), "a minoshroom trophy");
    container->setItem(0, *trophy);
    static_cast<DispenserBlock const&>(region->getBlock(bp).getBlockType()).dispenseFrom(*region, bp);
    co_await ctx.ticks(2);
    auto& p2 = sim(ctx);
    ctx.check(gf::game::items::idOf(p2.getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == "twilightforest:minoshroom_trophy", "the player in front wears the trophy");
    ctx.check(gf::game::items::countOf(container->getItem(0)) == 1, "one trophy left in the dispenser");
    // nobody in front: the trophy stays (OptionalDispenseItemBehavior fails)
    p2.teleport(Vec3{disp.x + 0.5f, float(disp.y + 3), disp.z + 3.5f}, p2.getDimensionId());
    co_await ctx.ticks(2);
    static_cast<DispenserBlock const&>(region->getBlock(bp).getBlockType()).dispenseFrom(*region, bp);
    co_await ctx.ticks(2);
    ctx.check(gf::game::items::countOf(container->getItem(0)) == 1 && itemsNear(ctx, at(disp, 1, 0, 0), "twilightforest:minoshroom_trophy") == 0, "with nobody in front the trophy stays in the dispenser");
    container->setItem(0, ItemStack::EMPTY_ITEM());
    sim(ctx).setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack::EMPTY_ITEM());
});

// ---- rope ---------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("building.rope", [](Context& ctx) -> ll::coro::CoroTask<> {
    using namespace gf::feature::building;
    auto p = ctx.testPos(540, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    // a stone overhead holds a vertical rope chain of three
    auto top = at(p, 0, 6, 2);
    ctx.require(ctx.setBlock(top, "minecraft:stone"), "a stone overhead");
    std::vector<GlobalPos> chain{at(top, 0, -1, 0), at(top, 0, -2, 0), at(top, 0, -3, 0)};
    for (auto const& c : chain) ctx.require(ctx.setBlock(c, "twilightforest:rope"), "rope");
    co_await ctx.ticks(4);
    bool kept = true;
    for (auto const& c : chain) kept = kept && ctx.blockAt(c) == "twilightforest:rope";
    ctx.check(kept, "a vertical chain under a stone survives");
    ctx.check(ropeCheckConnection(*region, gf::game::blockPos(chain[2]), Up), "the bottom rope's chain leads up to support");
    // the rope item used on the bottom rope extends it downwards (RopeItem: a y strand goes down)
    stand(ctx, chain[2], static_cast<int>(Facing::South), "twilightforest:rope");
    co_await ctx.ticks(kClickGap);
    useOn(ctx, chain[2], Facing::South, Vec3{0.5f, 0.5f, 0.6f});
    co_await ctx.ticks(3);
    auto extended = at(chain[2], 0, -1, 0);
    ctx.check(ctx.blockAt(extended) == "twilightforest:rope" && st(ctx, extended, "y") == "true", "the rope item extends the chain downwards (" + ctx.blockAt(extended) + ")");
    if (ctx.blockAt(extended) == "twilightforest:rope") chain.push_back(extended);
    // the support goes: one tick later every strand is dropped (one rope each)
    clearItems(ctx, chain[1]);
    ctx.setBlock(top, "minecraft:air");
    co_await ctx.ticks(8);
    bool gone = true;
    for (auto const& c : chain) gone = gone && ctx.blockAt(c) != "twilightforest:rope";
    ctx.check(gone, "without support the chain breaks");
    int dropped = itemsNear(ctx, chain[1], "twilightforest:rope");
    ctx.check(dropped == static_cast<int>(chain.size()), "one rope per strand dropped (" + std::to_string(dropped) + " of " + std::to_string(chain.size()) + ")");
    clearItems(ctx, chain[1]);
    // a horizontal strand between two walls; a cross rope drops only its lost strand
    auto w1 = at(p, 4, 2, 2), w2 = at(p, 6, 2, 2), mid = at(p, 5, 2, 2);
    ctx.require(ctx.setBlock(w1, "minecraft:stone") && ctx.setBlock(w2, "minecraft:stone"), "two walls");
    ctx.require(ctx.setBlock(mid, "twilightforest:rope", {{"twilightforest:x", true}, {"twilightforest:y", true}, {"twilightforest:z", false}}), "an x + y rope");
    co_await ctx.ticks(6);
    ctx.check(ctx.blockAt(mid) == "twilightforest:rope" && st(ctx, mid, "x") == "true" && st(ctx, mid, "y") == "false",
              "the unsupported y strand goes, the x strand between the walls stays (x " + st(ctx, mid, "x") + ", y " + st(ctx, mid, "y") + ")");
    ctx.check(itemsNear(ctx, mid, "twilightforest:rope") == 1, "one rope dropped for the lost strand");
    clearItems(ctx, mid);
    // never supported from below
    auto floor = at(p, 8, 1, 2), onFloor = at(p, 8, 2, 2);
    ctx.require(ctx.setBlock(floor, "minecraft:stone"), "a floor");
    ctx.check(!ropeCanConnectTo(*region, gf::game::blockPos(floor), Down), "a block below is no support");
    ctx.require(ctx.setBlock(onFloor, "twilightforest:rope"), "a rope on the floor");
    co_await ctx.ticks(6);
    ctx.check(ctx.blockAt(onFloor) != "twilightforest:rope", "a vertical rope standing on a floor breaks");
    clearItems(ctx, onFloor);
});

GF_REGISTER_SELFTEST("building.iron_ladder", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(550, 1);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // a wall of three, ladders on its south side
    auto w0 = at(p, 0, 1, 0), w1 = at(p, 1, 1, 0), w2 = at(p, 2, 1, 0);
    for (auto const& w : {w0, w1, w2}) ctx.require(ctx.setBlock(w, "minecraft:stone"), "wall");
    auto l0 = co_await place(ctx, w0, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "twilightforest:iron_ladder");
    co_await ctx.ticks(2);
    ctx.check(l0 && ctx.blockAt(*l0) == "twilightforest:iron_ladder" && st(ctx, *l0, "facing") == "south", "a ladder on a wall's south side faces south (" + (l0 ? st(ctx, *l0, "facing") : "not placed") + ")");
    auto l1 = co_await place(ctx, w1, Facing::South, Vec3{0.5f, 0.5f, 1.0f}, "twilightforest:iron_ladder");
    co_await ctx.ticks(3);
    ctx.require(l0 && l1, "two ladders");
    // facing south: counter-clockwise is east, clockwise west (l1 is east of l0)
    ctx.check(st(ctx, *l0, "left") == "true" && st(ctx, *l0, "right") == "false", "the west ladder connects to the east one on its left (" + st(ctx, *l0, "left") + st(ctx, *l0, "right") + ")");
    // the support goes: the ladder breaks
    clearItems(ctx, *l1);
    ctx.setBlock(w1, "minecraft:air");
    co_await ctx.ticks(4);
    ctx.check(ctx.blockAt(*l1) != "twilightforest:iron_ladder", "without its wall the ladder breaks");
    ctx.check(itemsNear(ctx, *l1, "twilightforest:iron_ladder") >= 1, "and drops itself");
    clearItems(ctx, *l1);
    ctx.setBlock(*l0, "minecraft:air");
});

// ---- storage blocks -----------------------------------------------------------------------------

GF_REGISTER_SELFTEST("building.storage_blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    using namespace gf::feature::building;
    ctx.check(arcticFurFallDamage(10.0f) == 1.0f && arcticFurFallDamage(1.0f) == 1.0f && arcticFurFallDamage(20.0f) == 2.0f && arcticFurFallDamage(0) == 0, "arctic fur: a tenth of the fall damage, rounded up");
    auto p = ctx.testPos(560, 2);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& sp = sim(ctx);
    sp.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, ItemStack::EMPTY_ITEM());
    // fiery block: stepping on it hurts, fiery boots protect
    auto fb = at(p, 3, 1, 0);
    ctx.require(ctx.setBlock(fb, "twilightforest:fiery_block"), "a fiery block");
    sim(ctx).teleport(Vec3{fb.x + 0.5f, fb.y + 1.0f, fb.z + 0.5f}, sim(ctx).getDimensionId());
    co_await ctx.ticks(2);
    float before = sim(ctx).getHealth();
    co_await ctx.ticks(20);
    ctx.info("on the fiery block: on ground " + std::to_string(sim(ctx).isOnGround()) + ", y " + std::to_string(sim(ctx).getPosition().y));
    float after = sim(ctx).getHealth();
    ctx.check(after < before, "the fiery block hurts the player stepping on it (" + std::to_string(before) + " -> " + std::to_string(after) + ")");
    // off the fiery block, and out of the fire and the hurt cooldown, so the knightmetal hit below
    // lands in full (LivingEntity#hurt inside invulnerableTime applies only the amount above lastHurt)
    auto rest = [&]() -> ll::coro::CoroTask<> {
        sim(ctx).teleport(Vec3{p.x + 0.5f, float(p.y), p.z + 3.5f}, sim(ctx).getDimensionId());
        for (int i = 0; i < 300 && sim(ctx).isOnFire(); ++i) co_await ctx.ticks(1);
        co_await ctx.ticks(25);
    };
    co_await rest();
    // a fire source (FieryBlock#isFireSource): fire on top never burns out, the game's own fire tick
    // keeps it; the same old fire on stone next to it goes out (FireBlock#tick: no valid fire
    // location and age > 3)
    {
        auto source = at(p, 6, 1, 0), flame = at(p, 6, 2, 0);
        auto stone = at(p, 8, 1, 0), control = at(p, 8, 2, 0);
        ctx.require(ctx.setBlock(source, "twilightforest:fiery_block") && ctx.setBlock(stone, "minecraft:stone"), "a second fiery block and a stone");
        ctx.require(ctx.setBlock(flame, "minecraft:fire", {{"age", 15}}) && ctx.setBlock(control, "minecraft:fire", {{"age", 15}}), "old fire on both");
        int waited = 0;
        for (; waited < 400 && ctx.blockAt(control) == "minecraft:fire"; waited += 5) co_await ctx.ticks(5);
        co_await ctx.ticks(60); // two more fire ticks at least
        ctx.check(ctx.blockAt(control) != "minecraft:fire", "old fire on stone burns out (after " + std::to_string(waited) + " ticks)");
        ctx.check(ctx.blockAt(flame) == "minecraft:fire", "the same fire on a fiery block burns on (" + ctx.blockAt(flame) + ")");
        // putting it out (water, a potion, a hit, a command) is not undone
        ctx.setBlock(flame, "minecraft:air");
        co_await ctx.ticks(5);
        ctx.check(ctx.blockAt(flame) == "minecraft:air", "fire put out on a fiery block stays out");
        ctx.setBlock(source, "minecraft:air");
        ctx.setBlock(control, "minecraft:air");
        ctx.setBlock(stone, "minecraft:air");
    }
    // KnightmetalBlock#getBlockPathType DAMAGE_OTHER and RopeBlock#isScaffolding as the game's block
    // properties (CausesDamage as magma; climbing reads Scaffolding)
    {
        auto has = [](char const* id, BlockProperty prop) {
            auto const* b = gf::game::blockWith(id);
            return b && (static_cast<std::uint64_t>(b->getBlockType().mProperties) & static_cast<std::uint64_t>(prop)) != 0;
        };
        ctx.check(has("twilightforest:fiery_block", BlockProperty::InfiniBurn), "the fiery block is an infinite fire source");
        ctx.check(has("twilightforest:knightmetal_block", BlockProperty::CausesDamage), "the knightmetal block causes damage (BlockProperty::CausesDamage, as magma)");
        ctx.check(has("twilightforest:rope", BlockProperty::Scaffolding) && has("twilightforest:rope", BlockProperty::Climbable), "the rope climbs as scaffolding");
    }
    co_await rest();
    // knightmetal: standing in its spiky shape hurts (4 knightmetal damage)
    auto km = at(p, 0, 1, 0);
    ctx.require(ctx.setBlock(km, "twilightforest:knightmetal_block"), "a knightmetal block");
    sim(ctx).teleport(Vec3{km.x + 0.5f, km.y + 15.0f / 16.0f, km.z + 0.5f}, sim(ctx).getDimensionId());
    before = sim(ctx).getHealth();
    co_await ctx.ticks(10);
    after = sim(ctx).getHealth();
    ctx.check(after <= before - 4.0f + 0.01f, "knightmetal hurts the player standing on it (" + std::to_string(before) + " -> " + std::to_string(after) + ")");
    ctx.check(fieryHurts(sim(ctx)), "fieryHurts without boots");
    if (auto boots = gf::game::items::make("twilightforest:fiery_boots")) {
        sim(ctx).setArmor(SharedTypes::Legacy::ArmorSlot::Feet, *boots);
        ctx.check(!fieryHurts(sim(ctx)), "fiery boots protect");
        sim(ctx).setArmor(SharedTypes::Legacy::ArmorSlot::Feet, ItemStack::EMPTY_ITEM());
    }
    sim(ctx).teleport(Vec3{p.x + 0.5f, float(p.y), p.z + 3.5f}, sim(ctx).getDimensionId());
});

// ---- recipes ------------------------------------------------------------------------------------

GF_REGISTER_SELFTEST("building.recipes", [](Context& ctx) {
    // the banner patterns from paper + trophy; the stonecutting sets
    for (auto const* boss : {"naga", "lich", "hydra", "ur_ghast", "knight_phantom", "snow_queen", "minoshroom", "alpha_yeti"}) {
        auto list = gf::recipes::producing(std::string("twilightforest:") + boss + "_banner_pattern");
        ctx.check(!list.empty(), std::string("a recipe makes the ") + boss + " banner pattern");
    }
    ctx.check(gf::recipes::producing("twilightforest:quest_ram_banner_pattern").empty(), "the questing ram pattern has no recipe (TF 4.8)");
});

GF_REGISTER_SELFTEST("building.stonecutting", [](Context& ctx) {
    auto const& cuts = kCuts;
    auto has = [&](Cut const& c) {
        for (auto const* r : gf::recipes::producing(c.output)) {
            if (r->type.find("stonecutting") == std::string::npos && r->type.find("stonecutter") == std::string::npos) continue;
            if (r->resultCount != c.count) continue;
            for (auto const& ing : r->ingredients)
                for (auto const& it : ing.items)
                    if (it == c.input) return true;
        }
        return false;
    };
    int found = 0;
    for (auto const& c : cuts) {
        bool ok = has(c);
        ok ? ++found : 0;
        if (!ok) ctx.check(false, std::string("stonecutter: ") + c.input + " -> " + c.output + " x" + std::to_string(c.count));
    }
    ctx.check(found == static_cast<int>(std::size(cuts)), "stonecutting recipes: " + std::to_string(found) + " of " + std::to_string(std::size(cuts)));
});

} // namespace

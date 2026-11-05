// U5 (building): selftests of TF's banner patterns (dx 570..579, rule R12).

#include <algorithm>
#include <string>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/BannerItem.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/BannerBlockActor.h"
#include "mc/world/level/block/actor/BannerPattern.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include "feature/building/Building.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace gf::feature::building {

std::optional<ItemStack> loomResult(ItemStack const& banner, std::string const& patternId, int dyeColor) {
    // LoomMenu#setupResultSlot (the loom's craft: CraftHandlerLoom on Bedrock): a copy of the banner
    // with one more layer {pattern, colour}; at most 6 layers (Java's BannerPatternLayers limit,
    // Bedrock's loom too)
    auto const* pattern = findBannerPattern(patternId);
    if (!pattern || banner.isNull() || game::items::idOf(banner) != "minecraft:banner") return std::nullopt;
    auto layers = BannerItem::getPatternAndColorPairsFromItemStack(banner);
    if (layers.size() >= 6) return std::nullopt;
    // Bedrock's banner data: {Base, Type, Patterns: [{Color, Pattern: nameID}]}
    std::string snbt = "{Base:" + std::to_string(banner.getAuxValue()) + ",Type:0,Patterns:[";
    for (auto const& [p, color] : layers) snbt += "{Color:" + std::to_string(static_cast<int>(color)) + ",Pattern:\"" + *p->mNameID + "\"},";
    snbt += "{Color:" + std::to_string(dyeColor) + ",Pattern:\"" + patternId + "\"}]}";
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag) return std::nullopt;
    ItemStack out = banner;
    out.setUserData(std::make_unique<CompoundTag>(std::move(*tag)));
    return out;
}

} // namespace gf::feature::building

namespace {

using gf::selftest::Context;
using gf::core::GlobalPos;
namespace b = gf::feature::building;
using Facing = ScriptModuleMinecraft::ScriptFacing;

// The client scenario's showcase marker: with a lapis block here the test keeps its banners up for
// the camera (tools/e2e/client/scenarios/building.ps1), else it cleans up at once
constexpr int kMarker[3] = {16, -61, 140};

GF_REGISTER_SELFTEST("building.banner_loom", [](Context& ctx) -> ll::coro::CoroTask<> {
    // TFBannerPatterns: nine patterns, each unlocked by its own pattern item in the loom
    for (auto const& s : b::bannerPatterns()) {
        auto const* p = b::findBannerPattern(s.nameId);
        ctx.check(p != nullptr, "pattern " + s.tfId + " registered as " + s.nameId);
        if (!p) continue;
        auto own = gf::game::items::make("twilightforest:" + s.boss + "_banner_pattern");
        ctx.require(own.has_value(), "the " + s.boss + " pattern item");
        ctx.check(p->matchesPatternItem(*own), s.boss + " banner pattern unlocks " + s.nameId);
        for (auto const& o : b::bannerPatterns())
            if (o.boss != s.boss)
                if (auto other = gf::game::items::make("twilightforest:" + o.boss + "_banner_pattern"))
                    if (p->matchesPatternItem(*other)) ctx.check(false, o.boss + " banner pattern must not unlock " + s.nameId);
        ctx.check(own->getMaxStackSize() == 1, s.boss + " banner pattern stacks to 1");
    }
    if (auto const* creeper = b::findBannerPattern("cre"))
        if (auto naga = gf::game::items::make("twilightforest:naga_banner_pattern")) ctx.check(!creeper->matchesPatternItem(*naga), "a vanilla pattern ignores TF's items");

    // the loom's result: a white banner + the naga pattern + red dye (1)
    auto banner = gf::game::items::make("minecraft:banner", 1, 15);
    ctx.require(banner.has_value(), "a white banner");
    auto result = b::loomResult(*banner, "tf_naga", 1);
    ctx.require(result.has_value(), "the loom makes a banner");
    auto layers = BannerItem::getPatternAndColorPairsFromItemStack(*result);
    bool found  = std::any_of(layers.begin(), layers.end(), [](auto const& l) { return *l.first->mNameID == "tf_naga"; });
    ctx.check(found, "the result's layer is TF's naga pattern (" + std::to_string(layers.size()) + " layer(s))");
    // a seventh layer is refused
    auto full = *result;
    for (int i = 0; i < 5; ++i) full = *b::loomResult(full, "tf_lich", 4);
    ctx.check(!b::loomResult(full, "tf_hydra", 2).has_value(), "six layers at most");
    // saved and loaded with the item
    auto save   = SaveContextFactory::createCloneSaveContext();
    auto saved  = result->save(*save);
    auto loaded = ItemStack::fromTag(*saved);
    ctx.check(gf::game::items::userData(loaded) && gf::game::items::userData(loaded)->toSnbt().find("tf_naga") != std::string::npos, "the layer survives a save");

    // placed: the standing banner's block actor holds the pattern (BannerItem#useOn)
    auto p = ctx.testPos(570, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto* region = ctx.region();
    ctx.require(region != nullptr, "region");
    bool  show = b::idAt(*region, BlockPos{kMarker[0], kMarker[1], kMarker[2]}) == "minecraft:lapis_block";
    auto& sp   = static_cast<SimulatedPlayer&>(ctx.player());
    std::vector<GlobalPos> placed;
    int                    i = 0;
    for (auto const& s : b::bannerPatterns()) {
        GlobalPos ground = p;
        ground.x += i++;
        ground.z += 2;
        ctx.require(ctx.setBlock(ground, "minecraft:stone"), "ground");
        auto one = b::loomResult(*banner, s.nameId, 1 + (i % 14));
        ctx.require(one.has_value(), "a banner of " + s.nameId);
        BlockPos gp = gf::game::blockPos(ground);
        sp.teleport(Vec3{gp.x + 0.5f, float(gp.y + 1), gp.z + 3.5f}, sp.getDimensionId());
        sp.simulateLookAt(gp, sim::LookDuration::Instant);
        sp.simulateSetItem(*one, true, 0);
        co_await ctx.ticks(2);
        auto& held = const_cast<ItemStack&>(sp.getSelectedItem());
        sp.mGameMode->useItemOn(held, gp, static_cast<uchar>(Facing::Up), Vec3{gp.x + 0.5f, gp.y + 1.0f, gp.z + 0.5f}, HandSlot::Mainhand, nullptr, true);
        co_await ctx.ticks(1);
        GlobalPos above = ground;
        above.y += 1;
        placed.push_back(ground);
        placed.push_back(above);
        auto* actor = region->getBlockEntity(gf::game::blockPos(above));
        ctx.check(actor != nullptr && ctx.blockAt(above) == "minecraft:standing_banner", "a standing banner of " + s.nameId + " (" + ctx.blockAt(above) + ")");
        if (!actor) continue;
        auto const& patterns = *static_cast<BannerBlockActor*>(actor)->mPatterns;
        auto const* want     = b::findBannerPattern(s.nameId);
        ctx.check(want && std::find(patterns.begin(), patterns.end(), *want->mID) != patterns.end(), "the placed banner shows " + s.nameId);
    }
    if (show) {
        gf::log().info("building: TF banners shown at {} {} {}", p.x, p.y + 1, p.z + 2);
        co_await ctx.ticks(20 * 20);
    }
    for (auto it = placed.rbegin(); it != placed.rend(); ++it) ctx.cleanupBlock(*it);
});

} // namespace

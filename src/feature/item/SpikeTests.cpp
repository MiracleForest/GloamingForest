// Spike S6 (F9): the evidence selftests behind docs/spikes/S6.md (prefix "item.spike_", testPos dx
// 5950..5999). They read the engine's closed registries (banner patterns, trim materials, creative
// groups) and prepare what the client e2e photographs: `item.spike_client` gives every real
// (non-simulated) player the carriers of S6 - a fixture item with a custom enchantment and a
// tooltip (lore + glint marker), an iron sword with only a custom enchantment, a banner with a
// pattern id vanilla clients do not know - and puts the fixture carrier effect (hidden Slowness)
// on them. On a server without real players it has nothing to give and passes.

#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/item/registry/TrimMaterial.h"
#include "mc/world/item/registry/TrimMaterialRegistryHardCodedInit.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BannerPattern.h"

#include "feature/catalog/CatalogInternal.h"
#include "feature/effects/Effects.h"
#include "feature/enchant/Enchant.h"
#include "feature/ext/item/Item.h"
#include "feature/item/Item.h"
#include "feature/item/ItemInternal.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace items = gf::game::items;

constexpr char kFixture[]       = "gf_test:fixture_item";
constexpr char kUnknownPattern[] = "gf_test_pattern";

/// A banner whose second pattern is an id no Bedrock client knows (TF's patterns would be such ids).
std::optional<ItemStack> unknownPatternBanner() {
    auto banner = items::make("minecraft:banner", 1, 15);
    if (!banner) return std::nullopt;
    auto tag = CompoundTag::fromSnbt(
        std::string("{Base:15,Type:0,Patterns:[{Color:1,Pattern:\"cre\"},{Color:4,Pattern:\"") + kUnknownPattern + "\"}]}"
    );
    if (!tag) return std::nullopt;
    banner->setUserData(std::make_unique<CompoundTag>(std::move(*tag)));
    return banner;
}

void registerFixtures(bool tooltip) {
    gf::enchant::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .minCost = {5, 9}, .nameKey = "enchantment.gf_test.fixture_react"});
    gf::effects::registerType("gf_test:fixture_frosted", {.carrier = "minecraft:slowness", .color = 0x56CBFD});
    if (tooltip) {
        // stays registered for the client's screenshots (the other item tests clear the fixture's)
        gf::item::internal::unregister(kFixture);
        gf::item::addTooltip(kFixture, [](ItemStack const&, std::vector<std::string>& lines, Player const*) { lines.push_back("S6 tooltip line"); });
        // the client mod draws this bar over the slot (60 %, orange) and names the item in TWILIGHT green
        gf::item::setBar(kFixture, [](ItemStack const&) -> std::optional<gf::item::Bar> { return gf::item::Bar{0.6f, 0xFF8000}; });
        gf::item::ext::setRarity(kFixture, gf::item::ext::Rarity::Twilight);
    }
}

} // namespace

GF_REGISTER_SELFTEST("item.spike_banner_registry", [](Context& ctx) {
    auto const& patterns = BannerPattern::mPatterns();
    std::set<std::string> names;
    for (auto const& p : patterns) names.insert(*p.mNameID);
    ctx.check(patterns.size() >= 38, "Bedrock's banner pattern registry is a fixed vector (got " + std::to_string(patterns.size()) + ")");
    ctx.check(names.contains("cre") && !names.contains(kUnknownPattern), "vanilla ids are known, ours is not");
    auto banner = unknownPatternBanner();
    ctx.require(banner.has_value(), "a banner with an unknown pattern id");
    // the server keeps the unknown id through a save / load round trip
    auto ctxSave = SaveContextFactory::createCloneSaveContext();
    auto saved   = banner->save(*ctxSave);
    ctx.require(saved != nullptr, "the banner saves");
    auto loaded = ItemStack::fromTag(*saved);
    ctx.check(items::userData(loaded) && items::userData(loaded)->toSnbt().find(kUnknownPattern) != std::string::npos, "the unknown pattern id survives a round trip");
    ctx.info("banner patterns: " + std::to_string(patterns.size()) + ", banner data " + items::userData(loaded)->toSnbt());
});

GF_REGISTER_SELFTEST("item.spike_trim_materials", [](Context& ctx) {
    auto materials = TrimMaterialRegistryHardCodedInit::getMaterials();
    std::string list;
    for (auto const& m : materials) list += m.mMaterialId->getString() + "=" + m.mItemName->getString() + " ";
    ctx.check(materials.size() >= 10, "trim materials are a hard-coded list (got " + std::to_string(materials.size()) + ")");
    ctx.info("trim materials: " + list);
});

GF_REGISTER_SELFTEST("item.spike_catalog_groups", [](Context& ctx) {
    auto names = gf::catalog::internal::groupNames();
    ctx.require(!names.empty(), "the creative registry has groups");
    std::string sample;
    for (std::size_t i = 0; i < names.size() && i < 6; ++i) sample += names[i] + " ";
    ctx.info("creative groups: " + std::to_string(names.size()) + ", e.g. " + sample);
});

GF_REGISTER_SELFTEST("item.spike_glint_marker", [](Context& ctx) {
    registerFixtures(false);
    auto sword = items::make("minecraft:iron_sword");
    ctx.require(sword.has_value(), "an iron sword");
    ctx.check(!sword->isEnchanted(), "a plain sword is not enchanted");
    gf::enchant::set(*sword, "gf_test:fixture_react", 2);
    ctx.check(sword->isEnchanted(), "a custom enchantment alone makes it enchanted (empty vanilla list)");
    ctx.check(sword->constructItemEnchantsFromUserData().getAllEnchants().empty(), "with no vanilla enchantment in it");
    ctx.info("sword data " + items::userData(*sword)->toSnbt());
    gf::enchant::set(*sword, "gf_test:fixture_react", 0);
    ctx.check(!sword->isEnchanted(), "the marker goes with the last custom enchantment");
});

GF_REGISTER_SELFTEST("item.spike_client", [](Context& ctx) {
    registerFixtures(true);
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    int given = 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        auto fixture = items::make(kFixture);
        auto sword   = items::make("minecraft:iron_sword");
        auto banner  = unknownPatternBanner();
        if (fixture) {
            gf::enchant::set(*fixture, "gf_test:fixture_react", 2);
            gf::item::internal::refreshLore(*fixture, &p);
            if (!p.add(*fixture)) p.drop(*fixture, false);
        }
        if (sword) {
            gf::enchant::set(*sword, "gf_test:fixture_react", 3);
            if (!p.add(*sword)) p.drop(*sword, false);
        }
        if (banner && !p.add(*banner)) p.drop(*banner, false);
        gf::effects::apply(p, "gf_test:fixture_frosted", 20 * 60, 1);
        p.sendInventory(false);
        ++given;
        gf::log().info("item: S6 carriers given to {}", p.getRealName());
        return true;
    });
    ctx.info("real players given the S6 carriers: " + std::to_string(given));
});

// Spike S6 (F9): the evidence selftests behind docs/spikes/S6.md (prefix "item.spike_", testPos dx
// 5950..5999). They read the engine's closed registries (banner patterns, trim materials, creative
// groups, record components) and prepare what the client e2e photographs: `item.spike_client` gives
// every real (non-simulated) player the carriers of S6 - a fixture item with a custom enchantment
// and a tooltip (lore + glint marker) and two fixture items whose bars come from their item data
// (one and three of four doses), an iron sword with only a custom enchantment, a banner with a
// pattern id vanilla clients do not know, armour trimmed with a material appended to the server's
// trim registry (and a vanilla-trimmed one beside it), and puts the fixture carrier effect (hidden
// Slowness II for frosted) on them. `item.spike_client_clear` takes all of it back (the 'full' run
// runs the two in a row). On a server without real players there is nothing to give.

#include <algorithm>
#include <set>
#include <variant>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/TrimDataPacket.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/sound/SoundEventIdentifier.h"
#include "mc/world/item/components/RecordItemComponent.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/item/registry/TrimMaterial.h"
#include "mc/world/item/registry/TrimMaterialRegistry.h"
#include "mc/world/item/registry/TrimMaterialRegistryHardCodedInit.h"
#include "mc/world/item/registry/TrimPattern.h"
#include "mc/world/item/registry/TrimPatternRegistry.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BannerPattern.h"

#include "feature/catalog/CatalogInternal.h"
#include "feature/effects/Effects.h"
#include "feature/effects/EffectsInternal.h"
#include "feature/enchant/Enchant.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/ext/item/Effects.h"
#include "feature/ext/item/Item.h"
#include "feature/item/Item.h"
#include "feature/item/ItemInternal.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace items = gf::game::items;

constexpr char kFixture[]        = "gf_test:fixture_item";
constexpr char kUnknownPattern[] = "gf_test_pattern";
constexpr char kTrimMaterial[]   = "gf_test_trim";

/// A banner whose second pattern is `pattern` (an id no Bedrock client knows: TF's would be such ids).
std::optional<ItemStack> bannerWith(std::string const& pattern) {
    auto banner = items::make("minecraft:banner", 1, 15);
    if (!banner) return std::nullopt;
    auto tag = CompoundTag::fromSnbt(std::string("{Base:15,Type:0,Patterns:[{Color:1,Pattern:\"cre\"},{Color:4,Pattern:\"") + pattern + "\"}]}");
    if (!tag) return std::nullopt;
    banner->setUserData(std::make_unique<CompoundTag>(std::move(*tag)));
    return banner;
}

/// Armour with an armour trim (Bedrock's "Trim" {Material, Pattern} user data).
std::optional<ItemStack> trimmed(char const* item, std::string const& material, std::string const& pattern) {
    auto armour = items::make(item);
    if (!armour) return std::nullopt;
    auto tag = CompoundTag::fromSnbt("{Trim:{Material:\"" + material + "\",Pattern:\"" + pattern + "\"}}");
    if (!tag) return std::nullopt;
    armour->setUserData(std::make_unique<CompoundTag>(std::move(*tag)));
    return armour;
}

void registerFixtures(bool client) {
    gf::enchant::registerType("gf_test:fixture_react", {.maxLevel = 3, .anvilCost = 8, .minCost = {5, 9}, .nameKey = "enchantment.gf_test.fixture_react"});
    gf::effects::registerType("gf_test:fixture_frosted", {.carrier = "minecraft:slowness", .color = 0x56CBFD});
    // a texture every pack has (TF's frosted icon comes with F1's import: textures/twilightforest/mob_effect/frosted)
    gf::effects::ext::setIcon("gf_test:fixture_frosted", "textures/items/snowball");
    if (client) {
        // stays registered for the client's screenshots until item.spike_client_clear
        gf::item::internal::unregister(kFixture);
        gf::item::addTooltip(kFixture, [](ItemStack const&, std::vector<std::string>& lines, Player const*) { lines.push_back("S6 tooltip line"); });
        // the client mod draws the bar from the slot's real stack: doses (item data) out of 4, orange
        gf::item::setBar(kFixture, [](ItemStack const& s) -> std::optional<gf::item::Bar> {
            auto doses = gf::item::getData(s, "doses");
            return gf::item::Bar{doses.is_number_integer() ? static_cast<float>(doses.get<int>()) / 4.0f : 0.6f, 0xFF8000};
        });
        gf::item::ext::setRarity(kFixture, gf::item::ext::Rarity::Twilight);
    }
}

void unregisterFixtures() {
    gf::item::internal::unregister(kFixture);
    gf::item::ext::setRarity(kFixture, gf::item::ext::Rarity::Common);
    gf::enchant::internal::unregisterType("gf_test:fixture_react");
    gf::effects::internal::unregisterType("gf_test:fixture_frosted");
}

/// The level's trim registries with `kTrimMaterial` appended (copied from emerald: §a, nether star as
/// its item) or removed; every real player gets the new TrimDataPacket.
bool setTrimProbe(bool add) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    auto materials = level->getTrimMaterialRegistry().lock();
    auto patterns  = level->getTrimPatternRegistry().lock();
    if (!materials || !patterns) return false;
    // TrimMaterial has no exported assignment: the list is rebuilt by copy construction and swapped
    auto&                     list = materials->mTrimMaterials.get();
    std::vector<TrimMaterial> kept;
    kept.reserve(list.size() + 1);
    for (auto const& m : list)
        if (m.mMaterialId->getString() != kTrimMaterial) kept.push_back(m);
    if (add) {
        if (kept.empty()) return false;
        TrimMaterial m(kept.front());
        m.mMaterialId = HashedString(kTrimMaterial);
        m.mColor      = std::string("\xC2\xA7" "2");
        m.mItemName   = HashedString("minecraft:nether_star");
        kept.push_back(m);
    }
    list.swap(kept);
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::TrimData);
        if (!packet) return true;
        auto&                     trim = static_cast<TrimDataPacket&>(*packet);
        std::vector<TrimMaterial> copy(list.begin(), list.end());
        trim.mTrimPatterns = patterns->getAllEntries();
        trim.mTrimMaterials.get().swap(copy);
        trim.sendTo(p);
        return true;
    });
    return true;
}

} // namespace

GF_REGISTER_SELFTEST("item.spike_banner_registry", [](Context& ctx) {
    auto const& patterns = BannerPattern::mPatterns();
    std::set<std::string> names;
    for (auto const& p : patterns) names.insert(*p.mNameID);
    ctx.check(patterns.size() >= 38, "Bedrock's banner pattern registry is a fixed vector (got " + std::to_string(patterns.size()) + ")");
    ctx.check(names.contains("cre"), "vanilla ids are known");
    ctx.info(std::string("the probe pattern ") + kUnknownPattern + (names.contains(kUnknownPattern) ? " is registered (client build: appended)" : " is unknown"));
    auto banner = bannerWith(kUnknownPattern);
    ctx.require(banner.has_value(), "a banner with an unknown pattern id");
    // the server keeps the unknown id through a save / load round trip
    auto ctxSave = SaveContextFactory::createCloneSaveContext();
    auto saved   = banner->save(*ctxSave);
    ctx.require(saved != nullptr, "the banner saves");
    auto loaded = ItemStack::fromTag(*saved);
    ctx.check(items::userData(loaded) && items::userData(loaded)->toSnbt().find(kUnknownPattern) != std::string::npos, "the unknown pattern id survives a round trip");
    ctx.info("banner patterns: " + std::to_string(patterns.size()) + ", banner data " + items::userData(loaded)->toSnbt());
});

#ifdef GF_CLIENT
// The client mod's side of TF's banner patterns: BannerPattern's constructors are MCAPI and the
// registry is a plain vector, so a pattern can be appended (the client of a local world shares it
// with its integrated server). The client e2e photographs a banner with it (local mode).
GF_REGISTER_SELFTEST("item.spike_banner_append", [](Context& ctx) {
    auto& patterns = const_cast<std::vector<BannerPattern>&>(BannerPattern::mPatterns());
    bool  known    = std::any_of(patterns.begin(), patterns.end(), [](BannerPattern const& p) { return *p.mNameID == kUnknownPattern; });
    ctx.info("banner pattern registry: " + std::to_string(patterns.size()) + " of capacity " + std::to_string(patterns.capacity()));
    if (!known) {
        if (patterns.size() == patterns.capacity()) {
            // a reallocation would move patterns the running game may point at: the client mod
            // appends TF's at load instead (U5); nothing to probe here
            ctx.info("no spare capacity: TF's patterns must be appended at mod load, before the game uses the registry");
            return;
        }
        patterns.emplace_back("gf_test", kUnknownPattern, true);
        patterns.back().mID = static_cast<uchar>(patterns.size() - 1);
    }
    known = std::any_of(patterns.begin(), patterns.end(), [](BannerPattern const& p) { return *p.mNameID == kUnknownPattern; });
    ctx.check(known, "the appended pattern is in the registry");
    ctx.info("banner patterns after the append: " + std::to_string(patterns.size()));
});
#endif

GF_REGISTER_SELFTEST("item.spike_trim_materials", [](Context& ctx) {
    auto        materials = TrimMaterialRegistryHardCodedInit::getMaterials();
    std::string list;
    for (auto const& m : materials) list += m.mMaterialId->getString() + "=" + m.mItemName->getString() + " ";
    ctx.check(materials.size() >= 10, "trim materials are a hard-coded list (got " + std::to_string(materials.size()) + ")");
    ctx.info("trim materials: " + list);
    // the level's registry is a vector the server sends in TrimDataPacket: a material can be appended
    ctx.require(setTrimProbe(true), "a material appended to the level's trim registry");
    auto level = ll::service::getLevel();
    auto reg   = level->getTrimMaterialRegistry().lock();
    bool found = std::any_of(reg->mTrimMaterials->begin(), reg->mTrimMaterials->end(), [](TrimMaterial const& m) { return m.mMaterialId->getString() == kTrimMaterial; });
    ctx.check(found, "the level's trim registry holds it");
    ctx.check(setTrimProbe(false), "and takes it back");
});

// How the game's own discs name their sound: the record component's SoundEventIdentifier is a
// variant of (none, LevelSoundEvent, sound event name); a name is what a TF disc's BP item gives.
GF_REGISTER_SELFTEST("item.spike_record_sounds", [](Context& ctx) {
    int components = 0, named = 0, enums = 0;
    std::string sample;
    for (char const* id : {"minecraft:music_disc_13", "minecraft:music_disc_cat", "minecraft:music_disc_pigstep", "minecraft:music_disc_otherside",
                           "minecraft:music_disc_5", "minecraft:music_disc_relic", "minecraft:music_disc_creator", "minecraft:music_disc_precipice"}) {
        auto stack = items::make(id);
        if (!stack || !stack->mItem) continue;
        auto const* component = stack->mItem->getComponent(RecordItemComponent::getIdentifier());
        if (!component) continue;
        ++components;
        auto const& sound = static_cast<RecordItemComponent const*>(component)->mSoundEvent->mSound.get();
        if (auto const* name = std::get_if<std::string>(&sound)) {
            ++named;
            if (sample.size() < 200) sample += std::string(id) + "=\"" + *name + "\" ";
        } else if (auto const* e = std::get_if<SharedTypes::Legacy::LevelSoundEvent>(&sound)) {
            ++enums;
            if (sample.size() < 200) sample += std::string(id) + "=LevelSoundEvent(" + std::to_string(static_cast<int>(*e)) + ") ";
        }
    }
    ctx.info("vanilla discs with a record component: " + std::to_string(components) + " (sound by name " + std::to_string(named) + ", by LevelSoundEvent "
             + std::to_string(enums) + "): " + sample);
    ctx.check(components > 0, "the game's discs are component items with a record component");
});

GF_REGISTER_SELFTEST("item.spike_catalog_groups", [](Context& ctx) {
    auto names = gf::catalog::internal::groupNames();
    ctx.require(!names.empty(), "the creative registry has groups");
    std::string sample;
    for (std::size_t i = 0; i < names.size() && i < 6; ++i) sample += names[i] + " ";
    ctx.info("creative groups: " + std::to_string(names.size()) + ", e.g. " + sample);
#ifdef GF_CLIENT
    ctx.info("CreativeItemRegistry::newItemEntry: exported by the client binary (local worlds' integrated server)");
#else
    // LeviLamina 26.51.7 declares newItemEntry / initializeFromCreativeContentPacket / updateNetIdMap
    // under LL_PLAT_C only (mc/world/item/registry/CreativeItemRegistry.h): BDS does not export them
    ctx.info("CreativeItemRegistry::newItemEntry: not exported by BDS (LL_PLAT_C only)");
#endif
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
    unregisterFixtures();
});

GF_REGISTER_SELFTEST("item.spike_client", [](Context& ctx) {
    registerFixtures(true);
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    setTrimProbe(true);
    int given = 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        int  slot = 0; // hotbar 1..7, whatever was there before
        auto give = [&](std::optional<ItemStack> s) { p.getInventory().setItem(slot++, s.value_or(ItemStack::EMPTY_ITEM())); };
        auto fixture = items::make(kFixture);
        if (fixture) {
            gf::enchant::set(*fixture, "gf_test:fixture_react", 2);
            gf::item::internal::refreshLore(*fixture, &p);
        }
        auto sword = items::make("minecraft:iron_sword");
        if (sword) gf::enchant::set(*sword, "gf_test:fixture_react", 3);
        auto one = items::make(kFixture), three = items::make(kFixture);
        if (one) gf::item::setData(*one, "doses", 1);
        if (three) gf::item::setData(*three, "doses", 3);
        give(fixture);                                                     // hotbar 1
        give(sword);                                                       // hotbar 2
        give(bannerWith(kUnknownPattern));                                 // hotbar 3
        give(one);                                                         // hotbar 4
        give(three);                                                       // hotbar 5
        give(trimmed("minecraft:diamond_chestplate", kTrimMaterial, "coast")); // hotbar 6
        give(trimmed("minecraft:diamond_chestplate", "emerald", "coast"));    // hotbar 7
        gf::effects::apply(p, "gf_test:fixture_frosted", 20 * 60, 1);
        p.sendInventory(false);
        ++given;
        gf::log().info("item: S6 carriers given to {}", p.getRealName());
        return true;
    });
    ctx.info("real players given the S6 carriers: " + std::to_string(given));
});

GF_REGISTER_SELFTEST("item.spike_client_clear", [](Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulatedPlayer()) gf::effects::remove(p, "gf_test:fixture_frosted");
        return true;
    });
    setTrimProbe(false);
    unregisterFixtures();
#ifdef GF_CLIENT
    // the probe pattern of item.spike_banner_append (last in the registry: no other pattern moves)
    auto& patterns = const_cast<std::vector<BannerPattern>&>(BannerPattern::mPatterns());
    if (!patterns.empty() && *patterns.back().mNameID == kUnknownPattern) patterns.pop_back();
#endif
    ctx.check(!gf::item::ext::barOf(*items::make(kFixture)), "the fixture item has no S6 registrations left");
});

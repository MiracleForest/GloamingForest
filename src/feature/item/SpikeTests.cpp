// Spike S6 (F9): the evidence selftests behind docs/spikes/S6.md (prefix "item.spike_", testPos dx
// 5950..5999). They read the engine's closed registries (banner patterns, trim materials, creative
// groups, record components) and prepare what the client e2e photographs: `item.spike_client` gives
// every real (non-simulated) player the carriers of S6 - a fixture item with a custom enchantment
// and a tooltip (lore + glint marker) and two fixture items whose bars come from their item data
// (one and three of four doses), an iron sword with only a custom enchantment, a banner with a
// pattern id vanilla clients do not know, armour trimmed with a material registered at load
// (ext::addTrimMaterial, in the level's registry before anyone joined: only in the spike's runs,
// whose prelaunch step leaves <mod>/s6-trim-probe.flag; otherwise it is appended with the late
// ones), a vanilla-trimmed one, and ones trimmed with materials appended while the player is in the
// world; and puts the fixture carrier effect (hidden Slowness II for frosted) on them.
// `item.spike_client_clear` takes all of it back (the 'full' run runs the two in a row). On a server
// without real players there is nothing to give.
//
// A companion client (remote-mod) runs no selftests: the host sends it "item.spike_fixtures"
// {"on": true} with the carriers and {"on": false} when it takes them back, and the client mod
// registers / unregisters the same fixtures (only where the gf_test packs are installed). Nothing is
// registered at mod load, so no fixture reaches a world that did not run item.spike_client.

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
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
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/BannerPattern.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"
#include "mc/world/level/ServerSoundInstance.h"

#include "feature/catalog/CatalogInternal.h"
#include "feature/effects/Effects.h"
#include "feature/effects/EffectsInternal.h"
#include "feature/enchant/Enchant.h"
#include "feature/enchant/EnchantInternal.h"
#include "feature/ext/item/Effects.h"
#include "feature/ext/item/Enchant.h"
#include "feature/ext/item/Item.h"
#include "feature/ext/item/Records.h"
#include "feature/item/DataFiles.h"
#include "feature/item/Item.h"
#include "feature/item/ItemInternal.h"
#include "feature/net/Net.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace items = gf::game::items;

constexpr char kFixture[]        = "gf_test:fixture_item";
constexpr char kUnknownPattern[] = "gf_test_pattern";
// Trim material probes. A trimmed item's icon is drawn with the material's colour palette texture
// (textures/trims/color_palettes/<material>.png of the resource packs): the vanilla pack has palettes
// no vanilla material uses (the *_darker ones of armour made of the material itself), so materials
// of those ids separate the registration's timing from the texture.
constexpr char kTrimMaterial[] = "iron_darker";  ///< registered at load (ext::addTrimMaterial) in the spike's runs
constexpr char kLateTrim[]     = "gold_darker";  ///< appended while players are in the world
constexpr char kNoPalette[]    = "gf_test_trim"; ///< appended while players are in the world, no palette texture
// The probe BP of the spike's runs (tools/e2e/server/scenarios/item.s6probe.ps1): a data-driven disc.
constexpr char kProbeDisc[]  = "gf_s6:disc";
constexpr char kProbeSound[] = "gloamingforest:music_disc.gloamingforest.radiance"; ///< F3's sound definitions

/// A banner whose second pattern is `pattern` (an id no Bedrock client knows: upstream's would be such ids).
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

constexpr char kFixturesChannel[] = "item.spike_fixtures";

void registerFixtures(bool client) {
    // a fixture the enchanting table never offers, for one item only
    gf::enchant::registerType(
        "gf_test:fixture_react",
        {.maxLevel = 3, .anvilCost = 8, .minCost = {5, 9}, .supportedItems = "minecraft:iron_sword", .discoverable = false, .nameKey = "enchantment.gf_test.fixture_react"}
    );
    gf::enchant::ext::setFlags("gf_test:fixture_react", {.treasure = false, .tradeable = false, .discoverable = false});
    gf::effects::registerType("gf_test:fixture_frosted", {.carrier = "minecraft:slowness", .color = 0x56CBFD});
    // a texture every pack has (upstream's frosted icon comes with F1's import: textures/gloamingforest/mob_effect/frosted)
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
        gf::item::ext::setRarity(kFixture, gf::item::ext::Rarity::Gloaming);
    }
}

void unregisterFixtures() {
    gf::item::internal::unregister(kFixture);
    gf::item::ext::setRarity(kFixture, gf::item::ext::Rarity::Common);
    gf::enchant::internal::unregisterType("gf_test:fixture_react");
    gf::effects::internal::unregisterType("gf_test:fixture_frosted");
}

/// Tells every companion client to register (or drop) the fixtures; the number of companions told.
int tellCompanions(bool on) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    int n = 0;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulatedPlayer() && gf::net::send(p, kFixturesChannel, {{"on", on}})) ++n;
        return true;
    });
    return n;
}

/// The items item.spike_client gave (hotbar 1..7) by player: item.spike_client_clear takes back what
/// is still there.
std::map<std::int64_t, std::vector<std::pair<int, std::string>>>& givenSlots() {
    static std::map<std::int64_t, std::vector<std::pair<int, std::string>>> m;
    return m;
}

/// The spike run's flag for the trim material registered at load: a development build (the gf_test
/// data is installed) with <mod>/s6-trim-probe.flag (the e2e prelaunch step) or GF_S6_TRIM_PROBE.
bool trimProbeFlagged() {
    std::error_code ec;
    if (!std::filesystem::is_directory(gf::feature::item::data::root() / "gf_test", ec)) return false;
    char*       value = nullptr;
    std::size_t size  = 0;
    if (_dupenv_s(&value, &size, "GF_S6_TRIM_PROBE") == 0 && value) {
        std::free(value);
        return true;
    }
    try {
        return std::filesystem::exists(gf::Mod::getInstance().getSelf().getModDir() / "s6-trim-probe.flag", ec);
    } catch (...) {
        return false;
    }
}

/// The materials setTrimProbe appended (and takes out again).
std::set<std::string>& appendedProbes() {
    static std::set<std::string> s;
    return s;
}

/// The level's trim registries with `kLateTrim` and `kNoPalette` appended (copies of the first
/// material: §2, the fixture item as their item) while players are in the world, and `kTrimMaterial`
/// too when the run did not register it at load (no prelaunch flag: the BDS host of the client's
/// remote modes); `add` false takes out what it appended. Every real player gets the new
/// TrimDataPacket, with the materials only: a client adds the packet's patterns to the ones it has
/// (TrimPatternRegistry::initializeFromTrimPatterns) and logs "Overwriting pattern template" for each
/// one it already took from the join's packet, and the patterns did not change.
bool setTrimProbe(bool add) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    auto materials = level->getTrimMaterialRegistry().lock();
    if (!materials) return false;
    // TrimMaterial has no exported assignment: the list is rebuilt by copy construction and swapped
    auto& list = materials->mTrimMaterials.get();
    auto& mine = appendedProbes();
    // what was appended belongs to the registry it went into (the client loads many worlds)
    static void const* appendedTo = nullptr;
    if (appendedTo != materials.get()) mine.clear();
    appendedTo  = materials.get();
    bool atLoad = std::any_of(list.begin(), list.end(), [](TrimMaterial const& m) { return m.mMaterialId->getString() == kTrimMaterial; })
               && !mine.contains(kTrimMaterial);
    std::vector<TrimMaterial> kept;
    kept.reserve(list.size() + 3);
    for (auto const& m : list)
        if (!mine.contains(m.mMaterialId->getString()) && m.mMaterialId->getString() != kLateTrim && m.mMaterialId->getString() != kNoPalette) kept.push_back(m);
    mine.clear();
    if (add) {
        if (kept.empty()) return false;
        std::vector<char const*> ids{kLateTrim, kNoPalette};
        if (!atLoad) ids.push_back(kTrimMaterial);
        for (auto const* id : ids) {
            TrimMaterial m(kept.front());
            m.mMaterialId = HashedString(id);
            m.mColor      = std::string("\xC2\xA7" "2");
            m.mItemName   = HashedString(kFixture);
            kept.push_back(m);
            mine.insert(id);
        }
    }
    list.swap(kept);
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::TrimData);
        if (!packet) return true;
        auto&                     trim = static_cast<TrimDataPacket&>(*packet);
        std::vector<TrimMaterial> copy(list.begin(), list.end());
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
// The client mod's side of upstream's banner patterns: BannerPattern's constructors are MCAPI and the
// registry is a plain vector, so a pattern can be appended (the client of a local world shares it
// with its integrated server). The client e2e photographs a banner with it (local mode).
GF_REGISTER_SELFTEST("item.spike_banner_append", [](Context& ctx) {
    auto& patterns = const_cast<std::vector<BannerPattern>&>(BannerPattern::mPatterns());
    bool  known    = std::any_of(patterns.begin(), patterns.end(), [](BannerPattern const& p) { return *p.mNameID == kUnknownPattern; });
    ctx.info("banner pattern registry: " + std::to_string(patterns.size()) + " of capacity " + std::to_string(patterns.capacity()));
    if (!known) {
        if (patterns.size() == patterns.capacity()) {
            // a reallocation would move patterns the running game may point at: the client mod
            // appends upstream's at load instead (U5); nothing to probe here
            ctx.info("no spare capacity: upstream's patterns must be appended at mod load, before the game uses the registry");
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
    // ext::addTrimMaterial: a material registered at load is in the level's registry from its start
    // (the spike's runs register the fixture material iron_darker: the prelaunch flag)
    bool flagged = trimProbeFlagged();
    if (flagged) ctx.check(gf::item::ext::hasTrimMaterial(kTrimMaterial), "the material registered at load is in the level's registry (appended before any player joined)");
    else {
        ctx.check(!gf::item::ext::hasTrimMaterial(kTrimMaterial), "without the spike's flag no fixture material is registered at load");
        ctx.info("no s6-trim-probe.flag in this run: the material registered at load is not probed");
    }
    // the level's registry is a vector the server sends in TrimDataPacket: a material can also be
    // appended later (clients that joined before take it from the re-sent packet: item.spike_client's hotbar 8)
    ctx.require(setTrimProbe(true), "a material appended to the level's trim registry");
    ctx.check(gf::item::ext::hasTrimMaterial(kLateTrim), "the level's trim registry holds it");
    ctx.check(setTrimProbe(false) && !gf::item::ext::hasTrimMaterial(kLateTrim), "and takes it back");
    ctx.check(gf::item::ext::hasTrimMaterial(kTrimMaterial) == flagged, "the material registered at load stays (only in the spike's run)");
});

namespace {
// The spike's runs (and only they) register the fixture trim material at load, as U7 registers upstream's,
// so it is in the world's registry before players join: the e2e scenario's prelaunch step
// (tools/e2e/{server,client}/scenarios/item.prelaunch.ps1) leaves <mod>/s6-trim-probe.flag next to
// a development build (or GF_S6_TRIM_PROBE is set). Every other world, the 'full' run included,
// never has it.
class SpikeTrimFixtureModule {
public:
    void install() {
        if (!trimProbeFlagged()) return;
        gf::item::ext::addTrimMaterial({.id = kTrimMaterial, .color = "\xC2\xA7" "2", .itemId = kFixture});
        gf::log().info("item: S6 trim probe registered at load ({})", kTrimMaterial);
    }
    void uninstall() {}
};
} // namespace
GF_REGISTER_MODULE(SpikeTrimFixtureModule);

// How the game's own discs name their sound: the record component's SoundEventIdentifier is a
// variant of (none, LevelSoundEvent, sound event name). A BP disc cannot name a custom sound: the BP
// parser binds "sound_event" to the LevelSoundEvent enum (the spike's first probe disc with
// "sound_event": "gf_s6.disc" was refused: "This value is bound to 'LevelSoundEvent' and expects one
// of the following strings ... Found: 'gf_s6.disc'"). So the probe BP disc (gf_s6:disc, prelaunch
// step) leaves sound_event out, and its song is a named sound of ext::setRecordSound: the jukebox's
// own start of the record plays it through gf::fx and its stop stops it (feature/item/Records.cpp).
GF_REGISTER_SELFTEST("item.spike_record_sounds", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (auto disc = items::make(kProbeDisc)) {
        auto const* component = disc->mItem ? disc->mItem->getComponent(RecordItemComponent::getIdentifier()) : nullptr;
        ctx.require(component != nullptr, std::string(kProbeDisc) + " (probe BP item) has its record component");
        auto const* record = static_cast<RecordItemComponent const*>(component);
        auto const& sound  = record->mSoundEvent->mSound.get();
        ctx.check(!std::holds_alternative<std::string>(sound), "the BP disc without sound_event names no sound");
        if (auto const* e = std::get_if<SharedTypes::Legacy::LevelSoundEvent>(&sound))
            ctx.info("the BP disc's default sound_event: LevelSoundEvent(" + std::to_string(static_cast<int>(*e)) + "), not played for a registered disc");
        else ctx.info("the BP disc has no sound_event (monostate)");
        ctx.check(record->mComparatorSignal == 7 && std::abs(record->mDuration - 5.f) < 0.01f, "with its comparator signal (7) and duration (5 s)");
        gf::item::ext::setRecordSound(kProbeDisc, kProbeSound);
        auto before = gf::item::internal::recordCounts();
        // a jukebox plays it: JukeboxBlockActor::_startRecordSound -> the named sound
        auto pos = ctx.testPos(5950);
        ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
        ctx.require(ctx.setBlock(pos, "minecraft:jukebox"), "a jukebox");
        auto  bp     = gf::game::blockPos(pos);
        auto* region = ctx.region();
        auto* actor  = region ? region->getBlockEntity(bp) : nullptr;
        ctx.require(actor && actor->getType() == BlockActorType::Jukebox, "the jukebox's block actor");
        auto& jukebox = static_cast<JukeboxBlockActor&>(*actor);
        jukebox.setRecord(*disc, true); // as JukeboxBlock#use: the record goes in and starts
        co_await ctx.ticks(2);
        region = ctx.region();
        actor  = region ? region->getBlockEntity(bp) : nullptr;
        ctx.require(actor && actor->getType() == BlockActorType::Jukebox, "the jukebox is still there");
        auto& playing = static_cast<JukeboxBlockActor&>(*actor);
        ctx.check(items::idOf(*playing.mRecord) == kProbeDisc, "the jukebox holds the BP disc");
        auto now = gf::item::internal::recordCounts();
        ctx.check(now.started > before.started && now.lastSound == kProbeSound,
                  std::string("the jukebox's start played ") + kProbeSound + " (" + std::to_string(now.started - before.started) + " start(s), last '" + now.lastSound + "')");
        ctx.info("jukebox ticks playing " + std::to_string(playing.mTicksPlaying) + ", server sound instance " + (playing.mServerSoundInstance->has_value() ? "yes" : "no"));
        playing.stopPlayingRecord(*region);
        ctx.check(gf::item::internal::recordCounts().stopped == before.stopped + 1, "stopping the record stops the named sound");
        playing.setRecord(ItemStack::EMPTY_ITEM(), false);
        gf::item::ext::setRecordSound(kProbeDisc, "");
    } else {
        ctx.info(std::string("no ") + kProbeDisc + " (the probe BP of the spike's prelaunch step): the BP disc is not probed");
    }
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

// Attachables: upstream's armour on the body is resource pack attachables (F2's tools/models/attachable.ps1
// over F1's BP items, in the mod's RP that BDS also sends vanilla clients). item.spike_attachables
// puts upstream's fiery helmet (F2's own geometry, geometry.gloamingforest.fiery_armor.helmet) and naga
// chestplate (the humanoid armour geometry with upstream's naga scale texture) into every real player's
// armour slots; the client e2e photographs the player from the front. item.spike_attachables_clear
// puts back what was there. On a server without real players there is nothing to dress.
namespace {
using ArmorSlot = SharedTypes::Legacy::ArmorSlot;
std::map<std::int64_t, std::pair<ItemStack, ItemStack>>& wornBefore() {
    static std::map<std::int64_t, std::pair<ItemStack, ItemStack>> m;
    return m;
}
} // namespace

GF_REGISTER_SELFTEST("item.spike_attachables", [](Context& ctx) {
    auto helmet = items::make("gloamingforest:fiery_helmet");
    auto chest  = items::make("gloamingforest:naga_chestplate");
    if (!helmet || !chest) {
        ctx.info("upstream's armour items are not in this build (F1's import): no attachable to show");
        return;
    }
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    int dressed = 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        auto uid = p.getOrCreateUniqueID().rawID;
        if (!wornBefore().contains(uid)) wornBefore()[uid] = {p.getArmor(ArmorSlot::Head), p.getArmor(ArmorSlot::Torso)};
        p.setArmor(ArmorSlot::Head, *helmet);
        p.setArmor(ArmorSlot::Torso, *chest);
        p.sendArmor(std::bitset<5>().set());
        p.sendInventory(false);
        ++dressed;
        gf::log().info("item: S6 attachables worn by {}", p.getRealName());
        return true;
    });
    ctx.info("real players wearing upstream's fiery helmet and naga chestplate: " + std::to_string(dressed));
});

GF_REGISTER_SELFTEST("item.spike_attachables_clear", [](Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    int undressed = 0;
    level->forEachPlayer([&](Player& p) {
        auto it = wornBefore().find(p.getOrCreateUniqueID().rawID);
        if (it == wornBefore().end()) return true;
        p.setArmor(ArmorSlot::Head, it->second.first);
        p.setArmor(ArmorSlot::Torso, it->second.second);
        p.sendArmor(std::bitset<5>().set());
        p.sendInventory(false);
        wornBefore().erase(it);
        ++undressed;
        return true;
    });
    ctx.info("players given back their armour: " + std::to_string(undressed));
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
    bool atLoad = gf::item::ext::hasTrimMaterial(kTrimMaterial);
    setTrimProbe(true);
    ctx.info(std::string("hotbar 6's material ") + kTrimMaterial + (atLoad ? ": registered at load (before the join)" : ": not flagged at load, appended with the late ones"));
    int given = 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        int   slot = 0; // hotbar 1..7, whatever was there before
        auto& mine = givenSlots()[p.getOrCreateUniqueID().rawID];
        mine.clear();
        auto give = [&](std::optional<ItemStack> s) {
            auto stack = s.value_or(ItemStack::EMPTY_ITEM());
            if (!items::isEmpty(stack)) mine.emplace_back(slot, items::idOf(stack));
            p.getInventory().setItem(slot++, stack);
        };
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
        give(trimmed("minecraft:diamond_chestplate", kTrimMaterial, "coast")); // hotbar 6: registered at load
        give(trimmed("minecraft:diamond_chestplate", "emerald", "coast"));    // hotbar 7: vanilla
        give(trimmed("minecraft:diamond_chestplate", kLateTrim, "coast"));    // hotbar 8: appended after the join
        give(trimmed("minecraft:diamond_chestplate", kNoPalette, "coast"));   // hotbar 9: appended after the join, no palette
        gf::effects::apply(p, "gf_test:fixture_frosted", 20 * 60, 1);
        p.sendInventory(false);
        ++given;
        gf::log().info("item: S6 carriers given to {}", p.getRealName());
        return true;
    });
    // a companion client draws the fixtures' bars, GLOAMING name and effect icon with the same
    // registrations (they go again with item.spike_client_clear)
    int told = tellCompanions(true);
    ctx.info("real players given the S6 carriers: " + std::to_string(given) + ", companions told: " + std::to_string(told));
});

#ifdef GF_CLIENT
namespace {
// The client mod's side of "item.spike_fixtures": the host's item.spike_client / _clear register and
// drop the fixtures; a client without the gf_test packs (a release install) ignores it.
class SpikeClientFixturesModule {
public:
    void load() {
        if (mLoaded) return;
        mLoaded = true;
        gf::net::client::on(kFixturesChannel, [](nlohmann::json const& msg) {
            if (!items::make(kFixture)) return; // no fixture packs: not a development install
            if (msg.value("on", false)) {
                registerFixtures(true);
                gf::log().info("item: S6 fixtures registered for the host");
            } else {
                unregisterFixtures();
                gf::log().info("item: S6 fixtures dropped for the host");
            }
        });
    }
    void install() { load(); }
    void uninstall() { unregisterFixtures(); }

private:
    bool mLoaded = false;
};
} // namespace
GF_REGISTER_MODULE(SpikeClientFixturesModule);
#endif

GF_REGISTER_SELFTEST("item.spike_client_clear", [](Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "a level");
    int taken = 0;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer()) return true;
        gf::effects::remove(p, "gf_test:fixture_frosted");
        // the carriers item.spike_client put into the hotbar, where they still are
        auto it = givenSlots().find(p.getOrCreateUniqueID().rawID);
        if (it != givenSlots().end()) {
            auto& inv = p.getInventory();
            for (auto const& [slot, id] : it->second)
                if (slot < inv.getContainerSize() && items::idOf(inv.getItem(slot)) == id) {
                    inv.setItem(slot, ItemStack::EMPTY_ITEM());
                    ++taken;
                }
            givenSlots().erase(it);
            p.sendInventory(false);
        }
        return true;
    });
    tellCompanions(false);
    setTrimProbe(false);
    unregisterFixtures();
    ctx.info("S6 carriers taken back: " + std::to_string(taken));
#ifdef GF_CLIENT
    // the probe pattern of item.spike_banner_append (last in the registry: no other pattern moves)
    auto& patterns = const_cast<std::vector<BannerPattern>&>(BannerPattern::mPatterns());
    if (!patterns.empty() && *patterns.back().mNameID == kUnknownPattern) patterns.pop_back();
#endif
    ctx.check(!gf::item::ext::barOf(*items::make(kFixture)), "the fixture item has no S6 registrations left");
});

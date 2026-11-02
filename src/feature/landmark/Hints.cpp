// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/util/StructureHints.java
//   src/main/java/twilightforest/world/components/structures/util/ProgressionStructure.java
//   src/main/java/twilightforest/asmhooks/ItemHooks.java
//   src/main/java/twilightforest/client/event/ClientGameEvents.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The hint system: a locked landmark sends a kobold carrying a written book about it, and the book
// always drops when the kobold dies (setDropChance(MAINHAND, PRESERVE_ITEM_DROP_CHANCE_THRESHOLD)).
// TF keeps the book's title, author and pages as translation keys and translates them on each
// reader's client (twilightforest:translatable_book, ModifyWrittenBookName, translateBookAuthor).
// Bedrock books hold plain text, so the book is written in the language of the player it is made
// for (gf::trFor) and keeps the keys in its item data (translatable_book): whoever picks it up or
// opens it gets it written again in their own language.
#include <algorithm>
#include <map>
#include <mutex>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/shared_types/legacy/actor/SlotWithDropChance.h"
#include "mc/entity/components/SlotDropChancesComponent.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/phys/AABB.h"

#include "core/landmark/Grid.h"
#include "feature/common/Interactions.h"
#include "feature/landmark/Internal.h"
#include "feature/progression/Progression.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::landmark {

namespace {

namespace ia = gf::interactions;
using DropSlot = SharedTypes::Legacy::SlotWithDropChance;

constexpr char kBookPrefix[]     = "twilightforest.book.";
constexpr char kTranslatable[]   = "translatable_book";
/// Mob.PRESERVE_ITEM_DROP_CHANCE_THRESHOLD: a drop chance above 1 always drops, undamaged.
constexpr float kPreserveDrop    = 2.0f;
/// Mob.DEFAULT_EQUIPMENT_DROP_CHANCE (the other slots of a drop chance table made here).
constexpr float kDefaultDrop     = 0.085f;
/// The hint mob's box for the spawn checks (TF's kobold: 0.8 x 0.9 blocks).
constexpr float kHintHalfWidth   = 0.4f;
constexpr float kHintHeight      = 0.9f;

struct HintState {
    std::mutex                           mutex;
    std::map<std::string, std::uint64_t> lastSpawned; ///< per structure (ProgressionStructure's field, 0 at start)
    core::landmark::JavaRandom           random{static_cast<std::int64_t>(0x5DEECE66DLL)};
    ll::event::ListenerPtr               pickup;
    std::vector<ia::HandlerId>           handlers;
};
HintState& hints() {
    static HintState s;
    return s;
}

std::string text(Player const* player, std::string const& key, std::string_view fallback) {
    return player ? trFor(*player, key, fallback) : tr(key, fallback);
}

/// Level#isUnobstructed(entity): no other entity that blocks building (living entities, boats,
/// minecarts) overlaps the box.
bool isUnobstructed(BlockSource& region, AABB const& box) {
    for (Actor* a : region.fetchEntities(nullptr, box, true, false)) {
        if (!a || !a->isAlive() || a->getTypeName() == "twilightforest:protection_box") continue;
        bool blocks = a->hasCategory(ActorCategory::Mob) || a->hasCategory(ActorCategory::Boat) || a->hasCategory(ActorCategory::Minecart);
        if (blocks && a->getAABB().intersects(box)) return false;
    }
    return true;
}

/// Rewrites every translatable book of a player's inventory in the player's language.
void retranslateInventory(Player& player) {
    auto& inv     = player.getInventory();
    bool  changed = false;
    for (int slot = 0; slot < inv.getContainerSize(); ++slot) {
        auto const& stack = inv.getItem(slot);
        if (game::items::isEmpty(stack) || game::items::getData(stack, kTranslatable).is_null()) continue;
        ItemStack copy = stack;
        if (!retranslateBook(copy, player)) continue;
        inv.setItem(slot, copy);
        changed = true;
    }
    if (changed) player.sendInventory(false);
}

} // namespace

std::uint64_t gameTime() {
    // Level#getGameTime: level.dat's currentTick (persisted with the world, unlike a session counter)
    auto level = ll::service::getLevel();
    if (!level) return 0;
    return level->getLevelData().mCurrentTick->tickID;
}

namespace {

/// StructureHints.addBookInformationStatic's text: pages, title and author written into `tag` for
/// `player` (the book's other data stays), and the translatable_book marker with the language.
void writeBookText(ItemStack& stack, std::unique_ptr<CompoundTag> tag, std::string const& key, int pageCount, Player const* player) {
    ListTag pages;
    for (int index = 0; index < pageCount; ++index) {
        auto pageKey = std::string(kBookPrefix) + key + "." + std::to_string(index + 1);
        auto page    = std::make_unique<CompoundTag>();
        page->putString("photoname", "");
        page->putString("text", text(player, pageKey, pageKey));
        pages.add(std::move(page));
    }
    tag->put("pages", std::move(pages));
    // translateBookAuthor / ModifyWrittenBookName: author and title are the translated keys
    tag->putString("author", text(player, std::string(kBookPrefix) + "author", "a forgotten explorer"));
    auto titleKey = std::string(kBookPrefix) + key;
    tag->putString("title", text(player, titleKey, titleKey));
    stack.setUserData(std::move(tag));
    // TFDataComponents.TRANSLATABLE_BOOK, with the language the text is written in
    game::items::setData(stack, kTranslatable, Json{{"name", key}, {"pages", pageCount}, {"locale", player ? localeOf(*player) : std::string{}}});
}

} // namespace

std::optional<ItemStack> makeBook(std::string const& name, int pageCount, Player const* player) {
    // StructureHints.addBookInformationStatic (generation 3: "tattered")
    auto stack = game::items::make("minecraft:written_book", 1, 0);
    if (!stack) return std::nullopt;
    auto tag = std::make_unique<CompoundTag>();
    tag->putInt("generation", 3);
    tag->putInt64("xuid", 0);
    writeBookText(*stack, std::move(tag), name.empty() ? "unknown" : name, pageCount, player);
    return stack;
}

bool retranslateBook(ItemStack& book, Player const& reader) {
    auto data = game::items::getData(book, kTranslatable);
    if (!data.is_object() || !data.contains("name")) return false;
    if (data.value("locale", std::string{}) == localeOf(reader)) return false;
    // the pages, title and author again, in the reader's language; everything else of the stack
    // (a custom name, enchantments, the repair cost) stays
    auto const* old = game::items::userData(book);
    auto        tag = old ? old->clone() : std::make_unique<CompoundTag>();
    auto        key = data.value("name", std::string{"unknown"});
    writeBookText(book, std::move(tag), key.empty() ? "unknown" : key, data.value("pages", 2), &reader);
    return true;
}

std::optional<ItemStack> createHintBook(Info const& info, Player const* player) {
    // ProgressionStructure#createHintBook: the configured hint item (empty without a hint config)
    if (info.hintBook.empty()) return std::nullopt;
    return makeBook(info.hintBook, info.hintPages, player);
}

std::optional<float> mainHandDropChance(Actor& actor) {
    auto c = actor.getEntityContext().tryGetComponent<SlotDropChancesComponent>();
    if (!c) return std::nullopt;
    for (auto const& e : c->mDropChances.get())
        if (e.mSlot == DropSlot::Mainhand) return e.mDropChance;
    return std::nullopt;
}

Actor* spawnHintCarrier(Info const& info, BlockSource& region, float x, float y, float z, Player const* player) {
    if (info.hintMob.empty()) return nullptr;
    auto book = createHintBook(info, player);
    return spawnEntity(region, info.hintMob, x, y, z, [book](Actor& mob) {
        if (!book) return;
        // hinty.setItemSlot(MAINHAND, book); hinty.setDropChance(MAINHAND, PRESERVE_ITEM_DROP_CHANCE_THRESHOLD)
        mob.setCarriedItem(*book);
        auto& ctx   = mob.getEntityContext();
        bool  fresh = !ctx.hasComponent<SlotDropChancesComponent>();
        auto& c     = ctx.getOrAddComponent<SlotDropChancesComponent>();
        auto& table = c.mDropChances.get();
        if (fresh) {
            for (std::size_t i = 0; i < table.size(); ++i) {
                table[i].mSlot       = static_cast<DropSlot>(static_cast<int>(i));
                table[i].mDropChance = kDefaultDrop;
            }
            c.mDefaultChance                = kDefaultDrop;
            c.mDefaultChanceForInvalidSlots = 0.0f;
        }
        auto it = std::find_if(table.begin(), table.end(), [](EquipmentTableDropChance const& e) { return e.mSlot == DropSlot::Mainhand; });
        if (it == table.end()) it = table.begin();
        it->mSlot       = DropSlot::Mainhand;
        it->mDropChance = kPreserveDrop;
    });
}

namespace {

/// StructureHints#didSpawnHintMonster.
bool didSpawnHintMonster(Info const& info, Player& player, int x, int y, int z) {
    if (info.hintMob.empty()) return false;
    auto* region = game::regionOf(static_cast<int>(player.getDimensionId()));
    if (!region) return false;
    int dx, dy, dz;
    {
        // find a target point
        std::lock_guard lock(hints().mutex);
        auto&           r = hints().random;
        dx = r.nextInt(16) - r.nextInt(16);
        dy = r.nextInt(4) - r.nextInt(4);
        dz = r.nextInt(16) - r.nextInt(16);
    }
    // Entity#moveTo(BlockPos): the block's bottom center
    float px = static_cast<float>(x + dx) + 0.5f, py = static_cast<float>(y + dy), pz = static_cast<float>(z + dz) + 0.5f;
    if (!region->hasChunksAt(BlockPos{x + dx, y + dy, z + dz}, 1, false)) return false;
    // Mob#checkSpawnObstruction (no liquid in the box, Level#isUnobstructed) and
    // Sensing#hasLineOfSight(player)
    AABB box{Vec3{px - kHintHalfWidth, py, pz - kHintHalfWidth}, Vec3{px + kHintHalfWidth, py + kHintHeight, pz + kHintHalfWidth}};
    if (region->containsAnyLiquid(box)) return false;
    if (!isUnobstructed(*region, box)) return false;
    if (!player.canSee(Vec3{px, py + 0.75f, pz}, ShapeType::Collision)) return false;
    // add items and hint book
    if (!spawnHintCarrier(info, *region, px, py, pz, &player)) return false;
    log().info("landmark: hint {} for {} at {} {} {}", info.hintMob, info.d.key, x + dx, y + dy, z + dz);
    return true;
}

} // namespace

bool trySpawnHintMonster(Info const& info, Player& player, int x, int y, int z) {
    // check if the timer is valid (Level#getGameTime)
    std::uint64_t currentTime = gameTime();
    {
        std::lock_guard lock(hints().mutex);
        auto&           last = hints().lastSpawned[info.d.key];
        // if someone set the time backwards, fix the spawn timer
        if (currentTime < last) last = 0;
        if (currentTime - last <= 1200) return false;
    }
    // okay, time is good, try several times to spawn one
    for (int i = 0; i < 20; i++) {
        if (didSpawnHintMonster(info, player, x, y, z)) {
            std::lock_guard lock(hints().mutex);
            hints().lastSpawned[info.d.key] = currentTime;
            return true;
        }
    }
    return false;
}

void installHints() {
    auto& s = hints();
    // whoever picks a hint book up gets it in their own language (TF translates on each client)
    s.pickup = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerPickUpItemEvent>([](ll::event::PlayerPickUpItemEvent& e) {
        auto& stack = e.itemActor().item();
        if (game::items::idOf(stack) == "minecraft:written_book") (void)retranslateBook(stack, e.self());
    });
    // ... and whoever opens one (a book handed over in a chest or by a hopper)
    s.handlers.push_back(ia::onUse("minecraft:written_book", [](ia::UseContext const& c) {
        if (retranslateBook(ia::mutableItem(c.item), c.player)) c.player.sendInventory(false);
        return ia::Result::Pass;
    }));
    static bool once = false;
    if (!once) {
        once = true;
        progression::events::onInventoryChanged([](Player& p) {
            auto id = p.getOrCreateUniqueID();
            ia::nextTick([id] {
                auto level = ll::service::getLevel();
                if (!level) return;
                if (auto* player = level->getPlayer(id)) retranslateInventory(*player);
            });
        });
    }
}

void resetHints() {
    std::lock_guard lock(hints().mutex);
    hints().lastSpawned.clear();
}

void uninstallHints() {
    auto& s = hints();
    if (s.pickup) ll::event::EventBus::getInstance().removeListener(s.pickup);
    s.pickup = nullptr;
    for (auto id : s.handlers) ia::remove(id);
    s.handlers.clear();
}

} // namespace gf::feature::landmark

namespace gf::landmark {

bool trySpawnHint(Player& player, Start const& start, BlockPos const& pos) {
    auto const* inf = feature::landmark::info(start.key);
    if (!inf || !inf->traits.progression) return false;
    return feature::landmark::trySpawnHintMonster(*inf, player, pos.x, pos.y, pos.z);
}

} // namespace gf::landmark

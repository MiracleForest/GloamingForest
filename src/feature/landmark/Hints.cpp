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
// The hint system: a locked landmark sends a kobold carrying a written book about it. TF keeps the
// book's title, author and pages as translation keys and translates them on the client
// (twilightforest:translatable_book, ModifyWrittenBookName, translateBookAuthor). Bedrock books
// hold plain text, so the book is written for the player it is made for, in that player's
// language (gf::trFor), and keeps the keys in its item data (translatable_book) so it can be
// written again for another reader.
#include <map>
#include <mutex>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/phys/AABB.h"

#include "core/landmark/Grid.h"
#include "feature/landmark/Internal.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::landmark {

namespace {

constexpr char kBookPrefix[] = "twilightforest.book.";

struct HintState {
    std::mutex                           mutex;
    std::map<std::string, std::uint64_t> lastSpawned; ///< per structure (ProgressionStructure's field)
    core::landmark::JavaRandom           random{static_cast<std::int64_t>(0x5DEECE66DLL)};
};
HintState& hints() {
    static HintState s;
    return s;
}

std::string text(Player const* player, std::string const& key, std::string_view fallback) {
    return player ? trFor(*player, key, fallback) : tr(key, fallback);
}

} // namespace

std::optional<ItemStack> makeBook(std::string const& name, int pageCount, Player const* player) {
    // StructureHints.addBookInformationStatic
    std::string key   = name.empty() ? "unknown" : name;
    auto        stack = game::items::make("minecraft:written_book", 1, 0);
    if (!stack) return std::nullopt;
    auto    tag = std::make_unique<CompoundTag>();
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
    tag->putInt("generation", 3);
    tag->putInt64("xuid", 0);
    stack->setUserData(std::move(tag));
    // TFDataComponents.TRANSLATABLE_BOOK
    game::items::setData(*stack, "translatable_book", Json{{"name", key}, {"pages", pageCount}});
    return stack;
}

std::optional<ItemStack> createHintBook(Info const& info, Player const* player) {
    // ProgressionStructure#createHintBook: the configured hint item (empty without a hint config)
    if (info.hintBook.empty()) return std::nullopt;
    return makeBook(info.hintBook, info.hintPages, player);
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
    // Mob#checkSpawnObstruction (no liquid in the box) and Sensing#hasLineOfSight(player)
    AABB box{Vec3{px - 0.4f, py, pz - 0.4f}, Vec3{px + 0.4f, py + 0.9f, pz + 0.4f}};
    if (region->containsAnyLiquid(box)) return false;
    if (!player.canSee(Vec3{px, py + 0.75f, pz}, ShapeType::Collision)) return false;
    // add items and hint book
    auto book  = createHintBook(info, &player);
    auto actor = spawnEntity(*region, info.hintMob, px, py, pz, [book](Actor& mob) {
        if (book) mob.setCarriedItem(*book);
    });
    if (!actor) return false;
    log().info("landmark: hint {} for {} at {} {} {}", info.hintMob, info.d.key, x + dx, y + dy, z + dz);
    return true;
}

} // namespace

bool trySpawnHintMonster(Info const& info, Player& player, int x, int y, int z) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    // check if the timer is valid
    std::uint64_t currentTime = level->getCurrentTick().tickID;
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

void resetHints() {
    std::lock_guard lock(hints().mutex);
    hints().lastSpawned.clear();
}

} // namespace gf::feature::landmark

namespace gf::landmark {

bool trySpawnHint(Player& player, Start const& start, BlockPos const& pos) {
    auto const* inf = feature::landmark::info(start.key);
    if (!inf || !inf->traits.progression) return false;
    return feature::landmark::trySpawnHintMonster(*inf, player, pos.x, pos.y, pos.z);
}

} // namespace gf::landmark

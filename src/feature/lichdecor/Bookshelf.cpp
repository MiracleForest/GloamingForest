// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/ChiseledCanopyShelfBlock.java
//   src/main/java/twilightforest/block/entity/bookshelf/ChiseledCanopyShelfBlockEntity.java
//   src/main/java/twilightforest/block/entity/bookshelf/BookshelfSpawner.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): twilightforest:chiseled_canopy_bookshelf. Vanilla's ChiseledBookShelfBlock (six
// book slots chosen by the hit position on the front face, #minecraft:bookshelf_books, comparator
// output = last interacted slot + 1) over BlockData, plus TF's Death Tome spawner (state `spawner`):
// interaction fails, a BookshelfSpawner spawns twilightforest:death_tome from the filled slots while
// a player (creative included) is within 8 blocks, a spawn egg sets the entity, burning spawns every
// book as a burning tome, breaking it plays the Death Tome's death sound with poof particles.
//
// Bedrock: the shelf's contents are BlockData ("lichdecor"."books", stacks as JSON); the shelf is a
// gf::inventory block (6 slots), which U21's lich tower generation fills. Shelves are tracked by
// position (gf:lichdecor:shelves) so the spawner ticks without scanning chunks.

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/common/BlockData.h"
#include "feature/common/Interactions.h"
#include "feature/common/Redstone.h"
#include "feature/families/BlockEvents.h"
#include "feature/inventory/Inventory.h"
#include "feature/lichdecor/Lichdecor.h"
#include "feature/lichdecor/TestAccess.h"
#include "feature/spawn/Spawn.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::lichdecor {

namespace {

namespace bd    = gf::blockdata;
namespace ia    = gf::interactions;
namespace items = gf::game::items;

constexpr char kShelf[]  = "twilightforest:chiseled_canopy_bookshelf";
constexpr char kTome[]   = "twilightforest:death_tome";
constexpr char kDbKey[]  = "gf:lichdecor:shelves";

// BookshelfSpawner's fields (no NBT variants reach a GloamingForest shelf)
constexpr int kMaxNearby      = 4;
constexpr int kSpawnCheck     = 12;
constexpr int kMinDelay       = 200;
constexpr int kMaxDelay       = 400;
constexpr int kPlayerRange    = 8;
constexpr int kInitialDelay   = 20;

// ---- the document --------------------------------------------------------------------------------------

Json docAt(BlockSource& region, BlockPos const& pos) {
    if (auto const* d = bd::get(region, pos, kDataKey); d && d->is_object()) return *d;
    return Json::object();
}

ItemStack bookAt(Json const& doc, int slot) {
    auto const& books = doc.value("books", Json::array());
    if (!books.is_array() || slot < 0 || slot >= static_cast<int>(books.size())) return items::empty();
    return items::fromJson(books[slot]);
}

std::set<GlobalPos>& shelves() {
    static std::set<GlobalPos> s;
    return s;
}
bool gDirty = false;

void track(GlobalPos const& gp) {
    if (shelves().insert(gp).second) gDirty = true;
}

/// Sets slot `slot` (ChiseledCanopyShelfBlockEntity#setItem / updateState): the stack, the
/// occupied state, the last interacted slot.
void setBook(BlockSource& region, BlockPos const& pos, int slot, ItemStack const& stack) {
    if (slot < 0 || slot >= 6) return;
    Json doc   = docAt(region, pos);
    Json books = doc.value("books", Json::array());
    if (!books.is_array()) books = Json::array();
    while (books.size() < 6) books.push_back(nullptr);
    books[slot]   = items::isEmpty(stack) ? Json(nullptr) : items::toJson(items::withCount(stack, 1));
    doc["books"]  = books;
    doc["last"]   = slot;
    bd::set(region, pos, kDataKey, doc);
    auto const& block = region.getBlock(pos);
    if (game::blockId(block) == kShelf)
        setStates(region, pos, block, Json{{"slot_" + std::to_string(slot) + "_occupied", !items::isEmpty(stack)}});
    track(game::globalPos(region, pos));
}

bool isBook(std::string const& itemId) { return !itemId.empty() && tags::has(tags::Registry::Item, "minecraft:bookshelf_books", itemId); }

/// ChiseledBookShelfBlock#getHitSlot: the slot under the hit on the front face (nullopt: another face).
std::optional<int> hitSlot(Block const& block, int face, interactions::Vec const& hit) {
    int facing = faceOf(state(block, "facing"));
    if (facing < 2 || face != facing) return std::nullopt;
    double x = 0;
    switch (facing) {
    case 2: x = 1.0 - hit[0]; break; // north
    case 3: x = hit[0]; break;       // south
    case 4: x = hit[2]; break;       // west
    case 5: x = 1.0 - hit[2]; break; // east
    default: return std::nullopt;
    }
    int col = x < 0.375 ? 0 : x < 0.6875 ? 1 : 2;
    int row = hit[1] >= 0.5 ? 0 : 1;
    return col + row * 3;
}

void playAt(BlockSource& region, BlockPos const& pos, char const* event, float volume, float pitch) {
    playSound(region, Vec3{pos.x + 0.5F, pos.y + 0.5F, pos.z + 0.5F}, event, volume, pitch);
}

// ---- the spawner ---------------------------------------------------------------------------------------

Level* level() { return ll::service::getLevel().as_ptr(); }

bool nearPlayer(int dim, BlockPos const& pos) {
    auto* lvl = level();
    if (!lvl) return false;
    bool near = false;
    lvl->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim || p.isSpectator()) return true;
        auto const& at = p.getPosition();
        double dx = at.x - (pos.x + 0.5), dy = at.y - (pos.y + 0.5), dz = at.z - (pos.z + 0.5);
        if (dx * dx + dy * dy + dz * dz < kPlayerRange * kPlayerRange) near = true;
        return !near;
    });
    return near;
}

void delay(Json& sp) { sp["delay"] = kMinDelay + randomInt(kMaxDelay - kMinDelay); }

double rand01() { return randomFloat(0.0F, 1.0F); }

/// BookshelfSpawner#attemptSpawnTome.
bool attemptSpawnTome(BlockSource& region, BlockPos const& pos, int slot, bool fire, Actor* assailant, int maxTries, Json& sp) {
    auto const& shelf  = region.getBlock(pos);
    int         facing = faceOf(state(shelf, "facing"));
    if (facing < 2) facing = 2;
    BlockPos front = relative(pos, facing);
    std::string entity = sp.value("entity", std::string(kTome));
    if (!game::replaceable(region, front)) {
        delay(sp);
        return false;
    }
    double x = front.x + (rand01() - rand01()) * 2.0;
    double y = pos.y + (rand01() - rand01());
    double z = front.z + (rand01() - rand01()) * 2.0;
    BlockPos at{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
    // level.noCollision(type.getSpawnAABB): the tome is small (0.6 wide): its block must be free
    if (!game::replaceable(region, at)) {
        if (maxTries != 0) return attemptSpawnTome(region, pos, slot, fire, assailant, maxTries - 1, sp);
        return false;
    }
    auto* lvl = level();
    if (lvl && lvl->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful) {
        delay(sp);
        return false;
    }
    // max nearby entities of the type in the check box (not when burning)
    if (!fire) {
        AABB box{Vec3{pos.x - (float)kSpawnCheck, pos.y - (float)kSpawnCheck, pos.z - (float)kSpawnCheck},
                 Vec3{pos.x + 1.0F + kSpawnCheck, pos.y + 1.0F + kSpawnCheck, pos.z + 1.0F + kSpawnCheck}};
        int count = 0;
        for (Actor* a : region.fetchEntities(static_cast<Actor const*>(nullptr), box, false, false))
            if (a && a->getTypeName() == entity) ++count;
        if (count >= kMaxNearby) {
            delay(sp);
            return false;
        }
    }
    float yaw    = randomFloat(0.0F, 360.0F);
    auto* spawned = gf::spawn::spawnMob(region, entity, Vec3{(float)x, (float)y, (float)z}, gf::spawn::SpawnReason::Spawner, [&](Actor& a) {
        a.setRotationWrapped(Vec2{0.0F, yaw});
        if (fire) a.setOnFire(10, true);
        if (assailant && a.hasCategory(ActorCategory::Mob)) static_cast<Mob&>(a).setTarget(assailant);
    });
    if (!spawned) {
        delay(sp);
        return false;
    }
    // Mob#spawnAnim: poof particles
    for (int i = 0; i < 20; ++i)
        particle(region, "minecraft:poof", Vec3{(float)x + randomFloat(-0.5F, 0.5F), (float)y + randomFloat(0.0F, 1.0F), (float)z + randomFloat(-0.5F, 0.5F)});
    setBook(region, pos, slot, items::empty());
    return true;
}

/// BookshelfSpawner#serverTick.
void tickShelf(BlockSource& region, int dim, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (!boolState(block, "spawner")) return;
    if (!nearPlayer(dim, pos)) return;
    Json doc = docAt(region, pos);
    Json sp  = doc.value("spawner", Json::object());
    if (!sp.is_object()) sp = Json::object();
    int d = sp.value("delay", kInitialDelay);
    if (d == -1) {
        delay(sp);
        d = sp["delay"].get<int>();
    }
    if (d > 0) {
        sp["delay"] = d - 1;
    } else {
        std::vector<int> filled;
        for (int i = 0; i < 6; ++i)
            if (boolState(block, "slot_" + std::to_string(i) + "_occupied")) filled.push_back(i);
        for (int i = static_cast<int>(filled.size()); i > 1; --i) std::swap(filled[i - 1], filled[randomInt(i)]);
        for (int slot : filled) {
            if (attemptSpawnTome(region, pos, slot, false, nullptr, 0, sp)) {
                delay(sp);
                break;
            }
        }
        // the spawn may have changed the document (the book): re-read it
        doc = docAt(region, pos);
        auto const& now = region.getBlock(pos);
        int full = 0;
        for (int i = 0; i < 6; ++i) full += boolState(now, "slot_" + std::to_string(i) + "_occupied") ? 1 : 0;
        if (full == 0) setStates(region, pos, now, Json{{"spawner", false}});
    }
    doc["spawner"] = sp;
    bd::set(region, pos, kDataKey, doc);
}

void tickAll() {
    static int counter = 0;
    ++counter;
    std::vector<GlobalPos> gone;
    for (auto const& gp : shelves()) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos pos = game::blockPos(gp);
        auto     id  = game::blockIdAt(*region, pos);
        if (id.empty()) continue; // not loaded
        if (id != kShelf) {
            gone.push_back(gp);
            continue;
        }
        tickShelf(*region, gp.dim, pos);
    }
    for (auto const& gp : gone) shelves().erase(gp);
    if (!gone.empty()) gDirty = true;
}

void save() {
    if (!gDirty) return;
    Json list = Json::array();
    for (auto const& gp : shelves()) list.push_back({gp.dim, gp.x, gp.y, gp.z});
    game::db::put(kDbKey, list);
    gDirty = false;
}

void load() {
    shelves().clear();
    if (auto j = game::db::get(kDbKey); j && j->is_array())
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4) shelves().insert(GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    gDirty = false;
}

/// "minecraft:zombie_spawn_egg" -> "minecraft:zombie".
std::string eggEntity(std::string const& itemId) {
    constexpr std::string_view suffix = "_spawn_egg";
    if (itemId.size() <= suffix.size() || !itemId.ends_with(suffix)) return {};
    return itemId.substr(0, itemId.size() - suffix.size());
}

class BookshelfModule {
public:
    void install() {
        static bool once = false;
        if (!once) {
            once = true;
            registerOnce();
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { tickAll(); });
    }
    void uninstall() {
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick = nullptr;
    }

private:
    static void registerOnce() {
        game::db::onWorldReady([] { load(); });
        game::db::onWorldSave([] { save(); });
        game::db::onWorldClosed([] {
            shelves().clear();
            gDirty = false;
        });

        gf::inventory::Handler h;
        h.slots = 6;
        h.get   = [](GlobalPos const& gp, int slot) -> ItemStack {
            auto* region = game::regionOf(gp.dim);
            return region ? bookAt(docAt(*region, game::blockPos(gp)), slot) : items::empty();
        };
        h.set = [](GlobalPos const& gp, int slot, ItemStack const& stack) {
            if (auto* region = game::regionOf(gp.dim)) setBook(*region, game::blockPos(gp), slot, stack);
        };
        h.insert = [](GlobalPos const& gp, ItemStack const& stack, bool simulate) -> ItemStack {
            // ChiseledBookShelfBlockEntity#canPlaceItem: a book into an empty slot, one at a time
            auto* region = game::regionOf(gp.dim);
            if (!region || items::isEmpty(stack) || !isBook(items::idOf(stack))) return stack;
            BlockPos pos = game::blockPos(gp);
            Json     doc = docAt(*region, pos);
            for (int i = 0; i < 6; ++i) {
                if (!items::isEmpty(bookAt(doc, i))) continue;
                if (!simulate) setBook(*region, pos, i, stack);
                return items::withCount(stack, items::countOf(stack) - 1);
            }
            return stack;
        };
        h.extract = [](GlobalPos const& gp, int slot, int count, bool simulate) -> ItemStack {
            auto* region = game::regionOf(gp.dim);
            if (!region || count <= 0) return items::empty();
            BlockPos pos  = game::blockPos(gp);
            auto     book = bookAt(docAt(*region, pos), slot);
            if (!items::isEmpty(book) && !simulate) setBook(*region, pos, slot, items::empty());
            return book;
        };
        gf::inventory::registerBlock(kShelf, h);

        // comparator: the last interacted slot + 1
        gf::redstone::registerAnalog(kShelf, [](BlockSource& region, BlockPos const& pos, Block const&) -> std::optional<int> {
            return docAt(region, pos).value("last", -1) + 1;
        });

        gf::blockevents::onPlaced(kShelf, [](BlockSource& region, BlockPos const& pos, Block const&, Actor*) { track(game::globalPos(region, pos)); });
        // burning (onCaughtFire): a spawner shelf turned into fire spawns every book as a burning tome
        gf::blockevents::onRemoved(kShelf, [](BlockSource& region, BlockPos const& pos, Block const& previous) {
            if (!boolState(previous, "spawner")) return;
            auto now = game::blockIdAt(region, pos);
            if (now != "minecraft:fire" && now != "minecraft:soul_fire") return;
            Json sp = Json::object();
            for (int i = 0; i < 6; ++i) {
                if (!boolState(previous, "slot_" + std::to_string(i) + "_occupied")) continue;
                // the shelf is gone: spawn in front of where it faced (5 tries each)
                int    facing = faceOf(state(previous, "facing"));
                BlockPos front = relative(pos, facing < 2 ? 2 : facing);
                gf::spawn::spawnMob(region, kTome, Vec3{front.x + 0.5F, (float)front.y, front.z + 0.5F}, gf::spawn::SpawnReason::Spawner,
                                    [](Actor& a) { a.setOnFire(10, true); });
            }
        });
        // contents spill when the shelf goes (Containers.dropContents in onRemove)
        bd::onErase([](GlobalPos const& gp, Json const& doc, std::string const& previous) {
            if (previous != kShelf || !doc.contains(kDataKey)) return;
            auto const& mine = doc[kDataKey];
            if (!mine.is_object() || !mine.contains("books")) return;
            std::vector<ItemStack> drops;
            for (auto const& b : mine["books"])
                if (!b.is_null()) drops.push_back(items::fromJson(b));
            if (drops.empty()) return;
            ia::nextTick([gp, drops] {
                if (auto* region = game::regionOf(gp.dim))
                    for (auto const& s : drops) popResource(*region, game::blockPos(gp), s);
            });
        });

        // breaking a spawner shelf: the Death Tome's death sound and 20 poof particles
        ia::onDestroyed({.block = kShelf}, [](ia::DestroyContext const& c, bool destroyed) {
            if (!destroyed || !boolState(c.block, "spawner")) return;
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return;
            BlockPos pos = game::blockPos(c.pos);
            playAt(*region, pos, "entity.twilightforest.death_tome.death", 1.0F, 1.0F);
            for (int i = 0; i < 20; ++i)
                particle(*region, "minecraft:poof", Vec3{pos.x + 0.5F, (float)pos.y, pos.z + 0.5F},
                         Vec3{0.15F * randomFloat(-1.0F, 1.0F), 0.15F * randomFloat(-1.0F, 1.0F), 0.15F * randomFloat(-1.0F, 1.0F)});
        });

        ia::onUseOn({.block = kShelf}, [](ia::UseOnContext const& c) {
            auto* region = game::regionOf(c.pos.dim);
            if (!region) return ia::Result::Pass;
            BlockPos pos = game::blockPos(c.pos);
            track(c.pos);
            // spawn eggs always reach the shelf (the spawner takes their entity)
            if (auto entity = eggEntity(c.itemId); !entity.empty()) {
                if (!c.firstEvent) return ia::Result::Consume;
                Json doc = docAt(*region, pos);
                bool empty = true;
                for (int i = 0; i < 6; ++i) empty = empty && items::isEmpty(bookAt(doc, i));
                if (empty) return ia::Result::Consume;
                playAt(*region, pos, "block.twilightforest.chiseled_bookshelf.convert", 0.35F, 0.6F + randomFloat() * 0.4F);
                Json sp           = doc.value("spawner", Json::object());
                sp["entity"]      = entity;
                doc["spawner"]    = sp;
                bd::set(*region, pos, kDataKey, doc);
                consumeOne(c.player, c.item);
                return ia::Result::Consume;
            }
            if (boolState(c.block, "spawner")) return ia::Result::Deny;
            auto slot = hitSlot(c.block, c.face, c.hit);
            if (!slot) return ia::Result::Pass;
            if (!c.firstEvent) return ia::Result::Consume;
            Json doc  = docAt(*region, pos);
            auto book = bookAt(doc, *slot);
            if (items::isEmpty(book)) {
                // ChiseledBookShelfBlock#addBook: only a book item, into an empty slot
                if (!isBook(c.itemId)) return ia::Result::Pass;
                bool enchanted = c.itemId == "minecraft:enchanted_book";
                setBook(*region, pos, *slot, c.item);
                consumeOne(c.player, c.item);
                playAt(*region, pos, enchanted ? "block.chiseled_bookshelf.insert.enchanted" : "block.chiseled_bookshelf.insert", 1.0F, 1.0F);
                return ia::Result::Consume;
            }
            // removeBook: the book goes to the player
            bool enchanted = items::idOf(book) == "minecraft:enchanted_book";
            setBook(*region, pos, *slot, items::empty());
            giveOrDrop(c.player, book);
            playAt(*region, pos, enchanted ? "block.chiseled_bookshelf.pickup.enchanted" : "block.chiseled_bookshelf.pickup", 1.0F, 1.0F);
            return ia::Result::Consume;
        });
    }

    ll::event::ListenerPtr mTick;
};

} // namespace

/// Selftest access (SelfTestsMisc.cpp): ticks the shelf spawner at `gp` once.
void tickShelfForTest(GlobalPos const& gp) {
    if (auto* region = game::regionOf(gp.dim)) tickShelf(*region, gp.dim, game::blockPos(gp));
}

} // namespace gf::feature::lichdecor

GF_REGISTER_MODULE(gf::feature::lichdecor::BookshelfModule);

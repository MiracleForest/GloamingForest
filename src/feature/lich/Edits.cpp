// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerWingRoom.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichPerimeterFence.java
//   src/main/java/gloamingforest/world/components/structures/lichtowerrevamp/LichTowerMagicGallery.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U21 (lich): the server-thread half of the lich tower's markers (Pending.h): the block entity
// contents and the entities upstream's pieces add while the chunk generates. The piece rolls every
// random value as upstream does and queues the result; these appliers write it once the chunk is loaded.

#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/BrewingStandBlockActor.h"
#include "mc/world/level/block/actor/LecternBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/entity/Entity.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/inventory/Inventory.h"
#include "feature/lich/Common.h"
#include "feature/lich/Edits.h"
#include "feature/lich/Lich.h"
#include "feature/lich/Pending.h"
#include "feature/loot/Loot.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::lich::edits {

namespace ge = gf::entity;

namespace {

/// A non-mob actor (leash knot, painting) by id.
Actor* spawnActor(BlockSource& region, std::string const& id, Vec3 const& pos, float yaw = 0.0F) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(id), nullptr, pos, Vec2{0.0F, yaw});
    return owned ? level->addEntity(region, std::move(owned)) : nullptr;
}

/// The zombies tied to a post: zombie uid -> where it is tied. Kept in the world DB (actor unique
/// ids survive a save), so a zombie loaded later is held again without looking for it.
struct Tether {
    int          dim = 0;
    BlockPos     post{0, 0, 0};
    std::int64_t knot    = 0; ///< the leash knot's uid (0: none)
    int          missing = 0; ///< checks in a row its chunk was loaded without it
};
constexpr char const* kTethersKey = "gf:lich:tethers";
struct Tethers {
    std::unordered_map<std::int64_t, Tether> all;
    bool                                     dirty = false;
};
Tethers& tethers() {
    static Tethers t;
    return t;
}

BlockPos posOf(Json const& j) { return BlockPos{j[0].get<int>(), j[1].get<int>(), j[2].get<int>()}; }

void loadTethers() {
    auto& t = tethers();
    if (auto j = game::db::get(kTethersKey); j && j->is_array())
        for (auto const& e : *j) {
            if (!e.is_object() || !e.contains("post") || !e["post"].is_array() || e["post"].size() != 3) continue;
            Tether one{e.value("dim", 0), posOf(e["post"]), e.value("knot", std::int64_t{0}), 0};
            t.all.emplace(e.value("uid", std::int64_t{0}), one); // a zombie tied before the world was ready stays as it is
        }
}

void saveTethers() {
    auto& t = tethers();
    if (!t.dirty) return;
    Json out = Json::array();
    for (auto const& [uid, one] : t.all) out.push_back(Json{{"uid", uid}, {"dim", one.dim}, {"post", {one.post.x, one.post.y, one.post.z}}, {"knot", one.knot}});
    if (out.empty()) game::db::erase(kTethersKey);
    else game::db::put(kTethersKey, out);
    t.dirty = false;
}

/// Mob#dropLeash: the leash knot goes when nothing is tied to it any more.
void untie(Actor* zombie, Tether const& one) {
    if (zombie) {
        zombie->setLeashHolder(ActorUniqueID::INVALID_ID());
        ge::data(*zombie, "lich:tether") = nullptr;
    }
    if (one.knot)
        if (auto* knot = actorById(one.knot); knot && knot->getTypeName() == "minecraft:leash_knot") knot->remove();
}

/// A zombie leashed to a knot on `post` (putZombieTrap / generateBoundZombie): persistent, its
/// leash held by the knot (the client draws the lead from the leash holder). Bedrock zombies have
/// no leashable component, so the unit keeps the tether itself (Mob#tickLeash: pulled back beyond
/// 6 blocks, free beyond 10).
Mob* leashedZombie(BlockSource& region, BlockPos const& post, Vec3 const& at) {
    auto* knot   = spawnActor(region, "minecraft:leash_knot", Vec3{post.x + 0.5f, float(post.y), post.z + 0.5f});
    Mob*  zombie = spawnMob(region, "minecraft:zombie", at, 0.0F);
    if (!zombie) {
        if (knot) knot->remove();
        return nullptr;
    }
    zombie->setPersistent();
    if (knot) zombie->setLeashHolder(knot->getOrCreateUniqueID());
    ge::data(*zombie, "lich:tether") = Json::array({post.x, post.y, post.z});
    tethers().all[uidOf(*zombie)] = Tether{static_cast<int>(region.getDimensionId()), post, knot ? uidOf(*knot) : 0, 0};
    tethers().dirty               = true;
    return zombie;
}

} // namespace

Mob* tieZombie(BlockSource& region, BlockPos const& post, Vec3 const& at) { return leashedZombie(region, post, at); }

std::size_t tetheredCount() { return tethers().all.size(); }

void tickLeashes() {
    static int sweep = 0;
    bool       check = ++sweep >= 100; // every 5 s: is a zombie that is not loaded gone for good?
    if (check) sweep = 0;
    auto& t = tethers();
    for (auto it = t.all.begin(); it != t.all.end();) {
        auto& one = it->second;
        auto* a   = actorById(it->first);
        if (!a) {
            // its chunk loaded three checks in a row without it: it died or despawned unloaded
            if (check) {
                auto* region = game::regionOf(one.dim);
                if (region && region->hasChunksAt(one.post, 2, false) && ++one.missing >= 3) {
                    untie(nullptr, one);
                    it      = t.all.erase(it);
                    t.dirty = true;
                    continue;
                }
                if (region && !region->hasChunksAt(one.post, 2, false)) one.missing = 0;
            }
            ++it;
            continue;
        }
        one.missing  = 0;
        auto const& d = ge::data(*a, "lich:tether");
        if (!a->isAlive() || !d.is_array() || d.size() != 3) {
            untie(a->isAlive() ? a : nullptr, one);
            it      = t.all.erase(it);
            t.dirty = true;
            continue;
        }
        Vec3   knot{one.post.x + 0.5f, float(one.post.y), one.post.z + 0.5f};
        auto   p  = a->getFeetPos();
        double dd = std::sqrt(distSqr(p, knot));
        if (dd > 10.0) {
            // the leash breaks (Leashable#tickLeash: dropLeash(true, true) beyond 10): a lead drops
            untie(a, one);
            if (auto lead = game::items::make("minecraft:lead", 1); lead && ll::service::getLevel())
                ll::service::getLevel()->getSpawner().spawnItem(a->getDimensionBlockSource(), *lead, a, Vec3{p.x, p.y + 0.0f, p.z}, 0);
            it      = t.all.erase(it);
            t.dirty = true;
            continue;
        }
        if (dd > 6.0) {
            // Mob#tickLeash: pulled toward the holder by copySign(dx^2 * 0.4, dx) / distance per axis
            double dx = (knot.x - p.x) / dd, dy = (knot.y - p.y) / dd, dz = (knot.z - p.z) / dd;
            Vec3   v  = a->getPosDelta();
            setMotion(*a, Vec3{float(v.x + std::copysign(dx * dx * 0.4, dx)), float(v.y + std::copysign(dy * dy * 0.4, dy)), float(v.z + std::copysign(dz * dz * 0.4, dz))});
        }
        ++it;
    }
}
void registerEdits() {
    game::db::onWorldReady([] { loadTethers(); });
    game::db::onWorldSave([] { saveTethers(); });
    game::db::onWorldClosed([] { tethers() = Tethers{}; });
    // putTrappableLectern: a lectern with a writable book, or a Death Tome mimic on it
    pending::registerKind("lectern", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        if (game::blockIdAt(region, pos) != "minecraft:lectern") return true; // gone: nothing to fill
        if (d.value("mimic", false)) return spawnLecternTome(region, pos) != nullptr;
        auto* actor = region.getBlockEntity(pos);
        if (!actor || actor->getType() != BlockActorType::Lectern) return false; // the block entity comes a tick later
        auto book = game::items::make("minecraft:writable_book", 1);
        if (!book) return true;
        static_cast<LecternBlockActor*>(actor)->setItem(0, *book);
        actor->refresh(region);
        return true;
    });
    // putBrewingStand: three potions (water / healing / regeneration / strong healing, a quarter
    // splash), 1..5 blaze powder and 10..20 fuel
    pending::registerKind("brewing_stand", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        if (game::blockIdAt(region, pos) != "minecraft:brewing_stand") return true;
        auto* actor = region.getBlockEntity(pos);
        if (!actor || actor->getType() != BlockActorType::BrewingStand) return false;
        auto& stand = *static_cast<BrewingStandBlockActor*>(actor);
        // Bedrock potion data: water 0, healing 21, strong healing 22, regeneration 28
        auto potion = game::items::make(d.value("splash", false) ? "minecraft:splash_potion" : "minecraft:potion", 1, d.value("aux", 0));
        auto blaze  = game::items::make("minecraft:blaze_powder", d.value("blaze", 1));
        if (potion)
            for (int slot = 1; slot <= 3; ++slot) stand.setItem(slot, *potion); // Bedrock: 0 ingredient, 1..3 bottles, 4 fuel
        if (blaze) stand.setItem(4, *blaze);
        stand.mFuelAmount = d.value("fuel", 10);
        stand.mFuelTotal  = 20;
        actor->refresh(region);
        return true;
    });
    // putMasonJar: the jar's loot table (or the item the label names); the jar is U6's container
    pending::registerKind("jar", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        auto h = gf::inventory::find(region, pos);
        if (!h) return true; // no container behind the jar block: nothing to fill (U6 not present)
        core::GlobalPos gp{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z};
        auto            table = d.value("table", std::string());
        auto            item  = d.contains("item") ? game::items::make(d["item"].get<std::string>(), 1) : std::nullopt;
        if (item && h->set) h->set(gp, 0, *item); // the label names an item rather than a table
        else if (!table.empty() && h->setLootTable) h->setLootTable(gp, table, d.value("seed", std::int64_t{0}));
        return true;
    });
    // putChest / LichTowerFoyer's chest: the loot table of the turned chest
    pending::registerKind("container", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        auto id = d.value("block", std::string("minecraft:chest"));
        if (game::blockIdAt(region, pos) != id) return true; // replaced since: no chest to fill
        return gf::loot::placeContainer(region, pos, id, d.value("facing", 2), d.value("table", std::string()), d.value("seed", std::int64_t{0}));
    });
    // putTrappableBookshelf: books (or an enchanted book) in the filled slots of U4's / U22's shelf
    pending::registerKind("shelf", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        auto h = gf::inventory::find(region, pos);
        if (!h || !h->set) return true;
        core::GlobalPos gp{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z};
        auto            level = ll::service::getLevel();
        for (auto const& s : d.value("slots", Json::array())) {
            int  slot    = s.value("slot", 0);
            int  enchant = s.value("enchant", 0);
            auto book    = game::items::make(enchant > 0 ? "minecraft:enchanted_book" : "minecraft:book", 1);
            if (!book) continue;
            // EnchantmentHelper.enchantItem(random, BOOK, 1..40, ...)
            if (enchant > 0 && level) EnchantUtils::randomlyEnchant(level->getIRandom(), *book, enchant, 0, false);
            h->set(gp, slot, *book);
        }
        return true;
    });
    // putSinisterSpawner: the ominous flame, the ominous spawner drops and the scan range
    pending::registerKind("sinister", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        auto s = gf::boss::ext::sinisterSpawner(region, pos);
        if (!s) return true;
        s->particles = {"gloamingforest:ominous_flame"};
        s->lootTable = "gloamingforest:blocks/ominous_spawner_drops";
        if (d.contains("range")) s->spawnRange = d["range"].get<int>();
        s->entityScanRange = d.contains("scan") ? d["scan"].get<int>() : s->spawnRange;
        if (s->potentials.empty() && d.contains("entity")) s->potentials = {{d["entity"].get<std::string>(), 1}};
        gf::boss::ext::setSinisterSpawner(region, pos, *s);
        return true;
    });
    // putZombieTrap / LichPerimeterFence#generateBoundZombie: a zombie leashed to an iron post
    pending::registerKind("leashed_zombie", [](BlockSource& region, BlockPos const&, Json const& d) {
        if (!d.contains("post") || !d.contains("at")) return true;
        auto        post = posOf(d["post"]);
        auto const& a    = d["at"];
        return leashedZombie(region, post, Vec3{a[0].get<float>(), a[1].get<float>(), a[2].get<float>()}) != nullptr;
    });
    // LichTowerMagicGallery: the gallery's magic painting (U22's entity, the variant named after
    // the gallery; its direction the piece's rotation of south)
    pending::registerKind("magic_painting", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        Actor* painting = spawnActor(region, "gloamingforest:magic_painting", Vec3{pos.x + 0.5f, float(pos.y), pos.z + 0.5f});
        if (!painting) return true; // U22's entity is not there: nothing to hang (one warning per id)
        ge::data(*painting, "variant")   = d.value("variant", std::string());
        ge::data(*painting, "direction") = d.value("direction", std::string("south"));
        return true;
    });
}

} // namespace gf::feature::lich::edits

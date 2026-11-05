// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerWingRoom.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichPerimeterFence.java
//   src/main/java/twilightforest/world/components/structures/lichtowerrevamp/LichTowerMagicGallery.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U21 (lich): the server-thread half of the lich tower's markers (Pending.h): the block entity
// contents and the entities TF's pieces add while the chunk generates. The piece rolls every
// random value as TF does and queues the result; these appliers write it once the chunk is loaded.

#include <cmath>
#include <string>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
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
#include "mod/Log.h"

namespace gf::feature::lich::edits {

namespace ge = gf::entity;

namespace {

/// A non-mob actor (leash knot, painting) or a mob, by id.
Actor* spawnActor(BlockSource& region, std::string const& id, Vec3 const& pos, float yaw = 0.0F) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    if (Mob* m = spawnMob(region, id, pos, yaw)) return m;
    auto owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(id), nullptr, pos, Vec2{0.0F, yaw});
    return owned ? level->addEntity(region, std::move(owned)) : nullptr;
}

std::unordered_set<std::int64_t>& tethered() {
    static std::unordered_set<std::int64_t> s;
    return s;
}

BlockPos posOf(Json const& j) { return BlockPos{j[0].get<int>(), j[1].get<int>(), j[2].get<int>()}; }

/// A zombie leashed to a knot on `post` (putZombieTrap / generateBoundZombie): persistent, its
/// leash held by the knot. Bedrock zombies have no leashable component, so the unit keeps the
/// tether itself (Mob#tickLeash: pulled back beyond 6 blocks, free beyond 10).
bool leashedZombie(BlockSource& region, BlockPos const& post, Vec3 const& at) {
    auto* knot   = spawnActor(region, "minecraft:leash_knot", Vec3{post.x + 0.5f, float(post.y), post.z + 0.5f});
    Mob*  zombie = spawnMob(region, "minecraft:zombie", at, 0.0F);
    if (!zombie) {
        if (knot) knot->remove();
        return false;
    }
    zombie->setPersistent();
    if (knot) zombie->setLeashHolder(knot->getOrCreateUniqueID());
    ge::data(*zombie, "lich:tether") = Json::array({post.x, post.y, post.z});
    tethered().insert(uidOf(*zombie));
    return true;
}

} // namespace

void tickTethers(BlockSource& region, int dim, AABB const& around) {
    // finds the tied zombies near the players (also the ones loaded from a save)
    for (auto* a : actorsIn(region, dim, around)) {
        if (!a || !a->isAlive() || a->getTypeName() != "minecraft:zombie") continue;
        auto const& t = ge::data(*a, "lich:tether");
        if (t.is_array() && t.size() == 3) tethered().insert(uidOf(*a));
    }
}

void tickLeashes() {
    for (auto it = tethered().begin(); it != tethered().end();) {
        auto* a = actorById(*it);
        if (!a || !a->isAlive()) {
            it = tethered().erase(it);
            continue;
        }
        auto& t = ge::data(*a, "lich:tether");
        if (!t.is_array() || t.size() != 3) {
            it = tethered().erase(it);
            continue;
        }
        Vec3   knot{t[0].get<int>() + 0.5f, float(t[1].get<int>()), t[2].get<int>() + 0.5f};
        auto   p = a->getFeetPos();
        double d = std::sqrt(distSqr(p, knot));
        if (d > 10.0) {
            t  = nullptr; // the leash breaks (Mob#tickLeash: dropLeash beyond 10)
            it = tethered().erase(it);
            continue;
        }
        if (d > 6.0) {
            // Mob#tickLeash: pulled toward the holder by copySign(dx^2 * 0.4, dx) / distance per axis
            double dx = (knot.x - p.x) / d, dy = (knot.y - p.y) / d, dz = (knot.z - p.z) / d;
            Vec3   v  = a->getPosDelta();
            setMotion(*a, Vec3{float(v.x + std::copysign(dx * dx * 0.4, dx)), float(v.y + std::copysign(dy * dy * 0.4, dy)), float(v.z + std::copysign(dz * dz * 0.4, dz))});
        }
        ++it;
    }
}

void registerEdits() {
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
        s->particles = {"twilightforest:ominous_flame"};
        s->lootTable = "twilightforest:blocks/ominous_spawner_drops";
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
        return leashedZombie(region, post, Vec3{a[0].get<float>(), a[1].get<float>(), a[2].get<float>()});
    });
    // LichTowerMagicGallery: the gallery's magic painting (U22's entity, the variant named after
    // the gallery; its direction the piece's rotation of south)
    pending::registerKind("magic_painting", [](BlockSource& region, BlockPos const& pos, Json const& d) {
        Actor* painting = spawnActor(region, "twilightforest:magic_painting", Vec3{pos.x + 0.5f, float(pos.y), pos.z + 0.5f});
        if (!painting) return true; // U22's entity is not there: nothing to hang (one warning per id)
        ge::data(*painting, "variant")   = d.value("variant", std::string());
        ge::data(*painting, "direction") = d.value("direction", std::string("south"));
        return true;
    });
}

} // namespace gf::feature::lich::edits

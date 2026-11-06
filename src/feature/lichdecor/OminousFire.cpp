// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/block/OminousFireBlock.java
//   src/main/java/twilightforest/events/EntityEvents.java (ominousFireConversion, zombifiedPlayerAttacks)
//   src/main/java/twilightforest/init/TFDataMaps.java (OMINOUS_FIRE)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor): the ominous fire and the conversion of what it kills. A custom block gets none
// of vanilla's BaseFireBlock, so each inherited part is added here:
//   - survival (sturdy top below, no fluid), re-checked on every neighbour change, silent removal;
//   - entities inside: not #minecraft:undead -> 1.0 twilightforest:ominous_fire damage (never
//     ignited); item entities lose 1 health per hit (an item at rest is hit every 4th tick, as
//     Java's ItemEntity only moves then), so an item is gone after 5 hits;
//   - BaseFireBlock#onPlace: an empty obsidian frame in the Overworld or the Nether lights a nether
//     portal (PortalBlock::trySpawnPortal; the fire is cleared first so Bedrock's frame test sees
//     an empty inside, and put back when no portal formed);
//   - a player's break plays minecraft:block.fire.extinguish (level event 1009); the RP removes the
//     break particles / sound;
//   - death by its damage converts: a player becomes a named persistent zombie whose attacks deal
//     ominous fire damage, other entities follow the data map twilightforest:ominous_fire (owned
//     here, rule R7).
// The ambient sound and large smoke (BaseFireBlock#animateTick) are client-side.

#include <cmath>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/PortalBlock.h"

#include "feature/combat/Combat.h"
#include "feature/datamaps/DataMaps.h"
#include "feature/death/Death.h"
#include "feature/entity/Entity.h"
#include "feature/families/BlockEvents.h"
#include "feature/lichdecor/LightablesInternal.h"
#include "feature/spawn/Spawn.h"
#include "feature/tags/Tags.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace gf::feature::lichdecor::light {

namespace {

constexpr char kZombifiedKey[] = "lichdecor:zombified_player"; ///< TFDataAttachments.ZOMBIFIED_PLAYER (the player's name)

bool isFluid(Block const& b) {
    auto const& id = b.getTypeName();
    return id == "minecraft:water" || id == "minecraft:flowing_water" || id == "minecraft:lava" || id == "minecraft:flowing_lava";
}

/// OminousFireBlock#canSurvive.
bool canSurvive(BlockSource& region, BlockPos const& pos) {
    BlockPos below = relative(pos, 0);
    return region.getBlock(below).canProvideFullSupport(1) && !isFluid(region.getExtraBlock(pos)) && !isFluid(region.getLiquidBlock(pos));
}

std::uint64_t now() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::uint64_t>(level->getCurrentTick().tickID) : 0;
}

/// Fire blocks put back after a failed portal try: their placed event is not a new placement.
std::set<GlobalPos>& restored() {
    static std::set<GlobalPos> s;
    return s;
}

Actor* actorByUid(std::int64_t uid) {
    auto level = ll::service::getLevel();
    if (!level || !uid) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

/// OminousFireBlock#entityInside.
void entityInside(BlockSource& region, BlockPos const&, Actor& entity) {
    auto const& type = entity.getTypeName();
    if (type == "minecraft:item") {
        // ItemEntity#hurt: no cooldown, health 5; a resting item runs move() (and so the block
        // check) only when (tickCount + id) % 4 == 0
        auto const& v      = entity.getPosDelta();
        bool        moving = std::abs(v.x) > 1.0E-4F || std::abs(v.z) > 1.0E-4F || std::abs(v.y) > 0.08F;
        if (!moving && (now() + static_cast<std::uint64_t>(entity.getOrCreateUniqueID().rawID)) % 4 != 0) return;
        auto& item = static_cast<ItemActor&>(entity);
        item.mHealth = item.mHealth - 1;
        if (item.mHealth <= 0) entity.remove();
        return;
    }
    if (gf::tags::has(gf::tags::Registry::EntityType, "minecraft:undead", type)) return;
    gf::combat::hurt(entity, 1.0F, kOminousFireDamage);
    (void)region;
}

/// EntityUtil.convertEntity / Mob#convertTo: the new mob at the old one's place, the old one gone.
void convert(Actor& victim, std::string const& to) {
    auto&       region = victim.getDimensionBlockSource();
    Vec3        pos    = victim.getFeetPos();
    Vec2        rot    = victim.getRotation();
    std::string name   = victim.getNameTag();
    Actor*      mob    = gf::spawn::spawnMob(region, to, pos, gf::spawn::SpawnReason::Conversion);
    if (!mob) {
        log().warn("lichdecor: ominous fire could not convert {} into {}", victim.getTypeName(), to);
        return;
    }
    const_cast<Vec2&>(mob->getRotation()) = rot;
    if (!name.empty()) mob->setNameTag(name);
    // the converted mob replaces the dying one (Java discards it right away)
    ia::nextTick([uid = victim.getOrCreateUniqueID().rawID] {
        if (auto* a = actorByUid(uid); a && !a->isPlayer()) a->remove();
    });
}

/// EntityEvents#ominousFireConversion.
bool onLethal(Actor& victim, gf::combat::DamageInfo const& d) {
    if (d.type != kOminousFireDamage) return false;
    if (victim.isPlayer()) {
        auto&  player = static_cast<Player&>(victim);
        auto&  region = victim.getDimensionBlockSource();
        Vec3   feet   = victim.getFeetPos();
        Vec2   rot    = victim.getRotation();
        std::string name = player.getRealName();
        Actor* zombie = gf::spawn::spawnMob(region, "minecraft:zombie", feet, gf::spawn::SpawnReason::Conversion, [&](Actor& a) {
            gf::entity::data(a, kZombifiedKey) = name; // TFDataAttachments.ZOMBIFIED_PLAYER
        });
        if (zombie) {
            const_cast<Vec2&>(zombie->getRotation()) = rot;
            zombie->setNameTag(name); // setCustomName(player.getName())
            if (zombie->hasCategory(ActorCategory::Mob)) static_cast<Mob*>(zombie)->setPersistent();
            gf::entity::data(*zombie, kZombifiedKey) = name;
        }
        return false;
    }
    auto const* map = gf::datamaps::get("twilightforest:ominous_fire", victim.getTypeName());
    if (!map) {
        // Bedrock's villager is villager_v2 (the data map holds Java's ids turned into Bedrock's)
        auto id = victim.getTypeName();
        if (id.size() > 3 && id.ends_with("_v2")) map = gf::datamaps::get("twilightforest:ominous_fire", id.substr(0, id.size() - 3));
    }
    if (!map || !map->is_object()) return false;
    auto to = map->value("transform_to", std::string());
    if (!to.empty()) convert(victim, to);
    return false;
}

/// EntityEvents#zombifiedPlayerAttacks: a zombified player's attacks are ominous fire.
gf::combat::Verdict zombifiedAttacks(gf::combat::DamageInfo& d) {
    if (d.type == kOminousFireDamage || !d.attacker) return gf::combat::Verdict::Pass;
    if (d.attacker->getTypeName() != "minecraft:zombie") return gf::combat::Verdict::Pass;
    auto const& flag = gf::entity::data(*d.attacker, kZombifiedKey);
    if (flag.is_null()) return gf::combat::Verdict::Pass;
    gf::combat::hurt(d.victim, d.amount, kOminousFireDamage, d.attacker, d.direct);
    return gf::combat::Verdict::Cancel;
}

/// BaseFireBlock#onPlace: a nether portal in an empty obsidian frame (Overworld / Nether only).
void onPlaced(BlockSource& region, BlockPos const& pos, Block const&, Actor*) {
    GlobalPos gp{static_cast<int>(region.getDimensionId()), pos.x, pos.y, pos.z};
    if (restored().erase(gp)) return;
    int dim = gp.dim;
    if (dim == 0 || dim == 1) {
        // Bedrock's frame test is written for its own air / fire / portal blocks: clear the spot
        game::clearBlock(region, pos);
        if (PortalBlock::trySpawnPortal(region, pos)) return;
        if (auto const* fire = game::blockWith(kOminousFire)) {
            restored().insert(gp);
            game::setBlock(region, pos, *fire);
        }
    }
    if (!canSurvive(region, pos)) game::clearBlock(region, pos);
}

} // namespace

void registerOminousFire() {
    gf::blockevents::onNeighborChanged(kOminousFire, [](BlockSource& region, BlockPos const& pos, BlockPos const&) {
        if (game::blockIdAt(region, pos) == kOminousFire && !canSurvive(region, pos)) game::clearBlock(region, pos);
    });
    gf::blockevents::onPlaced(kOminousFire, onPlaced);
    gf::blockevents::onEntityInside(kOminousFire, entityInside);
    // BaseFireBlock#playerWillDestroy: level event 1009 (fire extinguish), no break effect
    ia::onDestroyed({.block = kOminousFire}, [](ia::DestroyContext const& c, bool destroyed) {
        if (!destroyed) return;
        if (auto* region = game::regionOf(c.pos.dim))
            playSound(*region, centre(game::blockPos(c.pos)), "minecraft:block.fire.extinguish", 0.5F, 2.6F + (randomFloat() - randomFloat()) * 0.8F);
    });
    gf::death::onLethal(gf::combat::priority::Low, onLethal);
    gf::combat::onIncoming(gf::combat::priority::High, zombifiedAttacks);
}

} // namespace gf::feature::lichdecor::light

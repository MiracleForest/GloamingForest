// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/events/CharmEvents.java
//   src/main/java/gloamingforest/util/GloamingItemStackUtils.java
//   src/main/java/gloamingforest/entity/CharmEffect.java
//   src/main/java/gloamingforest/network/SpawnCharmPacket.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// upstream's death keeping on F10's pipeline (feature/death/Death.h):
//   - the charm of life is a lethal handler (upstream's LivingDeathEvent at HIGHEST);
//   - the charms of keeping are the first keeper: the first stack a dying player's keepers see
//     runs CharmEvents#handleCharmOfKeeping for the whole inventory (core/artifacts applyKeeping:
//     the tiers, scopes and casket rules), takes the kept stacks out of their slots and stores them
//     in "gf:artifacts:kept:<uuid>" (upstream's GloamingCharmInventory) with the consumed charm (CharmStack);
//     F10's lowest keeper then keeps #gloamingforest:kept_on_death as upstream does after the charm;
//   - the remaining drops place the keepsake casket (stockKeepsakeCasket);
//   - on respawn the stored stacks come back (loadNoClear: their slot when empty, else anywhere,
//     else at the feet) and the charm animation plays.
// The charm animation (SpawnCharmPacket): a gloamingforest:charm_effect entity circling the
// player's eyes (radius 0.75, angle tick / 10 + pi) with item particles for 200 ticks, the charm
// sound at the player (1.5, 1.0); with the "artifacts" key totemCharmAnimation the totem item
// activation instead (the server's copy of upstream's client key decides).
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/ActorEventPacket.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorEvent.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/BaseAttributeMap.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/death/Death.h"
#include "feature/entity/Entity.h"
#include "feature/fx/Fx.h"
#include "feature/net/Net.h"
#include "feature/stats/Stats.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace items = game::items;
namespace ca    = core::artifacts;

namespace {

constexpr char kKeptDataKey[] = "artifacts:charm";

std::string keptKey(Player const& p) { return "gf:artifacts:kept:" + p.getUuid().asString(); }

SharedTypes::Legacy::ArmorSlot bedrockArmor(int javaIndex) { return static_cast<SharedTypes::Legacy::ArmorSlot>(3 - javaIndex); }

ItemStack const& stackAt(Player& p, ca::SlotRef const& r) {
    switch (r.area) {
    case ca::SlotRef::Area::Main: return p.getInventory().getItem(r.index);
    case ca::SlotRef::Area::Armor: return p.getArmor(bedrockArmor(r.index));
    default: return p.getOffhandSlot();
    }
}

void setAt(Player& p, ca::SlotRef const& r, ItemStack const& s) {
    switch (r.area) {
    case ca::SlotRef::Area::Main: p.getInventory().setItem(r.index, s); break;
    case ca::SlotRef::Area::Armor: p.setArmor(bedrockArmor(r.index), s); break;
    default: p.setOffhandSlot(s); break;
    }
}

std::string slotName(ca::SlotRef const& r) {
    switch (r.area) {
    case ca::SlotRef::Area::Main: return "inv:" + std::to_string(r.index);
    case ca::SlotRef::Area::Armor: return "armor:" + std::to_string(r.index);
    default: return "offhand";
    }
}

std::optional<ca::SlotRef> slotOf(std::string const& s) {
    if (s.rfind("inv:", 0) == 0) return ca::SlotRef{ca::SlotRef::Area::Main, std::atoi(s.c_str() + 4)};
    if (s.rfind("armor:", 0) == 0) return ca::SlotRef{ca::SlotRef::Area::Armor, std::atoi(s.c_str() + 6)};
    if (s == "offhand") return ca::SlotRef{ca::SlotRef::Area::Offhand, 0};
    return std::nullopt;
}

std::vector<ca::SlotRef> allSlots() {
    std::vector<ca::SlotRef> out;
    for (int i = 0; i < 36; ++i) out.push_back({ca::SlotRef::Area::Main, i});
    for (int i = 0; i < 4; ++i) out.push_back({ca::SlotRef::Area::Armor, i});
    out.push_back({ca::SlotRef::Area::Offhand, 0});
    return out;
}

ca::Inventory snapshot(Player& p) {
    ca::Inventory inv;
    for (auto const& r : allSlots()) {
        auto const& s = stackAt(p, r);
        ca::Slot    slot{items::isEmpty(s) ? std::string() : items::idOf(s), items::isEmpty(s) ? 0 : items::countOf(s)};
        if (r.area == ca::SlotRef::Area::Main) inv.main[static_cast<std::size_t>(r.index)] = slot;
        else if (r.area == ca::SlotRef::Area::Armor) inv.armor[static_cast<std::size_t>(r.index)] = slot;
        else inv.offhand = slot;
    }
    inv.selected = p.getSelectedItemSlot();
    return inv;
}

ca::Slot const& coreSlot(ca::Inventory const& inv, ca::SlotRef const& r) {
    if (r.area == ca::SlotRef::Area::Main) return inv.main[static_cast<std::size_t>(r.index)];
    if (r.area == ca::SlotRef::Area::Armor) return inv.armor[static_cast<std::size_t>(r.index)];
    return inv.offhand;
}

/// The kept record (upstream's GloamingCharmInventory + CharmStack).
Json loadRecord(Player& p) {
    auto doc = game::db::get(keptKey(p));
    if (doc && doc->is_object()) return *doc;
    return Json{{"items", Json::array()}};
}

void addKept(Json& rec, ca::SlotRef const& r, ItemStack const& s) { rec["items"].push_back({{"slot", slotName(r)}, {"item", items::toJson(s)}}); }

void setHealth(Actor& a, float value) {
    auto* map = const_cast<BaseAttributeMap*>(static_cast<BaseAttributeMap const*>(a.getAttributes()));
    (void)map->setCurrentValue(SharedAttributes::HEALTH(), value);
}

void addEffect(Actor& actor, char const* name, int ticks, int amp) {
    auto const* e = MobEffect::getByName(name);
    if (!e) return;
    MobEffectInstance inst(e->mId, EffectDuration{ticks});
    inst.mAmplifier = amp;
    actor.addEffect(inst);
}

bool realSurvivalPlayer(Player& p) { return !p.isClientSide() && !p.isCreative() && !p.isSpectator(); }

/// The death a keeping plan ran for: (player uid, tick).
std::pair<std::int64_t, std::int64_t>& lastPlan() {
    static std::pair<std::int64_t, std::int64_t> p{0, -1};
    return p;
}

/// CharmEvents#handleCharmOfKeeping for the whole inventory (once per death).
void keepWithCharms(Player& player) {
    ca::Inventory inv    = snapshot(player);
    ca::Inventory before = inv;
    auto          result = ca::applyKeeping(inv);
    if (result.tier == 0) return;
    Json rec   = loadRecord(player);
    int  kept  = 0;
    for (auto const& r : allSlots()) {
        auto const& k   = coreSlot(result.kept, r);
        auto const& now = coreSlot(inv, r);
        auto const& was = coreSlot(before, r);
        if (k.empty() && now == was) continue;
        ItemStack original = stackAt(player, r);
        if (!k.empty()) {
            addKept(rec, r, items::withCount(original, k.count));
            ++kept;
        }
        setAt(player, r, now.empty() ? items::empty() : items::withCount(original, now.count));
    }
    if (result.consumedFrom) rec["charm"] = coreSlot(before, *result.consumedFrom).id;
    game::db::put(keptKey(player), rec);
    log().info("artifacts: {}'s charm of keeping {} kept {} stack(s)", player.getRealName(), result.tier, kept);
}

/// CharmEvents#stockKeepsakeCasket on the drops left after the keepers (`drops` follow the
/// non-empty slots in allSlots order).
void stockCasket(Player& player, std::vector<ItemStack>& drops) {
    // the drops by core slot index (main i, armour 36 + Java index, off-hand 40)
    auto indexOf = [](ca::SlotRef const& r) {
        return r.area == ca::SlotRef::Area::Main ? r.index : r.area == ca::SlotRef::Area::Armor ? 36 + r.index : 40;
    };
    std::vector<ItemStack*> bySlot(41, nullptr);
    ca::Inventory           inv;
    std::size_t             k = 0;
    // F10's slot order: main 0..35, the armour by Bedrock slot (head .. feet: Java 3 .. 0), the off-hand
    std::vector<ca::SlotRef> all;
    for (int i = 0; i < 36; ++i) all.push_back({ca::SlotRef::Area::Main, i});
    for (int i = 3; i >= 0; --i) all.push_back({ca::SlotRef::Area::Armor, i});
    all.push_back({ca::SlotRef::Area::Offhand, 0});
    for (auto const& r : all) {
        if (items::isEmpty(stackAt(player, r))) continue;
        if (k >= drops.size()) break;
        auto* s = &drops[k++];
        bySlot[static_cast<std::size_t>(indexOf(r))] = s;
        ca::Slot cs{items::isEmpty(*s) ? std::string() : items::idOf(*s), items::isEmpty(*s) ? 0 : items::countOf(*s)};
        if (r.area == ca::SlotRef::Area::Main) inv.main[static_cast<std::size_t>(r.index)] = cs;
        else if (r.area == ca::SlotRef::Area::Armor) inv.armor[static_cast<std::size_t>(r.index)] = cs;
        else inv.offhand = cs;
    }
    if (ca::hasNonCasketItem(inv)) {
        // the damage of the casket used (CasketDamage), then the casket goes
        ca::Inventory probe = inv;
        auto          from  = ca::consumeOne(probe, kCasket);
        if (!from) return;
        ItemStack* casket = bySlot[static_cast<std::size_t>(indexOf(*from))];
        int        damage = 0;
        if (casket) {
            auto d = items::getData(*casket, kDataCasketDamage);
            if (d.is_number_integer()) damage = d.get<int>();
            casket->remove(1);
            if (items::countOf(*casket) <= 0) *casket = items::empty();
        }
        std::vector<ItemStack> contents;
        for (int src : ca::casketSourceSlots()) {
            if (src < 0 || !bySlot[static_cast<std::size_t>(src)]) contents.push_back(items::empty());
            else contents.push_back(*bySlot[static_cast<std::size_t>(src)]);
        }
        if (!placeCasket(player, contents, damage)) return; // the casket is lost, the rest drops (upstream)
        for (auto& d : drops) d = items::empty();
        return;
    }
    // nothing but caskets left: they go into the kept inventory
    Json rec = loadRecord(player);
    int  n   = 0;
    for (auto* s : bySlot) {
        if (!s || items::isEmpty(*s) || items::idOf(*s) != kCasket) continue;
        rec["items"].push_back({{"slot", "any"}, {"item", items::toJson(*s)}});
        *s = items::empty();
        ++n;
    }
    if (n > 0) {
        game::db::put(keptKey(player), rec);
        log().info("artifacts: {} keeps {} keepsake casket stack(s) (nothing to store)", player.getRealName(), n);
    }
}

/// CharmEvents#returnStoredItems.
void returnStored(Player& player) {
    auto doc = game::db::get(keptKey(player));
    if (!doc || !doc->is_object()) return;
    game::db::erase(keptKey(player));
    std::vector<ItemStack> blocked;
    int                    n = 0;
    for (auto const& e : doc->value("items", Json::array())) {
        ItemStack s = items::fromJson(e.value("item", Json{}));
        if (items::isEmpty(s)) continue;
        ++n;
        auto r = slotOf(e.value("slot", std::string{}));
        if (r && items::isEmpty(stackAt(player, *r))) setAt(player, *r, s);
        else blocked.push_back(s);
    }
    for (auto& s : blocked)
        if (!player.add(s) && !items::isEmpty(s)) player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), s, nullptr, player.getPosition(), 0);
    player.sendInventory(false);
    if (n > 0) log().info("artifacts: {} got {} kept stack(s) back", player.getRealName(), n);
    if (auto charm = doc->value("charm", std::string{}); !charm.empty()) {
        spawnCharm(player, charm, "item.gloamingforest.charm.keep");
        gf::stats::award(player, "gloamingforest:keeping_charms_activated");
    }
}

// ---- the charm animation (CharmEffect) -------------------------------------------------------------

int charmIndex(std::string const& id) {
    if (id == kLife1) return 0;
    if (id == kLife2) return 1;
    if (id == kKeeping1) return 2;
    if (id == kKeeping2) return 3;
    if (id == kKeeping3) return 4;
    return 0;
}

void charmTick(Actor& self) {
    auto& d = gf::entity::data(self, kKeptDataKey);
    if (!d.is_object()) {
        self.remove(); // noSave: an effect from before a reload
        return;
    }
    int  age     = d.value("age", 0) + 1;
    d["age"]     = age;
    auto* orbiter = actorById(d.value("orbiter", std::int64_t{0}));
    if (age > 200 || !orbiter || !orbiter->isAlive() || orbiter->isInvisible()) {
        self.remove();
        return;
    }
    float rotation = static_cast<float>(age) / 10.0F + d.value("offset", 3.14159265F);
    Vec3  feet     = orbiter->getFeetPos();
    float eyeY     = orbiter->getEyePos().y;
    Vec3  at{feet.x + 0.75F * std::cos(rotation), eyeY, feet.z - 0.75F * std::sin(rotation)};
    self.teleportTo(at, false, 0, 1, false);
    auto& r = randomOf(self);
    Vec3  p{static_cast<float>(at.x + 0.25 * (r.nextDouble() - r.nextDouble())), static_cast<float>(at.y + 0.25 * (r.nextDouble() - r.nextDouble())),
           static_cast<float>(at.z + 0.25 * (r.nextDouble() - r.nextDouble()))};
    particle(self, "minecraft:item", p, Vec3{0.0F, 0.2F, 0.0F}, Json{{"item", d.value("item", std::string{kLife1})}});
}

} // namespace

bool handleCharmOfLife(Player& player) {
    ca::Inventory inv = snapshot(player);
    auto          two = ca::consumeOne(inv, kLife2);
    std::optional<ca::SlotRef> one;
    if (!two) one = ca::consumeOne(inv, kLife1);
    auto from = two ? two : one;
    if (!from) return false;
    ItemStack s = stackAt(player, *from);
    s.remove(1);
    setAt(player, *from, items::countOf(s) > 0 ? s : items::empty());
    if (one) {
        setHealth(player, 8.0F);
        addEffect(player, "regeneration", 100, 0);
    } else {
        setHealth(player, static_cast<float>(player.getMaxHealth()));
        addEffect(player, "regeneration", 600, 3);
        addEffect(player, "resistance", 600, 0);
        addEffect(player, "fire_resistance", 600, 0);
    }
    player.sendInventory(false);
    spawnCharm(player, one ? kLife1 : kLife2, "item.gloamingforest.charm.life");
    gf::stats::award(player, "gloamingforest:life_charms_activated");
    log().info("artifacts: {} was saved by a {}", player.getRealName(), one ? kLife1 : kLife2);
    return true;
}

void spawnCharm(Player& player, std::string const& charmId, std::string const& sound) {
    gf::fx::soundTo(player, sound, 1.5F, 1.0F);
    if (net::isCompanion(player)) (void)net::send(player, kChanCharm, Json{{"item", charmId}, {"sound", sound}});
    if (config().totemCharmAnimation) {
        // GameRenderer#displayItemActivation: Bedrock's totem activation of the player
        auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::ActorEvent);
        if (packet) {
            auto& pkt          = static_cast<ActorEventPacket&>(*packet);
            pkt.mRuntimeId     = player.getRuntimeID();
            pkt.mEventId       = ActorEvent::TalismanActivate;
            pkt.mData          = 0;
            packet->sendTo(player);
        }
        return;
    }
    auto level = ll::service::getLevel();
    if (!level) return;
    auto  feet = player.getFeetPos();
    Vec3  at{feet.x + 0.75F, player.getEyePos().y, feet.z};
    auto  owned = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(std::string(kCharmEffect)), &player, at, Vec2{0, 0});
    if (!owned) return;
    auto* e = level->addEntity(player.getDimensionBlockSource(), std::move(owned));
    if (!e) return;
    gf::entity::data(*e, kKeptDataKey) = Json{{"orbiter", uidOf(player)}, {"item", charmId}, {"offset", 3.14159265F}, {"age", 0}};
    gf::entity::setProperty(*e, "gloamingforest:charm", charmIndex(charmId));
}

/// A kept record whose stacks all still sit in their slots is a stale copy (the server stopped after
/// the death wrote it and before the player was saved without the stacks): returning it would
/// duplicate them (F10's guard for its own record).
void dropStaleRecord(Player& player) {
    auto doc = game::db::get(keptKey(player));
    if (!doc || !doc->is_object()) return;
    auto list = doc->value("items", Json::array());
    if (list.empty()) return;
    for (auto const& e : list) {
        auto r = slotOf(e.value("slot", std::string{}));
        auto s = items::fromJson(e.value("item", Json{}));
        if (!r || items::isEmpty(s)) return;
        auto const& cur = stackAt(player, *r);
        if (!items::sameItemSameData(cur, s) || items::countOf(cur) != items::countOf(s)) return;
    }
    game::db::erase(keptKey(player));
    log().warn("artifacts: ignored the kept items of {}: they are still in the inventory", player.getRealName());
}

void installCharmListeners(std::vector<ll::event::ListenerPtr>& out) {
    out.push_back(ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
        if (!ev.self().isClientSide()) dropStaleRecord(ev.self());
    }));
}

namespace test {
void keep(Player& player) { keepWithCharms(player); }
void stock(Player& player, std::vector<ItemStack>& drops) { stockCasket(player, drops); }
void returnStored(Player& player) { artifacts::returnStored(player); }
Json kept(Player& player) {
    auto doc = game::db::get(keptKey(player));
    return doc ? *doc : Json{};
}
} // namespace test

void registerCharms() {
    // CharmEvents#applyCharmOfLife (EventPriority.HIGHEST)
    gf::death::onLethal(gf::combat::priority::Highest, [](Actor& victim, gf::combat::DamageInfo const&) {
        if (!victim.isPlayer()) return false;
        auto& p = static_cast<Player&>(victim);
        if (!realSurvivalPlayer(p)) return false;
        return handleCharmOfLife(p);
    });
    // CharmEvents#applyKeepingAndCasket (EventPriority.HIGH): the charms plan the whole inventory
    // at the first stack the keepers see
    gf::death::registerKeeper(gf::combat::priority::Highest, [](Player& player, ItemStack&) {
        auto now = gameTime();
        auto uid = uidOf(player);
        if (lastPlan() == std::make_pair(uid, now)) return false;
        lastPlan() = {uid, now};
        if (!realSurvivalPlayer(player)) return false;
        keepWithCharms(player);
        return false; // the slots were changed in place
    });
    gf::death::onDropsRemaining([](Player& player, std::vector<ItemStack>& drops) {
        if (!realSurvivalPlayer(player)) return;
        stockCasket(player, drops);
    });
    gf::death::onRespawn([](Player& player) { returnStored(player); });
    gf::entity::registerType(kCharmEffect, {.tick = charmTick});
}

} // namespace gf::feature::artifacts





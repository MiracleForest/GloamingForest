// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/Kobold.java
//   src/main/java/twilightforest/entity/monster/MosquitoSwarm.java
//   net/minecraft/world/entity/Mob.java (vanilla, aiStep's item pick-up)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U16's monsters. Their vanilla goals (float, leap, melee, stroll, look, hurt-by and the player
// target, the run-away-with-bread avoidance) are the BP's behaviours (the kobold's player target and
// avoidance test the bread in its hand); what is TF's own is native here:
//   - the kobold: PanicOnFlockDeathGoal (2.0) and FlockToSameKindGoal (1.0) from F8, SeekBreadGoal,
//     the bread pick-up (Mob#aiStep + Kobold#pickUpItem: one bread in the hand, always dropped,
//     the target forgotten), the munching (Kobold#aiStep: the eating timer by difficulty, the munch
//     sound and the item particles of entity event 45), the PANICKED flag (the
//     twilightforest:panicked property; the sweat drops) and its saved timers;
//   - the mosquito swarm: Hunger on a hit (7 / 15 / 30 s by difficulty).
#include <algorithm>
#include <cmath>
#include <numbers>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/SlotWithDropChance.h"
#include "mc/entity/components/SlotDropChancesComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/AABB.h"

#include "feature/combat/Combat.h"
#include "feature/effects/Effects.h"
#include "feature/entity/Entity.h"
#include "feature/ext/fauna/W0Internals.h"
#include "feature/fauna/Fauna.h"
#include "feature/fx/Fx.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::fauna {

namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;
namespace cf   = gf::core::fauna;

namespace {

using DropSlot                  = SharedTypes::Legacy::SlotWithDropChance;
constexpr float kPreserveDrop    = 2.0f;   ///< Mob.PRESERVE_ITEM_DROP_CHANCE_THRESHOLD
constexpr float kDefaultDrop     = 0.085f; ///< Mob.DEFAULT_EQUIPMENT_DROP_CHANCE

/// #twilightforest:kobold_pacification_breads (minecraft:bread).
bool isBread(std::string const& id) {
    if (id.empty()) return false;
    return id == "minecraft:bread" || gf::tags::has(gf::tags::Registry::Item, "twilightforest:kobold_pacification_breads", id);
}

bool holdsBread(Mob& kobold) { return isBread(game::items::idOf(kobold.getCarriedItem())); }

bool panicked(Actor& kobold) {
    auto const& v = gf::entity::data(kobold, "entity:panicked");
    return v.is_boolean() && v.get<bool>();
}

bool mobGriefing() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    GameRuleId griefing;
    griefing.mValue = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
    return level->getGameRules().getBool(griefing, true);
}

/// Mob#setDropChance(MAINHAND, 2.0): the held item always drops, undamaged.
void preserveMainHand(Mob& mob) {
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
    auto it = std::find_if(table.begin(), table.end(), [](auto const& e) { return e.mSlot == DropSlot::Mainhand; });
    if (it == table.end()) it = table.begin();
    it->mSlot       = DropSlot::Mainhand;
    it->mDropChance = kPreserveDrop;
}

/// The bread item actors in the kobold's box inflated by (x, y, z).
std::vector<ItemActor*> breadAround(Mob& kobold, float x, float y, float z) {
    std::vector<ItemActor*> out;
    auto const&             bb = kobold.getAABB();
    AABB                    box{Vec3{bb.min.x - x, bb.min.y - y, bb.min.z - z}, Vec3{bb.max.x + x, bb.max.y + y, bb.max.z + z}};
    for (auto* a : fe::actorsIn(kobold.getDimensionBlockSource(), static_cast<int>(kobold.getDimensionId()), box)) {
        if (!a || !a->isType(ActorType::ItemEntity) || static_cast<bool>(a->mRemoved)) continue;
        auto* item = static_cast<ItemActor*>(a);
        if (isBread(game::items::idOf(item->item()))) out.push_back(item);
    }
    return out;
}

/// Kobold.SeekBreadGoal (inspired by Fox.FoxSearchForItemsGoal).
class SeekBreadGoal final : public gf::entity::Goal {
public:
    explicit SeekBreadGoal(Mob& mob) : mUid(mob.getOrCreateUniqueID().rawID) { flags = Move; }
    bool canUse() override {
        auto* m = self();
        if (!m || panicked(*m)) return false;
        if (fe::randomOf(*m).nextInt(10) != 0) return false;
        return !breadAround(*m, 8.0f, 8.0f, 8.0f).empty() && game::items::isEmpty(m->getCarriedItem());
    }
    void tick() override {
        auto* m = self();
        if (!m) return;
        auto list = breadAround(*m, 8.0f, 8.0f, 8.0f);
        if (game::items::isEmpty(m->getCarriedItem()) && !list.empty()) {
            mobs::moveTo(*m, list[0]->getFeetPos(), 1.2);
            mobs::lookAt(*m, list[0]->getFeetPos(), 10.0f, 40.0f); // LookControl#setLookAt(x, y, z): default speeds
        }
    }
    void start() override {
        auto* m = self();
        if (!m) return;
        auto list = breadAround(*m, 8.0f, 8.0f, 8.0f);
        if (!list.empty()) mobs::moveTo(*m, list[0]->getFeetPos(), 1.2);
    }

private:
    Mob* self() const {
        auto* a = fe::actorById(mUid);
        return a ? fe::asMob(*a) : nullptr;
    }
    std::int64_t mUid;
};

void addKoboldGoals(Mob& kobold) {
    if (nativeGoalsPresent(kobold)) return;
    gf::entity::addGoal(kobold, 1, gf::entity::goals::panicOnFlockDeath(kobold, 2.0));
    gf::entity::addGoal(kobold, 2, std::make_shared<SeekBreadGoal>(kobold));
    gf::entity::addGoal(kobold, 5, gf::entity::goals::flockToSameKind(kobold, 1.0));
}

/// Kobold#spawnItemParticles(stack, 8) with #getItemParticleVectors (entity event 45, FOX_EAT).
void munchParticles(Mob& kobold, std::string const& itemId) {
    auto& r     = rng();
    float pitch = kobold.getRotation().x * std::numbers::pi_v<float> / 180.0f;
    float yaw   = kobold.getYHeadRot() * std::numbers::pi_v<float> / 180.0f;
    auto  rot   = [&](Vec3 v) {
        // Vec3#xRot(-pitch) then #yRot(-yaw)
        float c = std::cos(-pitch), s = std::sin(-pitch);
        v       = Vec3{v.x, v.y * c + v.z * s, v.z * c - v.y * s};
        c       = std::cos(-yaw);
        s       = std::sin(-yaw);
        return Vec3{v.x * c + v.z * s, v.y, v.z * c - v.x * s};
    };
    auto eye = kobold.getEyePos();
    for (int i = 0; i < 8; ++i) {
        Vec3  vel = rot(Vec3{(r.nextFloat() - 0.5f) * 0.1f, static_cast<float>(r.nextDouble() * 0.1 + 0.1), 0.0f});
        float d0  = -r.nextFloat() * 0.6f - 0.3f;
        Vec3  at  = rot(Vec3{(r.nextFloat() - 0.5f) * 0.3f, d0, 0.6f});
        at        = Vec3{at.x + kobold.getFeetPos().x, at.y + eye.y, at.z + kobold.getFeetPos().z};
        gf::fx::particle(kobold.getDimension(), "minecraft:item", at, Vec3{vel.x, vel.y + 0.05f, vel.z}, 1, {{"item", itemId}});
    }
}

void koboldTick(Actor& self) {
    applyJavaEyeHeight(self);
    auto* kobold = fe::asMob(self);
    if (!kobold) return;
    addKoboldGoals(*kobold);
    bool panic = panicked(self);
    gf::entity::setProperty(self, "twilightforest:panicked", panic);
    if (!self.isAlive()) return;
    auto& r = rng();
    // the sweat drops (client side in TF; sent as particles here)
    if (panic) {
        float w   = 0.8f;
        auto  p   = self.getFeetPos();
        float eye = self.getEyePos().y;
        for (int i = 0; i < 2; ++i)
            gf::fx::particle(self.getDimension(), "minecraft:splash",
                             Vec3{p.x + static_cast<float>(r.nextDouble() - 0.5) * w * 0.5f, eye, p.z + static_cast<float>(r.nextDouble() - 0.5) * w * 0.5f});
    }
    // Mob#aiStep: pick up loot (canPickUpLoot, the mobGriefing rule, reach 1, 0, 1)
    if (mobGriefing())
        for (auto* item : breadAround(*kobold, 1.0f, 0.0f, 1.0f)) {
            int delay = item->mPickupDelay;
            if (static_cast<bool>(item->mRemoved) || game::items::isEmpty(item->item()) || delay > 0) continue;
            if (koboldPickUp(*kobold, *item)) break;
        }
    // bread munching
    if (!holdsBread(*kobold)) return;
    auto t = koboldTimers(self);
    ++t.lastEatenBread;
    if (t.eatingTime > 0) t.eatingTime--;
    std::string held = game::items::idOf(kobold->getCarriedItem());
    if (!panic) { // canEat: a food item, not panicked
        if (t.eatingTime <= 0) {
            // ItemStack#finishUsingItem -> LivingEntity#eat: the eating sound, the stack consumed
            gf::fx::sound(self.getDimension(), "entity.twilightforest.kobold.munch", self.getFeetPos(), 1.0f, 1.0f + (r.nextFloat() - r.nextFloat()) * 0.4f);
            kobold->setCarriedItem(game::items::empty());
            kobold->sendInventory(false);
            log().info("fauna: kobold ate its {}", held);
        }
        // every 3 seconds chew some bread
        if (t.lastEatenBread > 60 && r.nextFloat() < 0.1f) {
            gf::fx::sound(self.getDimension(), "entity.twilightforest.kobold.munch", self.getFeetPos(), 0.75f, 0.9f);
            munchParticles(*kobold, held);
            t.lastEatenBread = 0;
        }
    }
    setKoboldTimers(self, t);
}

/// MosquitoSwarm#doHurtTarget: Hunger on the living target of a successful melee hit.
void mosquitoBite(gf::combat::DamageInfo const& d) {
    if (!d.attacker || d.attacker->getTypeName() != kMosquitoSwarm) return;
    if (d.direct && d.direct != d.attacker) return;
    if (!d.victim.isAlive() || !fe::asMob(d.victim)) return;
    gf::effects::apply(d.victim, "minecraft:hunger", cf::mosquitoHungerSeconds(difficulty()) * 20, 0);
}

} // namespace

KoboldTimers koboldTimers(Actor& kobold) {
    auto const& j = gf::entity::data(kobold, "fauna:kobold");
    KoboldTimers t;
    if (j.is_object()) {
        t.eatingTime     = j.value("EatingTimeLeft", 0);
        t.lastEatenBread = j.value("TimeSinceBreadLastEaten", 0);
    }
    return t;
}

void setKoboldTimers(Actor& kobold, KoboldTimers timers) {
    gf::entity::data(kobold, "fauna:kobold") = {{"EatingTimeLeft", timers.eatingTime}, {"TimeSinceBreadLastEaten", timers.lastEatenBread}};
}

bool koboldPickUp(Mob& kobold, Actor& itemActor) {
    if (!itemActor.isType(ActorType::ItemEntity)) return false;
    auto& item  = static_cast<ItemActor&>(itemActor);
    auto  stack = item.item();
    // Kobold#canHoldItem: an empty hand, bread, not panicked
    if (!game::items::isEmpty(kobold.getCarriedItem()) || !isBread(game::items::idOf(stack)) || panicked(kobold)) return false;
    int n = game::items::countOf(stack);
    if (n > 1) {
        // dropItemStack(itemstack.split(i - 1))
        if (auto level = ll::service::getLevel())
            level->getSpawner().spawnItem(kobold.getDimensionBlockSource(), game::items::withCount(stack, n - 1), nullptr, kobold.getFeetPos(), 0);
    }
    kobold.setCarriedItem(game::items::withCount(stack, 1));
    preserveMainHand(kobold);
    kobold.sendInventory(false);
    item.remove();
    setKoboldTimers(kobold, {.eatingTime = cf::koboldEatDelay(difficulty()) + rng().nextInt(600), .lastEatenBread = 1});
    kobold.setTarget(nullptr);
    log().info("fauna: kobold picked up {}", game::items::idOf(stack));
    return true;
}

void installMonsters() {
    gf::entity::registerType(kKobold, {.onSpawn = [](Actor& self) {
                                           applyJavaEyeHeight(self);
                                           if (auto* m = fe::asMob(self)) addKoboldGoals(*m);
                                       },
                                       .tick = koboldTick});
    gf::entity::registerType(kMosquitoSwarm, {.onSpawn = [](Actor& self) { applyJavaEyeHeight(self); }, .tick = [](Actor& self) { applyJavaEyeHeight(self); }});
    gf::combat::onDealt(gf::combat::priority::Normal, mosquitoBite);
}

void uninstallMonsters() {}

} // namespace gf::feature::fauna

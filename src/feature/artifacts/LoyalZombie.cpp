// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/monster/LoyalZombie.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The zombie scepter's loyal zombie: a tamed zombie (BP behaviours: float, melee, follow owner,
// stroll, look; owner hurt by / owner hurt / hurt by / nearest monster targets) that decays once its
// Strength runs out (2 twilightforest:expired damage every 20 ticks), hits for a fixed 7 and lifts
// its target by 0.2, wears 3 points of armour, and is fed rotten flesh by its owner to renew the
// summon. Its target choice follows Wolf#wantsToAttack.
#include <algorithm>
#include <cmath>

#include "mc/legacy/ActorUniqueID.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/tags/Tags.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

namespace {

constexpr char kBabyKey[] = "artifacts:baby";

unsigned strengthId() {
    static unsigned const id = [] {
        auto const* e = MobEffect::getByName("strength");
        return e ? static_cast<unsigned>(e->mId) : 5u;
    }();
    return id;
}

void renewStrength(Actor& zombie) {
    zombie.removeEffect(static_cast<int>(strengthId()));
    MobEffectInstance inst(strengthId(), EffectDuration{1200});
    inst.mAmplifier = 1;
    zombie.addEffect(inst);
}

bool fireEvent(Actor& a, char const* event) {
    VariantParameterList params{};
    params.mSelf = &a;
    return a.executeEvent(event, params);
}

/// LoyalZombie#aiStep: decay without Strength.
void tick(Actor& self) {
    if (self.getEffect(strengthId()) == nullptr && tickCountOf(self) % 20 == 0) gf::combat::hurt(self, 2.0F, "twilightforest:expired");
}

/// LoyalZombie#interactAt: the owner feeds it rotten flesh.
bool interact(Actor& self, Player& player) {
    auto const& held = mainHand(player);
    if (game::items::idOf(held) != "minecraft:rotten_flesh") return false;
    if (self.getOwnerId().rawID != player.getOrCreateUniqueID().rawID) return false;
    renewStrength(self);
    self.heal(1);
    auto& r = randomOf(self);
    playSound(self, "minecraft:entity.zombie.infect", 1.0F, self.isBaby() ? (r.nextFloat() - r.nextFloat()) * 0.2F + 1.5F : (r.nextFloat() - r.nextFloat()) * 0.2F + 1.0F);
    if (!creative(player)) {
        ItemStack s = held;
        s.remove(1);
        player.setSelectedItem(s);
        player.sendInventory(false);
    }
    log().info("artifacts: {} fed a loyal zombie", player.getRealName());
    return true;
}

/// Wolf#wantsToAttack as LoyalZombie writes it (`owner` may be null).
bool wantsToAttack(Actor& target, Actor* owner) {
    auto const& id = target.getTypeName();
    if (id == "minecraft:creeper" || id == "minecraft:ghast") return false;
    if (id == kLoyalZombie) return !target.isTame() || target.getOwnerId().rawID != (owner ? uidOf(*owner) : 0);
    if (target.isPlayer() && owner && owner->isPlayer()) {
        // Player#canHarmPlayer: the pvp rule (teams aside)
        if (&target == owner) return false;
        GameRuleId pvp;
        pvp.mValue = static_cast<int>(GameRules::GameRulesIndex::Pvp);
        if (!owner->getLevel().getGameRules().getBool(pvp, false)) return false;
    }
    bool horse = id == "minecraft:horse" || id == "minecraft:donkey" || id == "minecraft:mule" || id == "minecraft:llama" || id == "minecraft:trader_llama"
              || id == "minecraft:skeleton_horse" || id == "minecraft:zombie_horse" || id == "minecraft:camel";
    if (horse && target.isTame()) return false;
    return !target.isTame();
}

} // namespace

void tameLoyalZombie(Mob& zombie, Player& owner) {
    fireEvent(zombie, "gf:tame");
    zombie.setOwner(owner.getOrCreateUniqueID());
    zombie.setPersistent();
}

void setLoyalZombieBaby(Mob& zombie, bool baby) {
    if (baby == loyalZombieBaby(zombie)) return;
    fireEvent(zombie, baby ? "gf:as_baby" : "gf:as_adult");
    gf::entity::data(zombie, kBabyKey) = baby;
}

bool loyalZombieBaby(Actor& zombie) {
    auto const& j = gf::entity::data(zombie, kBabyKey);
    return j.is_boolean() && j.get<bool>();
}

void registerLoyalZombie() {
    gf::entity::registerType(kLoyalZombie, {
        .tick     = tick,
        .interact = interact,
    });
    // LoyalZombie#doHurtTarget: a fixed 7 (no Strength bonus) of mob attack
    gf::combat::onIncoming(gf::combat::priority::Normal, [](gf::combat::DamageInfo& d) {
        if (!d.attacker || !isType(*d.attacker, kLoyalZombie) || (d.direct && d.direct != d.attacker)) return gf::combat::Verdict::Pass;
        if (d.type != "minecraft:mob_attack") return gf::combat::Verdict::Pass;
        d.amount = 7.0F;
        return gf::combat::Verdict::Pass;
    });
    // ... and pushes the target up 0.2 (Entity#push)
    gf::combat::onDealt(gf::combat::priority::Normal, [](gf::combat::DamageInfo const& d) {
        if (!d.attacker || !isType(*d.attacker, kLoyalZombie) || d.type != "minecraft:mob_attack" || d.amount <= 0) return;
        auto v = d.victim.getPosDelta();
        setMotion(d.victim, Vec3{v.x, v.y + 0.2F, v.z});
    });
    // Attributes.ARMOR 3 (CombatRules#getDamageAfterAbsorb, toughness 0)
    gf::combat::onIncoming(gf::combat::priority::Low, [](gf::combat::DamageInfo& d) {
        if (!isType(d.victim, kLoyalZombie)) return gf::combat::Verdict::Pass;
        if (gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_armor", d.type)) return gf::combat::Verdict::Pass;
        float const armor = 3.0F;
        float       f1    = std::clamp(armor - d.amount / 2.0F, armor * 0.2F, 20.0F);
        d.amount *= 1.0F - f1 / 25.0F;
        return gf::combat::Verdict::Pass;
    });
    gf::entity::addTargetFilter([](Mob& attacker, Actor& candidate) {
        if (!isType(attacker, kLoyalZombie)) return true;
        Actor* owner = actorById(attacker.getOwnerId().rawID);
        if (owner && &candidate == owner) return false;
        return wantsToAttack(candidate, owner);
    });
}

} // namespace gf::feature::artifacts

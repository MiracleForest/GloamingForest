// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/components/entity/FortificationShieldAttachment.java
//   src/main/java/twilightforest/events/CapabilityEvents.java
//   src/main/java/twilightforest/command/ShieldCommand.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Fortification shields (TF's attachment twilightforest:fortification_shields) on any living
// entity: temporary shields expire one by one every 240 ticks, damage that does not bypass armour
// is cancelled while any shield is left and breaks one outside the invulnerability frames.
// Players keep theirs in the world DB ("gf:artifacts:shields:<uuid>"), other entities in their
// entity data; the client mod draws the HUD from "artifacts.shields", vanilla players see the count
// on the action bar. /twilightforest shield <target> set|add <amount> [<temp>] (permission 2).
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/platform/UUID.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputParameter.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/combat/Combat.h"
#include "feature/entity/Entity.h"
#include "feature/net/Net.h"
#include "feature/stats/Stats.h"
#include "feature/tags/Tags.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace gf::feature::artifacts {

// Command parameter types need external linkage (reflection).
struct ShieldParams {
    CommandSelector<Actor> target;
    int                    amount = 0;
};
struct ShieldTempParams {
    CommandSelector<Actor> target;
    int                    amount = 0;
    bool                   temp   = true;
};

namespace {

using core::artifacts::Shields;

constexpr char kDataKey[] = "artifacts:shields";
constexpr int  kInvulnerableDuration = 20; // LivingEntity#invulnerableDuration

struct Entry {
    Shields shields;
    int     invulnerable = 0; ///< LivingEntity#invulnerableTime for the shield breaks
    bool    player       = false;
    int     lastSent     = -1;
};

std::unordered_map<std::int64_t, Entry>& entries() {
    static std::unordered_map<std::int64_t, Entry> m;
    return m;
}

std::string dbKey(Player const& p) { return "gf:artifacts:shields:" + p.getUuid().asString(); }

Json toJson(Shields const& s) { return Json{{"temporary_shields", s.temporary}, {"permanent_shields", s.permanent}}; }

Shields fromJson(Json const& j) {
    Shields s;
    if (!j.is_object()) return s;
    s.temporary = std::max(0, j.value("temporary_shields", 0));
    s.permanent = std::max(0, j.value("permanent_shields", 0));
    return s;
}

Entry& entryOf(Actor& actor) {
    auto uid = uidOf(actor);
    auto it  = entries().find(uid);
    if (it != entries().end()) return it->second;
    Entry e;
    e.player = actor.isPlayer();
    if (e.player) {
        if (auto doc = game::db::get(dbKey(static_cast<Player&>(actor)))) e.shields = fromJson(*doc);
    } else {
        e.shields = fromJson(gf::entity::data(actor, kDataKey));
    }
    return entries().emplace(uid, e).first->second;
}

/// Syncs the count to the player (client mod HUD / vanilla action bar).
void syncTo(Player& player, Entry& e) {
    int left = e.shields.left();
    if (left == e.lastSent) return;
    e.lastSent = left;
    if (net::isCompanion(player)) {
        (void)net::send(player, kChanShields, Json{{"temporary", e.shields.temporary}, {"permanent", e.shields.permanent}});
        return;
    }
    // vanilla clients: no shield HUD; the count on the action bar while it changes
    actionBar(player, left > 0 ? trFor(player, "gf.artifacts.shields", "Fortification Shields: %s", std::to_string(left)) : std::string());
}

/// FortificationShieldAttachment#setData: persist and sync.
void store(Actor& actor, Entry& e) {
    if (e.player) {
        auto& p = static_cast<Player&>(actor);
        if (e.shields.left() > 0) game::db::put(dbKey(p), toJson(e.shields));
        else game::db::erase(dbKey(p));
        syncTo(p, e);
    } else {
        gf::entity::data(actor, kDataKey) = e.shields.left() > 0 ? toJson(e.shields) : Json{};
    }
}

/// FortificationShieldAttachment#addShieldBreakParticles.
void shieldBreakParticles(Actor& entity, Actor* attacker, Actor* direct) {
    auto&               r = randomOf(entity);
    std::optional<Vec3> src;
    if (direct) src = isLiving(*direct) ? direct->getEyePos() : direct->getPosition();
    if (direct && !isLiving(*direct) && attacker && isLiving(*attacker)) src = attacker->getEyePos(); // TraceableEntity#getOwner
    auto feet = entity.getFeetPos();
    if (src) {
        Vec3   centre{feet.x, static_cast<float>(feet.y + bbHeight(entity) * 0.65), feet.z};
        double ox = src->x - centre.x, oz = src->z - centre.z;
        double len = std::sqrt(ox * ox + oz * oz);
        if (len > 1e-9) ox /= len, oz /= len;
        else ox = oz = 0;
        Vec3 pos{static_cast<float>(centre.x + ox * 0.55), centre.y, static_cast<float>(centre.z + oz * 0.55)};
        double const size = 0.85;
        for (int j = 0; j < 16; ++j) {
            double h = r.nextDouble() - 0.5;
            double x = size * oz * h;
            double y = size * (r.nextDouble() - 0.5);
            double z = size * ox * -h;
            particle(entity, "twilightforest:shield_break", Vec3{static_cast<float>(pos.x + x), static_cast<float>(pos.y + y), static_cast<float>(pos.z + z)},
                     Vec3{static_cast<float>(x * 0.5), static_cast<float>(y * 0.5), static_cast<float>(z * 0.5)});
        }
    } else {
        Vec3 pos{feet.x, static_cast<float>(feet.y + bbHeight(entity) * 0.65), feet.z};
        for (int j = 0; j < 16; ++j) {
            double x = r.nextDouble() - 0.5;
            double y = (r.nextDouble() - 0.5) * 0.25;
            double z = r.nextDouble() - 0.5;
            particle(entity, "twilightforest:shield_break", Vec3{static_cast<float>(pos.x + x), static_cast<float>(pos.y + y), static_cast<float>(pos.z + z)},
                     Vec3{static_cast<float>(x * 0.33), static_cast<float>(y * 0.33), static_cast<float>(z * 0.33)});
        }
    }
}

void tickAll() {
    auto& m = entries();
    for (auto it = m.begin(); it != m.end();) {
        auto* a = actorById(it->first);
        if (!a || !a->isAlive()) {
            // FORTIFICATION_SHIELDS is not copied on death: a dead player's shields are gone
            if (a && it->second.player) {
                game::db::erase(dbKey(static_cast<Player&>(*a)));
                it->second.shields = {};
                syncTo(static_cast<Player&>(*a), it->second);
            }
            it = m.erase(it); // persisted already; read again when seen
            continue;
        }
        auto& e = it->second;
        if (e.invulnerable > 0) --e.invulnerable;
        bool invulnerable = a->isPlayer() && (static_cast<Player*>(a)->isCreative() || static_cast<Player*>(a)->isSpectator());
        if (e.shields.temporary > 0) {
            int before = e.shields.left();
            if (e.shields.tick(invulnerable, wearsCrown(*a), tickCountOf(*a))) {
                // FortificationShieldAttachment#breakShield(entity, true): the sound, no stat
                auto& r = randomOf(*a);
                playSound(*a, "entity.twilightforest.fortification_shield.expire", 1.0F, (r.nextFloat() - r.nextFloat()) * 0.3F + 1.0F);
            }
            if (e.shields.left() != before) store(*a, e);
        }
        if (e.shields.left() <= 0 && !e.player && e.invulnerable <= 0) {
            it = m.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace

Shields shieldsOf(Actor& actor) { return entryOf(actor).shields; }

void setShields(Actor& actor, int amount, bool temp) {
    auto& e = entryOf(actor);
    e.shields.set(amount, temp);
    store(actor, e);
}

void addShields(Actor& actor, int amount, bool temp) {
    auto& e = entryOf(actor);
    e.shields.add(amount, temp);
    store(actor, e);
}

void breakShield(Actor& actor, bool expired) {
    auto& e = entryOf(actor);
    e.shields.breakOne();
    if (actor.isPlayer() && !expired) gf::stats::award(static_cast<Player&>(actor), "twilightforest:tf_shields_broken");
    auto& r = randomOf(actor);
    playSound(actor, expired ? "entity.twilightforest.fortification_shield.expire" : "entity.twilightforest.fortification_shield.break", 1.0F,
              (r.nextFloat() - r.nextFloat()) * 0.3F + 1.0F);
    store(actor, e);
}

void registerShields() {
    static bool done = false;
    if (done) return;
    done = true;
    // CapabilityEvents#absorbShieldHits (above armour)
    gf::combat::onIncoming(gf::combat::priority::High + 50, [](gf::combat::DamageInfo& d) {
        if (gf::tags::has(gf::tags::Registry::DamageType, "minecraft:bypasses_armor", d.type)) return gf::combat::Verdict::Pass;
        if (!isLiving(d.victim)) return gf::combat::Verdict::Pass;
        auto& e = entryOf(d.victim);
        if (e.shields.left() <= 0) return gf::combat::Verdict::Pass;
        if (e.invulnerable <= 0) {
            breakShield(d.victim, false);
            shieldBreakParticles(d.victim, d.attacker, d.direct);
            entryOf(d.victim).invulnerable = kInvulnerableDuration;
            log().info("artifacts: a fortification shield of {} broke ({} left)", d.victim.getTypeName(), entryOf(d.victim).shields.left());
        }
        return gf::combat::Verdict::Cancel;
    });
    net::onCompanionJoined([](Player& p) {
        auto& e    = entryOf(p);
        e.lastSent = -1;
        syncTo(p, e);
    });
    game::db::onWorldClosed([] { entries().clear(); });
}

void installShieldListeners(std::vector<ll::event::ListenerPtr>& out) {
    out.push_back(ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { tickAll(); }));
    out.push_back(ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
        auto& p = ev.self();
        if (p.isClientSide()) return;
        auto& e    = entryOf(p);
        e.lastSent = -1;
        if (e.shields.left() > 0) syncTo(p, e);
    }));
}

} // namespace gf::feature::artifacts

GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    namespace ar = gf::feature::artifacts;
    // ShieldCommand: requires(permission 2); no feedback, result 1; non-living targets are ignored
    auto run = [](CommandOrigin const& origin, CommandOutput& output, CommandSelector<Actor> const& selector, int amount, bool temp, bool add) {
        if (!gf::commands::isAdmin(origin)) return output.error("commands.generic.unknown", {CommandOutputParameter("shield")});
        auto targets = selector.results(origin);
        if (targets.empty()) return output.error("commands.generic.noTargetMatch");
        if (targets.size() > 1) return output.error("commands.generic.tooManyTargets");
        Actor* a = *targets.begin();
        if (a && ar::isLiving(*a)) {
            if (add) ar::addShields(*a, amount, temp);
            else ar::setShields(*a, amount, temp);
            gf::log().info("artifacts: shield {} {} {} on {}", add ? "add" : "set", amount, temp ? "temporary" : "permanent", a->getTypeName());
        }
        ++output.mSuccessCount; // ShieldCommand: no feedback, result 1
    };
    for (bool add : {false, true}) {
        cmd.overload<ar::ShieldParams>()
            .text("shield")
            .required("target")
            .text(add ? "add" : "set")
            .required("amount")
            .execute([add, run](CommandOrigin const& o, CommandOutput& out, ar::ShieldParams const& p) { run(o, out, p.target, p.amount, true, add); });
        cmd.overload<ar::ShieldTempParams>()
            .text("shield")
            .required("target")
            .text(add ? "add" : "set")
            .required("amount")
            .required("temp")
            .execute([add, run](CommandOrigin const& o, CommandOutput& out, ar::ShieldTempParams const& p) { run(o, out, p.target, p.amount, p.temp, add); });
    }
});




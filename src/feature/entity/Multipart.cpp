// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/TFPart.java
//   src/main/java/twilightforest/network/UpdateTFMultipartPacket.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TFPart multipart as linked actors (spike S4). Java's PartEntity is no real entity: the client
// resolves its hitbox and renderer through three ASM patches (ResolveEntitiesForRendering,
// ResolveEntityRenderer, SendDirtyEntityData). On Bedrock every part is a real actor:
//   - its type is `<parent id>_part` when the parent's unit declares one (with an integer
//     property `<ns>:part` = the part's index, for its geometry / render controller), otherwise
//     F8's invisible hitbox `gloamingforest:part`; so picking, rendering and data sync are the
//     engine's own (the three ASM patches have nothing left to do);
//   - the parent moves it to `offset(parent)` every tick (the engine interpolates on clients);
//   - its size is the actor's collision box (TFPart#setSize -> Actor::synchronousSetSize);
//   - a player's hit on it becomes the player's hit on the parent (TFPart#hurt ->
//     attackEntityFromPart), other damage is forwarded through gf::combat; right-clicks go to the
//     parent's interact (TFPart#interact);
//   - it is never saved (Java's parts are not either): the parent's onSpawn adds them again.
// Companion clients get the part list on "entity.multipart" (UpdateTFMultipartPacket).

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/ActorInfo.h"
#include "mc/world/actor/ActorInfoRegistry.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyGroup.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "feature/combat/Combat.h"
#include "feature/common/Interactions.h"
#include "feature/entity/EntityInternal.h"
#include "feature/ext/entity/EntityExt.h"
#include "feature/net/Net.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::entity {

namespace ia = gf::interactions;
namespace ge = gf::entity;

namespace {

constexpr char const* kGenericPart = "gloamingforest:part";

struct MultipartState {
    std::vector<std::int64_t>        toRemove; ///< part actors to remove on the next level tick
    std::unordered_set<std::string>  known;    ///< entity identifiers of the level (part types)
    bool                             knownLoaded = false;
    std::vector<ia::HandlerId>       handlers;
    std::string                      partHit;  ///< the part whose hit is being forwarded
    int                              forwarding = 0;
};
MultipartState& M() {
    static MultipartState s;
    return s;
}

/// Sets the part id of a forwarded hit for the duration of a scope (Behaviour::hurt reads it).
struct PartHitScope {
    std::string previous;
    explicit PartHitScope(std::string const& part) : previous(M().partHit) {
        M().partHit = part;
        ++M().forwarding;
    }
    ~PartHitScope() {
        M().partHit = previous;
        --M().forwarding;
    }
};

bool entityTypeExists(std::string const& id) {
    auto& m = M();
    if (!m.knownLoaded) {
        auto level = ll::service::getLevel();
        if (!level) return false;
        if (auto* infos = level->getActorInfoRegistry()) {
            for (auto const& info : infos->getActorInfoList()) {
                auto const& ident = info.mIdentifier.get();
                m.known.insert(ident.mNamespace.get() + ":" + ident.mIdentifier.get());
            }
            m.knownLoaded = true;
        }
    }
    return m.known.contains(id);
}

/// Sets the part index property `<ns>:part` when the part type declares it.
void setIndexProperty(Actor& part, int index) {
    auto const& type = part.getTypeName();
    auto        ns   = type.substr(0, type.find(':'));
    auto        name = ns + ":part";
    auto        props = part.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!props) return;
    if (!props->mPropertyGroup.get()->getPropertyMetadataByString(name)) return;
    ge::setProperty(part, name, index);
}

Actor* spawnPart(Mob& parent, PartRecord& rec) {
    auto  level  = ll::service::getLevel();
    auto& region = parent.getDimensionBlockSource();
    if (!level) return nullptr;
    auto        p    = parent.getFeetPos();
    Vec3        off  = rec.offset ? rec.offset(parent) : Vec3{0, 0, 0};
    Vec3        at{p.x + off.x, p.y + off.y, p.z + off.z};
    std::string type = parent.getTypeName() + "_part";
    if (!entityTypeExists(type)) type = kGenericPart;
    auto owner = level->getActorFactory().createSpawnedActor(ActorDefinitionIdentifier(type), nullptr, at, Vec2{0, parent.getRotation().y});
    if (!owner) return nullptr;
    auto* added = level->addEntity(region, std::move(owner));
    if (!added) return nullptr;
    added->synchronousSetSize(rec.size.width, rec.size.height);
    auto& ps    = stateOf(*added);
    ps.parentId = uidOf(parent);
    ps.partId   = rec.partId;
    rec.actorId = uidOf(*added);
    setIndexProperty(*added, rec.index);
    return added;
}

} // namespace

nlohmann::json partsMessage(Mob& parent, ActorState const& s) {
    nlohmann::json parts = nlohmann::json::array();
    for (auto const& rec : s.parts) {
        auto* a = actorById(rec.actorId);
        parts.push_back({
            {"id",     rec.partId                                                        },
            {"uid",    rec.actorId                                                       },
            {"rid",    a ? static_cast<std::int64_t>(a->getRuntimeID().rawID) : 0LL      },
            {"index",  rec.index                                                         },
            {"width",  rec.size.width                                                    },
            {"height", rec.size.height                                                   },
        });
    }
    return {
        {"parent",    uidOf(parent)                                         },
        {"parentRid", static_cast<std::int64_t>(parent.getRuntimeID().rawID)},
        {"parts",     std::move(parts)                                      },
    };
}

namespace {

/// UpdateTFMultipartPacket: the parent's parts to the companions that see it (a companion that
/// starts seeing it later gets them then: updateViewers).
void syncParts(Mob& parent, ActorState& s) {
    auto msg = partsMessage(parent, s);
    updateViewers(parent, s, [&](Player& p) { gf::net::send(p, "entity.multipart", msg); });
}

PartRecord* recordOf(ge::PartHandle const& h, ActorState** owner = nullptr) {
    auto* s = findState(h.parentId);
    if (!s) return nullptr;
    if (owner) *owner = s;
    for (auto& r : s->parts)
        if (r.partId == h.partId) return &r;
    return nullptr;
}

} // namespace

std::int64_t parentOfPart(Actor const& part) {
    auto* s = findState(uidOf(part));
    return s ? s->parentId : 0;
}

std::string const& currentPartHit() { return M().partHit; }

void followParts(Mob& parent, ActorState& s) {
    bool respawned = false;
    for (auto& rec : s.parts) {
        auto* a = actorById(rec.actorId);
        if (!a || !a->isAlive()) {
            // a part removed by something else (a command): TFPart lives as long as its parent
            a         = spawnPart(parent, rec);
            respawned = a != nullptr;
            if (!a) continue;
        }
        auto p   = parent.getFeetPos();
        Vec3 off = rec.offset ? rec.offset(parent) : Vec3{0, 0, 0};
        // Actor rotation is (pitch, yaw): TFPart's xRot / yRot as the parent set them
        Vec2 rot = rec.rotation ? Vec2{rec.rotation->second, rec.rotation->first} : a->getRotation();
        a->moveTo(Vec3{p.x + off.x, p.y + off.y, p.z + off.z}, rot);
        // TFPart#isInvisible: the parent's (an invisible Naga's segments do not render). Glowing
        // (TFPart#isCurrentlyGlowing) has no Bedrock counterpart: nothing glows on Bedrock.
        if (bool hidden = parent.isInvisible(); a->isInvisible() != hidden) a->setInvisible(hidden);
    }
    if (respawned) syncParts(parent, s);
}

void installMultipart() {
    auto& m = M();
    // TFPart#hurt from a player: the player's hit lands on the parent (attackEntityFromPart)
    m.handlers.push_back(ia::onAttack(
        {},
        [](ia::EntityContext const& c) {
            auto parentId = parentOfPart(c.entity);
            if (!parentId) return ia::Result::Pass;
            auto* parent = actorById(parentId);
            if (!parent || !parent->isAlive()) return ia::Result::Deny;
            auto*        ps = findState(uidOf(c.entity));
            PartHitScope scope(ps ? ps->partId : std::string());
            c.player.attack(*parent, SharedTypes::Legacy::ActorDamageCause::EntityAttack);
            return ia::Result::Consume;
        },
        ia::priority::High
    ));
    // the registrations below have no removal: once per process
    static bool once = false;
    if (std::exchange(once, true)) return;
    // every other damage (projectiles, explosions, ...) through the combat pipeline. Java's parts
    // are no living entities in the level's tick: nothing environmental (suffocation, drowning,
    // falls, fire, contact, freezing, the void, /kill) reaches them, so none of it reaches the
    // parent either (the generic part's damage sensor also refuses those causes, so attacks,
    // projectiles, explosions and magic reach Actor::hurt and this handler without a sensor).
    {
        gf::combat::onIncoming(gf::combat::priority::High, [](gf::combat::DamageInfo& d) {
            auto parentId = parentOfPart(d.victim);
            if (!parentId) return gf::combat::Verdict::Pass;
            static std::unordered_set<std::string> const environmental{
                "minecraft:in_wall",  "minecraft:drown",         "minecraft:fall",          "minecraft:on_fire",
                "minecraft:in_fire",  "minecraft:lava",          "minecraft:hot_floor",     "minecraft:cactus",
                "minecraft:sweet_berry_bush", "minecraft:freeze", "minecraft:starve",       "minecraft:out_of_world",
                "minecraft:fly_into_wall",    "minecraft:campfire", "minecraft:dry_out",    "minecraft:outside_border",
                "minecraft:generic_kill",     "minecraft:stalagmite",
            };
            if (environmental.contains(d.type)) return gf::combat::Verdict::Cancel;
            auto* parent = actorById(parentId);
            if (parent && parent->isAlive() && M().forwarding < 4) {
                auto*        ps = findState(uidOf(d.victim));
                PartHitScope scope(ps ? ps->partId : std::string());
                gf::combat::hurt(*parent, d.amount, d.type, d.attacker, d.direct);
            }
            return gf::combat::Verdict::Cancel;
        });
    }
    addInternalHooks({
        .interesting = [](std::string const& type) { return type == kGenericPart; },
        .tick =
            [](Actor& actor, ActorState& s) {
                if (!s.parentId) return;
                // TFPart lives as long as its parent entity: a dying boss keeps its parts through
                // its death animation (the Hydra's heads die one by one)
                auto*             parent = actorById(s.parentId);
                bool              keep   = parent != nullptr;
                PartRecord const* rec    = nullptr;
                if (keep)
                    if (auto* ps = findState(s.parentId))
                        for (auto const& r : ps->parts)
                            if (r.actorId == uidOf(actor)) rec = &r;
                if (!keep || !rec) {
                    M().toRemove.push_back(uidOf(actor));
                    return;
                }
                // the part's own engine tick (body control) ran just before: the parent's rotation
                // goes back on (TFPart's yRot / xRot are the parent's to set); the engine's look
                // control turns a mob's pitch towards its look target, so it is aimed along the
                // part's own direction
                if (rec->rotation) {
                    float yaw = rec->rotation->first, pitch = rec->rotation->second;
                    const_cast<Vec2&>(actor.getRotation()) = Vec2{pitch, yaw};
                    if (auto* m = asMob(actor)) {
                        double ry = yaw * std::numbers::pi / 180.0, rx = pitch * std::numbers::pi / 180.0;
                        auto   eye = m->getEyePos();
                        Vec3   at{float(eye.x - std::sin(ry) * std::cos(rx) * 8.0), float(eye.y - std::sin(rx) * 8.0), float(eye.z + std::cos(ry) * std::cos(rx) * 8.0)};
                        mobs::lookAt(*m, at, 180.0F, 180.0F);
                    }
                }
            },
        .save = [](Actor const& actor) { return parentOfPart(actor) == 0 && actor.getTypeName() != kGenericPart; },
        // Java's parts are no level entities (PartEntity): they never reach a portal (HydraPart,
        // NagaSegment#canUsePortal false)
        .usePortal = [](Actor const& actor) { return parentOfPart(actor) == 0 && actor.getTypeName() != kGenericPart; },
    });
    onLevelTick([] {
        auto& m = M();
        if (m.toRemove.empty()) return;
        auto ids = std::move(m.toRemove);
        m.toRemove.clear();
        for (auto id : ids)
            if (auto* a = actorById(id)) a->remove();
    });
    // the part lists reach companions that join or come into range later through the entity
    // tracker (updateViewers, Entity.cpp)
    gf::game::db::onWorldClosed([] {
        M().known.clear();
        M().knownLoaded = false;
        M().toRemove.clear();
    });
}

void uninstallMultipart() {
    for (auto id : M().handlers) ia::remove(id);
    M().handlers.clear();
}

} // namespace gf::feature::entity

// ---- contract -----------------------------------------------------------------------------------

namespace gf::entity {

namespace fe = gf::feature::entity;

PartHandle addPart(Mob& mob, std::string const& partId, PartSize size, std::function<Vec3(Mob& parent)> offset) {
    auto& s = fe::stateOf(mob);
    int   index = static_cast<int>(s.parts.size());
    for (auto it = s.parts.begin(); it != s.parts.end(); ++it) {
        if (it->partId != partId) continue;
        index = it->index;
        fe::M().toRemove.push_back(it->actorId);
        s.parts.erase(it);
        break;
    }
    fe::PartRecord rec;
    rec.partId = partId;
    rec.size   = size;
    rec.offset = std::move(offset);
    rec.index  = index;
    if (!fe::spawnPart(mob, rec)) {
        log().warn("entity: could not spawn part {} of {}", partId, mob.getTypeName());
        return {};
    }
    log().info("entity: {} got part {} ({})", mob.getTypeName(), partId, fe::actorById(rec.actorId) ? fe::actorById(rec.actorId)->getTypeName() : std::string("?"));
    s.parts.push_back(std::move(rec));
    fe::syncParts(mob, s);
    return {fe::uidOf(mob), partId};
}

Actor* partActor(PartHandle const& part) {
    auto* rec = fe::recordOf(part);
    return rec ? fe::actorById(rec->actorId) : nullptr;
}

namespace ext {

std::string const& hurtPart() { return fe::currentPartHit(); }

std::vector<PartHandle> parts(Mob& mob) {
    std::vector<PartHandle> out;
    if (auto* s = fe::findState(fe::uidOf(mob)))
        for (auto const& r : s->parts) out.push_back({fe::uidOf(mob), r.partId});
    return out;
}

void removePart(PartHandle const& part) {
    fe::ActorState* s   = nullptr;
    auto*           rec = fe::recordOf(part, &s);
    if (!rec || !s) return;
    fe::M().toRemove.push_back(rec->actorId);
    std::erase_if(s->parts, [&](fe::PartRecord const& r) { return r.partId == part.partId; });
    if (auto* parent = fe::actorById(part.parentId))
        if (auto* mob = fe::asMob(*parent)) fe::syncParts(*mob, *s);
}

void setPartRotation(PartHandle const& part, float yRot, float xRot) {
    auto* rec = fe::recordOf(part);
    if (!rec) return;
    rec->rotation = std::make_pair(yRot, xRot);
}

void setPartSize(PartHandle const& part, PartSize size) {
    fe::ActorState* s   = nullptr;
    auto*           rec = fe::recordOf(part, &s);
    if (!rec) return;
    rec->size = size;
    if (auto* a = fe::actorById(rec->actorId)) a->synchronousSetSize(size.width, size.height);
    if (auto* parent = fe::actorById(part.parentId))
        if (auto* mob = fe::asMob(*parent)) fe::syncParts(*mob, *s);
}

Actor* parentOf(Actor const& part) { return fe::actorById(fe::parentOfPart(part)); }

} // namespace ext

} // namespace gf::entity

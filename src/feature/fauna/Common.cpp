// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/entity/passive/TinyBirdVariant.java
//   src/main/java/twilightforest/entity/passive/DwarfRabbitVariant.java
//   src/main/java/twilightforest/init/custom/TinyBirdVariants.java
//   src/main/java/twilightforest/init/custom/DwarfRabbitVariants.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U16's shared helpers: the random source, biome and difficulty lookups, BP events, the colour and
// variant state of the bighorn, tiny bird and dwarf rabbit, Java's eye heights and the variant
// registries (data/twilightforest/twilight/{tiny_bird,dwarf_rabbit}_variant/*.json, TF's datapack
// registries; the Bedrock index of an entry is its place in the resource pack's texture arrays).
#include "feature/fauna/Fauna.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/vanilla_components/OffsetsComponent.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/phys/AABB.h"

#include "feature/dimension/World.h"
#include "feature/entity/Entity.h"
#include "feature/ext/fauna/W0Internals.h"
#include "feature/tags/Tags.h"
#include "feature/worldgen/Worldgen.h"
#include "game/Compat.h"
#include "mod/Log.h"

namespace gf::feature::fauna {

namespace fe = gf::feature::entity;

std::vector<std::string> const& mobIds() {
    static std::vector<std::string> const ids = {kBoar, kBighorn, kDeer, kPenguin, kTinyBird, kRaven, kSquirrel, kDwarfRabbit, kKobold, kMosquitoSwarm};
    return ids;
}

core::jrand::RandomSource& rng() {
    static core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 20 ^ static_cast<std::int64_t>(std::random_device{}()));
    return r;
}

int difficulty() {
    auto level = ll::service::getLevel();
    return level ? static_cast<int>(level->getDifficulty()) : 2;
}

bool despawnInPeaceful(Actor& actor) {
    if (difficulty() != 0 || !actor.isAlive()) return false;
    fe::removeLater(actor.getOrCreateUniqueID().rawID);
    return true;
}

BlockPos blockPosOf(Actor const& actor) {
    auto p = actor.getFeetPos();
    return BlockPos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y + 1e-4f)), static_cast<int>(std::floor(p.z))};
}

std::string biomeIdAt(Actor& actor) {
    auto pos = blockPosOf(actor);
    if (gf::world::isTwilight(actor.getDimension())) {
        auto id = gf::worldgen::biomeAt(pos);
        if (!id.empty()) return id;
    }
    auto const& name = actor.getDimensionBlockSource().getBiome(pos).mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

bool fireEvent(Actor& actor, std::string const& event) {
    VariantParameterList params{};
    return gf::game::compat::executeEvent(actor, event, params);
}

namespace {

/// tests/golden/mobs.json eyeHeight (Java's 0.85 x height of an adult).
float javaEyeHeight(std::string const& type) {
    static std::map<std::string, float> const eyes = {
        {kBoar, 0.765F},   {kBighorn, 1.105F},     {kDeer, 1.53F},    {kPenguin, 0.765F},      {kTinyBird, 0.255F},
        {kRaven, 0.425F},  {kSquirrel, 0.425F},    {kDwarfRabbit, 0.34F}, {kKobold, 0.935F}, {kMosquitoSwarm, 1.615F},
    };
    auto it = eyes.find(type);
    return it == eyes.end() ? -1.0F : it->second;
}

// ---- variant registries ---------------------------------------------------------------------------

struct Registry {
    std::string                     dir;      ///< "twilight/tiny_bird_variant"
    std::vector<std::string>        order;    ///< the RP texture array order (Bedrock index)
    std::string                     fallback; ///< the synced default
    std::vector<core::fauna::Variant> entries;
    bool                            loaded = false;
};

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

Registry& registryOf(std::string const& entityId) {
    static Registry tiny{"twilight/tiny_bird_variant", {"twilightforest:blue", "twilightforest:brown", "twilightforest:gold", "twilightforest:red"}, "twilightforest:red", {}, false};
    static Registry rabbit{"twilight/dwarf_rabbit_variant", {"twilightforest:brown", "twilightforest:dutch", "twilightforest:white"}, "twilightforest:brown", {}, false};
    return entityId == kTinyBird ? tiny : rabbit;
}

/// A HolderSet<Biome> of the codec: "#tag", an id, or a list of both.
void addBiomes(nlohmann::json const& v, std::vector<std::string>& out) {
    if (v.is_array()) {
        for (auto const& e : v) addBiomes(e, out);
        return;
    }
    if (!v.is_string()) return;
    auto s = v.get<std::string>();
    if (s.starts_with('#')) {
        for (auto const& m : gf::tags::members(gf::tags::Registry::Biome, s.substr(1))) out.push_back(m);
    } else {
        out.push_back(gf::feature::item::data::withNamespace(s));
    }
}

void load(Registry& r) {
    if (r.loaded) return;
    r.loaded = true;
    std::map<std::string, core::fauna::Variant> found;
    for (auto const& f : gf::feature::item::data::list(r.dir)) {
        auto doc = gf::feature::item::data::read(f.path);
        if (!doc.is_object()) continue;
        core::fauna::Variant v;
        v.id      = f.id();
        v.texture = doc.value("texture", std::string{});
        if (doc.contains("biomes")) {
            v.anyBiome = false;
            addBiomes(doc["biomes"], v.biomes);
        }
        found[v.id] = std::move(v);
    }
    for (auto const& id : r.order) {
        auto it = found.find(id);
        if (it != found.end()) {
            r.entries.push_back(std::move(it->second));
            found.erase(it);
        } else {
            // the registry data is missing: TF's bootstrap entry (no biome filter)
            r.entries.push_back(core::fauna::Variant{id, "", true, {}});
        }
    }
    for (auto const& kv : found) log().warn("fauna: variant {} of {} has no texture in the resource pack (skipped)", kv.first, r.dir);
}

} // namespace

void applyJavaEyeHeight(Actor& actor) {
    float eye = javaEyeHeight(actor.getTypeName());
    if (eye < 0.0F) return;
    if (actor.isBaby()) eye *= 0.5F;
    if (auto oc = actor.getEntityContext().tryGetComponent<OffsetsComponent>()) {
        Vec3 e = oc->mEyeOffset;
        if (e.y != eye) oc->mEyeOffset = Vec3{e.x, eye, e.z};
    }
}

bool nativeGoalsPresent(Actor& actor) {
    auto const* s = fe::findState(actor.getOrCreateUniqueID().rawID);
    return s && s->goals && s->goals->size() > 0;
}

bool nativeTargetGoalsPresent(Actor& actor) {
    auto const* s = fe::findState(actor.getOrCreateUniqueID().rawID);
    return s && s->targets && s->targets->size() > 0;
}

bool firstSetUp(Actor& actor) {
    auto& done = gf::entity::data(actor, "fauna:init");
    if (done.is_boolean() && done.get<bool>()) return false;
    done = true;
    return true;
}

int colorOf(Actor const& actor) {
    int c = static_cast<int>(actor.mEntityData->getInt8(static_cast<ushort>(ActorDataIDs::ColorIndex)));
    return c >= 0 && c < core::fauna::kDyeCount ? c : 0;
}

void setColor(Actor& actor, int color) {
    if (color < 0 || color >= core::fauna::kDyeCount) return;
    fireEvent(actor, "set_color_" + std::to_string(color));
}

int variantOf(Actor const& actor) { return actor.getVariant(); }

std::vector<core::fauna::Variant> const& variantsOf(std::string const& entityId) {
    std::lock_guard lock(registryMutex());
    auto&           r = registryOf(entityId);
    load(r);
    return r.entries;
}

int defaultVariant(std::string const& entityId) {
    auto const& v = variantsOf(entityId);
    auto        i = core::fauna::variantIndex(v, registryOf(entityId).fallback);
    return i.value_or(0);
}

void setVariant(Actor& actor, int index) {
    auto const& v = variantsOf(actor.getTypeName());
    if (index < 0 || index >= static_cast<int>(v.size())) return;
    fireEvent(actor, "set_variant_" + std::to_string(index));
    gf::entity::data(actor, "fauna:variant") = v[static_cast<std::size_t>(index)].id;
}

void restoreVariant(Actor& actor) {
    auto const& saved = gf::entity::data(actor, "fauna:variant");
    if (!saved.is_string()) return;
    auto const& v = variantsOf(actor.getTypeName());
    if (auto i = core::fauna::variantIndex(v, saved.get<std::string>()); i && *i != variantOf(actor)) setVariant(actor, *i);
}

bool isSheared(Actor const& actor) { return actor.getStatusFlag(ActorFlags::Sheared); }

std::vector<Actor*> nearbyParents(Actor& baby) {
    std::vector<Actor*> out;
    auto                p   = baby.getFeetPos();
    auto const&         ty  = baby.getTypeName();
    AABB                box{Vec3{p.x - 4.0f, p.y - 2.0f, p.z - 4.0f}, Vec3{p.x + 4.0f, p.y + 3.0f, p.z + 4.0f}};
    for (auto* a : fe::actorsIn(baby.getDimensionBlockSource(), static_cast<int>(baby.getDimensionId()), box))
        if (a && a != &baby && a->isAlive() && !a->isBaby() && a->getTypeName() == ty) out.push_back(a);
    std::sort(out.begin(), out.end(), [&](Actor* a, Actor* b) { return fe::mobs::distanceSqr(*a, p) < fe::mobs::distanceSqr(*b, p); });
    if (out.size() > 2) out.resize(2);
    return out;
}

} // namespace gf::feature::fauna

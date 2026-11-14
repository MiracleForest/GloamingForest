// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/entity/passive/QuestRam.java
//   src/main/java/gloamingforest/entity/ai/goal/QuestRamEatWoolGoal.java
//   src/main/java/gloamingforest/entity/EnforcedHomePoint.java
//   src/main/java/gloamingforest/util/features/FeaturePlacers.java (placeEntity's restriction point)
//   net/minecraft/world/entity/ai/goal/TemptGoal.java (vanilla, with QuestRam#isItemTempting)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The Questing Ram. Its vanilla goals (float, panic 1.38, stroll, look around) are the BP's
// behaviours (assets/pack/bp/entities/questcamps); what is upstream's own is native: the 16-colour quest
// (ColorFlags / Rewarded, saved and synced), feeding by hand (Behaviour::interact) and by dropped
// items (QuestRamEatWoolGoal, priority 2), the tempt goal for the colours it still needs (3), the
// restriction point (AttemptToGoHomeGoal 1.25, priority 5, radius 13), the reward every 70..119
// ticks once all 16 colours are in, and the coloured particles.

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/questcamps/Questcamps.h"
#include "feature/entity/Entity.h"
#include "feature/ext/questcamps/W0Internals.h"
#include "feature/fx/Fx.h"
#include "feature/landmark/Landmark.h"
#include "feature/loot/Loot.h"
#include "feature/net/Net.h"
#include "feature/progression/Progression.h"
#include "feature/questcamps/Questcamps.h"
#include "feature/spawn/Spawn.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace gf::feature::questcamps {

namespace cq   = gf::core::questcamps;
namespace fe   = gf::feature::entity;
namespace mobs = gf::feature::entity::mobs;

namespace {

constexpr char kAmbient[] = "entity.gloamingforest.quest_ram.ambient";
constexpr char kHomesKey[] = "gf:questcamps:homes";
constexpr int  kHomeRadius = 13;

Vec3 feet(Actor const& a) { return a.getFeetPos(); }
int  dimOf(Actor const& a) { return static_cast<int>(a.getDimensionId()); }

// ---- state ---------------------------------------------------------------------------------------------------

Json& ramData(Actor& ram) {
    auto& d = gf::entity::data(ram, kRamData);
    if (!d.is_object()) d = Json{{"ColorFlags", 0}, {"Rewarded", false}};
    return d;
}

/// QuestRam#randomTickDivider (not saved: a loaded ram starts at 0, as Java's constructor).
std::unordered_map<std::int64_t, int>& dividers() {
    static std::unordered_map<std::int64_t, int> m;
    return m;
}

/// Pushes the synced state: the model's segments (every client) and the data (companions).
void publish(Actor& ram) {
    gf::entity::setProperty(ram, kColorProperty, colorFlags(ram));
    gf::entity::sync(ram, kRamData);
}

// ---- restriction points of rams placed by the grove ----------------------------------------------------------

std::mutex& homesMutex() {
    static std::mutex m;
    return m;
}
std::vector<core::GlobalPos>& homes() {
    static std::vector<core::GlobalPos> v;
    return v;
}
void saveHomes() {
    Json list = Json::array();
    {
        std::lock_guard lock(homesMutex());
        for (auto const& h : homes()) list.push_back(Json::array({h.dim, h.x, h.y, h.z}));
    }
    gf::game::db::put(kHomesKey, list);
}

/// FeaturePlacers.placeEntity: a ram that appears on a queued marker takes it as its home.
void claimHome(Mob& ram) {
    auto     p = feet(ram);
    BlockPos at{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    std::optional<core::GlobalPos> home;
    {
        std::lock_guard lock(homesMutex());
        auto&           list = homes();
        for (auto it = list.begin(); it != list.end(); ++it) {
            // the ram stands on its marker (moveTo(pos): the block's center, yaw 0) when it first ticks
            if (it->dim != dimOf(ram) || it->x != at.x || it->y != at.y || it->z != at.z) continue;
            home = *it;
            list.erase(it);
            break;
        }
    }
    if (!home) return;
    gf::entity::setHome(ram, *home, kHomeRadius);
    saveHomes();
}

// ---- particles and sounds --------------------------------------------------------------------------------------

/// QuestRam#getVoicePitch.
float voicePitch(Actor& ram) {
    auto& r = fe::randomOf(ram);
    return (r.nextFloat() - r.nextFloat()) * 0.2F + 0.7F;
}

void playAmbient(Actor& ram) { gf::fx::sound(ram.getDimension(), kAmbient, feet(ram), 1.0F, voicePitch(ram)); }

/// QuestRam#animateAddColor: `iterations` ENTITY_EFFECT particles of the dye's colour around the
/// ram, for the players within 64 blocks of its block center.
void animateAddColor(Actor& ram, int dye, int iterations) {
    auto     p      = feet(ram);
    unsigned colour = 0xFF000000u | cq::textureDiffuse(dye);
    Vec3     center{std::floor(p.x) + 0.5F, std::floor(p.y) + 0.5F, std::floor(p.z) + 0.5F};
    if (fe::playersNear(dimOf(ram), center, 64.0).empty()) return;
    auto& r      = fe::randomOf(ram);
    auto  box    = ram.getAABB();
    float width  = box.max.x - box.min.x;
    float height = box.max.y - box.min.y;
    for (int i = 0; i < iterations; ++i) {
        double x = p.x + (r.nextDouble() - 0.5) * width * 1.5;
        double y = p.y + r.nextDouble() * height * 1.5;
        double z = p.z + (r.nextDouble() - 0.5) * width * 1.5;
        gf::fx::particle(ram.getDimension(), "minecraft:entity_effect", Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}, Vec3{}, 1,
                         Json{{"color", colour}});
    }
}

// ---- goals -----------------------------------------------------------------------------------------------------

Mob* mobOf(std::int64_t uid) {
    auto* a = fe::actorById(uid);
    return a ? fe::asMob(*a) : nullptr;
}

/// QuestRamEatWoolGoal (MOVE, LOOK).
class EatWoolGoal final : public gf::entity::Goal {
public:
    explicit EatWoolGoal(Mob& ram) : mUid(fe::uidOf(ram)) { flags = Move | Look; }

    bool canUse() override {
        auto* ram = mobOf(mUid);
        if (!ram) return false;
        auto  box = ram->getAABB();
        AABB  search{Vec3{box.min.x - 16, box.min.y - 16, box.min.z - 16}, Vec3{box.max.x + 16, box.max.y + 16, box.max.z + 16}};
        std::vector<std::pair<double, std::int64_t>> items;
        for (auto* a : fe::actorsIn(ram->getDimensionBlockSource(), dimOf(*ram), search)) {
            if (!a || !a->isType(ActorType::ItemEntity) || !a->isAlive()) continue;
            if (!(a->isOnGround() || a->isInWater())) continue;
            auto const& stack = static_cast<ItemActor*>(a)->item();
            if (game::items::isEmpty(stack) || !mobs::hasLineOfSight(*ram, *a) || !isItemTempting(*ram, stack)) continue;
            items.emplace_back(mobs::distanceSqr(*ram, *a), fe::uidOf(*a));
        }
        if (items.empty()) return false;
        std::stable_sort(items.begin(), items.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        mTarget = items.front().second;
        return true;
    }
    bool canContinueToUse() override {
        auto* ram  = mobOf(mUid);
        auto* item = mTarget ? fe::actorById(mTarget) : nullptr;
        return ram && ram->isAlive() && !mobs::navigationDone(*ram) && item && item->isAlive() &&
               isItemTempting(*ram, static_cast<ItemActor*>(item)->item());
    }
    void start() override {
        auto* ram  = mobOf(mUid);
        auto* item = mTarget ? fe::actorById(mTarget) : nullptr;
        if (!ram || !item) return;
        mobs::stopNavigation(*ram);
        mobs::lookAt(*ram, *item, 40.0F + 20.0F, 40.0F);
        mobs::moveTo(*ram, feet(*item), 1.0);
    }
    void stop() override { mTarget = 0; }
    void tick() override {
        auto* ram  = mobOf(mUid);
        auto* item = mTarget ? fe::actorById(mTarget) : nullptr;
        if (!ram || !item || !item->isAlive()) return;
        auto const& stack = static_cast<ItemActor*>(item)->item();
        if (!isItemTempting(*ram, stack)) return;
        mobs::lookAt(*ram, *item, 40.0F + 20.0F, 40.0F);
        if (mobs::distanceSqr(*ram, feet(*item)) < 6.25 && tryAccept(*ram, stack)) {
            // consume the wool (ItemEntity#discard; GameEvent.EAT has no Bedrock listener here)
            item->remove();
            gf::log().info("questcamps: a Questing Ram ate dropped {}", game::items::idOf(stack));
            mTarget = 0;
        }
    }

private:
    std::int64_t mUid;
    std::int64_t mTarget = 0;
};

/// TemptGoal(this, 1.0, this::isItemTempting, false): follows the nearest player within 10 blocks
/// (line of sight ignored) who holds a quest item the ram still needs, in either hand.
class TemptGoal final : public gf::entity::Goal {
public:
    explicit TemptGoal(Mob& ram) : mUid(fe::uidOf(ram)) { flags = Move | Look; }

    bool canUse() override {
        if (mCalmDown > 0) {
            --mCalmDown;
            return false;
        }
        auto* ram = mobOf(mUid);
        if (!ram) return false;
        Player* best  = nullptr;
        double  bestD = 0;
        for (auto* p : fe::playersNear(dimOf(*ram), feet(*ram), 10.0)) {
            if (!p || p->isSpectator() || !p->isAlive()) continue;
            if (!isItemTempting(*ram, p->getSelectedItem()) && !isItemTempting(*ram, p->getOffhandSlot())) continue;
            double d = mobs::distanceSqr(*ram, *p);
            if (!best || d < bestD) best = p, bestD = d;
        }
        mPlayer = best ? fe::uidOf(*best) : 0;
        return best != nullptr;
    }
    void stop() override {
        mPlayer = 0;
        if (auto* ram = mobOf(mUid)) mobs::stopNavigation(*ram);
        mCalmDown = 50; // reducedTickDelay(100)
    }
    void tick() override {
        auto* ram    = mobOf(mUid);
        auto* player = mPlayer ? fe::actorById(mPlayer) : nullptr;
        if (!ram || !player) return;
        mobs::lookAt(*ram, *player, 40.0F + 20.0F, 40.0F);
        if (mobs::distanceSqr(*ram, *player) < 6.25) mobs::stopNavigation(*ram);
        else mobs::moveTo(*ram, feet(*player), 1.0);
    }

private:
    std::int64_t mUid;
    std::int64_t mPlayer   = 0;
    int          mCalmDown = 0;
};

// ---- behaviour -------------------------------------------------------------------------------------------------

void onSpawn(Actor& self) {
    auto* mob = fe::asMob(self);
    if (!mob) return;
    (void)ramData(self);
    gf::entity::addGoal(*mob, 2, std::make_shared<EatWoolGoal>(*mob));
    gf::entity::addGoal(*mob, 3, std::make_shared<TemptGoal>(*mob));
    gf::entity::addGoal(*mob, 5, gf::entity::goals::goHome(*mob, 1.25));
    // a ram placed by the grove claims its home on its first tick only (a later ram never does)
    if (!gf::entity::home(*mob) && self.mTickCount <= 5) claimHome(*mob);
    dividers()[fe::uidOf(self)] = 0; // Java's constructor: a loaded ram starts at 0

    publish(self);
}

/// QuestRam#customServerAiStep.
void aiStep(Actor& self) {
    if (!self.isAlive()) return;
    auto& divider = dividers()[fe::uidOf(self)];
    if (--divider <= 0) {
        divider = 70 + fe::randomOf(self).nextInt(50);
        if (cq::complete(colorFlags(self)) && !rewarded(self)) {
            rewardQuest(self);
            setRewarded(self, true);
        }
    }
    if (cq::complete(colorFlags(self)) && !rewarded(self)) {
        animateAddColor(self, fe::randomOf(self).nextInt(16), 5);
        playAmbient(self);
    }
}

/// QuestRam#interactAt: a quest item the ram still needs is taken (one consumed unless the player
/// has infinite materials).
bool interact(Actor& self, Player& player) {
    auto const& held = player.getSelectedItem();
    if (!tryAccept(self, held)) return false;
    if (!player.isCreative()) {
        const_cast<ItemStack&>(held).remove(1);
        player.sendInventory(false);
    }
    return true;
}

} // namespace

int colorFlags(Actor& ram) { return ramData(ram).value("ColorFlags", 0); }
bool rewarded(Actor& ram) { return ramData(ram).value("Rewarded", false); }

void setColorFlags(Actor& ram, int flags) {
    ramData(ram)["ColorFlags"] = flags;
    publish(ram);
}

void setRewarded(Actor& ram, bool value) {
    ramData(ram)["Rewarded"] = value;
    gf::entity::sync(ram, kRamData);
}

bool isItemTempting(Actor& ram, ItemStack const& stack) {
    if (game::items::isEmpty(stack)) return false;
    auto colours = questColorsOf(game::items::idOf(stack));
    if (colours.empty()) return false;
    return !cq::colorPresent(colorFlags(ram), colours.front());
}

bool tryAccept(Actor& ram, ItemStack const& stack) {
    if (game::items::isEmpty(stack)) return false;
    for (int colour : questColorsOf(game::items::idOf(stack))) {
        int flags = colorFlags(ram);
        if (cq::colorPresent(flags, colour)) continue;
        setColorFlags(ram, flags | 1 << colour);
        animateAddColor(ram, colour, 50);
        playAmbient(ram);
        gf::log().info("questcamps: the Questing Ram took {} ({} of 16)", cq::kDyeNames[static_cast<std::size_t>(colour)], cq::countColors(flags | 1 << colour));
        return true;
    }
    return false;
}

void rewardQuest(Actor& ram) {
    auto&                 region = ram.getDimensionBlockSource();
    gf::loot::LootContext ctx;
    ctx.region     = &region;
    ctx.origin     = feet(ram);
    ctx.thisEntity = &ram;
    auto rewards   = gf::loot::roll(quest().reward, ctx);
    if (auto level = ll::service::getLevel()) {
        auto p = feet(ram);
        for (auto const& stack : rewards) level->getSpawner().spawnItem(region, stack, nullptr, Vec3{p.x, p.y + 1.0F, p.z}, 0);
    }
    // every server player inside the ram's box inflated by 16
    auto box = ram.getAABB();
    for (auto* p : fe::playersNear(dimOf(ram), feet(ram), 64.0)) {
        if (!p) continue;
        auto pp = feet(*p);
        if (pp.x < box.min.x - 16 || pp.x > box.max.x + 16 || pp.y < box.min.y - 16 - 1.8F || pp.y > box.max.y + 16 || pp.z < box.min.z - 16 ||
            pp.z > box.max.z + 16)
            continue;
        gf::progression::trigger(*p, kTrigger);
    }
    // LandmarkUtil.markStructureConquered(level, this, QUEST_GROVE, true)
    auto fp = feet(ram);
    gf::landmark::setConquered(dimOf(ram), BlockPos{static_cast<int>(std::floor(fp.x)), static_cast<int>(std::floor(fp.y)), static_cast<int>(std::floor(fp.z))},
                               kGrove, true);
    gf::log().info("questcamps: the Questing Ram's quest is complete ({} reward stacks)", rewards.size());
}

void queueHome(int dim, BlockPos const& pos) {
    {
        std::lock_guard lock(homesMutex());
        homes().push_back(core::GlobalPos{dim, pos.x, pos.y, pos.z});
    }
}

std::size_t pendingHomes() {
    std::lock_guard lock(homesMutex());
    return homes().size();
}

void installRam() {
    gf::entity::Behaviour b;
    b.onSpawn  = onSpawn;
    b.aiStep   = aiStep;
    b.interact = interact;
    b.die      = [](Actor& self, std::string const&, Actor*) { dividers().erase(fe::uidOf(self)); };
    b.load     = [](Actor& self, Json const&) { publish(self); };
    gf::entity::registerType(kRam, b);
    // GloamingEntities: ON_GROUND, MOTION_BLOCKING_NO_LEAVES, Animal.checkAnimalSpawnRules
    gf::spawn::registerPlacement(kRam, gf::spawn::PlacementRule::OnGround);

    // the homes of rams the grove placed whose chunk has not loaded yet, kept with the world
    static bool lifecycle = false;
    if (!std::exchange(lifecycle, true)) {
        game::db::onWorldReady([] {
            std::vector<core::GlobalPos> loaded;
            auto                         list = gf::game::db::get(kHomesKey);
            if (list && list->is_array())
                for (auto const& e : *list)
                    if (e.is_array() && e.size() == 4) loaded.push_back(core::GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
            // merged: worldgen may have queued homes before the world was ready
            std::lock_guard lock(homesMutex());
            for (auto const& h : loaded)
                if (std::none_of(homes().begin(), homes().end(), [&](core::GlobalPos const& e) { return e == h; })) homes().push_back(h);
        });
        game::db::onWorldSave([] { saveHomes(); });
        game::db::onWorldClosed([] {
            std::lock_guard lock(homesMutex());
            homes().clear();
            dividers().clear();
        });
        // SyncQuestsPacket: the quest context for the crosshair indicator of companion clients
        gf::feature::net::onCompanionJoined([](Player& player) { gf::feature::net::send(player, kSyncChannel, questJson()); });
    }
    gf::log().info("questcamps: registered the Questing Ram");
}

void uninstallRam() {}

} // namespace gf::feature::questcamps

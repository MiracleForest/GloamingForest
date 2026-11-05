// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/enums/extensions/TFBoatTypeEnumExtension.java
//   src/main/java/twilightforest/init/TFItems.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// TF's eight boat types (Boat.Type extensions: the wood's planks, minecraft:stick) are vanilla
// Boat / ChestBoat entities with TF's textures. Bedrock has no boat types to add: the boats are
// the entities twilightforest:boat and twilightforest:chest_boat (BP runtime identifiers
// minecraft:boat / minecraft:chest_boat: the game's own boat physics, seats, paddling and chest)
// with the wood as the entity property twilightforest:wood (0..7, kTfWoods order), which picks the
// texture on every client, vanilla ones included. The items place them as Java's BoatItem#use
// does, and a destroyed TF boat drops its TF item instead of the vanilla boat item the game's
// Boat#destroy drops for the entity's (oak) variant.

#include "feature/woodwork/Woodwork.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/item/Boat.h"
#include "mc/world/actor/item/ChestBoat.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/common/Interactions.h"
#include "feature/entity/Entity.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace gf::feature::woodwork {

namespace ia = interactions;

namespace {

constexpr char kBoat[]      = "twilightforest:boat";
constexpr char kChestBoat[] = "twilightforest:chest_boat";
constexpr char kWood[]      = "twilightforest:wood";

std::vector<ia::HandlerId>& handlerIds() {
    static std::vector<ia::HandlerId> ids;
    return ids;
}

/// The boat's wood (property twilightforest:wood).
int woodOf(Actor& boat) {
    int  wood  = 0;
    auto props = boat.getEntityContext().tryGetComponent<PropertyComponent>();
    if (props) {
        auto v = props->mPropertyContainer->getValueVariant(HashedString::computeHash(kWood));
        if (v) {
            if (auto const* i = std::get_if<int>(&*v)) wood = *i;
        }
    }
    return std::clamp(wood, 0, static_cast<int>(kTfWoods.size()) - 1);
}

/// Player#blockInteractionRange (Attributes.BLOCK_INTERACTION_RANGE: 4.5, creative 5).
double reachOf(Player const& player) { return player.isCreative() ? 5.0 : 4.5; }

/// The TF item a boat entity drops (Boat#getDropItem).
std::optional<std::string> dropItemOf(Actor& boat) {
    auto const& type = boat.getTypeName();
    bool        chest = type == kChestBoat;
    if (!chest && type != kBoat) return std::nullopt;
    int wood = woodOf(boat);
    return std::string(kNs) + std::string(kTfWoods[static_cast<std::size_t>(wood)]) + (chest ? "_chest_boat" : "_boat");
}

/// BoatItem#use: the player's view hits a block or a fluid within reach; no entity contains the
/// player's eyes; the boat (at the hit, turned the player's way) collides with no block.
ia::Result useBoatItem(ia::UseContext const& c, int wood, bool chest) {
    Player& player = c.player;
    auto&   region = player.getDimensionBlockSource();
    Vec3    eye    = player.getHeadPos();
    Vec3    view   = player.getViewVector(1.0f);
    double  reach  = reachOf(player);
    Vec3    end{static_cast<float>(eye.x + view.x * reach), static_cast<float>(eye.y + view.y * reach), static_cast<float>(eye.z + view.z * reach)};
    // ClipContext.Fluid.ANY: liquids count as hits
    HitResult hit = region.clip(eye, end, true, ShapeType::Outline, static_cast<int>(std::ceil(reach)) + 1, false, false, &player, [](BlockSource const&, Block const&, bool) { return true; }, true);
    Vec3      at;
    if (hit.mIsHitLiquid) at = hit.mLiquidPos;
    else if (hit.mType == HitResultType::Tile) at = hit.mPos;
    else return ia::Result::Pass; // MISS
    // an entity in the way that contains the player's eyes
    AABB search = player.getAABB();
    search.min  = Vec3{std::min(search.min.x, search.min.x + view.x * 5.f) - 1.f, std::min(search.min.y, search.min.y + view.y * 5.f) - 1.f, std::min(search.min.z, search.min.z + view.z * 5.f) - 1.f};
    search.max  = Vec3{std::max(search.max.x, search.max.x + view.x * 5.f) + 1.f, std::max(search.max.y, search.max.y + view.y * 5.f) + 1.f, std::max(search.max.z, search.max.z + view.z * 5.f) + 1.f};
    for (Actor* e : region.getEntities(search, 0.0f)) {
        if (!e || e == &player || e->getTypeName() == "minecraft:item") continue;
        auto const& b = e->getAABB();
        if (eye.x >= b.min.x && eye.x <= b.max.x && eye.y >= b.min.y && eye.y <= b.max.y && eye.z >= b.min.z && eye.z <= b.max.z) return ia::Result::Pass;
    }
    // Boat dimensions 1.375 x 0.5625 (Java); the level must have room for it
    AABB box{Vec3{at.x - 0.6875f, at.y, at.z - 0.6875f}, Vec3{at.x + 0.6875f, at.y + 0.5625f, at.z + 0.6875f}};
    if (!region.fetchCollisionShapes(box, false, std::nullopt, nullptr).empty()) return ia::Result::Deny;
    std::string event = std::string(kNs) + std::string(kTfWoods[static_cast<std::size_t>(wood)]);
    ActorUniqueID id{};
    auto*         boat = CommandUtils::spawnEntityAt(region, at, ActorDefinitionIdentifier("twilightforest", chest ? "chest_boat" : "boat", event), id, nullptr);
    if (!boat) {
        log().warn("woodwork: could not spawn a {} boat", event);
        return ia::Result::Deny;
    }
    entity::setProperty(*boat, kWood, wood);
    // Java: the boat's yRot is the player's; Bedrock's boat rows along its local x, its seat turns
    // the rider by -90 degrees, so the body faces the player's yaw + 90
    boat->teleport(at, player.getDimensionId(), Vec2{0.0f, player.getRotation().y + 90.0f});
    consumeOne(player, c.item);
    log().info("woodwork: {} placed a {} {}", player.getRealName(), std::string(kTfWoods[static_cast<std::size_t>(wood)]), chest ? "chest boat" : "boat");
    return ia::Result::Consume;
}

/// After Boat#destroy / ChestBoat#destroy of a TF boat: the vanilla boat item the game dropped for
/// the entity's variant becomes the TF item.
void replaceDrop(Actor& boat) {
    auto tf = dropItemOf(boat);
    if (!tf) return;
    auto& region = boat.getDimensionBlockSource();
    auto  pos    = boat.getPosition();
    AABB  box{Vec3{pos.x - 2.5f, pos.y - 2.5f, pos.z - 2.5f}, Vec3{pos.x + 2.5f, pos.y + 2.5f, pos.z + 2.5f}};
    for (Actor* a : region.getEntities(box, 0.0f)) {
        if (!a || a->getTypeName() != "minecraft:item") continue;
        auto& stack = static_cast<ItemActor*>(a)->item();
        auto  id    = game::items::idOf(stack);
        if (!id.starts_with("minecraft:") || !(id.ends_with("_boat") || id.ends_with("_raft"))) continue;
        Vec3 at     = a->getPosition();
        Vec3 motion = a->getPosDelta();
        int  delay  = static_cast<ItemActor*>(a)->pickupDelay();
        int  n      = stack.mCount;
        a->remove();
        if (auto s = game::items::make(*tf, n)) {
            // the same spot, motion and pickup delay as the game's drop
            if (auto* fresh = region.getLevel().getSpawner().spawnItem(region, *s, nullptr, at, 0)) {
                static_cast<ItemActor*>(fresh)->pickupDelay() = delay;
                const_cast<Vec3&>(fresh->getPosDelta())       = motion;
            }
        }
        log().info("woodwork: a destroyed TF boat dropped {}", *tf);
        return;
    }
}

} // namespace

// Boat#destroy is the game's own drop of a boat (no loot table, no contract event): the TF boats
// alone (by entity type) get their TF item; every other boat passes through untouched.
LL_TYPE_INSTANCE_HOOK(WoodworkBoatDestroyHook, ll::memory::HookPriority::Normal, Boat, &Boat::$destroy, void, ::Actor* source) {
    origin(source);
    replaceDrop(*this);
}

LL_TYPE_INSTANCE_HOOK(WoodworkChestBoatDestroyHook, ll::memory::HookPriority::Normal, ChestBoat, &ChestBoat::$destroy, void, ::Actor* source) {
    origin(source);
    replaceDrop(*this);
}

void installBoats() {
    for (std::size_t i = 0; i < kTfWoods.size(); ++i) {
        int wood = static_cast<int>(i);
        for (bool chest : {false, true}) {
            std::string item = std::string(kNs) + std::string(kTfWoods[i]) + (chest ? "_chest_boat" : "_boat");
            handlerIds().push_back(ia::onUse(item, [wood, chest](ia::UseContext const& c) { return useBoatItem(c, wood, chest); }));
        }
    }
    WoodworkBoatDestroyHook::hook();
    WoodworkChestBoatDestroyHook::hook();
}

void uninstallBoats() {
    WoodworkChestBoatDestroyHook::unhook();
    WoodworkBoatDestroyHook::unhook();
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::woodwork

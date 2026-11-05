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
// texture. The items place them as Java's BoatItem#use does, a destroyed TF boat drops its TF item
// instead of the game's boat item (src/feature/ext/woodwork/BoatDrops.h), and a TF boat breaks on a
// fall as Java's Boat#checkFallDamage (the entity contract's tick).

#include "feature/woodwork/Woodwork.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>

#include <utility>
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandUtils.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/level/BlockPos.h"
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
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/HitResult.h"

#include "feature/common/Interactions.h"
#include "feature/ext/woodwork/BoatDrops.h"
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

/// Entity#isPickable of Java (living entities, boats, minecarts, hanging entities, end crystals,
/// primed TNT, falling blocks, fireballs and shulker bullets; never items, experience orbs, arrows,
/// thrown items, area effect clouds, fishing hooks or evoker fangs); spectators never count.
bool isPickable(Actor& e) {
    if (!e.isAlive() || e.isSpectator()) return false;
    if (e.hasCategory(ActorCategory::Mob) || e.hasCategory(ActorCategory::Boat) || e.hasCategory(ActorCategory::Minecart)) return true;
    static constexpr std::string_view kPickable[] = {"minecraft:painting", "minecraft:leash_knot", "minecraft:ender_crystal", "minecraft:tnt", "minecraft:falling_block",
                                                     "minecraft:fireball", "minecraft:small_fireball", "minecraft:dragon_fireball", "minecraft:wither_skull",
                                                     "minecraft:wither_skull_dangerous", "minecraft:shulker_bullet", "minecraft:wind_charge_projectile",
                                                     "minecraft:breeze_wind_charge_projectile"};
    auto const& type = e.getTypeName();
    return std::find(std::begin(kPickable), std::end(kPickable), type) != std::end(kPickable);
}

/// Boat#canCollideWith (Boat.canVehicleCollide): other.canBeCollidedWith() (boats, shulkers) or
/// other.isPushable() (living entities that are alive and not spectators; boats;
/// minecarts; never armor stands).
bool collidesWithBoat(Actor& e) {
    if (!e.isAlive() || e.isSpectator()) return false;
    if (e.hasCategory(ActorCategory::Boat) || e.hasCategory(ActorCategory::Minecart)) return true;
    if (!e.hasCategory(ActorCategory::Mob)) return false;
    auto const& type = e.getTypeName();
    if (type == "minecraft:armor_stand") return false;
    return true; // Java also skips a mob on a ladder (onClimbable); Bedrock has no such query here
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
        if (!e || e == &player || !isPickable(*e)) continue; // EntitySelector.NO_SPECTATORS.and(Entity::isPickable)
        auto const& b = e->getAABB();                        // inflated by getPickRadius (0)
        if (eye.x >= b.min.x && eye.x <= b.max.x && eye.y >= b.min.y && eye.y <= b.max.y && eye.z >= b.min.z && eye.z <= b.max.z) return ia::Result::Pass;
    }
    // Boat dimensions 1.375 x 0.5625 (Java); level.noCollision(boat, box): no block shape and no
    // entity the boat collides with (Boat#canCollideWith: canBeCollidedWith or isPushable)
    AABB box{Vec3{at.x - 0.6875f, at.y, at.z - 0.6875f}, Vec3{at.x + 0.6875f, at.y + 0.5625f, at.z + 0.6875f}};
    if (!region.fetchCollisionShapes(box, false, std::nullopt, nullptr).empty()) return ia::Result::Deny;
    for (Actor* e : region.getEntities(box, 0.0f)) {
        if (!e || !collidesWithBoat(*e)) continue;
        auto const& b = e->getAABB();
        if (b.min.x < box.max.x && b.max.x > box.min.x && b.min.y < box.max.y && b.max.y > box.min.y && b.min.z < box.max.z && b.max.z > box.min.z) return ia::Result::Deny;
    }
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

/// Boat#checkFallDamage (Java 1.21.1, every boat type): a boat that is not riding anything counts
/// the distance it falls while it is not over water; landing after more than 3 blocks on land
/// (status ON_LAND: on the ground, out of water) breaks it: the riders take the fall
/// (Entity#causeFallDamage propagates to the passengers), the boat goes without its item (kill) and,
/// with doEntityDrops, drops 3 planks of its wood and 2 sticks (Boat.Type: the wood's planks,
/// minecraft:stick). Landing in water resets the distance. The game's boats never break on a fall:
/// this runs for the TF boats only.
void checkFallDamage(Actor& boat) {
    auto& fall = entity::data(boat, "woodwork_fall"); // {"d": fallDistance, "y": last y}
    if (!fall.is_object()) fall = Json{{"d", 0.0}, {"y", boat.getPosition().y}};
    double y     = boat.getPosition().y;
    double dy    = y - fall.value("y", y);
    double dist  = fall.value("d", 0.0);
    fall["y"]    = y;
    if (boat.getVehicle()) {
        fall["d"] = 0.0;
        return;
    }
    auto& region = boat.getDimensionBlockSource();
    if (boat.isOnGround()) {
        if (dist > 3.0 && !boat.isInWater() && !waterAt(region, BlockPos{static_cast<int>(std::floor(boat.getPosition().x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(boat.getPosition().z))})) {
            // causeFallDamage(fallDistance, 1.0F, fall): the boat itself is immune, the riders fall
            auto pos = boat.getPosition();
            AABB box{Vec3{pos.x - 3.f, pos.y - 3.f, pos.z - 3.f}, Vec3{pos.x + 3.f, pos.y + 4.f, pos.z + 3.f}};
            for (Actor* rider : region.getEntities(box, 0.0f)) {
                if (rider && rider->getVehicle() == &boat) rider->causeFallDamageToActor(static_cast<float>(dist), 1.0f, ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Fall, ""));
            }
            int  wood  = woodOf(boat);
            bool drops = true;
            if (auto level = ll::service::getLevel()) {
                GameRuleId rule;
                rule.mValue = static_cast<int>(GameRules::GameRulesIndex::DoEntityDrops);
                drops       = level->getGameRules().getBool(rule, true);
            }
            if (drops) {
                std::string planks = std::string(kNs) + std::string(kTfWoods[static_cast<std::size_t>(wood)]) + "_planks";
                for (int i = 0; i < 3; ++i)
                    if (auto s = game::items::make(planks)) boat.spawnAtLocation(*s, 0.0f);
                for (int i = 0; i < 2; ++i)
                    if (auto s = game::items::make("minecraft:stick")) boat.spawnAtLocation(*s, 0.0f);
            }
            log().info("woodwork: a {} boat broke on landing after a {:.1f} block fall", kTfWoods[static_cast<std::size_t>(wood)], dist);
            // kill(): gone without the boat item (the game's destroy would drop it); the riders
            // dismount as the boat is removed
            // removed on the next tick: not inside the actor's own tick (Actor::tick)
            auto id = boat.getOrCreateUniqueID();
            ia::nextTick([id] {
                auto level = ll::service::getLevel();
                if (!level) return;
                if (auto* b = level->fetchEntity(id, false)) {
                    b->removeAllPassengers(true, true);
                    b->remove();
                }
            });
            fall["d"] = 0.0;
            return;
        }
        fall["d"] = 0.0;
        return;
    }
    // the fluid below the boat: no fall over water (canBoatInFluid)
    BlockPos below{static_cast<int>(std::floor(boat.getPosition().x)), static_cast<int>(std::floor(y)) - 1, static_cast<int>(std::floor(boat.getPosition().z))};
    if (!waterAt(region, below) && !boat.isInWater() && dy < 0.0) fall["d"] = dist - dy;
}

} // namespace

void installBoats() {
    for (std::size_t i = 0; i < kTfWoods.size(); ++i) {
        int wood = static_cast<int>(i);
        for (bool chest : {false, true}) {
            std::string item = std::string(kNs) + std::string(kTfWoods[i]) + (chest ? "_chest_boat" : "_boat");
            handlerIds().push_back(ia::onUse(item, [wood, chest](ia::UseContext const& c) { return useBoatItem(c, wood, chest); }));
        }
    }
    static bool once = false;
    if (!std::exchange(once, true)) {
        // registrations of the entity contract last for the process
        for (auto const* type : {kBoat, kChestBoat}) entity::registerType(type, {.tick = [](Actor& boat) { checkFallDamage(boat); }});
    }
    // Boat#getDropItem: the TF item of the boat's wood instead of the game's boat item
    // (src/feature/ext/woodwork/BoatDrops.h)
    woodwork_ext::boatdrops::setReplacement([](Actor& boat) { return dropItemOf(boat); });
    woodwork_ext::boatdrops::install();
}

void uninstallBoats() {
    woodwork_ext::boatdrops::uninstall();
    woodwork_ext::boatdrops::setReplacement({});
    for (auto id : handlerIds()) ia::remove(id);
    handlerIds().clear();
}

} // namespace gf::feature::woodwork
// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// U9 (artifacts): small Java equivalents over the engine (see Artifacts.h).
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"
#include "mc/world/phys/AABB.h"

#include "feature/artifacts/Artifacts.h"
#include "feature/fx/Fx.h"
#include "game/Items.h"

namespace gf::feature::artifacts {

std::int64_t uidOf(Actor const& actor) { return actor.getOrCreateUniqueID().rawID; }

Actor* actorById(std::int64_t uid) {
    if (!uid) return nullptr;
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id;
    id.rawID = uid;
    return level->fetchEntity(id, false);
}

Mob* asMob(Actor& actor) { return actor.hasCategory(ActorCategory::Mob) ? static_cast<Mob*>(&actor) : nullptr; }

bool isType(Actor const& actor, char const* id) { return actor.getTypeName() == id; }

int dimOf(Actor const& actor) { return static_cast<int>(actor.getDimensionId()); }

bool isLiving(Actor const& a) { return a.isPlayer() || a.hasCategory(ActorCategory::Mob); }

std::vector<Player*> playersIn(int dim) {
    std::vector<Player*> out;
    auto                 level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer(std::function<bool(Player&)>([&](Player& p) {
        if (dimOf(p) == dim && p.isAlive()) out.push_back(&p);
        return true;
    }));
    return out;
}

std::vector<Actor*> actorsIn(BlockSource& region, int dim, AABB const& box) {
    auto out = region.getEntities(box, 0.0f);
    for (auto* p : playersIn(dim)) {
        auto const& b   = p->getAABB();
        bool        hit = b.min.x < box.max.x && b.max.x > box.min.x && b.min.y < box.max.y && b.max.y > box.min.y && b.min.z < box.max.z
                && b.max.z > box.min.z;
        if (hit && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return out;
}

Vec3 viewVector(Actor const& actor) {
    auto   rot   = actor.getRotation(); // x pitch, y yaw (degrees)
    double pitch = rot.x * std::numbers::pi / 180.0, yaw = -rot.y * std::numbers::pi / 180.0;
    double c     = std::cos(pitch);
    return Vec3{float(std::sin(yaw) * c), float(-std::sin(pitch)), float(std::cos(yaw) * c)};
}

void setMotion(Actor& actor, Vec3 const& v) { const_cast<Vec3&>(actor.getPosDelta()) = v; }

float bbWidth(Actor const& actor) {
    auto const& b = actor.getAABB();
    return b.max.x - b.min.x;
}

float bbHeight(Actor const& actor) {
    auto const& b = actor.getAABB();
    return b.max.y - b.min.y;
}

Mob* spawnMob(BlockSource& region, std::string const& id, Vec3 const& pos, float yaw) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto* mob = level->getSpawner().spawnMob(region, ActorDefinitionIdentifier(id), nullptr, pos, false, false, true);
    if (!mob) return nullptr;
    mob->setYBodyRotation(yaw);
    const_cast<Vec2&>(mob->getRotation()) = Vec2{0.0f, yaw};
    return mob;
}

namespace {
std::unordered_map<std::int64_t, std::unique_ptr<Random>>& randoms() {
    static std::unordered_map<std::int64_t, std::unique_ptr<Random>> m;
    return m;
}
std::int64_t freshSeed() {
    static std::mt19937_64 rng{std::random_device{}()};
    return static_cast<std::int64_t>(rng());
}
std::unique_ptr<Random>& levelRandomSlot() {
    static std::unique_ptr<Random> r = std::make_unique<Random>(freshSeed());
    return r;
}
} // namespace

Random& randomOf(Actor const& actor) {
    auto& slot = randoms()[uidOf(actor)];
    if (!slot) slot = std::make_unique<Random>(freshSeed());
    return *slot;
}

void seedRandom(Actor const& actor, std::int64_t seed) { randoms()[uidOf(actor)] = std::make_unique<Random>(seed); }

void pruneRandoms() {
    for (auto it = randoms().begin(); it != randoms().end();) it = actorById(it->first) ? std::next(it) : randoms().erase(it);
}

Random& levelRandom() { return *levelRandomSlot(); }

void seedLevelRandom(std::int64_t seed) { levelRandomSlot() = std::make_unique<Random>(seed); }

void playSound(Actor& at, std::string const& event, float volume, float pitch) { gf::fx::sound(at.getDimension(), event, at.getPosition(), volume, pitch); }

void playSoundAt(Actor& in, Vec3 const& pos, std::string const& event, float volume, float pitch) {
    gf::fx::sound(in.getDimension(), event, pos, volume, pitch);
}

void particle(Actor& in, std::string const& id, Vec3 const& pos, Vec3 const& velocity, Json const& params) {
    gf::fx::particle(in.getDimension(), id, pos, velocity, 1, params);
}

void actionBar(Player& player, std::string const& text) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::SetTitle);
    if (!packet) return;
    auto& title      = static_cast<SetTitlePacket&>(*packet);
    title.mType      = SetTitlePacketPayload::TitleType::Actionbar;
    title.mTitleText = text;
    packet->sendTo(player);
}

bool creative(Player const& player) { return player.isCreative(); }

bool wearsCrown(Actor const& actor) { return game::items::idOf(actor.getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == kMysticCrown; }

ItemStack const& mainHand(Actor const& actor) { return actor.getCarriedItem(); }

ItemStack const& offHand(Actor const& actor) { return actor.getOffhandSlot(); }

bool holding(Actor const& actor, std::string const& itemId) {
    return game::items::idOf(mainHand(actor)) == itemId || game::items::idOf(offHand(actor)) == itemId;
}

std::int64_t gameTime() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::int64_t>(level->getCurrentServerTick().tickID) : 0;
}

int tickCountOf(Actor const& actor) { return actor.mTickCount; }

} // namespace gf::feature::artifacts


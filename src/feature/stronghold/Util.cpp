// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// U25 (stronghold): helpers shared by the unit's mobs (see Util.h).
#include "feature/stronghold/Util.h"

#include <algorithm>
#include <random>

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/entity/EntityInternal.h"
#include "feature/fx/Fx.h"
#include "game/Blocks.h"
#include "game/Items.h"

namespace gf::feature::stronghold {

namespace fe = gf::feature::entity;

namespace {
core::jrand::LegacyRandom& rng() {
    static core::jrand::LegacyRandom r(static_cast<std::int64_t>(std::random_device{}()) << 21 ^ static_cast<std::int64_t>(std::random_device{}()));
    return r;
}
} // namespace

core::jrand::RandomSource& levelRandom() { return rng(); }
void                       seedLevelRandomForTest(std::int64_t seed) { rng().setSeed(seed); }

std::vector<Actor*> actorsInBox(int dim, Vec3 const& min, Vec3 const& max, std::string const& type) {
    std::vector<Actor*> out;
    auto*               region = game::regionOf(dim);
    if (!region) return out;
    for (auto* a : fe::actorsIn(*region, dim, AABB{min, max}))
        if (a && a->isAlive() && (type.empty() || a->getTypeName() == type)) out.push_back(a);
    return out;
}

std::vector<Actor*> actorsAround(Actor& actor, std::string const& type, double inflate) {
    auto const& b = actor.getAABB();
    auto        r = static_cast<float>(inflate);
    return actorsInBox(static_cast<int>(actor.getDimensionId()), Vec3{b.min.x - r, b.min.y - r, b.min.z - r}, Vec3{b.max.x + r, b.max.y + r, b.max.z + r}, type);
}

float armorFactor(float damage, float armor, float toughness) {
    float f  = 2.0F + toughness / 4.0F;
    float f1 = std::clamp(armor - damage / f, armor * 0.2F, 20.0F);
    return 1.0F - f1 / 25.0F;
}

void playSound(Actor& actor, std::string const& event, float volume, float pitch) {
    gf::fx::sound(actor.getDimension(), event, actor.getFeetPos(), volume, pitch);
}

float voicePitch(core::jrand::RandomSource& random) {
    float a = random.nextFloat();
    float b = random.nextFloat();
    return (a - b) * 0.2F + 1.0F;
}

bool holds(Actor const& actor, std::string const& itemId) { return game::items::idOf(actor.getCarriedItem()) == itemId; }

} // namespace gf::feature::stronghold

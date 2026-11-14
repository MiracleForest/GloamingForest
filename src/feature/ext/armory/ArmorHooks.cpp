// U7 extension (ext/armory/ArmorHooks.h): armour toughness of upstream's armour.
#include "feature/ext/armory/ArmorHooks.h"

#include <cmath>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/ItemStack.h"

namespace gf::ext::armory {

namespace {

struct Store {
    std::mutex  mutex;
    ToughnessFn toughness;
};

Store& store() {
    static Store s;
    return s;
}

ToughnessFn toughnessFn() {
    std::lock_guard lock(store().mutex);
    return store().toughness;
}

} // namespace

void setToughness(ToughnessFn fn) {
    std::lock_guard lock(store().mutex);
    store().toughness = std::move(fn);
}

int extraToughness(ItemStack const& head, ItemStack const& chest, ItemStack const& legs, ItemStack const& feet) {
    auto fn = toughnessFn();
    if (!fn) return 0;
    float sum = 0.f;
    for (auto const* s : {&head, &chest, &legs, &feet})
        if (!s->isNull()) sum += fn(*s);
    return static_cast<int>(std::lround(sum));
}

} // namespace gf::ext::armory

namespace {

using Slot = SharedTypes::Legacy::ArmorSlot;

LL_TYPE_INSTANCE_HOOK(ArmoryToughnessHook, ll::memory::HookPriority::Normal, Mob, &Mob::getToughnessValue, int) {
    int base = origin();
    return base + gf::ext::armory::extraToughness(getArmor(Slot::Head), getArmor(Slot::Torso), getArmor(Slot::Legs), getArmor(Slot::Feet));
}

} // namespace

namespace gf::ext::armory {

void installHooks() { ArmoryToughnessHook::hook(); }

void uninstallHooks() { ArmoryToughnessHook::unhook(); }

} // namespace gf::ext::armory

// Neutral stub of the "entity" contract (F0). F8 replaces this file (rule R18).
#include "feature/entity/Entity.h"

#include <map>

#include "core/Stub.h"

namespace gf::entity {

namespace {
/// Inert goal returned by the stub factories.
class NoGoal final : public Goal {
public:
    bool canUse() override { return false; }
};
GoalPtr noGoal(char const* name) {
    GF_STUB(name);
    return std::make_shared<NoGoal>();
}
} // namespace

void registerType(std::string const&, Behaviour) { GF_STUB("entity.registerType"); }

Json& data(Actor& actor, std::string const& key) {
    GF_STUB("entity.data");
    static std::map<std::pair<Actor const*, std::string>, Json> scratch; // not persisted
    return scratch[{&actor, key}];
}

void sync(Actor&, std::string const&) { GF_STUB("entity.sync"); }
void setProperty(Actor&, std::string const&, PropertyValue const&) { GF_STUB("entity.setProperty"); }
void addGoal(Mob&, int, GoalPtr) { GF_STUB("entity.addGoal"); }
void addTargetGoal(Mob&, int, GoalPtr) { GF_STUB("entity.addTargetGoal"); }

namespace goals {
GoalPtr charge(Mob&, ChargeSpec) { return noGoal("entity.goals.charge"); }
GoalPtr breath(Mob&, BreathSpec) { return noGoal("entity.goals.breath"); }
GoalPtr flockToSameKind(Mob&, double) { return noGoal("entity.goals.flockToSameKind"); }
GoalPtr panicOnFlockDeath(Mob&, double) { return noGoal("entity.goals.panicOnFlockDeath"); }
GoalPtr avoidAny(Mob&, std::function<bool(Actor&)>, float, double, double) { return noGoal("entity.goals.avoidAny"); }
GoalPtr goHome(Mob&, double) { return noGoal("entity.goals.goHome"); }
GoalPtr alwaysWatchTarget(Mob&) { return noGoal("entity.goals.alwaysWatchTarget"); }
GoalPtr randomLookIfBored(Mob&) { return noGoal("entity.goals.randomLookIfBored"); }
GoalPtr simplifiedAttack(Mob&) { return noGoal("entity.goals.simplifiedAttack"); }
} // namespace goals

void setMoveControl(Mob&, MoveKind) { GF_STUB("entity.setMoveControl"); }
void setMoveControl(Mob&, std::function<void(Mob&)>) { GF_STUB("entity.setMoveControl"); }
void setHome(Mob&, core::GlobalPos const&, int) { GF_STUB("entity.setHome"); }
std::optional<Home> home(Mob const&) {
    GF_STUB("entity.home");
    return std::nullopt;
}
PartHandle addPart(Mob&, std::string const&, PartSize, std::function<Vec3(Mob&)>) {
    GF_STUB("entity.addPart");
    return {};
}
Actor* partActor(PartHandle const&) {
    GF_STUB("entity.partActor");
    return nullptr;
}

namespace mount {
bool grab(Mob&, Player&) {
    GF_STUB("entity.mount.grab");
    return false;
}
void throwRider(Mob&, Vec3 const&) { GF_STUB("entity.mount.throwRider"); }
} // namespace mount

void addTargetFilter(std::function<bool(Mob&, Actor&)>) { GF_STUB("entity.addTargetFilter"); }
void seedForTest(Actor&, std::int64_t) { GF_STUB("entity.seedForTest"); }

} // namespace gf::entity

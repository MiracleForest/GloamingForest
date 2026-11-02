#pragma once

// The fixture boss of F8's selftests (gf_test:boss, development builds only): its Behaviour (which
// counts what it sees in `probe()`) and its boss registration (orange bar, loot gf_test:entities/boss,
// home landmark gf_test:fixture) exist only while a FixtureScope lives, so other units' tests that
// use gf_test:boss in the 'full' run see a plain mob.

#include <string>

namespace gf::feature::entity::tests {

/// The fixture boss's bar colour: orange, which no vanilla BossBarColor has (red is nearest).
constexpr unsigned kFixtureRgb = 0xFF8000;

/// What the fixture behaviour of gf_test:boss saw.
struct Probe {
    int         spawns = 0, ticks = 0, aiSteps = 0, dies = 0, interacts = 0, loads = 0;
    std::string lastDieType;
};
Probe& probe();

/// Registers the fixture boss's Behaviour and boss spec while alive (nested scopes share one).
class FixtureScope {
public:
    FixtureScope();
    ~FixtureScope();
    FixtureScope(FixtureScope const&)            = delete;
    FixtureScope& operator=(FixtureScope const&) = delete;
};

} // namespace gf::feature::entity::tests

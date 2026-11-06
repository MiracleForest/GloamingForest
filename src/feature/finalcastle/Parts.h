#pragma once

// U30 (finalcastle): the unit's parts. Module.cpp installs them once per process (the dispatches of
// interactions / blockevents / entity have no removal for most registrations).
//
//  - blocks:  castle doors, force fields, thorns, thorn rose / leaves, cinder furnace / log / wood,
//             the thorns feature
//  - mobs:    adherent, harbinger cube, roving cube, plateau boss + final boss spawner, the
//             progression_end message and the gazebo WIP texts
//  - castle:  the final castle structure and its pieces

namespace gf::feature::finalcastle {

namespace blocks {
void install();
} // namespace blocks

namespace mobs {
void install();
} // namespace mobs

namespace castle {
void install();
} // namespace castle

} // namespace gf::feature::finalcastle

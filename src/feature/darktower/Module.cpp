// U26 (darktower): the unit's module: the device runtime, the devices, the ghasts and mobs, the
// slider and the dark tower structure.
#include "feature/darktower/Devices.h"
#include "feature/darktower/Mobs.h"
#include "feature/darktower/Runtime.h"
#include "feature/darktower/Slider.h"
#include "feature/darktower/Structure.h"
#include "feature/darktower/UrGhast.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::darktower {

class DarkTowerModule {
public:
    void install() {
        runtime::install();
        static bool registered = false;
        if (!registered) {
            // the dispatches of interactions / blockevents / entity have no removal for most
            // registrations: register once per process (the client enables the mod once)
            registered = true;
            devices::install();
            slider::install();
            ghasts::install();
            mobs::install();
            landmarkgen::install();
        }
    }
    void uninstall() { runtime::uninstall(); }
};

} // namespace gf::feature::darktower

GF_REGISTER_MODULE(gf::feature::darktower::DarkTowerModule);

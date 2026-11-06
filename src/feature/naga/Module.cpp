// The U20 module: the Naga (Naga.cpp) and the Naga courtyard (Courtyard.cpp).

#include "feature/naga/NagaInternal.h"
#include "mod/Module.h"

namespace gf::feature::naga {

struct NagaModule {
    void install() {
        installCourtyard();
        installNaga();
    }
    void uninstall() { uninstallNaga(); }
};

} // namespace gf::feature::naga

GF_REGISTER_MODULE(gf::feature::naga::NagaModule);

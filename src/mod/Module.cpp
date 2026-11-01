// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Module.cpp), LGPL-3.0-or-later.
#include "mod/Module.h"

#include <algorithm>
#include <exception>

#include "mod/Log.h"

namespace gf {

namespace {
std::vector<ModuleEntry> gInstalled;

std::vector<ModuleEntry> sortedEntries() {
    auto entries = ModuleRegistry::entries();
    std::stable_sort(entries.begin(), entries.end(), [](ModuleEntry const& a, ModuleEntry const& b) {
        return a.order < b.order;
    });
    return entries;
}
} // namespace

void loadModules() {
    for (auto const& e : sortedEntries()) {
        if (!e.load) continue;
        try {
            e.load();
        } catch (std::exception const& ex) {
            log().error("module {} failed to load: {}", e.name, ex.what());
        }
    }
}

void installModules() {
    for (auto const& e : sortedEntries()) {
        try {
            e.install();
            gInstalled.push_back(e);
            log().debug("module {} installed", e.name);
        } catch (std::exception const& ex) {
            log().error("module {} failed to install: {}", e.name, ex.what());
        }
    }
}

void uninstallModules() {
    for (auto it = gInstalled.rbegin(); it != gInstalled.rend(); ++it) {
        try {
            it->uninstall();
        } catch (std::exception const& ex) {
            log().error("module {} failed to uninstall: {}", it->name, ex.what());
        }
    }
    gInstalled.clear();
}

} // namespace gf

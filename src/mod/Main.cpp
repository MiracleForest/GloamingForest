// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Main.cpp), LGPL-3.0-or-later.
#include "mod/Main.h"

#include "ll/api/mod/RegisterHelper.h"

#include "core/Stub.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/ModConfig.h"
#include "mod/Module.h"

GF_REGISTER_CONFIG("mod", gf::ModConfig);

namespace gf {

ll::io::Logger& log() { return Mod::getInstance().getSelf().getLogger(); }

Mod& Mod::getInstance() {
    static Mod instance;
    return instance;
}

bool Mod::load() {
    // Flush every message so e2e harnesses (and people tailing logs) see them immediately.
    getSelf().getLogger().setFlushLevel(ll::io::LogLevel::Trace);
    config::detail::setConfigDir(getSelf().getConfigDir());
    config::loadAll();
    core::setStubSink([](std::string const& line) {
        if (config::get<ModConfig>().logContractStubs) log().warn("{}", line);
    });
    loadTranslations(getSelf().getModDir() / "lang");
    loadModules();
    return true;
}

bool Mod::enable() {
    installModules();
    mEnabled = true;
    log().info("GloamingForest enabled");
    return true;
}

bool Mod::disable() {
    uninstallModules();
    mEnabled = false;
    return true;
}

} // namespace gf

LL_REGISTER_MOD(gf::Mod, gf::Mod::getInstance());

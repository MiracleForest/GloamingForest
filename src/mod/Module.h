// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Module.h), LGPL-3.0-or-later.
#pragma once

#include <concepts>
#include <string_view>

#include "mod/Registry.h"

namespace gf {

/// A unit of the mod that hooks into the game. Any class with `install()` and `uninstall()`
/// can be a module; register it with `GF_REGISTER_MODULE(T)` in its .cpp file.
///
/// - `install()` runs on mod enable, `uninstall()` on disable (reverse order). Both run on the
///   server thread. Undo everything in `uninstall()` (listeners, hooks, commands' state).
/// - If `T` has `static T& get()`, that instance is used; otherwise the registrar owns one.
/// - Optional `load()` runs at mod load (before the world is created; BDS: before the server
///   starts). Use it only for things that must happen that early (client input/render
///   listeners, world pack config).
/// - Order: lower `order` installs first. Foundation modules use negative orders
///   (see `ModuleOrder`); feature units use the default 0.
template <class T>
concept ModuleType = requires(T& t) {
    t.install();
    t.uninstall();
};

namespace ModuleOrder {
inline constexpr int Config    = -1000; ///< configs, translations
inline constexpr int Storage   = -900;  ///< world DB
inline constexpr int Packs     = -800;  ///< pack deployer
inline constexpr int Net       = -700;  ///< companion protocol
inline constexpr int Services  = -600;  ///< W0 services (interactions, block data, redstone, screens)
inline constexpr int Contracts = -550;  ///< W0b contract implementations (dimension, worldgen, combat, ...)
inline constexpr int Commands  = -500;  ///< /gloamingforest command tree
inline constexpr int Feature   = 0;     ///< default for feature units
} // namespace ModuleOrder

struct ModuleEntry {
    std::string_view name;
    int              order;
    void (*install)();
    void (*uninstall)();
    void (*load)(); ///< optional (nullptr): `T::load()` runs at mod load, before the world exists
};

using ModuleRegistry = StaticRegistry<ModuleEntry>;

namespace detail {
template <ModuleType T>
T& moduleInstance() {
    if constexpr (requires {
                      { T::get() } -> std::same_as<T&>;
                  }) {
        return T::get();
    } else {
        static T instance;
        return instance;
    }
}
template <ModuleType T>
struct ModuleRegistrar {
    ModuleRegistrar(std::string_view name, int order) {
        void (*loadFn)() = nullptr;
        if constexpr (requires(T& t) { t.load(); }) loadFn = [] { moduleInstance<T>().load(); };
        ModuleRegistry::add(ModuleEntry{
            name,
            order,
            [] { moduleInstance<T>().install(); },
            [] { moduleInstance<T>().uninstall(); },
            loadFn,
        });
    }
};
} // namespace detail

/// Runs the optional `load()` of all registered modules (sorted by order). Mod entry only.
void loadModules();
/// Installs all registered modules (sorted by order). Called by the mod entry only.
void installModules();
/// Uninstalls all installed modules in reverse order. Called by the mod entry only.
void uninstallModules();

} // namespace gf

/// Registers module `T` (default order 0). Put it in the module's .cpp file.
#define GF_REGISTER_MODULE(T)                                                                                          \
    static ::gf::detail::ModuleRegistrar<T> GF_UNIQUE(gf_module_registrar_)(#T, ::gf::ModuleOrder::Feature)
/// Registers module `T` with an explicit install order.
#define GF_REGISTER_MODULE_ORDERED(T, order)                                                                           \
    static ::gf::detail::ModuleRegistrar<T> GF_UNIQUE(gf_module_registrar_)(#T, (order))

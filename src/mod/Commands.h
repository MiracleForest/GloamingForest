// Adapted from LiteLDev/Levitate v0.1.0 (src/mod/Commands.h), LGPL-3.0-or-later.
#pragma once

#include <functional>

#include "mod/Registry.h"

namespace ll::command {
class CommandHandle;
}
class CommandOrigin;
class Player;

namespace gf::commands {

/// The root command: `/gloamingforest`, without aliases
/// (GloamingCommand). Units add subcommands (rule R14: every subcommand has one owner in
/// docs/spec/owners.tsv). The function runs every time the command is (re)registered (BDS start,
/// every client local world):
///
///     GF_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
///         cmd.overload().text("center").execute([](CommandOrigin const& o, CommandOutput& out) { ... });
///     });
///
/// Foundation subcommands: `selftest [glob]`, `reload`, `config [section]`, `stubs`.
using SubcommandFn       = std::function<void(ll::command::CommandHandle&)>;
using SubcommandRegistry = StaticRegistry<SubcommandFn>;

inline constexpr char kRootCommand[] = "gloamingforest";

/// The player who ran the command (nullptr for the console / command blocks).
[[nodiscard]] Player* playerOf(CommandOrigin const& origin);
/// Permission level 2+ (upstream's admin commands need it).
[[nodiscard]] bool isAdmin(CommandOrigin const& origin);

namespace detail {
/// While above 0 on the current thread, command error replies are not logged ("Command error:",
/// Commands.cpp): `selftest::runServerCommand` sets it, since its caller gets the messages in the
/// result and BDS never echoes them on its console.
extern thread_local int quietCommandErrors;

struct SubcommandRegistrar {
    explicit SubcommandRegistrar(SubcommandFn fn) { SubcommandRegistry::add(std::move(fn)); }
};
} // namespace detail

} // namespace gf::commands

#define GF_REGISTER_SUBCOMMAND(...)                                                                                    \
    static ::gf::commands::detail::SubcommandRegistrar GF_UNIQUE(gf_subcommand_)(__VA_ARGS__)

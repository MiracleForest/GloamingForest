#pragma once

// Neutral contract stubs (W0a). F0 declares every W0 contract in a header; until the W0b unit that
// owns a contract replaces its stub .cpp, each entry point returns a neutral value (false, 0,
// nullopt, empty, Pass) and reports itself once:
//
//     bool tryActivate(BlockSource&, BlockPos const&) { GF_STUB("world.portal.tryActivate"); return false; }
//
// The first call of each name logs "<name>: not implemented (contract stub)" through the sink
// (the mod routes it to its logger as a warning; tests and tools get stderr), later calls are
// silent. `stubCalls()` lists the names that were hit (selftests, the W2 unimplemented report).

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gf::core {

/// Reports a call of an unimplemented contract entry point (logged once per name; thread-safe).
void notImplemented(std::string_view name);
/// Where the one-time lines go (default: stderr). The mod installs its logger at load.
void setStubSink(std::function<void(std::string const& line)> sink);
/// Names reported so far, in first-call order.
[[nodiscard]] std::vector<std::string> stubCalls();

} // namespace gf::core

#define GF_STUB(name) ::gf::core::notImplemented(name)

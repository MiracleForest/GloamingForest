#pragma once

// Internals of the tag runtime shared with F9's selftests and extension headers.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "feature/tags/Tags.h"

namespace gf::tags::internal {

/// The registry directories that have tag files ("block", "item", "worldgen/biome", "gloaming", ...).
[[nodiscard]] std::vector<std::string> registryDirs();
/// The tags of one directory.
[[nodiscard]] std::vector<std::string> tagsOf(std::string const& dir);
/// Membership / members by directory name: a `tags::Registry` directory answers as has / members
/// (bridge included); upstream's own registries ("gloaming": wood palettes, ...) have no vanilla bridge.
[[nodiscard]] bool                     hasIn(std::string const& dir, std::string const& tag, std::string const& id);
[[nodiscard]] std::vector<std::string> membersIn(std::string const& dir, std::string const& tag);
/// The data members of a tag (nested data tags flattened, the data's Java ids renamed to Bedrock's;
/// without the vanilla bridge, whose members come from the game's registries).
[[nodiscard]] std::vector<std::string> dataMembers(std::string const& dir, std::string const& tag);
/// The bridged vanilla tags a tag reaches through its includes (data and bridge), sorted.
[[nodiscard]] std::vector<std::string> bridgedOf(Registry registry, std::string const& tag);
[[nodiscard]] std::size_t              fileCount();
/// The published snapshot's generation (every load publishes a new one).
[[nodiscard]] std::uint64_t generation();
void                        reload();

/// While it lives, the queries of the constructing thread (and only of it) read a snapshot built
/// from the data tree below `root` (`<root>/<ns>/tags/...`) in place of the published one: the
/// selftests' synthetic trees, which world generation on other threads never sees. Nothing is
/// published; destroy it on the thread that made it, before that thread suspends (a synchronous test:
/// it must never span a co_await). Destroyed on another thread it logs an error, and the
/// constructing thread drops the snapshot at its next query (it never keeps the synthetic data).
class ScopedSnapshot {
public:
    explicit ScopedSnapshot(std::filesystem::path const& root);
    ~ScopedSnapshot();
    ScopedSnapshot(ScopedSnapshot const&)            = delete;
    ScopedSnapshot& operator=(ScopedSnapshot const&) = delete;

private:
    std::shared_ptr<std::atomic<bool>> mAlive;
    std::thread::id                    mThread;
};

} // namespace gf::tags::internal

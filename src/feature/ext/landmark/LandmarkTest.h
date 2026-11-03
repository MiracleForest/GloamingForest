#pragma once

// Extension of the "landmark" contract for selftests (F6, rule R18; folded by A8 in W2b): removing
// the starts a test made. landmark::forceStart stores a start, in memory and in the world DB, that
// the contract itself never removes, so a test forcing one would leave it in the world (a kept
// development world keeps it for good, rule R12). Other units' tests use these instead of F6's
// internals (feature/landmark/Internal.h is no contract):
//
//     // the start lives as long as `forced`, also when the test aborts (require / fail throw)
//     gf::landmark::ext::ScopedStart forced("gf_test:landmark_fixture_open", center);
//     ctx.require(forced && forced->generated, "forced the fixture landmark");
//     auto start = gf::landmark::startAt(center.dim, gf::game::blockPos(center));
//
//     // or by hand, for a start made some other way (setConquered keeps a start's flag otherwise)
//     (void)gf::landmark::forceStart("gf_test:landmark_fixture", center);
//     gf::landmark::ext::forgetStartForTest(center, "gf_test:landmark_fixture");
//
// Users: F5 worldgen.landmark_rules (its forced fixture and dark tower), F7 structure.count_loot,
// F8 entity.boss_conquest (in place of Internal.h's eraseRecord + flushRecords), F6's own selftests.
// The fixtures are gf_test:landmark_fixture (locked by twilightforest:progress_naga, controlled
// spawns: labelled monster list 1 on the shell's upper floor, list 0 on the porch, none on the
// shell's ground floor) and gf_test:landmark_fixture_open (no lock, spawn overrides: monsters in its
// pieces, no ambient mobs in its box); see feature/landmark/Helpers.cpp.
//
// Both run on the server thread (they write the world DB). The blocks a forced start placed stay
// where they are: a test's flatArea restores them.

#include <cstddef>
#include <optional>
#include <string>

#include "core/GlobalPos.h"
#include "feature/landmark/Landmark.h"

namespace gf::landmark::ext {

/// Removes the start of `key` stored with start chunk (pos.x >> 4, pos.z >> 4) of pos.dim (the chunk
/// forceStart stores its start in), or, with no key, every start forceStart or generateAt stored
/// there, from memory and the world DB. The chunk's structure sets count as not evaluated afterwards,
/// so a natural start a forced one replaced comes back on the next lookup. Returns how many starts
/// were removed. Server thread.
std::size_t forgetStartForTest(core::GlobalPos const& pos, std::string const& key = {});

/// landmark::forceStart for a scope: the start it stored is removed (forgetStartForTest with its key)
/// when the object is destroyed. Not copyable or movable.
class ScopedStart {
public:
    ScopedStart(std::string const& key, core::GlobalPos const& pos);
    ~ScopedStart();
    ScopedStart(ScopedStart const&)            = delete;
    ScopedStart& operator=(ScopedStart const&) = delete;

    /// What forceStart returned (nullopt: unknown key, or the generator made no pieces).
    [[nodiscard]] std::optional<Start> const& start() const { return mStart; }
    [[nodiscard]] explicit operator bool() const { return mStart.has_value(); }
    [[nodiscard]] Start const&                operator*() const { return *mStart; }
    [[nodiscard]] Start const*                operator->() const { return &*mStart; }

private:
    std::string          mKey;
    core::GlobalPos      mPos;
    std::optional<Start> mStart;
};

} // namespace gf::landmark::ext

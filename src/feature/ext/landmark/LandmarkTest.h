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
//     // or by hand, for a start forceStart made some other way (only forced starts are removed: a
//     // natural start of the key in that chunk, a real landmark site, stays as it is)
//     (void)gf::landmark::forceStart("gf_test:landmark_fixture", center);
//     gf::landmark::ext::forgetStartForTest(center, "gf_test:landmark_fixture");
//
// Prefer ScopedStart, above all with a real landmark key (twilightforest:dark_tower, ...): it puts
// back the natural start a forced one replaced, conquest state included. The by-hand form is for
// forced starts only; forcedOnly = false (also erasing a natural start) is for a test that stored a
// stand-in natural start itself.
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

#include <cstdint>
#include <memory>

namespace gf::feature::landmark {
struct Record; // F6's stored start (feature/landmark/Internal.h, no contract)
}

namespace gf::landmark::ext {

/// Removes the start of `key` stored with start chunk (pos.x >> 4, pos.z >> 4) of pos.dim (the chunk
/// forceStart stores its start in), or, with no key, every start forceStart or generateAt stored
/// there, from memory and the world DB. Only starts forceStart or generateAt made are removed, unless
/// `forcedOnly` is false (then a keyed call also erases a natural start of the key there, resetting
/// that site, its conquest included: only for a stand-in the test stored itself). The chunk's structure sets count as not evaluated afterwards,
/// so a natural start a forced one replaced comes back on the next lookup. Returns how many starts
/// were removed. Server thread.
///
/// A natural start comes back as the start chunk's structure sets make it again: unconquered, and
/// without anything set on it at run time. A forced start that replaced a natural one took over its
/// conquered flag, and forgetting it by hand loses that flag; ScopedStart keeps it (below).
std::size_t forgetStartForTest(core::GlobalPos const& pos, std::string const& key = {}, bool forcedOnly = true);

/// landmark::forceStart for a scope. When the object is destroyed, the start it stored is removed
/// (forgetStartForTest with its key); a start of the same key and start chunk that it replaced (a
/// natural one, or an earlier forced one) is put back as it was before, its conquered flag included,
/// instead of being made again. Not copyable or movable.
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
    std::string                                       mKey;
    core::GlobalPos                                   mPos;
    std::shared_ptr<gf::feature::landmark::Record>    mReplaced; ///< the start forceStart replaced
    std::uint64_t                                     mWorld = 0; ///< the world generation it belongs to
    std::optional<Start>                              mStart;
};

} // namespace gf::landmark::ext

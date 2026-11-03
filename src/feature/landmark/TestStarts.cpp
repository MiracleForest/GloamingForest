// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// The selftests' start removal (F6's contract extension feature/ext/landmark/LandmarkTest.h): the
// starts landmark::forceStart stored, taken out of the start store and the world DB again.
#include "feature/ext/landmark/LandmarkTest.h"

#include "feature/landmark/Internal.h"
#include "mod/Log.h"

namespace gf::landmark::ext {

namespace fl = gf::feature::landmark;

std::size_t forgetStartForTest(core::GlobalPos const& pos, std::string const& key) {
    int const   cx = pos.x >> 4, cz = pos.z >> 4;
    auto const  wanted = key.empty() ? std::string{} : fl::normalizeKey(key);
    std::size_t removed = 0;
    for (auto const& rec : fl::recordsStartedIn(pos.dim, cx, cz)) {
        if (wanted.empty() ? !rec->forced : rec->start.key != wanted) continue;
        fl::eraseRecord(pos.dim, cx, cz, rec->start.key);
        ++removed;
    }
    // a natural start of the chunk that a forced one replaced is made again by the next lookup
    fl::forgetEvaluation(pos.dim, cx, cz);
    fl::flushRecords();
    if (removed > 0) gf::log().info("landmark: removed {} test start(s) of chunk {} {} ({})", removed, cx, cz, wanted.empty() ? "forced" : wanted);
    return removed;
}

ScopedStart::ScopedStart(std::string const& key, core::GlobalPos const& pos) : mKey(fl::normalizeKey(key)), mPos(pos), mStart(forceStart(key, pos)) {
    if (mStart) mKey = mStart->key;
}

ScopedStart::~ScopedStart() {
    if (!mStart) return; // forceStart stored nothing
    try {
        (void)forgetStartForTest(mPos, mKey);
    } catch (...) {}
}

} // namespace gf::landmark::ext

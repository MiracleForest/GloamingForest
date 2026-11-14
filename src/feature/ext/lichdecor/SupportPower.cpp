// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/block/CandelabraBlock.java (getSignal / getDirectSignal)
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// U22 (lichdecor) contract extension: a directional producer in the circuit graph at a custom
// block's position (SupportPower.h).

#include "feature/ext/lichdecor/SupportPower.h"

#include <algorithm>
#include <bitset>
#include <map>
#include <memory>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"
#include "mc/world/redstone/circuit/components/DirectionalProducerComponent.h"

#include "core/GlobalPos.h"

namespace gf::feature::ext::lichdecor {

namespace {

/// The producers this extension created: position -> the component (to recognise our own).
std::map<core::GlobalPos, BaseCircuitComponent*>& ours() {
    static std::map<core::GlobalPos, BaseCircuitComponent*> m;
    return m;
}

core::GlobalPos keyOf(BlockSource& region, BlockPos const& at) { return core::GlobalPos{static_cast<int>(region.getDimensionId()), at.x, at.y, at.z}; }

BaseCircuitComponent* componentAt(CircuitSystem& system, BlockPos const& pos) {
    auto& all = system.mSceneGraph->mAllComponents;
    if (auto it = all.find(pos); it != all.end()) return it->second.get();
    // created this tick: still queued until the graph's next update
    auto& pending = system.mSceneGraph->mPendingAdds;
    auto  it      = pending.find(pos);
    return it == pending.end() ? nullptr : it->second.mRawComponentPtr;
}

} // namespace

bool setDirectedPower(BlockSource& region, BlockPos const& at, int towards, int strength) {
    CircuitSystem* system = region.getDimension().mCircuitSystem.get();
    if (!system) return false;
    auto  key  = keyOf(region, at);
    auto* comp = componentAt(*system, at);
    auto  it   = ours().find(key);
    bool  mine = comp && it != ours().end() && it->second == comp && !comp->mRemoved;
    if (comp && !mine && !comp->mRemoved) {
        if (it != ours().end()) ours().erase(it);
        return strength <= 0; // a vanilla component sits there: leave it alone
    }
    if (strength <= 0) {
        if (mine) {
            comp->setStrength(0);
            comp->mRemoved = true; // the graph drops removed components on its next update
            system->mSceneGraph->remove(at, comp);
        }
        ours().erase(key);
        return true;
    }
    if (!mine) {
        auto producer = std::make_unique<DirectionalProducerComponent>();
        std::bitset<6> allowed;
        if (towards >= 0 && towards < 6) allowed.set(static_cast<std::size_t>(towards));
        producer->mAllowedConnections = allowed;
        producer->mAttachedAllowed    = true;
        auto* created                 = system->createComponent(at, static_cast<uchar>(towards), std::move(producer));
        if (!created) return false;
        ours()[key] = created;
    }
    // on the component itself: CircuitSystem#setStrength only finds components already in the graph,
    // and one created this tick waits in the pending adds until the graph's next update
    ours()[key]->setStrength(std::min(strength, 15));
    return true;
}

int directedPower(BlockSource& region, BlockPos const& at) {
    CircuitSystem* system = region.getDimension().mCircuitSystem.get();
    if (!system) return 0;
    auto* comp = componentAt(*system, at);
    auto  it   = ours().find(keyOf(region, at));
    if (!comp || it == ours().end() || it->second != comp || comp->mRemoved) return 0;
    return comp->getStrength();
}

} // namespace gf::feature::ext::lichdecor

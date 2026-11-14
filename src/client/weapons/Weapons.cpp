// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Translated from the upstream 4.8 source (commit 9e40a757):
//   src/main/java/gloamingforest/client/event/ClientGameEvents.java (updateBowFOV)
//   src/main/java/gloamingforest/client/renderer/entity/BlockChainRenderer.java
// Copyright (c) the upstream authors (see NOTICE).
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Client build only (U8 weapons): the FOV zoom of upstream's four bows while drawn (Bedrock zooms only
// for its own bow) and the chain links from the thrower's eye to a flying block and chain (the
// ball itself is the RP entity's geometry; vanilla clients see the ball only).

#ifdef GF_CLIENT

#include <algorithm>
#include <cmath>
#include <string>

#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerItemInUse.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "client/Client.h"
#include "client/render/Render.h"
#include "client/world/Sky.h"
#include "feature/ext/entity/EntityClientExt.h"
#include "game/Items.h"
#include "mod/Module.h"

namespace gf::client::weapons {

namespace {

bool isGloamingBow(std::string const& id) {
    return id == "gloamingforest:triple_bow" || id == "gloamingforest:ender_bow" || id == "gloamingforest:ice_bow" || id == "gloamingforest:seeker_bow";
}

float bowFov(float fov) {
    auto* p = reinterpret_cast<Player*>(gf::client::currentPlayer());
    if (!p) return fov;
    auto& inUse = p->mItemInUse.get();
    if (inUse.mItem->isNull() || !isGloamingBow(game::items::idOf(inUse.mItem.get()))) return fov;
    int   max  = inUse.mItem->mItem ? inUse.mItem->mItem->getMaxUseDuration(&inUse.mItem.get()) : 0;
    int   used = std::max(0, max - p->getItemUseDuration());
    float f    = static_cast<float>(used) / 20.0F;
    f          = f > 1.0F ? 1.0F : f * f;
    return fov * (1.0F - f * 0.15F);
}

/// BlockChainRenderer: a link every 1.5 blocks from the owner's eye to the ball.
void drawChain(render::RenderContext& ctx, Actor& chain, float) {
    auto data = gf::client::entity::data(chain.getOrCreateUniqueID().rawID, "weapons:chain");
    if (!data.is_object() || !data.contains("owner")) return;
    ActorUniqueID id;
    id.rawID     = data["owner"].get<std::int64_t>();
    Actor* owner = chain.getLevel().fetchEntity(id, false);
    if (!owner) return;
    auto   cp     = chain.getPosition();
    auto   ob     = owner->getAABB();
    float  oh     = ob.max.y - ob.min.y;
    auto   cb     = chain.getAABB();
    Vec3   base{cp.x, cb.min.y + (cb.max.y - cb.min.y) * 0.5f, cp.z};
    Vec3   eye    = owner->isPlayer() ? owner->getPosition() : owner->getHeadPos();
    Vec3   xyz{eye.x - cp.x, eye.y - (cb.min.y + oh * 0.5f), eye.z - cp.z};
    double len    = std::sqrt(double(xyz.x) * xyz.x + double(xyz.y) * xyz.y + double(xyz.z) * xyz.z);
    if (len < 1e-3) return;
    double perLink = 1.5;
    double links   = len / perLink;
    Vec3   n{static_cast<float>(xyz.x / len), static_cast<float>(xyz.y / len), static_cast<float>(xyz.z / len)};
    for (int i = 1; i < links; ++i) {
        double k = -(links - i) * perLink;
        Vec3   at{static_cast<float>(base.x + xyz.x + n.x * k), static_cast<float>(base.y + xyz.y + n.y * k), static_cast<float>(base.z + xyz.z + n.z * k)};
        render::drawGeo(ctx, "geometry.gloamingforest.chain", "textures/gloamingforest/entity/block_and_chain", {.translation = at});
    }
}

class WeaponsClientModule {
public:
    void install() {
        static bool once = false;
        if (once) return;
        once = true;
        gf::client::camera::addFovModifier(bowFov);
        render::registerEntityRenderer("gloamingforest:chain_block", drawChain);
    }
    void uninstall() {}
};

} // namespace

} // namespace gf::client::weapons

GF_REGISTER_MODULE(gf::client::weapons::WeaponsClientModule);

#endif // GF_CLIENT

// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/item/travellers_gear/TravellersArmorItem.java (ArmorRender, getArmorTexture)
//   src/main/java/twilightforest/client/event/TravellersClientEvents.java (renderGlovesInFirstPerson)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// The worn models' state of a player, for every viewer (vanilla clients too): the bits of the
// player's synced mark variant (ActorDataIDs::MarkVariant; the player definition gives players no
// mark variant of their own), which an attachable can read as q.mark_variant of its wearer:
//   1  the chest piece carries gloves (TRAVELLERS_HAS_GLOVES: the gloves item, or a vest merged with
//      gloves): the gloves arms (ArmorRender#getHumanoidArmorModel, CHEST)
//   2  the wearer's skin has slim arms (ArmorRender#isModelSlim: chest_gloves_slim)
//   4  the legs piece shows the belt (TRAVELLERS_HAS_BELT, or swap_hotbar on the wings)
//   8  zooming with the goggles (IS_USING_GOGGLES_ZOOM_MODIFIER: travellers_layer_1_down)
//   16 the wearer's firstPersonGloveOverlay (the client mod sends its config value; TF's default on)
// The attachables that read them belong to F2's generated partition (docs/units/tgear.md: the
// conditional parts are routed to F2).
#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/shared_types/persona/ArmSizeType.h"
#include "mc/deps/vanilla_components/ActorDataDirtyFlagsComponent.h"
#include "mc/entity/components/SynchedActorDataComponent.h"
#include "mc/util/ThreadOwner.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/DataItem2.h"
#include "mc/world/actor/SynchedActorData.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/SerializedSkinImpl.h"
#include "mc/world/actor/player/SerializedSkinRef.h"
#include "mc/world/item/ItemStack.h"

#include "feature/net/Net.h"
#include "feature/tgear/Gear.h"
#include "feature/tgear/Internal.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace gf::feature::tgear {

namespace {

namespace items = game::items;
using Slot      = SharedTypes::Legacy::ArmorSlot;

struct OverlayState {
    std::mutex                              mutex;
    std::unordered_map<std::int64_t, bool> firstPersonGloves; ///< the client mod's config, per player
};
OverlayState& overlay() {
    static OverlayState s;
    return s;
}

std::int64_t uidOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

bool contains(std::string s, std::string_view what) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.find(what) != std::string::npos;
}

} // namespace

bool hasSlimArms(Player const& player) {
    try {
        auto const& ref = player.mSkin;
        if (!ref) return false;
        auto const& owner = ref->mSkinImpl;
        if (!owner) return false;
        SerializedSkinImpl const& skin = owner->mObject;
        // a persona's arm size; a classic skin by its geometry (geometry.humanoid.customSlim)
        if (skin.mIsPersona) return skin.mArmSizeType == SharedTypes::persona::ArmSizeType::Slim;
        return contains(*skin.mResourcePatch, "slim") || contains(*skin.mDefaultGeometryName, "slim");
    } catch (...) {
        return false;
    }
}

int wornModelBits(Player const& player, bool zooming) {
    int         bits  = 0;
    auto const& chest = player.getArmor(Slot::Torso);
    if (!items::isEmpty(chest) && isGear(items::idOf(chest)) && hasGloves(chest)) bits |= kWornGloves;
    if (hasSlimArms(player)) bits |= kWornSlim;
    auto const& legs = player.getArmor(Slot::Legs);
    if (!items::isEmpty(legs)) {
        auto id = items::idOf(legs);
        if (id == ids::kBelt || (id == ids::kWings && hasModifier(legs, ids::kSwapHotbar))) bits |= kWornBelt;
    }
    if (zooming) bits |= kWornZoom;
    bool overlayOn = true; // TFConfig.firstPersonGloveOverlay's default
    {
        std::lock_guard lock(overlay().mutex);
        if (auto it = overlay().firstPersonGloves.find(uidOf(player)); it != overlay().firstPersonGloves.end()) overlayOn = it->second;
    }
    if (overlayOn) bits |= kWornFirstPersonGloves;
    return bits;
}

int wornModelState(Player const& player) { return player.getMarkVariant(); }

void refreshWornModel(Player& player) {
    int bits = wornModelBits(player, usingZoom(player));
    if (player.getMarkVariant() == bits) return;
    try {
        auto& wrapper = player.mEntityData.get();
        auto& data    = wrapper.mData->mData.get();
        auto& list    = data.mItemsArray.get();
        auto  id      = static_cast<std::size_t>(ActorDataIDs::MarkVariant);
        if (id >= list.size() || !list[id]) return; // every actor defines it (Actor::_defineEntityData)
        if (!list[id]->setData(bits)) return;
        wrapper.mDirtyFlags->mDirtyFlags.get().set(id);
    } catch (...) {}
}

void setFirstPersonGloveOverlay(Player& player, bool on) {
    {
        std::lock_guard lock(overlay().mutex);
        overlay().firstPersonGloves[uidOf(player)] = on;
    }
    refreshWornModel(player);
}

void forgetWornModel(Player const& player) {
    std::lock_guard lock(overlay().mutex);
    overlay().firstPersonGloves.erase(uidOf(player));
}

} // namespace gf::feature::tgear

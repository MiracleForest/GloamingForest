// U5 (building): a real loom craft with TF's naga banner pattern, through the server's own item stack
// request handling (dx 575, rule R12).
//
// The test writes the bytes a Bedrock client sends for a loom craft (ItemStackRequestPacket: the
// banner, the dye and the pattern item put into the loom, then CraftLoom "tf_naga", the two
// consumes and the result taken to the cursor), lets the game read them (ItemStackRequestPacket
// _read, ItemStackRequestPacketPayload::toRequestBatch) and hands the batch to the simulated
// player's ItemStackNetManagerServer, as ServerNetworkHandler does for a client's packet. So the
// server's validation runs as for a player: the loom screen opened by a use on a real loom block,
// LoomMaterialContainerValidation for TF's pattern item (Banners.cpp's slot hook), CraftHandlerLoom
// with the tf_naga pattern (BannerPattern::matchesPatternItem, Banners.cpp's hook) and the
// created-output take. A second craft asks for tf_lich with the naga pattern item: the server must
// refuse it (Java's LoomMenu offers only the patterns of #pattern_item/<boss>).

#include <cstdint>
#include <string>
#include <variant>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/ItemStackRequestPacket.h"
#include "mc/network/packet/item_stack_request_packet_data/RequestData.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/network/ItemStackNetManagerBase.h"
#include "mc/world/inventory/network/ItemStackNetManagerServer.h"
#include "mc/world/inventory/network/ItemStackRequestActionType.h"
#include "mc/world/inventory/network/ItemStackRequestBatch.h"
#include "mc/world/item/BannerItem.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/actor/BannerPattern.h"

#include "feature/building/Building.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {

using gf::selftest::Context;
namespace b = gf::feature::building;
using Facing = ScriptModuleMinecraft::ScriptFacing;

/// The wire format of ItemStackRequestPacket (protocol of 1.21.20 and later).
class RequestWriter {
public:
    void u8(std::uint8_t v) { mBytes.push_back(static_cast<char>(v)); }
    void uvarint(std::uint32_t v) {
        while (v >= 0x80) {
            u8(static_cast<std::uint8_t>(v | 0x80));
            v >>= 7;
        }
        u8(static_cast<std::uint8_t>(v));
    }
    void varint(std::int32_t v) { uvarint((static_cast<std::uint32_t>(v) << 1) ^ static_cast<std::uint32_t>(v >> 31)); }
    void i32(std::int32_t v) {
        for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>((static_cast<std::uint32_t>(v) >> (8 * i)) & 0xFF));
    }
    void string(std::string const& s) {
        uvarint(static_cast<std::uint32_t>(s.size()));
        mBytes += s;
    }
    /// ItemStackRequestSlotInfo: FullContainerName {name, optional dynamic id}, slot, net id
    void slot(ContainerEnumName name, int slot, int netId) {
        u8(static_cast<std::uint8_t>(name));
        u8(0); // no dynamic container id
        u8(static_cast<std::uint8_t>(slot));
        varint(netId);
    }
    void take(int count, ContainerEnumName from, int fromSlot, int fromNetId, ContainerEnumName to, int toSlot, int toNetId) {
        u8(static_cast<std::uint8_t>(ItemStackRequestActionType::Take));
        u8(static_cast<std::uint8_t>(count));
        slot(from, fromSlot, fromNetId);
        slot(to, toSlot, toNetId);
    }
    void place(int count, ContainerEnumName from, int fromSlot, int fromNetId, ContainerEnumName to, int toSlot, int toNetId) {
        u8(static_cast<std::uint8_t>(ItemStackRequestActionType::Place));
        u8(static_cast<std::uint8_t>(count));
        slot(from, fromSlot, fromNetId);
        slot(to, toSlot, toNetId);
    }
    void consume(int count, ContainerEnumName from, int fromSlot, int fromNetId) {
        u8(static_cast<std::uint8_t>(ItemStackRequestActionType::Consume));
        u8(static_cast<std::uint8_t>(count));
        slot(from, fromSlot, fromNetId);
    }
    void craftLoom(std::string const& pattern) {
        u8(static_cast<std::uint8_t>(ItemStackRequestActionType::CraftLoom));
        string(pattern);
        u8(1); // times crafted
    }
    /// One request: id, actions (written by `body`, `count` of them), no strings to filter.
    template <class Fn>
    void request(int id, int count, Fn&& body) {
        varint(id);
        uvarint(static_cast<std::uint32_t>(count));
        body(*this);
        uvarint(0); // strings to filter
        i32(0);     // their origin
    }
    std::string mBytes;
};

int netIdOf(ItemStack const& item) {
    auto const& v = item.mNetIdVariant->mVariant.get();
    if (auto const* id = std::get_if<ItemStackNetId>(&v)) return id->mRawId;
    return 0;
}

/// Reads the client's bytes into an ItemStackRequestPacket and hands the batch to the player's
/// server-side manager; the error text, or "" when the game took the request.
std::string sendRequest(Player& player, std::string const& bytes, int requests, std::string const& wantPattern) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::ItemStackRequest);
    if (!packet) return "no ItemStackRequestPacket";
    auto&                pkt = static_cast<ItemStackRequestPacket&>(*packet);
    ReadOnlyBinaryStream stream(bytes, true);
    auto                 read = pkt._read(stream);
    if (!read) return "the game did not read the request bytes (error " + std::to_string(read.error().code().value()) + ")";
    auto const& data = pkt.mRequests.get();
    if (static_cast<int>(data.size()) != requests) return "the game read " + std::to_string(data.size()) + " request(s)";
    if (!wantPattern.empty()) {
        bool seen = false;
        for (auto const& r : data)
            for (auto const& a : r.mActions.get())
                if (auto const* loom = std::get_if<ItemStackRequestCereal::CraftLoomActionData>(&a)) seen = seen || *loom->mPatternNameId == wantPattern;
        if (!seen) return "the read request has no CraftLoom " + wantPattern;
    }
    auto* base = player.mItemStackNetManager.get();
    if (!base || !base->isEnabled()) return "the player has no item stack net manager";
    auto batch = pkt.toRequestBatch();
    if (!batch) return "no request batch";
    static_cast<ItemStackNetManagerServer*>(base)->handleRequestBatch(*batch, nullptr);
    return "";
}

ItemStack const& uiItem(Player& player, PlayerUISlot slot) { return player.mPlayerUIContainer->getItem(static_cast<int>(slot)); }

bool hasPattern(ItemStack const& banner, std::string const& nameId) {
    if (banner.isNull()) return false;
    for (auto const& [p, color] : BannerItem::getPatternAndColorPairsFromItemStack(banner))
        if (p && *p->mNameID == nameId) return true;
    return false;
}

void clearLoom(Player& player) {
    for (auto s : {PlayerUISlot::CursorSelected, PlayerUISlot::LoomInput, PlayerUISlot::LoomDye, PlayerUISlot::LoomMaterial, PlayerUISlot::CreatedItemOutput})
        player.setPlayerUIItem(s, ItemStack::EMPTY_ITEM(), false);
}

GF_REGISTER_SELFTEST("building.loom_request", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(575, 1);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto loomPos = p;
    loomPos.y += 1;
    ctx.require(ctx.setBlock(p, "minecraft:stone") && ctx.setBlock(loomPos, "minecraft:loom"), "a loom");
    BlockPos lp = gf::game::blockPos(loomPos);
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
        sp.teleport(Vec3{lp.x + 0.5f, float(lp.y), lp.z + 2.5f}, sp.getDimensionId());
        sp.getInventory().removeAllItems();
        clearLoom(sp);
    }
    co_await ctx.ticks(2);
    // a use of the loom with an empty hand opens its screen (the server's loom container screen)
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
        sp.simulateLookAt(lp, sim::LookDuration::Instant);
        ItemStack empty;
        sp.simulateSetItem(empty, true, 0);
        auto& hand = const_cast<ItemStack&>(sp.getSelectedItem());
        sp.mGameMode->useItemOn(hand, lp, static_cast<uchar>(Facing::South), Vec3{lp.x + 0.5f, lp.y + 0.5f, lp.z + 1.0f}, HandSlot::Mainhand, nullptr, true);
    }
    co_await ctx.ticks(2);
    auto& sp0  = static_cast<SimulatedPlayer&>(ctx.player());
    auto* base = sp0.mItemStackNetManager.get();
    ctx.require(base && base->isEnabled(), "the simulated player handles item stack requests");
    auto screen = base->getScreenContext().mScreenContainerType;
    ctx.require(screen == SharedTypes::Legacy::ContainerType::Loom, "the loom screen is open (container type " + std::to_string(static_cast<int>(screen)) + ")");

    // the client's first request: the banner, the dye and TF's pattern item from the hotbar into the
    // loom's input, dye and pattern slots
    auto banner  = gf::game::items::make("minecraft:banner", 1, 15);
    auto dye     = gf::game::items::make("minecraft:red_dye", 1);
    auto pattern = gf::game::items::make("twilightforest:naga_banner_pattern", 1);
    ctx.require(banner && dye && pattern, "a white banner, red dye and the naga banner pattern");
    int ids[3]{};
    {
        auto& sp  = static_cast<SimulatedPlayer&>(ctx.player());
        auto& inv = sp.getInventory();
        inv.setItem(0, *banner);
        inv.setItem(1, *dye);
        inv.setItem(2, *pattern);
        for (int i = 0; i < 3; ++i) {
            auto& item = const_cast<ItemStack&>(inv.getItem(i));
            if (netIdOf(item) == 0) item.serverInitNetId();
            ids[i] = netIdOf(item);
        }
        RequestWriter w;
        w.uvarint(1);
        w.request(-101, 3, [&](RequestWriter& r) {
            r.place(1, ContainerEnumName::HotbarContainer, 0, ids[0], ContainerEnumName::LoomInputContainer, static_cast<int>(PlayerUISlot::LoomInput), 0);
            r.place(1, ContainerEnumName::HotbarContainer, 1, ids[1], ContainerEnumName::LoomDyeContainer, static_cast<int>(PlayerUISlot::LoomDye), 0);
            r.place(1, ContainerEnumName::HotbarContainer, 2, ids[2], ContainerEnumName::LoomMaterialContainer, static_cast<int>(PlayerUISlot::LoomMaterial), 0);
        });
        auto err = sendRequest(sp, w.mBytes, 1, "");
        ctx.require(err.empty(), "the loom request (place): " + err);
    }
    co_await ctx.ticks(2);
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
        ctx.check(gf::game::items::idOf(uiItem(sp, PlayerUISlot::LoomInput)) == "minecraft:banner", "the banner is in the loom (" + gf::game::items::idOf(uiItem(sp, PlayerUISlot::LoomInput)) + ")");
        ctx.check(gf::game::items::idOf(uiItem(sp, PlayerUISlot::LoomDye)) == "minecraft:red_dye", "the dye is in the loom");
        ctx.require(gf::game::items::idOf(uiItem(sp, PlayerUISlot::LoomMaterial)) == "twilightforest:naga_banner_pattern",
                    "the server let TF's pattern item into the loom's pattern slot (" + gf::game::items::idOf(uiItem(sp, PlayerUISlot::LoomMaterial)) + ")");
    }

    // a pattern the item does not unlock: refused, nothing is consumed
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
        int   in = netIdOf(uiItem(sp, PlayerUISlot::LoomInput)), dy = netIdOf(uiItem(sp, PlayerUISlot::LoomDye));
        RequestWriter w;
        w.uvarint(1);
        w.request(-103, 4, [&](RequestWriter& r) {
            r.craftLoom("tf_lich");
            r.consume(1, ContainerEnumName::LoomInputContainer, static_cast<int>(PlayerUISlot::LoomInput), in);
            r.consume(1, ContainerEnumName::LoomDyeContainer, static_cast<int>(PlayerUISlot::LoomDye), dy);
            r.take(1, ContainerEnumName::CreatedOutputContainer, static_cast<int>(PlayerUISlot::CreatedItemOutput), -103, ContainerEnumName::CursorContainer, 0, 0);
        });
        auto err = sendRequest(sp, w.mBytes, 1, "tf_lich");
        ctx.require(err.empty(), "the loom request (tf_lich): " + err);
    }
    co_await ctx.ticks(2);
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
        ctx.check(!hasPattern(uiItem(sp, PlayerUISlot::CursorSelected), "tf_lich"), "the naga pattern item does not make a lich banner");
        ctx.check(gf::game::items::idOf(uiItem(sp, PlayerUISlot::LoomInput)) == "minecraft:banner", "the refused craft kept the banner in the loom");
    }

    // the craft: CraftLoom tf_naga, the banner and the dye consumed, the result to the cursor
    {
        auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
        int   in = netIdOf(uiItem(sp, PlayerUISlot::LoomInput)), dy = netIdOf(uiItem(sp, PlayerUISlot::LoomDye));
        RequestWriter w;
        w.uvarint(1);
        w.request(-105, 4, [&](RequestWriter& r) {
            r.craftLoom("tf_naga");
            r.consume(1, ContainerEnumName::LoomInputContainer, static_cast<int>(PlayerUISlot::LoomInput), in);
            r.consume(1, ContainerEnumName::LoomDyeContainer, static_cast<int>(PlayerUISlot::LoomDye), dy);
            r.take(1, ContainerEnumName::CreatedOutputContainer, static_cast<int>(PlayerUISlot::CreatedItemOutput), -105, ContainerEnumName::CursorContainer, 0, 0);
        });
        auto err = sendRequest(sp, w.mBytes, 1, "tf_naga");
        ctx.require(err.empty(), "the loom request (tf_naga): " + err);
    }
    co_await ctx.ticks(2);
    {
        auto& sp     = static_cast<SimulatedPlayer&>(ctx.player());
        auto const& cursor = uiItem(sp, PlayerUISlot::CursorSelected);
        ctx.check(hasPattern(cursor, "tf_naga"), "the server's loom made a banner with TF's naga pattern (" + gf::game::items::idOf(cursor) + ")");
        if (hasPattern(cursor, "tf_naga")) {
            for (auto const& [pp, color] : BannerItem::getPatternAndColorPairsFromItemStack(cursor))
                if (pp && *pp->mNameID == "tf_naga") ctx.check(static_cast<int>(color) == 1, "in the dye's colour, red (" + std::to_string(static_cast<int>(color)) + ")");
            gf::log().info("building: the server's loom crafted a banner with tf_naga for a client request");
        }
        ctx.check(uiItem(sp, PlayerUISlot::LoomInput).isNull(), "the banner was used up");
        ctx.check(uiItem(sp, PlayerUISlot::LoomDye).isNull(), "the dye was used up");
        ctx.check(gf::game::items::idOf(uiItem(sp, PlayerUISlot::LoomMaterial)) == "twilightforest:naga_banner_pattern", "the pattern item stays in the loom (LoomMenu keeps it)");
        clearLoom(sp);
        sp.getInventory().removeAllItems();
        sp.doDeleteContainerManager(false);
    }
    ctx.cleanupBlock(loomPos);
    ctx.cleanupBlock(p);
});

} // namespace

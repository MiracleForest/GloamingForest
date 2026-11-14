#pragma once

// Contract "client.render" (header owned by F0, implemented by F12; spike S5: Classic and
// Vibrant Visuals). Client build only.
//
// World-space rendering registries. Units never hook LevelRenderer or the actor renderers
// (rule R8); they register by id:
//
//     gf::client::render::registerBlockRenderer("gloamingforest:trophy_pedestal", drawPedestal);
//     gf::client::render::onWorld([](auto& ctx, float partial) {
//         gf::client::render::Lines lines(ctx);
//         lines.box(lo, hi, 0xFFFF0000);
//     });
//     gf::client::tint::registerBlock("gloamingforest:rainbow_oak_leaves", [](BlockPos const& p) { return rgbAt(p); });
//
// Durability / dose bar colours come from gf::item::setBar (feature/item/Item.h).

#ifdef GF_CLIENT

#include <functional>
#include <memory>
#include <string>

#include "mc/deps/core/math/Vec3.h"

class Actor;
class Block;
class BlockPos;
class ItemStack;

namespace gf::client::render {

/// The per-frame render context handed to renderers (camera, matrices, the engine's render
/// context). Valid during the callback only.
class RenderContext;

struct Flags {
    bool throughWalls = false; ///< drawn without depth test (red thread vision, protection boxes)
};
using BlockRenderFn = std::function<void(RenderContext& ctx, BlockPos const& pos, Block const& block, float partialTicks)>;
/// Extra rendering for every loaded block of `blockId` (block-entity renderer equivalent).
void registerBlockRenderer(std::string const& blockId, BlockRenderFn fn, Flags flags = {});

/// Where an item is drawn (Java's ItemDisplayContext), as bit flags.
enum ItemContext : unsigned { Gui = 1, FirstPerson = 2, ThirdPerson = 4, Ground = 8, Fixed = 16, Head = 32 };
using Contexts     = unsigned;
using ItemRenderFn = std::function<void(RenderContext& ctx, ItemStack const& stack, ItemContext where)>;
/// Custom item rendering (BlockEntityWithoutLevelRenderer equivalent) in `contexts`.
void registerItemRenderer(std::string const& itemId, ItemRenderFn fn, Contexts contexts);

using EntityRenderFn = std::function<void(RenderContext& ctx, Actor& entity, float partialTicks)>;
/// Extra rendering after the entity's own geometry (chains, beams, layers).
void registerEntityRenderer(std::string const& entityId, EntityRenderFn fn);

using WorldFn = std::function<void(RenderContext& ctx, float partialTicks)>;
/// Called once per frame in world space (after opaque geometry).
void onWorld(WorldFn fn);

/// Line drawing in absolute world coordinates, flushed when it leaves scope.
class Lines {
public:
    explicit Lines(RenderContext& ctx);
    ~Lines();
    Lines(Lines const&)            = delete;
    Lines& operator=(Lines const&) = delete;
    void line(Vec3 const& a, Vec3 const& b, unsigned argb);
    /// The 12 edges of the box [lo, hi].
    void box(Vec3 const& lo, Vec3 const& hi, unsigned argb);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

struct Transform {
    Vec3 translation{};      ///< world position
    Vec3 rotationDeg{};      ///< x, y, z (applied y, x, z)
    Vec3 scale{1.f, 1.f, 1.f};
};
/// Draws geometry `geoId` ("geometry.gloamingforest.naga_trophy") with `texture` (resource pack path).
void drawGeo(RenderContext& ctx, std::string const& geoId, std::string const& texture, Transform const& transform);

} // namespace gf::client::render

namespace gf::client::tint {
/// Position-dependent tint (0xRRGGBB) of a block (PositionalTintResolver; upstream's leaf and grass colours).
using TintFn = std::function<unsigned(BlockPos const& pos)>;
void registerBlock(std::string const& blockId, TintFn fn);
} // namespace gf::client::tint

#endif // GF_CLIENT

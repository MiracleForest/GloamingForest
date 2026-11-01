// Neutral stub of "client.render" (F0). F12 replaces this file (rule R18).
#ifdef GF_CLIENT

#include "client/render/Render.h"

#include "core/Stub.h"

namespace gf::client::render {

struct Lines::Impl {};

void registerBlockRenderer(std::string const&, BlockRenderFn, Flags) { GF_STUB("client.render.registerBlockRenderer"); }
void registerItemRenderer(std::string const&, ItemRenderFn, Contexts) { GF_STUB("client.render.registerItemRenderer"); }
void registerEntityRenderer(std::string const&, EntityRenderFn) { GF_STUB("client.render.registerEntityRenderer"); }
void onWorld(WorldFn) { GF_STUB("client.render.onWorld"); }

Lines::Lines(RenderContext&) : mImpl(std::make_unique<Impl>()) { GF_STUB("client.render.Lines"); }
Lines::~Lines() = default;
void Lines::line(Vec3 const&, Vec3 const&, unsigned) {}
void Lines::box(Vec3 const&, Vec3 const&, unsigned) {}

void drawGeo(RenderContext&, std::string const&, std::string const&, Transform const&) { GF_STUB("client.render.drawGeo"); }

} // namespace gf::client::render

namespace gf::client::tint {
void registerBlock(std::string const&, TintFn) { GF_STUB("client.tint.registerBlock"); }
} // namespace gf::client::tint

#endif // GF_CLIENT

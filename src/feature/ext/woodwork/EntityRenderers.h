#pragma once

// Contract extension of U4 woodwork for "client.render" (rule R18: new files only; A8 folds it into
// F12's src/client/render/Render.h). Client build only.
//
// The game picks an entity's renderer by its runtime identifier: a data-driven entity running on a
// game entity (a BP runtime_identifier such as minecraft:boat) is drawn by the game's own renderer
// for that entity (BoatRenderer: the vanilla boat textures), never with the client entity of its
// resource pack. Two rules: the renderer the game picks (ActorRenderDispatcher::_getRenderer, never
// null), and the entities ActorRenderDispatcher::render skips:
//
//     // upstream's boats: the geometry, upstream textures and paddle animation of their client entities
//     gf::woodwork_ext::renderers::useDataDriven("gloamingforest:boat");
//     // entities the client mod draws itself (the drying rack's item display): not drawn
//     gf::woodwork_ext::renderers::hideWhen([](Actor const& a) { return isMine(a); });
//
// useDataDriven falls back to the game's renderer when the resource packs have no client entity of
// the type. Render thread; the predicate runs once per drawn entity per frame: keep it cheap.

#ifdef GF_CLIENT

#include <cstddef>
#include <functional>
#include <string>

class Actor;

namespace gf::woodwork_ext::renderers {

/// Entities of type `entityId` are drawn with their resource pack client entity.
void useDataDriven(std::string const& entityId);
/// Entities for which `hidden` returns true are not drawn (no geometry, no held item); returns a
/// handle for show().
std::size_t hideWhen(std::function<bool(Actor const&)> hidden);
void        show(std::size_t handle);

/// How many entities each rule took since the start (tests and e2e trace lines).
[[nodiscard]] std::size_t dataDrivenUses();
[[nodiscard]] std::size_t hiddenUses();

void install();
void uninstall();

} // namespace gf::woodwork_ext::renderers

#endif // GF_CLIENT

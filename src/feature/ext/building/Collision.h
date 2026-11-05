#pragma once

// Extension of contract "families+blockevents" by U5 (rule R18; to be folded by A8 into F11's
// collision hook, src/feature/families/Shapes.cpp FamiliesCollisionHook).
//
// Entity-dependent collision of custom blocks (Java Block#getCollisionShape with a
// CollisionContext). A Bedrock custom block's minecraft:collision_box is one static box per
// permutation; the game asks BlockType::addCollisionShapes for an entity's movement with the
// moving entity (GetCollisionShapeInterface: its box, whether it descends through blocks, as for
// scaffolding). A registered provider replaces the block's boxes for those queries:
//
//   gf::building::ext::registerCollision("twilightforest:rope", [](Block const& b, BlockPos const& pos,
//       std::optional<gf::building::ext::CollisionEntity> e, std::vector<gf::building::ext::Box>& out) { ... });
//
// Queries without an entity (CollisionContext.empty()) get the provider with `entity` empty. Every
// other query of the game (getCollisionShape: spawn checks, pathfinding, camera) keeps the block's
// static minecraft:collision_box, which is also what vanilla clients predict with.

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class Block;
class BlockPos;

namespace gf::building::ext {

/// The entity a collision query is for (Java EntityCollisionContext).
struct CollisionEntity {
    float bottom     = 0;     ///< the entity's box min y (Java entityBottom)
    bool  descending = false; ///< Java isDescending (a sneaking player; Bedrock canDescendThroughBlock)
};
/// A box in block pixels (0..16 per axis, Java Block.box): x0, y0, z0, x1, y1, z1.
using Box = std::array<float, 6>;
using CollisionProvider =
    std::function<void(Block const& block, BlockPos const& pos, std::optional<CollisionEntity> entity, std::vector<Box>& out)>;

/// Registers the entity-dependent collision of a block id (one provider per id; later wins).
void registerCollision(std::string const& blockId, CollisionProvider provider);
/// Resolves the registered ids to block types (call when a world is ready: the client builds its
/// block registry per world).
void refreshCollisionTypes();
void installCollision();
void uninstallCollision();

} // namespace gf::building::ext

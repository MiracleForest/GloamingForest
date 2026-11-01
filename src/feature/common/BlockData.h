// Adapted from LiteLDev/Levitate v0.1.0 (src/feature/common/BlockData.h), LGPL-3.0-or-later.
#pragma once

// Contract "blockdata" (W0a): the block-entity replacement.
//
// Custom Bedrock blocks can't have block entities, so per-position state (drying rack contents,
// jar items, trophy rotation, candle data, sign text when S3 picks this backend, ...) lives here:
// one JSON object per (dimension, position), shared by top-level key (the owning unit's name or
// the block's path). Server thread only.
//
// - Persistence: grouped per chunk in the world DB (key "gf:common:bd:<dim>:<cx>:<cz>"), loaded
//   lazily on first access, written on `onWorldSave` (autosave / stop) when dirty.
// - Lifetime: an entry is erased automatically when the block at its position changes to a
//   different block type (broken, replaced, exploded, moved by a vanilla piston); block *state*
//   changes keep it. Erase listeners let owners drop items first.
// - Rotation: direction-dependent values of a key turn with the block (structure placement,
//   rotating tools) through `onRotate(key, fn)`.
//
//     namespace bd = gf::feature::common::blockdata;
//     bd::set(region, pos, "drying_rack", {{"item", items::toJson(stack)}, {"ticks", 0}});
//     if (auto const* rack = bd::get(region, pos, "drying_rack")) ...;

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"

class BlockPos;
class BlockSource;

namespace gf::feature::common::blockdata {

using Json = nlohmann::json;
using core::GlobalPos;

// ---- by key (the contract's form) ----------------------------------------------------------------

/// The value of top-level `key` in the document at (region, pos), or nullptr. Valid until the
/// next BlockData call.
[[nodiscard]] Json const* get(BlockSource& region, BlockPos const& pos, std::string const& key);
/// Sets top-level `key` of the document at (region, pos) (`null` removes the key; an emptied
/// document is erased).
void set(BlockSource& region, BlockPos const& pos, std::string const& key, Json value);

// ---- whole documents -------------------------------------------------------------------------------

/// The document at `pos`, or nullptr. The pointer is valid until the next BlockData call.
[[nodiscard]] Json const* get(GlobalPos const& pos);
/// The document at `pos` (created as {} when missing) for writing; marks the chunk dirty.
/// The reference is valid until the next BlockData call.
[[nodiscard]] Json& edit(GlobalPos const& pos);
/// Replaces the whole document (`null` erases it).
void set(GlobalPos const& pos, Json value);
/// Erases the document (runs erase listeners). Returns true if something was erased.
bool erase(GlobalPos const& pos);
/// Whether a document exists at `pos`.
[[nodiscard]] bool has(GlobalPos const& pos);

/// Removes the document and returns it without running erase listeners (moving a block).
[[nodiscard]] std::optional<Json> take(GlobalPos const& pos);
/// Stores a document taken with `take()` at a (new) position.
void put(GlobalPos const& pos, Json value);

/// Calls `fn(pos, doc)` for every document of chunk (dim, cx, cz) (loads it if needed).
void forEachInChunk(int dim, int cx, int cz, std::function<void(GlobalPos const&, Json const&)> const& fn);
/// Calls `fn(pos, doc)` for every document of every chunk that has data (loads them all;
/// commands and selftests only).
void forEach(std::function<void(GlobalPos const&, Json const&)> const& fn);

/// Listener run before a document is erased because its block changed or `erase()` was called:
/// `fn(pos, doc, previousBlockId)` (previousBlockId is "" for explicit erases).
using EraseListener = std::function<void(GlobalPos const&, Json const&, std::string const& previousBlockId)>;
void onErase(EraseListener fn);

/// Writes dirty chunks now (also done on every world save).
void flush();

/// Owners that keep their part of a document cached elsewhere write it back here: `fn(nullptr)`
/// runs before every save / `flush()`, `fn(&pos)` before `take(pos)`.
using WriteBackFn = std::function<void(GlobalPos const* pos)>;
void onWriteBack(WriteBackFn fn);
/// Listener run after `put()` stored a moved document: owners with a cache adopt it.
using PutListener = std::function<void(GlobalPos const& pos, Json const& doc)>;
void onPut(PutListener fn);
/// Direction-dependent data of top-level key `key` is turned with the block when it is rotated
/// by `quarterTurns` about `axis` (0 x, 1 y, 2 z; right-hand rule).
using RotateFn = std::function<void(Json& value, int axis, int quarterTurns)>;
void onRotate(std::string key, RotateFn fn);
/// Applies the registered rotations to a whole document.
void rotate(Json& doc, int axis, int quarterTurns);

} // namespace gf::feature::common::blockdata

namespace gf {
namespace blockdata = feature::common::blockdata;
} // namespace gf

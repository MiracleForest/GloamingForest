# landmark (F6): notes for the units that build on it

The contract is `src/feature/landmark/Landmark.h` (+ the pure grid in `src/core/landmark/Landmark.h`);
the extensions are `src/feature/ext/landmark/LandmarkExt.h` (terrain, layouts, pieces, features,
spawns, locate) and `src/feature/ext/landmark/LandmarkTest.h` (selftests). This page lists what the
structure units (U18-U30) and the other W0 units' tests need to know beyond the header comments.
(It belongs in `docs/units/landmark.md`, which has no owner row yet: `tools/spec/owners.ps1` gives
`docs/units/<name>.md` to the W1 units only.)

## Hollow hills: monsters spawn in the dome only (U18)

TF: `EntityEvents#gatherPotentialSpawns` asks `HollowHillStructure#canSpawnMob(pos,
start.getBoundingBox())` after the conquered check and before the piece lookup; outside the half
dome of the start box it answers `null`, so the hill's spawn override applies (an empty `full` list
for every category: no monsters there at all).

GloamingForest: **F6 applies it**, keyed on the structure type exactly like Java's `instanceof`:
every start whose descriptor has `type` `twilightforest:hollow_hill` (the small, medium and large
hollow hills) gets `gf::core::landmark::hollowHillCanSpawnMob(start.box, x, y, z)`
(`src/core/landmark/Spawning.h`, gtested) in `gf::landmark::ext::potentialSpawns`, before the spawn
index. U18 registers **no hook** for it. What U18's generator must do:

- `registerGenerator("twilightforest:{small,medium,large}_hollow_hill", {.layout = ...})` with the
  layout adding TF's `HollowHillComponent` (built on F7's `structure::bases::HollowHill`, a
  `bases::Component`) with TF's box: the start box (`Start::box`, the union of the pieces;
  terrain adaptation none) is what the dome is cut from, as Java's `start.getBoundingBox()`.
- Leave `Generator::spawnIndex` unset: the default lookup is `EntityEvents#getSpawnListIndexAt`, the
  highest `bases::Component::getSpawnIndex()` (TF's `spawnListIndex`, 0 for the hill) of the pieces
  holding the position. A `spawnIndex` replaces that lookup (it is for landmarks that pick the list
  themselves, such as the dark tower's floors); returning -1 means "no list here" (the override,
  else the biome's list).

The selftest `landmark.spawns` checks the dome on a small hollow hill start with a stand-in piece.

## Piece types and saved tags of a start (F7, U18-U30)

`gf::landmark::ext::startPieces(start)` (`LandmarkExt.h`) returns every piece of a stored start
(`StructureStart#getPieces`): type id, box, `StructurePiece#createTag` (`{"id", "BB", "O", "GD",
"gf_mirror", "gf_rotation"}` plus the piece's `Piece::save()` fields such as `"Template"`),
`protectedPiece` and `spawnIndex`. The start returned by `landmark::startAt` also carries the tags in
`data["pieces"]` (the shape F7's `structure_util display_pieces` and `count_template` read);
`startsIntersecting` leaves them out (it runs per feature placement). `startAt` saves every piece of
the start on each call: use it for one-off lookups (commands, tests, an interaction), not per tick or
per block; per-tick callers use `startsIntersecting` and the boxes, then `startPieces` for the one
start they act on. For a `Start` the store does not hold, `startPieces` gives its boxes with type `""`,
`protectedPiece` false and `spawnIndex` -1 (unknown: nothing claimed). A piece whose type has no
factory yet (a W1 unit's, before it registers) keeps the tag it was saved with: it is written back
unchanged and `startPieces` returns it. This is an extension and not a field of the contract's
`Start` because `Landmark.h` is F0's file (owners.tsv) and a `Start` copy must stay cheap.

## Reserved sites

Before a structure unit registers its generator, F6 stores a start for every site the structure
sets choose, without pieces (`Start::generated` false, the clearance square as its box). Such a
start is no `StructureStart#isValid` start: protection, conquest, terraforming, controlled spawns,
spawn overrides, `ext::featureBlocked` and `decorationAllowed` all skip it (`startAt` and
`startsIntersecting` still list it, with no `pieceBoxes`). Once the generator exists, the pieces are
rebuilt from its deterministic layout when the start chunk is evaluated again or a chunk the site
reaches generates.

## Selftests: forcing a start and removing it

`landmark::forceStart(key, pos)` stores a start in memory and the world DB, in start chunk
`(pos.x >> 4, pos.z >> 4)`. Remove it with `src/feature/ext/landmark/LandmarkTest.h`:

```cpp
gf::landmark::ext::ScopedStart forced("gf_test:landmark_fixture_open", center); // removed at scope end
ctx.require(forced && forced->generated, "forced the fixture landmark");
// or: gf::landmark::ext::forgetStartForTest(center, "gf_test:landmark_fixture_open");
```

`ScopedStart` puts back the start of the same key and start chunk that its forced start replaced (a
natural one or an earlier forced one), as it was, its conquered flag included. `forgetStartForTest`
by hand instead forgets the chunk's structure set evaluation, so a natural start that a forced one
replaced is made again on the next lookup: unconquered (forceStart copied its flag onto the forced
start, and removing that loses it). Server thread only. Do not include `feature/landmark/Internal.h`
from another unit.

The task text of W0d asked for a `"piece"` spawn override on `gf_test:landmark_fixture`. It is on
`gf_test:landmark_fixture_open` instead: a ControlledSpawns start (the locked fixture) answers every
non-monster category itself and its monster lists come from `Generator::spawnIndex`, as TF's
`EntityEvents#gatherPotentialSpawns` does, so a monster override there would apply where the spawn
index gives no list: the shell's ground floor, which F5's `worldgen.landmark_rules` expects to have
none. Tests of `structureSpecialSpawns`' override path use the open fixture.

The fixtures (development builds, `src/feature/landmark/Helpers.cpp`), both a 7x7x7 stone brick
shell around the center (x and z within 3, y up to +6) and a 7x1x3 oak porch in front of it
(z +4..+6, y +0), piece types `gf_test:landmark_fixture_shell` / `_porch` (registered with
`structure::registerPieceType`):

| Key | Lock | Spawns |
|---|---|---|
| `gf_test:landmark_fixture` | `twilightforest:progress_naga` (the shell is protected, the porch not); hint book `lichtower` | ControlledSpawns: monster list `1` (skeleton 4 2-3, spider 1 1-2) on the shell's upper floor (y >= center + 4), list `0` (zombie 1 1-1) on the porch, no list on the shell's ground floor or beside it (`Generator::spawnIndex`); empty ambient and water lists; no creatures in the chunks it reaches |
| `gf_test:landmark_fixture_open` | none (conquerable, protects nothing) | spawn overrides: `monster` `piece` (witch 3 1-1, creeper 7 2-4) in the shell and the porch, `ambient` `full` (empty) in the whole box |

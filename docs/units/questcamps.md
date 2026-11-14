# U19 `questcamps`: implementation notes

What the code and the original templates of U19 do beyond docs/spec/questcamps.md.

## Code

| File | What |
|---|---|
| `src/core/questcamps/Questcamps.{h,cpp}` | Dye colours, the ram's colour flags and model pose, the template features' ground averaging, the graveyard grid (gtests `tests/questcamps`). |
| `src/feature/questcamps/Quest.cpp` | `gloaming/quests/questing_ram` (fallback: the 16 wools), Ingredient matching (`item`, `tag`, lists). |
| `src/feature/questcamps/QuestRam.cpp` | The Questing Ram: quest flags (entity data `questcamps` + property `gloamingforest:color_flags`), feeding, `QuestRamEatWoolGoal`, `TemptGoal`, the restriction point, the reward, `questcamps.sync_quests`. |
| `src/feature/questcamps/Grove.cpp` | `TFQuest1` and the `gloamingforest:quest_grove` landmark generator (BEARD_THIN). |
| `src/feature/questcamps/Camp.cpp` | The `gloamingforest:camp` generator: `CampStructure#findGenerationPoint` + F7's jigsaw assembly, BEARD_BOX per pool entry. |
| `src/feature/questcamps/TemplateFeatures.cpp` | `TemplateFeature` (druid hut, simple / fancy well, grove ruins, stone circle) and `GraveyardFeature`. |
| `src/client/questcamps/Indicator.cpp` | `QuestingRamIndicatorOverlay` as an F12 HUD layer. |
| `src/feature/ext/questcamps/W0Internals.h` | The W0 internals U19 uses (rule R18 request for A8). |

The grove's ram is spawned through F7's queued template entities; its restriction point waits in
`gf:questcamps:homes` until the ram first ticks at the marker (FeaturePlacers.placeEntity).

## Templates (rule R19)

All 57 are original designs written with the F7 DSL in `tools/structures/questcamps/*.gfs.ps1`.
Their ids are upstream's own (R1): the camp's templates `gloamingforest:camp/...` stand in for upstream's template
definitions of the same ids (pools `gloamingforest:camp/<pool>` from the imported definitions, which
assemble knows), the template features' `gloamingforest:feature/<path>` (upstream has no definition for
them) and the grove `gloamingforest:quest_grove`.

Camp jigsaw convention (`QuestcampsLib.ps1`): layer 0 of starts, tents and decos is the ground air
level; paths have layer 0 under the surface, layer 1 the surface (the pool's gravity processor) and
the connectors on layer 2. Seed connectors are named `gloamingforest:camp/structure_start` (front
down, top south), `gloamingforest:camp/tent`, `gloamingforest:camp/path` and `gloamingforest:camp/deco`;
parent connectors target those names. Starts lead to tents, a main path and (two of them) a rack path;
main paths lead to paths, tents and decos; paths end in decos (racks through `rack_path`'s pool alias).

## Client assets

`tools/structures/questcamps/make-ram-assets.ps1` derives the ram's tinted texture (16 copies of
the wool segment's UV box tinted with `Sheep#getColor`; `tf-derived`, CC BY-NC-SA 4.0), the tinted and
the 1.025-scaled lines geometries, the pose animation (`QuestRamModel#prepareMobModel` from the packed
colour property) and the client entity / render controllers from F1's and F2's files.

## Audit round 1

Checked (A1, W2 round 1); nothing changed in U19's code this round:

- **Template ids**: the camp and feature templates still use `gloamingforest:` ids. They go back to
  `gloamingforest:` once assemble knows template pool ids (being fixed in F0's assemble this round);
  not merged when this audit ran.
- **Graveyard**: `gloamingforest:feature/graveyard/grave_lower` and `grave_full` use
  `minecraft:rooted_dirt`, which F7's Java-to-Bedrock block table (`renames()` in
  `src/feature/structure/BlockStates.cpp`) does not map (`... has no Bedrock block (processed with no
  block, not placed)` in every full run): those cells are left as they were. The fix is F7's: map it to
  `minecraft:dirt_with_roots` (the features kit already does). Reported.
- **Camp pots**: `structure: no container accepts loot table gloamingforest:camp_pot at a
  minecraft:decorated_pot` (every full run): upstream's camp decorated pots carry a loot table
  (DecoratedPotBlockEntity is a RandomizableContainer); F10's loot / inventory contract has no
  handler for Bedrock's decorated pot (one item stack), so the camp's pots are empty. Reported to
  F10.

## Audit round 2

Checked (W2 round 2) the round-1 items, the indicator against QuestingRamIndicatorOverlay and the
R19 goldens.

Fixed:

- **Template ids**: the camp's templates, pools and jigsaw connector names are `gloamingforest:camp/...`
  and the template features' `gloamingforest:feature/...` (upstream's ids, R1) now that assemble knows the
  template pool ids of the imported definitions. `Camp.cpp` no longer registers alias copies of upstream's
  camp template definitions: the jigsaw uses upstream's definitions directly. `tools/structures/build.ps1
  -Check` now validates the 35 camp templates against those definitions (204 of 205 designed).
- **Indicator**: drawn with the crosshair's inverting blend (F12 `ui::blitSprite(..., Blend::Invert)`,
  Upstream's `blendFuncSeparate(ONE_MINUS_DST_COLOR, ONE_MINUS_SRC_COLOR, ONE, ZERO)`), only in first person
  (Bedrock's view perspective 0; upstream `getCameraType().isFirstPerson()`); never for spectators
  (`canRenderCrosshairForSpectator` answers an entity hit only for a MenuProvider).
- **Goldens (R19)**: besides the raw template hashes and the camp's layout hash, the placed grove
  (`grove_placed`, questcamps.grove_layout) and the post-processed seeded camp (`camp_placed`,
  questcamps.camp_assembly) have golden volume hashes. The camp layout hash changed with the template
  ids it hashes. `grave_full` / `grave_lower` place `minecraft:dirt_with_roots` for upstream's rooted dirt
  (F7's table maps it since round 1); their goldens were refreshed then and still match.
- **questcamps.graveyard timeout**: `flatArea` gives an area's chunks 200 ticks (10 s) to load; the
  Gloaming Forest's 9 to 16 fresh chunks of a 48-block area run the whole generator, which under a
  loaded 'full' run took longer ("every chunk of the area loaded within 10 s"). The Gloaming tests
  (druid hut, wells, graveyard, ruins) now stand the player at the area's center and give the chunks
  up to 60 s before `flatArea` (`gloamingArea`). F0 could make flatArea's own wait longer for every unit.

Still open:

- The camp's decorated pots stay empty (F10, round 1).
- The goldens' one-time review with client screenshots: the client scenario was not run this round.


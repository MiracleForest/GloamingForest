# U19 `questcamps`: implementation notes

What the code and the original templates of U19 do beyond docs/spec/questcamps.md.

## Code

| File | What |
|---|---|
| `src/core/questcamps/Questcamps.{h,cpp}` | Dye colours, the ram's colour flags and model pose, the template features' ground averaging, the graveyard grid (gtests `tests/questcamps`). |
| `src/feature/questcamps/Quest.cpp` | `twilight/quests/questing_ram` (fallback: the 16 wools), Ingredient matching (`item`, `tag`, lists). |
| `src/feature/questcamps/QuestRam.cpp` | The Questing Ram: quest flags (entity data `questcamps` + property `twilightforest:color_flags`), feeding, `QuestRamEatWoolGoal`, `TemptGoal`, the restriction point, the reward, `questcamps.sync_quests`. |
| `src/feature/questcamps/Grove.cpp` | `TFQuest1` and the `twilightforest:quest_grove` landmark generator (BEARD_THIN). |
| `src/feature/questcamps/Camp.cpp` | The `twilightforest:camp` generator: `CampStructure#findGenerationPoint` + F7's jigsaw assembly, BEARD_BOX per pool entry. |
| `src/feature/questcamps/TemplateFeatures.cpp` | `TemplateFeature` (druid hut, simple / fancy well, grove ruins, stone circle) and `GraveyardFeature`. |
| `src/client/questcamps/Indicator.cpp` | `QuestingRamIndicatorOverlay` as an F12 HUD layer. |
| `src/feature/ext/questcamps/W0Internals.h` | The W0 internals U19 uses (rule R18 request for A8). |

The grove's ram is spawned through F7's queued template entities; its restriction point waits in
`gf:questcamps:homes` until the ram first ticks at the marker (FeaturePlacers.placeEntity).

## Templates (rule R19)

All 57 are original designs written with the F7 DSL in `tools/structures/questcamps/*.gfs.ps1`.
TF's feature templates have no template definition: their GloamingForest ids take the
`gloamingforest:` namespace (`gloamingforest:feature/druid_hut/druid_hut` stands for TF's
`twilightforest:feature/druid_hut/druid_hut`).

Camp jigsaw convention (`QuestcampsLib.ps1`): layer 0 of starts, tents and decos is the ground air
level; paths have layer 0 under the surface, layer 1 the surface (the pool's gravity processor) and
the connectors on layer 2. Seed connectors are named `twilightforest:camp/structure_start` (front
down, top south), `twilightforest:camp/tent`, `twilightforest:camp/path` and `twilightforest:camp/deco`;
parent connectors target those names. Starts lead to tents, a main path and (two of them) a rack path;
main paths lead to paths, tents and decos; paths end in decos (racks through `rack_path`'s alias).

## Client assets

`tools/structures/questcamps/make-ram-assets.ps1` derives the ram's tinted texture (16 copies of
the wool segment's UV box tinted with `Sheep#getColor`; `tf-derived`, CC BY-NC-SA 4.0), the tinted and
the 1.025-scaled lines geometries, the pose animation (`QuestRamModel#prepareMobModel` from the packed
colour property) and the client entity / render controllers from F1's and F2's files.

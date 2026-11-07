# fauna (U16): passive and ambient fauna

Implementation notes of U16 against [docs/spec/fauna.md](../spec/fauna.md).

## Layout

| Path | What |
|---|---|
| `src/core/fauna/` | Pure rules (gtested in `tests/fauna/`): dyes and their vanilla two-dye mixes, the fleece colour draw, the bighorn loot table by colour, the variant picks (biome filter, common variants, the dwarf rabbit kit's inheritance), the bird flap and flight steering, the kobold's eating delay and the mosquito's hunger by difficulty. |
| `src/feature/fauna/` | `Animals.cpp` (boar, bighorn, deer, squirrel, dwarf rabbit, penguin), `Birds.cpp` (FlyingBird: tiny bird, raven), `Monsters.cpp` (kobold, mosquito swarm), `World.cpp` (spawn placements and rules, `addPrey`, parrot imitations), `Common.cpp` (colour, variant, eye height helpers; the variant registries), `Tests.cpp` (selftests `fauna.*`, dx 1600..1699). |
| `src/feature/ext/fauna/W0Internals.h` | The only include of F8's `EntityInternal.h` and F9's `DataFiles.h` (R18 request for A8). |
| `assets/pack/bp/entities/fauna/` | The ten BP entities: the vanilla goals as Bedrock behaviours with TF's priorities and speeds, adult / baby groups (breedable, ageable), the bighorn's colour / sheared groups, the variant groups, the flying birds' landed / flying groups, the properties `twilightforest:landed` and `twilightforest:panicked`. |
| `assets/pack/rp/{entity,animations,render_controllers,models/entity}/fauna/` | Client entities, the setupAnim translations F2 left (bird bob, landed feet, raven head tilt, kobold jaw, horns / antlers hidden on babies, the sheep's grazing), the variant texture arrays and the bighorn's fleece pass (vanilla SheepFurModel cubes, Bedrock's sheep texture and `sheep` material tinted by `minecraft:color`). |
| `assets/pack/rp/sounds.json.d/fauna.json` | The entity sounds (ambient, hurt, death, the step at 0.15); the tiny bird's ambient is native (song 1 in 20). |
| `tools/import/overrides/fauna.json` | Inlines Java 1.21.1's `minecraft:entities/sheep` and `minecraft:entities/rabbit` into the bighorn and dwarf rabbit tables (Bedrock has no Java vanilla entity tables). |

## State

- Colour: `minecraft:color` (the DyeColor id), set through the BP's `set_color_<n>` events, read from the
  actor's synced `ColorIndex`. Sheared: `minecraft:is_sheared` (events `shear`, `regrow_wool` from
  `behavior.eat_block`).
- Variants: `minecraft:variant` (tiny bird blue 0, brown 1, gold 2, red 3; dwarf rabbit brown 0, dutch 1,
  white 2) through `set_variant_<n>`, plus the registry id in `gf::entity::data(actor, "fauna:variant")`
  (Java's saved `variant` string, restored on load; unknown ids keep the variant).
- Spawn set-up (`finalizeSpawn`) runs once per actor (`fauna:init`): babies born near two adults take the
  lamb colour / kit variant of the nearest two (Bedrock's breeding gives no parent references).
- The kobold's timers are saved as `fauna:kobold` {`EatingTimeLeft`, `TimeSinceBreadLastEaten`}.
- FlyingBird's landed flag, flight target and flight time are not saved (Java does not save them either:
  a loaded bird starts landed).

## Bedrock limits

- Goals with Java priorities run in two selectors (the BP's behaviours and F8's native goals: the
  kobold's flock panic, bread seeking and flocking, the flying birds' flight); they do not share flags.
- One `behavior.random_stroll` per entity: the squirrel's second stroll (1.25) and the dwarf rabbit's
  second (1.0) are not ported.
- Walk target values only affect spawning (`Mob#checkSpawnRules`); Bedrock's stroll picks its own spots.
- The mosquito swarm still shows the fire overlay when burning (no render hook for it).
- Babies are drawn at half scale (Bedrock's `minecraft:scale`), without Java's bigger baby heads.
- `twilightforest:jeed/hunger` (a JEED compat recipe) is not ported: JEED does not exist on Bedrock.

## Audit round 1

A5 (mobs audit, W2 round 1), against F13's goldens (tests/golden/mobs.json, loot.json) and TF 4.8; U17's
spawn sweep (`hostiles.spawn_sweep`, `hostiles.mob_assets`) covers the ten fauna mobs too.

- **Checked, matching**: max health, movement speed, attack damage, follow range, box and fire immunity of
  all ten mobs; the loot tables; hurt / death sounds (the mosquito swarm's are null in Java and absent
  here); far-away despawn (the kobold and the mosquito swarm despawn, the animals stay); every setupAnim
  todo of the F2 bones files (horns / antlers hidden on babies, the bighorn's grazing, the flying birds'
  landed feet, the kobold's jaw while rising); the bighorn's fleece (vanilla SheepFurModel on Bedrock's
  64 x 64 sheep texture: the wool UVs are already the +32 rows).
- **Fixed**: dyeing a bighorn plays `item.dye.use` (DyeItem#interactLivingEntity; Bedrock's `sign.dye.use`).
  The kobold and the mosquito swarm leave on Peaceful (Monster#shouldDespawnInPeaceful; Bedrock kept
  them), with the selftest `fauna.peaceful`.
- **Still open**: none found beyond the Bedrock limits above.

## Audit round 2

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 26 of this unit's sound events still played F3's name-derived baseline placeholder. Deer, kobold, mosquito, penguin, raven, squirrel and tiny bird events are refined; the parrot imitations play the imitated owners' events (aliases: 10 events of caves, darktower, lich, hostiles, hydra, aurora, labyrinth and naga in `assets/gen/audio/fauna/aliases.json`); 8 of the imitations were baseline only because the owners' events were, and they follow this round's hostiles, caves, naga and aurora refinements; the alias copies were refreshed by rendering fauna after every other owner (`synth.ps1 -Unit fauna` again whenever one of those owners changes its event). `tools/sounds/recipes/fauna.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; TF's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit fauna` into `assets/gen/audio/fauna/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Open: nothing for audio.

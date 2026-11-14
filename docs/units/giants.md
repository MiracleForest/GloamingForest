# U29 giants: Beanstalk, cloud castle and giants

Spec: [docs/spec/giants.md](../spec/giants.md). Code: `src/core/giants` (gtested in `tests/giants`),
`src/feature/giants`, `src/client/giants`, `src/feature/ext/giants` (contract extensions).

## How it maps to Bedrock

| Upstream | GloamingForest |
|---|---|
| `GrowingBeanstalkBlockEntity` | BlockData key `grower` (`core::giants::BeanstalkState`, upstream's saved fields) + `gf:giants:growers`; the module ticks every loaded grower each server tick (`tickBeanstalk`, exact translation with Mth.sin's table) |
| Beanstalk rumble | dust: block particles (20 a tick, sent by the server); camera shake: `giants.rumble` to companions (F12 `camera::shake`) |
| `SpecialStemLeavesBlock` | F11 leaves family; `huge_stalk` carries the families `log` flag so the leaf distance counts from it |
| `UberousSoilBlock` | F11 `onNeighborChanged` / `onPlaced`, `interactions::onPlace` (getStateForPlacement), bone meal through `BlockType::onFertilized` (15 times on the next tick) |
| `GiantBlockModel` | block states `gf:giant_tiled` (false = untiled item / unfixed block) and `gf:giant_x` / `_y` / `_z` (0..3, the cell position; Bedrock allows 16 values per state; gf: so the frozen upstream states stay as imported) whose permutations pick one of 64 geometries with the face's 4 x 4 sub-square and only the cell's outer faces: every client, vanilla included, draws the tiling |
| `GiantBlock` placement / removal | `interactions::onUseOn` refuses an occupied cell, `onPlace` fills it next tick, F11 `onRemoved` removes an incomplete cell without drops |
| Giant pickaxe mining + loot modifier | `interactions::onDestroy` / `onDestroyed`, F10 `registerCondition("gloamingforest:giant_pick_used")` and `addGlobalModifier("gloamingforest:giant_block_grouping")` |
| Cloud weather | F11 random ticks (cauldrons, snow layers; farmland moisture and fire out under rainy clouds); `rainingFromCloudAt` = BlockHooks#isRainingAt's scan |
| CloudEvents rendering | client mod: drops (`gf:giants_cloud_rain` / `_snow`) and rain splashes from the clouds near the camera; vanilla clients: the same drops sent by the server |
| Giants | BP goals / attributes; native gear, `gloamingforest:ant` melee, spawn rule |
| `giant_house` | F6 generator creating U27's `gloamingforest:TFClCa` from its saved form (R2), registered once the piece type exists |

## Deviations

- Giants render with the default skin (Bedrock cannot bind another player's skin to a mob);
  `client.giantSkinUUIDs` is kept in the config section but unused.
- The rain / snow under clouds are falling particles, not upstream's sheets; vanilla clients get them from
  the server (4 a tick per player).
- Entity wetness under rainy clouds is emulated by the unit (fire out, drown damage to water-sensitive mobs);
  other isRainingAt consumers (lightning rods, fishing, riptide) do not see the clouds.
- The giant pickaxe / sword's +2.5 interaction range has no Bedrock attribute (not reproduced).
- The giant block outline is drawn besides Bedrock's own highlight (it cannot be replaced).
- Up / down faces of the tiled geometry follow Blockbench's Bedrock UV convention (both axes flipped):
  to be checked on the post-merge client screenshots.

## Audit round 1

Checked against upstream 4.8 (GrowingBeanstalkBlockEntity, CloudCastleComponent via U27's pieces, GiantPickItem /
GiantSwordItem, CloudEvents, BlockHooks#isRainingAt).

Fixed:
- `giants.cloud_castle_hash` golden committed (`18f7c25dab5940c5`, the same value on two independent runs); the
  one-time screenshot review is the client scenario `giants.ps1` (not run this round).
- The beanstalk rumble dust: 20 block particles a tick like upstream (was 5).

Open (round 2, all Bedrock limits; not-ported candidates):
- Giant skins (a mob cannot wear another player's skin).
- The giant pickaxe / sword's +2.5 interaction range (no reach attribute; the server validates reach).
- Cloud rain / snow sheets (falling particles instead) and entity wetness under rainy clouds (no isRainingAt hook
  that units may own; R8).
- The giant block outline replacing the highlight (drawn beside Bedrock's own).

## Audit round 2

Checked against upstream 4.8 (`GiantMiner#doHurtTarget`, `GiantMiner` / `ArmoredGiant` sounds, `BlockHooks#isRainingAt`,
`Entity#move` / `LivingEntity#aiStep` under rain, `TFGiantRenderer`).

Fixed:

- **Ant damage inside the hit**: GiantMiner#doHurtTarget's `gloamingforest:ant` damage is dealt inside the
  combat handler (F10 runs the handlers of a nested hurt), not on the next tick; `giants.giant_mobs` checks it
  at once.
- **Hurt / death sounds** (A5): the giants are plain `Monster`s in Java (`entity.hostile.hurt` /
  `entity.hostile.death`, both the damage hit sounds): `assets/pack/rp/sounds.json.d/giants.json` plays
  Bedrock's `game.player.hurt` (the same `damage/hit1..3`) for both, pitch 0.8..1.2.
- **Entity wetness under rainy clouds**: an entity whose feet or head block has a raining cloud above it
  (`isRainingAt` through the clouds, where the level itself does not rain) is wet: its fire goes out with the
  extinguish fizz (`Entity#move`), and an enderman, blaze, snow golem or strider takes 1 drown damage
  (`LivingEntity#aiStep`, isSensitiveToWater). Selftest `giants.cloud_wetness`.
- **Cloud castle review**: `giants.client_showcase` builds a giant_house's cloud castle ahead of the viewer at
  y 168 and moves the viewer up to it; the client scenario shoots `giants-castle-<mode>` (rule R19's one-time
  review; written, not run).

Open (Bedrock limits; not-ported candidates):

- Giant skins (`TFGiantRenderer` binds the viewer's own skin, or `giantSkinUUIDs` profiles): a Bedrock mob's
  texture comes from the resource pack; binding a player's skin texture would need an F12 entity texture hook.
- The giant pickaxe / sword's +2.5 block and entity interaction range: Bedrock has no reach attribute and the
  client limits its pick range; needs an F12 client pick-range extension.
- Cloud rain / snow sheets (falling particles instead; F12's `drawGeo` has no scrolling UVs for a sheet).
- Other isRainingAt consumers under clouds (lightning rods, fishing, tridents' riptide, skeleton horse traps):
  engine-side, not reached.
- The giant block outline replacing the highlight (drawn beside Bedrock's own; replacing it is F12's renderer).

Sounds (W2 round 2, audio pass). Checked with `tools/sounds/lint.ps1` on the assembled pack (`tools/assemble.ps1 -Check`): before this round 1 of this unit's sound events still played F3's name-derived baseline placeholder. Beanstalk growth. `tools/sounds/recipes/giants.json` now designs every one of them (original synthesized sounds, unit-local patches layered from the shared library; upstream's audio never read; ids, subtitles, categories, volumes, pitches and variant counts unchanged), rendered with `pwsh tools/sounds/synth.ps1 -Unit giants` into `assets/gen/audio/giants/`. `synth.ps1 -Check` and `lint.ps1` pass; with the other round-2 audio PRs (one per owning unit) the lint reports 0 baseline-only events for the whole pack.

Also: `assets/pack/rp/sounds.json.d/giants.json` gives the giant miner and armored giant Monster's default hurt/death sounds (Java `entity.hostile.hurt` / `entity.hostile.death` -> Bedrock `game.player.hurt` / `game.player.die`); before, they had no entry (hostiles.mob_assets audit). Bedrock's own `entity_sounds.defaults` may already supply these two events; the explicit entry states upstream's Monster defaults either way and is what the audit reads. Open: nothing for audio.

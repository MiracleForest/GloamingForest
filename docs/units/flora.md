# U6 flora: unit notes

Behaviour spec: [docs/spec/flora.md](../spec/flora.md).

## Audit round 2

Fixed:
- Cicada and cicada jar notes (`src/client/flora/FloraClient.cpp`): `minecraft:note_particle` was emitted
  with an empty molang map, so every note logged a content-log error for the unset `variable.note_color`
  (about 170k lines in the atmosphere client scenario). The notes now carry NoteParticle's colour of
  speed 0 (upstream adds `ParticleTypes.NOTE` with speed (0, 0, 0)).
- Shears on berry bushes: `BerryBushBlock#getDestroyProgress` gives shears a fixed progress of 0.2 per
  tick. The eight overworld and dark tower bushes get `item_specific_speeds` for `minecraft:shears` of
  0.833333 s (Bedrock gives an entry of `s` seconds `5 / (30 s)` per tick with shears, as measured by U3
  for leaves). The patch is in `tools/import/overrides/flora.json`.
- Mason jar light: the jar now emits the light of a stored block item. It uses a GloamingForest state,
  `gloamingforest:jar_light` 0..15 (the import override adds the state and 15 light permutations),
  which `jars::setContents` sets from the stored block's light emission. Block data (lid, rotation,
  contents) survives the state change. Checked in `flora.jars`.
- Tints (client mod): fallen leaves take upstream's biome foliage colour (U1's `biomes::ext::foliageColor`;
  FoliageColor's default without a world). The huge lily pad takes 0x208030 in the world and 0x71C35C
  without one (ColorHandler).

Still open:
- Hoppers and the mason jar: upstream lets a hopper insert from above only (the item handler on the UP side).
  The jar keeps its slot in block data, which the game's hoppers cannot see, and F11's hopper bridge
  (`feature/ext/families/Hoppers.h`) has no per-side rule. Closing this needs a native container
  (`minecraft:block_entity`) plus a per-side hopper rule in F11.
- The jar light state has no effect on stacks loaded into the jar by other means than `setContents`
  (for example a loot table merge).

# U21 `lich`: Lich tower and Lich

What the unit implements, how it maps TF 4.8 onto Bedrock, and what is still missing. The behaviour spec is
[docs/spec/lich.md](../spec/lich.md).

## Code

| Path | What |
|---|---|
| `src/feature/lich/Lich.cpp` | `Lich.java` and the four Lich goals (shadows, minions, pop mobs, absorb minions), the go-home / melee / target goals as the Lich overrides them, teleporting (`findVecInLOSOf`, `teleportToNewTarget`, `teleportHome`), clones, the candle spawn sequence and restore, the 295-tick death animation (`Lifecycle::tickDeath`), the boss bar style per phase, the boss / spawner / lifecycle registration with F8's kit, `Lich#hurt` in the combat pipeline (priority High) |
| `src/feature/lich/Mobs.cpp` | `LichMinion` (master binding, kill without master, the Lich-hurt boost, baby rule on Hard), `DeathTome` (lectern mimic with the looking-at test, ranged attack goal, fire x2, hurt loot), `RisingZombie` (looking-at trigger, 130-tick rise, conversion to `minecraft:zombie`) |
| `src/feature/lich/Projectiles.cpp` | `LichBolt` (reflection on a hit, `canHitEntity`, deflection by a phase 2-3 Lich), `LichBomb` (explosion power 2 without block damage, damage type `twilightforest:lich_bomb` for the owner), `TomeBolt` (lost_words / schooled, slowness by difficulty, passes the owner's lectern) |
| `src/feature/lich/Tower.cpp` | `LichTowerStructure` and the lichtowerrevamp pieces (foyer, foyer decor, base, base trim, segments, boss room / boundary / roof, spawner bridges, wing bridges, wing rooms with ladders / roofs / beards / decor, room decor, magic gallery), their piece type factories, the spawn labels and the terraformer |
| `src/feature/lich/Module.cpp` | registration, the `twilightforest:is_minion` loot condition, the level tick (phase-1 boss bar progress) |
| `src/core/lich/TowerMath.*` | pure parts (BlockPos#asLong, Mth tables, boss room candle ring, stair decay levels), gtested in `tests/lich` |
| `tools/structures/lich/**` | the original template designs (rule R19): `root/` (tower, bridges, mob bridges, placeholder rooms) and `gallery/` |
| `tools/import/overrides/lich.json` | the client entity definitions (geometry, textures, animations, render controllers) and the `angry_lich` particle, patched into the imported files (the RP `entity/` folder belongs to F8's path rule) |
| `assets/pack/bp/entities/lich/*` | the BP entities (properties, navigation, behaviours for the minion and the tome) |
| `assets/pack/rp/{animations,render_controllers,sounds.json.d}/lich*` | death animation, legs, tome lectern pose, rising zombie, the shadow clone render controllers, entity sounds |

## Bedrock adaptation

- Synced data are entity properties: `twilightforest:phase`, `twilightforest:shield`, `twilightforest:clone`,
  `twilightforest:death_time` (Lich), `gf:on_lectern` (Death Tome), `twilightforest:rising_ticks` (rising zombie). They
  drive the RP on every client, vanilla included.
- Shadow clones render with a TF-derived translucent texture (`textures/twilightforest/lich/tf-derived`, the
  SHADOW_CLONE colour 0.333 x RGB, alpha 0.5) plus the eyes texture as an emissive layer.
- `Lich#hurt` runs as a `gf::combat::onIncoming` handler (priority High) so it sees the direct entity (shield break
  particles); the rest of `hurt` (hits without teleport) is an `onDealt` handler.
- The boss kit sets the bar's progress to the health fraction every tick; in phase 1 the unit sets it back to
  shield / 6 from a level tick listener (two updates a tick while the shield is up).
- A hit on a lich bolt or bomb comes through `interactions::onAttack`; the BP gives them a `minecraft:custom_hit_test`
  of 2.25 blocks (Java's pick radius 1.0). The owner change of a reflected bolt is kept by the unit (F8's projectile
  extension has no `setOwner`).
- Jigsaw junction names use the `gf:` namespace (`gf:lich_tower/bridge`, `gf:lich_tower/room`,
  `gf:lich_tower/roof`, `gf:lich_tower/beard`, `gf:lich_tower/decor`, `gf:ladder_below/<n>`, `gf:shelf`,
  `gf:mob_bridge`, `gf:lich_tower/bridge_center`, `gf:lich_tower/tower_below` / `tower_above`): assemble rejects
  `twilightforest:` strings it does not know, and TF's junction names are no registered ids. The templates TF's code
  names directly have `gf:lich_tower/...` ids (`tower_foyer`, `tower_base`, `central_trim`, `tower_slice`,
  `tower_boss_room`, `tower_boss_boundary`, `tower_boss_roof`, `foyer_decor`). **U22's room, roof, beard and decor
  templates must use these junction names.**
- While U22's room templates are missing, a wing's room is a placeholder room of the same size
  (`gf:lich_tower/placeholder/<n>x<n>`).

## Not ported

- `twilightforest:clone_count`, `twilightforest:minion_count` (and U9's `twilightforest:max_shield_strength` that
  the Lich reads): Bedrock has no custom attributes and no `/attribute`; the values are actor data with TF's defaults
  (2, 9, 6) and cannot be changed in game, with or without the client mod (for `docs/spec/not-ported.md` section 4).

## Not implemented

- The yard: `LichYardBox`, `LichPerimeterFence`, `LichYardGrave`, `LichYardLights` and their templates (`grave`,
  `grave_ajar`, `grave_open`, `outer_fence_1..7`); with them the yard's controlled spawns (label 0: rising zombies
  never spawn naturally).
- `LichTowerBase.TrimProcessor`, the room markers' block entities (mason jars, chiseled canopy shelves, brewing
  stands, sinister spawner data, zombie traps, the lectern's writable book and the Death Tome mimic), the lectern
  tomes (`addTomesToLecterns`), the gallery's magic painting (U22's entity) and the `tree` decor marker's TF trees.
- The rising zombie's block-break pitch (Bedrock plays the block's own break sound).
- Phase-2 deflection of U9's `twilightforest:wand_bolt` (its hit is U9's projectile code).
- The Lich's held-item hiding while teleport-invisible on the client mod (the invisible flag hides the whole actor).

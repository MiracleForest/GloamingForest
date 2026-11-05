# U21 `lich`: Lich tower and Lich

What the unit implements, how it maps TF 4.8 onto Bedrock, and what is still missing. The behaviour spec is
[docs/spec/lich.md](../spec/lich.md).

## Code

| Path | What |
|---|---|
| `src/feature/lich/Lich.cpp` | `Lich.java` and the four Lich goals (shadows, minions, pop mobs, absorb minions), the go-home / melee / target goals as the Lich overrides them, teleporting (`findVecInLOSOf`, `teleportToNewTarget`, `teleportHome`), clones, the candle spawn sequence and restore, the 295-tick death animation (`Lifecycle::tickDeath`; clones too), the boss bar style per phase, `Lich#deflection` (lich bolts and bombs in their hits, U9's wand bolt before its hit), the boss / spawner / lifecycle registration with F8's kit, `Lich#hurt` in the combat pipeline (priority High) |
| `src/feature/lich/Mobs.cpp` | `LichMinion` (master binding, kill without master, the Lich-hurt boost, baby rule on Hard, the Zombie's drowned conversion, reinforcements on Hard and natural armor 2), `DeathTome` (lectern mimic with its seat and the looking-at test, ranged attack goal, fire x2, hurt loot), `MiscEvents#addTomesToLecterns` (a Death Tome spawn egg on an empty lectern), `RisingZombie` (looking-at trigger, 130-tick rise, conversion to `minecraft:zombie`, armor 2) |
| `src/feature/lich/Projectiles.cpp` | `LichBolt` (reflection on any hit by an entity, `canHitEntity`, deflection by a phase 2-3 Lich), `LichBomb` (explosion power 2 without block damage, damage type `twilightforest:lich_bomb` for the owner, exploding when hit), `EntityEvents#lichBombsDontBlowUpItems`, `TomeBolt` (lost_words / schooled, slowness by difficulty, passes the owner's lectern) |
| `src/feature/lich/Tower.cpp` | `LichTowerStructure` and all 21 lichtowerrevamp pieces (foyer, foyer decor, base with its `TrimProcessor`, base trim, segments, boss room / boundary / roof, spawner bridges with the 180-degree flip, wing bridges, wing rooms with ladders / roofs / beards / decor and every room marker, room decor with TF's trees, magic gallery with its magic painting, and the yard: `LichYardBox`, `LichPerimeterFence`, `LichYardGrave`, `LichYardLights`), their piece type factories, the spawn labels (yard 0, interior 1, empty 2, Deny) and the terraformer |
| `src/feature/lich/Pending.*`, `Edits.cpp` | the block entity contents and entities the pieces add while a chunk generates (lectern book or Death Tome mimic, brewing stand, mason jar, chiseled shelf, sinister spawner, chest loot with facing, leashed zombies, magic painting): rolled at generation as TF rolls them, applied on the server thread once the chunk is loaded (`gf:lich:pending`) |
| `src/feature/lich/Module.cpp` | registration, the `twilightforest:is_minion` loot condition, the level tick (phase-1 boss bar progress, the queued edits, the leashes) |
| `src/client/lich/MapIcon.cpp` | client mod: the magic map icon of `twilightforest:lich_tower` (`TFMapDecorations.LICH_TOWER`) |
| `src/core/lich/TowerMath.*`, `Simplex.*` | pure parts (BlockPos#asLong, Mth tables, boss room candle ring, stair decay levels, JOML's simplex noise for the yard), gtested in `tests/lich` |
| `tools/structures/lich/**` | the original template designs (rule R19): `root/` (tower, bridges, mob bridges, placeholder rooms, yard graves and fences, one-block stamps) and `gallery/` |
| `tools/import/overrides/lich.json` | the client entity definitions (geometry, textures, animations, render controllers) and the `angry_lich` particle, patched into the imported files |
| `tools/sounds/recipes/lich.json` | the refined synthesized sounds (Death Tome rustles, scepter zap / drain, minion summon) |
| `assets/pack/bp/entities/lich/*` | the BP entities (properties, navigation, behaviours for the minion and the tome) |
| `assets/pack/rp/{animations,render_controllers,sounds.json.d}/lich*` | the Lich's arms per phase / hand / dying, the death shake and crown, the Death Tome's lectern pose and flipping pages, the rising zombie's rise and legs, the shadow clone render controllers, entity sounds |

## Bedrock adaptation

- Synced data are entity properties: `twilightforest:phase`, `twilightforest:shield`, `twilightforest:clone`,
  `twilightforest:death_time` (Lich), `gf:on_lectern` (Death Tome), `twilightforest:rising_ticks` (rising zombie). They
  drive the RP on every client, vanilla included. The Death Tome's page flip (`flip`, `oFlip`, `flipT`, `flipA`, client
  state in Java) runs in the client entity's Molang with the same rule.
- The per-actor state (shield, summons, clone links, a minion's master, a tome's lectern, the rising ticks) is saved
  in the actor's document (`gf::entity::data`); the maps are caches rebuilt from it after an unload.
- Shadow clones render with a TF-derived translucent texture (`textures/twilightforest/lich/tf-derived`, the
  SHADOW_CLONE colour 0.333 x RGB, alpha 0.5) plus the eyes texture as an emissive layer; their ambient sound is
  removed by a component group (`Lich#getAmbientSound` is null for clones).
- `Lich#hurt` runs as a `gf::combat::onIncoming` handler (priority High) so it sees the direct entity (shield break
  particles); the rest of `hurt` (hits without teleport) is an `onDealt` handler.
- The boss kit sets the bar's progress to the health fraction every tick; in phase 1 the unit sets it back to
  shield / 6 from a level tick listener (two updates a tick while the shield is up).
- A player's hit on a lich bolt or bomb comes through `interactions::onAttack`; the BP gives them a
  `minecraft:custom_hit_test` of 2.25 blocks (Java's pick radius 1.0). Any other damage with an entity source reaches
  them through the damage pipeline. The owner change of a reflected bolt is kept by the unit (F8's projectile
  extension has no `setOwner`).
- A Death Tome put on a lectern remembers the lectern (its seat) and is kept in the lectern's block; Bedrock's
  collision would push it out.
- Zombies tied to an iron post (the rooms' zombie traps, the perimeter fence) are persistent zombies whose leash
  holder is a leash knot on the post (the client draws the lead from the leash holder); Bedrock zombies have no
  leashable component, so the unit applies `Mob#tickLeash` itself (pulled back beyond 6 blocks, loose beyond 10: the
  knot goes and a lead drops). The tied zombies are kept by actor id in the world DB (`gf:lich:tethers`), so a zombie
  loaded later is held again without scanning for it; one whose chunk is loaded three checks in a row without it is
  dropped.
- `EntityEvents#lichBombsDontBlowUpItems` removes item entities from the explosion's affected list. Bedrock has no such
  list: for the bomb's `Level::explode` call every item in reach gets a health it cannot lose (`ItemActor#_hurt`
  removes an item at 0 whatever path the damage takes), its knockback is undone afterwards, and the damage pipeline
  cancels the bomb's damage to items; an item the engine removed all the same is put back as it was.
- `LichMinionModel#renderToBuffer`'s tint (red and blue x0.5, x0.25 while the minion has Strength) is the render
  controller's `color` on the `entity_alphatest_change_color` material; the server mirrors `hasEffect(DAMAGE_BOOST)`
  into the `twilightforest:strong` property.
- The yard pieces (`LichYardBox`, `LichYardLights`, `LichYardGrave`) read `WorldGenLevel#getHeight` from the blocks of
  the column (the heightmap rules of each type), not from the region's `height()`: F6's loaded-chunk adapter
  (`landmark::generateAt`, `/tf place`) answers `BlockSource#getAboveTopSolidBlock`, through which the yard turned
  no ground (`lich.tower_yard_loaded` places through an adapter that answers the same way). Where no terrain
  provider gives a height (outside the Twilight Forest, or before U2 registers one) the layout reads the loaded ground
  on the server thread and the start's height elsewhere, so heightmap-projected pieces (the perimeter fence) stand on
  the ground instead of at y 0.
- The magic map icon of the lich tower (`twilightforest:lich_tower`, `map/decorations/lich_tower`) is registered on the
  client with `gf::client::maps::setIcon` (`src/client/lich/MapIcon.cpp`).
- Room spawners get TF's spawn data (one mob, custom spawn rules block light 0..7 / sky light 0..15, the marker's
  spawn range) by placing a one-block stamp template with `SpawnerProcessor.compile`; that processor also sets the
  start delay to a quarter of the default (TF's `putSpawner` keeps the default 20 ticks).
- Jigsaw junction names use the `gf:` namespace (`gf:lich_tower/bridge`, `gf:lich_tower/room`,
  `gf:lich_tower/roof`, `gf:lich_tower/beard`, `gf:lich_tower/decor`, `gf:ladder_below/<n>`, `gf:shelf`,
  `gf:mob_bridge`, `gf:lich_tower/bridge_center`, `gf:lich_tower/tower_below` / `tower_above`, `gf:lich_tower/path`,
  `gf:lich_tower/grave`, `gf:lich_tower/fence_source`, `gf:lich_tower/fence_edge_left` / `_right`): assemble rejects
  `twilightforest:` strings it does not know, and TF's junction names are no registered ids. The templates TF's code
  names directly have `gf:lich_tower/...` ids (`tower_foyer`, `tower_base`, `central_trim`, `tower_slice`,
  `tower_boss_room`, `tower_boss_boundary`, `tower_boss_roof`, `foyer_decor`, `outer_fence_1..7`). **U22's room,
  roof, beard and decor templates must use these junction names.** Restoring TF's ids needs F0/F1 to know them
  (tools/import/known-ids.json, or assemble skipping jigsaw name / target keys).
- The magic painting of a gallery is U22's `twilightforest:magic_painting`; the unit spawns it with the actor data
  `variant` (TF's variant id) and `direction` for U22's entity to read.
- While U22's room templates are missing, a wing's room is a placeholder room of the same size
  (`gf:lich_tower/placeholder/<n>x<n>`). The golden layout and volume hashes (`lich.tower_layout`) hold for that
  case; with U22 present the test logs both hashes for U22 / A8 to commit.

## Not ported

- `twilightforest:clone_count`, `twilightforest:minion_count` (and U9's `twilightforest:max_shield_strength` that
  the Lich reads): Bedrock has no custom attributes and no `/attribute`; the values are actor data with TF's defaults
  (2, 9, 6) and cannot be changed in game, with or without the client mod (for `docs/spec/not-ported.md` section 4).
- The rising zombie's block-break pitch (0.7..0.85): Bedrock plays a block's break sound through
  `LevelSoundEvent::BreakBlock`, which has no pitch, and the block's sound type is not exposed to name the sound.

## Notes

- Spec difference: docs/spec/lich.md says room spawners are "40% zombie"; TF's `defaultRandomMob` is zombie 0-2 (30 %),
  swarm spider 3, hedge spider 4, cave spider 5, spider 6 (40 % spiders) and skeleton 7-9 (30 %). The unit follows TF.
- The lich minion burns in daylight: it is a `Zombie` in TF (sun sensitive), as Bedrock's `burns_in_daylight`.
- The mason jar's item rotation and the chiseled shelf's Death Tome spawner are their owners' block data (U6, U22):
  the unit fills the jar and the shelf through `gf::inventory` when those blocks register a container, and records the
  rolled rotation in the queued edit.

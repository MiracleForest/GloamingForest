# U22 `lichdecor`: Lich tower interiors and decor

What the unit implements, how it maps TF 4.8 onto Bedrock, and what is still missing. The behaviour spec is
[docs/spec/lichdecor.md](../spec/lichdecor.md).

## Code

| Path | What |
|---|---|
| `src/feature/lichdecor/Lichdecor.h`, `Common.cpp` | shared helpers (states with the `twilightforest:` prefix, directions, candles, sounds) and the config section `lichdecor` (`disableSkullCandleCreation`) |
| `src/feature/lichdecor/Lightable.cpp`, `LightablesInternal.h` | `LightableBlock` (firestarter, empty-hand extinguish, burning projectiles), TF's `CandleColors` numbering, the drop stash read by the loot modifiers |
| `src/feature/lichdecor/Candelabra.cpp` | `twilightforest:candelabra`: placement candidates and the torch-like support test, the three candle slots (BlockData `candles`), redstone -> DIM, scepter / exanimate essence -> OMINOUS, emitter and comparator output, the silk-touch drop with `twilightforest:candelabra_data` |
| `src/feature/lichdecor/SkullCandles.cpp` | the 12 skull candles: creation from a vanilla head with a vanilla candle (`disableSkullCandleCreation`, stat `twilightforest:skull_candles_made`), adding / removing candles, restoring the head, the six items' placement, the drops (`twilightforest:skull_candles`) |
| `src/feature/lichdecor/OminousCandles.cpp` | the 17 ominous candles: empty hand -> the unlit vanilla candle, the matching candle adds one |
| `src/feature/lichdecor/OminousFire.cpp` | `twilightforest:ominous_fire`: survival, entities inside (not undead, never ignited, items destroyed after 5 hits), the nether portal in the Overworld / Nether, the player-break sound, the conversion on death (zombified player, data map `twilightforest:ominous_fire`), `zombifiedPlayerAttacks` |
| `src/feature/lichdecor/Brazier.cpp` | `twilightforest:brazier`: two-block placement, halves kept together, flint and steel / fire charge cycle, water bucket |
| `src/feature/ext/lichdecor/SupportPower.*` | contract extension (R18): a directional producer in the circuit graph for the DIM candelabra's strong power into its support |
| `src/feature/lichdecor/SelfTestsLight.cpp` | selftests of the lightable blocks and the ominous fire (dx 2200..2249) |
| `assets/pack/{bp,rp}/**/lichdecor/**` | block geometry (skull candles with the vanilla head shapes, ominous candles, candelabra candles, brazier, skull chest), the `dim_flame` / `ominous_flame` particles (vanilla small flame with TF's sprites), the magic painting client entity (one geometry per variant, a plane per layer, variant property `twilightforest:variant`) |
| `src/feature/lichdecor/SinisterSpawner.cpp` | `/twilightforest sinister_spawner add_particle / remove_particle / set_loot / clear_loot` (`SinisterSpawnerCommand`), the creative trophy / boss spawner use (`SinisterSpawnerBlock#particlesFromItem`), the drops (the stored loot table, 15 + nextInt(15) + nextInt(15) xp) |
| `src/feature/lichdecor/Bookshelf.cpp` | `twilightforest:chiseled_canopy_bookshelf`: vanilla's chiseled bookshelf (slot by hit position, `#minecraft:bookshelf_books`, comparator = last slot + 1), a `gf::inventory` block (U21's tower fills it), the `BookshelfSpawner` of Death Tomes, the spawn egg conversion, the burning and breaking effects |
| `src/feature/lichdecor/SkullChest.cpp`, `assets/data/containers/lichdecor.json` | `twilightforest:skull_chest`: F10's declared 45-slot container (5 rows) plus the owner lock, the block-filled refusal, the named drop and the logged block, lava + water -> obsidian |
| `src/feature/lichdecor/WroughtIron.cpp` | `twilightforest:wrought_iron_fence`: the side / post shape on placement and neighbour changes, the item's finial toggle and tooltip, a lead on a post |
| `src/feature/lichdecor/MagicPainting.cpp` | `twilightforest:magic_painting`: placement from the item, the variants, the survival check, the drop; the gallery painting U21 spawns |
| `src/feature/lichdecor/Terrorcotta.cpp` | the placement states of terrorcotta curves (GlazedTerracotta) and lines (BinaryRotated); arcs are F11's pillar family |
| `src/feature/lichdecor/RoomTests.cpp`, `SelfTestsMisc.cpp` | selftests (`lichdecor.*`, dx 2250..2299), the room template goldens |
| `src/core/lichdecor/Arguments.*`, `tests/lichdecor` | pure rules: Java's particle / loot arguments over the 1.21.1 particle registry + TF's 29, a minimal SNBT check, the fence shape rules (gtested) |
| `src/feature/ext/lichdecor/LootTables.*` | contract extension (R18): `gf::loot::ext::tableExists` (F10's table store has no existence query) |
| `tools/structures/lichdecor/**`, `assets/structures/lich/rooms/**` | the 130 original room, roof, beard and decor templates of the lich tower pools |

## Bedrock adaptation

- Per-block data is BlockData under `lichdecor`: the shelf's books (`books`, `last`, `spawner` {delay, entity}), the
  skull chest's `owner` {name, uuid} (U9's keepsake casket writes it when it places a chest), the sinister spawner's
  `sinister_options` (the (id, options) entries of its particle set; F8 keeps the ids in its own `sinister_spawner`
  record and gets one parameter set per id, `{"snbt": "..."}` for the option types).
- `/twilightforest sinister_spawner`: `<particle>` and `<loot>` are one string each (quote Java's `{...}` options),
  `<pos>` a block position. The errors are Java's keys with the English texts as fallbacks. The command returns its
  1 / 0 as the command's success count and sends no feedback.
- The sinister spawner's drops on an explosion are rolled from the BlockData erase listener (the record is gone by
  then); a player's break rolls them after the break with the player and tool.
- The chiseled canopy bookshelf ticks from a position list (`gf:lichdecor:shelves`, every shelf placed or touched),
  not from a block entity. Catching fire is seen as the shelf turning into fire (`onRemoved` + the new block), so
  the igniter is unknown and the burning tomes target nobody.
- The magic painting is an entity standing at the bottom centre of its picture, 1/32 off the wall, facing away from
  it; the variant is the entity property `twilightforest:variant` (0..4: castaway_paradise, darkness, lucid_lands,
  music_in_the_mire, the_hostile_paradise) and the actor data `variant` (TF's id). U21's gallery spawns it with
  `variant` and `direction`; the first tick hangs it at that block.
- The wrought iron fence computes all four sides on every neighbour change (Java updates the changed side; with
  consistent neighbours the result is the same). A lead on a post moves the mobs leashed to the player to a
  `minecraft:leash_knot` on the post.

- `<loot>` of `set_loot` is a file-path token (Bedrock's string arguments stop at `/`, which every loot id has).
- The candelabra has no `waterlogged` state on Bedrock: water in the block's liquid layer counts as waterlogged
  for `canBeLit`.
- Bedrock heads carry no player profile: a player skull candle has no owner to keep.
- The selftests raise the difficulty to Easy while they spawn hostile mobs (the test world is peaceful).

## Not done yet

- Client-mod renderers (`src/client/lichdecor`): the animated flames and smoke (`animateTick`), the brazier's fire,
  the ominous candles' bobbing flames, the skull chest lid, the magic painting parallax / opacity layers, the candle
  colours of skull candles and candelabras (block data, not states: the pack shows the plain candle).
- The keepsake-casket style multilogging of arbitrary blocks in the skull chest; the bookshelf's real igniter.

## Not ported

- `set_loot` / `clear_loot` keep the stored key with `blockdata::set`, which always marks the chunk's BlockData dirty:
  a loot change always survives a save (TF does not flag it for saving: it reaches disk only when something else marks
  the chunk unsaved). Nothing is sent to clients (as in TF).
- Java's vanilla `minecraft:` loot tables: GloamingForest ships no Java vanilla loot tables (Mojang data; Bedrock's
  own tables have other ids and formats), so `set_loot minecraft:...` reports the table as unknown.
- The particle option codecs are checked for their required fields only (not their value ranges); the
  `Can't parse particle options` text lists the missing keys as DataFixerUpper does, without the full codec context.

## Audit round 1

Checked (A3, landmarks and bosses I): the candelabra's power into vanilla mechanisms and what a vanilla comparator
reads (`lichdecor.candelabra_circuit`), on top of the existing light / room tests.

Fixed:

- The DIM candelabra now lights vanilla mechanisms through its support (`CandelabraBlock#getDirectSignal`): the
  extension's producer (`src/feature/ext/lichdecor/SupportPower.cpp`) was created with the right connection (the
  support's powered-block component listed it as its source) but at strength 0: `CircuitSystem#setStrength` only finds
  components already in the scene graph, and a producer created that tick waits in the pending adds. The strength is
  now set on the component itself. `lichdecor.candelabra_circuit` lights a redstone lamp beside the support of a
  DIM standing candelabra and turns it off with the candelabra.
- Comparator side test: a vanilla comparator reads an OMINOUS candelabra right behind it (6 + 2 candles = 8) and
  nothing from one at its side.

Open: the client-mod renderers listed under "Not done yet".

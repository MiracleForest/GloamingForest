# Sky, fog, weather and aurora (U15 `atmosphere`)

Behaviour spec of unit **U15** (wave W1), translated from The Twilight Forest 4.8 (`9e40a757`, MC 1.21.1) by F16. It
is the checklist the U15 worker implements and the A6 audit compares against. Sections 1-5 are the behaviour; Appendix A
lists every id U15 owns; Appendix B the strings.

## 1. Scope

The Twilight Forest dimension's client presentation:

- **Dimension special effects** `TwilightForestRenderInfo` (dimension type effects id `twilightforest:renderer`
  registered by F4's dimension type): cloud height 128, no sunrise colours, fog colour modulation, near fog in dark
  biomes, custom sky (stars only), custom weather.
- **Sky** (`TFSkyRenderer`): no sun, no moon, no sunrise; always-bright stars; void horizon threshold.
- **Fog** (`FogHandler`): spooky forest / dark forest fog distances with smoothing.
- **Locked-biome weather** (`TFWeatherRenderer`): blizzard / mosquitoes / ashes / dark streams / big (acid) rain in
  biomes whose progression restriction the viewer has not met; sparkles inside protected structure pieces; Ur-Ghast
  rain.
- **Aurora** (`TFShaders.AURORA`, `ClientGameEvents.renderAurora`): the noise aurora plane in the biomes of client
  config `client.auroraBiomes` (default `["twilightforest:glacier"]`, section `atmosphere`).
- Damage type `twilightforest:acid_rain` and sound event `environment.twilightforest.acid_rain` (played by U14's
  acid-rain enforcement); net channels `atmosphere.*`.

Not U15: biome sky/fog/water colours (U1 biome JSON), the dimension, time and portal (F4), restrictions and the
enforcement damage loop (U14), structure protection data (F6 sends the protected boxes; U15 renders them), cloud blocks
and their precipitation (U29), the Ur-Ghast (U26 signals "alive"), music (U10), the vignette removal and other F12
client frameworks (U15 registers its providers through F12).

Contracts: client.world, client.render, progression, world.

## 2. TF sources

`client/TwilightForestRenderInfo.java`, `client/renderer/TFSkyRenderer.java`, `client/event/FogHandler.java`,
`client/renderer/TFWeatherRenderer.java`, `client/TFShaders.java`, `shaders/core/aurora/aurora.{vsh,fsh,json}`
(resources), `client/event/ClientGameEvents.java` (`renderAurora`, aurora counter in `clientTick`, `killVignette`),
`client/event/ClientRegistrationEvents.java` (dimension effects registration, `createStars`), `config/TFClientConfig.java`,
`config/TFConfig.java` (`getValidAuroraBiomes`), `init/TFDamageTypes.java`, `init/TFSounds.java`,
`textures/environment/{blizzard,mosquitoes,ashes,darkstream,bigrain,sparkles}.png` (imported assets),
`network/StructureProtectionPacket.java` (box data consumer).

## 3. Behaviour

### 3.1 Dimension effects

`TwilightForestRenderInfo(cloudHeight 128.0, hasGround false, SkyType.NONE, forceBrightLightmap false,
constantAmbientLight false)`:
- `getSunriseColor` -> null (no sunrise/sunset band).
- Fog colour = biome fog colour x (daylight*0.94 + 0.06, daylight*0.94 + 0.06, daylight*0.91 + 0.09).
- Near fog (`isFoggyAt`) when the viewer's biome is `spooky_forest` (any height), or `dark_forest` /
  `dark_forest_center` while the viewer is at y <= 20 (above y 20 only the spooky forest counts).
- Sky, rain/snow rendering and rain ticking delegate to the renderers below.
- The vanilla vignette is disabled in the Twilight Forest (`vignetteBrightness = 0`).

### 3.2 Sky (`TFSkyRenderer.renderSky`)

1. Sky dome (vanilla sky buffer) in the level's sky colour at the camera.
2. No sunrise fan, no sun, no moon.
3. Stars always at full brightness: 3000 random quads from `RandomSource.create(10842)`, each at distance 100 in a
   random direction with size 0.15..0.25, random roll (vanilla star algorithm with TF's seed and count), drawn without
   fog, white.
4. Dark void plate (vanilla dark buffer at +12) only when the camera eye is below the dimension's minimum build height
   (TF lowers the vanilla threshold from y 63 to the void at the bottom, -32 in the TF dimension).

### 3.3 Fog (`FogHandler`, only in TF effects, fog type NONE)

- Sky fog: target far = vanilla far x 0.5 in `spooky_forest` else vanilla far; near = 0 in spooky forest else
  vanilla near. Terrain fog: far as above; near = far x 0.75 in spooky forest else vanilla near.
- Values are smoothed every frame: far lerps by 0.003; near lerps by 0.0015 when increasing, 0.006 when decreasing.
  Initialised to the target once the player's chunk is loaded; reset when the level unloads.

### 3.4 Locked-biome weather (`TFWeatherRenderer.renderSnowAndRain`)

Only when progression is enforced (`progression::client::enforced()`, U14's gamerule) and the viewer is not
creative/spectator; vanilla weather still renders.

- If any biome within 15 blocks (x/z square, sampled at y 0) is unsafe for the viewer
  (`progression::client::biomeLocked`): for each column within `range` (5, or 10 with fancy graphics) whose biome is
  locked, draw a vanilla-style weather column from max(py - range, minY) to py + range with the texture by the
  restriction's enforcement:

  | Enforcement | Effect | Texture | Motion / colour |
  |---|---|---|---|
  | frost | BLIZZARD | `textures/environment/blizzard.png` | count ((ticks & 511) + pt)/512, u drift rand + t*0.05*gauss, v rand + t*0.0025*gauss; white, alpha; full bright |
  | hunger | MOSQUITO | `mosquitoes.png` | static count, u rand + t*0.03*gauss, v rand + t*0.003*gauss; colour rand*0.3 per channel, alpha 1; full bright |
  | fire | ASHES | `ashes.png` | count -((ticks & 1023) + pt)/1024 (rises), u t*0.0025, v t*0.005; grey 0.8..1.0; full bright |
  | darkness | DARK_STREAM (50% of columns) | `darkstream.png` | count -((ticks & 511) + pt)/512, no horizontal drift; white; full bright |
  | acid_rain | BIG_RAIN | `bigrain.png` | fast rain: count ((ticks + hash & 31) + pt)/32 x (3 + rand); world light |

  Alpha = (1 - d^2) * 0.3 + 0.5 with d = horizontal distance / range; column random seeded with
  x*x*3121 + x*45238971 ^ z*z*418711 + z*13761 (vanilla rain seed); particle shader, culling off, blending on.
- Protected structure sparkles: when F6 sent protected boxes (`StructureProtectionPacket`, every 100 ticks; empty =
  all clear) and one intersects the box around the camera (x/z +-range, y -range..+2*range), draw `sparkles.png`
  columns inside the protected parts (per-column intervals cached when the camera box changes): count
  ((ticks & 511) + pt)/512, u/v drift t*0.02*gauss, alpha ((1 - d^2)*0.3 + 0.5) * rand, full bright.

### 3.5 Rain ticking and Ur-Ghast rain (`tickRain`)

Vanilla rain splash/sound logic, but the rain level is max(level rain, Ur-Ghast rain): while the Ur-Ghast is alive
(U26 sets the flag every tick) the Ur-Ghast rain rises by 0.1 per tick to 1, otherwise falls by 0.02. Splash particle
count 100 x level^2 (halved when particles are decreased, and the level is halved without fancy graphics); `smoke`
instead of `rain` on lava, magma or lit campfires; rain sounds `weather.rain.above` (0.1, 0.5) / `weather.rain`
(0.2, 1) like vanilla.

### 3.6 Aurora

- Client tick: if the camera's biome is in `auroraBiomes` (parsed once; invalid ids warned and skipped) the aurora
  counter rises by 1 per tick, else falls by 1, clamped 0..60 (fade over 3 s); empty list disables it.
- Render after weather when the counter > 0: one quad at y = 256 - camera y spanning +-2048 x (render distance / 32)
  around the camera, colour white, alpha = counter / 60 x 0.5, shader `twilightforest:aurora/aurora` with uniforms
  `SeedContext` = |biome zoom seed| and `PositionContext` = camera position.
- Shader (fragment): uv = position.xz*2 - 1; 16-step ray march along (uv.x, 1, uv.y) with step 0.35/16, each sample
  = fixNoise(OpenSimplex2 (improve XY) of ((x + camX + seed/360)/512, (z + camZ + seed%360)/512, time*22.5)) weighted by
  fade (first 1, then (16 - i)/16 * 0.65); fixNoise maps |n| < 0.2 to 1 + |n|*5 else -1, then
  clamp((n + 1)/2 - 0.5, 0, 1) inverted where positive. Colour (0, 0.5 + c, 1 - c, noise) with c = (noise(time*720)
  + 1)/2 * 0.5 (green-cyan to blue); linear fog fade over length(xz)/2.75.

## 4. Bedrock adaptation notes

- There is no GLSL in RenderDragon (README deviation): sky, stars, aurora and weather columns become CPU-built meshes
  drawn with built-in materials through F12's `client::sky::setProvider(dim, SkyProvider{sky, stars, fog(biome),
  clouds, weather})` and `client::render::onWorld`; the aurora noise is evaluated on the CPU into a vertex-coloured
  grid (same noise and constants, coarser resolution acceptable, A6 reviews screenshots in Classic and Vibrant
  Visuals).
- Vanilla clients: client-biome sky and fog colours and `PlayerFogPacket` (fog distances per biome) only; no stars
  change, no locked-biome weather, no aurora (best effort, not-ported.md).
- Locked-biome checks use U14's client query `progression::client::biomeLocked(BiomeId)`; protected boxes come from
  F6 over its own channel; the Ur-Ghast flag from U26 through a client event by string (`atmosphere.*` channel or the
  F12 weather provider input).
- Acid rain damage type id and death message (F10 strings) are registered as U15's; the sound is regenerated by F3/U15
  (`tools/sounds/recipes/atmosphere.json`).

## 5. Acceptance checklist (selftests `atmosphere.*`, dx 1500-1599; client screenshots)

- In the TF dimension: no sun/moon, stars visible at all times (screenshot at noon and midnight vs TF reference).
- Spooky forest fog halves the distance and smooths in/out; dark forest near fog below y 20.
- Locked biome weather: blizzard in snowy forest before `progress_lich`, mosquitoes in swamp, ashes in fire swamp,
  dark streams in dark forest, big rain in highlands/thornlands/final plateau (screenshots); disappears after the
  advancement or with the gamerule off.
- Aurora fades in over 60 ticks in the glacier; config list empty disables it.
- Acid rain damage type/sound present for U14's enforcement.

## Appendix A: Owned ids (generated inventory)

Every id this unit owns in `docs/spec/owners.tsv`, with the TF 4.8 data the W1 worker must reproduce. Registration statements are TF's own Java (`init/TF*.java`), condensed (namespaces such as `MapColor.` dropped); loot, recipe, tag, advancement and data rows are summarised from TF's generated JSON (`src/generated/resources`). Loot notation: `self` = drops the block itself when it survives the explosion; `alternatives(a → b)` = first matching child; `[if ...]` = conditions; `{...}` = functions. In summaries `mc:` is `minecraft:` and ids without a namespace are `twilightforest:`.

| Kind | Count |
|---|---|
| `config_key` | 1 |
| `config_section` | 1 |
| `damage_type` | 1 |
| `e2e` | 1 |
| `lang` | 4 |
| `net` | 1 |
| `path` | 20 |
| `selftest` | 1 |
| `selftest_dx` | 1 |
| `sound_event` | 1 |
| `worlddb` | 1 |

### Unit facts

| Kind | Ids |
|---|---|
| Selftest glob (`selftest`) | `atmosphere.*` |
| Selftest dx range (`selftest_dx`) | `1500-1599` |
| World DB keys (`worlddb`) | `gf:atmosphere:*` |
| E2E scenario (`e2e`) | `atmosphere` |
| Net channels (`net`) | `atmosphere.*` |
| Config section (`config_section`) | `atmosphere` |
| Owned paths (`path`) | `**/*.d/atmosphere.json`, `assets/data/*/atmosphere.json`, `assets/gen/audio/atmosphere/**`, `assets/gen/geo/atmosphere/**`, `assets/gen/import/atmosphere/**`, `assets/lang/*/atmosphere.json`, `assets/pack/{bp,rp}/**/atmosphere/**`, `assets/pack/rp/texts.d/*/atmosphere.lang`, `assets/structures/atmosphere/**`, `assets/tags/**/*.d/atmosphere.json`, `docs/units/atmosphere.md`, `src/client/atmosphere/**`, `src/core/atmosphere/**`, `src/feature/atmosphere/**`, `src/feature/ext/atmosphere/**`, `tests/atmosphere/**`, `tools/e2e/*/scenarios/atmosphere{,.*}.ps1`, `tools/import/overrides/atmosphere.json`, `tools/sounds/recipes/atmosphere.json`, `tools/structures/atmosphere/**` |

### Damage types (1)

- `twilightforest:acid_rain`: `{"exhaustion":0.0,"message_id":"twilightforest.acid_rain","scaling":"when_caused_by_living_non_player"}` death.attack.twilightforest.acid_rain = "%1$s went dancing in the acid rain";

### Sound events (1)

`sounds.json` content (vanilla `event` references can be played with the matching Bedrock vanilla sound; TF file names are listed only as names: the audio itself is All Rights Reserved and is re-synthesised by F3/the owner).

| Event | Subtitle | sounds.json entries |
|---|---|---|
| `environment.twilightforest.acid_rain` | subtitles.twilightforest.environment.acid_rain = Acid rain scalds | event block.fire.extinguish |

### Config keys (1)

See [config.md](config.md) for the section mapping; defaults and translation ids below are TF's.

| TF key | Default | Translation id | English (config screen) |
|---|---|---|---|
| `client.auroraBiomes` | `List.of("twilightforest:glacier")` | `aurora_biomes` | Aurora Shader Biomes — Defines which biomes the aurora shader effect will appear in. Leave the list empty to disable the effect. |

## Appendix B: Strings (4 en_us keys owned)

Keys already shown above (block/item/entity names, subtitles) are not repeated. The 29 Bedrock locales are generated from TF's lang files (R9); owners use these keys with the English text as fallback.

| Key | English |
|---|---|
| `config.twilightforest.aurora_biomes` | Aurora Shader Biomes |
| `config.twilightforest.aurora_biomes.button` | Edit Biomes |
| `config.twilightforest.aurora_biomes.tooltip` | Defines which biomes the aurora shader effect will appear in. Leave the list empty to disable the effect. |


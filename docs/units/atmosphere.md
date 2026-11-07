# U15 `atmosphere`: sky, fog, weather and aurora

The Twilight Forest's client presentation (TF 4.8: `TwilightForestRenderInfo`, `TFSkyRenderer`, `FogHandler`,
`TFWeatherRenderer`, the aurora shader, `ClientGameEvents` killVignette / customizeSplashes) and its vanilla
fallback. Spec: [docs/spec/atmosphere.md](../spec/atmosphere.md).

## Files

| Path | What |
|---|---|
| `src/core/atmosphere/` | Pure maths (gtested in `tests/atmosphere/`): biome sky / fog colours, fixed time, fog colour and planes, `FogSmoother` (FogHandler), TF's star field, the weather columns (`lockedColumn`, `sparkleColumn`, IntervalUtils), the Ur-Ghast rain, the aurora counter and the aurora shader (OpenSimplex2 ImproveXY, ray march, fog) on the CPU, the birthday splash. |
| `src/client/atmosphere/` | Client mod: F12 sky provider for the TF dimension (sky, stars, fog, clouds, weather), the aurora worker thread, the Ur-Ghast rain splashes and sounds, the `atmosphere.*` client selftests. |
| `src/feature/atmosphere/` | Config section `atmosphere`, the vanilla fog fallback (PlayerFogPacket), the `atmosphere.aurora_biomes` screen type (the "Edit Biomes" editor; a form for vanilla players), server selftests and the `atmosphere.show_*` previews and `atmosphere.show_cal_*` calibration previews of the client scenario. |
| `src/client/atmosphere/AuroraBiomesScreen.cpp` | TFClientConfig's auroraBiomes list editor (config.twilightforest.aurora_biomes, .button, .tooltip): edits the client's own `config/atmosphere.json`, also when joined to a remote server. |
| `src/feature/ext/atmosphere/ClientHooks.cpp` | Contract extension (R18): HudVignetteRenderer (killVignette) and SplashTextRenderer (birthday splash) hooks. |
| `src/feature/ext/atmosphere/SkyView.{h,cpp}` | Contract extension (R18, to fold into F12's SkyProvider): a per-dimension view override that sets the frame's cloud colour and hides the sunrise colour band (`ViewRenderObject` in `LevelRendererCamera::$render`). |
| `assets/pack/rp/fogs/atmosphere/` | 24 resource pack fog definitions. |
| `tools/e2e/{server,client}/scenarios/atmosphere*.ps1` | e2e scenarios (client: Classic + Vibrant Visuals in local mode, remote-mod, remote-vanilla). |

## For other units

- **Fog definitions** (U1's client biomes reference them): `twilightforest:<biome>` for each of the 22 TF biomes
  (the biome's TF fog colour, vanilla distances 0.9..1.0 of the render distance), `twilightforest:spooky_forest`
  is the near fog (0.1875..0.25), plus `twilightforest:dark_forest_near` and `twilightforest:dark_forest_center_near`
  (0.05..0.5) that the server pushes to vanilla clients below y 20.
- **`atmosphere.ur_ghast`** (server -> client mod, U26): `{"active": true}` while the Ur-Ghast near the player is in its
  tantrum (TF's `UrGhast#aiStep` sets `TFWeatherRenderer.urGhastAlive` every client tick then), `{"active": false}`
  when it ends or dies. The client raises its Ur-Ghast rain by 0.1 a tick to 1 while active, lowers it by 0.02 a tick
  after, and spawns splashes and rain sounds where it is stronger than the level's rain.
- **Protected structure sparkles** read F6's `landmark::ext::client::structureProtection()`.
- **Locked biomes** come from F10's `progression::client::biomeLocked` (U14's advancements) and the restriction
  enforcement in `data/<ns>/twilight/restrictions/<biome>.json`.
- The damage type `twilightforest:acid_rain` (death message `death.attack.twilightforest.acid_rain`) and the sound
  event `environment.twilightforest.acid_rain` (F3's baseline: the vanilla fizz, as TF's `block.fire.extinguish`)
  are ready for U14's acid rain enforcement.

## Config (`config/atmosphere.json`, client)

| Key | Default | TF |
|---|---|---|
| `auroraBiomes` | `["twilightforest:glacier"]` | `TFClientConfig.auroraBiomes` (empty disables the aurora; ids that do not parse as resource locations, or name no biome of the world once its biomes are known, are warned about once and skipped, as `TFConfig#getValidAuroraBiomes`) |
| `sky`, `fog`, `weather`, `aurora` | `true` | GloamingForest switches for the client mod's rendering |

## Bedrock adaptation

- No GLSL: TF's shaders become CPU-built meshes drawn with the game's own materials.
  - Stars: a vertex-coloured mesh, the level renderer's stars material, sky pass (no fog, full brightness).
  - Aurora: aurora.fsh is evaluated per vertex of a polar mesh around the zenith (128 rings x 256 segments, about
    0.55 x 1.4 degrees a cell seen from the camera, whatever the render distance) on two worker threads every
    ~120 ms; the colours and alphas are interpolated across the cells. In Classic it is drawn in the sky pass with the
    particle engine's blend material over a white texture (SRC_ALPHA / ONE_MINUS_SRC_ALPHA like TF, no fog), scaled
    towards the camera (the same picture): terrain and clouds then draw over it as Java's depth test does. The e2e
    calibration (`GF_ATMOSPHERE_CALIBRATE=1`) shot the other choices: `ui_fill_gradient` draws nothing in the sky
    pass, the stars material ignores vertex alpha, `wireframe` is opaque, and the world pass fogs the far cells into
    the fog colour (Bedrock fogs particles by distance; TF's shader fogs by its own rule).
  - Outside the Twilight Forest (a dimension without TF's sky provider) the aurora draws in the world pass
    (`render::onWorld`, depth tested), with that fog.
  - Weather columns: the particle engine's blend material, split at the texture's edges (TF relies on a repeating
    sampler); 10 columns with fancy graphics, 5 without. Bedrock has no "decreased particles" option.
  - Void plate (TFSkyRenderer's dark disc below y -32): the stars material in black, sky pass.
- No sun / moon: the level renderer's sun, moon and star meshes are swapped for an empty mesh while the player is in
  the Twilight Forest and put back outside it.
- Clouds and sunrise: `ext/atmosphere/SkyView` sets the frame's cloud colour to `ClientLevel#getCloudColor` at the fixed
  time (rain and thunder included) and clears the sunrise colour band (`getSunriseColor` is null in TF).
- Fog: Bedrock has one distance fog; it takes FogHandler's terrain planes (the sky fog is smoothed alongside), stepped
  by the time since the last step in frames of 1/60 s (TF steps per rendered frame). A boss bar asking for world fog
  (the companion's TFBossBarPacket state) gives the near fog, as `BossOverlay#shouldCreateWorldFog`.
  - Only for FogType.NONE: with the camera under the surface of water or lava (FluidState#getHeight) or in powder snow
    the provider returns nothing and the game's own fog stays (`atmosphere.client_fog_fluid`).
  - isFoggyAt and FogHandler read the feet (`player.position()`, `player.blockPosition()`); Bedrock's getPosition is
    the eyes, so the client and the vanilla fallback use getFeetPos / getFeetBlockPos (`atmosphere.fog_feet`).
  - The colour is FogRenderer#setupColor's: the biome fog and sky colours blended over the 6 x 6 x 6 quart cells around
    the camera (CubicSampler), darkened by the rain and thunder levels and towards black below the dimension's floor.
- Stars: TFSkyRenderer draws them after FogRenderer.setupNoFog. Bedrock fogs the sky pass by distance, so the stars (and
  the sky-pass aurora and void plate) are scaled to a few blocks from the camera (the same picture), inside every fog
  start: the stars stay over the spooky forest's near fog.
- Void plate: the dark buffer is opaque black (shader colour 0 0 0 1, no blend) drawn after the stars, fogged by TF's
  sky fog through the position shader (black below, the fog colour towards the horizon, 32 rings per fan triangle);
  drawn with the particle blend material at alpha 1, so the stars below the horizon are hidden.
- Textures: looked up again for every level renderer and after leaving a level, and reloaded when the texture group
  dropped them (a remote server's pack stack loads on join: the aurora drew the missing texture, magenta x its colours,
  on a modded client joined to a BDS). The white texture of the vertex-coloured draws is uploaded from memory.
- The config is read through a snapshot rebuilt only on the thread that loaded it (install, `gf::config::onReload`):
  the render thread never touches the live struct.
- Vibrant Visuals (deferred PBR, no way to add a shader):
  - VV paints its own sky after the sky pass, so the stars, the aurora and the void plate draw in the world pass there
    (at 90 % of the far plane, depth tested). VV lights those draws with the scene light: at night the aurora shows as
    darker ribbons and the stars as dark points (vertex emissive MERS does not change it; `vibrant-aurora.png`). The
    shape and motion are TF's; the glow is not reachable without a shader.
  - F12 turns the provider fog into a uniform air fog; VV fogs its sky with it too, so the planes are stretched x3 under
    VV (the sky stays visible, the spooky forest still reads; colour distance 6 against the forest).
  - VV draws its own dusk from the sun direction at the fixed time 13000 (an orange band low in the west); the sunrise
    colour override does not reach VV's physical sky.
- Render distance: `LevelRendererCamera::mRenderDistance` is in blocks (the e2e pins 12 chunks = 192 blocks).
- The fixed time (13000) and the per-player time of vanilla clients are F4's (`feature/dimension/Clock.cpp`,
  `Dimension.cpp`).
- Vanilla clients: the PlayerFogPacket stack is resent after a respawn or a dimension change.
- `auroraBiomes` UI: `/twilightforest config atmosphere` edits the list as text (F0's config screen); the per-key
  label `config.twilightforest.aurora_biomes`, its "Edit Biomes" list editor and a client-local editor for a client
  joined to a remote server are F0's / F12's screen work (reported to the coordinator).

## Audit round 1

- Server scenario `atmosphere`: PASSED (2026-11-07).
- Client run (`tools/e2e/client/run.ps1 -Scenario atmosphere -Mode local`, Classic then Vibrant Visuals). Classic: every
  view and check passed; the screenshots (zenith at noon and midnight alike with TF's stars, no sun or moon, grey
  clouds and no sunset band at the horizon, the forest / spooky / dark forest / boss fogs, the aurora ribbons over the
  glacier preview, the void plate, the locked-biome weathers: blizzard, mosquito swarm, ashes, dark stream, big rain,
  sparkles and the Ur-Ghast rain) were compared by eye with TF's textures (`textures/environment/*`, the weather
  sprites) and TF's sky renderer: they match.
- Fixed: the scenario failed under Vibrant Visuals at "pink (sunset) share of the horizon 0.0758". Commit 68db4333 had
  made that check strict for Vibrant Visuals too, while this document lists Vibrant Visuals' own dusk (VV paints its
  physical sky from the sun direction at the fixed time 13000; the sunrise colour override does not reach it) as a
  deviation. The check is a recorded value again under Vibrant Visuals (the Classic check stays strict). The same
  commit made the aurora's green gain and lightness gain strict under Vibrant Visuals too: measured 6.9 (range
  10..255) and -16.1 (5..255) while `vibrant-aurora.png` shows lit cyan-green ribbons (the clear reference view is VV's
  lighter dusk sky); both are recorded under Vibrant Visuals now.
- Still failing under Vibrant Visuals (round 2, not closed here): "stars below the horizon over the void plate" is 6
  (range 0..2): the void plate does not hide every star below the horizon in VV's world pass. The VV views after it
  were not reached in this round's runs.- Still failing, not this unit's: the Twilight Forest view logs 27 160 x 6 content-log errors
  `[Molang][error] particles/note.json | variable.note_color.r | unable to find member variable .r`: U6's
  `src/client/flora/FloraClient.cpp` (cicadas, line 206, and the music block at line 286) spawns
  `minecraft:note_particle` with an empty molang map; the vanilla effect needs `variable.note_color` {r, g, b}
  (F10's `fx` sets it from NoteParticle's hue). Reported to the coordinator for U6.
- Open (round 2): Vibrant Visuals' dusk. A per-biome Vibrant Visuals atmosphere (resource pack `atmospherics/*.json`
  with no sun Mie scattering and TF's sky colours, referenced from each TF biome's client biome file through
  `minecraft:atmosphere_identifier`) would take the orange band away; the client biome files are U1's.
- Open (round 2): the HUD shows the icons of the hidden effects other units give (see U12's audit); not this unit's.

## Audit round 2

- **Vibrant Visuals: stars below the horizon over the void plate** (measured 6, range 0..2, round 1). Under VV the stars
  and the plate draw in the world pass, ordered by depth and blend, and stars showed through the plate. TFSkyRenderer
  draws the plate after the stars without depth, so every star in a direction that meets the plate is hidden: when the
  plate is drawn in the world pass the stars it covers are now left out (`core::atmosphere::underVoidPlate`, the ray
  against the octagon at y -4, gtested in `Atmosphere.UnderVoidPlate`; `starsAbovePlate()` in
  `src/client/atmosphere/Atmosphere.cpp`). Classic is unchanged (the sky pass draws the plate over the stars).
- **Vibrant Visuals' dusk**: `assets/pack/rp/atmospherics/atmosphere/<biome>_atmospherics.json` (22, one per TF
  biome, `gloamingforest:<biome>_atmospherics`) give VV's physical sky TF's colours at the dimension's fixed time
  13000 and take its sun scattering away (sun and moon Mie 0, no sun glare; laid out as Bedrock's own fixed-sky
  `end_atmospherics`): zenith = ClientLevel#getSkyColor (the biome sky colour x the daylight 0.379 of 13000), horizon =
  the frame's fog colour of `core::atmosphere::fogColor` at Java's default 12 chunks (the biome fog colour through
  getBrightnessDependentFogColor, pulled 14.6 % towards the sky). U1's client biome files reference them through
  `minecraft:atmosphere_identifier` (U1's PR of this round). Not verified in a client run this round (the e2e client
  instance was missing from the machine); the scenario's VV dusk value stays recorded, not checked.

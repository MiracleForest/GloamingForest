# U15 `atmosphere`: sky, fog, weather and aurora

The Twilight Forest's client presentation (TF 4.8: `TwilightForestRenderInfo`, `TFSkyRenderer`, `FogHandler`,
`TFWeatherRenderer`, the aurora shader, `ClientGameEvents` killVignette / customizeSplashes) and its vanilla
fallback. Spec: [docs/spec/atmosphere.md](../spec/atmosphere.md).

## Files

| Path | What |
|---|---|
| `src/core/atmosphere/` | Pure maths (gtested in `tests/atmosphere/`): biome sky / fog colours, fixed time, fog colour and planes, `FogSmoother` (FogHandler), TF's star field, the weather columns (`lockedColumn`, `sparkleColumn`, IntervalUtils), the Ur-Ghast rain, the aurora counter and the aurora shader (OpenSimplex2 ImproveXY, ray march, fog) on the CPU, the birthday splash. |
| `src/client/atmosphere/` | Client mod: F12 sky provider for the TF dimension (sky, stars, fog, clouds, weather), the aurora worker thread, the Ur-Ghast rain splashes and sounds, the `atmosphere.*` client selftests. |
| `src/feature/atmosphere/` | Config section `atmosphere`, the vanilla fog fallback (PlayerFogPacket), server selftests and the `atmosphere.show_*` previews of the client scenario. |
| `src/feature/ext/atmosphere/ClientHooks.cpp` | Contract extension (R18): HudVignetteRenderer (killVignette) and SplashTextRenderer (birthday splash) hooks. |
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
| `auroraBiomes` | `["twilightforest:glacier"]` | `TFClientConfig.auroraBiomes` (empty disables the aurora) |
| `sky`, `fog`, `weather`, `aurora` | `true` | GloamingForest switches for the client mod's rendering |

## Bedrock adaptation

- No GLSL: the stars and the aurora are vertex-coloured meshes drawn with the level renderer's stars material in the
  sky pass (additive like TF's stars; TF draws the aurora with alpha blending, over the dark sky the two look alike);
  the aurora grid (64 x 64 cells over the faded radius 2.75 x fog end) is evaluated on a worker thread and drawn
  scaled towards the camera (the same picture inside the sky's depth range). The weather columns use the particle
  engine's blend material, split at the texture's edges (TF relies on a repeating sampler).
- No sun / moon: the level renderer's sun, moon and star meshes are swapped for an empty mesh while the player is in
  the Twilight Forest and put back outside it; TF's star field is drawn by the provider at full brightness.
- Fog: Bedrock has one distance fog; it takes FogHandler's terrain planes (the sky fog is smoothed alongside), stepped
  by the time since the last step in frames of 1/60 s (TF steps per rendered frame).
- The fixed time (13000) and the per-player time of vanilla clients are F4's (`feature/dimension/Clock.cpp`,
  `Dimension.cpp`).

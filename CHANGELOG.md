# Changelog

All notable changes to GloamingForest are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

Status: **Preview** — every unit of The Twilight Forest 4.8 is ported and passes the server selftests;
client screenshot review pending; Bedrock gaps in `docs/spec/not-ported.md`.

### Added

- The Twilight Forest dimension: portal, time, teleport, the 22 biomes, terrain, carvers, trees, flora and the
  landmark grid with conquest, protection and hints.
- Every landmark and boss: Naga courtyard, Lich tower, Labyrinth and Minoshroom, Hydra lair, Knight stronghold,
  Dark tower and Ur-Ghast, troll and yeti caves, Aurora palace and Snow Queen, beanstalk and cloud castle, final
  castle; minor landmarks, quest grove and camps.
- All mobs (passive, ambient, hostile, minions) with their AI, multipart bodies, projectiles, loot and sounds.
- All blocks, items, armour and tool tiers, weapons, scepters, charms, artifacts, provisions, enchantments,
  effects, music discs, the uncrafting table, magic and maze maps, Travellers' Gear and its movement, the 45
  advancements, sky, fog, weather and aurora.
- Original synthesized sounds for every TF sound event and original structure templates; TF's CC BY-NC-SA
  textures, models and translations imported for 29 locales.
- Client-mod screens and renderers with vanilla-client fallbacks (forms, DDUI, lore).
- About 960 in-game selftests (`/twilightforest selftest`) and the C++ unit tests.
- The build: xmake targets `gf-core`, `GloamingForest` (server and client builds) and `gf-tests`;
  `tools/build.ps1` with machine-wide throttle slots; the shared Twilight Forest checkout (`tools/tf/fetch.ps1`).
- The foundation adapted from Levitate v0.1.0: modules, configs and the in-game config screen,
  `/twilightforest` (aliases `/tf`, `/tffeature`), translations for 29 locales, world data, the companion
  protocol, interactions with tag matchers, block data, redstone, click and hold, the pack deployer and the
  selftest framework.
- The W0 contract headers with neutral stubs, the `gf_test:*` fixture packs, the asset assembler with its
  checks, the ownership manifest (`docs/spec/owners.tsv`) and the CI guards.
- The server and client end-to-end harnesses with the `smoke` scenarios.
- The lip package `github.com/MiracleForest/GloamingForest`, described by `lip.toml` of format 4 (lip
  **0.40.0** or later, or LeviLauncher). GloamingForest is compiled against one LeviLamina line at a time,
  so each supported line is a variant named after it, with a server and a client build: `26_51` for
  LeviLamina 26.51.x (Minecraft 1.26.51). The default variant and `client` are the newest supported line.
  Settings in the mod's `config/` folder are kept across updates.
- One download per side and LeviLamina line: `gloamingforest-server-win-x64-ll26.51.zip` and
  `gloamingforest-client-win-x64-ll26.51.zip`, with the mod's files at the root of the ZIP.

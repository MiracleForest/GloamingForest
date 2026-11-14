# Changelog

All notable changes to GloamingForest are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-11-14

First public release, for Minecraft 1.26.51 with LeviLamina 26.51.x, on dedicated servers (BDS) and on clients
through LeviLauncher. Status: preview; what Bedrock cannot do (or does differently) is listed in
`docs/spec/not-ported.md`.

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
- Original sounds and structure templates; textures, models and translations for 29 locales (CC BY-NC-SA 4.0,
  see `assets/LICENSE`).
- Client-mod screens and renderers, with best-effort fallbacks (forms, DDUI, lore) for vanilla Bedrock clients
  joining a server.
- The `/twilightforest` command (aliases `/tf`, `/tffeature`) and an in-game config screen for operators
  (`/twilightforest config`); settings are kept across updates.
- About 960 in-game selftests (`/twilightforest selftest`) and C++ unit tests.
- The lip package `github.com/MiracleForest/GloamingForest` (lip 0.40.0 or later, or LeviLauncher), with the
  variant `26_51` for LeviLamina 26.51.x and the default variant tracking the newest supported line; each
  variant has a server and a client build.
- One download per side and LeviLamina line: `gloamingforest-server-win-x64-ll26.51.zip` and
  `gloamingforest-client-win-x64-ll26.51.zip`, with the mod's files at the root of the ZIP.

[Unreleased]: https://github.com/MiracleForest/GloamingForest/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/MiracleForest/GloamingForest/releases/tag/v0.1.0

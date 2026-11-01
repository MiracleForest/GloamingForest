# Changelog

All notable changes to GloamingForest are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- The build: xmake targets `gf-core`, `GloamingForest` (server and client builds) and `gf-tests`;
  `tools/build.ps1` with machine-wide throttle slots; the shared Twilight Forest checkout (`tools/tf/fetch.ps1`).
- The foundation adapted from Levitate v0.1.0: modules, configs and the in-game config screen,
  `/twilightforest` (aliases `/tf`, `/tffeature`), translations for 29 locales, world data, the companion
  protocol, interactions with tag matchers, block data, redstone, click and hold, the pack deployer and the
  selftest framework.
- The W0 contract headers with neutral stubs, the `gf_test:*` fixture packs, the asset assembler with its
  checks, the ownership manifest (`docs/spec/owners.tsv`) and the CI guards.
- The server and client end-to-end harnesses with the `smoke` scenarios.

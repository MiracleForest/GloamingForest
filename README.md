# GloamingForest

English | [简体中文](README.zh-CN.md)

GloamingForest brings the Gloaming Forest to Minecraft: Bedrock Edition: a magical dimension of dark woods,
landmarks, bosses and their loot, written as a [LeviLamina](https://github.com/LiteLDev/LeviLamina) native mod.
One code base builds a server mod (Bedrock Dedicated Server) and a client mod (local worlds through
LeviLauncher). The goal is the complete experience: every feature, interaction and screen.

> GloamingForest is an unofficial, fan-made project. It is not affiliated with or endorsed by Mojang Studios
> or Microsoft; "Minecraft" is a trademark of Mojang Studios / Microsoft. See [NOTICE](NOTICE) for the
> third-party notices. Please report problems in this repository.

## Status

Preview. Everything is in: the dimension and its portal, the 22 biomes and
terrain, every landmark with its boss (Naga, Lich, Minoshroom, Hydra, Knight Phantoms, Ur-Ghast, Alpha Yeti,
Snow Queen, giants, final castle), the mobs, blocks, items, armour, artifacts, the uncrafting table and maps,
Travellers' Gear, advancements, the sky and weather, and original sounds and structure templates. Every unit
is checked by the server selftests (about 960 in game, plus the C++ unit tests). Expect rough edges: the
client-mod screenshot review is still pending, and what Bedrock cannot do (or does differently) is listed in
[docs/spec/not-ported.md](docs/spec/not-ported.md). Report problems in the issues.

| | |
|---|---|
| Minecraft | Bedrock 1.26.51 |
| LeviLamina | 26.51.x (lip variant `26_51`, the default) |
| Reference | the Java source pinned in [NOTICE](NOTICE) (version 4.8, commit `9e40a757`) |
| Platforms | Windows x64: BDS (`server`) and client (`client`) |

## Install

Install it with [lip](https://github.com/LiteLDev/lip) 0.40 or later in the folder of a LeviLamina BDS or
client instance, or from the mod list of LeviLauncher:

```sh
lip install github.com/MiracleForest/GloamingForest          # the newest supported LeviLamina line
lip install github.com/MiracleForest/GloamingForest#26_51    # LeviLamina 26.51 (Minecraft 1.26.51)
```

lip picks the server or the client build for the workspace. By hand: each release has one ZIP per side
and LeviLamina line, `gloamingforest-<server|client>-win-x64-ll<line>.zip` (for example
`gloamingforest-server-win-x64-ll26.51.zip`); its files go into a `GloamingForest` folder in `plugins/`
of a LeviLamina 26.51 BDS, or in `mods/` of a LeviLamina client instance. Players with the client mod get
the full experience; vanilla Bedrock clients can join a server running the mod with best-effort
fallbacks.

In game: `/gloamingforest`; operators can edit the settings with
`/gloamingforest config`.

## Build

Windows x64 with [xmake](https://xmake.io) 3.x and LLVM (clang-cl):

```powershell
git config --global core.longpaths true
pwsh tools/build.ps1 -Target all -Tests     # bin/server/GloamingForest, bin/client/GloamingForest + gtest
xmake run -P . gf-tests
```

`-LeviLamina <version>` picks the LeviLamina version to build against (default 26.51.7); each supported
line has its package lock in `locks/<variant>.lock`.

Contributors and agents: read [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).

## Licences

- GloamingForest code: [LGPL-3.0-or-later](COPYING.LESSER) (with [the GPL](COPYING)). The foundation is
  adapted from [LiteLDev/Levitate](https://github.com/LiteLDev/Levitate) (LGPL-3.0-or-later).
- Code translated from the upstream Java source (LGPL-2.1-or-later) keeps an attribution header naming the
  source file.
- Textures, models and translations come from the upstream project under [CC BY-NC-SA 4.0](assets/LICENSE):
  non-commercial use only, share alike.
- The upstream sounds, music and structure templates are All Rights Reserved and are **not** included:
  GloamingForest ships original replacements.

See [NOTICE](NOTICE) for the third-party notices and the full attribution.

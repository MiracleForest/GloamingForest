# GloamingForest

English | [简体中文](README.zh-CN.md)

GloamingForest is an **unofficial** port of [The Twilight Forest](https://github.com/TeamTwilight/twilightforest)
4.8 to Minecraft: Bedrock Edition, written as a [LeviLamina](https://github.com/LiteLDev/LeviLamina) native mod.
One code base builds a server mod (Bedrock Dedicated Server) and a client mod (local worlds through
LeviLauncher). The goal is full parity with The Twilight Forest: every feature, interaction and screen.

> GloamingForest is not affiliated with, endorsed by or supported by TeamTwilight, Mojang Studios or
> Microsoft. "The Twilight Forest" is the name of TeamTwilight's mod; "Minecraft" is a trademark of
> Mojang Studios / Microsoft. Please report problems here, not to TeamTwilight.

## Status

Early development. The foundation, the build, the contract headers and the test harnesses are in place;
the dimension, its content and the client screens are being written (see [CHANGELOG.md](CHANGELOG.md)).

| | |
|---|---|
| Minecraft | Bedrock 1.26.51 |
| LeviLamina | 26.51.7 |
| Reference | The Twilight Forest 4.8 (branch `1.21.1`, commit `9e40a757`) |
| Platforms | Windows x64: BDS (`server`) and client (`client`) |

## Install

Download `gloamingforest-server-windows-x64.zip` or `gloamingforest-client-windows-x64.zip` from the
releases and extract the `GloamingForest` folder into `plugins/` of a LeviLamina BDS, or into `mods/` of
a LeviLamina client instance. Players with the client mod get the full experience; vanilla Bedrock
clients can join a server running the mod with best-effort fallbacks.

In game: `/twilightforest` (aliases `/tf`, `/tffeature`); operators can edit the settings with
`/twilightforest config`.

## Build

Windows x64 with [xmake](https://xmake.io) 3.x and LLVM (clang-cl):

```powershell
git config --global core.longpaths true
pwsh tools/build.ps1 -Target all -Tests     # bin/server/GloamingForest, bin/client/GloamingForest + gtest
xmake run -P . gf-tests
```

Contributors and agents: read [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).

## Licences

- GloamingForest code: [LGPL-3.0-or-later](COPYING.LESSER) (with [the GPL](COPYING)). The foundation is
  adapted from [LiteLDev/Levitate](https://github.com/LiteLDev/Levitate) (LGPL-3.0-or-later).
- Code translated from The Twilight Forest (LGPL-2.1-or-later) keeps an attribution header naming the source file.
- Textures, models and translations come from The Twilight Forest under
  [CC BY-NC-SA 4.0](assets/LICENSE): non-commercial use only, share alike.
- The Twilight Forest's sounds, music and structure templates are All Rights Reserved and are **not**
  included: GloamingForest ships original replacements.

See [NOTICE](NOTICE) for the full attribution.

# Contributing to GloamingForest

Thanks for helping. GloamingForest aims at full parity with the upstream 4.8, so every change is
measured against upstream's behaviour, textures and screens.

## Before you start

1. Read [AGENTS.md](AGENTS.md) (layout, rules R1-R25, contracts, build and test) and
   [docs/spec/README.md](docs/spec/README.md) (the units and their owners).
2. Fetch the reference checkout once: `pwsh tools/upstream/fetch.ps1` (into `%LOCALAPPDATA%\gf-upstream\view`). Read upstream only
   from there.
3. Find the owner of what you want to change in [docs/spec/owners.tsv](docs/spec/owners.tsv). A pull request
   changes the files of **one** unit (plus `src/feature/ext/<unit>/` for a needed contract extension);
   `tools/ci/ownership.ps1 -Unit <unit>` checks it.

## Licences and assets

- Code is LGPL-3.0-or-later. Code translated from upstream (LGPL-2.1-or-later) starts with the attribution header
  in AGENTS.md naming the upstream source file.
- Upstream textures, models, blockstates and lang (CC BY-NC-SA 4.0) enter the repository through `tools/import`;
  never copy them by hand.
- Upstream sounds, music (`.ogg`) and structure templates (`.nbt`) are All Rights Reserved: never open, decode,
  copy or convert them. Sounds are synthesised (`tools/sounds`), templates are original `.gft` designs
  (`tools/structures`). `tools/ci/arr-guard.ps1` rejects any matching file, in any commit of a pull request
  (a file added and deleted again would still reach the public history).
- A hand-edited file derived from an upstream texture, model, blockstate or text stays CC BY-NC-SA 4.0: keep it under a
  `tf-derived/` directory with a `tf-derived/SOURCES.json` naming its upstream source (AGENTS.md, assets/LICENSE).
- Do not add AI attribution (for example `Co-Authored-By` lines naming an AI) to commits, pull requests or code.

## Workflow

```powershell
git switch -c <unit>-<topic>
pwsh tools/build.ps1 -Target all -Tests          # both builds + gtest, through a build slot
xmake run -P . gf-tests
pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario <unit>
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario <unit> -Mode local
pwsh tools/ci/ownership.ps1 -Unit <unit>
pwsh tools/ci/arr-guard.ps1 -Range origin/main      # every commit of the branch, not only its tip
```

- Builds and test runs go only through `tools/build.ps1` and `tools/e2e/*/run.ps1`: they take the machine-wide
  throttle slots. Exit code 2 means NOT RUN (no slot in time): retry.
- New `src/core` code comes with gtests in `tests/<unit>/`; features come with selftests
  (`GF_REGISTER_SELFTEST("<unit>.<case>", ...)`) and an e2e scenario `tools/e2e/*/scenarios/<unit>.ps1`.
  Open every client screenshot and compare it with upstream.
- Format C++ with `.clang-format`; keep files UTF-8 with LF line endings (`.editorconfig`, `.gitattributes`).
- Commit in small buildable steps with imperative messages ("Add the naga courtyard layout").
- Pull request titles: `U<n>: <imperative summary>` or `F<n>: ...`, into `main`. Fill in the template: what of
  the unit record is done, what remains, contract extensions, and the verification you ran.

## Reporting bugs

Open an issue here with the game version, the build (server or client), the
LeviLamina version, the steps, what upstream does and what GloamingForest does, and the relevant log lines.

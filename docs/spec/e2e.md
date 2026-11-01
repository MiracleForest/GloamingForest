# End-to-end recipe

Every unit proves its work in the real game before its pull request (rule R22). The harnesses and their
helpers are documented in [tools/e2e/README.md](../../tools/e2e/README.md); this page is the recipe a unit
follows and the conventions of its scenarios.

## The recipe

Run from the unit's worktree, in a fresh pwsh:

```powershell
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
git config --global core.longpaths true

pwsh tools/build.ps1 -Target all -Tests                                        # both builds + gtest (build slot)
xmake run -P . gf-tests                                                        # every gtest passes

pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario <unit>                # bds slot
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario <unit> -Mode local    # client slot + game lock
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario <unit> -Mode remote-vanilla   # when the unit has a vanilla fallback

pwsh tools/ci/ownership.ps1 -Unit <unit>                                       # the diff touches only owned paths
pwsh tools/ci/arr-guard.ps1 -Range origin/main                                 # no All Rights Reserved TF file, in any commit
```

A run passes when it exits 0: the scenario finished, every `Invoke-SelfTest` printed
`SELFTEST DONE pass=<N> fail=0` with at least one test, and no `ERR` / `FTL` line from GloamingForest,
LeviLamina or CrashLogger and no Bedrock content-log error appeared. **Exit code 2 is NOT RUN** (no throttle
slot or game lock in time): retry later, never count it as a pass. Exit code 1 is a failure: the console
lists the reasons; `tools/e2e/<server|client>/out/<scenario>/` holds the logs, `selftest.json` and the
screenshots.

Open **every** screenshot of the client run and compare it with TF's textures and GUI images in
`%LOCALAPPDATA%\gf-tf` (never with TF's audio or templates, which are not in the checkout).

Exceptions:

- **F0** runs the `smoke` scenarios: server `smoke`, client `smoke` in `-Mode local` and `-Mode remote-vanilla`.
- **F14-F16** (specification units) skip e2e and run the doc lint.
- Units without a client-visible part may keep the client scenario to a selftest run plus one screenshot.
- Add `-Mode remote-mod` (client mod against BDS) when the unit sends companion packets.

## Scenarios

- Names follow rule R3: `tools/e2e/server/scenarios/<unit>.ps1`, `tools/e2e/client/scenarios/<unit>.ps1`, with
  optional `<unit>.prelaunch.ps1` (before the server boots) and `<unit>.restore.ps1`. Extra scenarios are
  `<unit>.<topic>.ps1`.
- Put the logic in selftests inside the mod (`GF_REGISTER_SELFTEST("<unit>.<case>", ...)`, dx range of the unit,
  rule R12) and start them from the scenario with `Invoke-SelfTest '<unit>.*'`. The scenario adds what only the
  harness can do: restarts (`Restart-Server`), chat commands, keys and clicks, screenshots.
- The server scenario `full` runs every registered selftest (`*`); a unit's tests must pass there too, so they
  depend only on W0 and the unit itself and clean up after themselves (`flatArea` restores its area).
- Selftests that need the Twilight Forest use `co_await ctx.inTwilight()`; tests of structures compare
  `snapshotVolume(box)` with a golden hash and are reviewed once with screenshots (rule R19).
- Wait for log lines, never for fixed times: modules log one line per user-visible action, and the client logs
  `client screen: <name>` and `client teleported to x y z`.

## Shared machine

Other workers run builds and games on the same machine. Builds and e2e runs go only through
`tools/build.ps1` and `tools/e2e/*/run.ps1`, which take `tools/lib/Throttle.psm1` slots (build 3 with a 10 GB
free-memory gate, bds 5, client 2) and the game instance locks shared with Levitate's, BEI's and
BeetrootSkin's harnesses. Never start BDS, the game or `xmake` by hand for a test (rule R23).
`Import-Module tools/lib/Throttle.psm1; Get-GfSlotStatus` shows who holds what.

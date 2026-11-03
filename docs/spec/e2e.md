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

git fetch origin
pwsh tools/ci/ownership.ps1 -Unit <unit>                                       # the diff touches only owned paths (vs origin/main)
pwsh tools/ci/arr-guard.ps1 -Range origin/main                                 # no All Rights Reserved TF file, in any commit
```

`tools/ci/ownership.ps1` compares with `origin/main` by default (`main` only when there is no remote): the local
`main` of a worktree is usually behind, and a stale base lists other units' merged changes as violations.

A run passes when it exits 0: the scenario finished, every `Invoke-SelfTest` printed
`SELFTEST DONE pass=<N> fail=0` with at least one test, no `ERR` / `FTL` line appeared in the log (see
[Error lines](#error-lines)) and, on the client, the game's content log has no error. **Exit code 2 is NOT
RUN**: no throttle slot or game lock in time, a game that stopped answering (client watchdog) or a client that
was not in its test world: retry later, never count it as a pass. Exit code 1 is a failure: the console lists
the reasons; `tools/e2e/<server|client>/out/<scenario>/` holds the logs, `selftest.json` and the screenshots.

### Error lines

Every `ERR` / `FTL` line of the run fails it, whatever the logger (`GloamingForest`, `LeviLamina`,
`CrashLogger`, Bedrock's `[Server]` with or without a content category such as `[Blocks]`, BeDD, ...), on the
server console, in the client log and in a remote client run's `[host]` lines (`Select-ErrorLines` in
`tools/e2e/common/common.ps1`). So an `ERR` line always means a real error, with exactly two exceptions:

- **BDS's transport banner.** Every boot prints six `ERR [Server]` lines, `================ TRANSPORT TYPE ERROR
  ===================` through `=======================================================`, because the harness
  runs RakNet on purpose (`transport=raknet`: the client harness joins `127.0.0.1` without the online signaling
  service that NetherNet needs). Exactly those six texts are skipped; any other line of the same look fails.
- **The console's echo of a command reply.** BDS prints the error reply of a command run on its console as
  `ERR [Server] <reply>`, and scenarios run failing commands on purpose (a refused `/twilightforest locate`). The
  mod logs every command error first as `[GloamingForest] Command error: <reply>` (`src/mod/Commands.cpp`); an
  `ERR [Server]` line whose text is such a reply of the same run is the echo, not an error. A scenario checks
  the replies it provokes itself. The mod logs a reply as its message id and parameters, so this covers the
  mod's own literal replies; a vanilla command's translated reply (`commands.tickingarea-remove...` logged, "No
  ticking area ..." printed) is not recognised: a scenario does not send failing vanilla commands on the console
  (selftests run them through `runServerCommand`, whose output never reaches the console).

### Client runs: content log, game options, watchdog, test world

- **Content log.** Every client run turns the game's content log file on (`options.txt content_log_file:1`,
  the old value is put back afterwards) and reads the `ContentLog*.txt` of its game sessions in
  `<instance>\Minecraft Bedrock\logs` once the game closed (the game writes them as it closes): every entry of
  level `error` fails the run, `warning` entries are listed in the summary (`content-log warning: ...`) and
  fail only in a strict run (`-StrictContentLog`, or a scenario sets `$script:ContentLogStrict = $true`). All
  entries go to `out/<scenario>/client-content-log.txt`. Entries that are not about this build's packs are
  expected (`$script:ContentLogExpected` in `tools/e2e/client/lib.ps1`: the models scenario's deliberately
  broken `gfpreview_canary` files, the game's cached diving-helmet fogs); a scenario adds the lines it causes on
  purpose with `$script:ContentLogExpected += '<regex>'`.
- **Game options.** Before every run the harness puts back the options scenarios toggle, in every
  `minecraftpe\options.txt` of the instance: `gfx_hidehud:0` (F1 hides the HUD: models, ui, families),
  `gfx_hidehand:0` (ui), `game_thirdperson:0` (F5: ui) and `graphics_mode:2` (ui switches Classic / Vibrant
  Visuals). A scenario that changes another option restores it in its `<unit>.restore.ps1` and adds it to
  `$script:ForcedOptions` (lib.ps1).
- **Watchdog.** A BeDD pipe call waits for the game's answer without a timeout of its own. The harness wraps
  every pipe call and window capture (`Invoke-Guarded`): when one takes longer than 120 s plus the waits it asked
  for (`$env:GF_E2E_PIPE_TIMEOUT_SEC` changes the 120), the process tree of the run's game is killed (only while
  the run holds that instance's game lock), the console shows `[e2e] WATCHDOG: ...` and the run is NOT RUN.
- **Test world.** After joining, `Enter-TestWorld` checks the mod's `world: level '<LevelName>' opened` line
  (`src/game/WorldDb.cpp`): a world other than `GF-Test`, or a BeDD `join` the run did not send, gets one more
  fresh game with a freshly installed `GF-Test`; the second time the run is NOT RUN (the console says why).

### Pending joins on a shared instance

On clone `hi2` the game left its world and joined `127.0.0.1:19140` ("[BeDD] join: connecting to
127.0.0.1:19140", screen `joining_multiplayer_external_server`), and the harness's retry then entered another
world. Nothing in the instance's files asked for it (BeDD's `config.json` said `mode ClientOnly`, LeviLauncher's
files and `external_servers.txt` name no such server): another harness drove the same instance through the
instance's default BeDD pipe `BeDD-<instance>` without taking our `hi<N>-e2e.lock`. 19140 is the first port of
BeDD's own development servers (`%LOCALAPPDATA%\BeDD\servers`), and BeDD's tools address a game by
`BeDD-<instance>` (`Initialize-BeDDInstance`); its client resends a request after a broken pipe and waits for the
pipe to come back, so a request of that harness reached the next game started in the instance, ours. The same
harness parked our deployed mod in `mods.bedd-it-aside\GloamingForest`. Fixes in the harness:

- each run gives its game a pipe of its own, `BeDD-<instance>-gf<pid>` (`Install-BeDDMod -PipeName`): a client
  of the default name can no longer reach it;
- before every run, copies of our mod in another harness's `mods.<harness>-aside` folder are deleted
  (`Remove-StaleModCopies`), so no stale build comes back into `mods\`;
- the test-world check above turns any remaining stray join into a retry and then NOT RUN, never a pass or a
  scenario failure.

Open **every** screenshot of the client run and compare it with TF's textures and GUI images in
`%LOCALAPPDATA%\gf-tf` (never with TF's audio or templates, which are not in the checkout).

Exceptions:

- **F0** runs the `smoke` scenarios: server `smoke`, client `smoke` in `-Mode local` and `-Mode remote-vanilla`.
- **F14-F16** (specification units) skip e2e and run the doc lint.
- Units without a client-visible part may keep the client scenario to a selftest run plus one screenshot.
- Add `-Mode remote-mod` (client mod against BDS) when the unit sends companion packets.

## Scenarios

- Names follow rule R3: `tools/e2e/server/scenarios/<unit>.ps1`, `tools/e2e/client/scenarios/<unit>.ps1`, with
  optional `<unit>.prelaunch.ps1` (before the server boots, or before the game starts) and `<unit>.restore.ps1`.
  Extra scenarios are `<unit>.<topic>.ps1`.
- Remote client modes (`remote-mod`, `remote-vanilla`): the BDS the client joins is the server harness's scenario
  `client-host`. Before it boots it runs the optional `tools/e2e/client/scenarios/<unit>.host-prelaunch.ps1`,
  dot-sourced in the server harness (after the slot reset, the mod deployment and `server.properties`), so a unit can
  install a probe pack on the host: `Join-Path $script:SlotDir "$(Get-ModsDirName $script:SlotDir)\$script:ModName"`
  is the host's mod folder, `$HostMode` the client mode. The client's own `<unit>.prelaunch.ps1` does not reach the
  host.
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

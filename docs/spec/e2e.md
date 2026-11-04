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
the reasons; `tools/e2e/server/out/<scenario>/` and `tools/e2e/client/out/<scenario>[-<mode>][-slot<N>]/`
(remote modes add the mode, clone instances the slot, so no run deletes another mode's or slot's output) hold the
logs, `selftest.json` and the screenshots.

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
  mod logs a command error first as `[GloamingForest] Command error: <msgId> [<params>]` and, when the server
  language prints it differently (a translated vanilla reply, a reply with parameters filled in), also as
  `[GloamingForest] Command error text: <text>`, the text `I18n` makes of the id and parameters
  (`src/mod/Commands.cpp`; the same reply at most once per 10 s, its guard against ticking functions); BDS
  prints the echo right after it. An `ERR [Server]` line is that echo when the mod logged the same text (either
  form) from the same source (in a remote client run the host's `[host]` lines) at most 10 s before it; an
  `ERR [Server]` line with no such reply before it, a later one or one with other text still fails the run. So
  the mod's own replies, with or without parameters, and vanilla replies are covered alike (the server `smoke`
  scenario sends a failing `tickingarea remove` on purpose to keep it so). A scenario checks the replies it
  provokes itself.

### Client runs: content log, game options, watchdog, test world

- **Content log.** Every client run turns the game's content log file on (`options.txt content_log_file:1`,
  the old value is put back afterwards) and reads the `ContentLog*.txt` of its game sessions in
  `<instance>\Minecraft Bedrock\logs` once the game closed: every entry of level `error` fails the run,
  `warning` entries are listed in the summary (`content-log warning: ...`) and fail only in a strict run
  (`-StrictContentLog`, or a scenario sets `$script:ContentLogStrict = $true`), and entries of every other
  level (`verbose`, `info`) are listed in the summary (`content-log verbose/info: ...`) and **never** fail a
  run (they can still point at a parity bug: read them).
- **Content-log canary.** The game creates a `ContentLog*.txt` only on its first entry, so a clean session
  writes no file, and "no file" alone cannot tell "clean" from "logging broken". Every run therefore loads
  one deliberate, harmless bad line: `GloamingForest_Test_RP` (`assets/test/rp`, development builds only)
  has an animation used by nothing whose Molang does not parse
  (`animations/gf_e2e_content_log_canary.animation.json`). The client logs it as it loads the pack:
  `[Molang][error] ... gf_e2e_content_log_canary ...` (the client mod's global pack in `local` and
  `remote-mod` runs; in remote modes the BDS host sends the same pack, so a `remote-vanilla` client logs it
  too). The canary is expected (never an error of the run), but it is **required per game session**: the
  harness records every game it starts (`Start-Game`) and how it ended (`Stop-Game`: `closed`, `killed`
  after 30 s, or `gone` when it had already exited: a crash or the watchdog), and assigns each
  `ContentLog*.txt` to the last session started before the file was created. Every `closed` session must
  have a file that reports the canary: no file, or files without it, fail the run ("the check cannot see
  that session's pack errors"), even when another session of the run (a relaunch, ui's Vibrant Visuals
  half) logged it. A `killed` or `gone` session is listed and needs no canary, for itself only: it never
  waives the check for the run's other sessions. A file with only the canary is clean. The summary and
  `client-content-log.txt` list every session with its files and canary count. The canary was chosen over
  "a missing file counts as clean once `content_log_file:1` was verified": that check cannot see a log
  written elsewhere, a game that never loaded the packs or stopped writing the file, and the canary proves
  the whole path (option, pack load, file, parser) in every session. Its bone is named `gfpreview_canary`
  because the models scenario (F2's `models.ps1`) treats every error that names `gfpreview_canary` as one
  of its own canaries and fails on any other Molang error; once that scenario drops the entries matching
  `$script:ContentLogExpected` first, the bone can get a neutral name.
  `pwsh -NoProfile -File tools/e2e/client/selfcheck.ps1` checks these verdicts without a game.
  `out/<scenario>[-<mode>][-slot<N>]/client-content-log.txt` lists every distinct entry with its count, then the first 2000 entries
  as written (a broken particle can log hundreds of thousands). Entries that are not about this build's packs are
  expected (`$script:ContentLogExpected` in `tools/e2e/client/lib.ps1`: the models scenario's deliberately
  broken `gfpreview_canary` files, the game's cached diving-helmet fogs); a scenario adds the lines it causes on
  purpose with `$script:ContentLogExpected += '<regex>'`.
- **Game options.** Before every launch the harness sets the options scenarios toggle, in every
  `minecraftpe\options.txt` of the instance: `gfx_hidehud:0` (F1 hides the HUD: models, ui, families),
  `gfx_hidehand:0` (ui), `game_thirdperson:0` (F5: ui) and `graphics_mode:2` (ui switches Classic / Vibrant
  Visuals), plus `content_log_file:1`. When the run ends (after the scenario's `<unit>.restore.ps1`) each
  gets the instance's own value back (`Restore-RunOptions`): slot 1 is shared with BEI's and BeetrootSkin's
  harnesses and with a person's own play, so a person's own `gfx_hidehud:1`, `gfx_hidehand:1` or Classic
  graphics mode survives our runs. The old values wait in `%LOCALAPPDATA%\lv-e2e\game-options.<instance>.json`
  until then, so the next run puts back what a killed run found, not what it left. HUD safety net: every run
  plays with the HUD and hand shown, and for the options in `$script:ForcedOptions` the value a killed run's
  backup recorded wins even when the option changed since (the killed run's scenario toggled it in game:
  models hides the HUD with F1 and the game writes `gfx_hidehud:1` itself), so a killed models run never
  leaves an instance without its HUD past the next run. Its one cost: a person who changes one of these
  options between a killed run and the next run gets the killed run's backup value back. Other options a
  killed run's backup holds keep a value that changed since. A scenario that changes another option
  restores it in its `<unit>.restore.ps1` and adds it to `$script:ForcedOptions` (lib.ps1).
- **Hangs (watchdog).** A BeDD pipe call waits for the game's answer without a timeout of its own. The harness
  wraps every pipe call and window capture (`Invoke-Guarded`): when one takes longer than 120 s plus the waits
  it asked for (`$env:GF_E2E_PIPE_TIMEOUT_SEC` changes the 120), the process tree of the run's game is killed
  (only while the run holds that instance's game lock). The BeDD connect loop stops at once after that instead
  of retrying until its own deadline. A game that is still running without `GloamingForest enabled` after 180 s
  (`$env:GF_E2E_ENABLE_TIMEOUT_SEC`) is hung in "Enabling mods" when it used under 0.5 s of CPU in the next
  10 s, and gets 120 s more when it is busy; a game whose BeDD pipe never answers while it loads is hung too.
  Each prints `[e2e] WATCHDOG: ...`, writes a minidump of the game (`out/hangs/<scenario>-<mode>-<instance>-<time>-<pid>.dmp`:
  every thread's stack, the module list, a few MB; open it in WinDbg or Visual Studio; the run's client log and
  install trace are saved next to it as `-client.log` and `-install-trace.txt`; no run clears `out/hangs`, so the
  retry a NOT RUN asks for keeps them; files older than 30 days go) and kills it, prints the mod's
  install trace (below) and makes the run NOT RUN, with two exceptions that fail it (exit 1): failures recorded
  before the hang (ERR lines, failing selftests, content-log errors of a session that closed), and repeated
  hangs of the same scenario and mode on the same build: two runs in a row that hung on two different
  instances, or three in a row on one instance (a broken clone such as `hi2` gets one more retry than the
  build, never an endless one). The record is
  `%LOCALAPPDATA%\lv-e2e\gf-hang.<scenario>.<mode>.<dll hash>.json` with the instance of every hung run; a run
  of that scenario, mode and build that does not hang clears it. So a deadlock in the mod's own code cannot be
  retried as NOT RUN for ever. `$env:GF_E2E_TEST_HANG=enable` (freeze the game before
  `GloamingForest enabled`) or `=pipe` (freeze it right before the scenario) suspends the game on purpose to
  exercise both paths; a run that reaches the freeze point is not counted, but a real hang **before** it (the
  game stuck in "Enabling mods" before the harness froze it) is a real hang and counts like any other.
- **Install trace.** The mod logs `installing <N> modules` when it starts enabling, `<N> modules installed in
  <ms> ms` when done, `module <name> installed in <ms> ms (slow)` for an `install()` over 1 s, and, from a
  watcher thread, `install: module <name> still running after <ms> ms` every 20 s while one `install()` has not
  returned (`src/mod/Module.cpp`; the same for `uninstall`). A hung run prints these lines; none at all means
  the game hung before the mod's `enable()`.
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
  scenario failure;
- a game of the instance that is still running once the run holds the game lock (another harness's game still
  exiting after it gave the lock back keeps `mods\GloamingForest\GloamingForest.dll` open) is closed, given 60 s
  and then killed (`Clear-InstanceGames`); `Deploy-Mod` retries the removal of the old mod folder for 30 s, and a
  folder that stays open makes the run NOT RUN.

Residual risk: that harness (BeDD's own integration tests, `bedd-it`, outside this repository) still does not
take `hi<N>-e2e.lock`. Its pipe requests no longer reach our game, but while a GloamingForest run holds the
instance it can still move our deployed mod aside or start a game of its own in the instance. Such a run ends in
a wrong world, a missing `GloamingForest enabled` or a game the harness did not start, i.e. NOT RUN or a
visible failure, never a false pass. The fix belongs to that harness: it should take the instance's
`%APPDATA%\levilauncher.exe\hi<N>-e2e.lock` (slot 1: `bei-e2e.lock`) for as long as it uses the instance, as
BEI's, BeetrootSkin's and our harnesses do.

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
  host. The host starts only once the game lock is the run's; when no server slot frees up within
  `-SlotWaitMinutes` (3), the run kills the host harness with its whole process tree, waits until every process of
  it has exited, closes the files its output was redirected into, gives the game lock back and queues again. Its
  output folder is cleared again on re-entry, with retries for 20 s; a file still in use after that makes the run
  NOT RUN (exit 2), never FAILED. The folder is renamed aside first and only then deleted, so a folder that cannot
  be cleared keeps every file of the previous run (client log, screenshots, content-log report). Each process of
  the host tree gets at least 2 s to exit, even after a slow one used up the shared deadline.
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

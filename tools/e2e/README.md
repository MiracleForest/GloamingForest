<!-- Adapted from LiteLDev/Levitate v0.1.0 (tools/e2e/README.md), LGPL-3.0-or-later. -->
# GloamingForest end-to-end tests

Two harnesses, one per LeviLamina platform. Both run the real game binaries:

| | Server (`tools/e2e/server`) | Client (`tools/e2e/client`) |
|---|---|---|
| Runs | Bedrock Dedicated Server + LeviLamina 26.51.7 (`bedrock_server_mod.exe`) | Minecraft 1.26.51 through LeviLauncher (instance `1.26.51.01` and its `-hi<N>` clones) |
| Mod taken from | `bin/server/GloamingForest` | `bin/client/GloamingForest` |
| Driven by | console stdin/stdout | **headless input** through the [BeDD](https://docs.levimc.org/BeDD/) mod's pipe (the game stays in the background, your mouse and keyboard are untouched; `-Foreground` = real `SendInput`) and window screenshots |
| Parallel runs | the throttle's **bds** pool: 5 slots | the throttle's **client** pool: 2 runs, each on a free game instance |
| Output | `server/out/<scenario>/` `server.log`, `selftest.json` | `client/out/<scenario>[-<mode>][-slot<N>]/` `*.png`, `client.log`, `selftest.json` |

Prefer the **server** harness for logic: it is fast, headless and parallel, and selftests spawn
SimulatedPlayers there. Use the **client** for UI, rendering and anything that needs a real player.

Exit codes: `0` pass, `1` fail, `2` **NOT RUN** (no throttle slot or game lock in time: retry, it is
never a pass).

```powershell
pwsh tools/build.ps1 -Target all -Tests                             # both builds + gtest (through a build slot)
pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario smoke      # [-TimeoutSec 900] [-Quiet] [-Slot n]
pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario full       # every selftest of every unit (raises its own budget to 4 h)
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario smoke -Mode local
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario smoke -Mode remote-mod
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario smoke -Mode remote-vanilla
pwsh -NoProfile -File tools/e2e/client/run-all.ps1 [-RemoteVanilla] [-Scenario a,b] # client acceptance run
```

A scenario that needs more than the server harness's default budget (`-TimeoutSec 900`) calls
`Request-ScenarioTime <seconds>` first (`full` does); an explicit `-TimeoutSec` still wins.

**Client acceptance run** (`client/run-all.ps1`): every unit's client scenario
(`client/scenarios/<name>.ps1` without a dot; `-Variants` adds `biomes.classic`, `ui.graphics`, ...)
in sequence, `-Mode local` by default, `-RemoteVanilla` adds the vanilla-client run. A run that
exits 2 (NOT RUN) is retried (`-MaxAttempts 3`, `-RetryDelaySec 60`); a failure is not. It writes
`client/out/run-all/<time>/summary.md` (result, attempts, time, selftests, content-log warnings,
screenshot folder and count, first failure per run; also copied to `client/out/run-all/summary.md`)
and every attempt's harness output next to it. Exit 0 all passed, 1 a failure, 2 only NOT RUN left.

Never start BDS or the game by hand for a test, and never run `xmake` outside `tools/build.ps1`
(rule R23): the scripts take the throttle slots that keep a shared machine usable.

## Throttle and shared locks

`tools/lib/Throttle.psm1` (`Enter-GfSlot -Pool build|bds|client`) holds one exclusively opened lock
file per slot (`%LOCALAPPDATA%\gf-throttle\<pool>\<n>.lock`, `<n>.lock.owner` names the holder:
pid, worktree, unit). The OS releases a lock when its process dies, so nothing goes stale.
`Get-GfSlotStatus` lists the holders.

- **bds** (5 slots): slot n is the server directory `%LOCALAPPDATA%\lv-e2e\slots\<n>\` that
  Levitate's harness uses too; the throttle also takes Levitate's `slots\<n>.lock`, so both
  projects share the slots and the BDS base install (`lv-e2e\bds-base`, `install.lock`). Slot n
  uses UDP ports `19600 + 10n` (IPv4) and `+1` (IPv6), or the next free pair of its band; a
  "Port [n] may be in use" exit boots again on the next pair (up to 4 times).
- **client** (2 slots): a run then takes a game instance lock shared with Levitate's, BEI's and
  BeetrootSkin's harnesses: `%APPDATA%\levilauncher.exe\bei-e2e.lock` for the base instance,
  `hi<N>-e2e.lock` for the clone `versions\1.26.51.01-hi<N>`. By default every existing instance
  is a candidate (`-Slot 2`, `$env:GF_E2E_CLIENT_SLOTS = '1,2,3'` restrict it). Remote modes take a
  bds slot too, only once the game lock is held.
- **build** (3 slots, each `xmake -j6`): granted only while 10 GB of memory is free.

## Client modes (`-Mode`)

| Mode | World | Client mod | What it proves |
|---|---|---|---|
| `local` (default) | the `GF-Test` local world; the integrated server runs the mod too | deployed | everything in one process |
| `remote-mod` | a BDS slot started through the server harness (scenario `client-host`), joined through Play > Servers | deployed | the companion protocol over the network |
| `remote-vanilla` | the same BDS | **moved aside** (only LeviLamina; the BDS resource packs are downloaded on join) | a vanilla client joins and plays without companion packets (best effort parity) |

In remote modes the BDS console log is merged into the session log as `[host] ...` lines, so
`Wait-LogLine` sees client and server lines alike, and `Send-HostConsole 'cmd'` runs a server
console command. The host stops when the client run ends (or dies).

**Host hook.** Before the host boots, `server/scenarios/client-host.prelaunch.ps1` dot-sources the
client scenario's optional `client/scenarios/<scenario>.host-prelaunch.ps1` inside the server
harness, after the slot was reset, the mod deployed and `server.properties` written. It sees the
server API (`lib.ps1`): the host's mod folder is
`Join-Path $script:SlotDir "$(Get-ModsDirName $script:SlotDir)\$script:ModName"` (e.g. for a probe
pack), `$HostMode` is `remote-mod` or `remote-vanilla`, `$env:GF_E2E_HOST_DIR` the host folder the
client also sees (`$script:HostDir`). The next run of the slot syncs it again, so nothing needs
undoing. The client's `<scenario>.prelaunch.ps1` prepares the game only. `smoke.host-prelaunch.ps1`
leaves a marker that the remote `smoke` runs check.

## Prerequisites

- **Server:** [lip](https://github.com/LiteLDev/lip) on `PATH`; the first run installs BDS +
  LeviLamina into `%LOCALAPPDATA%\lv-e2e\bds-base` if Levitate's harness has not already.
- **Client:** the LeviLauncher instance `%APPDATA%\levilauncher.exe\versions\1.26.51.01` was started
  and signed in once. BeDD, in this order: `$env:BEDD_DIR` (any BeDD bin folder); the local development
  build `%USERPROFILE%\Desktop\BeDD\bin\BeDD` only with `$env:GF_E2E_BEDD_DEV=1`; else the pinned release
  (v0.3.1, SHA-256 checked), which the harness downloads once into `%LOCALAPPDATA%\gf-e2e\BeDD` and
  re-downloads when that copy is incomplete or another version (`$env:GF_BEDD_URL` + optional
  `$env:GF_BEDD_SHA256` install another zip there). Leave both variables unset for acceptance runs. Every
  run prints `BeDD v<version> from <dir> (<source>)` at the start and `input: BeDD ...` at the end. A BeDD
  that cannot hook the game (`FTL ... Couldn't find: ...` + `In module: BeDD`) stops the run as soon as the
  game has loaded; one that was not the release copy is retried once with it (docs/spec/e2e.md, BeDD).

## What a run does

**Server** (`server/run.ps1`): optional `-Build`; installs BDS if needed; takes a bds slot; syncs
the slot from `bds-base` and wipes its world, logs and configs; copies the mod to
`plugins\GloamingForest`; writes `server.properties` for a fresh `GF-Test` world (FLAT, seed
20260521, survival, peaceful, cheats on, `texturepack-required=true`, content log on the console);
runs `scenarios/<name>.prelaunch.ps1` if present; boots and waits for `GloamingForest enabled` and
`Server started`; sets always day, clear weather, no mob spawning; dot-sources
`scenarios/<name>.ps1`; sends `stop`.

**Client** (`client/run.ps1`): optional `-Build` (before any lock); takes a client slot and a game
instance lock; remote modes start the BDS host now; closes a running game of that instance;
deletes copies of our mod another harness parked in a `mods.<harness>-aside` folder; deploys the mod
to `mods\GloamingForest` (fresh copy) and BeDD with a pipe of this run only
(`BeDD-<instance>-gf<pid>`); moves every other mod (a leftover Levitate, BEI, ...) to
`mods.lv-e2e-aside\`; installs the `GF-Test` world (or adds the server entry); puts the options
scenarios toggle back (`gfx_hidehud:0`, `gfx_hidehand:0`, `game_thirdperson:0`, `graphics_mode:2`)
and turns the game's content log on (remote modes: a host that found no server slot within
`-SlotWaitMinutes` is killed with its whole process tree and waited for, its redirected output files
are closed, and the run gives the game lock back and queues again; an output folder still in use
after 20 s of retries is NOT RUN); launches the game, waits for `GloamingForest enabled`, enters
the world, checks it is `GF-Test` (else one more fresh game, then NOT RUN) and waits for the HUD;
dot-sources the scenario; **always** cleans up (game closed, content log checked, world and mod
removed, other mods restored, the instance's own values of the options put back: a person's own
hidden HUD or hand on slot 1 survives, and after a killed run the values its backup recorded win, so
a killed models run never leaves the HUD hidden (the HUD safety net), locks released). Every BeDD pipe call and
window capture runs under a watchdog: a game that does not answer within 120 s (plus the call's own
waits; `$env:GF_E2E_PIPE_TIMEOUT_SEC`) is killed and the run is NOT RUN (`[e2e] WATCHDOG: ...`), with a
minidump of the game in `out/hangs/<scenario>-<mode>-<instance>-<time>-<pid>.dmp` (its client log and
install trace next to it; no run clears `out/hangs`) and the mod's install trace printed.

A run **fails** when the scenario throws; a selftest fails, never prints `DONE` or matches no test;
any `ERR`/`FTL` line appears in the log, of any logger (`GloamingForest`, `LeviLamina`,
`CrashLogger`, Bedrock content errors `ERR [Server] [Blocks|Items|...]`, other `ERR [Server]` lines,
...), except BDS's six-line "TRANSPORT TYPE ERROR" banner and the console's echo of a command reply the
mod logged as `Command error:` or `Command error text:` ([docs/spec/e2e.md](../../docs/spec/e2e.md#error-lines)); the client's
content log has an error, or a game session that closed normally has no content log with the canary of
`GloamingForest_Test_RP` (then the check cannot see that session's log; per session, a killed or crashed one is exempt, and so is a remote-vanilla one that neither joined the host nor downloaded its packs; [docs/spec/e2e.md](../../docs/spec/e2e.md#client-runs-content-log-game-options-watchdog-test-world)) (warnings are listed, and fail with `-StrictContentLog` or
`$script:ContentLogStrict = $true`; verbose and info lines are listed and never fail;
`out/<scenario>[-<mode>][-slot<N>]/client-content-log.txt`); a crash file appears;
the server exits non-zero or has to be killed.

## Writing a scenario

A scenario is `server/scenarios/<name>.ps1` or `client/scenarios/<name>.ps1` (rule R3:
`<unit>.ps1`, plus optional `<unit>.prelaunch.ps1` / `<unit>.restore.ps1`, and for the client's
remote modes `<unit>.host-prelaunch.ps1`, run on the BDS host: see Client modes). Put the feature logic in
**selftests inside the mod** (`/gloamingforest selftest "<unit>.*"`) and use the scenario to start
them; the client scenario adds what only a real client shows: screens, keys, screenshots.

```powershell
# server/scenarios/trees.ps1
$r = Invoke-SelfTest 'trees.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) trees selftest(s) failed" }

# client/scenarios/trees.ps1
Invoke-Teleport '/tp @s 300 -60 0'
Save-Screenshot 'canopy-tree' | Out-Null      # -> client/out/trees/canopy-tree.png: open it and compare
```

### Helpers

| Helper | Server | Client | What it does |
|---|:-:|:-:|---|
| `Invoke-SelfTest 'glob' [-TimeoutSeconds 600] [-AllowEmpty]` | x | x | Sends `gloamingforest selftest "<glob>"` (console on the server, chat on the client), waits for `SELFTEST DONE` and returns `{Pass, Fail, Passed[], Failed[], Results[], Ok, Problems[]}`. It does not throw; the run fails at the end if any call was not OK. |
| `Wait-LogLine 'regex' [timeoutSec] [-Since $mark]` | x | x | Returns the matching line; throws on timeout or if the server/game exits. |
| `Get-LogMark` / `Get-ServerLog [-Since $mark]` | x | x | Log position / log lines of this run (client: `Get-LeviLog`). |
| `Send-Console 'cmd'` | x | | One line to the server's stdin, no leading `/`. |
| `Restart-Server` | x | | Stops the server and boots it again on the same world (world reload); waits for `world ready`. |
| `Wait-Seconds n` | x | | Sleeps while echoing server output. |
| `Invoke-Chat '/cmd'`, `Invoke-Teleport '/tp ...'`, `Invoke-ChatConfirmed` | | x | Chat commands (confirmed through the client trace lines). |
| `Open-Inventory` / `Close-Screen` | | x | `E` / `Esc` and wait. |
| `Click x y -Relative [-Right]`, `Move-Mouse`, `Scroll n`, `Press-Key`, `Type-Text` | | x | Input (fractions of the 1600x900 client area). |
| `Save-Screenshot 'name'` | | x | `client/out/<scenario>[-<mode>][-slot<N>]/name.png` (1280 wide). |
| `Send-HostConsole 'cmd'` | | x | Remote modes: a console command on the BDS host. |
| `Invoke-Guarded 'what' { ... } [-Seconds n]` | | x | Runs a block that waits on the game under the watchdog (the game is killed and the run is NOT RUN when it takes longer). BeDD pipe calls and `Save-Screenshot` are guarded already. |
| `$script:ContentLogExpected += 'regex'`, `$script:ContentLogStrict = $true` | | x | Content-log lines the scenario causes on purpose; warnings fail too. |

`pwsh -NoProfile -File tools/e2e/client/selfcheck.ps1` checks the client harness's game-free logic
(content-log verdicts, clearing a locked output folder, the host harness teardown) without a game or
a slot; run it after changing `client/lib.ps1` or `client/run.ps1`.

The client mod logs `client screen: <name>` when the top screen changes and `client teleported to
x y z` on server teleports (`src/client/Trace.cpp`); the client helpers wait for these lines.

### Selftest protocol (contract with the mod)

```
SELFTEST START count=<N>
SELFTEST PASS <name>
SELFTEST FAIL <name>: <reason>
SELFTEST DONE pass=<N> fail=<M>
```

The glob is sent quoted (`gloamingforest selftest "core.*"`): `*` is not valid in an unquoted
command argument. Lines may appear twice (logger and command output); the harness de-duplicates.

## Reading results

- Console output ends with `PASSED`, `FAILED:` and the reasons, or `NOT RUN`.
- `server.log` (`server-2.log` after a `Restart-Server`) / `client.log`: this run only.
- `selftest.json`: every `Invoke-SelfTest` result.
- Screenshots are the evidence for UI: open every PNG and compare it with upstream's textures and GUI
  images in the local upstream checkout (`%LOCALAPPDATA%\gf-upstream\view`).

## Pitfalls

- **Windows Firewall asks once per new executable path** (each bds slot, each clone instance).
- `%LOCALAPPDATA%` is virtualized when the harness runs from a packaged app; processes started from
  the same app see the same files.
- The fixture world `client/fixtures/GF-Test/` holds only `level.dat` (the game generates flat
  terrain); `client/fixtures/build-fixture.ps1 -Source <world>` rebuilds it after a game update.

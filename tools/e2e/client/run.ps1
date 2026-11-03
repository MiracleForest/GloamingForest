# Adapted from LiteLDev/Levitate v0.1.0 (tools/e2e/client/run.ps1), LGPL-3.0-or-later.
<#
.SYNOPSIS
  Run a GloamingForest scenario in the real game client (LeviLauncher instance).

.DESCRIPTION
  1. (-Build) builds bin/client/GloamingForest via tools/build.ps1 -Target client (before
     taking the lock, so the game is not held while compiling).
  2. Takes a game lock: slot 1 (the LeviLauncher instance) uses
     %APPDATA%\levilauncher.exe\bei-e2e.lock, shared with BEI's and BeetrootSkin's harnesses;
     slots 2.. (-Slot, headless only) are clone instances with a lock each. Waits while every
     allowed slot is busy; gives up after -LockTimeoutMinutes with exit code 2. Remote modes
     start the BDS host only after that, so a queued run holds no server slot.

  Input is headless by default: the BeDD mod (https://github.com/LiteLDev/BeDD,
  %USERPROFILE%\Desktop\BeDD; $env:BEDD_DIR overrides its bin folder) is
  deployed with ours and every click, key, text and screenshot goes through its control pipe. The
  game stays in the background and never takes the real mouse, keyboard or foreground, so the PC
  stays usable during a run. -Foreground uses real SendInput as before (slot 1 only).
  3. Closes a running game, restores mods a killed run left aside, deletes stale copies of our mod
     that other harnesses parked (mods.<harness>-aside), deploys the mod to mods\GloamingForest,
     moves every other mod (e.g. BEI) aside, installs the GF-Test world, puts the options scenarios
     toggle back (gfx_hidehud, gfx_hidehand, game_thirdperson, graphics_mode) and turns the game's
     content log on (content_log_file:1, the old value is put back at the end).
  4. Runs optional scenarios/<name>.prelaunch.ps1, launches the game, waits for
     "GloamingForest enabled", enters the world, checks that it is GF-Test (the mod's
     "world: level 'GF-Test' opened" line; another world: one more fresh game, then NOT RUN),
     dot-sources scenarios/<name>.ps1.
  5. Fails on: scenario exception, failing/empty/unfinished selftests, ERR/FTL lines (any logger;
     only BDS's transport banner and the console's echo of a command reply are not errors), errors
     in the game's content log (warnings are listed; they fail with -StrictContentLog or when the
     scenario sets $script:ContentLogStrict = $true).
  6. Always: closes the game, removes the GF-Test world and our mod, restores the other mods,
     runs scenarios/<name>.restore.ps1, puts back the instance's own values of every option the run
     set (HUD, hand, camera, graphics mode, content log), releases the lock.
  Every BeDD pipe call and window capture is guarded by a watchdog (lib.ps1 Invoke-Guarded,
  $env:GF_E2E_PIPE_TIMEOUT_SEC, default 120 s on top of the call's own waits), and a game still
  running without "GloamingForest enabled" after $env:GF_E2E_ENABLE_TIMEOUT_SEC (180 s; idle, or
  120 s more when busy) counts as hung too: the game is killed with its process tree and the run is
  NOT RUN (exit 2, a "WATCHDOG:" line). It fails (exit 1) instead when ERR lines, failing selftests
  or content-log errors came before the hang, or when the same scenario and mode hung on the same
  build in the runs before (two in a row on different instances, three on one). A hung game gets a
  minidump (out/<scenario>/hang-<pid>.dmp) and the mod's install trace is printed.
  $env:GF_E2E_TEST_HANG=enable|pipe freezes the game on purpose to test this.
  Output: tools/e2e/client/out/<scenario>/ (screenshots, client.log, client-content-log.txt,
  selftest.json). Look at the screenshots.

  Modes (-Mode):
    local           (default) the GF-Test local world: the integrated server runs the mod too.
    remote-mod      starts a BDS slot with the server build (tools/e2e/server, scenario
                    client-host), joins it through the Servers tab; the client runs the mod.
    remote-vanilla  same BDS, but our client mod is moved aside: a vanilla client (plus
                    LeviLamina) that only gets the server's resource pack: what players without the
                    client mod see.
  In remote modes the BDS console log is merged into the session log ("[host] " lines) and
  Send-HostConsole runs server console commands. Before the host boots, it runs the optional
  scenarios/<name>.host-prelaunch.ps1 in the server harness (e.g. a probe pack for the host's mod;
  server/scenarios/client-host.prelaunch.ps1); scenarios/<name>.prelaunch.ps1 prepares the client.

.EXAMPLE
  pwsh tools/e2e/client/run.ps1 -Scenario smoke -Build
  pwsh tools/e2e/client/run.ps1 -Scenario smoke -Mode remote-mod
  pwsh tools/e2e/client/run.ps1 -Scenario smoke -KeepOpen   # play around; close the game to finish
#>
param(
    [Parameter(Mandatory)] [string]$Scenario,
    [switch]$Build,
    [switch]$KeepOpen,          # leave the game running; the harness waits until you close it, then cleans up
    [switch]$KeepOtherMods,     # do not move other mods (BEI, ...) aside
    [string]$ModDir,            # default: <repo>/bin/client/GloamingForest
    [ValidateSet('local', 'remote-mod', 'remote-vanilla')][string]$Mode = 'local',
    [int]$LockTimeoutMinutes = 120,  # give up (exit 2, "NOT RUN") if the game lock is not free by then; 0 = wait forever
    [int]$SlotWaitMinutes = 3,       # remote modes: no server slot by then -> release the game lock and queue again
    [int]$HostTimeoutMinutes = 15,   # remote modes: max wait for the BDS host (slot + boot), game lock held
    [switch]$HeadlessTrace,          # BeDD trace mode: log every key/mouse event reaching the game's input layer
    [switch]$Foreground,             # real SendInput, game in the foreground (takes over the PC); slot 1 only
    [switch]$StrictContentLog,       # content-log warnings fail the run too (a scenario: $script:ContentLogStrict = $true)
    # Game instances this run may use (headless only); the first free one is taken. Default:
    # $env:LEVI_E2E_SLOTS ("1,2,3") or 1. Slots 2.. are clones (versions\<base>-hi<N>) made on first
    # use, so several runs can play at once.
    [int[]]$Slot
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

if (-not $Foreground) {
    $shim = Join-Path $script:BeDDDir 'harness-shim.ps1'
    if (-not (Test-Path $shim) -and -not $env:BEDD_DIR) { $script:BeDDDir = Install-BeDDRelease; $shim = Join-Path $script:BeDDDir 'harness-shim.ps1' }
    if (-not (Test-Path $shim)) {
        throw "BeDD is not available ($shim missing). Set `$env:BEDD_DIR to a BeDD build, or pass -Foreground to use the real mouse and keyboard."
    }
    # Replaces Click/Press-Key/Type-Text/Scroll/Move-Mouse/Focus-Game/Assert-GameForeground/
    # Set-GameWindowSize and routes [BsE2E.Native]::Key/MouseButton/... (used by scenarios)
    # through the mod's pipe.
    . $shim
    Register-BeDDNative ([BsE2E.Native])
}
[GfE2E.Watchdog]::Reset()
if ($script:Headless) { Register-PipeWatchdog }
$script:ContentLogStrict = [bool]$StrictContentLog
$script:ContentLogWarnings = @()
$script:GamesStarted = 0
$script:GameKilled = $false
$script:NotRunReason = $null
# Game instances: -Slot, $env:GF_E2E_CLIENT_SLOTS / $env:LEVI_E2E_SLOTS ("1,2,3"), or every instance
# that exists (the base and its -hi<N> clones); the first free one is taken.
$slotList = if ($env:GF_E2E_CLIENT_SLOTS) { $env:GF_E2E_CLIENT_SLOTS } else { $env:LEVI_E2E_SLOTS }
$slots = if ($Foreground) { @(1) } elseif ($Slot) { $Slot } elseif ($slotList) {
    @($slotList -split '[,; ]+' | Where-Object { $_ } | ForEach-Object { [int]$_ }) } else {
    @(1) + @(Get-ChildItem (Join-Path $script:LauncherData 'versions') -Directory -Filter "$($script:BaseVersion)-hi*" -ErrorAction SilentlyContinue |
        ForEach-Object { if ($_.Name -match '-hi(\d+)$') { [int]$Matches[1] } } | Sort-Object) }

$scenarioFile = Join-Path $PSScriptRoot "scenarios/$Scenario.ps1"
if (-not (Test-Path $scenarioFile)) { throw "Unknown scenario: $scenarioFile" }
if (-not $ModDir) { $ModDir = Join-Path $script:RepoRoot "bin/client/$script:ModName" }
$script:Unit = $Scenario
# The output folder is fixed once the game lock (and so the instance slot) is ours: slot 1 keeps
# out/<scenario>, slots 2.. use out/<scenario>-slot<N> so parallel runs of one scenario don't collide.
$outDir = Join-Path $script:OutRoot $Scenario
function Set-RunOutDir {
    $script:Unit = if ($script:InstanceSlot -gt 1) { "$Scenario-slot$($script:InstanceSlot)" } else { $Scenario }
    $dir = Join-Path $script:OutRoot $script:Unit
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force $dir | Out-Null
    return $dir
}

$failures = [System.Collections.Generic.List[string]]::new()
# The failures a hang cannot explain (ERR lines, failing selftests, content-log errors): they fail
# the run even when it ends in a hang.
$hardFailures = [System.Collections.Generic.List[string]]::new()
$script:TestHang = $false
$script:Mode   = $Mode
$remote        = $Mode -ne 'local'
$hostInfo      = $null
$lockTimeout   = $null   # set when the run could not start: "NOT RUN", exit 2
$lockAcquired  = $false
$script:HostDir = $null

# Starts the BDS host (server harness, scenario client-host) in a free server slot and waits until
# it is ready. Called with the game lock held, so a queued run does not hold a slot meanwhile.
# Returns $null (host killed) when no slot became free within -SlotWaitMinutes: the caller then
# gives the game lock back instead of blocking the game while every slot is busy.
function Start-RemoteHost {
    $script:HostDir = Join-Path $outDir 'host'
    New-Item -ItemType Directory -Force $script:HostDir | Out-Null
    $env:GF_E2E_HOST_DIR = $script:HostDir
    $env:GF_E2E_HOST_OWNER_PID = "$PID"
    # the host runs scenarios/<scenario>.host-prelaunch.ps1 before it boots (server/scenarios/client-host.prelaunch.ps1)
    $env:GF_E2E_HOST_SCENARIO = $Scenario
    $env:GF_E2E_HOST_MODE = $Mode
    $serverRun = Join-Path $PSScriptRoot '../server/run.ps1'
    $harnessLog = Join-Path $outDir 'host-harness.log'
    # Own server output folder per client run (out/client-host-<unit>), so parallel remote runs don't collide.
    $script:HostOutName = "client-host-$script:Unit"
    # CreateNoWindow, not Start-Process -WindowStyle Hidden: Start-Process gives the host a new console,
    # and with Windows Terminal as the default terminal that console opens as a visible window.
    $psi = [System.Diagnostics.ProcessStartInfo]::new('pwsh')
    foreach ($a in @('-NoProfile', '-File', $serverRun, '-Scenario', 'client-host', '-OutName', $script:HostOutName, '-Quiet', '-TimeoutSec', '7200')) { $psi.ArgumentList.Add($a) }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $script:HostProc = [System.Diagnostics.Process]::Start($psi)
    # Copied straight into the files (unbuffered, readable meanwhile): the wait below reads the log.
    foreach ($s in @(@($script:HostProc.StandardOutput, $harnessLog), @($script:HostProc.StandardError, (Join-Path $outDir 'host-harness.err')))) {
        $null = $s[0].BaseStream.CopyToAsync([IO.FileStream]::new($s[1], 'Create', 'Write', 'ReadWrite', 1))
    }
    Write-Step "starting the BDS host (pid $($script:HostProc.Id))"
    $ready = Join-Path $script:HostDir 'ready.json'
    $start = Get-Date
    $deadline = $start.AddMinutes($HostTimeoutMinutes)
    $slotDeadline = $start.AddMinutes($SlotWaitMinutes)
    $nextReport = $start.AddSeconds(60)
    while (-not (Test-Path $ready)) {
        if ($script:HostProc.HasExited) { throw "the BDS host exited early (see $harnessLog)" }
        if ((Get-Date) -gt $slotDeadline -and -not (Select-String -Path $harnessLog -Pattern 'acquired server slot' -Quiet -ErrorAction SilentlyContinue)) {
            Write-Step "no free server slot within $SlotWaitMinutes min; stopping the host"
            $script:HostProc.Kill($true); $script:HostProc.WaitForExit(15000) | Out-Null
            $script:HostProc = $null
            return $null
        }
        if ((Get-Date) -gt $deadline) { throw "the BDS host did not get ready in $HostTimeoutMinutes min (see $harnessLog)" }
        if ((Get-Date) -ge $nextReport) {
            $last = Get-Content $harnessLog -Tail 1 -ErrorAction SilentlyContinue
            Write-Step ("waiting for the BDS host ({0:N1} min): {1}" -f ((Get-Date) - $start).TotalMinutes, $last)
            $nextReport = (Get-Date).AddSeconds(60)
        }
        Start-Sleep -Seconds 2
    }
    $info = Get-Content $ready -Raw | ConvertFrom-Json
    if (-not $info.port) { throw "bad $ready" }
    $script:HostLog = Join-Path $PSScriptRoot "../server/out/$script:HostOutName/server.log"
    Write-Step "BDS host ready on port $($info.port) (slot $($info.slot))"
    return $info
}

# Asks the host to stop (it shuts the server down and releases its slot); does not wait.
function Request-HostStop {
    if ($script:HostProc -and $script:HostDir -and -not $script:HostProc.HasExited) {
        Set-Content -Path (Join-Path $script:HostDir 'stop') -Value 'stop'
    }
}

$script:HostProc = $null
$script:HostSlotTimedOut = $false
try {
    if ($Build) { Invoke-ModBuild client; if ($remote) { Invoke-ModBuild server } }
    # One of the throttle's two client slots for the whole run (tools/lib/Throttle.psm1), then a
    # game instance lock shared with Levitate's harness (bei-e2e.lock / hi<N>-e2e.lock).
    try { $script:ClientPoolSlot = Enter-GfSlot -Pool client -TimeoutMinutes $LockTimeoutMinutes -Unit $Scenario }
    catch [System.TimeoutException] { $script:LockTimedOut = $true; throw }
    # Game lock first, then the BDS host: a run queued for the game holds no server slot. If no
    # slot is free, give the game lock back and queue again (until -LockTimeoutMinutes).
    $lockDeadline = if ($LockTimeoutMinutes -gt 0) { (Get-Date).AddMinutes($LockTimeoutMinutes) } else { [datetime]::MaxValue }
    while ($true) {
        $left = if ($LockTimeoutMinutes -gt 0) { [Math]::Max(1, [int][Math]::Ceiling(($lockDeadline - (Get-Date)).TotalMinutes)) } else { 0 }
        Enter-E2ELock -TimeoutMinutes $left -Slots $slots
        $outDir = Set-RunOutDir
        [GfE2E.Watchdog]::DumpDir = $outDir   # a hung game's minidump (lib.ps1 WriteDump)
        if (-not $remote) { break }
        try { $hostInfo = Start-RemoteHost } catch { Exit-E2ELock; throw }
        if ($hostInfo) { break }
        Exit-E2ELock
        if ((Get-Date) -gt $lockDeadline) { $script:HostSlotTimedOut = $true; throw "no free server slot whenever the game lock was ours (within $LockTimeoutMinutes min)" }
        Write-Step "released the game lock; queueing again in 30 s"
        Start-Sleep -Seconds 30
    }
    $lockAcquired = $true
    try {
        # A clone instance is made on first use (cheap: hard links); it keeps its own mods and data.
        if ($script:InstanceSlot -gt 1) { & (Join-Path $script:BeDDDir 'clone-instance.ps1') -Slot $script:InstanceSlot -Source $script:BaseVersion }
        Clear-InstanceGames         # also a window-less game that is still exiting (it holds the DLLs)
        Restore-OtherMods
        Remove-StaleModCopies       # our mod parked by another harness (mods.<harness>-aside)
        if ($Mode -eq 'remote-vanilla') {
            Remove-DeployedMod # vanilla client: only LeviLamina (+ CrashLogger)
        } else {
            Deploy-Mod $ModDir
        }
        if (-not $KeepOtherMods) { Move-OtherModsAside }
        Remove-BeDDLeftover   # of a killed run
        if ($script:Headless) { Install-BeDDMod -ModsDir $script:ModsDir -PipeName $script:BeDDPipe -Trace:$HeadlessTrace }
        if ($remote) { Set-ExternalServer $hostInfo.port } else {
            Restore-TestWorld
            # Single-player test world: games of all slots share one signaling-service ID, and
            # worlds opened for multiplayer knock each other out ("An error has occurred", "Blaze").
            if ($script:Headless) { Set-WorldSinglePlayer (Join-Path (Get-WorldsDir) $script:WorldName) }
        }
        Reset-GameOptions           # HUD, hand, camera, graphics mode as every run expects them
        Enable-ContentLog           # checked after the game closed (Get-ContentLogProblems)
        Clear-CachedGfPacks         # never a stale pack cached by another build
        $prelaunch = Join-Path $PSScriptRoot "scenarios/$Scenario.prelaunch.ps1"
        if (Test-Path $prelaunch) { . $prelaunch }
        try {
            # Joining the local world sometimes fails with "Connection timed out (Kelp)" and,
            # once it does, keeps failing in that game session: retry with a fresh game.
            # A game in another world than GF-Test (Enter-TestWorld) gets one more fresh game; the
            # second time the run is NOT RUN (nothing of the scenario ran there).
            $wrongWorlds = 0
            for ($launch = 1; ; $launch++) {
                if ($remote) { Clear-OurServerEntries }
                Start-Game | Out-Null
                $script:GamesStarted++
                Wait-GameReady -WithoutMod:($Mode -eq 'remote-vanilla')
                try {
                    if ($remote) { Enter-RemoteServer -Attempts 1 } else { Enter-TestWorld -Attempts 2 }
                    break
                } catch {
                    if ($_.Exception -is [GfE2E.WrongWorldException]) {
                        $wrongWorlds++
                        if ($wrongWorlds -ge 2) {
                            $script:NotRunReason = "the game was not in the scenario's $($script:WorldName) world, twice: $($_.Exception.Message)"
                            throw
                        }
                        Write-Step "WRONG WORLD: $($_.Exception.Message); restarting the game with a fresh $($script:WorldName) (once)"
                        Stop-Game
                        Restore-TestWorld
                        if ($script:Headless) { Set-WorldSinglePlayer (Join-Path (Get-WorldsDir) $script:WorldName) }
                        Start-Sleep -Seconds 3
                        continue
                    }
                    if (Get-WatchdogFired) { throw }
                    if ($launch -ge 3) { throw }
                    Write-Step "could not enter the world ($_); restarting the game (launch $($launch + 1)/3)"
                    Stop-Game
                    Start-Sleep -Seconds 3
                }
            }
            Wait-HudReady
            Invoke-TestHang 'pipe'      # $env:GF_E2E_TEST_HANG=pipe only (lib.ps1)
            Write-Step "running scenario '$Scenario'"
            . $scenarioFile
        } catch {
            # the scenario's own Enter-TestWorld (after a restart, e.g. ui's Vibrant Visuals half)
            if ($_.Exception -is [GfE2E.WrongWorldException] -and -not $script:NotRunReason) {
                $script:NotRunReason = "the game was not in the scenario's $($script:WorldName) world after the scenario restarted it: $($_.Exception.Message)"
            }
            if ($script:NotRunReason -or (Get-WatchdogFired)) {
                Write-Step "scenario stopped: $_"
            } else {
                $failures.Add("scenario: $_")
                Write-Host "[e2e] $($_.ScriptStackTrace)" -ForegroundColor DarkGray
                try { Save-Screenshot 'failure' | Out-Null } catch {}
            }
            Get-LeviLog | Select-Object -Last 40 | ForEach-Object { Write-Host "  | $_" }
        }
        $errs = Get-ErrorLines
        if ($errs) {
            $f = "$($errs.Count) ERR/FTL line(s) in the log (docs/spec/e2e.md, Error lines)"
            $failures.Add($f); $hardFailures.Add($f)
            $errs | Select-Object -First 20 | ForEach-Object { Write-Host "  ! $_" -ForegroundColor Red }
        }
        foreach ($p in Get-SelfTestProblems) { $failures.Add($p) }
        # failing tests (not the unfinished ones a hang leaves) fail the run even when it hung later
        foreach ($r in @($script:SelfTestRuns | Where-Object { @($_.Failed).Count -gt 0 })) {
            $hardFailures.Add("selftest '$($r.Glob)': $(@($r.Failed).Count) failing: $((@($r.Failed) | ForEach-Object Name) -join ', ')")
        }
        Save-SelfTestReport (Join-Path $outDir 'selftest.json')
        Get-LeviLog | Set-Content -Path (Join-Path $outDir 'client.log') -Encoding UTF8
    } finally {
        if ($KeepOpen -and (Get-GameProcess)) {
            Write-Step "-KeepOpen: the game stays open; close it to finish the run (lock still held)"
            if ($script:Headless) {
                # Hand the game to the human: real mouse and keyboard work again.
                try { Set-BeDDBackground $false | Out-Null; Write-Step "headless input off: the game takes real input again" } catch {}
            }
            while (Get-GameProcess) { Start-Sleep -Seconds 2 }
        }
        try {
            Stop-Game
            Request-HostStop # the host shuts down while we clean up
            # the game's content log of every session of the run (written as the game closed)
            # (the sessions that closed normally: a killed game writes none)
            try { foreach ($p in Get-ContentLogProblems -OutDir $outDir -Strict:([bool]$script:ContentLogStrict)) { $failures.Add($p); $hardFailures.Add($p) } }
            catch { $failures.Add("content log: $_") }
            if ($remote) { Restore-ExternalServers } else { Remove-TestWorld }
            Remove-DeployedMod
            Remove-BeDDLeftover
            Restore-OtherMods
            $restore = Join-Path $PSScriptRoot "scenarios/$Scenario.restore.ps1"
            # Dot-sourced (like the prelaunch script) so it sees this run's slot ($script:VersionName,
            # Get-GameDataDir, ...); a failing restore must not keep the lock.
            if (Test-Path $restore) { try { . $restore } catch { Write-Host "[e2e] restore script failed: $_" -ForegroundColor Red } }
        } finally {
            # after the scenario's own restore (models turns the log on itself), and also when a
            # cleanup step above failed: the instance's own option values (HUD, hand, camera,
            # graphics mode, content log) as they were before the run
            try { Restore-RunOptions } catch { Write-Host "[e2e] could not put the game options back: $_" -ForegroundColor Red }
            Exit-E2ELock
        }
    }
} catch {
    if (($script:LockTimedOut -and -not $lockAcquired) -or $script:HostSlotTimedOut) {
        $lockTimeout = "$_"
    } else {
        $failures.Add("harness: $_")
        Write-Host "[e2e] $($_.ScriptStackTrace)" -ForegroundColor DarkGray
    }
} finally {
    # Runs on success, failure, timeout and Ctrl+C: never leave the host (and its slot) behind.
    if ($script:HostProc) {
        $hp = $script:HostProc
        if (-not $hostInfo) {
            # Not ready yet (still waiting for a slot or booting): nobody needs it, kill it now.
            if (-not $hp.HasExited) { Write-Step "killing the BDS host that never got ready"; $hp.Kill($true) }
        } else {
            Request-HostStop
            if (-not $hp.WaitForExit(120000)) { $hp.Kill($true); $failures.Add('BDS host did not stop') }
            elseif ($hp.ExitCode -ne 0) { $failures.Add("BDS host failed (exit $($hp.ExitCode), see tools/e2e/server/out/$script:HostOutName/)") }
        }
        Remove-Item Env:GF_E2E_HOST_DIR, Env:GF_E2E_HOST_OWNER_PID, Env:GF_E2E_HOST_SCENARIO, Env:GF_E2E_HOST_MODE -ErrorAction SilentlyContinue
    }
    Exit-GfSlot $script:ClientPoolSlot
}

if ($lockTimeout) {
    Write-Host "[e2e] client scenario '$Scenario' NOT RUN (game lock or server slot not free in time; nothing was tested)" -ForegroundColor Yellow
    Write-Host "[e2e]   $lockTimeout" -ForegroundColor Yellow
    exit 2
}

# The run could not test the scenario: a game call hung (the watchdog killed the game) or the game
# was in another world than GF-Test twice. Never a pass: NOT RUN (retry), unless the run had
# already failed before (ERR lines, failing selftests, content-log errors) or the same scenario hung
# on the same build in the run before as well (a hang of the mod's own making): then it fails.
$hung = [bool](Get-WatchdogFired)
$notRun = if ($hung) { "WATCHDOG: $(Get-WatchdogFired)" } elseif ($script:NotRunReason) { $script:NotRunReason }
$dll = Join-Path $ModDir "$script:ModName.dll"
# (a run that never started a game, or a test hang, leaves the count as it is)
$hangs = if ($script:TestHang -or $script:GamesStarted -eq 0) { @{ Count = 0; Instances = @(); Fails = $false } } else { Update-HangRecord $Scenario $Mode $dll -Instance $script:VersionName -Hung:$hung }
if ($notRun) {
    if ($hung -and $Mode -ne 'remote-vanilla') {
        # what the mod logged about its module installs (which install() was still running)
        $trace = @(Get-ModEnableTrace)
        if ($trace.Count -gt 0) { Write-Host "[e2e] the mod's install trace before the hang:" -ForegroundColor Yellow; $trace | Select-Object -Last 12 | ForEach-Object { Write-Host "  | $_" } }
        elseif ($script:GamesStarted -gt 0) { Write-Host "[e2e] the mod logged no install trace: the hang came before its enable() (see client.log)" -ForegroundColor Yellow }
    }
    # only for a hang: a wrong world's own ERR lines (another world's mod state) are not this scenario's
    if ($hung -and $hardFailures.Count -gt 0) {
        Write-Host "[e2e] client scenario '$Scenario' FAILED (it also could not finish: $notRun)" -ForegroundColor Red
        $hardFailures | ForEach-Object { Write-Host "[e2e]   - $_" -ForegroundColor Red }
        foreach ($f in $failures) { if (-not $hardFailures.Contains($f)) { Write-Host "[e2e]   (after the hang, not counted: $f)" -ForegroundColor DarkYellow } }
        Write-Host "[e2e] output: $outDir"
        exit 1
    }
    if ($hangs.Fails) {
        Write-Host "[e2e] client scenario '$Scenario' FAILED: the game hung in $($hangs.Count) runs in a row of this scenario ($Mode) on the same build (instances: $($hangs.Instances -join ', '))" -ForegroundColor Red
        Write-Host "[e2e]   $notRun" -ForegroundColor Red
        Write-Host "[e2e] output: $outDir"
        exit 1
    }
    Write-Host "[e2e] client scenario '$Scenario' NOT RUN (the run could not test the scenario; retry it)" -ForegroundColor Yellow
    Write-Host "[e2e]   $notRun" -ForegroundColor Yellow
    if ($hung -and -not $script:TestHang) { Write-Host "[e2e]   (hung runs in a row of '$Scenario' ($Mode) on this build: $($hangs.Count), on $($hangs.Instances -join ', '); two on different instances, or three on one, fail)" -ForegroundColor Yellow }
    foreach ($f in $failures) { Write-Host "[e2e]   (not counted: $f)" -ForegroundColor DarkYellow }
    Write-Host "[e2e] output: $outDir"
    exit 2
}

foreach ($w in $script:ContentLogWarnings) { Write-Host "[e2e] content-log warning: $w" -ForegroundColor Yellow }
if ($failures.Count -gt 0) {
    Write-Host "[e2e] client scenario '$Scenario' FAILED:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "[e2e]   - $_" -ForegroundColor Red }
    Write-Host "[e2e] output: $outDir"
    exit 1
}
Write-Host "[e2e] client scenario '$Scenario' PASSED (screenshots: $outDir)" -ForegroundColor Green
exit 0

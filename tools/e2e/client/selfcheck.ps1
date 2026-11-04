<#
.SYNOPSIS
  Self-check of the client harness's game-free logic (no game, no lock, no slot).

.DESCRIPTION
  Checks, with temporary files only (under %TEMP%, removed afterwards):
  - content log (lib.ps1 Get-ContentLogProblems), per game session: a session that closed normally
    with no file, or with files without the canary of GloamingForest_Test_RP -> failure, even when
    another session's file holds it; a killed (or crashed) session needs no canary, for itself only;
    a remote-vanilla session needs it only once it joined the host (a failed join that was
    relaunched is waived); how a session ended (Get-GameSessionEnd: exit code 0 -> closed, a
    non-zero or unknown one or the watchdog -> gone);
    only the canary -> clean; verbose / info lines -> listed in $script:ContentLogNotes, never a
    failure; errors -> failure; warnings -> listed, a failure only with -Strict; the canary file in
    assets/test parses as JSON and names the canary;
  - game options (lib.ps1 Reset-GameOptions, Restore-RunOptions, on a temporary options.txt): the HUD
    and hand are shown during a run, a person's own hidden HUD comes back afterwards, and after a
    run killed while its scenario hid the HUD the next run leaves it shown;
  - output folder (lib.ps1 Clear-RunOutDir): a file held open by another process is retried and
    cleared once released; one that stays open throws GfE2E.OutDirLockedException (run.ps1: NOT RUN)
    and leaves every file of the folder in place;
  - host harness teardown (lib.ps1 Start-RedirectedOutput, Stop-ProcessTree, Close-RedirectedOutput):
    a pwsh child whose redirected output goes into files and that started a grandchild of its own is
    killed with its tree, and its folder can be cleared at once (the remote-vanilla requeue).
  Exit code 0 = every check passed, 1 = a check failed.

.EXAMPLE
  pwsh -NoProfile -File tools/e2e/client/selfcheck.ps1
#>
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$failed = [System.Collections.Generic.List[string]]::new()
function Assert-True([bool]$Cond, [string]$What) {
    if ($Cond) { Write-Host "  ok    $What" -ForegroundColor Green }
    else { Write-Host "  FAIL  $What" -ForegroundColor Red; $failed.Add($What) }
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) "gf-e2e-selfcheck-$PID"
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    # ---- content log ----
    Write-Host '[selfcheck] content log'
    $canaryFile = Join-Path $script:RepoRoot 'assets/test/rp/animations/gf_e2e_content_log_canary.animation.json'
    $canaryJson = Get-Content $canaryFile -Raw | ConvertFrom-Json -AsHashtable
    Assert-True ($canaryJson.animations.Contains("animation.$script:ContentLogCanary")) 'the canary animation in assets/test names the canary'
    Assert-True ('gfpreview_canary' -in $canaryJson.animations["animation.$script:ContentLogCanary"].bones.Keys) 'the canary bone is gfpreview_canary (models.ps1 filter)'

    $canaryLine = "12:00:01[Molang][error]-./mods/GloamingForest/resource_packs/GloamingForest_Test_RP/animations/$script:ContentLogCanary.animation.json | animations | animation.$script:ContentLogCanary | bones | gfpreview_canary | rotation | math.cos(query.life_time * | Error: Could not find Right Parenthesis ')' to close section started with Left Parenthesis '('"
    function New-Log([string]$Name, [string[]]$Lines) { $p = Join-Path $tmp $Name; Set-Content -Path $p -Value $Lines -Encoding UTF8; $p }
    $out = Join-Path $tmp 'out'

    # game sessions as Start-Game / Stop-Game record them; a file belongs to the last session that
    # started before it was created
    $t0 = (Get-Date).AddMinutes(-10)
    function New-Session([int]$Launch, [int]$StartMin, [string]$Ended, [switch]$Vanilla, [switch]$NeverJoined) {
        [pscustomobject]@{ Launch = $Launch; Start = $t0.AddMinutes($StartMin); End = $t0.AddMinutes($StartMin + 1); Ended = $Ended; Pid = 1000 + $Launch
            InWorld = -not $NeverJoined; CanaryOnJoin = [bool]$Vanilla; Process = $null }
    }
    function Set-LogTime([string]$Path, [int]$Min) { $t = $t0.AddMinutes($Min); [IO.File]::SetCreationTime($Path, $t); [IO.File]::SetLastWriteTime($Path, $t) }
    $one = @(New-Session 1 0 'closed')

    $r = @(Get-ContentLogProblems -Files @() -Sessions $one -OutDir $out)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'session 1 .*wrote no content log') 'no file: failure (logging not captured)'
    $r = @(Get-ContentLogProblems -Files @() -Sessions @(New-Session 1 0 'killed'))
    Assert-True ($r.Count -eq 0) 'no file after a killed session: not checked'
    $r = @(Get-ContentLogProblems -Files @() -Sessions @(New-Session 1 0 'gone'))
    Assert-True ($r.Count -eq 0) 'no file after a session that exited by itself (crash, watchdog): not checked'

    $clean = New-Log 'ContentLog_clean.txt' @($canaryLine, $canaryLine)
    $r = @(Get-ContentLogProblems -Files @($clean) -Sessions $one -OutDir $out)
    Assert-True ($r.Count -eq 0) 'only the canary: clean'
    Assert-True ((Get-Content (Join-Path $out 'client-content-log.txt') -Raw) -match '\(canary\)') 'client-content-log.txt marks the canary'

    $none = New-Log 'ContentLog_nocanary.txt' @('12:00:02[Sound][verbose]-No sound found for block type ''wood'' and event type ''chest.open''')
    $r = @(Get-ContentLogProblems -Files @($none) -Sessions $one)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'does not report the canary') 'no canary: failure'
    $r = @(Get-ContentLogProblems -Files @($none) -Sessions @(New-Session 1 0 'killed'))
    Assert-True ($r.Count -eq 0) 'no canary after a killed session: not counted'

    # per session: two sessions, the canary in the first one's file only
    $s1 = New-Log 'ContentLog_s1.txt' @($canaryLine); Set-LogTime $s1 1
    $s2 = New-Log 'ContentLog_s2.txt' @('12:00:02[Sound][verbose]-a verbose line'); Set-LogTime $s2 3
    $two = @((New-Session 1 0 'closed'), (New-Session 2 2 'closed'))
    $r = @(Get-ContentLogProblems -Files @($s1) -Sessions $two)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'session 2 .*wrote no content log') 'two closed sessions, a file for the first only: the second fails'
    $r = @(Get-ContentLogProblems -Files @($s1, $s2) -Sessions $two)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'session 2 .*does not report the canary') 'two closed sessions, the canary in the first file only: the second fails'
    $s2c = New-Log 'ContentLog_s2c.txt' @($canaryLine); Set-LogTime $s2c 3
    $r = @(Get-ContentLogProblems -Files @($s1, $s2c) -Sessions $two)
    Assert-True ($r.Count -eq 0) 'two closed sessions, a canary in each: clean'
    # a file the first session wrote as it closed, 1 s before the second one started, is the first one's
    $late = New-Log 'ContentLog_s1late.txt' @($canaryLine)
    $tl = $t0.AddMinutes(2).AddSeconds(-1); [IO.File]::SetCreationTime($late, $tl); [IO.File]::SetLastWriteTime($late, $tl)
    $r = @(Get-ContentLogProblems -Files @($late, $s2c) -Sessions $two)
    Assert-True ($r.Count -eq 0) 'a file written just before the next session started belongs to the session before'
    # a killed session waives the canary for itself only
    $killedFirst = @((New-Session 1 0 'killed'), (New-Session 2 2 'closed'))
    $r = @(Get-ContentLogProblems -Files @() -Sessions $killedFirst)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'session 2 ') 'a killed session, then a closed one with no file: the closed one fails'
    $r = @(Get-ContentLogProblems -Files @($s2c) -Sessions $killedFirst)
    Assert-True ($r.Count -eq 0) 'a killed session, then a closed one with the canary: clean'
    # remote-vanilla: the canary comes only with the host's packs, once the client joined
    $r = @(Get-ContentLogProblems -Files @() -Sessions @(New-Session 1 0 'closed' -Vanilla -NeverJoined))
    Assert-True ($r.Count -eq 0) 'remote-vanilla: a closed session that never joined the host, no file: not checked'
    $r = @(Get-ContentLogProblems -Files @($none) -Sessions @(New-Session 1 0 'closed' -Vanilla -NeverJoined))
    Assert-True ($r.Count -eq 0) 'remote-vanilla: a closed session that never joined the host, a file without the canary: not checked'
    $r = @(Get-ContentLogProblems -Files @() -Sessions @(New-Session 1 0 'closed' -Vanilla))
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'session 1 .*wrote no content log') 'remote-vanilla: a closed session that joined, no file: failure'
    $relaunch = @((New-Session 1 0 'closed' -Vanilla -NeverJoined), (New-Session 2 2 'closed' -Vanilla))
    $r = @(Get-ContentLogProblems -Files @($s2c) -Sessions $relaunch)
    Assert-True ($r.Count -eq 0) 'remote-vanilla: a failed join relaunched, the second session joined with the canary: clean'
    $r = @(Get-ContentLogProblems -Files @() -Sessions $relaunch)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'session 2 ') 'remote-vanilla: a failed join relaunched, the joined session wrote nothing: it fails'
    $r = @(Get-ContentLogProblems -Files @() -Sessions @(New-Session 1 0 'closed' -NeverJoined))
    Assert-True ($r.Count -eq 1) 'local / remote-mod: a closed session that never reached the world still needs the canary (the global test pack logs it at start)'

    # how a session ended (Stop-Game, Get-GameSessionEnd)
    $exit0 = Start-Process pwsh -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command', 'exit 0'); [void]$exit0.Handle
    $exit1 = Start-Process pwsh -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command', 'exit 3'); [void]$exit1.Handle
    $exit0.WaitForExit(30000) | Out-Null; $exit1.WaitForExit(30000) | Out-Null
    Assert-True ((Get-GameSessionEnd -Killed $true -Running 1 -Process $null -Watchdog $false) -eq 'killed') 'session end: killed by Stop-Game -> killed'
    Assert-True ((Get-GameSessionEnd -Killed $false -Running 1 -Process $null -Watchdog $false) -eq 'closed') 'session end: closed by Stop-Game -> closed'
    Assert-True ((Get-GameSessionEnd -Killed $false -Running 0 -Process $exit0 -Watchdog $false) -eq 'closed') 'session end: exited by itself with code 0 (closed from its menu) -> closed (canary required)'
    Assert-True ((Get-GameSessionEnd -Killed $false -Running 0 -Process $exit1 -Watchdog $false) -eq 'gone') 'session end: exited by itself with a non-zero code (crash) -> gone'
    Assert-True ((Get-GameSessionEnd -Killed $false -Running 0 -Process $null -Watchdog $false) -eq 'gone') 'session end: exited by itself, exit code unknown -> gone'
    Assert-True ((Get-GameSessionEnd -Killed $false -Running 0 -Process $exit0 -Watchdog $true) -eq 'gone') 'session end: the watchdog fired -> gone'

    $verbose = New-Log 'ContentLog_verbose.txt' @($canaryLine,
        '12:00:02[Sound][verbose]-No sound found for block type ''wood'' and event type ''chest.open''',
        '12:00:03[Sound][verbose]-No sound found for block type ''wood'' and event type ''chest.open''',
        '12:00:04[Pack][info]-Some informational line')
    $r = @(Get-ContentLogProblems -Files @($verbose) -Sessions $one)
    Assert-True ($r.Count -eq 0) 'verbose / info lines: no failure'
    Assert-True ($script:ContentLogNotes.Count -eq 2 -and $script:ContentLogNotes[0] -match '\(x2\)$') 'verbose / info lines: listed with their count'

    $err = New-Log 'ContentLog_error.txt' @($canaryLine, '12:00:05[Item][error]-Overwriting pattern template. Duplicate entries', '12:00:06[Json][warning]-some warning')
    $r = @(Get-ContentLogProblems -Files @($err) -Sessions $one)
    Assert-True ($r.Count -eq 1 -and $r[0] -match '1 content-log error') 'an error: failure'
    Assert-True ($script:ContentLogWarnings.Count -eq 1) 'a warning: listed'
    $warn = New-Log 'ContentLog_warning.txt' @($canaryLine, '12:00:06[Json][warning]-some warning')
    $r = @(Get-ContentLogProblems -Files @($warn) -Sessions $one)
    Assert-True ($r.Count -eq 0) 'a warning: no failure in a normal run'
    $r = @(Get-ContentLogProblems -Files @($warn) -Sessions $one -Strict)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'strict') 'a warning: failure in a strict run'

    $noise = New-Log 'ContentLog_noise.txt' @($canaryLine, '12:00:07[Fog][error]-fogs/diving_helmet.json | bad')
    $r = @(Get-ContentLogProblems -Files @($noise) -Sessions $one)
    Assert-True ($r.Count -eq 0) 'expected entries ($script:ContentLogExpected): no failure'

    # ---- game options: the HUD safety net keeps a person's own values ----
    Write-Host '[selfcheck] game options'
    $optFile = Join-Path $tmp 'minecraftpe\options.txt'
    $optBackup = Join-Path $tmp 'game-options.selfcheck.json'
    New-Item -ItemType Directory -Force (Split-Path $optFile) | Out-Null
    # (never an instance's real options or backup: the files, the backup and the e2e home are ours)
    function Get-OptionsFiles { @(Get-Item -LiteralPath $optFile) }
    function Get-OptionsBackupPath { $optBackup }
    function Get-E2EHome { $tmp }
    $script:VersionName = 'selfcheck'
    function Get-Opt([string]$Key) { ((Get-Content $optFile) | Where-Object { $_ -match "^${Key}:" } | Select-Object -First 1) -replace "^${Key}:", '' }
    # a person's own hidden HUD and hand: 0 while the run plays, theirs again afterwards
    Set-Content $optFile @('gfx_hidehud:1', 'gfx_hidehand:1', 'graphics_mode:2', 'game_thirdperson:0')
    Reset-GameOptions
    Assert-True ((Get-Opt gfx_hidehud) -eq '0' -and (Get-Opt gfx_hidehand) -eq '0') 'during a run: HUD and hand shown'
    Set-Content $optFile @((Get-Content $optFile) -replace '^gfx_hidehud:.*$', 'gfx_hidehud:1')   # the scenario pressed F1
    Restore-RunOptions
    Assert-True ((Get-Opt gfx_hidehud) -eq '1' -and (Get-Opt gfx_hidehand) -eq '1' -and -not (Test-Path $optBackup)) 'after a run: the person''s own hidden HUD and hand are back'
    # a run killed after its scenario hid the HUD: the next run shows it again and keeps it shown
    Set-Content $optFile @('gfx_hidehud:0', 'gfx_hidehand:0', 'graphics_mode:2', 'game_thirdperson:0')
    Reset-GameOptions
    Set-Content $optFile @((Get-Content $optFile) -replace '^gfx_hidehud:.*$', 'gfx_hidehud:1')   # F1, then the run is killed
    $script:OptionsBackup = $null; $script:OptionsSet = $null                                       # (a new harness process)
    Assert-True (Test-Path $optBackup) 'a killed run leaves its options backup'
    Reset-GameOptions
    Assert-True ((Get-Opt gfx_hidehud) -eq '0') 'the run after a killed one: HUD shown'
    Restore-RunOptions
    Assert-True ((Get-Opt gfx_hidehud) -eq '0' -and (Get-Opt gfx_hidehand) -eq '0') 'the run after a killed one: HUD and hand stay shown afterwards (the killed run''s F1 is not taken for the person''s choice)'

    # ---- output folder ----
    Write-Host '[selfcheck] output folder'
    $dir = Join-Path $tmp 'run-out'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $held = Join-Path $dir 'host-harness.err'
    Set-Content -Path $held -Value 'x'
    # another process holds the file (no delete sharing) for 3 s, then exits
    $holder = Start-Process pwsh -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command',
        "`$f = [IO.File]::Open('$held', 'Open', 'Read', 'Read'); Start-Sleep -Seconds 3; `$f.Dispose()")
    Start-Sleep -Milliseconds 1500
    $t0 = Get-Date
    $ok = $true
    try { Clear-RunOutDir $dir -TimeoutSec 20 } catch { $ok = $false; Write-Host "    $_" }
    Assert-True ($ok -and -not (Test-Path $dir)) ("a file held for a few seconds: cleared after the retries ({0:N1} s)" -f ((Get-Date) - $t0).TotalSeconds)
    $holder.WaitForExit(10000) | Out-Null

    New-Item -ItemType Directory -Force $dir | Out-Null
    Set-Content -Path $held -Value 'x'
    $evidence = @('client.log', 'client-content-log.txt', 'shot-hud.png') | ForEach-Object { $p = Join-Path $dir $_; Set-Content -Path $p -Value 'evidence'; $p }
    $fs = [IO.File]::Open($held, 'Open', 'Read', 'Read')
    $kind = $null
    try { Clear-RunOutDir $dir -TimeoutSec 2 } catch { $kind = $_.Exception.GetType().FullName }
    $fs.Dispose()
    Assert-True ($kind -eq 'GfE2E.OutDirLockedException') 'a file that stays in use: OutDirLockedException (NOT RUN)'
    Assert-True (-not @($evidence | Where-Object { -not (Test-Path $_) })) 'a folder that could not be cleared keeps every file of the previous run'
    Assert-True (-not @(Get-ChildItem $tmp -Directory -Filter 'run-out.trash-*')) 'no renamed copy is left behind'
    $ok = $true
    try { Clear-RunOutDir $dir -TimeoutSec 2 } catch { $ok = $false; Write-Host "    $_" }
    Assert-True ($ok -and -not (Test-Path $dir)) 'released: the folder clears'

    # ---- host harness teardown ----
    Write-Host '[selfcheck] host harness teardown'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $psi = [System.Diagnostics.ProcessStartInfo]::new('pwsh')
    # a harness-like child: writes to both streams and starts a long-lived grandchild that inherits nothing
    foreach ($a in @('-NoProfile', '-Command', "Write-Output 'host started'; [Console]::Error.WriteLine('host stderr'); Start-Process pwsh -WindowStyle Hidden -ArgumentList '-NoProfile','-Command','Start-Sleep -Seconds 120'; Start-Sleep -Seconds 120")) { $psi.ArgumentList.Add($a) }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    $outputs = @(Start-RedirectedOutput $proc (Join-Path $dir 'host-harness.log') (Join-Path $dir 'host-harness.err'))
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline -and @(Get-ProcessDescendants $proc.Id).Count -lt 1) { Start-Sleep -Milliseconds 300 }
    $kids = @(Get-ProcessDescendants $proc.Id)
    Assert-True ($kids.Count -ge 1) "the child started a grandchild ($($kids.Count) descendant(s))"
    $stopped = Stop-ProcessTree $proc -TimeoutSec 30
    Close-RedirectedOutput $outputs
    Assert-True ($stopped -and $proc.HasExited -and -not @($kids | Where-Object { -not $_.HasExited })) 'the whole tree exited'
    Assert-True ((Get-Content (Join-Path $dir 'host-harness.log') -Raw) -match 'host started') 'the redirected output reached its file'
    $ok = $true
    try { Clear-RunOutDir $dir -TimeoutSec 1 } catch { $ok = $false; Write-Host "    $_" }
    Assert-True ($ok -and -not (Test-Path $dir)) 'the folder clears at once after the teardown'
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}

if ($failed.Count -gt 0) { Write-Host "[selfcheck] $($failed.Count) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host '[selfcheck] every check passed' -ForegroundColor Green
exit 0

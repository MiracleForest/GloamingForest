<#
.SYNOPSIS
  Self-check of the client harness's game-free logic (no game, no lock, no slot).

.DESCRIPTION
  Checks, with temporary files only (under %TEMP%, removed afterwards):
  - content log (lib.ps1 Get-ContentLogProblems): no file -> failure (or "not checked" after a
    killed session); only the canary of GloamingForest_Test_RP -> clean; no canary -> failure;
    verbose / info lines -> listed in $script:ContentLogNotes, never a failure; errors -> failure;
    warnings -> listed, a failure only with -Strict; the canary file in assets/test parses as JSON
    and names the canary;
  - output folder (lib.ps1 Clear-RunOutDir): a file held open by another process is retried and
    cleared once released; one that stays open throws GfE2E.OutDirLockedException (run.ps1: NOT RUN);
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

    $r = @(Get-ContentLogProblems -Files @() -OutDir $out)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'no content log') 'no file: failure (logging not captured)'
    $r = @(Get-ContentLogProblems -Files @() -Killed)
    Assert-True ($r.Count -eq 0) 'no file after a killed session: not checked'

    $clean = New-Log 'ContentLog_clean.txt' @($canaryLine, $canaryLine)
    $r = @(Get-ContentLogProblems -Files @($clean) -OutDir $out)
    Assert-True ($r.Count -eq 0) 'only the canary: clean'
    Assert-True ((Get-Content (Join-Path $out 'client-content-log.txt') -Raw) -match '\(canary\)') 'client-content-log.txt marks the canary'

    $none = New-Log 'ContentLog_nocanary.txt' @('12:00:02[Sound][verbose]-No sound found for block type ''wood'' and event type ''chest.open''')
    $r = @(Get-ContentLogProblems -Files @($none))
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'does not report the canary') 'no canary: failure'
    $r = @(Get-ContentLogProblems -Files @($none) -Killed)
    Assert-True ($r.Count -eq 0) 'no canary after a killed session: not counted'

    $verbose = New-Log 'ContentLog_verbose.txt' @($canaryLine,
        '12:00:02[Sound][verbose]-No sound found for block type ''wood'' and event type ''chest.open''',
        '12:00:03[Sound][verbose]-No sound found for block type ''wood'' and event type ''chest.open''',
        '12:00:04[Pack][info]-Some informational line')
    $r = @(Get-ContentLogProblems -Files @($verbose))
    Assert-True ($r.Count -eq 0) 'verbose / info lines: no failure'
    Assert-True ($script:ContentLogNotes.Count -eq 2 -and $script:ContentLogNotes[0] -match '\(x2\)$') 'verbose / info lines: listed with their count'

    $err = New-Log 'ContentLog_error.txt' @($canaryLine, '12:00:05[Item][error]-Overwriting pattern template. Duplicate entries', '12:00:06[Json][warning]-some warning')
    $r = @(Get-ContentLogProblems -Files @($err))
    Assert-True ($r.Count -eq 1 -and $r[0] -match '1 content-log error') 'an error: failure'
    Assert-True ($script:ContentLogWarnings.Count -eq 1) 'a warning: listed'
    $warn = New-Log 'ContentLog_warning.txt' @($canaryLine, '12:00:06[Json][warning]-some warning')
    $r = @(Get-ContentLogProblems -Files @($warn))
    Assert-True ($r.Count -eq 0) 'a warning: no failure in a normal run'
    $r = @(Get-ContentLogProblems -Files @($warn) -Strict)
    Assert-True ($r.Count -eq 1 -and $r[0] -match 'strict') 'a warning: failure in a strict run'

    $noise = New-Log 'ContentLog_noise.txt' @($canaryLine, '12:00:07[Fog][error]-fogs/diving_helmet.json | bad')
    $r = @(Get-ContentLogProblems -Files @($noise))
    Assert-True ($r.Count -eq 0) 'expected entries ($script:ContentLogExpected): no failure'

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
    $fs = [IO.File]::Open($held, 'Open', 'Read', 'Read')
    $kind = $null
    try { Clear-RunOutDir $dir -TimeoutSec 2 } catch { $kind = $_.Exception.GetType().FullName }
    $fs.Dispose()
    Assert-True ($kind -eq 'GfE2E.OutDirLockedException') 'a file that stays in use: OutDirLockedException (NOT RUN)'

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

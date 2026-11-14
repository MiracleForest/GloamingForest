<#
.SYNOPSIS
  Client acceptance run: every unit's client scenario in sequence, with a markdown summary.

.DESCRIPTION
  Runs tools/e2e/client/run.ps1 once per scenario and mode, one after the other (each run takes the
  game lock and, in remote modes, a server slot itself, rule R23). A run that exits 2 (NOT RUN: no
  game lock, server slot or output folder in time, a hung game) is retried up to -MaxAttempts times
  after -RetryDelaySec; it never counts as a pass. Exit 1 is a failure and is not retried.

  Scenarios: every tools/e2e/client/scenarios/<name>.ps1 whose name has no dot (the unit
  scenarios; <name>.prelaunch / .restore / .host-prelaunch are a scenario's hooks). -Variants adds
  the dotted extra scenarios (biomes.classic, ui.graphics, woodwork.display, ...). -Scenario
  narrows the list (names or wildcards), -Exclude drops some.

  Output: tools/e2e/client/out/run-all/<yyyyMMdd-HHmmss>/
    summary.md          one row per scenario and mode: result, attempts, time, selftests,
                        content-log warnings, screenshot folder and count, the first failure
    <scenario>[-<mode>]-<attempt>.log   the full harness output of each attempt
  and a copy of summary.md at tools/e2e/client/out/run-all/summary.md (the latest run).
  The screenshots stay where run.ps1 writes them (out/<scenario>[-<mode>][-slot<N>]/): open them.

  Exit code 0 = every run passed, 1 = a run failed, 2 = no failure but a run stayed NOT RUN.

.EXAMPLE
  pwsh -NoProfile -File tools/e2e/client/run-all.ps1                                  # every unit, local
  pwsh -NoProfile -File tools/e2e/client/run-all.ps1 -RemoteVanilla                   # + remote-vanilla
  pwsh -NoProfile -File tools/e2e/client/run-all.ps1 -Scenario smoke,ui,progression -Build
#>
param(
    [string[]]$Scenario = @('*'),
    [string[]]$Exclude = @(),
    [switch]$Variants,                 # include the dotted extra scenarios (biomes.classic, ...)
    [string[]]$Mode = @('local'),      # local, remote-mod, remote-vanilla
    [switch]$RemoteVanilla,            # shorthand: add -Mode remote-vanilla
    [switch]$Build,                    # build first (client; server too for remote modes), once
    [int]$MaxAttempts = 3,             # attempts per run while it exits 2 (NOT RUN)
    [int]$RetryDelaySec = 60,
    [string[]]$RunArgs = @(),          # extra arguments for every run.ps1 call (e.g. -StrictContentLog)
    [switch]$List                      # print the planned runs and exit
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$runPs1 = Join-Path $PSScriptRoot 'run.ps1'
$outRoot = Join-Path $PSScriptRoot 'out'
# `pwsh -File` passes "a,b" as one string: split the lists here
function Split-List([string[]]$v) { @($v | ForEach-Object { $_ -split '[,;\s]+' } | ForEach-Object { $_.Trim("'`"") } | Where-Object { $_ }) }
$Scenario = Split-List $Scenario
$Exclude = Split-List $Exclude
$Mode = Split-List $Mode
foreach ($m in $Mode) { if ($m -notin 'local', 'remote-mod', 'remote-vanilla') { throw "unknown -Mode '$m' (local, remote-mod, remote-vanilla)" } }
if ($RemoteVanilla -and $Mode -notcontains 'remote-vanilla') { $Mode = @($Mode) + 'remote-vanilla' }

# ---- the scenarios ------------------------------------------------------------------------------
$hooks = '\.(prelaunch|restore|host-prelaunch)$'
$all = @(Get-ChildItem (Join-Path $PSScriptRoot 'scenarios') -Filter *.ps1 -File |
    ForEach-Object { $_.BaseName } | Where-Object { $_ -notmatch $hooks } | Sort-Object)
if (-not $Variants) { $all = @($all | Where-Object { $_ -notmatch '\.' }) }
$names = @($all | Where-Object { $n = $_; ($Scenario | Where-Object { $n -like $_ }) -and -not ($Exclude | Where-Object { $n -like $_ }) })
# a named scenario that is a variant counts even without -Variants
foreach ($s in $Scenario) {
    if ($s -notmatch '[*?\[]' -and $names -notcontains $s -and -not ($Exclude | Where-Object { $s -like $_ })) {
        if ($s -notmatch $hooks -and (Test-Path (Join-Path $PSScriptRoot "scenarios/$s.ps1"))) { $names += $s } else { throw "Unknown client scenario '$s'" }
    }
}
if ($names.Count -eq 0) { throw "no client scenario matches $($Scenario -join ', ')" }
if ($List) {
    foreach ($n in $names) { foreach ($m in $Mode) { "$n $m" } }
    exit 0
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $outRoot "run-all/$stamp"
New-Item -ItemType Directory -Force $runDir | Out-Null
Write-Host "[run-all] $($names.Count) scenario(s) x $($Mode -join ', '): $($names -join ', ')"
Write-Host "[run-all] logs and summary: $runDir"

if ($Build) {
    $target = if ($Mode | Where-Object { $_ -ne 'local' }) { 'all' } else { 'client' }
    & pwsh -NoProfile -File (Join-Path $root 'tools/build.ps1') -Target $target
    if ($LASTEXITCODE -eq 2) { Write-Host '[run-all] NOT RUN: no build slot in time' -ForegroundColor Yellow; exit 2 }
    if ($LASTEXITCODE) { Write-Host '[run-all] build failed' -ForegroundColor Red; exit 1 }
}

# ---- one run ------------------------------------------------------------------------------------
function Invoke-One([string]$name, [string]$mode) {
    $label = if ($mode -eq 'local') { $name } else { "$name-$mode" }
    $result = [ordered]@{ Scenario = $name; Mode = $mode; Result = 'NOT RUN'; Attempts = 0; Seconds = 0
        Selftests = ''; Warnings = 0; Shots = ''; ShotCount = 0; Failure = ''; Log = '' }
    $started = Get-Date
    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        $result.Attempts = $attempt
        $log = Join-Path $runDir "$label-$attempt.log"
        $result.Log = $log
        $t0 = Get-Date
        Write-Host "[run-all] $label (attempt $attempt/$MaxAttempts) ..."
        & pwsh -NoProfile -File $runPs1 -Scenario $name -Mode $mode @RunArgs *> $log
        $code = $LASTEXITCODE
        $text = @(Get-Content $log -ErrorAction SilentlyContinue)
        # the run's own output folder: "[e2e] output: <dir>" / "PASSED (screenshots: <dir>)"
        $dir = $text | ForEach-Object {
            if ($_ -match '\[e2e\] output: (.+)$') { $Matches[1].Trim() }
            elseif ($_ -match 'PASSED \(screenshots: (.+)\)$') { $Matches[1].Trim() } } | Select-Object -Last 1
        if (-not $dir) {
            # out/<label>[-slot<N>] written during this attempt
            $dir = Get-ChildItem $outRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match "^$([regex]::Escape($label))(-slot\d+)?$" -and $_.LastWriteTime -ge $t0 } |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
        }
        if ($dir -and (Test-Path $dir)) {
            $result.Shots = [IO.Path]::GetRelativePath($root, $dir).Replace('\', '/')
            $result.ShotCount = @(Get-ChildItem $dir -Filter *.png -File -Recurse -ErrorAction SilentlyContinue).Count
        }
        # the run's selftest.json (one entry per Invoke-SelfTest call), else the harness output
        $result.Selftests = ''
        $stFile = if ($dir) { Join-Path $dir 'selftest.json' }
        if ($stFile -and (Test-Path $stFile)) {
            try {
                $st = @(Get-Content $stFile -Raw | ConvertFrom-Json)
                $p = ($st | Measure-Object -Property Pass -Sum).Sum; $fl = ($st | Measure-Object -Property Fail -Sum).Sum
                $result.Selftests = "$p pass / $fl fail"
            } catch {}
        }
        if (-not $result.Selftests) {
            $done = $text | Select-String -Pattern 'SELFTEST DONE pass=(\d+) fail=(\d+)' | Select-Object -Last 1
            if ($done) { $result.Selftests = "$($done.Matches[0].Groups[1].Value) pass / $($done.Matches[0].Groups[2].Value) fail" }
        }
        $result.Warnings = @($text | Select-String -Pattern '\[e2e\] content-log warning:').Count
        $fail = $text | Select-String -Pattern '^\[e2e\]\s+-\s+(.+)$' | Select-Object -First 1
        $result.Failure = if ($fail) { $fail.Matches[0].Groups[1].Value.Trim() } else { '' }
        if ($code -eq 0) { $result.Result = 'PASS'; break }
        if ($code -ne 2) { $result.Result = 'FAIL'; if (-not $result.Failure) { $result.Failure = "exit code $code" }; break }
        $why = $text | Select-String -Pattern 'NOT RUN|WATCHDOG' | Select-Object -Last 1
        $result.Failure = if ($why) { $why.Line.Trim() } else { 'NOT RUN (exit 2)' }
        Write-Host "[run-all] $label NOT RUN (attempt $attempt): $($result.Failure)" -ForegroundColor Yellow
        if ($attempt -lt $MaxAttempts) { Start-Sleep -Seconds $RetryDelaySec }
    }
    $result.Seconds = [int]((Get-Date) - $started).TotalSeconds
    $color = @{ PASS = 'Green'; FAIL = 'Red'; 'NOT RUN' = 'Yellow' }[$result.Result]
    Write-Host "[run-all] $label $($result.Result) in $($result.Seconds) s ($($result.ShotCount) screenshot(s) in $($result.Shots))" -ForegroundColor $color
    return [pscustomobject]$result
}

function Write-Summary($rows, [bool]$final) {
    $md = [Collections.Generic.List[string]]::new()
    $pass = @($rows | Where-Object Result -eq 'PASS').Count
    $fail = @($rows | Where-Object Result -eq 'FAIL').Count
    $nr = @($rows | Where-Object Result -eq 'NOT RUN').Count
    $md.Add("# Client acceptance run $stamp")
    $md.Add('')
    $state = if ($final) { 'finished' } else { 'in progress' }
    $md.Add("Run $state $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'); commit $((git -C $root rev-parse --short HEAD 2>$null)); modes: $($Mode -join ', ').")
    $md.Add('')
    $md.Add("**$pass pass, $fail fail, $nr not run** of $($rows.Count) run(s) ($($names.Count * $Mode.Count) planned).")
    $md.Add('')
    $md.Add('| Scenario | Mode | Result | Attempts | Time | Selftests | Content-log warnings | Screenshots | First failure |')
    $md.Add('|---|---|---|---|---|---|---|---|---|')
    foreach ($r in $rows) {
        $shots = if ($r.Shots) { "``$($r.Shots)`` ($($r.ShotCount))" } else { '-' }
        $time = '{0}:{1:00}' -f [int][math]::Floor($r.Seconds / 60), ($r.Seconds % 60)
        $failure = ($r.Failure -replace '\|', '\|') -replace '\s+', ' '
        if ($failure.Length -gt 200) { $failure = $failure.Substring(0, 200) + '...' }
        $md.Add("| $($r.Scenario) | $($r.Mode) | $($r.Result) | $($r.Attempts) | $time | $($r.Selftests) | $($r.Warnings) | $shots | $failure |")
    }
    $md.Add('')
    $md.Add("Logs: ``$([IO.Path]::GetRelativePath($root, $runDir).Replace('\', '/'))/<scenario>[-<mode>]-<attempt>.log``. Open every screenshot and compare it with upstream's textures and GUIs.")
    $text = $md -join "`n"
    Set-Content -Path (Join-Path $runDir 'summary.md') -Value $text -Encoding UTF8
    Set-Content -Path (Join-Path $outRoot 'run-all/summary.md') -Value $text -Encoding UTF8
}

$rows = [Collections.Generic.List[object]]::new()
foreach ($name in $names) {
    foreach ($m in $Mode) {
        $rows.Add((Invoke-One $name $m))
        Write-Summary $rows $false
    }
}
Write-Summary $rows $true
$summary = Join-Path $runDir 'summary.md'
Write-Host "[run-all] summary: $summary"
$failed = @($rows | Where-Object Result -eq 'FAIL')
$notRun = @($rows | Where-Object Result -eq 'NOT RUN')
foreach ($r in $failed) { Write-Host "[run-all]   FAIL $($r.Scenario) ($($r.Mode)): $($r.Failure)" -ForegroundColor Red }
foreach ($r in $notRun) { Write-Host "[run-all]   NOT RUN $($r.Scenario) ($($r.Mode)): $($r.Failure)" -ForegroundColor Yellow }
if ($failed) { exit 1 }
if ($notRun) { exit 2 }
Write-Host "[run-all] all $($rows.Count) run(s) passed" -ForegroundColor Green
exit 0

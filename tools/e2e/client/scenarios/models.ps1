# Client scenario 'models' (unit F2): every converted model on a test actor (BeDD, local world).
# The pack holds every partition of assets/gen/geo; models.prelaunch.ps1 added the preview packs,
# turned on the game's content log file and started the game with the HUD shown. This runs the
# models.* selftests, then hides the HUD with F1 and shows the models four at a time in front of a
# fixed camera (out/models/shot_NN.png; preview-shots.json says what each shot shows; shot_01 starts
# with the x-sign check: the blue cube must sit on the red one), then shows the HUD again with F1
# (out/models/hud_back.png: the hotbar is back). Every F1 press is checked with the client-only
# selftest models.client_hud (the game's own hide-GUI option) and repeated when the game missed it.
# Open every screenshot and compare the models with upstream's textures. Last, the client's content log:
# the two canaries of the preview pack must be reported (the log is captured), and any other error or
# warning (geometry, animation Molang, attachables, render controllers, client entities, items, JSON)
# fails the run; out/models/content-log.txt keeps every entry. The harness puts the instance's own options
# back (run.ps1 Restore-RunOptions). Dot-sourced by tools/e2e/client/run.ps1.
$gfOut = Join-Path $script:OutRoot $script:Unit
$planFile = Join-Path $gfOut 'preview-shots.json'
if (-not (Test-Path $planFile)) { throw "no preview plan: $planFile (models.prelaunch.ps1 did not run)" }
$plan = Get-Content $planFile -Raw | ConvertFrom-Json

# The client's hide-GUI options (F1) as the game holds them, from the client-only selftest
# models.client_hud (src/feature/ext/models/ModelsClientHud.cpp): @{ Hud; Hand } = 'hidden' or
# 'shown', or $null when the line is missing. -Since: read the line of a run that already happened.
function Get-GfHudState([int]$Since = -1) {
    $mark = $Since
    if ($Since -lt 0) {
        $mark = Get-LogMark
        $r = Invoke-SelfTest 'models.client_hud' -TimeoutSeconds 60
        if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw "the HUD probe models.client_hud did not pass (pass=$($r.Pass) fail=$($r.Fail))" }
    }
    $hit = Get-LeviLog -Since $mark | Select-String -Pattern 'SELFTEST INFO models\.client_hud: hud (hidden|shown), hand (hidden|shown)' | Select-Object -Last 1
    if (-not $hit) { return $null }
    [pscustomobject]@{ Hud = $hit.Matches[0].Groups[1].Value; Hand = $hit.Matches[0].Groups[2].Value }
}

# Presses F1 until the game's HUD and hand are both $Want ('hidden' or 'shown'), asking the game after
# every press: a press the game missed (it arrived while a screen was closing, say) is repeated, and so
# is one that left the hand out of step with the HUD (F1 toggles both). Throws when they are not both
# $Want after $Presses presses, or the state cannot be read.
function Set-GfHud([ValidateSet('hidden', 'shown')][string]$Want, [int]$Presses = 4) {
    $state = Get-GfHudState
    for ($i = 1; -not $state -and $i -le 2; $i++) { Start-Sleep -Seconds 1; $state = Get-GfHudState }
    if (-not $state) { throw "cannot read the HUD state (no 'SELFTEST INFO models.client_hud: hud ...' line)" }
    $pressed = 0
    while ($state.Hud -ne $Want -or $state.Hand -ne $Want) {
        if ($pressed -ge $Presses) {
            $why = if ($state.Hud -eq $Want) { 'F1 does not bring the hand in step with the HUD' } else { 'the game misses F1' }
            throw "the HUD is $($state.Hud) and the hand $($state.Hand) after $pressed F1 press(es) (wanted: both $Want; $why)"
        }
        # F1 on the HUD itself, not on a screen that is still closing (the chat of the last command)
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-ClientScreen) -ne 'hud_screen' -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
        Start-Sleep -Milliseconds 500
        Press-Key F1
        $pressed++
        Start-Sleep -Milliseconds 800
        $state = Get-GfHudState
        if (-not $state) { throw "cannot read the HUD state after F1 press $pressed (no 'SELFTEST INFO models.client_hud: hud ...' line)" }
        Write-Step "F1 press ${pressed}: hud $($state.Hud), hand $($state.Hand)"
    }
    Write-Step "HUD $Want (hand $($state.Hand)) after $pressed F1 press(es)"
}

Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$gfMark = Get-LogMark
$r = Invoke-SelfTest 'models.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) models selftest(s) failed" }
# the game started with the HUD shown (the harness sets gfx_hidehud:0 / gfx_hidehand:0, lib.ps1 Reset-GameOptions)
$gfHud = Get-GfHudState -Since $gfMark
if (-not $gfHud) { throw "no 'SELFTEST INFO models.client_hud: hud ...' line in the models.* run" }
Write-Step "HUD at the start: hud $($gfHud.Hud), hand $($gfHud.Hand)"
if ($gfHud.Hud -ne 'shown') { Write-Step "warning: the game started with the HUD hidden although options.txt said gfx_hidehud:0" }
foreach ($what in 'geometries', 'base animations', 'attachables') {
    $hit = Get-LeviLog | Select-String -Pattern "SELFTEST INFO models\.\w+: $what present (\d+)/(\d+)" | Select-Object -Last 1
    if (-not $hit) { throw "no '$what present' line from the models selftests" }
    $g = $hit.Matches[0].Groups
    if ($g[1].Value -ne $g[2].Value) { throw "not every converted file is in the pack: $($hit.Line)" }
}

# no command feedback in the chat (F1 hides the HUD, not the chat), then let the chat fade
Invoke-Chat '/gamerule sendcommandfeedback false'
Invoke-Chat '/gamerule commandblockoutput false'
Invoke-Chat '/gamerule showcoordinates false'   # no 'Position: x, y, z' overlay in the shots
Invoke-Chat '/gamemode spectator'
Invoke-Chat '/time set noon'
Invoke-Chat '/weather clear'
Invoke-Teleport "/tp @s $($plan.camera.x) $($plan.camera.y) $($plan.camera.z) facing 0 -59.2 0"
Set-GfHud hidden   # no HUD in the screenshots, whatever the HUD was before
Start-Sleep -Seconds 12   # the chat lines of the probe fade
foreach ($s in $plan.shots) {
    Invoke-Chat "/function gfpreview/$($s.name)"
    # the commands naming preview items (kept out of the functions, see preview.ps1)
    foreach ($c in @($s.commands)) { if ($c) { Invoke-Chat "/$c" } }
    Start-Sleep -Milliseconds 1800
    Save-Screenshot $s.name | Out-Null
    Write-Step "$($s.name): $($s.label)"
}
Invoke-Chat '/function gfpreview/clear'
# the HUD again (the game keeps F1's choice in options.txt for later runs; the harness's
# Restore-RunOptions puts the instance's own values back as well), and a picture of it: the hotbar is shown in creative mode
Set-GfHud shown
Invoke-Chat '/gamemode creative'
Start-Sleep -Seconds 2
Save-Screenshot 'hud_back' | Out-Null
# a function with one bad command is dropped by the game: every shot must have run
$missing = @(Get-LeviLog | Select-String -Pattern 'functionNameNotFound \[gfpreview/[^\]]+\]|Syntax error.*gfpreview' | ForEach-Object { $_.Matches[0].Value } | Sort-Object -Unique)
if ($missing) { throw "preview commands failed: $($missing -join ', ')" }

# ---- the client's content log -------------------------------------------------------------------
# The game buffers the file and writes it out when it closes: close it first (run.ps1's own Stop-Game
# then finds nothing to stop; its log checks read LeviLamina's log, which stays on disk).
Stop-Game
Start-Sleep -Seconds 2
$gfState = Get-Content (Join-Path $gfOut 'content-log.json') -Raw | ConvertFrom-Json
$gfSince = [datetime]::Parse($gfState.since)
$gfLogs = @(Get-ChildItem $gfState.logs -Filter 'ContentLog*.txt' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $gfSince } | Sort-Object Name)
if (-not $gfLogs) { throw "the game wrote no content log in $($gfState.logs) (options.txt content_log_file did not take effect)" }
$gfEntries = [System.Collections.Generic.List[object]]::new()
foreach ($f in $gfLogs) {
    $fs = [System.IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
    try { $text = (New-Object System.IO.StreamReader($fs, [Text.Encoding]::UTF8)).ReadToEnd() } finally { $fs.Dispose() }
    $current = $null
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match '^\d\d:\d\d:\d\d\[([^\]]*)\]\[(\w+)\]-(.*)$') {
            $current = [pscustomobject]@{ Category = $Matches[1]; Level = $Matches[2].ToLowerInvariant(); Text = $Matches[3]; More = [System.Text.StringBuilder]::new() }
            $gfEntries.Add($current)
        } elseif ($current -and $line.Trim()) { [void]$current.More.AppendLine($line) }
    }
}
$gfEntries | ForEach-Object { "[$($_.Category)][$($_.Level)] $($_.Text)$(if ($_.More.Length) { "`n$($_.More.ToString().TrimEnd())" })" } |
    Set-Content -Path (Join-Path $gfOut 'content-log.txt') -Encoding UTF8
$gfSummary = $gfEntries | Group-Object Category, Level | Sort-Object Name | ForEach-Object { "$($_.Name) x$($_.Count)" }
Write-Step "content log: $($gfEntries.Count) entries in $($gfLogs.Count) file(s): $($gfSummary -join '; ')"
# Known lines that are not problems: the harness's list (lib.ps1 $script:ContentLogExpected: its own
# canary, these canaries, the game's cached diving-helmet fog definitions, ...).
$gfNoise = (@($script:ContentLogExpected) | Where-Object { $_ }) -join '|'
$gfBad = @($gfEntries | Where-Object { $_.Level -in 'error', 'warning' })
$gfCanary = @($gfBad | Where-Object { "$($_.Text) $($_.More)" -match 'gfpreview_canary' })
$gfProblems = @($gfBad | Where-Object { "$($_.Text) $($_.More)" -notmatch 'gfpreview_canary' -and -not ($gfNoise -and "$($_.Text) $($_.More)" -match $gfNoise) })
foreach ($c in $gfCanary) { Write-Step "content log canary: [$($c.Category)][$($c.Level)] $($c.Text)" }
foreach ($want in @(@{ re = 'gfpreview_canary\.geo\.json'; what = 'geometry (models/entity/gfpreview_canary.geo.json)' }, @{ re = 'gfpreview_canary\.animation\.json'; what = 'animation Molang (animations/gfpreview_canary.animation.json)' })) {
    if (-not ($gfCanary | Where-Object { "$($_.Text) $($_.More)" -match $want.re })) { throw "the content log does not report the broken canary $($want.what): the check cannot see those errors" }
}
if ($gfProblems) {
    $gfProblems | Select-Object -First 30 | ForEach-Object { Write-Host "  ! [$($_.Category)][$($_.Level)] $($_.Text)" -ForegroundColor Red }
    throw "$($gfProblems.Count) content-log error(s) / warning(s) (see $(Join-Path $gfOut 'content-log.txt'))"
}
Write-Step "content log: no error or warning about the packs (canaries reported: $($gfCanary.Count))"

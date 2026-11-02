# Client scenario 'models' (unit F2): every converted model on a test actor (BeDD, local world).
# The pack holds every partition of assets/gen/geo; models.prelaunch.ps1 added the preview packs and
# turned on the game's content log file. This runs the models.* selftests, then shows the models four
# at a time in front of a fixed camera (out/models/shot_NN.png; preview-shots.json says what each
# shot shows; shot_01 starts with the x-sign check: the blue cube must sit on the red one). Open every
# screenshot and compare the models with TF's textures. Last, the client's content log: the two
# canaries of the preview pack must be reported (the log is captured), and any other error or
# warning (geometry, animation Molang, attachables, render controllers, client entities, items, JSON)
# fails the run; out/models/content-log.txt keeps every entry. Dot-sourced by tools/e2e/client/run.ps1.
$gfOut = Join-Path $script:OutRoot $script:Unit
$planFile = Join-Path $gfOut 'preview-shots.json'
if (-not (Test-Path $planFile)) { throw "no preview plan: $planFile (models.prelaunch.ps1 did not run)" }
$plan = Get-Content $planFile -Raw | ConvertFrom-Json

Wait-LogLine 'GloamingForest world ready' 120 | Out-Null
$r = Invoke-SelfTest 'models.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) models selftest(s) failed" }
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
Press-Key F1   # hide the HUD for the screenshots
Start-Sleep -Seconds 12
foreach ($s in $plan.shots) {
    Invoke-Chat "/function gfpreview/$($s.name)"
    # the commands naming preview items (kept out of the functions, see preview.ps1)
    foreach ($c in @($s.commands)) { if ($c) { Invoke-Chat "/$c" } }
    Start-Sleep -Milliseconds 1800
    Save-Screenshot $s.name | Out-Null
    Write-Step "$($s.name): $($s.label)"
}
Invoke-Chat '/function gfpreview/clear'
Press-Key F1
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
# Known lines that are not about this mod's packs: the game's own cached diving-helmet fog
# definitions (a vanilla pack in the packcache that fails the fog schema on every start).
$gfNoise = 'fogs/(netherite_)?diving_helmet\.json'
$gfBad = @($gfEntries | Where-Object { $_.Level -in 'error', 'warning' })
$gfCanary = @($gfBad | Where-Object { "$($_.Text) $($_.More)" -match 'gfpreview_canary' })
$gfProblems = @($gfBad | Where-Object { "$($_.Text) $($_.More)" -notmatch 'gfpreview_canary' -and $_.Text -notmatch $gfNoise })
foreach ($c in $gfCanary) { Write-Step "content log canary: [$($c.Category)][$($c.Level)] $($c.Text)" }
foreach ($want in @(@{ re = 'gfpreview_canary\.geo\.json'; what = 'geometry (models/entity/gfpreview_canary.geo.json)' }, @{ re = 'gfpreview_canary\.animation\.json'; what = 'animation Molang (animations/gfpreview_canary.animation.json)' })) {
    if (-not ($gfCanary | Where-Object { "$($_.Text) $($_.More)" -match $want.re })) { throw "the content log does not report the broken canary $($want.what): the check cannot see those errors" }
}
if ($gfProblems) {
    $gfProblems | Select-Object -First 30 | ForEach-Object { Write-Host "  ! [$($_.Category)][$($_.Level)] $($_.Text)" -ForegroundColor Red }
    throw "$($gfProblems.Count) content-log error(s) / warning(s) (see $(Join-Path $gfOut 'content-log.txt'))"
}
Write-Step "content log: no error or warning about the packs (canaries reported: $($gfCanary.Count))"

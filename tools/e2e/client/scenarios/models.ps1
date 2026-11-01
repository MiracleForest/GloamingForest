# Client scenario 'models' (unit F2): every converted model on a test actor (BeDD, local world).
# The pack holds every partition of assets/gen/geo; models.prelaunch.ps1 added the preview packs.
# This runs the models.* selftests, then shows the models four at a time in front of a fixed camera
# (out/models/shot_NN.png; preview-shots.json says what each shot shows). Open every screenshot
# and compare the models with TF's textures. The run fails on any content-log error (geometry,
# animation Molang, attachables, entity / item JSON). Dot-sourced by tools/e2e/client/run.ps1.
$planFile = Join-Path (Join-Path $script:OutRoot $script:Unit) 'preview-shots.json'
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

Invoke-Chat '/gamemode spectator'
Invoke-Chat '/time set noon'
Invoke-Chat '/weather clear'
Invoke-Teleport "/tp @s $($plan.camera.x) $($plan.camera.y) $($plan.camera.z) facing 0 -59.2 0"
Press-Key F1   # hide the HUD (and the chat feedback) for the screenshots
Start-Sleep -Seconds 2
foreach ($s in $plan.shots) {
    Invoke-Chat "/function gfpreview/$($s.name)"
    Start-Sleep -Milliseconds 1800
    Save-Screenshot $s.name | Out-Null
    Write-Step "$($s.name): $($s.label)"
}
Invoke-Chat '/function gfpreview/clear'
Press-Key F1
# a function with one bad command is dropped by the game: every shot must have run
$missing = @(Get-LeviLog | Select-String -Pattern 'functionNameNotFound \[gfpreview/[^\]]+\]' | ForEach-Object { $_.Matches[0].Value } | Sort-Object -Unique)
if ($missing) { throw "preview functions failed to load: $($missing -join ', ')" }

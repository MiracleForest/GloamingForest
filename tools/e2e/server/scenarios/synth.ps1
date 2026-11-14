# Synth (F3): the assembled resource pack defines every upstream sound event as upstream's inventory says
# (category, subtitle, variants with upstream's volume, pitch and stream) and every synthesized file is a
# valid mono Ogg Vorbis stream (synth.definitions); synth.play_all plays every event to the real
# players of the server, and says it is skipped when there are none (a BDS without clients: the
# client scenario's remote modes run it with one); the BDS content log stays clean (run.ps1 fails on
# content errors); the renderer reproduces the committed tree byte for byte and every definition
# matches upstream's inventory (tools/sounds/synth.ps1 -Check), and the baseline and the inventory match
# upstream's sounds.json (tools/sounds/baseline/generate.ps1 -Check, when the upstream checkout is present).
Wait-LogLine 'GloamingForest world ready' 120 | Out-Null

$r = Invoke-SelfTest 'synth.*' -TimeoutSeconds 300
if ($r.Fail -gt 0) { throw "$($r.Fail) synth selftest(s) failed" }
if ($r.Pass -lt 2) { throw "expected 2 synth selftests to pass, got $($r.Pass)" }
$line = Wait-LogLine 'synth: (played \d+ sound events|play_all skipped)' 30
if ($line -match 'skipped') { Write-Step 'synth.play_all SKIPPED: no real player on this BDS (tools/e2e/client/run.ps1 -Scenario synth -Mode remote-vanilla plays every event to one)' }

$root = (Resolve-Path "$PSScriptRoot/../../../..").Path
Write-Step 'tools/sounds/synth.ps1 -Check'
& pwsh -NoProfile -File (Join-Path $root 'tools/sounds/synth.ps1') -Check
if ($LASTEXITCODE -ne 0) { throw 'tools/sounds/synth.ps1 -Check failed: the rendered audio is not deterministic, not committed or not upstream''s' }
$tf = if ($env:GF_UPSTREAM_HOME) { $env:GF_UPSTREAM_HOME } else { Join-Path $env:LOCALAPPDATA 'gf-upstream/view' }
if (Test-Path (Join-Path $tf 'src/generated/resources/assets/gloamingforest/sounds.json')) {
    Write-Step 'tools/sounds/baseline/generate.ps1 -Check'
    & pwsh -NoProfile -File (Join-Path $root 'tools/sounds/baseline/generate.ps1') -Check
    if ($LASTEXITCODE -ne 0) { throw 'tools/sounds/baseline/generate.ps1 -Check failed: the baseline or the inventory differs from upstream''s sounds.json' }
} else {
    Write-Step 'tools/sounds/baseline/generate.ps1 -Check skipped (no upstream checkout: pwsh tools/upstream/fetch.ps1)'
}

# The sound lint on the mod this run deployed ($ModDir of tools/e2e/server/run.ps1) (tools/sounds/lint.ps1): every upstream event in sound_definitions.json with its
# files, the 9 composed tracks, and the refined / vanilla / baseline count of every owner (reported).
$lintLog = Join-Path $script:OutDir 'sound-lint.log'
& pwsh -NoProfile -File (Join-Path $root 'tools/sounds/lint.ps1') -Out $ModDir -Json (Join-Path $script:OutDir 'sound-lint.json') *> $lintLog
$lintExit = $LASTEXITCODE
Get-Content $lintLog | ForEach-Object { Write-Step "  $_" }
if ($lintExit -ne 0) { throw "tools/sounds/lint.ps1 failed (exit $lintExit; see $lintLog)" }

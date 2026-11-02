# Server scenario 'models' (unit F2): the converter's own tests (tools/models/test.ps1: lexer, parser,
# interpreter, conversion, setupAnim -> Molang, item display transforms, the committed tree and
# convert.ps1 -Check), then every converted geometry, base animation and attachable
# (assets/gen/geo/**, tools/models/convert.ps1) is in the deployed pack, well-formed and matching the
# converter's index (models.* selftests). BDS loads behavior packs only, so the resource-pack content
# log (geometry, animations, attachables, render controllers) is checked by the client scenario
# 'models'. Dot-sourced by tools/e2e/server/run.ps1.
$gfTestLog = Join-Path $script:OutDir 'models-test.log'
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/models/test.ps1') *> $gfTestLog
$gfTestExit = $LASTEXITCODE
$gfTestSummary = Get-Content $gfTestLog | Select-String -Pattern '\[geo-test\] \d+ passed' | Select-Object -Last 1
Get-Content $gfTestLog | Select-String -Pattern '^\s+(FAIL|- )' | ForEach-Object { Write-Host "  ! $($_.Line.Trim())" -ForegroundColor Red }
if ($gfTestExit -ne 0 -or -not $gfTestSummary) { throw "tools/models/test.ps1 failed (exit $gfTestExit; see $gfTestLog)" }
Write-Step "models: converter tests: $($gfTestSummary.Line.Trim())"

$r = Invoke-SelfTest 'models.*'
if ($r.Fail -gt 0) { throw "$($r.Fail) models selftest(s) failed" }
if ($r.Pass -lt 4) { throw "expected 4 models selftests, got $($r.Pass)" }
$log = Get-ServerLog
foreach ($what in 'geometries', 'base animations', 'attachables') {
    $hit = $log | Select-String -Pattern "SELFTEST INFO models\.\w+: $what present (\d+)/(\d+)" | Select-Object -Last 1
    if (-not $hit) { throw "no '$what present' line from the models selftests" }
    $g = $hit.Matches[0].Groups
    if ($g[1].Value -ne $g[2].Value -or [int]$g[2].Value -lt 1) { throw "not every converted file is in the pack: $($hit.Line)" }
    Write-Step "models: $what present $($g[1].Value)/$($g[2].Value)"
}
